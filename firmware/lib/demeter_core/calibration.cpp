#include "calibration.h"

#include <cmath>
#include <ctime>

namespace demeter {

float tdsPpmFromVoltage(float volts, float tempC) {
    const float coef = 1.f + 0.02f * (tempC - 25.f);
    if (coef < 0.2f) {
        return 0.f;
    }
    const float v = volts / coef;
    const float ppm =
        (133.42f * v * v * v - 255.86f * v * v + 857.39f * v) * 0.5f;
    return ppm < 0.f ? 0.f : ppm;
}

int phPointCount(const PhCal& cal) {
    int n = 0;
    for (int i = 0; i < kPhPoints; ++i) {
        if (cal.points[i].set) {
            ++n;
        }
    }
    return n;
}

namespace {

struct VoltPoint {
    float volts;
    float ph;
    float tempC;
};

int collectPoints(const PhCal& cal, VoltPoint* out) {
    int n = 0;
    for (int i = 0; i < kPhPoints; ++i) {
        if (!cal.points[i].set) {
            continue;
        }
        out[n].volts = cal.points[i].volts;
        out[n].ph = cal.points[i].ph;
        out[n].tempC = cal.points[i].tempC;
        ++n;
    }
    for (int i = 0; i < n; ++i) {
        for (int j = i + 1; j < n; ++j) {
            if (out[j].volts < out[i].volts) {
                const VoltPoint tmp = out[i];
                out[i] = out[j];
                out[j] = tmp;
            }
        }
    }
    return n;
}

bool linearPh(const VoltPoint& a, const VoltPoint& b, float volts, float& ph, float& tempC) {
    const float dv = b.volts - a.volts;
    if (std::fabs(dv) < 1e-4f) {
        return false;
    }
    const float t = (volts - a.volts) / dv;
    ph = a.ph + t * (b.ph - a.ph);
    tempC = 0.5f * (a.tempC + b.tempC);
    return true;
}

}  // namespace

bool phFromVoltage(const PhCal& cal, float volts, float tempC, float& phOut) {
    VoltPoint pts[kPhPoints];
    const int n = collectPoints(cal, pts);
    if (n < 2) {
        return false;
    }
    float raw = 0.f;
    float tCal = 25.f;
    bool ok = false;
    if (n == 2) {
        ok = linearPh(pts[0], pts[1], volts, raw, tCal);
    } else {
        int idx = 0;
        if (volts >= pts[n - 1].volts) {
            idx = n - 2;
        } else {
            for (int i = 0; i < n - 1; ++i) {
                if (volts <= pts[i + 1].volts) {
                    idx = i;
                    break;
                }
            }
        }
        ok = linearPh(pts[idx], pts[idx + 1], volts, raw, tCal);
    }
    if (!ok) {
        return false;
    }
    const float tk = tempC + 273.15f;
    const float tCalK = tCal + 273.15f;
    if (tk < 250.f || tCalK < 250.f) {
        return false;
    }
    phOut = 7.f + (raw - 7.f) * (tCalK / tk);
    return true;
}

float median(const float* values, int count) {
    if (count <= 0) {
        return 0.f;
    }
    float tmp[16];
    if (count > 16) {
        count = 16;
    }
    for (int i = 0; i < count; ++i) {
        tmp[i] = values[i];
    }
    for (int i = 0; i < count; ++i) {
        for (int j = i + 1; j < count; ++j) {
            if (tmp[j] < tmp[i]) {
                const float s = tmp[i];
                tmp[i] = tmp[j];
                tmp[j] = s;
            }
        }
    }
    return tmp[count / 2];
}

bool formatShellyTimespec(int minuteOfDay, char* out, size_t outLen) {
    if (!out || outLen < 12 || minuteOfDay < 0 || minuteOfDay >= 1440) {
        return false;
    }
    const int hour = minuteOfDay / 60;
    const int minute = minuteOfDay % 60;
    std::snprintf(out, outLen, "0 %d %d * *", minute, hour);
    return true;
}

int lightOffMinute(const Targets& targets) {
    int duration = static_cast<int>(targets.photoperiodHours * 60.f + 0.5f);
    if (duration < 0) {
        duration = 0;
    }
    if (duration > 24 * 60) {
        duration = 24 * 60;
    }
    return (targets.lightOnMinute + duration) % 1440;
}

bool lightsShouldBeOn(int minuteOfDay, const Targets& targets) {
    if (minuteOfDay < 0 || minuteOfDay >= 1440) {
        return false;
    }
    int duration = static_cast<int>(targets.photoperiodHours * 60.f + 0.5f);
    if (duration >= 1440) {
        return true;
    }
    if (duration <= 0) {
        return false;
    }
    const int on = targets.lightOnMinute;
    const int off = (on + duration) % 1440;
    if (on < off) {
        return minuteOfDay >= on && minuteOfDay < off;
    }
    return minuteOfDay >= on || minuteOfDay < off;
}

int lightFallbackSchedules(const Targets& targets, ScheduleSpec* out, int cap) {
    if (!out || cap < 1) {
        return 0;
    }
    int duration = static_cast<int>(targets.photoperiodHours * 60.f + 0.5f);
    if (duration >= 1440) {
        if (!formatShellyTimespec(targets.lightOnMinute, out[0].timespec, sizeof(out[0].timespec))) {
            return 0;
        }
        out[0].on = true;
        return 1;
    }
    if (duration <= 0) {
        if (!formatShellyTimespec(0, out[0].timespec, sizeof(out[0].timespec))) {
            return 0;
        }
        out[0].on = false;
        return 1;
    }
    if (cap < 2) {
        return 0;
    }
    if (!formatShellyTimespec(targets.lightOnMinute, out[0].timespec, sizeof(out[0].timespec))) {
        return 0;
    }
    out[0].on = true;
    const int off = lightOffMinute(targets);
    if (!formatShellyTimespec(off, out[1].timespec, sizeof(out[1].timespec))) {
        return 0;
    }
    out[1].on = false;
    return 2;
}

int pumpFallbackSchedules(ScheduleSpec* out, int cap) {
    if (!out || cap < 1) {
        return 0;
    }
    // Dedicated circulation plug. Hourly edges bring the pump back if the
    // controller drops offline. Low-water shutdown needs the controller up.
    std::snprintf(out[0].timespec, sizeof(out[0].timespec), "0 0 * * *");
    out[0].on = true;
    return 1;
}

void civilFromUnix(int64_t unixUtc, int offsetMin, int& year, int& month, int& day, int& minuteOfDay) {
    const time_t t = static_cast<time_t>(unixUtc + static_cast<int64_t>(offsetMin) * 60);
    struct tm tm {};
    gmtime_r(&t, &tm);
    year = tm.tm_year + 1900;
    month = tm.tm_mon + 1;
    day = tm.tm_mday;
    minuteOfDay = tm.tm_hour * 60 + tm.tm_min;
}

int yyyymmddFromCivil(int year, int month, int day) {
    return year * 10000 + month * 100 + day;
}

bool parseClock(const char* text, int& minuteOfDay) {
    if (!text || !text[0]) {
        return false;
    }
    int hour = 0;
    int minute = 0;
    if (std::sscanf(text, "%d:%d", &hour, &minute) != 2) {
        return false;
    }
    if (hour < 0 || hour > 23 || minute < 0 || minute > 59) {
        return false;
    }
    minuteOfDay = hour * 60 + minute;
    return true;
}

}  // namespace demeter
