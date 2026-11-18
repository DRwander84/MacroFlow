#pragma once
#include <QObject>
#include <QString>

#include "SettingsManager.h"
#include "core/ShortcutTypes.h"

namespace mmr {

struct ApplyResult {
    bool ok = false;
    QString error;
};

// Single entry point for changing shortcuts. A new set becomes active (OS registration AND
// disk) only if everything succeeds; otherwise the previous working set stays untouched.
class ShortcutManager : public QObject {
    Q_OBJECT
public:
    ShortcutManager(SettingsManager& settings, IHotkeyBackend& backend, QObject* parent = nullptr);

    // Registers the saved shortcuts at startup. Returns a warning text if some could not be registered.
    QString initialize();

    ApplyResult apply(const BindingMap& wanted);
    ApplyResult resetToDefaults();
    QString check(const BindingMap& wanted) const;  // "" if valid, else the reason (no side effects)

    const BindingMap& active() const { return active_; }
    void setLocked(bool locked) { locked_ = locked; }
    bool isLocked() const { return locked_; }

    // While the user presses keys in the capture box the OS hotkeys are released so e.g. F2 reaches the UI.
    void beginCapture() { backend_.setSuspended(true); }
    void endCapture() { backend_.setSuspended(false); }

signals:
    void bindingsChanged();

private:
    SettingsManager& settings_;
    IHotkeyBackend& backend_;
    BindingMap active_;
    bool locked_ = false;
};

}  // namespace mmr
