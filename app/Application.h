#pragma once
#include <QObject>
#include <memory>

#include "core/AutoClicker.h"
#include "core/MacroController.h"
#include "platform/windows/GlobalHotkeys.h"
#include "settings/SettingsManager.h"
#include "settings/ShortcutManager.h"
#include "storage/DatabaseManager.h"
#include "storage/MacroRepository.h"
#include "ui/ClickOverlay.h"
#include "ui/MainWindow.h"

namespace mmr {

// Composition root: builds every module once and connects hotkeys to the state machine.
class Application : public QObject {
    Q_OBJECT
public:
    explicit Application(QObject* parent = nullptr);
    ~Application() override;
    bool init(QString* fatalError);
    void show();

private:
    void onShortcut(int action);
    void onState(AppState s);

    std::unique_ptr<SettingsManager> settings_;
    std::unique_ptr<DatabaseManager> db_;
    std::unique_ptr<MacroRepository> repo_;
    std::unique_ptr<GlobalHotkeys> hotkeys_;
    std::unique_ptr<ShortcutManager> shortcuts_;
    std::unique_ptr<MacroController> controller_;
    std::unique_ptr<AutoClicker> autoClicker_;
    std::unique_ptr<ClickOverlay> overlay_;
    std::unique_ptr<MainWindow> window_;
    AppState prevState_ = AppState::Idle;
    QString startupWarning_;
};

}  // namespace mmr
