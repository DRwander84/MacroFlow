#pragma once
#include <QString>

namespace mmr {
// Per-user "start with Windows" (HKCU Run key). Only launches the app; never runs a macro.
bool setAutoStart(bool enabled, QString* error);
bool isAutoStartEnabled();
}  // namespace mmr
