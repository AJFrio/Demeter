#pragma once

// Adafruit ESP32 Feather V2. Analog sensors stay on ADC1 so Wi-Fi can run.
// Pump PPM pins are outputs that are idle at reset (not GPIO 12/14/15).

constexpr int kPinPh = 34;        // A2, ADC1, input only
constexpr int kPinTds = 39;       // A3, ADC1, input only
constexpr int kPinTemp = 32;      // D32, DS18B20 data, 4.7k to 3.3V
constexpr int kPinFloat = 33;     // D33, float switch to GND
constexpr int kPinFlow = 27;      // D27, SEN0217, 3.3V pull-up
constexpr int kPinPumpPhUp = 4;   // A5
constexpr int kPinPumpPhDown = 25;  // A1
constexpr int kPinPumpA = 26;     // A0
constexpr int kPinPumpB = 21;     // MI
constexpr int kPinLed = 13;

constexpr int kPumpPins[4] = {kPinPumpPhUp, kPinPumpPhDown, kPinPumpA, kPinPumpB};
