#pragma once
#include <string>

namespace mmr {

// Stable text description of the current monitor layout (virtual desktop + each monitor
// rectangle and DPI). Stored with each record and compared before playback.
std::string currentLayoutSignature();

}  // namespace mmr
