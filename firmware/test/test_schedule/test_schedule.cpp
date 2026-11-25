#include <unity.h>

#include "calibration.h"
#include "presets.h"

void setUp() {}
void tearDown() {}

void test_lights_wrap_past_midnight() {
    demeter::Targets targets = demeter::basilPreset();
    targets.lightOnMinute = 18 * 60;
    targets.photoperiodHours = 16.f;
    TEST_ASSERT_TRUE(demeter::lightsShouldBeOn(18 * 60, targets));
    TEST_ASSERT_TRUE(demeter::lightsShouldBeOn(23 * 60, targets));
    TEST_ASSERT_TRUE(demeter::lightsShouldBeOn(9 * 60, targets));
    TEST_ASSERT_FALSE(demeter::lightsShouldBeOn(10 * 60, targets));
    TEST_ASSERT_FALSE(demeter::lightsShouldBeOn(17 * 60, targets));
    TEST_ASSERT_EQUAL_INT(10 * 60, demeter::lightOffMinute(targets));
}

void test_lettuce_day_schedule() {
    const demeter::Targets targets = demeter::lettucePreset();
    TEST_ASSERT_FALSE(demeter::lightsShouldBeOn(5 * 60, targets));
    TEST_ASSERT_TRUE(demeter::lightsShouldBeOn(6 * 60, targets));
    TEST_ASSERT_TRUE(demeter::lightsShouldBeOn(21 * 60, targets));
    TEST_ASSERT_FALSE(demeter::lightsShouldBeOn(22 * 60, targets));
}

void test_shelly_timespec_and_fallbacks() {
    char buf[32];
    TEST_ASSERT_TRUE(demeter::formatShellyTimespec(6 * 60, buf, sizeof(buf)));
    TEST_ASSERT_EQUAL_STRING("0 0 6 * *", buf);
    demeter::ScheduleSpec specs[2];
    const int n = demeter::lightFallbackSchedules(demeter::lettucePreset(), specs, 2);
    TEST_ASSERT_EQUAL_INT(2, n);
    TEST_ASSERT_TRUE(specs[0].on);
    TEST_ASSERT_FALSE(specs[1].on);
    TEST_ASSERT_EQUAL_STRING("0 0 22 * *", specs[1].timespec);
    demeter::ScheduleSpec pump[1];
    TEST_ASSERT_EQUAL_INT(1, demeter::pumpFallbackSchedules(pump, 1));
    TEST_ASSERT_TRUE(pump[0].on);
    TEST_ASSERT_EQUAL_STRING("0 0 * * *", pump[0].timespec);
}

void test_always_on_and_off() {
    demeter::Targets targets = demeter::lettucePreset();
    targets.photoperiodHours = 24.f;
    demeter::ScheduleSpec specs[2];
    TEST_ASSERT_EQUAL_INT(1, demeter::lightFallbackSchedules(targets, specs, 2));
    TEST_ASSERT_TRUE(specs[0].on);
    TEST_ASSERT_TRUE(demeter::lightsShouldBeOn(0, targets));
    targets.photoperiodHours = 0.f;
    TEST_ASSERT_FALSE(demeter::lightsShouldBeOn(12 * 60, targets));
}

void test_civil_time_offset() {
    int year = 0;
    int month = 0;
    int day = 0;
    int minute = 0;
    demeter::civilFromUnix(0, 0, year, month, day, minute);
    TEST_ASSERT_EQUAL_INT(1970, year);
    TEST_ASSERT_EQUAL_INT(1, month);
    TEST_ASSERT_EQUAL_INT(1, day);
    TEST_ASSERT_EQUAL_INT(0, minute);
    demeter::civilFromUnix(8 * 3600, -480, year, month, day, minute);
    TEST_ASSERT_EQUAL_INT(1970, year);
    TEST_ASSERT_EQUAL_INT(1, day);
    TEST_ASSERT_EQUAL_INT(0, minute);
    TEST_ASSERT_EQUAL_INT(19700101, demeter::yyyymmddFromCivil(year, month, day));
}

void test_parse_clock() {
    int minute = -1;
    TEST_ASSERT_TRUE(demeter::parseClock("06:30", minute));
    TEST_ASSERT_EQUAL_INT(6 * 60 + 30, minute);
    TEST_ASSERT_FALSE(demeter::parseClock("24:00", minute));
    TEST_ASSERT_FALSE(demeter::parseClock("6", minute));
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_lights_wrap_past_midnight);
    RUN_TEST(test_lettuce_day_schedule);
    RUN_TEST(test_shelly_timespec_and_fallbacks);
    RUN_TEST(test_always_on_and_off);
    RUN_TEST(test_civil_time_offset);
    RUN_TEST(test_parse_clock);
    return UNITY_END();
}
