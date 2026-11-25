#include <unity.h>

#include "calibration.h"

#include <cmath>

void setUp() {}
void tearDown() {}

void test_tds_at_25c_one_volt() {
    const float ppm = demeter::tdsPpmFromVoltage(1.0f, 25.0f);
    TEST_ASSERT_FLOAT_WITHIN(0.05f, 367.475f, ppm);
}

void test_tds_temperature_compensation_raises_colder_water() {
    const float warm = demeter::tdsPpmFromVoltage(1.0f, 25.0f);
    const float cold = demeter::tdsPpmFromVoltage(1.0f, 15.0f);
    // DFRobot divides by 1+0.02*(T-25), so the same voltage means more TDS when cold.
    TEST_ASSERT_TRUE(cold > warm);
}

void test_ph_two_point_and_temperature() {
    demeter::PhCal cal;
    cal.points[0] = {true, 7.0f, 1.5f, 25.0f};
    cal.points[1] = {true, 4.0f, 2.0f, 25.0f};
    float ph = 0.f;
    TEST_ASSERT_TRUE(demeter::phFromVoltage(cal, 1.5f, 25.0f, ph));
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 7.0f, ph);
    TEST_ASSERT_TRUE(demeter::phFromVoltage(cal, 1.75f, 25.0f, ph));
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 5.5f, ph);
    TEST_ASSERT_TRUE(demeter::phFromVoltage(cal, 1.75f, 35.0f, ph));
    const float expected = 7.f + (5.5f - 7.f) * (298.15f / 308.15f);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, expected, ph);
}

void test_ph_three_point_piecewise() {
    demeter::PhCal cal;
    cal.points[0] = {true, 7.0f, 1.5f, 25.0f};
    cal.points[1] = {true, 4.0f, 2.0f, 25.0f};
    cal.points[2] = {true, 10.0f, 1.0f, 25.0f};
    float ph = 0.f;
    TEST_ASSERT_TRUE(demeter::phFromVoltage(cal, 1.25f, 25.0f, ph));
    TEST_ASSERT_FLOAT_WITHIN(0.02f, 8.5f, ph);
    TEST_ASSERT_EQUAL_INT(3, demeter::phPointCount(cal));
}

void test_ph_needs_two_points() {
    demeter::PhCal cal;
    cal.points[0] = {true, 7.0f, 1.5f, 25.0f};
    float ph = 0.f;
    TEST_ASSERT_FALSE(demeter::phFromVoltage(cal, 1.5f, 25.0f, ph));
}

void test_median_rejects_spike() {
    const float values[] = {6.1f, 6.0f, 9.5f, 6.2f, 6.05f};
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 6.1f, demeter::median(values, 5));
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_tds_at_25c_one_volt);
    RUN_TEST(test_tds_temperature_compensation_raises_colder_water);
    RUN_TEST(test_ph_two_point_and_temperature);
    RUN_TEST(test_ph_three_point_piecewise);
    RUN_TEST(test_ph_needs_two_points);
    RUN_TEST(test_median_rejects_spike);
    return UNITY_END();
}
