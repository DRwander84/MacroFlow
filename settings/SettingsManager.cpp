#include "SettingsManager.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSettings>
#include <QStandardPaths>
#include <QStringList>

namespace mmr {
namespace {

int clampInt(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

// Keeps every Auto Clicker value inside its valid range (a hand-edited INI can never break the engine).
AutoClickSettings sanitized(AutoClickSettings a) {
    a.clickCount = clampInt(a.clickCount, 1, AutoClickSettings::kMaxClicks);
    a.locationCount = clampInt(a.locationCount, 1, AutoClickSettings::kMaxLocations);
    a.intervalUnit = clampInt(a.intervalUnit, 0, 3);
    static const int kUnitMax[4] = {3'600'000, 86'400, 1'440, 168};
    a.intervalValue = clampInt(a.intervalValue, 1, kUnitMax[a.intervalUnit]);
    a.button = clampInt(a.button, 0, 2);
    a.locationGapMs = clampInt(a.locationGapMs, 0, 60'000);
    a.jitterPercent = clampInt(a.jitterPercent, 0, 50);
    if (a.points.size() > size_t(AutoClickSettings::kMaxLocations)) a.points.resize(AutoClickSettings::kMaxLocations);
    return a;
}

QString pointsToString(const std::vector<QPoint>& pts) {
    QStringList parts;
    for (const QPoint& p : pts) parts << QString("%1,%2").arg(p.x()).arg(p.y());
    return parts.join(';');
}

std::vector<QPoint> pointsFromString(const QString& text) {
    std::vector<QPoint> out;
    for (const QString& part : text.split(';', Qt::SkipEmptyParts)) {
        const QStringList xy = part.split(',');
        bool okX = false, okY = false;
        const int x = xy.size() == 2 ? xy[0].trimmed().toInt(&okX) : 0;
        const int y = xy.size() == 2 ? xy[1].trimmed().toInt(&okY) : 0;
        if (okX && okY && out.size() < size_t(AutoClickSettings::kMaxLocations)) out.emplace_back(x, y);
    }
    return out;
}

}  // namespace

QString SettingsManager::defaultPath() {
    return QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation) + "/settings.ini";
}

SettingsManager::SettingsManager(QString filePath) : path_(std::move(filePath)) {
    load();
    if (needsRewrite_) write(nullptr);  // first run: the defaults are stored once, then belong to the user
}

void SettingsManager::load() {
    bindings_ = defaultBindings();
    general_ = {};
    autoClick_ = {};
    lastTab_ = 0;
    QDir().mkpath(QFileInfo(path_).absolutePath());
    if (!QFile::exists(path_)) { needsRewrite_ = true; return; }

    QSettings s(path_, QSettings::IniFormat);
    s.setFallbacksEnabled(false);
    if (s.status() != QSettings::NoError) {  // unparsable file: keep a copy, start clean
        recovered_ = true;
        QFile::rename(path_, path_ + ".bad-" + QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss"));
        return;
    }

    BindingMap loaded = defaultBindings();
    bool bad = false;
    std::vector<ShortcutAction> addedActions;  // actions that older settings files do not know yet
    s.beginGroup("shortcuts");
    // Old versions stored removed actions and called "Start / Stop Playback" stopPlayback.
    if (s.contains("stopRecording") || s.contains("playSelected") || s.contains("stopPlayback")) needsRewrite_ = true;
    for (int i = 0; i < int(ShortcutAction::Count); ++i) {
        const auto a = ShortcutAction(i);
        QString key = actionKey(a);
        if (!s.contains(key) && a == ShortcutAction::StartStopPlayback && s.contains("stopPlayback")) key = "stopPlayback";
        if (!s.contains(key)) {
            if (a == ShortcutAction::AutoClickStartStop || a == ShortcutAction::AutoClickPauseResume) addedActions.push_back(a);
            continue;
        }
        const auto c = keyComboFromString(s.value(key).toString().toStdString());
        if (!c || !validateCombo(*c).empty()) { bad = true; continue; }
        loaded[a] = *c;
    }
    s.endGroup();
    // Upgrade from a version without the Auto Clicker shortcuts: give them their default keys, unless the user
    // already uses that key for something else (then the next free F-key, or unassigned). Never resets the user's set.
    for (const ShortcutAction a : addedActions) {
        const KeyCombo want = loaded[a];
        loaded[a] = {};
        auto used = [&](const KeyCombo& k) {
            for (const auto& kv : loaded) if (kv.second == k) return true;
            return false;
        };
        KeyCombo pick = used(want) ? KeyCombo{} : want;
        for (uint32_t vk = 0x75; pick.isNone() && vk <= 0x7A; ++vk)  // F6..F11
            if (!used({0, vk})) pick = {0, vk};
        loaded[a] = pick;
        needsRewrite_ = true;
    }
    if (!validateBindings(loaded).empty()) { loaded = defaultBindings(); bad = true; }
    bindings_ = loaded;

    s.beginGroup("general");
    general_.startWithWindows = s.value("startWithWindows", false).toBool();
    general_.restoreLastTab = s.value("restoreLastTab", true).toBool();
    general_.confirmDelete = s.value("confirmDelete", true).toBool();
    general_.showClickOverlay = s.value("showClickOverlay", false).toBool();
    general_.recordMouse = s.value("recordMouse", true).toBool();
    general_.recordKeyboard = s.value("recordKeyboard", false).toBool();
    s.endGroup();

    s.beginGroup("autoclicker");
    AutoClickSettings ac;
    ac.infinite = s.value("infinite", true).toBool();
    ac.clickCount = s.value("clickCount", 100).toInt();
    ac.locationCount = s.value("locationCount", 1).toInt();
    ac.points = pointsFromString(s.value("points").toString());
    ac.noDelay = s.value("noDelay", false).toBool();
    ac.intervalValue = s.value("intervalValue", 1).toInt();
    ac.intervalUnit = s.value("intervalUnit", 1).toInt();
    ac.button = s.value("button", 0).toInt();
    ac.doubleClick = s.value("doubleClick", false).toBool();
    ac.locationGapMs = s.value("locationGapMs", 50).toInt();
    ac.jitterPercent = s.value("jitterPercent", 0).toInt();
    ac.restoreCursor = s.value("restoreCursor", true).toBool();
    ac.minimizeOnStart = s.value("minimizeOnStart", true).toBool();
    s.endGroup();
    autoClick_ = sanitized(ac);

    bool ok = false;
    const int tab = s.value("ui/lastTab", 0).toInt(&ok);
    lastTab_ = (ok && tab >= 0 && tab <= 3) ? tab : 0;
    if (!ok) bad = true;
    recovered_ = bad;
}

bool SettingsManager::write(QString* error) {
    QSettings s(path_, QSettings::IniFormat);
    s.setFallbacksEnabled(false);
    s.clear();  // drop every previously stored value: the file always holds exactly the current settings
    for (const auto& [a, c] : bindings_) s.setValue(QString("shortcuts/") + actionKey(a), QString::fromStdString(toString(c)));
    s.setValue("general/startWithWindows", general_.startWithWindows);
    s.setValue("general/restoreLastTab", general_.restoreLastTab);
    s.setValue("general/confirmDelete", general_.confirmDelete);
    s.setValue("general/showClickOverlay", general_.showClickOverlay);
    s.setValue("general/recordMouse", general_.recordMouse);
    s.setValue("general/recordKeyboard", general_.recordKeyboard);
    s.setValue("autoclicker/infinite", autoClick_.infinite);
    s.setValue("autoclicker/clickCount", autoClick_.clickCount);
    s.setValue("autoclicker/locationCount", autoClick_.locationCount);
    s.setValue("autoclicker/points", pointsToString(autoClick_.points));
    s.setValue("autoclicker/noDelay", autoClick_.noDelay);
    s.setValue("autoclicker/intervalValue", autoClick_.intervalValue);
    s.setValue("autoclicker/intervalUnit", autoClick_.intervalUnit);
    s.setValue("autoclicker/button", autoClick_.button);
    s.setValue("autoclicker/doubleClick", autoClick_.doubleClick);
    s.setValue("autoclicker/locationGapMs", autoClick_.locationGapMs);
    s.setValue("autoclicker/jitterPercent", autoClick_.jitterPercent);
    s.setValue("autoclicker/restoreCursor", autoClick_.restoreCursor);
    s.setValue("autoclicker/minimizeOnStart", autoClick_.minimizeOnStart);
    s.setValue("ui/lastTab", lastTab_);
    s.sync();  // QSettings writes through a temp file + rename
    if (s.status() != QSettings::NoError) {
        if (error) *error = "Could not write the settings file (" + path_ + ").";
        return false;
    }
    return true;
}

bool SettingsManager::setShortcuts(const BindingMap& b, QString* error) {
    const std::string v = validateBindings(b);
    if (!v.empty()) { if (error) *error = QString::fromStdString(v); return false; }
    if (b == bindings_) return true;  // nothing changed: no disk write
    const BindingMap old = bindings_;
    bindings_ = b;
    if (!write(error)) { bindings_ = old; return false; }
    return true;
}

bool SettingsManager::setGeneral(const GeneralSettings& g, QString* error) {
    if (g == general_) return true;
    const GeneralSettings old = general_;
    general_ = g;
    if (!write(error)) { general_ = old; return false; }
    return true;
}

bool SettingsManager::setLastTab(int tab, QString* error) {
    if (tab < 0 || tab > 3 || tab == lastTab_) return true;
    const int old = lastTab_;
    lastTab_ = tab;
    if (!write(error)) { lastTab_ = old; return false; }
    return true;
}

bool SettingsManager::setAutoClick(const AutoClickSettings& a, QString* error) {
    const AutoClickSettings clean = sanitized(a);
    if (clean == autoClick_) return true;
    const AutoClickSettings old = autoClick_;
    autoClick_ = clean;
    if (!write(error)) { autoClick_ = old; return false; }
    return true;
}

}  // namespace mmr
