#pragma once

#include <cstdint>
#include <cstdio>
#include <cstring>

namespace demeter {

constexpr const char* kFirmwareVersion = "1.0.0";
constexpr const char* kDeviceName = "demeter";
constexpr int kPumpCount = 4;
constexpr int kPhPoints = 3;
constexpr int kCustomProfiles = 4;
constexpr int kDoseLog = 16;
constexpr int kHistory = 60;
constexpr float kSensorMaxPpm = 1000.f;
constexpr uint32_t kPumpStopUs = 1500;
constexpr uint32_t kPumpClockwiseUs = 2500;
constexpr uint32_t kPumpCounterclockwiseUs = 500;

enum class Mode : uint8_t { Auto = 0, Service = 1 };

enum class PumpId : uint8_t {
    PhUp = 0,
    PhDown = 1,
    NutrientA = 2,
    NutrientB = 3
};

inline const char* pumpName(PumpId id) {
    switch (id) {
        case PumpId::PhUp:
            return "ph_up";
        case PumpId::PhDown:
            return "ph_down";
        case PumpId::NutrientA:
            return "nutrient_a";
        case PumpId::NutrientB:
            return "nutrient_b";
    }
    return "unknown";
}

inline bool pumpFromName(const char* name, PumpId& out) {
    if (!name) {
        return false;
    }
    if (std::strcmp(name, "ph_up") == 0 || std::strcmp(name, "phup") == 0) {
        out = PumpId::PhUp;
        return true;
    }
    if (std::strcmp(name, "ph_down") == 0 || std::strcmp(name, "phdown") == 0) {
        out = PumpId::PhDown;
        return true;
    }
    if (std::strcmp(name, "nutrient_a") == 0 || std::strcmp(name, "a") == 0) {
        out = PumpId::NutrientA;
        return true;
    }
    if (std::strcmp(name, "nutrient_b") == 0 || std::strcmp(name, "b") == 0) {
        out = PumpId::NutrientB;
        return true;
    }
    return false;
}

inline uint32_t dosePulseUs(bool invert) {
    return invert ? kPumpCounterclockwiseUs : kPumpClockwiseUs;
}

inline void copyStr(char* dst, size_t n, const char* src) {
    if (n == 0) {
        return;
    }
    if (!src) {
        dst[0] = '\0';
        return;
    }
    std::snprintf(dst, n, "%s", src);
}

struct Targets {
    float phMin = 6.0f;
    float phMax = 7.0f;
    float ecMin = 1.2f;
    float ecMax = 1.8f;
    int lightOnMinute = 6 * 60;
    float photoperiodHours = 16.f;
    char profile[24] = "lettuce";
};

struct PhPoint {
    bool set = false;
    float ph = 0.f;
    float volts = 0.f;
    float tempC = 25.f;
};

struct PhCal {
    PhPoint points[kPhPoints];
};

struct TdsCal {
    bool calibrated = false;
    float scale = 1.f;
};

struct PumpCal {
    float mlPerSec = 1.f;
    bool calibrated = false;
    bool invert = false;
    float dailyLimitMl = 50.f;
    float dosedTodayMl = 0.f;
};

struct Limits {
    float maxDoseMl = 2.f;
    float minDoseMl = 0.3f;
    float maxManualMl = 20.f;
    float phGainMlPerPh = 4.f;
    float ecGainMlPerMs = 8.f;
    float phDeadband = 0.02f;
    float ecDeadband = 0.03f;
    float nutrientBPerA = 1.f;
    uint32_t mixLockoutMs = 120000;
    uint32_t sensorStaleMs = 10000;
    uint32_t startupHoldMs = 15000;
    float noFlowLpm = 0.5f;
    uint32_t noFlowDelayMs = 15000;
    float ppmPerMs = 500.f;
    bool useFactoryTds = false;
    bool floatActiveLow = true;
    float flowPulsesPerLiter = 450.f;
    int tzOffsetMinutes = 0;
    int dstOffsetMinutes = 0;
    uint32_t primeMaxSec = 30;
};

struct Readings {
    float ph = 0.f;
    bool phValid = false;
    const char* phReason = "no_sample";
    float phVolts = 0.f;
    float ecMs = 0.f;
    float ecUs = 0.f;
    float tdsPpm = 0.f;
    bool ecValid = false;
    bool ecSaturated = false;
    const char* ecReason = "no_sample";
    float tdsVolts = 0.f;
    float waterTempC = 0.f;
    bool tempValid = false;
    float flowLpm = 0.f;
    bool flowValid = false;
    bool waterLow = false;
    uint32_t phAgeMs = 0;
    uint32_t ecAgeMs = 0;
    uint32_t tempAgeMs = 0;
};

struct ProfileSlot {
    bool used = false;
    char name[24] = "";
    Targets targets;
};

struct DoseEvent {
    uint8_t pump = 0;
    float ml = 0.f;
    int64_t unixTime = 0;
    bool manual = false;
};

struct HistorySample {
    uint32_t uptimeMs = 0;
    int64_t unixTime = 0;
    float ph = 0.f;
    float ecMs = 0.f;
    float tempC = 0.f;
    float flowLpm = 0.f;
    bool phValid = false;
    bool ecValid = false;
};

struct ActiveDose {
    bool running = false;
    PumpId pump = PumpId::PhUp;
    uint32_t stopAtMs = 0;
    uint32_t startedAtMs = 0;
    float ml = 0.f;
    bool manual = false;
    uint32_t id = 0;
};

struct FaultFlags {
    bool lowWater = false;
    bool noFlow = false;
    bool phInvalid = false;
    bool ecInvalid = false;
    bool tempInvalid = false;
    bool ecHigh = false;
    bool ecSaturated = false;
    bool dailyLimit = false;
    bool shellyPumpUnreachable = false;
    bool shellyLightsUnreachable = false;
    bool shellyPumpUnconfigured = false;
    bool shellyLightsUnconfigured = false;
    bool timeUnsynced = false;
    bool pumpUncalibrated = false;
    bool phUncalibrated = false;
    bool tdsUncalibrated = false;
    bool loopStall = false;
    bool startupHold = false;
    bool mixing = false;
};

struct Outputs {
    bool circulation = false;
    bool commandLights = false;
    bool lights = false;
};

struct Inputs {
    uint32_t nowMs = 0;
    bool timeSynced = false;
    int64_t unixTime = 0;
    int localMinute = 0;
    int localYyyymmdd = 0;
    Readings sensors;
    bool circulationConfirmed = false;
    bool lightsConfirmed = false;
    bool shellyPumpReachable = false;
    bool shellyLightsReachable = false;
    bool shellyPumpConfigured = false;
    bool shellyLightsConfigured = false;
};

inline float maxMeasurableEc(float ppmPerMs) {
    if (ppmPerMs < 1.f) {
        return 0.f;
    }
    return kSensorMaxPpm / ppmPerMs;
}

inline float ecToPpm(float ecMs, float ppmPerMs) { return ecMs * ppmPerMs; }

inline float ppmToEc(float ppm, float ppmPerMs) {
    if (ppmPerMs < 1.f) {
        return 0.f;
    }
    return ppm / ppmPerMs;
}

}  // namespace demeter
