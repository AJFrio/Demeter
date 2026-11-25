#include "control.h"

#include <cmath>

namespace demeter {
namespace {

bool nameOk(const char* name) {
    if (!name || !name[0] || std::strlen(name) > 23) {
        return false;
    }
    for (const char* p = name; *p; ++p) {
        const char c = *p;
        const bool ok = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-';
        if (!ok) {
            return false;
        }
    }
    return true;
}

bool targetsOk(const Targets& t, float maxEc, const char*& error) {
    if (!(t.phMin < t.phMax) || t.phMin < 0.f || t.phMax > 14.f) {
        error = "invalid_ph_range";
        return false;
    }
    if (!(t.ecMin < t.ecMax) || t.ecMin < 0.f) {
        error = "invalid_ec_range";
        return false;
    }
    if (t.ecMax > maxEc + 0.001f) {
        error = "ec_above_sensor_range";
        return false;
    }
    if (t.lightOnMinute < 0 || t.lightOnMinute >= 1440) {
        error = "invalid_light_on";
        return false;
    }
    if (t.photoperiodHours < 0.f || t.photoperiodHours > 24.f) {
        error = "invalid_photoperiod";
        return false;
    }
    return true;
}

}  // namespace

const char* faultText(const char* code) {
    if (!code) {
        return "";
    }
    if (std::strcmp(code, "low_water") == 0) {
        return "Reservoir water is low. Circulation and dosing are blocked.";
    }
    if (std::strcmp(code, "no_flow") == 0) {
        return "Circulation is commanded but flow is below the no-flow threshold.";
    }
    if (std::strcmp(code, "ph_invalid") == 0) {
        return "pH reading is missing, stale, or outside the sanity range.";
    }
    if (std::strcmp(code, "ec_invalid") == 0) {
        return "EC reading is missing, stale, uncalibrated, or outside the sanity range.";
    }
    if (std::strcmp(code, "temp_invalid") == 0) {
        return "Water temperature is missing, so pH and EC compensation is not trusted.";
    }
    if (std::strcmp(code, "ec_high") == 0) {
        return "EC is above the target. The controller cannot dilute; nutrient dosing is paused.";
    }
    if (std::strcmp(code, "ec_saturated") == 0) {
        return "TDS probe is at the top of its 0-1000 ppm range. Nutrient dosing is paused.";
    }
    if (std::strcmp(code, "daily_limit") == 0) {
        return "A dosing pump has reached its daily volume limit.";
    }
    if (std::strcmp(code, "shelly_pump_unreachable") == 0) {
        return "The circulation Shelly plug is not answering on the local network.";
    }
    if (std::strcmp(code, "shelly_lights_unreachable") == 0) {
        return "The lights Shelly plug is not answering on the local network.";
    }
    if (std::strcmp(code, "shelly_pump_unconfigured") == 0) {
        return "No host is set for the circulation Shelly plug.";
    }
    if (std::strcmp(code, "shelly_lights_unconfigured") == 0) {
        return "No host is set for the lights Shelly plug.";
    }
    if (std::strcmp(code, "time_unsynced") == 0) {
        return "Clock is not synced, so the controller is not overriding the light schedule.";
    }
    if (std::strcmp(code, "pump_uncalibrated") == 0) {
        return "Automatic dosing is waiting for a mL/sec calibration on the pump it needs.";
    }
    if (std::strcmp(code, "ph_uncalibrated") == 0) {
        return "pH needs two or three calibration points before automatic pH dosing.";
    }
    if (std::strcmp(code, "tds_uncalibrated") == 0) {
        return "TDS scale is uncalibrated. Nutrient dosing stays off until a one-point cal or factory mode.";
    }
    if (std::strcmp(code, "loop_stall") == 0) {
        return "The control loop was late. Dosing pumps are stopped by the dose timer.";
    }
    if (std::strcmp(code, "startup") == 0) {
        return "Startup hold is still running so the first readings can settle.";
    }
    if (std::strcmp(code, "mixing") == 0) {
        return "Waiting for the reservoir to mix before the next dose.";
    }
    if (std::strcmp(code, "service_mode_required") == 0) {
        return "Manual actuator commands require service mode.";
    }
    if (std::strcmp(code, "pump_busy") == 0) {
        return "Another dosing pump is already running.";
    }
    if (std::strcmp(code, "circulation_unconfirmed") == 0) {
        return "Circulation is not confirmed on, so dosing stays off.";
    }
    if (std::strcmp(code, "ec_above_sensor_range") == 0) {
        return "That EC target is above the TDS probe range of about 2.0 mS/cm.";
    }
    return code;
}

Controller::Controller() { setDefaults(); }

void Controller::setDefaults() {
    targets = lettucePreset();
    limits = Limits();
    phCal = PhCal();
    tdsCal = TdsCal();
    for (int i = 0; i < kPumpCount; ++i) {
        pumps[i] = PumpCal();
    }
    for (int i = 0; i < kCustomProfiles; ++i) {
        custom[i] = ProfileSlot();
    }
    lettuceOverride = false;
    basilOverride = false;
    mode = Mode::Auto;
    outputs = Outputs();
    faults = FaultFlags();
    active = ActiveDose();
    startupDone = false;
    nextNutrientIsB = false;
    serviceCirculation = false;
    serviceLights = false;
    doseDay = 0;
    lastBlock = nullptr;
    doseCount = 0;
    doseNext = 0;
    historyCount = 0;
    historyNext = 0;
    haveHistory = false;
    wifiSsid[0] = '\0';
    wifiPassword[0] = '\0';
    wifiApplyRequested = false;
    wifiConnected = false;
    ipAddress[0] = '\0';
    wifiRssi = 0;
    apiToken[0] = '\0';
    shellyPumpHost[0] = '\0';
    shellyLightsHost[0] = '\0';
    shellyScheduleDirty = true;
    dirty = false;
    simulation = Simulation();
}

bool Controller::phCalibrated() const { return phPointCount(phCal) >= 2; }

float Controller::sensorMaxEc() const { return maxMeasurableEc(limits.ppmPerMs); }

int Controller::lightOff() const { return lightOffMinute(targets); }

bool Controller::profileNameOk(const char* name) const { return nameOk(name); }

bool Controller::findProfile(const char* name, Targets& out, bool& builtin, bool& overridden) const {
    builtin = false;
    overridden = false;
    if (!name) {
        return false;
    }
    if (std::strcmp(name, "lettuce") == 0) {
        builtin = true;
        overridden = lettuceOverride;
        out = lettuceOverride ? lettuceCustom : lettucePreset();
        return true;
    }
    if (std::strcmp(name, "basil") == 0) {
        builtin = true;
        overridden = basilOverride;
        out = basilOverride ? basilCustom : basilPreset();
        return true;
    }
    for (int i = 0; i < kCustomProfiles; ++i) {
        if (custom[i].used && std::strcmp(custom[i].name, name) == 0) {
            out = custom[i].targets;
            return true;
        }
    }
    return false;
}

const char* Controller::setTargets(const Targets& next) {
    const char* error = nullptr;
    Targets copy = next;
    if (copy.profile[0] == '\0') {
        copyStr(copy.profile, sizeof(copy.profile), "custom");
    }
    if (!targetsOk(copy, sensorMaxEc(), error)) {
        return error;
    }
    targets = copy;
    shellyScheduleDirty = true;
    dirty = true;
    return nullptr;
}

const char* Controller::applyProfile(const char* name) {
    Targets next;
    bool builtin = false;
    bool overridden = false;
    if (!findProfile(name, next, builtin, overridden)) {
        return "unknown_profile";
    }
    copyStr(next.profile, sizeof(next.profile), name);
    return setTargets(next);
}

const char* Controller::saveProfile(const char* name, const Targets& incoming) {
    if (!profileNameOk(name)) {
        return "invalid_profile_name";
    }
    Targets next = incoming;
    copyStr(next.profile, sizeof(next.profile), name);
    const char* error = nullptr;
    if (!targetsOk(next, sensorMaxEc(), error)) {
        return error;
    }
    if (std::strcmp(name, "lettuce") == 0) {
        lettuceOverride = true;
        lettuceCustom = next;
        dirty = true;
        return nullptr;
    }
    if (std::strcmp(name, "basil") == 0) {
        basilOverride = true;
        basilCustom = next;
        dirty = true;
        return nullptr;
    }
    int freeSlot = -1;
    for (int i = 0; i < kCustomProfiles; ++i) {
        if (custom[i].used && std::strcmp(custom[i].name, name) == 0) {
            custom[i].targets = next;
            dirty = true;
            return nullptr;
        }
        if (!custom[i].used && freeSlot < 0) {
            freeSlot = i;
        }
    }
    if (freeSlot < 0) {
        return "profile_storage_full";
    }
    custom[freeSlot].used = true;
    copyStr(custom[freeSlot].name, sizeof(custom[freeSlot].name), name);
    custom[freeSlot].targets = next;
    dirty = true;
    return nullptr;
}

const char* Controller::resetBuiltin(const char* name) {
    if (std::strcmp(name, "lettuce") == 0) {
        lettuceOverride = false;
        dirty = true;
        return nullptr;
    }
    if (std::strcmp(name, "basil") == 0) {
        basilOverride = false;
        dirty = true;
        return nullptr;
    }
    return "not_builtin";
}

const char* Controller::deleteProfile(const char* name) {
    if (!name) {
        return "unknown_profile";
    }
    if (std::strcmp(name, "lettuce") == 0 || std::strcmp(name, "basil") == 0) {
        return "builtin_profile";
    }
    for (int i = 0; i < kCustomProfiles; ++i) {
        if (custom[i].used && std::strcmp(custom[i].name, name) == 0) {
            custom[i] = ProfileSlot();
            dirty = true;
            return nullptr;
        }
    }
    return "unknown_profile";
}

const char* Controller::setMode(Mode next) {
    if (next == Mode::Service && mode != Mode::Service) {
        serviceCirculation = outputs.circulation;
        serviceLights = outputs.lights;
    }
    mode = next;
    dirty = true;
    return nullptr;
}

void Controller::pushDose(PumpId id, float ml, int64_t unixTime, bool manual) {
    DoseEvent& ev = doses[doseNext];
    ev.pump = static_cast<uint8_t>(id);
    ev.ml = ml;
    ev.unixTime = unixTime;
    ev.manual = manual;
    doseNext = (doseNext + 1) % kDoseLog;
    if (doseCount < kDoseLog) {
        ++doseCount;
    }
}

void Controller::startDose(PumpId id, float ml, uint32_t nowMs, bool manual) {
    PumpCal& pump = pumps[static_cast<int>(id)];
    const uint32_t duration = static_cast<uint32_t>((ml / pump.mlPerSec) * 1000.f);
    const uint32_t runMs = duration < 200 ? 200 : duration;
    const float actual = pump.mlPerSec * (static_cast<float>(runMs) / 1000.f);
    pump.dosedTodayMl += actual;
    active.running = true;
    active.pump = id;
    active.startedAtMs = nowMs;
    active.stopAtMs = nowMs + runMs;
    active.ml = actual;
    active.manual = manual;
    active.id += 1;
    if (id == PumpId::NutrientA) {
        nextNutrientIsB = true;
    } else if (id == PumpId::NutrientB) {
        nextNutrientIsB = false;
    }
    pushDose(id, actual, last.unixTime, manual);
    dirty = true;
}

const char* Controller::manualDose(PumpId id, float ml, uint32_t nowMs) {
    const char* block = manualDoseBlock(mode, last.sensors, pumps[static_cast<int>(id)], limits, ml,
                                       active.running, limits.maxManualMl);
    if (block) {
        return block;
    }
    startDose(id, ml, nowMs, true);
    return nullptr;
}

const char* Controller::manualPrime(PumpId id, uint32_t seconds, uint32_t nowMs) {
    if (seconds == 0 || seconds > limits.primeMaxSec) {
        return "invalid_amount";
    }
    const float ml = pumps[static_cast<int>(id)].mlPerSec * static_cast<float>(seconds);
    const char* block = manualDoseBlock(mode, last.sensors, pumps[static_cast<int>(id)], limits, ml,
                                         active.running, limits.primeMaxSec * 20.f);
    if (block) {
        return block;
    }
    startDose(id, ml, nowMs, true);
    return nullptr;
}

const char* Controller::manualCirculation(bool on) {
    if (mode != Mode::Service) {
        return "service_mode_required";
    }
    if (on && last.sensors.waterLow) {
        return "low_water";
    }
    serviceCirculation = on;
    outputs.circulation = on && !last.sensors.waterLow;
    return nullptr;
}

const char* Controller::manualLights(bool on) {
    if (mode != Mode::Service) {
        return "service_mode_required";
    }
    serviceLights = on;
    outputs.lights = on;
    outputs.commandLights = true;
    return nullptr;
}

const char* Controller::emergencyStop(uint32_t nowMs) {
    mode = Mode::Service;
    active.running = false;
    mixUntilMs = nowMs;
    serviceCirculation = false;
    outputs.circulation = false;
    outputs.commandLights = true;
    dirty = true;
    return nullptr;
}

const char* Controller::capturePhPoint(float ph, float volts, float tempC) {
    if (ph < 0.f || ph > 14.f || volts < 0.05f || volts > 3.1f) {
        return "invalid_calibration";
    }
    int slot = -1;
    for (int i = 0; i < kPhPoints; ++i) {
        if (phCal.points[i].set && std::fabs(phCal.points[i].ph - ph) < 0.25f) {
            slot = i;
            break;
        }
        if (!phCal.points[i].set && slot < 0) {
            slot = i;
        }
    }
    if (slot < 0) {
        float farthest = -1.f;
        for (int i = 0; i < kPhPoints; ++i) {
            const float d = std::fabs(phCal.points[i].ph - ph);
            if (d > farthest) {
                farthest = d;
                slot = i;
            }
        }
    }
    phCal.points[slot].set = true;
    phCal.points[slot].ph = ph;
    phCal.points[slot].volts = volts;
    phCal.points[slot].tempC = tempC;
    dirty = true;
    return nullptr;
}

const char* Controller::clearPhCal() {
    phCal = PhCal();
    dirty = true;
    return nullptr;
}

const char* Controller::captureTds(float referencePpm, float volts, float tempC) {
    if (referencePpm < 20.f || referencePpm > kSensorMaxPpm) {
        return "invalid_calibration";
    }
    const float factory = tdsPpmFromVoltage(volts, tempC);
    if (factory < 10.f) {
        return "invalid_calibration";
    }
    tdsCal.scale = referencePpm / factory;
    if (tdsCal.scale < 0.25f || tdsCal.scale > 4.f) {
        tdsCal.scale = 1.f;
        return "invalid_calibration";
    }
    tdsCal.calibrated = true;
    dirty = true;
    return nullptr;
}

const char* Controller::clearTdsCal() {
    tdsCal = TdsCal();
    dirty = true;
    return nullptr;
}

const char* Controller::setPumpCal(PumpId id, float mlPerSec, bool invert) {
    if (mlPerSec < 0.05f || mlPerSec > 20.f) {
        return "invalid_calibration";
    }
    PumpCal& pump = pumps[static_cast<int>(id)];
    pump.mlPerSec = mlPerSec;
    pump.invert = invert;
    pump.calibrated = true;
    dirty = true;
    return nullptr;
}

void Controller::acknowledgeFaults() {
    faults.noFlow = false;
    noFlowAccumMs = 0;
}

void Controller::rollDay(int yyyymmdd) {
    if (yyyymmdd <= 0) {
        return;
    }
    if (doseDay == 0) {
        doseDay = yyyymmdd;
        return;
    }
    if (yyyymmdd != doseDay) {
        doseDay = yyyymmdd;
        for (int i = 0; i < kPumpCount; ++i) {
            pumps[i].dosedTodayMl = 0.f;
        }
        dirty = true;
    }
}

void Controller::finishDoseIfDue(uint32_t nowMs) {
    if (!active.running) {
        return;
    }
    const bool aborted = last.sensors.waterLow;
    if (aborted || static_cast<int32_t>(nowMs - active.stopAtMs) >= 0) {
        active.running = false;
        mixUntilMs = nowMs + (aborted ? 0 : limits.mixLockoutMs);
    }
}

void Controller::pushHistory(const Inputs& in) {
    if (haveHistory && static_cast<int32_t>(in.nowMs - lastHistoryMs) < 60000) {
        return;
    }
    HistorySample& s = history[historyNext];
    s.uptimeMs = in.nowMs;
    s.unixTime = in.timeSynced ? in.unixTime : 0;
    s.ph = in.sensors.ph;
    s.ecMs = in.sensors.ecMs;
    s.tempC = in.sensors.waterTempC;
    s.flowLpm = in.sensors.flowLpm;
    s.phValid = in.sensors.phValid;
    s.ecValid = in.sensors.ecValid;
    historyNext = (historyNext + 1) % kHistory;
    if (historyCount < kHistory) {
        ++historyCount;
    }
    lastHistoryMs = in.nowMs;
    haveHistory = true;
}

void Controller::updateFaults(const Inputs& in, bool mixing) {
    faults.lowWater = in.sensors.waterLow;
    faults.phInvalid = !in.sensors.phValid;
    faults.ecInvalid = !in.sensors.ecValid;
    faults.tempInvalid = !in.sensors.tempValid;
    faults.ecSaturated = in.sensors.ecSaturated;
    faults.ecHigh = in.sensors.ecValid && in.sensors.ecMs > targets.ecMax + limits.ecDeadband;
    faults.shellyPumpUnconfigured = !in.shellyPumpConfigured;
    faults.shellyLightsUnconfigured = !in.shellyLightsConfigured;
    faults.shellyPumpUnreachable = in.shellyPumpConfigured && !in.shellyPumpReachable;
    faults.shellyLightsUnreachable = in.shellyLightsConfigured && !in.shellyLightsReachable;
    faults.timeUnsynced = !in.timeSynced;
    faults.phUncalibrated = !phCalibrated();
    faults.tdsUncalibrated = !tdsCal.calibrated && !limits.useFactoryTds;
    faults.mixing = mixing;
    faults.startupHold = !startupDone;
    bool limit = false;
    for (int i = 0; i < kPumpCount; ++i) {
        if (pumps[i].dosedTodayMl >= pumps[i].dailyLimitMl - 0.001f) {
            limit = true;
        }
    }
    faults.dailyLimit = limit;
    faults.pumpUncalibrated = lastBlock && std::strcmp(lastBlock, "pump_uncalibrated") == 0;
}

void Controller::tick(const Inputs& in) {
    last = in;
    uint32_t dt = 0;
    if (haveTick) {
        dt = in.nowMs - lastTickMs;
    }
    if (haveTick && dt > 8000) {
        faults.loopStall = true;
    } else if (haveTick && dt < 2000) {
        faults.loopStall = false;
    }
    if (dt > 2000) {
        dt = 2000;
    }
    haveTick = true;
    lastTickMs = in.nowMs;

    if (!startupDone && in.nowMs >= limits.startupHoldMs) {
        startupDone = true;
    }
    if (in.timeSynced) {
        rollDay(in.localYyyymmdd);
    }
    finishDoseIfDue(in.nowMs);

    const bool mixing = active.running || static_cast<int32_t>(in.nowMs - mixUntilMs) < 0;

    bool wantCirculation = false;
    if (in.sensors.waterLow) {
        wantCirculation = false;
    } else if (mode == Mode::Service) {
        wantCirculation = serviceCirculation;
    } else if (faults.noFlow) {
        wantCirculation = false;
    } else {
        wantCirculation = true;
    }

    if (wantCirculation && in.circulationConfirmed && in.sensors.flowValid &&
        in.sensors.flowLpm < limits.noFlowLpm) {
        noFlowAccumMs += dt;
        if (noFlowAccumMs >= limits.noFlowDelayMs) {
            faults.noFlow = true;
            wantCirculation = mode == Mode::Service;
        }
    } else if (in.sensors.flowValid && in.sensors.flowLpm >= limits.noFlowLpm) {
        noFlowAccumMs = 0;
        faults.noFlow = false;
    } else if (!wantCirculation) {
        noFlowAccumMs = 0;
    }

    if (faults.noFlow && mode == Mode::Auto) {
        wantCirculation = false;
    }
    if (in.sensors.waterLow) {
        wantCirculation = false;
    }
    outputs.circulation = wantCirculation;

    if (mode == Mode::Service) {
        outputs.lights = serviceLights;
        outputs.commandLights = true;
    } else if (!in.timeSynced) {
        outputs.commandLights = false;
    } else {
        outputs.commandLights = true;
        outputs.lights = lightsShouldBeOn(in.localMinute, targets);
    }

    lastBlock = nullptr;
    if (mode == Mode::Auto && !active.running) {
        DoseContext ctx;
        ctx.targets = &targets;
        ctx.limits = &limits;
        ctx.readings = &in.sensors;
        ctx.pumps = pumps;
        ctx.phCalibrated = phCalibrated();
        ctx.nutrientReady = tdsCal.calibrated || limits.useFactoryTds;
        ctx.mixing = static_cast<int32_t>(in.nowMs - mixUntilMs) < 0;
        ctx.pumpBusy = false;
        ctx.circulationConfirmed = in.circulationConfirmed && outputs.circulation;
        ctx.startupHold = !startupDone;
        ctx.nextNutrientIsB = nextNutrientIsB;
        const DosePlan plan = planAutoDose(ctx);
        lastBlock = plan.block;
        if (plan.start) {
            startDose(plan.pump, plan.ml, in.nowMs, false);
        }
    }

    updateFaults(in, mixing || active.running);
    pushHistory(in);
}

}  // namespace demeter
