#include "ShortcutManager.h"

#include <QStringList>

namespace mmr {

ShortcutManager::ShortcutManager(SettingsManager& settings, IHotkeyBackend& backend, QObject* parent)
    : QObject(parent), settings_(settings), backend_(backend) {}

QString ShortcutManager::initialize() {
    std::string err;
    if (backend_.apply(settings_.shortcuts(), &err)) {
        active_ = settings_.shortcuts();
        return {};
    }
    // Something else owns one of the keys: activate everything we can, report the rest.
    QStringList failed;
    BindingMap partial;
    for (const auto& [a, c] : settings_.shortcuts()) {
        BindingMap trial = partial;
        trial[a] = c;
        std::string e;
        if (c.isNone() || backend_.apply(trial, &e)) partial = trial;
        else failed << QString("%1 (%2)").arg(QString::fromLatin1(actionLabel(a)), QString::fromStdString(toString(c)));
    }
    backend_.apply(partial, &err);
    active_ = partial;
    return "These shortcuts are used by another program and are inactive: " + failed.join(", ") +
           ". Pick different keys in Settings.";
}

QString ShortcutManager::check(const BindingMap& wanted) const {
    return QString::fromStdString(validateBindings(wanted));
}

ApplyResult ShortcutManager::apply(const BindingMap& wanted) {
    if (locked_) return {false, "Shortcuts can't be changed while recording or playing. Stop first."};
    const QString invalid = check(wanted);
    if (!invalid.isEmpty()) return {false, invalid};

    std::string err;
    if (!backend_.apply(wanted, &err))  // backend rolls back by itself
        return {false, QString::fromStdString(err) + " Your previous shortcuts were kept."};

    QString saveErr;
    if (!settings_.setShortcuts(wanted, &saveErr)) {
        std::string ignore;
        backend_.apply(active_, &ignore);  // undo OS registration, disk unchanged
        return {false, saveErr + " Your previous shortcuts were kept."};
    }
    active_ = wanted;
    emit bindingsChanged();
    return {true, {}};
}

ApplyResult ShortcutManager::resetToDefaults() { return apply(defaultBindings()); }

}  // namespace mmr
