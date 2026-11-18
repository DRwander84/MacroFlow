#include "AutoStart.h"

#include <QCoreApplication>
#include <QDir>
#include <QSettings>

namespace mmr {
namespace {
const char* kRunKey = "HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run";
const char* kValue = "MacroFlow";
const char* kLegacyValue = "MouseMacroRecorder";  // old name: remove so it never launches twice
}  // namespace

bool setAutoStart(bool enabled, QString* error) {
    QSettings reg(kRunKey, QSettings::NativeFormat);
    reg.remove(kLegacyValue);
    if (enabled)
        reg.setValue(kValue, "\"" + QDir::toNativeSeparators(QCoreApplication::applicationFilePath()) + "\"");
    else
        reg.remove(kValue);
    reg.sync();
    if (reg.status() != QSettings::NoError) {
        if (error) *error = "Could not update the Windows startup entry.";
        return false;
    }
    return true;
}

bool isAutoStartEnabled() {
    QSettings reg(kRunKey, QSettings::NativeFormat);
    return reg.contains(kValue);
}

}  // namespace mmr
