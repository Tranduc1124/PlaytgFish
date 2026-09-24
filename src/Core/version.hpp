#pragma once

#include <cstdint>

#define PLAYFISH_VERSION "0.1.0"
#define PLAYFISH_BUILD "__DATE__ __TIME__"

namespace PF {
inline constexpr const char* kVersion = PLAYFISH_VERSION;
inline constexpr const char* kBuild = PLAYFISH_BUILD;
} // namespace PF
