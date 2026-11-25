#include <unity.h>

#include <ArduinoJson.h>

#include "api.h"

#include <cstring>

namespace {

demeter::ApiResponse call(demeter::Controller& c, const char* method, const char* path, const char* body = "") {
    demeter::ApiRequest req;
    req.method = method;
    req.path = path;
    req.body = body ? body : "";
    req.bypassAuth = true;
    return demeter::dispatch(c, req);
}

}  // namespace

void setUp() {}
void tearDown() {}

void test_status_contract_and_presets() {
    demeter::Controller c;
    const demeter::ApiResponse status = call(c, "GET", "/api/v1/status");
    TEST_ASSERT_EQUAL_INT(200, status.status);
    JsonDocument doc;
    TEST_ASSERT_FALSE(deserializeJson(doc, status.body));
    TEST_ASSERT_TRUE(doc["ok"].as<bool>());
    TEST_ASSERT_EQUAL_STRING("auto", doc["mode"].as<const char*>());
    TEST_ASSERT_EQUAL_STRING("demeter.local", doc["wifi"]["mdns"].as<const char*>());
    TEST_ASSERT_FALSE(doc["readings"]["ph"]["value"].isNull());
    TEST_ASSERT_FALSE(doc["readings"]["ec"]["mS"].isNull());
    TEST_ASSERT_FALSE(doc["readings"]["ec"]["ppm"].isNull());
    TEST_ASSERT_FALSE(doc["readings"]["waterTempC"]["value"].isNull());
    TEST_ASSERT_FALSE(doc["readings"]["flowLpm"]["value"].isNull());
    TEST_ASSERT_EQUAL_STRING("ok", doc["readings"]["level"].as<const char*>());
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 1.2f, doc["targets"]["ecMin"].as<float>());
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 6.0f, doc["targets"]["phMin"].as<float>());
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 16.f, doc["targets"]["photoperiodHours"].as<float>());
    TEST_ASSERT_EQUAL_STRING("SEN0244", doc["nutrientSensor"]["model"].as<const char*>());
    TEST_ASSERT_TRUE(doc["faults"].is<JsonArray>());

    const demeter::ApiResponse applied = call(c, "POST", "/api/v1/profiles/apply", "{\"name\":\"basil\"}");
    TEST_ASSERT_EQUAL_INT(200, applied.status);
    JsonDocument basil;
    TEST_ASSERT_FALSE(deserializeJson(basil, applied.body));
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 1.0f, basil["ecMin"].as<float>());
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 5.5f, basil["phMin"].as<float>());
    TEST_ASSERT_EQUAL_STRING("basil", basil["profile"].as<const char*>());

    const demeter::ApiResponse profiles = call(c, "GET", "/api/v1/profiles");
    JsonDocument list;
    deserializeJson(list, profiles.body);
    TEST_ASSERT_EQUAL_STRING("lettuce", list["profiles"][0]["name"].as<const char*>());
    TEST_ASSERT_EQUAL_STRING("basil", list["profiles"][1]["name"].as<const char*>());
}

void test_manual_actions_require_service_and_interlocks() {
    demeter::Controller c;
    c.limits.startupHoldMs = 0;
    TEST_ASSERT_NULL(c.setPumpCal(demeter::PumpId::PhUp, 1.0f, false));
    demeter::Inputs in;
    in.nowMs = 5000;
    in.sensors.waterLow = false;
    in.sensors.phValid = true;
    in.sensors.ecValid = true;
    in.sensors.tempValid = true;
    in.sensors.flowValid = true;
    in.sensors.flowLpm = 3;
    c.tick(in);

    demeter::ApiResponse denied = call(c, "POST", "/api/v1/actions/dose", "{\"pump\":\"ph_up\",\"ml\":1}");
    TEST_ASSERT_EQUAL_INT(409, denied.status);
    JsonDocument err;
    deserializeJson(err, denied.body);
    TEST_ASSERT_EQUAL_STRING("service_mode_required", err["error"].as<const char*>());

    call(c, "POST", "/api/v1/mode", "{\"mode\":\"service\"}");
    demeter::ApiResponse dose = call(c, "POST", "/api/v1/actions/dose", "{\"pump\":\"ph_up\",\"ml\":1}");
    TEST_ASSERT_EQUAL_INT(200, dose.status);
    JsonDocument ok;
    deserializeJson(ok, dose.body);
    TEST_ASSERT_EQUAL_STRING("service", ok["mode"].as<const char*>());
    TEST_ASSERT_TRUE(ok["dosing"]["running"].as<bool>());

    demeter::Readings low = in.sensors;
    low.waterLow = true;
    in.sensors = low;
    in.nowMs = 6000;
    c.tick(in);
    c.emergencyStop(6000);
    demeter::ApiResponse blocked = call(c, "POST", "/api/v1/actions/circulation", "{\"on\":true}");
    TEST_ASSERT_EQUAL_INT(409, blocked.status);
}

void test_target_above_sensor_rejected_and_tds_units_accepted() {
    demeter::Controller c;
    demeter::ApiResponse bad = call(c, "PUT", "/api/v1/targets", "{\"ecMin\":3.0,\"ecMax\":4.0}");
    TEST_ASSERT_EQUAL_INT(400, bad.status);
    demeter::ApiResponse tds = call(c, "PUT", "/api/v1/targets", "{\"tdsMinPpm\":500,\"tdsMaxPpm\":800}");
    TEST_ASSERT_EQUAL_INT(200, tds.status);
    JsonDocument doc;
    deserializeJson(doc, tds.body);
    TEST_ASSERT_FLOAT_WITHIN(0.02f, 1.0f, doc["ecMin"].as<float>());
    TEST_ASSERT_FLOAT_WITHIN(0.02f, 1.6f, doc["ecMax"].as<float>());
}

void test_schedules_and_serial_line() {
    demeter::Controller c;
    const demeter::ApiResponse schedules = call(c, "GET", "/api/v1/schedules");
    JsonDocument doc;
    deserializeJson(doc, schedules.body);
    TEST_ASSERT_TRUE(doc["lights"][0]["on"].as<bool>());
    TEST_ASSERT_EQUAL_STRING("0 0 6 * *", doc["lights"][0]["timespec"].as<const char*>());
    TEST_ASSERT_EQUAL_STRING("0 0 * * *", doc["circulation"][0]["timespec"].as<const char*>());

    demeter::ApiRequest req;
    std::string error;
    TEST_ASSERT_TRUE(demeter::parseSerialLine("apply basil", req, error));
    TEST_ASSERT_EQUAL_STRING("POST", req.method.c_str());
    TEST_ASSERT_EQUAL_STRING("/api/v1/profiles/apply", req.path.c_str());
    const demeter::ApiResponse applied = demeter::dispatch(c, req);
    TEST_ASSERT_EQUAL_INT(200, applied.status);

    TEST_ASSERT_TRUE(demeter::parseSerialLine(
        "{\"method\":\"PUT\",\"path\":\"/api/v1/targets\",\"body\":{\"phMin\":5.8,\"phMax\":6.2}}", req, error));
    const demeter::ApiResponse put = demeter::dispatch(c, req);
    JsonDocument body;
    deserializeJson(body, put.body);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 5.8f, body["phMin"].as<float>());
}

void test_config_round_trip_hides_secrets() {
    demeter::Controller c;
    TEST_ASSERT_NULL(c.capturePhPoint(7.f, 1.5f, 25.f));
    TEST_ASSERT_NULL(c.capturePhPoint(4.f, 2.0f, 25.f));
    TEST_ASSERT_NULL(c.setPumpCal(demeter::PumpId::NutrientA, 1.25f, true));
    call(c, "POST", "/api/v1/wifi", "{\"ssid\":\"grow\",\"password\":\"secret-pass\"}");
    call(c, "PUT", "/api/v1/config",
         "{\"shellyPumpHost\":\"192.168.1.40\",\"shellyLightsHost\":\"192.168.1.41\",\"apiToken\":\"token-1234\"}");
    const std::string saved = demeter::exportPersisted(c);
    TEST_ASSERT_TRUE(saved.size() < 3800);
    TEST_ASSERT_TRUE(saved.find("secret-pass") != std::string::npos);

    const demeter::ApiResponse config = call(c, "GET", "/api/v1/config");
    TEST_ASSERT_TRUE(config.body.find("secret-pass") == std::string::npos);
    TEST_ASSERT_TRUE(config.body.find("token-1234") == std::string::npos);
    JsonDocument pub;
    deserializeJson(pub, config.body);
    TEST_ASSERT_TRUE(pub["wifi"]["passwordSet"].as<bool>());
    TEST_ASSERT_TRUE(pub["tokenSet"].as<bool>());

    demeter::Controller restored;
    TEST_ASSERT_TRUE(demeter::importPersisted(restored, saved.c_str()));
    TEST_ASSERT_EQUAL_STRING("grow", restored.wifiSsid);
    TEST_ASSERT_EQUAL_STRING("secret-pass", restored.wifiPassword);
    TEST_ASSERT_EQUAL_STRING("192.168.1.40", restored.shellyPumpHost);
    TEST_ASSERT_TRUE(restored.phCalibrated());
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 1.25f, restored.pumps[2].mlPerSec);
    TEST_ASSERT_TRUE(restored.pumps[2].invert);
    TEST_ASSERT_FALSE(restored.active.running);

    demeter::ApiRequest authed;
    authed.method = "GET";
    authed.path = "/api/v1/status";
    authed.bypassAuth = false;
    authed.token = "nope";
    TEST_ASSERT_EQUAL_INT(401, demeter::dispatch(restored, authed).status);
    authed.token = "token-1234";
    TEST_ASSERT_EQUAL_INT(200, demeter::dispatch(restored, authed).status);
}

void test_unknown_path_and_bad_json() {
    demeter::Controller c;
    TEST_ASSERT_EQUAL_INT(404, call(c, "GET", "/api/v1/missing").status);
    TEST_ASSERT_EQUAL_INT(400, call(c, "PUT", "/api/v1/targets", "{").status);
    TEST_ASSERT_EQUAL_INT(405, call(c, "DELETE", "/api/v1/status").status);
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_status_contract_and_presets);
    RUN_TEST(test_manual_actions_require_service_and_interlocks);
    RUN_TEST(test_target_above_sensor_rejected_and_tds_units_accepted);
    RUN_TEST(test_schedules_and_serial_line);
    RUN_TEST(test_config_round_trip_hides_secrets);
    RUN_TEST(test_unknown_path_and_bad_json);
    return UNITY_END();
}
