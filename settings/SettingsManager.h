#pragma once
#include <QPoint>
#include <QString>
#include <vector>

#include "core/ShortcutTypes.h"

namespace mmr {

struct GeneralSettings {
    bool startWithWindows = false;  // default OFF
    bool restoreLastTab = true;
    bool confirmDelete = true;
    bool showClickOverlay = false;  // accuracy-test markers on screen, default OFF
    bool recordMouse = true;        // recordings capture mouse movement / buttons / wheel
    bool recordKeyboard = false;    // recordings capture every key (off by default: keystrokes can be sensitive)
    bool operator==(const GeneralSettings&) const = default;
};

// Everything the Auto Clicker tab configures. Click points are physical virtual-desktop pixels
// (exactly what SetCursorPos takes); only the first `locationCount` of them are used.
struct AutoClickSettings {
    static constexpr int kMaxLocations = 20;
    static constexpr int kMaxClicks = 99'999'999;
    bool infinite = true;
    int clickCount = 100;
    int locationCount = 1;
    std::vector<QPoint> points;
    bool noDelay = false;
    int intervalValue = 1;
    int intervalUnit = 1;  // 0 = milliseconds, 1 = seconds, 2 = minutes, 3 = hours
    int button = 0;        // 0 = left, 1 = right, 2 = middle
    bool doubleClick = false;
    int locationGapMs = 50;  // pause between two locations of the same round
    int jitterPercent = 0;   // random +/- variation of the interval
    bool restoreCursor = true;
    bool minimizeOnStart = true;
    bool operator==(const AutoClickSettings&) const = default;
};

// QSettings (INI) in the standard per-user app-config folder. Loads defensively (anything
// invalid falls back to defaults) and writes only when a value actually changed.
class SettingsManager {
public:
    static QString defaultPath();
    explicit SettingsManager(QString filePath);

    const BindingMap& shortcuts() const { return bindings_; }
    const GeneralSettings& general() const { return general_; }
    int lastTab() const { return lastTab_; }
    const AutoClickSettings& autoClick() const { return autoClick_; }
    bool recoveredFromProblem() const { return recovered_; }

    bool setShortcuts(const BindingMap& b, QString* error);   // on failure in-memory value is unchanged
    bool setGeneral(const GeneralSettings& g, QString* error);
    bool setLastTab(int tab, QString* error = nullptr);
    bool setAutoClick(const AutoClickSettings& a, QString* error);  // values are clamped to their valid ranges

private:
    void load();
    bool write(QString* error);
    QString path_;
    BindingMap bindings_;
    GeneralSettings general_;
    AutoClickSettings autoClick_;
    int lastTab_ = 0;
    bool recovered_ = false;
    bool needsRewrite_ = false;  // first run or old-format file: persist the clean current format once
};

}  // namespace mmr
