#include "api.h"

#include <ArduinoJson.h>

#include <cctype>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>

namespace demeter {
namespace {

const char* apiMessage(const char* code) {
    if (!code) {
        return "";
    }
    if (std::strcmp(code, "invalid_json") == 0) {
        return "Request body must be a JSON object.";
    }
    if (std::strcmp(code, "unauthorized") == 0) {
        return "API token was missing or wrong.";
    }
    if (std::strcmp(code, "not_found") == 0) {
        return "No such API path.";
    }
    if (std::strcmp(code, "method_not_allowed") == 0) {
        return "That method is not allowed on this path.";
    }
    if (std::strcmp(code, "invalid_mode") == 0) {
        return "Mode must be auto or service.";
    }
    if (std::strcmp(code, "invalid_pump") == 0) {
        return "Pump must be ph_up, ph_down, nutrient_a, or nutrient_b.";
    }
    if (std::strcmp(code, "invalid_amount") == 0) {
        return "Volume or prime time is outside the allowed range.";
    }
    if (std::strcmp(code, "invalid_calibration") == 0) {
        return "Calibration value was rejected.";
    }
    if (std::strcmp(code, "invalid_host") == 0) {
        return "Host must be a hostname or IP with no scheme or spaces.";
    }
    if (std::strcmp(code, "invalid_profile_name") == 0) {
        return "Profile names use lowercase letters, digits, underscore, or hyphen.";
    }
    return faultText(code);
}

int httpStatus(const char* error) {
    if (!error) {
        return 200;
    }
    if (std::strcmp(error, "unauthorized") == 0) {
        return 401;
    }
    if (std::strcmp(error, "not_found") == 0 || std::strcmp(error, "unknown_profile") == 0) {
        return 404;
    }
    if (std::strcmp(error, "method_not_allowed") == 0) {
        return 405;
    }
    if (std::strcmp(error, "service_mode_required") == 0 || std::strcmp(error, "low_water") == 0 ||
        std::strcmp(error, "pump_busy") == 0 || std::strcmp(error, "daily_limit") == 0 ||
        std::strcmp(error, "pump_not_configured") == 0 || std::strcmp(error, "circulation_unconfirmed") == 0) {
        return 409;
    }
    return 400;
}

ApiResponse fail(const char* error) {
    JsonDocument doc;
    doc["ok"] = false;
    doc["error"] = error;
    doc["message"] = apiMessage(error);
    ApiResponse response;
    response.status = httpStatus(error);
    serializeJson(doc, response.body);
    return response;
}

ApiResponse okBody(JsonDocument& doc) {
    doc["ok"] = true;
    ApiResponse response;
    response.status = 200;
    serializeJson(doc, response.body);
    return response;
}

bool isMethod(const ApiRequest& req, const char* method) { return req.method == method; }

void writeClock(char* out, size_t n, int minuteOfDay) {
    if (minuteOfDay < 0) {
        minuteOfDay = 0;
    }
    std::snprintf(out, n, "%02d:%02d", minuteOfDay / 60, minuteOfDay % 60);
}

void writeTargets(JsonObject obj, const Targets& t, float ppmPerMs) {
    char clock[8];
    writeClock(clock, sizeof(clock), t.lightOnMinute);
    obj["profile"] = t.profile;
    obj["phMin"] = t.phMin;
    obj["phMax"] = t.phMax;
    obj["ecMin"] = t.ecMin;
    obj["ecMax"] = t.ecMax;
    obj["tdsMinPpm"] = ecToPpm(t.ecMin, ppmPerMs);
    obj["tdsMaxPpm"] = ecToPpm(t.ecMax, ppmPerMs);
    obj["lightOn"] = clock;
    obj["lightOnMinute"] = t.lightOnMinute;
    obj["photoperiodHours"] = t.photoperiodHours;
    obj["lightOffMinute"] = lightOffMinute(t);
}

const char* readTargets(Targets& t, JsonObject obj, float ppmPerMs, bool markCustom) {
    if (obj.isNull()) {
        return "invalid_json";
    }
    const bool numeric = !obj["phMin"].isNull() || !obj["phMax"].isNull() || !obj["ecMin"].isNull() ||
                         !obj["ecMax"].isNull() || !obj["tdsMinPpm"].isNull() || !obj["tdsMaxPpm"].isNull() ||
                         !obj["lightOn"].isNull() || !obj["lightOnMinute"].isNull() ||
                         !obj["photoperiodHours"].isNull();
    if (!obj["phMin"].isNull()) {
        t.phMin = obj["phMin"].as<float>();
    }
    if (!obj["phMax"].isNull()) {
        t.phMax = obj["phMax"].as<float>();
    }
    if (!obj["ecMin"].isNull()) {
        t.ecMin = obj["ecMin"].as<float>();
    }
    if (!obj["ecMax"].isNull()) {
        t.ecMax = obj["ecMax"].as<float>();
    }
    if (obj["ecMin"].isNull() && obj["ecMax"].isNull() && !obj["tdsMinPpm"].isNull() && !obj["tdsMaxPpm"].isNull()) {
        t.ecMin = ppmToEc(obj["tdsMinPpm"].as<float>(), ppmPerMs);
        t.ecMax = ppmToEc(obj["tdsMaxPpm"].as<float>(), ppmPerMs);
    }
    if (!obj["lightOnMinute"].isNull()) {
        t.lightOnMinute = obj["lightOnMinute"].as<int>();
    }
    if (!obj["lightOn"].isNull()) {
        int minute = 0;
        if (!parseClock(obj["lightOn"].as<const char*>(), minute)) {
            return "invalid_light_on";
        }
        t.lightOnMinute = minute;
    }
    if (!obj["photoperiodHours"].isNull()) {
        t.photoperiodHours = obj["photoperiodHours"].as<float>();
    }
    if (!obj["profile"].isNull()) {
        copyStr(t.profile, sizeof(t.profile), obj["profile"].as<const char*>());
    } else if (markCustom && numeric) {
        copyStr(t.profile, sizeof(t.profile), "custom");
    }
    return nullptr;
}

void addFault(JsonArray arr, bool on, const char* code) {
    if (on) {
        arr.add(code);
    }
}

void writeFaults(JsonArray arr, const FaultFlags& f) {
    addFault(arr, f.lowWater, "low_water");
    addFault(arr, f.noFlow, "no_flow");
    addFault(arr, f.phInvalid, "ph_invalid");
    addFault(arr, f.ecInvalid, "ec_invalid");
    addFault(arr, f.tempInvalid, "temp_invalid");
    addFault(arr, f.ecHigh, "ec_high");
    addFault(arr, f.ecSaturated, "ec_saturated");
    addFault(arr, f.dailyLimit, "daily_limit");
    addFault(arr, f.shellyPumpUnreachable, "shelly_pump_unreachable");
    addFault(arr, f.shellyLightsUnreachable, "shelly_lights_unreachable");
    addFault(arr, f.shellyPumpUnconfigured, "shelly_pump_unconfigured");
    addFault(arr, f.shellyLightsUnconfigured, "shelly_lights_unconfigured");
    addFault(arr, f.timeUnsynced, "time_unsynced");
    addFault(arr, f.pumpUncalibrated, "pump_uncalibrated");
    addFault(arr, f.phUncalibrated, "ph_uncalibrated");
    addFault(arr, f.tdsUncalibrated, "tds_uncalibrated");
    addFault(arr, f.loopStall, "loop_stall");
    addFault(arr, f.startupHold, "startup");
    addFault(arr, f.mixing, "mixing");
}

void writeStatus(JsonDocument& doc, const Controller& c) {
    doc["device"] = kDeviceName;
    doc["version"] = kFirmwareVersion;
    doc["mode"] = c.mode == Mode::Service ? "service" : "auto";
    doc["uptimeMs"] = c.last.nowMs;
    doc["simulation"] = c.simulation.enabled;
    JsonObject time = doc["time"].to<JsonObject>();
    time["synced"] = c.last.timeSynced;
    time["unix"] = c.last.unixTime;
    time["minuteOfDay"] = c.last.localMinute;
    time["yyyymmdd"] = c.last.localYyyymmdd;
    time["tzOffsetMinutes"] = c.limits.tzOffsetMinutes;
    time["dstOffsetMinutes"] = c.limits.dstOffsetMinutes;
    JsonObject wifi = doc["wifi"].to<JsonObject>();
    wifi["ssid"] = c.wifiSsid;
    wifi["connected"] = c.wifiConnected;
    wifi["ip"] = c.ipAddress;
    wifi["rssi"] = c.wifiRssi;
    wifi["mdns"] = "demeter.local";

    const Readings& r = c.last.sensors;
    JsonObject readings = doc["readings"].to<JsonObject>();
    JsonObject ph = readings["ph"].to<JsonObject>();
    ph["value"] = r.ph;
    ph["valid"] = r.phValid;
    ph["reason"] = r.phReason ? r.phReason : "";
    ph["volts"] = r.phVolts;
    ph["ageMs"] = r.phAgeMs;
    JsonObject ec = readings["ec"].to<JsonObject>();
    ec["mS"] = r.ecMs;
    ec["uS"] = r.ecUs;
    ec["ppm"] = r.tdsPpm;
    ec["valid"] = r.ecValid;
    ec["saturated"] = r.ecSaturated;
    ec["reason"] = r.ecReason ? r.ecReason : "";
    ec["volts"] = r.tdsVolts;
    ec["ageMs"] = r.ecAgeMs;
    JsonObject temp = readings["waterTempC"].to<JsonObject>();
    temp["value"] = r.waterTempC;
    temp["valid"] = r.tempValid;
    temp["ageMs"] = r.tempAgeMs;
    JsonObject flow = readings["flowLpm"].to<JsonObject>();
    flow["value"] = r.flowLpm;
    flow["valid"] = r.flowValid;
    readings["level"] = r.waterLow ? "low" : "ok";

    JsonObject sensor = doc["nutrientSensor"].to<JsonObject>();
    sensor["model"] = "SEN0244";
    sensor["kind"] = "tds";
    sensor["maxPpm"] = kSensorMaxPpm;
    sensor["ppmPerMs"] = c.limits.ppmPerMs;
    sensor["maxEc"] = c.sensorMaxEc();
    sensor["ecFromTds"] = "mS/cm = ppm / ppmPerMs";

    JsonObject targets = doc["targets"].to<JsonObject>();
    writeTargets(targets, c.targets, c.limits.ppmPerMs);

    JsonObject circ = doc["circulation"].to<JsonObject>();
    circ["desired"] = c.outputs.circulation;
    circ["confirmed"] = c.last.circulationConfirmed;
    circ["reachable"] = c.last.shellyPumpReachable;
    circ["configured"] = c.last.shellyPumpConfigured;
    circ["host"] = c.shellyPumpHost;

    JsonObject lights = doc["lights"].to<JsonObject>();
    lights["desired"] = c.outputs.lights;
    lights["commanded"] = c.outputs.commandLights;
    lights["confirmed"] = c.last.lightsConfirmed;
    lights["reachable"] = c.last.shellyLightsReachable;
    lights["configured"] = c.last.shellyLightsConfigured;
    lights["host"] = c.shellyLightsHost;
    lights["onMinute"] = c.targets.lightOnMinute;
    lights["offMinute"] = c.lightOff();
    lights["photoperiodHours"] = c.targets.photoperiodHours;

    JsonObject dosing = doc["dosing"].to<JsonObject>();
    dosing["running"] = c.active.running;
    if (c.active.running) {
        dosing["pump"] = pumpName(c.active.pump);
        dosing["ml"] = c.active.ml;
        dosing["manual"] = c.active.manual;
        dosing["stopAtMs"] = c.active.stopAtMs;
        dosing["id"] = c.active.id;
    } else {
        dosing["pump"] = nullptr;
    }
    const int32_t mixLeft = static_cast<int32_t>(c.mixUntilMs - c.last.nowMs);
    dosing["mixRemainingMs"] = mixLeft > 0 ? mixLeft : 0;
    dosing["block"] = c.lastBlock ? c.lastBlock : "";
    dosing["nextNutrient"] = c.nextNutrientIsB ? "nutrient_b" : "nutrient_a";

    writeFaults(doc["faults"].to<JsonArray>(), c.faults);
}

void writeCalibration(JsonDocument& doc, const Controller& c) {
    JsonArray points = doc["ph"]["points"].to<JsonArray>();
    for (int i = 0; i < kPhPoints; ++i) {
        if (!c.phCal.points[i].set) {
            continue;
        }
        JsonObject point = points.add<JsonObject>();
        point["ph"] = c.phCal.points[i].ph;
        point["volts"] = c.phCal.points[i].volts;
        point["tempC"] = c.phCal.points[i].tempC;
    }
    doc["ph"]["calibrated"] = c.phCalibrated();
    doc["tds"]["calibrated"] = c.tdsCal.calibrated;
    doc["tds"]["scale"] = c.tdsCal.scale;
    doc["tds"]["useFactory"] = c.limits.useFactoryTds;
    JsonArray pumps = doc["pumps"].to<JsonArray>();
    for (int i = 0; i < kPumpCount; ++i) {
        JsonObject pump = pumps.add<JsonObject>();
        pump["pump"] = pumpName(static_cast<PumpId>(i));
        pump["mlPerSec"] = c.pumps[i].mlPerSec;
        pump["calibrated"] = c.pumps[i].calibrated;
        pump["invert"] = c.pumps[i].invert;
        pump["dailyLimitMl"] = c.pumps[i].dailyLimitMl;
        pump["dosedTodayMl"] = c.pumps[i].dosedTodayMl;
    }
}

void writeLimits(JsonObject obj, const Limits& limits) {
    obj["maxDoseMl"] = limits.maxDoseMl;
    obj["minDoseMl"] = limits.minDoseMl;
    obj["maxManualMl"] = limits.maxManualMl;
    obj["phGainMlPerPh"] = limits.phGainMlPerPh;
    obj["ecGainMlPerMs"] = limits.ecGainMlPerMs;
    obj["phDeadband"] = limits.phDeadband;
    obj["ecDeadband"] = limits.ecDeadband;
    obj["nutrientBPerA"] = limits.nutrientBPerA;
    obj["mixLockoutSec"] = limits.mixLockoutMs / 1000;
    obj["sensorStaleSec"] = limits.sensorStaleMs / 1000;
    obj["startupHoldSec"] = limits.startupHoldMs / 1000;
    obj["noFlowLpm"] = limits.noFlowLpm;
    obj["noFlowDelaySec"] = limits.noFlowDelayMs / 1000;
    obj["primeMaxSec"] = limits.primeMaxSec;
}

void writeConfig(JsonDocument& doc, const Controller& c) {
    JsonObject wifi = doc["wifi"].to<JsonObject>();
    wifi["ssid"] = c.wifiSsid;
    wifi["passwordSet"] = c.wifiPassword[0] != '\0';
    wifi["connected"] = c.wifiConnected;
    wifi["ip"] = c.ipAddress;
    wifi["rssi"] = c.wifiRssi;
    wifi["mdns"] = "demeter.local";
    JsonObject shelly = doc["shelly"].to<JsonObject>();
    shelly["pumpHost"] = c.shellyPumpHost;
    shelly["lightsHost"] = c.shellyLightsHost;
    doc["tokenSet"] = c.apiToken[0] != '\0';
    doc["tzOffsetMinutes"] = c.limits.tzOffsetMinutes;
    doc["dstOffsetMinutes"] = c.limits.dstOffsetMinutes;
    doc["floatActiveLow"] = c.limits.floatActiveLow;
    doc["flowPulsesPerLiter"] = c.limits.flowPulsesPerLiter;
    doc["ppmPerMs"] = c.limits.ppmPerMs;
    doc["useFactoryTds"] = c.limits.useFactoryTds;
    doc["sensorMaxEc"] = c.sensorMaxEc();
    doc["mode"] = c.mode == Mode::Service ? "service" : "auto";
    writeLimits(doc["limits"].to<JsonObject>(), c.limits);
}

void writeRing(JsonArray arr, const DoseEvent* items, int count, int next) {
    const int start = count == kDoseLog ? next : 0;
    for (int i = 0; i < count; ++i) {
        const DoseEvent& ev = items[(start + i) % kDoseLog];
        JsonObject obj = arr.add<JsonObject>();
        obj["pump"] = pumpName(static_cast<PumpId>(ev.pump));
        obj["ml"] = ev.ml;
        obj["unix"] = ev.unixTime;
        obj["manual"] = ev.manual;
    }
}

void writeHistory(JsonArray arr, const Controller& c) {
    const int start = c.historyCount == kHistory ? c.historyNext : 0;
    for (int i = 0; i < c.historyCount; ++i) {
        const HistorySample& s = c.history[(start + i) % kHistory];
        JsonObject obj = arr.add<JsonObject>();
        obj["uptimeMs"] = s.uptimeMs;
        obj["unix"] = s.unixTime;
        obj["ph"] = s.ph;
        obj["ecMs"] = s.ecMs;
        obj["waterTempC"] = s.tempC;
        obj["flowLpm"] = s.flowLpm;
        obj["phValid"] = s.phValid;
        obj["ecValid"] = s.ecValid;
    }
}

void writeSchedules(JsonDocument& doc, const Controller& c) {
    ScheduleSpec lights[2];
    const int nLights = lightFallbackSchedules(c.targets, lights, 2);
    JsonArray lightArr = doc["lights"].to<JsonArray>();
    for (int i = 0; i < nLights; ++i) {
        JsonObject obj = lightArr.add<JsonObject>();
        obj["timespec"] = lights[i].timespec;
        obj["on"] = lights[i].on;
        obj["method"] = "Switch.Set";
    }
    ScheduleSpec pump[1];
    const int nPump = pumpFallbackSchedules(pump, 1);
    JsonArray pumpArr = doc["circulation"].to<JsonArray>();
    for (int i = 0; i < nPump; ++i) {
        JsonObject obj = pumpArr.add<JsonObject>();
        obj["timespec"] = pump[i].timespec;
        obj["on"] = pump[i].on;
        obj["method"] = "Switch.Set";
    }
    doc["note"] =
        "Fallback schedules live on the Shelly plugs and keep running if the controller goes offline. "
        "Low-water shutdown of the circulation pump requires the controller to stay online.";
}

void addProfile(JsonArray arr, const Controller& c, const char* name) {
    Targets targets;
    bool builtin = false;
    bool overridden = false;
    if (!c.findProfile(name, targets, builtin, overridden)) {
        return;
    }
    JsonObject item = arr.add<JsonObject>();
    writeTargets(item, targets, c.limits.ppmPerMs);
    item["name"] = name;
    item["builtin"] = builtin;
    item["overridden"] = overridden;
}

bool hostOk(const char* host) {
    if (!host) {
        return false;
    }
    const size_t n = std::strlen(host);
    if (n > 62) {
        return false;
    }
    for (size_t i = 0; i < n; ++i) {
        const unsigned char ch = static_cast<unsigned char>(host[i]);
        if (ch <= 32 || ch == '/' || ch == '"' || ch == '\\') {
            return false;
        }
    }
    return true;
}

bool tokenOk(const char* token) {
    if (!token) {
        return false;
    }
    const size_t n = std::strlen(token);
    if (n < 8 || n > 48) {
        return false;
    }
    for (size_t i = 0; i < n; ++i) {
        const unsigned char ch = static_cast<unsigned char>(token[i]);
        const bool ok = std::isalnum(ch) || ch == '-' || ch == '_' || ch == '.';
        if (!ok) {
            return false;
        }
    }
    return true;
}

float optFloat(JsonObject obj, const char* key, float current, bool& present) {
    if (obj[key].isNull()) {
        return current;
    }
    present = true;
    return obj[key].as<float>();
}

ApiResponse statusResponse(const Controller& c) {
    JsonDocument doc;
    writeStatus(doc, c);
    return okBody(doc);
}

const char* applyConfig(Controller& c, JsonObject body) {
    bool changed = false;
    if (!body["shellyPumpHost"].isNull()) {
        const char* host = body["shellyPumpHost"].as<const char*>();
        if (!host) {
            host = "";
        }
        if (!hostOk(host)) {
            return "invalid_host";
        }
        copyStr(c.shellyPumpHost, sizeof(c.shellyPumpHost), host);
        c.shellyScheduleDirty = true;
        changed = true;
    }
    if (!body["shellyLightsHost"].isNull()) {
        const char* host = body["shellyLightsHost"].as<const char*>();
        if (!host) {
            host = "";
        }
        if (!hostOk(host)) {
            return "invalid_host";
        }
        copyStr(c.shellyLightsHost, sizeof(c.shellyLightsHost), host);
        c.shellyScheduleDirty = true;
        changed = true;
    }
    if (!body["tzOffsetMinutes"].isNull()) {
        const int value = body["tzOffsetMinutes"].as<int>();
        if (value < -12 * 60 || value > 14 * 60) {
            return "invalid_amount";
        }
        c.limits.tzOffsetMinutes = value;
        changed = true;
    }
    if (!body["dstOffsetMinutes"].isNull()) {
        const int value = body["dstOffsetMinutes"].as<int>();
        if (value < 0 || value > 120) {
            return "invalid_amount";
        }
        c.limits.dstOffsetMinutes = value;
        changed = true;
    }
    if (!body["floatActiveLow"].isNull()) {
        c.limits.floatActiveLow = body["floatActiveLow"].as<bool>();
        changed = true;
    }
    if (!body["useFactoryTds"].isNull()) {
        c.limits.useFactoryTds = body["useFactoryTds"].as<bool>();
        changed = true;
    }
    if (!body["flowPulsesPerLiter"].isNull()) {
        const float value = body["flowPulsesPerLiter"].as<float>();
        if (value < 1.f || value > 5000.f) {
            return "invalid_amount";
        }
        c.limits.flowPulsesPerLiter = value;
        changed = true;
    }
    if (!body["ppmPerMs"].isNull()) {
        const float value = body["ppmPerMs"].as<float>();
        if (value < 300.f || value > 800.f) {
            return "invalid_amount";
        }
        if (c.targets.ecMax > (kSensorMaxPpm / value) + 0.001f) {
            return "ec_above_sensor_range";
        }
        c.limits.ppmPerMs = value;
        changed = true;
    }
    bool present = false;
    const float maxDose = optFloat(body, "maxDoseMl", c.limits.maxDoseMl, present);
    const float minDose = optFloat(body, "minDoseMl", c.limits.minDoseMl, present);
    const float phGain = optFloat(body, "phGainMlPerPh", c.limits.phGainMlPerPh, present);
    const float ecGain = optFloat(body, "ecGainMlPerMs", c.limits.ecGainMlPerMs, present);
    const float daily = optFloat(body, "dailyLimitMl", c.pumps[0].dailyLimitMl, present);
    if (present) {
        if (maxDose < 0.2f || maxDose > 20.f || minDose < 0.05f || minDose > maxDose || phGain <= 0.f ||
            phGain > 50.f || ecGain <= 0.f || ecGain > 100.f || daily < 1.f || daily > 500.f) {
            return "invalid_amount";
        }
        c.limits.maxDoseMl = maxDose;
        c.limits.minDoseMl = minDose;
        c.limits.phGainMlPerPh = phGain;
        c.limits.ecGainMlPerMs = ecGain;
        for (int i = 0; i < kPumpCount; ++i) {
            c.pumps[i].dailyLimitMl = daily;
        }
        changed = true;
    }
    if (!body["mixLockoutSec"].isNull()) {
        const int value = body["mixLockoutSec"].as<int>();
        if (value < 5 || value > 3600) {
            return "invalid_amount";
        }
        c.limits.mixLockoutMs = static_cast<uint32_t>(value) * 1000;
        changed = true;
    }
    if (!body["noFlowLpm"].isNull()) {
        const float value = body["noFlowLpm"].as<float>();
        if (value < 0.f || value > 30.f) {
            return "invalid_amount";
        }
        c.limits.noFlowLpm = value;
        changed = true;
    }
    if (!body["noFlowDelaySec"].isNull()) {
        const int value = body["noFlowDelaySec"].as<int>();
        if (value < 3 || value > 300) {
            return "invalid_amount";
        }
        c.limits.noFlowDelayMs = static_cast<uint32_t>(value) * 1000;
        changed = true;
    }
    if (!body["clearApiToken"].isNull() && body["clearApiToken"].as<bool>()) {
        c.apiToken[0] = '\0';
        changed = true;
    } else if (!body["apiToken"].isNull()) {
        const char* token = body["apiToken"].as<const char*>();
        if (!token || token[0] == '\0') {
            c.apiToken[0] = '\0';
        } else if (!tokenOk(token)) {
            return "invalid_amount";
        } else {
            copyStr(c.apiToken, sizeof(c.apiToken), token);
        }
        changed = true;
    }
    if (changed) {
        c.dirty = true;
    }
    return nullptr;
}

bool parseBody(const ApiRequest& req, JsonDocument& doc, JsonObject& obj, ApiResponse& error) {
    if (req.body.empty()) {
        obj = doc.to<JsonObject>();
        return true;
    }
    if (deserializeJson(doc, req.body) || !doc.is<JsonObject>()) {
        error = fail("invalid_json");
        return false;
    }
    obj = doc.as<JsonObject>();
    return true;
}

std::string trimCopy(const char* line) {
    if (!line) {
        return "";
    }
    while (*line == ' ' || *line == '\t' || *line == '\r' || *line == '\n') {
        ++line;
    }
    std::string out(line);
    while (!out.empty() && (out.back() == ' ' || out.back() == '\t' || out.back() == '\r' || out.back() == '\n')) {
        out.pop_back();
    }
    return out;
}

int splitTokens(const std::string& line, std::string* tok, int maxTok) {
    int n = 0;
    size_t i = 0;
    while (i < line.size() && n < maxTok) {
        while (i < line.size() && (line[i] == ' ' || line[i] == '\t')) {
            ++i;
        }
        if (i >= line.size()) {
            break;
        }
        const size_t start = i;
        while (i < line.size() && line[i] != ' ' && line[i] != '\t') {
            ++i;
        }
        tok[n++] = line.substr(start, i - start);
    }
    return n;
}

void upper(std::string& text) {
    for (char& ch : text) {
        ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
    }
}

}  // namespace

const char* kApiHelp =
    "Commands: help, status, readings, targets, faults, doses, history, config, profiles, schedules, "
    "mode auto|service, estop, ack, apply <profile>, set ph <min> <max>, set ec <min> <max>, "
    "set light <hh:mm> <hours>, set shelly pump|lights <host>, dose <pump> <ml>, prime <pump> <seconds>, "
    "pump on|off, lights on|off, cal ph <value>|clear, cal tds <ppm>|clear, cal pump <pump> <mlPerSec>, "
    "wifi <ssid> <password>, token set <token>|clear, sim on|off, sim ph|ec|temp|flow <value>, "
    "sim level ok|low. One JSON object per line is also accepted.";

ApiResponse dispatch(Controller& c, const ApiRequest& req) {
    if (!req.bypassAuth && c.apiToken[0] != '\0' && req.token != c.apiToken) {
        return fail("unauthorized");
    }

    JsonDocument bodyDoc;
    JsonObject body;
    ApiResponse parseError;
    if (!parseBody(req, bodyDoc, body, parseError)) {
        return parseError;
    }

    const std::string& p = req.path;

    if (p == "/api/v1" && isMethod(req, "GET")) {
        JsonDocument doc;
        doc["device"] = kDeviceName;
        doc["version"] = kFirmwareVersion;
        doc["api"] = "/api/v1";
        doc["mdns"] = "demeter.local";
        doc["docs"] = "See firmware/docs/API.md";
        return okBody(doc);
    }
    if (p == "/api/v1/help" && isMethod(req, "GET")) {
        JsonDocument doc;
        doc["help"] = kApiHelp;
        return okBody(doc);
    }
    if (p == "/api/v1/status" && isMethod(req, "GET")) {
        return statusResponse(c);
    }
    if (p == "/api/v1/readings" && isMethod(req, "GET")) {
        JsonDocument doc;
        writeStatus(doc, c);
        JsonDocument out;
        out["readings"] = doc["readings"];
        out["nutrientSensor"] = doc["nutrientSensor"];
        return okBody(out);
    }
    if (p == "/api/v1/targets" && isMethod(req, "GET")) {
        JsonDocument doc;
        writeTargets(doc.to<JsonObject>(), c.targets, c.limits.ppmPerMs);
        return okBody(doc);
    }
    if (p == "/api/v1/targets" && (isMethod(req, "PUT") || isMethod(req, "POST"))) {
        Targets next = c.targets;
        if (const char* error = readTargets(next, body, c.limits.ppmPerMs, true)) {
            return fail(error);
        }
        if (const char* error = c.setTargets(next)) {
            return fail(error);
        }
        JsonDocument doc;
        writeTargets(doc.to<JsonObject>(), c.targets, c.limits.ppmPerMs);
        return okBody(doc);
    }
    if (p == "/api/v1/profiles" && isMethod(req, "GET")) {
        JsonDocument doc;
        JsonArray arr = doc["profiles"].to<JsonArray>();
        addProfile(arr, c, "lettuce");
        addProfile(arr, c, "basil");
        for (int i = 0; i < kCustomProfiles; ++i) {
            if (c.custom[i].used) {
                addProfile(arr, c, c.custom[i].name);
            }
        }
        return okBody(doc);
    }
    if (p == "/api/v1/profiles/apply" && isMethod(req, "POST")) {
        const char* name = body["name"] | "";
        if (const char* error = c.applyProfile(name)) {
            return fail(error);
        }
        JsonDocument doc;
        writeTargets(doc.to<JsonObject>(), c.targets, c.limits.ppmPerMs);
        return okBody(doc);
    }
    if (p.rfind("/api/v1/profiles/", 0) == 0) {
        std::string rest = p.substr(std::strlen("/api/v1/profiles/"));
        bool reset = false;
        const size_t slash = rest.find('/');
        if (slash != std::string::npos) {
            if (rest.substr(slash) != "/reset") {
                return fail("not_found");
            }
            reset = true;
            rest = rest.substr(0, slash);
        }
        if (rest.empty()) {
            return fail("not_found");
        }
        if (reset && isMethod(req, "POST")) {
            if (const char* error = c.resetBuiltin(rest.c_str())) {
                return fail(error);
            }
            return statusResponse(c);
        }
        if (!reset && isMethod(req, "PUT")) {
            Targets next;
            bool builtin = false;
            bool overridden = false;
            if (!c.findProfile(rest.c_str(), next, builtin, overridden)) {
                next = c.targets;
            }
            if (const char* error = readTargets(next, body, c.limits.ppmPerMs, false)) {
                return fail(error);
            }
            if (const char* error = c.saveProfile(rest.c_str(), next)) {
                return fail(error);
            }
            JsonDocument doc;
            writeTargets(doc.to<JsonObject>(), next, c.limits.ppmPerMs);
            doc["name"] = rest.c_str();
            return okBody(doc);
        }
        if (!reset && isMethod(req, "DELETE")) {
            if (const char* error = c.deleteProfile(rest.c_str())) {
                return fail(error);
            }
            JsonDocument doc;
            doc["deleted"] = rest.c_str();
            return okBody(doc);
        }
        return fail("method_not_allowed");
    }
    if (p == "/api/v1/calibration" && isMethod(req, "GET")) {
        JsonDocument doc;
        writeCalibration(doc, c);
        return okBody(doc);
    }
    if (p == "/api/v1/calibration/ph" && isMethod(req, "POST")) {
        if (body["ph"].isNull()) {
            return fail("invalid_calibration");
        }
        const float ph = body["ph"].as<float>();
        const float volts = body["volts"].isNull() ? c.last.sensors.phVolts : body["volts"].as<float>();
        const float temp = body["tempC"].isNull() ? c.last.sensors.waterTempC : body["tempC"].as<float>();
        if (const char* error = c.capturePhPoint(ph, volts, temp)) {
            return fail(error);
        }
        JsonDocument doc;
        writeCalibration(doc, c);
        return okBody(doc);
    }
    if (p == "/api/v1/calibration/ph/clear" && isMethod(req, "POST")) {
        c.clearPhCal();
        JsonDocument doc;
        writeCalibration(doc, c);
        return okBody(doc);
    }
    if (p == "/api/v1/calibration/tds" && isMethod(req, "POST")) {
        if (body["ppm"].isNull()) {
            return fail("invalid_calibration");
        }
        const float ppm = body["ppm"].as<float>();
        const float volts = body["volts"].isNull() ? c.last.sensors.tdsVolts : body["volts"].as<float>();
        const float temp = body["tempC"].isNull() ? c.last.sensors.waterTempC : body["tempC"].as<float>();
        if (const char* error = c.captureTds(ppm, volts, temp)) {
            return fail(error);
        }
        JsonDocument doc;
        writeCalibration(doc, c);
        return okBody(doc);
    }
    if (p == "/api/v1/calibration/tds/clear" && isMethod(req, "POST")) {
        c.clearTdsCal();
        JsonDocument doc;
        writeCalibration(doc, c);
        return okBody(doc);
    }
    if (p == "/api/v1/calibration/pump" && isMethod(req, "POST")) {
        PumpId id;
        if (!pumpFromName(body["pump"] | "", id) || body["mlPerSec"].isNull()) {
            return fail(body["pump"].isNull() ? "invalid_pump" : "invalid_calibration");
        }
        const bool invert = body["invert"].isNull() ? c.pumps[static_cast<int>(id)].invert : body["invert"].as<bool>();
        if (const char* error = c.setPumpCal(id, body["mlPerSec"].as<float>(), invert)) {
            return fail(error);
        }
        JsonDocument doc;
        writeCalibration(doc, c);
        return okBody(doc);
    }
    if (p == "/api/v1/faults" && isMethod(req, "GET")) {
        JsonDocument doc;
        writeFaults(doc["faults"].to<JsonArray>(), c.faults);
        doc["block"] = c.lastBlock ? c.lastBlock : "";
        return okBody(doc);
    }
    if (p == "/api/v1/faults/ack" && isMethod(req, "POST")) {
        c.acknowledgeFaults();
        JsonDocument doc;
        writeFaults(doc["faults"].to<JsonArray>(), c.faults);
        return okBody(doc);
    }
    if (p == "/api/v1/mode" && isMethod(req, "POST")) {
        const char* mode = body["mode"] | "";
        if (std::strcmp(mode, "auto") == 0) {
            c.setMode(Mode::Auto);
        } else if (std::strcmp(mode, "service") == 0) {
            c.setMode(Mode::Service);
        } else {
            return fail("invalid_mode");
        }
        return statusResponse(c);
    }
    if (p == "/api/v1/actions/dose" && isMethod(req, "POST")) {
        PumpId id;
        if (!pumpFromName(body["pump"] | "", id)) {
            return fail("invalid_pump");
        }
        if (const char* error = c.manualDose(id, body["ml"].as<float>(), c.last.nowMs)) {
            return fail(error);
        }
        return statusResponse(c);
    }
    if (p == "/api/v1/actions/prime" && isMethod(req, "POST")) {
        PumpId id;
        if (!pumpFromName(body["pump"] | "", id)) {
            return fail("invalid_pump");
        }
        const uint32_t seconds = body["seconds"].as<uint32_t>();
        if (const char* error = c.manualPrime(id, seconds, c.last.nowMs)) {
            return fail(error);
        }
        return statusResponse(c);
    }
    if (p == "/api/v1/actions/circulation" && isMethod(req, "POST")) {
        if (body["on"].isNull()) {
            return fail("invalid_json");
        }
        if (const char* error = c.manualCirculation(body["on"].as<bool>())) {
            return fail(error);
        }
        return statusResponse(c);
    }
    if (p == "/api/v1/actions/lights" && isMethod(req, "POST")) {
        if (body["on"].isNull()) {
            return fail("invalid_json");
        }
        if (const char* error = c.manualLights(body["on"].as<bool>())) {
            return fail(error);
        }
        return statusResponse(c);
    }
    if (p == "/api/v1/actions/estop" && isMethod(req, "POST")) {
        c.emergencyStop(c.last.nowMs);
        return statusResponse(c);
    }
    if (p == "/api/v1/doses" && isMethod(req, "GET")) {
        JsonDocument doc;
        writeRing(doc["doses"].to<JsonArray>(), c.doses, c.doseCount, c.doseNext);
        return okBody(doc);
    }
    if (p == "/api/v1/history" && isMethod(req, "GET")) {
        JsonDocument doc;
        writeHistory(doc["history"].to<JsonArray>(), c);
        return okBody(doc);
    }
    if (p == "/api/v1/schedules" && isMethod(req, "GET")) {
        JsonDocument doc;
        writeSchedules(doc, c);
        return okBody(doc);
    }
    if (p == "/api/v1/config" && isMethod(req, "GET")) {
        JsonDocument doc;
        writeConfig(doc, c);
        return okBody(doc);
    }
    if (p == "/api/v1/config" && (isMethod(req, "PUT") || isMethod(req, "POST"))) {
        if (const char* error = applyConfig(c, body)) {
            return fail(error);
        }
        JsonDocument doc;
        writeConfig(doc, c);
        return okBody(doc);
    }
    if (p == "/api/v1/wifi" && isMethod(req, "POST")) {
        const char* ssid = body["ssid"] | "";
        const char* password = body["password"] | "";
        if (!ssid[0] || std::strlen(ssid) > 32 || std::strlen(password) > 64) {
            return fail("invalid_amount");
        }
        copyStr(c.wifiSsid, sizeof(c.wifiSsid), ssid);
        copyStr(c.wifiPassword, sizeof(c.wifiPassword), password);
        c.wifiApplyRequested = true;
        c.dirty = true;
        JsonDocument doc;
        doc["ssid"] = c.wifiSsid;
        doc["applyRequested"] = true;
        return okBody(doc);
    }
    if (p == "/api/v1/simulation" && isMethod(req, "POST")) {
        if (!body["enabled"].isNull()) {
            c.simulation.enabled = body["enabled"].as<bool>();
        }
        if (!body["ph"].isNull()) {
            c.simulation.ph = body["ph"].as<float>();
        }
        if (!body["ecMs"].isNull()) {
            c.simulation.ecMs = body["ecMs"].as<float>();
        }
        if (!body["waterTempC"].isNull()) {
            c.simulation.waterTempC = body["waterTempC"].as<float>();
        }
        if (!body["flowLpm"].isNull()) {
            c.simulation.flowLpm = body["flowLpm"].as<float>();
        }
        if (!body["level"].isNull()) {
            const char* level = body["level"] | "";
            if (std::strcmp(level, "low") == 0) {
                c.simulation.waterLow = true;
            } else if (std::strcmp(level, "ok") == 0) {
                c.simulation.waterLow = false;
            } else {
                return fail("invalid_amount");
            }
        }
        JsonDocument doc;
        doc["enabled"] = c.simulation.enabled;
        doc["ph"] = c.simulation.ph;
        doc["ecMs"] = c.simulation.ecMs;
        doc["waterTempC"] = c.simulation.waterTempC;
        doc["flowLpm"] = c.simulation.flowLpm;
        doc["level"] = c.simulation.waterLow ? "low" : "ok";
        doc["persisted"] = false;
        return okBody(doc);
    }

    if (p == "/api/v1/status" || p == "/api/v1/readings" || p == "/api/v1/targets" || p == "/api/v1/profiles" ||
        p == "/api/v1/calibration" || p == "/api/v1/faults" || p == "/api/v1/doses" || p == "/api/v1/history" ||
        p == "/api/v1/config" || p == "/api/v1/schedules" || p == "/api/v1/help" || p == "/api/v1") {
        return fail("method_not_allowed");
    }
    return fail("not_found");
}

bool parseSerialLine(const char* line, ApiRequest& request, std::string& error) {
    request = ApiRequest();
    request.bypassAuth = true;
    const std::string text = trimCopy(line);
    if (text.empty()) {
        error = "empty";
        return false;
    }
    if (text[0] == '{' || (text.rfind("json ", 0) == 0)) {
        const char* json = text[0] == '{' ? text.c_str() : text.c_str() + 5;
        JsonDocument doc;
        if (deserializeJson(doc, json) || !doc.is<JsonObject>()) {
            error = "invalid_json";
            return false;
        }
        request.method = doc["method"] | "GET";
        upper(request.method);
        request.path = doc["path"] | "";
        if (request.path.empty()) {
            error = "invalid_json";
            return false;
        }
        if (!doc["body"].isNull()) {
            serializeJson(doc["body"], request.body);
        }
        return true;
    }

    std::string tok[6];
    const int n = splitTokens(text, tok, 6);
    if (n <= 0) {
        error = "empty";
        return false;
    }
    JsonDocument body;
    const std::string& cmd = tok[0];
    if (cmd == "help") {
        request.method = "GET";
        request.path = "/api/v1/help";
        return true;
    }
    if (cmd == "status" || cmd == "readings" || cmd == "targets" || cmd == "faults" || cmd == "doses" ||
        cmd == "history" || cmd == "config" || cmd == "profiles" || cmd == "schedules") {
        request.method = "GET";
        request.path = std::string("/api/v1/") + (cmd == "faults" ? "faults" : cmd);
        return true;
    }
    if (cmd == "mode" && n == 2 && (tok[1] == "auto" || tok[1] == "service")) {
        request.method = "POST";
        request.path = "/api/v1/mode";
        body["mode"] = tok[1];
        serializeJson(body, request.body);
        return true;
    }
    if (cmd == "estop") {
        request.method = "POST";
        request.path = "/api/v1/actions/estop";
        return true;
    }
    if (cmd == "ack") {
        request.method = "POST";
        request.path = "/api/v1/faults/ack";
        return true;
    }
    if (cmd == "apply" && n == 2) {
        request.method = "POST";
        request.path = "/api/v1/profiles/apply";
        body["name"] = tok[1];
        serializeJson(body, request.body);
        return true;
    }
    if (cmd == "set" && n == 4 && tok[1] == "ph") {
        request.method = "PUT";
        request.path = "/api/v1/targets";
        body["phMin"] = std::atof(tok[2].c_str());
        body["phMax"] = std::atof(tok[3].c_str());
        serializeJson(body, request.body);
        return true;
    }
    if (cmd == "set" && n == 4 && tok[1] == "ec") {
        request.method = "PUT";
        request.path = "/api/v1/targets";
        body["ecMin"] = std::atof(tok[2].c_str());
        body["ecMax"] = std::atof(tok[3].c_str());
        serializeJson(body, request.body);
        return true;
    }
    if (cmd == "set" && n == 4 && tok[1] == "light") {
        request.method = "PUT";
        request.path = "/api/v1/targets";
        body["lightOn"] = tok[2];
        body["photoperiodHours"] = std::atof(tok[3].c_str());
        serializeJson(body, request.body);
        return true;
    }
    if (cmd == "set" && n == 4 && tok[1] == "shelly" && (tok[2] == "pump" || tok[2] == "lights")) {
        request.method = "PUT";
        request.path = "/api/v1/config";
        body[tok[2] == "pump" ? "shellyPumpHost" : "shellyLightsHost"] = tok[3];
        serializeJson(body, request.body);
        return true;
    }
    if ((cmd == "dose" || cmd == "prime") && n == 3) {
        request.method = "POST";
        request.path = cmd == "dose" ? "/api/v1/actions/dose" : "/api/v1/actions/prime";
        body["pump"] = tok[1];
        if (cmd == "dose") {
            body["ml"] = std::atof(tok[2].c_str());
        } else {
            body["seconds"] = std::atoi(tok[2].c_str());
        }
        serializeJson(body, request.body);
        return true;
    }
    if ((cmd == "pump" || cmd == "lights") && n == 2 && (tok[1] == "on" || tok[1] == "off")) {
        request.method = "POST";
        request.path = cmd == "pump" ? "/api/v1/actions/circulation" : "/api/v1/actions/lights";
        body["on"] = tok[1] == "on";
        serializeJson(body, request.body);
        return true;
    }
    if (cmd == "cal" && n >= 3 && tok[1] == "ph") {
        request.method = "POST";
        if (tok[2] == "clear") {
            request.path = "/api/v1/calibration/ph/clear";
        } else {
            request.path = "/api/v1/calibration/ph";
            body["ph"] = std::atof(tok[2].c_str());
            serializeJson(body, request.body);
        }
        return true;
    }
    if (cmd == "cal" && n >= 3 && tok[1] == "tds") {
        request.method = "POST";
        if (tok[2] == "clear") {
            request.path = "/api/v1/calibration/tds/clear";
        } else {
            request.path = "/api/v1/calibration/tds";
            body["ppm"] = std::atof(tok[2].c_str());
            serializeJson(body, request.body);
        }
        return true;
    }
    if (cmd == "cal" && n >= 4 && tok[1] == "pump") {
        request.method = "POST";
        request.path = "/api/v1/calibration/pump";
        body["pump"] = tok[2];
        body["mlPerSec"] = std::atof(tok[3].c_str());
        if (n >= 5 && tok[4] == "invert") {
            body["invert"] = true;
        }
        serializeJson(body, request.body);
        return true;
    }
    if (cmd == "wifi" && n == 3) {
        request.method = "POST";
        request.path = "/api/v1/wifi";
        body["ssid"] = tok[1];
        body["password"] = tok[2];
        serializeJson(body, request.body);
        return true;
    }
    if (cmd == "token" && n == 2 && tok[1] == "clear") {
        request.method = "PUT";
        request.path = "/api/v1/config";
        body["clearApiToken"] = true;
        serializeJson(body, request.body);
        return true;
    }
    if (cmd == "token" && n == 3 && tok[1] == "set") {
        request.method = "PUT";
        request.path = "/api/v1/config";
        body["apiToken"] = tok[2];
        serializeJson(body, request.body);
        return true;
    }
    if (cmd == "sim" && n == 2 && (tok[1] == "on" || tok[1] == "off")) {
        request.method = "POST";
        request.path = "/api/v1/simulation";
        body["enabled"] = tok[1] == "on";
        serializeJson(body, request.body);
        return true;
    }
    if (cmd == "sim" && n == 3 && (tok[1] == "ph" || tok[1] == "ec" || tok[1] == "temp" || tok[1] == "flow")) {
        request.method = "POST";
        request.path = "/api/v1/simulation";
        body["enabled"] = true;
        const char* key = tok[1] == "ph" ? "ph" : tok[1] == "ec" ? "ecMs" : tok[1] == "temp" ? "waterTempC" : "flowLpm";
        body[key] = std::atof(tok[2].c_str());
        serializeJson(body, request.body);
        return true;
    }
    if (cmd == "sim" && n == 3 && tok[1] == "level") {
        request.method = "POST";
        request.path = "/api/v1/simulation";
        body["enabled"] = true;
        body["level"] = tok[2];
        serializeJson(body, request.body);
        return true;
    }
    error = "not_found";
    return false;
}

std::string exportPersisted(const Controller& c) {
    JsonDocument doc;
    doc["v"] = 1;
    doc["mode"] = c.mode == Mode::Service ? "service" : "auto";
    writeTargets(doc["targets"].to<JsonObject>(), c.targets, c.limits.ppmPerMs);
    writeLimits(doc["limits"].to<JsonObject>(), c.limits);
    doc["limits"]["dailyLimitMl"] = c.pumps[0].dailyLimitMl;
    doc["tzOffsetMinutes"] = c.limits.tzOffsetMinutes;
    doc["dstOffsetMinutes"] = c.limits.dstOffsetMinutes;
    doc["floatActiveLow"] = c.limits.floatActiveLow;
    doc["flowPulsesPerLiter"] = c.limits.flowPulsesPerLiter;
    doc["ppmPerMs"] = c.limits.ppmPerMs;
    doc["useFactoryTds"] = c.limits.useFactoryTds;
    JsonArray points = doc["ph"].to<JsonArray>();
    for (int i = 0; i < kPhPoints; ++i) {
        if (!c.phCal.points[i].set) {
            continue;
        }
        JsonObject point = points.add<JsonObject>();
        point["ph"] = c.phCal.points[i].ph;
        point["volts"] = c.phCal.points[i].volts;
        point["tempC"] = c.phCal.points[i].tempC;
    }
    doc["tdsScale"] = c.tdsCal.scale;
    doc["tdsCalibrated"] = c.tdsCal.calibrated;
    JsonArray pumps = doc["pumps"].to<JsonArray>();
    for (int i = 0; i < kPumpCount; ++i) {
        JsonObject pump = pumps.add<JsonObject>();
        pump["mlPerSec"] = c.pumps[i].mlPerSec;
        pump["calibrated"] = c.pumps[i].calibrated;
        pump["invert"] = c.pumps[i].invert;
        pump["dailyLimitMl"] = c.pumps[i].dailyLimitMl;
        pump["dosedTodayMl"] = c.pumps[i].dosedTodayMl;
    }
    doc["wifi"]["ssid"] = c.wifiSsid;
    doc["wifi"]["password"] = c.wifiPassword;
    doc["apiToken"] = c.apiToken;
    doc["shellyPumpHost"] = c.shellyPumpHost;
    doc["shellyLightsHost"] = c.shellyLightsHost;
    doc["doseDay"] = c.doseDay;
    doc["nextNutrientIsB"] = c.nextNutrientIsB;
    if (c.lettuceOverride) {
        writeTargets(doc["lettuce"].to<JsonObject>(), c.lettuceCustom, c.limits.ppmPerMs);
    }
    if (c.basilOverride) {
        writeTargets(doc["basil"].to<JsonObject>(), c.basilCustom, c.limits.ppmPerMs);
    }
    JsonArray custom = doc["custom"].to<JsonArray>();
    for (int i = 0; i < kCustomProfiles; ++i) {
        if (!c.custom[i].used) {
            continue;
        }
        JsonObject item = custom.add<JsonObject>();
        item["name"] = c.custom[i].name;
        writeTargets(item["targets"].to<JsonObject>(), c.custom[i].targets, c.limits.ppmPerMs);
    }
    writeRing(doc["doses"].to<JsonArray>(), c.doses, c.doseCount, c.doseNext);
    std::string out;
    serializeJson(doc, out);
    return out;
}

bool importPersisted(Controller& c, const char* json) {
    JsonDocument doc;
    if (!json || deserializeJson(doc, json) || !doc.is<JsonObject>()) {
        return false;
    }
    std::unique_ptr<Controller> next(new Controller());
    if (!doc["targets"].isNull()) {
        Targets targets = next->targets;
        if (const char* error = readTargets(targets, doc["targets"].as<JsonObject>(), next->limits.ppmPerMs, false)) {
            (void)error;
            return false;
        }
        if (const char* error = next->setTargets(targets)) {
            (void)error;
            return false;
        }
    }
    JsonObject limits = doc["limits"].as<JsonObject>();
    if (!limits.isNull()) {
        if (const char* error = applyConfig(*next, limits)) {
            (void)error;
            return false;
        }
        if (!limits["startupHoldSec"].isNull()) {
            next->limits.startupHoldMs = static_cast<uint32_t>(limits["startupHoldSec"].as<int>()) * 1000;
        }
        if (!limits["sensorStaleSec"].isNull()) {
            next->limits.sensorStaleMs = static_cast<uint32_t>(limits["sensorStaleSec"].as<int>()) * 1000;
        }
    }
    JsonObject extra = doc.as<JsonObject>();
    if (const char* error = applyConfig(*next, extra)) {
        (void)error;
        return false;
    }
    if (!doc["ph"].isNull()) {
        next->phCal = PhCal();
        JsonArray points = doc["ph"].as<JsonArray>();
        int slot = 0;
        for (JsonObject point : points) {
            if (slot >= kPhPoints) {
                break;
            }
            next->phCal.points[slot].set = true;
            next->phCal.points[slot].ph = point["ph"].as<float>();
            next->phCal.points[slot].volts = point["volts"].as<float>();
            next->phCal.points[slot].tempC = point["tempC"].isNull() ? 25.f : point["tempC"].as<float>();
            ++slot;
        }
    }
    if (!doc["tdsCalibrated"].isNull()) {
        next->tdsCal.calibrated = doc["tdsCalibrated"].as<bool>();
        next->tdsCal.scale = doc["tdsScale"].isNull() ? 1.f : doc["tdsScale"].as<float>();
    }
    if (!doc["pumps"].isNull()) {
        int i = 0;
        for (JsonObject pump : doc["pumps"].as<JsonArray>()) {
            if (i >= kPumpCount) {
                break;
            }
            next->pumps[i].mlPerSec = pump["mlPerSec"].isNull() ? 1.f : pump["mlPerSec"].as<float>();
            next->pumps[i].calibrated = pump["calibrated"].as<bool>();
            next->pumps[i].invert = pump["invert"].as<bool>();
            if (!pump["dailyLimitMl"].isNull()) {
                next->pumps[i].dailyLimitMl = pump["dailyLimitMl"].as<float>();
            }
            next->pumps[i].dosedTodayMl = pump["dosedTodayMl"].isNull() ? 0.f : pump["dosedTodayMl"].as<float>();
            ++i;
        }
    }
    if (!doc["wifi"].isNull()) {
        copyStr(next->wifiSsid, sizeof(next->wifiSsid), doc["wifi"]["ssid"] | "");
        copyStr(next->wifiPassword, sizeof(next->wifiPassword), doc["wifi"]["password"] | "");
    }
    const char* mode = doc["mode"] | "auto";
    next->mode = std::strcmp(mode, "service") == 0 ? Mode::Service : Mode::Auto;
    next->doseDay = doc["doseDay"].isNull() ? 0 : doc["doseDay"].as<int>();
    next->nextNutrientIsB = doc["nextNutrientIsB"].as<bool>();
    if (!doc["lettuce"].isNull()) {
        Targets t = lettucePreset();
        if (readTargets(t, doc["lettuce"].as<JsonObject>(), next->limits.ppmPerMs, false) == nullptr) {
            next->saveProfile("lettuce", t);
        }
    }
    if (!doc["basil"].isNull()) {
        Targets t = basilPreset();
        if (readTargets(t, doc["basil"].as<JsonObject>(), next->limits.ppmPerMs, false) == nullptr) {
            next->saveProfile("basil", t);
        }
    }
    if (!doc["custom"].isNull()) {
        for (JsonObject item : doc["custom"].as<JsonArray>()) {
            const char* name = item["name"] | "";
            Targets t = next->targets;
            if (name[0] && !item["targets"].isNull() &&
                readTargets(t, item["targets"].as<JsonObject>(), next->limits.ppmPerMs, false) == nullptr) {
                next->saveProfile(name, t);
            }
        }
    }
    if (!doc["doses"].isNull()) {
        for (JsonObject ev : doc["doses"].as<JsonArray>()) {
            PumpId id;
            if (!pumpFromName(ev["pump"] | "", id)) {
                continue;
            }
            DoseEvent& slot = next->doses[next->doseNext];
            slot.pump = static_cast<uint8_t>(id);
            slot.ml = ev["ml"].as<float>();
            slot.unixTime = ev["unix"].as<int64_t>();
            slot.manual = ev["manual"].as<bool>();
            next->doseNext = (next->doseNext + 1) % kDoseLog;
            if (next->doseCount < kDoseLog) {
                ++next->doseCount;
            }
        }
    }
    next->active.running = false;
    next->dirty = false;
    next->shellyScheduleDirty = true;
    next->wifiApplyRequested = false;
    c = *next;
    return true;
}

}  // namespace demeter
