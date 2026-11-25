#include "dosing.h"

namespace demeter {
namespace {

float clamp(float v, float lo, float hi) {
    if (v < lo) {
        return lo;
    }
    if (v > hi) {
        return hi;
    }
    return v;
}

bool pumpHasRoom(const PumpCal& pump, const Limits& limits, float& ml) {
    if (pump.mlPerSec < 0.05f) {
        return false;
    }
    const float remaining = pump.dailyLimitMl - pump.dosedTodayMl;
    if (remaining < limits.minDoseMl) {
        return false;
    }
    ml = clamp(ml, limits.minDoseMl, limits.maxDoseMl);
    if (ml > remaining) {
        ml = remaining;
    }
    return ml >= limits.minDoseMl;
}

DosePlan nutrientDose(const DoseContext& ctx, float ml) {
    DosePlan plan;
    const int first = ctx.nextNutrientIsB ? 3 : 2;
    const int second = ctx.nextNutrientIsB ? 2 : 3;
    const int order[2] = {first, second};
    bool sawLimit = false;
    bool sawUncal = false;
    for (int i = 0; i < 2; ++i) {
        const PumpCal& pump = ctx.pumps[order[i]];
        float amount = ml;
        if (order[i] == static_cast<int>(PumpId::NutrientB)) {
            amount *= ctx.limits->nutrientBPerA;
        }
        if (!pump.calibrated) {
            sawUncal = true;
            continue;
        }
        if (!pumpHasRoom(pump, *ctx.limits, amount)) {
            sawLimit = true;
            continue;
        }
        plan.start = true;
        plan.pump = static_cast<PumpId>(order[i]);
        plan.ml = amount;
        return plan;
    }
    plan.block = sawLimit && !sawUncal ? "daily_limit" : "pump_uncalibrated";
    return plan;
}

DosePlan phDose(const DoseContext& ctx, PumpId id, float ml) {
    DosePlan plan;
    const PumpCal& pump = ctx.pumps[static_cast<int>(id)];
    if (!pump.calibrated) {
        plan.block = "pump_uncalibrated";
        return plan;
    }
    if (!pumpHasRoom(pump, *ctx.limits, ml)) {
        plan.block = "daily_limit";
        return plan;
    }
    plan.start = true;
    plan.pump = id;
    plan.ml = ml;
    return plan;
}

}  // namespace

float clampDose(float ml, float minMl, float maxMl, float remaining) {
    if (ml > remaining) {
        ml = remaining;
    }
    if (ml > maxMl) {
        ml = maxMl;
    }
    if (ml < minMl) {
        return 0.f;
    }
    return ml;
}

const char* manualDoseBlock(Mode mode, const Readings& readings, const PumpCal& pump, const Limits& limits,
                            float ml, bool pumpBusy, float maxMl) {
    if (mode != Mode::Service) {
        return "service_mode_required";
    }
    if (readings.waterLow) {
        return "low_water";
    }
    if (pumpBusy) {
        return "pump_busy";
    }
    if (pump.mlPerSec < 0.05f) {
        return "pump_not_configured";
    }
    if (!(ml > 0.f) || ml > maxMl) {
        return "invalid_amount";
    }
    const float remaining = pump.dailyLimitMl - pump.dosedTodayMl;
    if (ml > remaining + 0.001f) {
        return "daily_limit";
    }
    (void)limits;
    return nullptr;
}

DosePlan planAutoDose(const DoseContext& ctx) {
    DosePlan none;
    if (!ctx.targets || !ctx.limits || !ctx.readings || !ctx.pumps) {
        none.block = "internal";
        return none;
    }
    if (ctx.startupHold) {
        none.block = "startup";
        return none;
    }
    if (ctx.readings->waterLow) {
        none.block = "low_water";
        return none;
    }
    if (ctx.pumpBusy) {
        none.block = "pump_busy";
        return none;
    }
    if (ctx.mixing) {
        none.block = "mixing";
        return none;
    }
    if (!ctx.circulationConfirmed) {
        none.block = "circulation_unconfirmed";
        return none;
    }
    if (!ctx.nutrientReady) {
        none.block = "tds_uncalibrated";
        return none;
    }

    const Readings& r = *ctx.readings;
    const Targets& t = *ctx.targets;
    const Limits& lim = *ctx.limits;
    const float ecLo = t.ecMin - lim.ecDeadband;
    const float ecHi = t.ecMax + lim.ecDeadband;

    if (!r.ecValid) {
        none.block = "ec_invalid";
        return none;
    }

    if (!r.ecSaturated && r.ecMs < ecLo) {
        const float err = t.ecMin - r.ecMs;
        float ml = err * lim.ecGainMlPerMs;
        if (ml < lim.minDoseMl) {
            ml = lim.minDoseMl;
        }
        DosePlan planned = nutrientDose(ctx, ml);
        if (planned.start || planned.block) {
            return planned;
        }
    }

    if (!r.phValid) {
        none.block = r.ecMs > ecHi ? "ec_high" : "ph_invalid";
        return none;
    }
    if (!ctx.phCalibrated) {
        none.block = "ph_uncalibrated";
        return none;
    }

    const float phLo = t.phMin - lim.phDeadband;
    const float phHi = t.phMax + lim.phDeadband;
    if (r.ph > phHi) {
        float ml = (r.ph - t.phMax) * lim.phGainMlPerPh;
        if (ml < lim.minDoseMl) {
            ml = lim.minDoseMl;
        }
        DosePlan planned = phDose(ctx, PumpId::PhDown, ml);
        if (!planned.start && r.ecMs > ecHi && planned.block == nullptr) {
            planned.block = "ec_high";
        }
        return planned;
    }
    if (r.ph < phLo) {
        float ml = (t.phMin - r.ph) * lim.phGainMlPerPh;
        if (ml < lim.minDoseMl) {
            ml = lim.minDoseMl;
        }
        return phDose(ctx, PumpId::PhUp, ml);
    }

    if (r.ecSaturated) {
        none.block = "ec_saturated";
        return none;
    }
    if (r.ecMs > ecHi) {
        none.block = "ec_high";
        return none;
    }
    none.block = nullptr;
    return none;
}

}  // namespace demeter
