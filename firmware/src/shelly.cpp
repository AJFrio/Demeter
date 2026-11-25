#include "board.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFi.h>

#include <cstdio>
#include <string>

namespace {

bool gPumpReachable = false;
bool gLightsReachable = false;
bool gPumpOn = false;
bool gLightsOn = false;

bool rpc(const char* host, const JsonDocument& request, JsonDocument& response) {
    if (host == nullptr || host[0] == '\0' || WiFi.status() != WL_CONNECTED) {
        return false;
    }
    std::string body;
    serializeJson(request, body);
    HTTPClient http;
    const String url = String("http://") + host + "/rpc";
    if (!http.begin(url)) {
        return false;
    }
    http.setTimeout(400);
    http.addHeader("Content-Type", "application/json");
    const int code = http.POST(body.c_str());
    const String payload = http.getString();
    http.end();
    if (code < 200 || code >= 300) {
        return false;
    }
    deserializeJson(response, payload);
    return true;
}

bool switchSet(const char* host, bool on) {
    JsonDocument request;
    request["id"] = 1;
    request["method"] = "Switch.Set";
    request["params"]["id"] = 0;
    request["params"]["on"] = on;
    JsonDocument response;
    return rpc(host, request, response);
}

bool switchGet(const char* host, bool& on) {
    JsonDocument request;
    request["id"] = 1;
    request["method"] = "Switch.GetStatus";
    request["params"]["id"] = 0;
    JsonDocument response;
    if (!rpc(host, request, response)) {
        return false;
    }
    if (!response["result"]["output"].isNull()) {
        on = response["result"]["output"].as<bool>();
    }
    return true;
}

void formatPosixTz(const demeter::Limits& limits, char* out, size_t n) {
    const int westMinutes = -(limits.tzOffsetMinutes + limits.dstOffsetMinutes);
    const int sign = westMinutes < 0 ? -1 : 1;
    const int absMinutes = westMinutes < 0 ? -westMinutes : westMinutes;
    const int hours = sign * (absMinutes / 60);
    const int minutes = absMinutes % 60;
    if (minutes == 0) {
        std::snprintf(out, n, "UTC%d", hours);
    } else {
        std::snprintf(out, n, "UTC%d:%02d", hours, minutes);
    }
}

bool setTimezone(const char* host, const demeter::Limits& limits) {
    char tz[24];
    formatPosixTz(limits, tz, sizeof(tz));
    JsonDocument request;
    request["id"] = 1;
    request["method"] = "Sys.SetConfig";
    request["params"]["config"]["location"]["tz"] = tz;
    JsonDocument response;
    return rpc(host, request, response);
}

bool installSchedules(const char* host, const demeter::ScheduleSpec* specs, int count, const demeter::Limits& limits) {
    if (!setTimezone(host, limits)) {
        return false;
    }
    JsonDocument wipe;
    wipe["id"] = 1;
    wipe["method"] = "Schedule.DeleteAll";
    wipe["params"].to<JsonObject>();
    JsonDocument wipeResponse;
    if (!rpc(host, wipe, wipeResponse)) {
        return false;
    }
    for (int i = 0; i < count; ++i) {
        JsonDocument create;
        create["id"] = i + 2;
        create["method"] = "Schedule.Create";
        create["params"]["enable"] = true;
        create["params"]["timespec"] = specs[i].timespec;
        JsonObject call = create["params"]["calls"].to<JsonArray>().add<JsonObject>();
        call["method"] = "Switch.Set";
        call["params"]["id"] = 0;
        call["params"]["on"] = specs[i].on;
        JsonDocument response;
        if (!rpc(host, create, response)) {
            return false;
        }
    }
    return true;
}

}  // namespace

bool shellyPumpReachable() { return gPumpReachable; }
bool shellyLightsReachable() { return gLightsReachable; }
bool shellyPumpOn() { return gPumpOn; }
bool shellyLightsOn() { return gLightsOn; }

void shellyBegin() {}

void shellyPoll(demeter::Controller& controller) {
    static uint32_t lastPoll = 0;
    static uint32_t lastSchedule = 0;
    if (WiFi.status() != WL_CONNECTED) {
        gPumpReachable = false;
        gLightsReachable = false;
        return;
    }
    const uint32_t now = millis();
    if (now - lastPoll >= 2000) {
        lastPoll = now;
        if (controller.shellyPumpHost[0] != '\0') {
            gPumpReachable = switchSet(controller.shellyPumpHost, controller.outputs.circulation);
            if (gPumpReachable) {
                gPumpOn = controller.outputs.circulation;
            }
        } else {
            gPumpReachable = false;
            gPumpOn = false;
        }
        if (controller.shellyLightsHost[0] != '\0') {
            if (!controller.outputs.commandLights) {
                gLightsReachable = switchGet(controller.shellyLightsHost, gLightsOn);
            } else {
                gLightsReachable = switchSet(controller.shellyLightsHost, controller.outputs.lights);
                if (gLightsReachable) {
                    gLightsOn = controller.outputs.lights;
                }
            }
        } else {
            gLightsReachable = false;
            gLightsOn = false;
        }
    }

    const bool due = controller.shellyScheduleDirty && (now - lastSchedule > 5000 || lastSchedule == 0);
    if (!due) {
        return;
    }
    lastSchedule = now;
    bool ok = true;
    if (controller.shellyPumpHost[0] != '\0') {
        demeter::ScheduleSpec specs[1];
        const int n = demeter::pumpFallbackSchedules(specs, 1);
        ok = installSchedules(controller.shellyPumpHost, specs, n, controller.limits) && ok;
    }
    if (controller.shellyLightsHost[0] != '\0') {
        demeter::ScheduleSpec specs[2];
        const int n = demeter::lightFallbackSchedules(controller.targets, specs, 2);
        ok = installSchedules(controller.shellyLightsHost, specs, n, controller.limits) && ok;
    }
    if (ok) {
        controller.shellyScheduleDirty = false;
    }
}
