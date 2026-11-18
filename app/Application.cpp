#include "Application.h"

#include <QCoreApplication>

namespace mmr {

Application::Application(QObject* parent) : QObject(parent) {}

Application::~Application() {
    if (autoClicker_) autoClicker_->stop();
    if (controller_) controller_->shutdown();
    overlay_.reset();
    window_.reset();       // UI first
    autoClicker_.reset();
    controller_.reset();   // then engines / hook
    shortcuts_.reset();
    hotkeys_.reset();      // unregisters every hotkey
    repo_.reset();
    db_.reset();
}

bool Application::init(QString* fatalError) {
    settings_ = std::make_unique<SettingsManager>(SettingsManager::defaultPath());

    db_ = std::make_unique<DatabaseManager>(DatabaseManager::defaultPath());
    if (!db_->open(fatalError)) return false;
    repo_ = std::make_unique<MacroRepository>(*db_);

    hotkeys_ = std::make_unique<GlobalHotkeys>();
    shortcuts_ = std::make_unique<ShortcutManager>(*settings_, *hotkeys_);
    controller_ = std::make_unique<MacroController>(*repo_);
    overlay_ = std::make_unique<ClickOverlay>();
    autoClicker_ = std::make_unique<AutoClicker>();
    controller_->setOverlayEnabledQuery([this] { return settings_->general().showClickOverlay; });
    controller_->setInputModeQuery([this] {
        const auto& g = settings_->general();
        return RecordInputMode{g.recordMouse, g.recordKeyboard};
    });
    controller_->setControlKeysQuery([this] {
        std::vector<ControlKey> keys;
        for (const auto& [action, combo] : shortcuts_->active())
            if (!combo.isNone()) keys.push_back({combo.vk, combo.mods});
        return keys;
    });
    connect(controller_.get(), &MacroController::liveEvent, overlay_.get(), &ClickOverlay::feed);
    connect(controller_.get(), &MacroController::playbackPreview, overlay_.get(), &ClickOverlay::showRecord);
    connect(controller_.get(), &MacroController::stateChanged, this, &Application::onState);
    startupWarning_ = shortcuts_->initialize();  // re-registers the saved shortcuts

    window_ = std::make_unique<MainWindow>(*controller_, *repo_, *settings_, *shortcuts_, *autoClicker_);
    connect(hotkeys_.get(), &GlobalHotkeys::triggered, this, &Application::onShortcut);

    QStringList notes;
    if (!startupWarning_.isEmpty()) notes << startupWarning_;
    if (settings_->recoveredFromProblem()) notes << "Some saved settings were invalid and were reset to defaults.";
    if (db_->recoveredFromCorruption()) notes << "The records database was damaged; a backup copy was kept next to it and a new one was created.";
    if (!notes.isEmpty()) window_->showNotice(notes.join(" "));
    return true;
}

void Application::show() { window_->show(); }

void Application::onState(AppState s) {
    if (s == AppState::Recording && prevState_ == AppState::RecordingCountdown) {
        if (settings_->general().showClickOverlay && settings_->general().recordMouse) overlay_->beginLive();  // markers start with the recording
    } else if (s == AppState::Saving || s == AppState::Idle) {
        overlay_->hideAll();  // recording/playback is over: nothing stays on screen
    }
    prevState_ = s;
}

void Application::onShortcut(int a) {
    const AppState s = controller_->state();
    const bool autoBusy = autoClicker_->isActive();  // recording / playback never run while the Auto Clicker works
    switch (ShortcutAction(a)) {
        case ShortcutAction::StartRecording:  // F2 by default: start, and also stop
            if (s == AppState::Idle) { if (!autoBusy) controller_->startRecording(); }
            else if (isRecordingState(s)) controller_->stopRecording();
            break;
        case ShortcutAction::PauseResumeRecording:
            if (s == AppState::Recording || s == AppState::RecordingPaused) controller_->pauseResumeRecording();
            break;
        case ShortcutAction::StartStopPlayback:  // F1 by default: start the selected record, and also stop
            if (s == AppState::Idle) { if (!autoBusy) window_->startSelectedPlayback(); }
            else if (isPlaybackState(s)) controller_->stopPlayback();
            break;
        case ShortcutAction::PauseResumePlayback:
            if (s == AppState::Playing || s == AppState::PlaybackPaused) controller_->pauseResumePlayback();
            break;
        case ShortcutAction::AutoClickStartStop:  // F6 by default: start, and also stop
            if (autoBusy) autoClicker_->stop();
            else window_->startAutoClicker();
            break;
        case ShortcutAction::AutoClickPauseResume:
            if (autoBusy) autoClicker_->pauseResume();
            break;
        default: break;
    }
}

}  // namespace mmr
