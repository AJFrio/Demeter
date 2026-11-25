#include "presets.h"

namespace demeter {

Targets lettucePreset() {
    Targets t;
    t.phMin = 6.0f;
    t.phMax = 7.0f;
    t.ecMin = 1.2f;
    t.ecMax = 1.8f;
    t.lightOnMinute = 6 * 60;
    t.photoperiodHours = 16.f;
    copyStr(t.profile, sizeof(t.profile), "lettuce");
    return t;
}

Targets basilPreset() {
    Targets t;
    t.phMin = 5.5f;
    t.phMax = 6.0f;
    t.ecMin = 1.0f;
    t.ecMax = 1.6f;
    t.lightOnMinute = 6 * 60;
    t.photoperiodHours = 16.f;
    copyStr(t.profile, sizeof(t.profile), "basil");
    return t;
}

bool builtinPreset(const char* name, Targets& out) {
    if (!name) {
        return false;
    }
    if (std::strcmp(name, "lettuce") == 0) {
        out = lettucePreset();
        return true;
    }
    if (std::strcmp(name, "basil") == 0) {
        out = basilPreset();
        return true;
    }
    return false;
}

}  // namespace demeter
