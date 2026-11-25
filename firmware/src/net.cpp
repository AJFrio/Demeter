#include "board.h"

#include <Arduino.h>
#include <ESPmDNS.h>
#include <Preferences.h>
#include <WebServer.h>
#include <WiFi.h>

#include <string>

namespace {

WebServer gServer(80);
bool gMdns = false;
bool gAp = false;
bool gStaStarted = false;
uint32_t gStaBegan = 0;
int gAppliedTz = 99999;
int gAppliedDst = 99999;
uint32_t gSaveAfter = 0;

const char* kPortalPage =
    "<!doctype html><html><head><meta name=viewport content=\"width=device-width,initial-scale=1\">"
    "<title>Demeter setup</title></head><body>"
    "<h1>Demeter controller</h1>"
    "<p>Live API: <a href=\"/api/v1/status\">/api/v1/status</a>. "
    "On the home network the host is <code>demeter.local</code>.</p>"
    "<form method=\"POST\" action=\"/setup/wifi\">"
    "<p><label>Wi-Fi name <input name=\"ssid\" maxlength=\"32\" required></label></p>"
    "<p><label>Password <input name=\"password\" type=\"password\" maxlength=\"64\"></label></p>"
    "<p><button type=\"submit\">Save and connect</button></p>"
    "</form>"
    "<p>Setup AP: Demeter-Setup / demeter-setup</p>"
    "</body></html>";

const char* methodName(HTTPMethod method) {
    switch (method) {
        case HTTP_POST:
            return "POST";
        case HTTP_PUT:
            return "PUT";
        case HTTP_DELETE:
            return "DELETE";
        case HTTP_GET:
        default:
            return "GET";
    }
}

void sendJson(int code, const std::string& body) {
    gServer.sendHeader("Access-Control-Allow-Origin", "*");
    gServer.sendHeader("Access-Control-Allow-Methods", "GET, POST, PUT, DELETE, OPTIONS");
    gServer.sendHeader("Access-Control-Allow-Headers", "Content-Type, Authorization, X-Api-Key");
    gServer.send(code, "application/json", body.c_str());
}

void loadState(demeter::Controller& controller) {
    Preferences prefs;
    if (!prefs.begin("demeter", true)) {
        return;
    }
    const String saved = prefs.getString("state", "");
    prefs.end();
    if (saved.length() > 0 && !demeter::importPersisted(controller, saved.c_str())) {
        Serial.println("{\"ok\":false,\"error\":\"config_reset\"}");
    }
}

void saveState(demeter::Controller& controller) {
    const std::string blob = demeter::exportPersisted(controller);
    Preferences prefs;
    if (!prefs.begin("demeter", false)) {
        return;
    }
    prefs.putString("state", blob.c_str());
    prefs.end();
    controller.dirty = false;
}

void applyClock(const demeter::Controller& controller) {
    if (gAppliedTz == controller.limits.tzOffsetMinutes && gAppliedDst == controller.limits.dstOffsetMinutes) {
        return;
    }
    configTime(controller.limits.tzOffsetMinutes * 60, controller.limits.dstOffsetMinutes * 60, "pool.ntp.org",
               "time.nist.gov");
    gAppliedTz = controller.limits.tzOffsetMinutes;
    gAppliedDst = controller.limits.dstOffsetMinutes;
}

void handlePortal() { gServer.send(200, "text/html", kPortalPage); }

void handleSetup(demeter::Controller& controller) {
    const bool apUp = WiFi.getMode() == WIFI_AP || WiFi.getMode() == WIFI_AP_STA;
    if (!apUp && controller.apiToken[0] != '\0') {
        sendJson(401, "{\"ok\":false,\"error\":\"unauthorized\"}");
        return;
    }
    const String ssid = gServer.arg("ssid");
    const String password = gServer.arg("password");
    if (ssid.length() == 0 || ssid.length() > 32 || password.length() > 64) {
        gServer.send(400, "text/plain", "SSID is required.");
        return;
    }
    demeter::copyStr(controller.wifiSsid, sizeof(controller.wifiSsid), ssid.c_str());
    demeter::copyStr(controller.wifiPassword, sizeof(controller.wifiPassword), password.c_str());
    controller.wifiApplyRequested = true;
    controller.dirty = true;
    gServer.send(200, "text/html", "<p>Saved. The controller is joining that network.</p>");
}

void handleApi(demeter::Controller& controller) {
    if (gServer.method() == HTTP_OPTIONS) {
        sendJson(204, "");
        return;
    }
    demeter::ApiRequest request;
    request.method = methodName(gServer.method());
    request.path = gServer.uri().c_str();
    request.body = gServer.arg("plain").c_str();
    String token = gServer.header("X-Api-Key");
    const String auth = gServer.header("Authorization");
    if (token.length() == 0 && auth.startsWith("Bearer ")) {
        token = auth.substring(7);
    }
    request.token = token.c_str();
    const demeter::ApiResponse response = demeter::dispatch(controller, request);
    sendJson(response.status, response.body);
}

void handleSerial(demeter::Controller& controller) {
    static char line[512];
    static size_t used = 0;
    while (Serial.available() > 0) {
        const char ch = static_cast<char>(Serial.read());
        if (ch != '\n' && ch != '\r') {
            if (used + 1 < sizeof(line)) {
                line[used++] = ch;
            }
            continue;
        }
        line[used] = '\0';
        used = 0;
        demeter::ApiRequest request;
        std::string error;
        if (!demeter::parseSerialLine(line, request, error)) {
            if (error != "empty") {
                Serial.println("{\"ok\":false,\"error\":\"not_found\"}");
            }
            continue;
        }
        const demeter::ApiResponse response = demeter::dispatch(controller, request);
        Serial.println(response.body.c_str());
    }
}

}  // namespace

void netBegin(demeter::Controller& controller) {
    loadState(controller);
    const char* headerKeys[] = {"Authorization", "X-Api-Key"};
    gServer.collectHeaders(headerKeys, 2);
    gServer.on("/", HTTP_GET, handlePortal);
    gServer.on("/setup/wifi", HTTP_POST, [&controller]() { handleSetup(controller); });
    gServer.onNotFound([&controller]() {
        if (gServer.uri().startsWith("/api/")) {
            handleApi(controller);
            return;
        }
        handlePortal();
    });
    gServer.begin();
    Serial.println("{\"ok\":true,\"event\":\"api_ready\",\"path\":\"/api/v1/status\"}");
}

void netPoll(demeter::Controller& controller) {
    if (controller.wifiApplyRequested) {
        controller.wifiApplyRequested = false;
        WiFi.mode(WIFI_STA);
        WiFi.setHostname("demeter");
        WiFi.begin(controller.wifiSsid, controller.wifiPassword);
        gStaStarted = true;
        gStaBegan = millis();
        gAp = false;
        gMdns = false;
    }

    if (WiFi.status() == WL_CONNECTED) {
        controller.wifiConnected = true;
        demeter::copyStr(controller.ipAddress, sizeof(controller.ipAddress), WiFi.localIP().toString().c_str());
        controller.wifiRssi = WiFi.RSSI();
        if (!gMdns) {
            if (MDNS.begin("demeter")) {
                MDNS.addService("http", "tcp", 80);
                MDNS.addService("demeter", "tcp", 80);
                gMdns = true;
            }
        }
        applyClock(controller);
        if (gAp && millis() - gStaBegan > 30000) {
            WiFi.softAPdisconnect(true);
            WiFi.mode(WIFI_STA);
            gAp = false;
        }
    } else {
        controller.wifiConnected = false;
        controller.ipAddress[0] = '\0';
        controller.wifiRssi = 0;
        gMdns = false;
        if (!gStaStarted && controller.wifiSsid[0] != '\0') {
            WiFi.mode(WIFI_STA);
            WiFi.setHostname("demeter");
            WiFi.begin(controller.wifiSsid, controller.wifiPassword);
            gStaStarted = true;
            gStaBegan = millis();
        }
        const bool giveUp = controller.wifiSsid[0] == '\0' || (gStaStarted && millis() - gStaBegan > 20000);
        if (giveUp && !gAp) {
            WiFi.mode(controller.wifiSsid[0] ? WIFI_AP_STA : WIFI_AP);
            WiFi.softAP("Demeter-Setup", "demeter-setup");
            gAp = true;
            Serial.println("{\"ok\":true,\"event\":\"setup_ap\",\"ssid\":\"Demeter-Setup\"}");
        }
    }

    gServer.handleClient();
    handleSerial(controller);

    if (controller.dirty) {
        if (gSaveAfter == 0) {
            gSaveAfter = millis() + 2000;
        } else if (static_cast<int32_t>(millis() - gSaveAfter) >= 0) {
            saveState(controller);
            gSaveAfter = 0;
        }
    } else {
        gSaveAfter = 0;
    }
}
