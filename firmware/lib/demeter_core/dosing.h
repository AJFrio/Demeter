#pragma once

#include "types.h"

namespace demeter {

struct DosePlan {
    bool start = false;
    PumpId pump = PumpId::PhUp;
    float ml = 0.f;
    const char* block = nullptr;
};

struct DoseContext {
    const Targets* targets = nullptr;
    const Limits* limits = nullptr;
    const Readings* readings = nullptr;
    const PumpCal* pumps = nullptr;
    bool phCalibrated = false;
    bool nutrientReady = false;
    bool mixing = false;
    bool pumpBusy = false;
    bool circulationConfirmed = false;
    bool startupHold = false;
    bool nextNutrientIsB = false;
};

DosePlan planAutoDose(const DoseContext& ctx);

const char* manualDoseBlock(Mode mode, const Readings& readings, const PumpCal& pump, const Limits& limits,
                            float ml, bool pumpBusy, float maxMl);

float clampDose(float ml, float minMl, float maxMl, float remaining);

}  // namespace demeter
