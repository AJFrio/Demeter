#pragma once

#include "types.h"

namespace demeter {

Targets lettucePreset();
Targets basilPreset();
bool builtinPreset(const char* name, Targets& out);

}  // namespace demeter
