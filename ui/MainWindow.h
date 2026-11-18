#pragma once
#include <QColor>
#include <QLabel>
#include <QPoint>
#include <QPropertyAnimation>
#include <QStackedWidget>
#include <QTabBar>
#include <QWidget>

#include <memory>

#include "core/AutoClicker.h"
#include "platform/windows/TaskbarBadge.h"
#include "AutoClickerWidget.h"
#include "RecorderWidget.h"
#include "RecordsWidget.h"
#include "SettingsWidget.h"

namespace mmr {

class MainWindow : public QWidget {
    Q_OBJECT
    Q_PROPERTY(QColor accent READ accent WRITE setAccent)
public:
    MainWindow(MacroController& controller, MacroRepository& repo, SettingsManager& settings,
               ShortcutManager& shortcuts, AutoClicker& autoClicker, QWidget* parent = nullptr);
    QColor accent() const { return accent_; }
    void setAccent(const QColor& c) { accent_ = c; update(); }
    void startSelectedPlayback();    // used by the Start / Stop Playback shortcut
    void requestPlay(qint64 id);     // validates monitor layout, then starts playback
    void showNotice(const QString& text);
    void startAutoClicker();         // used by the Start / Stop Auto Clicker shortcut

protected:
    void paintEvent(QPaintEvent*) override;
    void showEvent(QShowEvent*) override;
    void closeEvent(QCloseEvent*) override;
    void mousePressEvent(QMouseEvent*) override;

private:
    void onState(AppState s);
    void onAutoClickState(AutoClickState s);
    void switchTab(int index);
    void askNameAndSave();
    void updateExcludedWindow();

    MacroController& c_;
    MacroRepository& repo_;
    SettingsManager& settings_;
    AutoClicker& autoClicker_;
    AutoClickerWidget* autoPage_;
    QTabBar* tabs_;
    QStackedWidget* pages_;
    RecordsWidget* records_;
    QLabel* notice_;
    QColor accent_{120, 124, 140};
    QPropertyAnimation* accentAnim_;
    std::unique_ptr<TaskbarBadge> badge_;  // recording / paused dot on the taskbar icon
};

}  // namespace mmr
