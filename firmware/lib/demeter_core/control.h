#pragma once

#include "calibration.h"
#include "dosing.h"
#include "presets.h"
#include "types.h"

namespace demeter {

struct Simulation {
    bool enabled = false;
    float ph = 6.2f;
    float ecMs = 1.4f;
    float waterTempC = 22.f;
    float flowLpm = 4.f;
    bool waterLow = false;
};

class Controller {
public:
    Controller();

    void setDefaults();
    void tick(const Inputs& in);

    const char* setMode(Mode mode);
    const char* manualDose(PumpId id, float ml, uint32_t nowMs);
    const char* manualPrime(PumpId id, uint32_t seconds, uint32_t nowMs);
    const char* manualCirculation(bool on);
    const char* manualLights(bool on);
    const char* emergencyStop(uint32_t nowMs);
    const char* applyProfile(const char* name);
    const char* saveProfile(const char* name, const Targets& targets);
    const char* resetBuiltin(const char* name);
    const char* deleteProfile(const char* name);
    const char* capturePhPoint(float ph, float volts, float tempC);
    const char* clearPhCal();
    const char* captureTds(float referencePpm, float volts, float tempC);
    const char* clearTdsCal();
    const char* setPumpCal(PumpId id, float mlPerSec, bool invert);
    const char* setTargets(const Targets& targets);
    void acknowledgeFaults();

    bool findProfile(const char* name, Targets& out, bool& builtin, bool& overridden) const;
    float sensorMaxEc() const;
    bool phCalibrated() const;
    int lightOff() const;

    Targets targets;
    Limits limits;
    PhCal phCal;
    TdsCal tdsCal;
    PumpCal pumps[kPumpCount];
    ProfileSlot custom[kCustomProfiles];
    bool lettuceOverride = false;
    bool basilOverride = false;
    Targets lettuceCustom;
    Targets basilCustom;

    Mode mode = Mode::Auto;
    Outputs outputs;
    FaultFlags faults;
    ActiveDose active;
    Inputs last;
    bool haveTick = false;
    uint32_t lastTickMs = 0;
    uint32_t mixUntilMs = 0;
    uint32_t noFlowAccumMs = 0;
    bool startupDone = false;
    bool nextNutrientIsB = false;
    bool serviceCirculation = false;
    bool serviceLights = false;
    int doseDay = 0;
    const char* lastBlock = nullptr;

    DoseEvent doses[kDoseLog];
    int doseCount = 0;
    int doseNext = 0;

    HistorySample history[kHistory];
    int historyCount = 0;
    int historyNext = 0;
    uint32_t lastHistoryMs = 0;
    bool haveHistory = false;

    char wifiSsid[33] = "";
    char wifiPassword[65] = "";
    bool wifiApplyRequested = false;
    bool wifiConnected = false;
    char ipAddress[40] = "";
    int wifiRssi = 0;
    char apiToken[49] = "";
    char shellyPumpHost[64] = "";
    char shellyLightsHost[64] = "";
    bool shellyScheduleDirty = true;
    bool dirty = false;

    Simulation simulation;

private:
    void startDose(PumpId id, float ml, uint32_t nowMs, bool manual);
    void finishDoseIfDue(uint32_t nowMs);
    void rollDay(int yyyymmdd);
    void pushDose(PumpId id, float ml, int64_t unixTime, bool manual);
    void pushHistory(const Inputs& in);
    void updateFaults(const Inputs& in, bool mixing);
    bool profileNameOk(const char* name) const;
};

const char* faultText(const char* code);

}  // namespace demeter
