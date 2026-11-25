#pragma once

#include "types.h"

namespace demeter {

// DFRobot SEN0244 cubic, already on the 500-scale NaCl ppm curve.
float tdsPpmFromVoltage(float volts, float tempC);

// Piecewise linear pH from 2 or 3 stored points, then Nernst slope
// adjustment around the pH 7 isopotential.
bool phFromVoltage(const PhCal& cal, float volts, float tempC, float& phOut);

int phPointCount(const PhCal& cal);

float median(const float* values, int count);

struct ScheduleSpec {
    char timespec[32];
    bool on;
};

// Shelly timespec is "ss mm hh DD WW".
bool formatShellyTimespec(int minuteOfDay, char* out, size_t outLen);

int lightFallbackSchedules(const Targets& targets, ScheduleSpec* out, int cap);
int pumpFallbackSchedules(ScheduleSpec* out, int cap);

bool lightsShouldBeOn(int minuteOfDay, const Targets& targets);
int lightOffMinute(const Targets& targets);

void civilFromUnix(int64_t unixUtc, int offsetMin, int& year, int& month, int& day, int& minuteOfDay);
int yyyymmddFromCivil(int year, int month, int day);

bool parseClock(const char* text, int& minuteOfDay);

}  // namespace demeter
