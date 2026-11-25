#include "board.h"
#include "pins.h"

#include <Arduino.h>
#include <DallasTemperature.h>
#include <OneWire.h>
#include <esp_timer.h>

namespace {

OneWire gOneWire(kPinTemp);
DallasTemperature gTemps(&gOneWire);
bool gTempWaiting = false;
uint32_t gTempAskedAt = 0;
float gTempC = 25.f;
bool gTempValid = false;
uint32_t gTempAt = 0;

float gPhSamples[9];
float gTdsSamples[9];
int gSampleCount = 0;
int gSampleNext = 0;
uint32_t gLastAdcMs = 0;
uint32_t gFilteredAt = 0;
float gPhVolts = 0.f;
float gTdsVolts = 0.f;
bool gFiltered = false;

volatile uint32_t gFlowPulses = 0;
uint32_t gFlowWindowAt = 0;
uint32_t gFlowWindowPulses = 0;
float gFlowLpm = 0.f;
bool gFlowValid = false;

bool gFloatStable = false;
bool gFloatLast = false;
uint32_t gFloatChangedAt = 0;

portMUX_TYPE gPumpMux = portMUX_INITIALIZER_UNLOCKED;
esp_timer_handle_t gDoseTimer = nullptr;
bool gDoseTimerOn = false;
uint32_t gArmedDoseId = 0;

void IRAM_ATTR onFlowPulse() { gFlowPulses++; }

uint32_t pulseDuty(uint32_t pulseUs) {
    if (pulseUs > 2500) {
        pulseUs = 2500;
    }
    return static_cast<uint32_t>((static_cast<uint64_t>(pulseUs) * 65535ULL) / 20000ULL);
}

void writeChannel(int channel, int pin, uint32_t pulseUs) {
    const uint32_t duty = pulseDuty(pulseUs);
    portENTER_CRITICAL(&gPumpMux);
#if ESP_ARDUINO_VERSION >= ESP_ARDUINO_VERSION_VAL(3, 0, 0)
    (void)channel;
    ledcWrite(pin, duty);
#else
    (void)pin;
    ledcWrite(channel, duty);
#endif
    portEXIT_CRITICAL(&gPumpMux);
}

void stopPumpsFromTimer(void*) {
    for (int i = 0; i < 4; ++i) {
        writeChannel(i, kPumpPins[i], demeter::kPumpStopUs);
    }
    gDoseTimerOn = false;
}

void ensureTimer() {
    if (gDoseTimer != nullptr) {
        return;
    }
    esp_timer_create_args_t args = {};
    args.callback = stopPumpsFromTimer;
    args.name = "dose";
    esp_timer_create(&args, &gDoseTimer);
}

void armDose(uint32_t durationMs) {
    ensureTimer();
    if (gDoseTimer == nullptr) {
        return;
    }
    esp_timer_stop(gDoseTimer);
    if (durationMs < 1) {
        durationMs = 1;
    }
    esp_timer_start_once(gDoseTimer, static_cast<uint64_t>(durationMs) * 1000ULL);
    gDoseTimerOn = true;
}

}  // namespace

void halBegin() {
    analogReadResolution(12);
    analogSetPinAttenuation(kPinPh, ADC_11db);
    analogSetPinAttenuation(kPinTds, ADC_11db);
    pinMode(kPinFloat, INPUT_PULLUP);
    pinMode(kPinFlow, INPUT_PULLUP);
    pinMode(kPinLed, OUTPUT);
    attachInterrupt(digitalPinToInterrupt(kPinFlow), onFlowPulse, RISING);

    for (int i = 0; i < 4; ++i) {
        pinMode(kPumpPins[i], OUTPUT);
        digitalWrite(kPumpPins[i], LOW);
#if ESP_ARDUINO_VERSION >= ESP_ARDUINO_VERSION_VAL(3, 0, 0)
        ledcAttach(kPumpPins[i], 50, 16);
#else
        ledcSetup(i, 50, 16);
        ledcAttachPin(kPumpPins[i], i);
#endif
        writeChannel(i, kPumpPins[i], demeter::kPumpStopUs);
    }
    ensureTimer();

    gTemps.begin();
    gTemps.setWaitForConversion(false);
    gTemps.requestTemperatures();
    gTempWaiting = true;
    gTempAskedAt = millis();
    gFlowWindowAt = millis();
    gFlowWindowPulses = gFlowPulses;
}

void halPoll(const demeter::Controller& controller) {
    const uint32_t now = millis();
    if (now - gLastAdcMs >= 50) {
        gLastAdcMs = now;
        gPhSamples[gSampleNext] = analogReadMilliVolts(kPinPh) / 1000.f;
        gTdsSamples[gSampleNext] = analogReadMilliVolts(kPinTds) / 1000.f;
        gSampleNext = (gSampleNext + 1) % 9;
        if (gSampleCount < 9) {
            ++gSampleCount;
        }
        if (gSampleCount >= 5) {
            float phCopy[9];
            float tdsCopy[9];
            const int start = gSampleCount == 9 ? gSampleNext : 0;
            for (int i = 0; i < gSampleCount; ++i) {
                const int idx = (start + i) % 9;
                phCopy[i] = gPhSamples[idx];
                tdsCopy[i] = gTdsSamples[idx];
            }
            gPhVolts = demeter::median(phCopy, gSampleCount);
            gTdsVolts = demeter::median(tdsCopy, gSampleCount);
            gFiltered = true;
            gFilteredAt = now;
        }
    }

    if (gTempWaiting && now - gTempAskedAt >= 800) {
        const float temp = gTemps.getTempCByIndex(0);
        if (temp > -10.f && temp < 60.f) {
            gTempC = temp;
            gTempValid = true;
            gTempAt = now;
        } else {
            gTempValid = false;
        }
        gTemps.requestTemperatures();
        gTempAskedAt = now;
    }

    if (now - gFlowWindowAt >= 1000) {
        const uint32_t pulses = gFlowPulses;
        const uint32_t delta = pulses - gFlowWindowPulses;
        const uint32_t dt = now - gFlowWindowAt;
        const float ppl = controller.limits.flowPulsesPerLiter < 1.f ? 450.f : controller.limits.flowPulsesPerLiter;
        gFlowLpm = (static_cast<float>(delta) / ppl) * (60000.f / static_cast<float>(dt));
        gFlowValid = true;
        gFlowWindowPulses = pulses;
        gFlowWindowAt = now;
    }

    const bool rawLow = digitalRead(kPinFloat) == LOW;
    const bool active = controller.limits.floatActiveLow ? rawLow : !rawLow;
    if (active != gFloatLast) {
        gFloatLast = active;
        gFloatChangedAt = now;
    } else if (now - gFloatChangedAt >= 200) {
        gFloatStable = active;
    }
}

demeter::Readings halReadings(const demeter::Controller& controller) {
    demeter::Readings readings;
    const uint32_t now = millis();
    if (controller.simulation.enabled) {
        readings.ph = controller.simulation.ph;
        readings.phValid = readings.ph > 0.5f && readings.ph < 13.5f;
        readings.phReason = readings.phValid ? "simulation" : "simulation_invalid";
        readings.phVolts = 0.f;
        readings.ecMs = controller.simulation.ecMs;
        readings.tdsPpm = demeter::ecToPpm(readings.ecMs, controller.limits.ppmPerMs);
        readings.ecUs = readings.ecMs * 1000.f;
        readings.ecSaturated = readings.tdsPpm >= 990.f;
        readings.ecValid = readings.ecMs >= 0.f && readings.ecMs <= controller.sensorMaxEc() + 0.05f;
        readings.ecReason = readings.ecValid ? "simulation" : "simulation_invalid";
        readings.waterTempC = controller.simulation.waterTempC;
        readings.tempValid = readings.waterTempC > 0.f && readings.waterTempC < 60.f;
        readings.flowLpm = controller.simulation.flowLpm;
        readings.flowValid = true;
        readings.waterLow = controller.simulation.waterLow;
        return readings;
    }

    const bool tempFresh = gTempValid && (now - gTempAt) <= controller.limits.sensorStaleMs;
    readings.waterTempC = gTempC;
    readings.tempValid = tempFresh;
    readings.tempAgeMs = gTempValid ? now - gTempAt : 0;
    const float tempForCal = tempFresh ? gTempC : 25.f;

    readings.phVolts = gPhVolts;
    readings.tdsVolts = gTdsVolts;
    const bool fresh = gFiltered && (now - gFilteredAt) <= controller.limits.sensorStaleMs;
    readings.phAgeMs = gFiltered ? now - gFilteredAt : 0;
    readings.ecAgeMs = readings.phAgeMs;

    float ph = 0.f;
    const bool phMath = demeter::phFromVoltage(controller.phCal, gPhVolts, tempForCal, ph);
    readings.ph = phMath ? ph : 0.f;
    if (!controller.phCalibrated()) {
        readings.phReason = "ph_uncalibrated";
    } else if (!gFiltered) {
        readings.phReason = "no_sample";
    } else if (!fresh) {
        readings.phReason = "stale";
    } else if (gPhVolts < 0.05f || gPhVolts > 3.05f) {
        readings.phReason = "voltage_range";
    } else if (!tempFresh) {
        readings.phReason = "temp_invalid";
    } else if (!phMath || ph < 0.5f || ph > 13.5f) {
        readings.phReason = "out_of_range";
    } else {
        readings.phReason = "ok";
        readings.phValid = true;
    }

    const float ppm = demeter::tdsPpmFromVoltage(gTdsVolts, tempForCal) * controller.tdsCal.scale;
    readings.tdsPpm = ppm;
    readings.ecMs = demeter::ppmToEc(ppm, controller.limits.ppmPerMs);
    readings.ecUs = readings.ecMs * 1000.f;
    readings.ecSaturated = ppm >= 990.f || gTdsVolts >= 2.25f;
    const bool trusted = controller.tdsCal.calibrated || controller.limits.useFactoryTds;
    if (!gFiltered) {
        readings.ecReason = "no_sample";
    } else if (!fresh) {
        readings.ecReason = "stale";
    } else if (gTdsVolts < 0.02f || gTdsVolts > 2.45f) {
        readings.ecReason = "voltage_range";
    } else if (!tempFresh) {
        readings.ecReason = "temp_invalid";
    } else if (ppm > demeter::kSensorMaxPpm + 20.f) {
        readings.ecReason = "out_of_range";
    } else if (!trusted) {
        readings.ecReason = "tds_uncalibrated";
    } else {
        readings.ecReason = readings.ecSaturated ? "saturated" : "ok";
        readings.ecValid = !readings.ecSaturated;
    }

    readings.flowLpm = gFlowLpm;
    readings.flowValid = gFlowValid;
    readings.waterLow = gFloatStable;
    return readings;
}

void halApplyOutputs(demeter::Controller& controller) {
    if (!controller.active.running) {
        if (gArmedDoseId != 0) {
            if (gDoseTimer) {
                esp_timer_stop(gDoseTimer);
            }
            for (int i = 0; i < 4; ++i) {
                writeChannel(i, kPumpPins[i], demeter::kPumpStopUs);
            }
            gArmedDoseId = 0;
            gDoseTimerOn = false;
        }
        return;
    }
    if (gArmedDoseId == controller.active.id) {
        return;
    }
    const int index = static_cast<int>(controller.active.pump);
    const uint32_t pulse = demeter::dosePulseUs(controller.pumps[index].invert);
    for (int i = 0; i < 4; ++i) {
        writeChannel(i, kPumpPins[i], i == index ? pulse : demeter::kPumpStopUs);
    }
    const int32_t remain = static_cast<int32_t>(controller.active.stopAtMs - millis());
    armDose(remain > 0 ? static_cast<uint32_t>(remain) : 1);
    gArmedDoseId = controller.active.id;
}
