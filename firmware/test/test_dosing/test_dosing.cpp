#include <unity.h>

#include "control.h"

namespace {

demeter::Readings healthy() {
    demeter::Readings r;
    r.ph = 6.4f;
    r.phValid = true;
    r.phReason = "ok";
    r.phVolts = 1.6f;
    r.ecMs = 1.4f;
    r.ecUs = 1400.f;
    r.tdsPpm = 700.f;
    r.ecValid = true;
    r.ecReason = "ok";
    r.tdsVolts = 1.2f;
    r.waterTempC = 22.f;
    r.tempValid = true;
    r.flowLpm = 4.f;
    r.flowValid = true;
    r.waterLow = false;
    return r;
}

demeter::Inputs ready(uint32_t now, const demeter::Readings& readings) {
    demeter::Inputs in;
    in.nowMs = now;
    in.timeSynced = true;
    in.unixTime = 1700000000;
    in.localMinute = 8 * 60;
    in.localYyyymmdd = 20231114;
    in.sensors = readings;
    in.circulationConfirmed = true;
    in.lightsConfirmed = true;
    in.shellyPumpReachable = true;
    in.shellyLightsReachable = true;
    in.shellyPumpConfigured = true;
    in.shellyLightsConfigured = true;
    return in;
}

void calibrate(demeter::Controller& c) {
    c.limits.startupHoldMs = 0;
    c.limits.useFactoryTds = true;
    TEST_ASSERT_NULL(c.capturePhPoint(7.0f, 1.5f, 25.0f));
    TEST_ASSERT_NULL(c.capturePhPoint(4.0f, 2.0f, 25.0f));
    for (int i = 0; i < demeter::kPumpCount; ++i) {
        TEST_ASSERT_NULL(c.setPumpCal(static_cast<demeter::PumpId>(i), 1.0f, false));
    }
}

}  // namespace

void setUp() {}
void tearDown() {}

void test_ec_is_corrected_before_ph() {
    demeter::Controller c;
    calibrate(c);
    demeter::Readings r = healthy();
    r.ecMs = 1.0f;
    r.ph = 7.4f;
    c.tick(ready(1000, r));
    TEST_ASSERT_TRUE(c.active.running);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(demeter::PumpId::NutrientA), static_cast<int>(c.active.pump));
}

void test_nutrients_alternate_after_mix() {
    demeter::Controller c;
    calibrate(c);
    c.limits.mixLockoutMs = 1000;
    demeter::Readings r = healthy();
    r.ecMs = 1.0f;
    c.tick(ready(1000, r));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(demeter::PumpId::NutrientA), static_cast<int>(c.active.pump));
    const uint32_t stop = c.active.stopAtMs;
    c.tick(ready(stop, r));
    TEST_ASSERT_FALSE(c.active.running);
    c.tick(ready(stop + 999, r));
    TEST_ASSERT_FALSE(c.active.running);
    TEST_ASSERT_EQUAL_STRING("mixing", c.lastBlock);
    c.tick(ready(stop + 1000, r));
    TEST_ASSERT_TRUE(c.active.running);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(demeter::PumpId::NutrientB), static_cast<int>(c.active.pump));
}

void test_low_water_blocks_dosing_and_circulation() {
    demeter::Controller c;
    calibrate(c);
    demeter::Readings r = healthy();
    r.waterLow = true;
    r.ecMs = 0.4f;
    c.tick(ready(1000, r));
    TEST_ASSERT_FALSE(c.active.running);
    TEST_ASSERT_FALSE(c.outputs.circulation);
    TEST_ASSERT_EQUAL_STRING("low_water", c.lastBlock);
    TEST_ASSERT_TRUE(c.faults.lowWater);
}

void test_daily_limit_blocks_nutrient_pumps() {
    demeter::Controller c;
    calibrate(c);
    c.pumps[static_cast<int>(demeter::PumpId::NutrientA)].dosedTodayMl = 50.f;
    c.pumps[static_cast<int>(demeter::PumpId::NutrientB)].dosedTodayMl = 50.f;
    demeter::Readings r = healthy();
    r.ecMs = 1.0f;
    c.tick(ready(1000, r));
    TEST_ASSERT_FALSE(c.active.running);
    TEST_ASSERT_EQUAL_STRING("daily_limit", c.lastBlock);
    TEST_ASSERT_TRUE(c.faults.dailyLimit);
}

void test_high_ec_does_not_dose_nutrients_but_fixes_ph() {
    demeter::Controller c;
    calibrate(c);
    demeter::Readings r = healthy();
    r.ecMs = 1.95f;
    r.ph = 7.6f;
    c.tick(ready(1000, r));
    TEST_ASSERT_TRUE(c.active.running);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(demeter::PumpId::PhDown), static_cast<int>(c.active.pump));
    TEST_ASSERT_TRUE(c.faults.ecHigh);
}

void test_stale_ec_skips_dosing() {
    demeter::Controller c;
    calibrate(c);
    demeter::Readings r = healthy();
    r.ecValid = false;
    r.ecReason = "stale";
    r.ph = 7.8f;
    c.tick(ready(1000, r));
    TEST_ASSERT_FALSE(c.active.running);
    TEST_ASSERT_EQUAL_STRING("ec_invalid", c.lastBlock);
}

void test_factory_tds_off_blocks_until_calibrated() {
    demeter::Controller c;
    calibrate(c);
    c.limits.useFactoryTds = false;
    demeter::Readings r = healthy();
    r.ecMs = 0.5f;
    c.tick(ready(1000, r));
    TEST_ASSERT_FALSE(c.active.running);
    TEST_ASSERT_EQUAL_STRING("tds_uncalibrated", c.lastBlock);
    TEST_ASSERT_NULL(c.captureTds(700.f, 1.0f, 25.f));
    c.tick(ready(2000, r));
    TEST_ASSERT_TRUE(c.active.running);
}

void test_service_manual_dose_and_interlock() {
    demeter::Controller c;
    calibrate(c);
    c.tick(ready(1000, healthy()));
    TEST_ASSERT_EQUAL_STRING("service_mode_required", c.manualDose(demeter::PumpId::PhUp, 1.0f, 1000));
    c.setMode(demeter::Mode::Service);
    demeter::Readings low = healthy();
    low.waterLow = true;
    c.tick(ready(1100, low));
    TEST_ASSERT_EQUAL_STRING("low_water", c.manualDose(demeter::PumpId::PhUp, 1.0f, 1100));
    TEST_ASSERT_EQUAL_STRING("low_water", c.manualCirculation(true));
    c.tick(ready(1200, healthy()));
    TEST_ASSERT_NULL(c.manualDose(demeter::PumpId::PhUp, 1.0f, 1200));
    TEST_ASSERT_TRUE(c.active.running);
    TEST_ASSERT_EQUAL_STRING("pump_busy", c.manualDose(demeter::PumpId::PhDown, 1.0f, 1200));
}

void test_light_schedule_and_unsynced_hold() {
    demeter::Controller c;
    calibrate(c);
    demeter::Inputs in = ready(1000, healthy());
    in.localMinute = 7 * 60;
    c.tick(in);
    TEST_ASSERT_TRUE(c.outputs.lights);
    TEST_ASSERT_TRUE(c.outputs.commandLights);
    in.timeSynced = false;
    in.nowMs = 2000;
    c.tick(in);
    TEST_ASSERT_FALSE(c.outputs.commandLights);
    TEST_ASSERT_TRUE(c.faults.timeUnsynced);
}

void test_no_flow_latches_in_auto() {
    demeter::Controller c;
    calibrate(c);
    c.limits.noFlowDelayMs = 1000;
    demeter::Readings r = healthy();
    r.flowLpm = 0.0f;
    c.tick(ready(1000, r));
    TEST_ASSERT_TRUE(c.outputs.circulation);
    demeter::Inputs later = ready(3000, r);
    c.tick(later);
    TEST_ASSERT_TRUE(c.faults.noFlow);
    TEST_ASSERT_FALSE(c.outputs.circulation);
    c.acknowledgeFaults();
    c.tick(ready(3500, r));
    TEST_ASSERT_FALSE(c.faults.noFlow);
    TEST_ASSERT_TRUE(c.outputs.circulation);
    c.tick(ready(4600, r));
    TEST_ASSERT_TRUE(c.faults.noFlow);
    TEST_ASSERT_FALSE(c.outputs.circulation);
}

void test_low_water_stops_an_active_dose() {
    demeter::Controller c;
    calibrate(c);
    c.setMode(demeter::Mode::Service);
    c.tick(ready(1000, healthy()));
    TEST_ASSERT_NULL(c.manualDose(demeter::PumpId::PhUp, 1.0f, 1000));
    TEST_ASSERT_TRUE(c.active.running);
    demeter::Readings low = healthy();
    low.waterLow = true;
    c.tick(ready(1100, low));
    TEST_ASSERT_FALSE(c.active.running);
    TEST_ASSERT_FALSE(c.outputs.circulation);
}

void test_estop_enters_service_and_stops_circulation() {
    demeter::Controller c;
    calibrate(c);
    c.tick(ready(1000, healthy()));
    TEST_ASSERT_TRUE(c.outputs.circulation);
    c.emergencyStop(1500);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(demeter::Mode::Service), static_cast<int>(c.mode));
    TEST_ASSERT_FALSE(c.outputs.circulation);
    TEST_ASSERT_FALSE(c.active.running);
}

void test_day_rollover_clears_totals() {
    demeter::Controller c;
    calibrate(c);
    demeter::Readings r = healthy();
    r.ecMs = 1.0f;
    demeter::Inputs in = ready(1000, r);
    c.tick(in);
    TEST_ASSERT_TRUE(c.pumps[2].dosedTodayMl > 0.f);
    in.nowMs = c.active.stopAtMs;
    in.localYyyymmdd = 20231115;
    c.tick(in);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.f, c.pumps[2].dosedTodayMl);
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_ec_is_corrected_before_ph);
    RUN_TEST(test_nutrients_alternate_after_mix);
    RUN_TEST(test_low_water_blocks_dosing_and_circulation);
    RUN_TEST(test_daily_limit_blocks_nutrient_pumps);
    RUN_TEST(test_high_ec_does_not_dose_nutrients_but_fixes_ph);
    RUN_TEST(test_stale_ec_skips_dosing);
    RUN_TEST(test_factory_tds_off_blocks_until_calibrated);
    RUN_TEST(test_service_manual_dose_and_interlock);
    RUN_TEST(test_light_schedule_and_unsynced_hold);
    RUN_TEST(test_no_flow_latches_in_auto);
    RUN_TEST(test_low_water_stops_an_active_dose);
    RUN_TEST(test_estop_enters_service_and_stops_circulation);
    RUN_TEST(test_day_rollover_clears_totals);
    return UNITY_END();
}
