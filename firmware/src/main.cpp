#include "board.h"
#include "pins.h"

#include <Arduino.h>
#include <esp_task_wdt.h>

#include <ctime>

demeter::Controller gCtrl;

namespace {

void fillTime(demeter::Inputs& inputs) {
    const time_t now = time(nullptr);
    inputs.timeSynced = now > 1700000000;
    inputs.unixTime = static_cast<int64_t>(now);
    if (!inputs.timeSynced) {
        inputs.localMinute = 0;
        inputs.localYyyymmdd = 0;
        return;
    }
    int year = 0;
    int month = 0;
    int day = 0;
    int minute = 0;
    const int offset = gCtrl.limits.tzOffsetMinutes + gCtrl.limits.dstOffsetMinutes;
    demeter::civilFromUnix(static_cast<int64_t>(now), offset, year, month, day, minute);
    inputs.localMinute = minute;
    inputs.localYyyymmdd = demeter::yyyymmddFromCivil(year, month, day);
}

void startWatchdog() {
#if ESP_ARDUINO_VERSION >= ESP_ARDUINO_VERSION_VAL(3, 0, 0)
    esp_task_wdt_config_t config = {};
    config.timeout_ms = 15000;
    config.trigger_panic = true;
    esp_task_wdt_reconfigure(&config);
#else
    esp_task_wdt_init(15, true);
#endif
    esp_task_wdt_add(nullptr);
}

}  // namespace

void setup() {
    Serial.begin(115200);
    delay(50);
    halBegin();
    startWatchdog();
    netBegin(gCtrl);
    shellyBegin();
    Serial.println("{\"ok\":true,\"event\":\"boot\",\"version\":\"1.0.0\",\"board\":\"adafruit_feather_esp32_v2\"}");
}

void loop() {
    esp_task_wdt_reset();
    const uint32_t now = millis();
    digitalWrite(kPinLed, (now / (gCtrl.wifiConnected ? 1000 : 200)) % 2 == 0 ? HIGH : LOW);

    halPoll(gCtrl);
    netPoll(gCtrl);

    demeter::Inputs inputs;
    inputs.nowMs = now;
    inputs.sensors = halReadings(gCtrl);
    fillTime(inputs);
    inputs.shellyPumpConfigured = gCtrl.shellyPumpHost[0] != '\0';
    inputs.shellyLightsConfigured = gCtrl.shellyLightsHost[0] != '\0';
    inputs.shellyPumpReachable = shellyPumpReachable();
    inputs.shellyLightsReachable = shellyLightsReachable();
    inputs.circulationConfirmed = shellyPumpOn();
    inputs.lightsConfirmed = shellyLightsOn();

    gCtrl.tick(inputs);
    halApplyOutputs(gCtrl);
    shellyPoll(gCtrl);
}
