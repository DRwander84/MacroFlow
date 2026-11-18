#include "GlobalHotkeys.h"

#include <QCoreApplication>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

namespace mmr {

GlobalHotkeys::GlobalHotkeys(QObject* parent) : QObject(parent) {
    QCoreApplication::instance()->installNativeEventFilter(this);
}

GlobalHotkeys::~GlobalHotkeys() {
    unregisterAll();
    if (auto* app = QCoreApplication::instance()) app->removeNativeEventFilter(this);
}

void GlobalHotkeys::unregisterAll() {
    for (const auto& r : regs_) UnregisterHotKey(nullptr, r.id);
    regs_.clear();
}

bool GlobalHotkeys::registerSet(const BindingMap& bindings, std::string* error) {
    std::vector<Reg> groups;
    for (const auto& [action, combo] : bindings) {
        if (combo.isNone()) continue;
        bool placed = false;
        for (auto& g : groups)
            if (g.combo == combo) { g.actions.push_back(action); placed = true; break; }
        if (!placed) groups.push_back({0, combo, {action}});
    }
    for (auto& g : groups) {
        g.id = nextId_++;
        if (!RegisterHotKey(nullptr, g.id, g.combo.mods | MOD_NOREPEAT, g.combo.vk)) {
            const DWORD err = GetLastError();
            if (error) {
                *error = err == ERROR_HOTKEY_ALREADY_REGISTERED
                             ? toString(g.combo) + " is already in use by Windows or another application."
                             : "Windows refused " + toString(g.combo) + " (error " + std::to_string(err) + ").";
            }
            for (const auto& done : regs_) UnregisterHotKey(nullptr, done.id);
            regs_.clear();
            return false;
        }
        regs_.push_back(g);
    }
    return true;
}

bool GlobalHotkeys::apply(const BindingMap& bindings, std::string* error) {
    unregisterAll();
    if (!suspended_ && !registerSet(bindings, error)) {
        registerSet(current_, nullptr);  // roll back: previous working shortcuts stay active
        return false;
    }
    current_ = bindings;
    return true;
}

void GlobalHotkeys::setSuspended(bool suspended) {
    if (suspended == suspended_) return;
    suspended_ = suspended;
    if (suspended_) unregisterAll();
    else registerSet(current_, nullptr);
}

bool GlobalHotkeys::nativeEventFilter(const QByteArray& eventType, void* message, qintptr*) {
    if (eventType != "windows_generic_MSG" && eventType != "windows_dispatcher_MSG") return false;
    const MSG* msg = static_cast<const MSG*>(message);
    if (msg->message != WM_HOTKEY) return false;
    const int id = static_cast<int>(msg->wParam);
    for (const auto& r : regs_) {
        if (r.id != id) continue;
        for (ShortcutAction a : r.actions)
            QMetaObject::invokeMethod(this, [this, a] { emit triggered(int(a)); }, Qt::QueuedConnection);
        return true;
    }
    return false;
}

}  // namespace mmr
