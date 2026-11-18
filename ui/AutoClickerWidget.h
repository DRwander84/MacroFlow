#pragma once
#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QIntValidator>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QWidget>
#include <memory>

#include "ClickPointPicker.h"
#include "core/AutoClicker.h"
#include "core/MacroController.h"
#include "settings/SettingsManager.h"
#include "settings/ShortcutManager.h"

namespace mmr {

// The "Auto Clicker" tab: Start in the middle, the click settings below it. Every setting is saved as soon as
// it is changed, so the global Start / Stop shortcut always runs exactly what this page shows.
class AutoClickerWidget : public QWidget {
    Q_OBJECT
public:
    AutoClickerWidget(AutoClicker& clicker, MacroController& controller, SettingsManager& settings,
                      ShortcutManager& shortcuts, QWidget* parent = nullptr);
    ~AutoClickerWidget() override;

    bool requestStart();  // Start button and the Start / Stop shortcut; false if it could not start (a notice says why)

signals:
    void notice(const QString& text);

protected:
    void hideEvent(QHideEvent* e) override;

private:
    AutoClickSettings stage() const;
    static int readInt(const QLineEdit* e, int fallback, int lo, int hi);
    void loadFromSettings();
    void commit();
    void refreshEnabled();
    void refreshHints();
    void refreshStatus();
    void applyState(AutoClickState s);
    void setLocationCount(int n);
    void showCircles(bool on);
    AutoClickPlan makePlan(const AutoClickSettings& s) const;

    AutoClicker& ac_;
    MacroController& c_;
    SettingsManager& settings_;
    ShortcutManager& shortcuts_;
    std::unique_ptr<ClickPointPicker> picker_;

    QPushButton *start_, *pause_, *stop_;
    QLabel *status_, *hint_, *placeHint_;
    QWidget* content_;

    QCheckBox *cbInfinite_, *cbCount_, *cbNoDelay_, *cbEvery_, *cbRestore_, *cbMinimize_;
    QButtonGroup *clickGroup_, *intervalGroup_;
    QLineEdit *countEdit_, *locEdit_, *everyEdit_, *gapEdit_, *jitterEdit_;
    QIntValidator* everyValidator_;
    QComboBox *unit_, *button_, *clickType_;
    QPushButton *locMinus_, *locPlus_, *placeBtn_, *advToggle_;
    QWidget* advBody_;

    quint64 clicks_ = 0;
    quint64 total_ = 0;       // 0 = infinite
    QString lastResult_;      // "Finished · 25 clicks" until the next run
    bool loading_ = false;
    bool starting_ = false;   // window is being minimized before the first click
};

}  // namespace mmr
