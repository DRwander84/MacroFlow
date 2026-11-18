#include "MacroController.h"

#include <QDateTime>
#include <QMetaObject>

#include "platform/windows/DisplayMonitor.h"

namespace mmr {

const char* toString(AppState s) {
    switch (s) {
        case AppState::Idle: return "Idle";
        case AppState::RecordingCountdown: return "RecordingCountdown";
        case AppState::Recording: return "Recording";
        case AppState::RecordingPaused: return "RecordingPaused";
        case AppState::Saving: return "Saving";
        case AppState::PlaybackCountdown: return "PlaybackCountdown";
        case AppState::Playing: return "Playing";
        case AppState::PlaybackPaused: return "PlaybackPaused";
        case AppState::Stopping: return "Stopping";
    }
    return "?";
}

MacroController::MacroController(MacroRepository& repo, QObject* parent)
    : QObject(parent), repo_(repo), play_(input_) {
    qRegisterMetaType<mmr::AppState>("mmr::AppState");
    qRegisterMetaType<mmr::MacroEvent>("mmr::MacroEvent");
    countdownTimer_.setInterval(1000);
    countdownTimer_.setTimerType(Qt::PreciseTimer);
    connect(&countdownTimer_, &QTimer::timeout, this, &MacroController::onCountdown);
    elapsedTimer_.setInterval(250);  // the clock shows whole seconds: wake rarely, update the UI only when a second ticks
    connect(&elapsedTimer_, &QTimer::timeout, this, [this] {
        qint64 us;
        {
            std::lock_guard lk(recMutex_);
            us = rec_.activeDurationUs(nowMicros());
        }
        const qint64 sec = us / 1000000;
        if (sec == lastElapsedSec_) return;
        lastElapsedSec_ = sec;
        emit elapsedChanged(us / 1000);
    });
}

MacroController::~MacroController() { shutdown(); }

void MacroController::setState(AppState s) {
    if (s == state_) return;
    state_ = s;
    emit stateChanged(s);
}

void MacroController::shutdown() {
    countdownTimer_.stop();
    elapsedTimer_.stop();
    play_.stop();
    hook_.uninstall();
    if (state_ != AppState::Idle && state_ != AppState::Saving) setState(AppState::Idle);
}

// ---------------------------------------------------------------- recording

void MacroController::startRecording() {
    if (state_ != AppState::Idle) return;
    recMode_ = inputModeQuery_ ? inputModeQuery_() : RecordInputMode{};
    if (!recMode_.any()) {  // nothing to capture: do not start, tell the user how to fix it
        emit noInputSelected();
        return;
    }
    setState(AppState::RecordingCountdown);
    countdownLeft_ = kCountdownSeconds;
    emit countdownTick(countdownLeft_);
    countdownTimer_.start();
}

void MacroController::onCountdown() {
    if (--countdownLeft_ > 0) { emit countdownTick(countdownLeft_); return; }
    countdownTimer_.stop();
    if (state_ == AppState::RecordingCountdown) beginRecording();
    else if (state_ == AppState::PlaybackCountdown) beginPlayback();
}

void MacroController::beginRecording() {
    int x = 0, y = 0;
    MouseInput::cursorPos(x, y);
    autoStopQueued_ = false;
    liveEvents_ = overlayQuery_ && overlayQuery_();
    {
        std::lock_guard lk(recMutex_);
        // engine first: no event can arrive before it is ready. Keyboard-only recordings store no cursor
        // position, so playback never moves the mouse.
        rec_.start(nowMicros(), x, y, recMode_.mouse);
    }
    unsigned long le = 0;
    const bool ok = hook_.install([this](const RawInputEvent& e) { onRaw(e); }, {recMode_.mouse, recMode_.keyboard},
                                  recMode_.keyboard && controlKeysQuery_ ? controlKeysQuery_() : std::vector<ControlKey>{}, &le);
    if (!ok) {
        {
            std::lock_guard lk(recMutex_);
            rec_.reset();
        }
        setState(AppState::Idle);
        emit errorOccurred("Mouse hook failed",
                           QString("Windows would not let the app watch your input (error %1). Recording was not started.").arg(le));
        return;
    }
    pendingGeometry_ = QString::fromStdString(currentLayoutSignature());
    pendingMeta_ = QString("loop=infinite;fmt=%1;input=%2").arg(kFormatVersion).arg(
        recMode_.mouse && recMode_.keyboard ? "mouse+keyboard" : recMode_.mouse ? "mouse" : "keyboard");
    lastElapsedSec_ = 0;
    elapsedTimer_.start();
    setState(AppState::Recording);
    // After the state change: the Recorder tab only accepts clock updates while in the Recording state. Sent
    // before it, the 00:00:00 was dropped and the last countdown digit ("1") stayed on screen for about a second.
    emit elapsedChanged(0);
}

// Runs on the dedicated hook thread: tiny, no file/DB work, one short uncontended lock.
void MacroController::onRaw(const RawInputEvent& e) {
    const int64_t now = nowMicros();
    bool full = false, forward = false;
    MacroEvent live;
    {
        std::lock_guard lk(recMutex_);
        const auto st = rec_.state();
        switch (e.type) {
            case EventType::Move: rec_.onMove(now, e.x, e.y); break;
            case EventType::ButtonDown: rec_.onButton(now, e.button, true, e.x, e.y, e.hasAnchor ? &e.anchor : nullptr); break;
            case EventType::ButtonUp: rec_.onButton(now, e.button, false, e.x, e.y); break;
            case EventType::Wheel: rec_.onWheel(now, false, e.delta, e.x, e.y); break;
            case EventType::HWheel: rec_.onWheel(now, true, e.delta, e.x, e.y); break;
            case EventType::KeyDown: rec_.onKey(now, e.key, true, e.focusHash); break;
            case EventType::KeyUp: rec_.onKey(now, e.key, false); break;
            case EventType::WindowAnchor:
            case EventType::FocusAnchor: break;  // never produced by the hooks
        }
        full = rec_.full();
        if (liveEvents_ && !isKeyEvent(e.type) && (st == RecordingEngine::State::Recording ||
                            (st == RecordingEngine::State::Paused && e.type == EventType::ButtonUp))) {
            forward = true;
            live.timeUs = rec_.activeDurationUs(now);
            live.x = e.x;
            live.y = e.y;
            live.delta = e.delta;
            live.type = e.type;
            live.button = e.button;
        }
    }
    if (forward) emit liveEvent(live);
    if (full && !autoStopQueued_) {  // safety cap reached: finish cleanly outside the callback
        autoStopQueued_ = true;
        QMetaObject::invokeMethod(this, &MacroController::stopRecording, Qt::QueuedConnection);
    }
}

void MacroController::pauseResumeRecording() {
    const int64_t now = nowMicros();
    std::optional<AppState> next;
    {
        std::lock_guard lk(recMutex_);  // keep the lock short; signals are emitted after releasing it
        if (state_ == AppState::Recording) {
            if (rec_.pause(now)) next = AppState::RecordingPaused;
        } else if (state_ == AppState::RecordingPaused) {
            if (rec_.resume(now)) next = AppState::Recording;
        }
    }
    if (next) setState(*next);
}

void MacroController::stopRecording() {
    if (state_ == AppState::RecordingCountdown) {  // cancel before anything was recorded
        countdownTimer_.stop();
        setState(AppState::Idle);
        return;
    }
    if (state_ != AppState::Recording && state_ != AppState::RecordingPaused) return;
    setState(AppState::Saving);
    elapsedTimer_.stop();
    hook_.uninstall();  // joins the hook thread: no callback can run after this line
    RecordingResult r;
    {
        std::lock_guard lk(recMutex_);
        r = rec_.stop(nowMicros());
    }
    if (r.events.size() < 2) {
        setState(AppState::Idle);
        emit errorOccurred("Nothing recorded", "No input was captured, so nothing was saved.");
        return;
    }
    pendingSave_ = std::move(r);
    emit elapsedChanged(pendingSave_->activeDurationUs / 1000);
    emit recordingReady(pendingSave_->activeDurationUs / 1000, int(pendingSave_->events.size()),
                        "Record " + QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss"));
}

bool MacroController::commitRecording(const QString& name) {
    if (state_ != AppState::Saving || !pendingSave_) return false;
    QString err;
    const auto id = repo_.insert(name, pendingSave_->events, pendingSave_->activeDurationUs, pendingGeometry_,
                                 pendingMeta_, &err);
    if (!id) {
        emit errorOccurred("Could not save record", err);
        return false;
    }
    pendingSave_.reset();
    setState(AppState::Idle);
    emit recordSaved(*id);
    return true;
}

void MacroController::discardRecording() {
    if (state_ != AppState::Saving) return;
    pendingSave_.reset();
    setState(AppState::Idle);
}

// ---------------------------------------------------------------- playback

void MacroController::playRecord(qint64 id, qint64 loopIntervalMs) {
    if (state_ != AppState::Idle) return;  // recording and playback never overlap
    QString err;
    std::vector<MacroEvent> events;
    if (!repo_.loadEvents(id, events, &err)) {
        emit errorOccurred("Cannot play record", err);
        return;
    }
    pendingPlay_ = std::move(events);
    pendingIntervalMs_ = loopIntervalMs > 0 ? loopIntervalMs : 0;
    if (overlayQuery_ && overlayQuery_()) emit playbackPreview(pendingPlay_);  // markers appear immediately
    activeRecordId_ = id;
    emit activeRecordChanged(id);
    setState(AppState::PlaybackCountdown);
    countdownLeft_ = kCountdownSeconds;
    emit countdownTick(countdownLeft_);
    countdownTimer_.start();
}

void MacroController::beginPlayback() {
    PlaybackEngine::Callbacks cb;
    cb.onCycle = [this](uint64_t c) {
        QMetaObject::invokeMethod(this, [this, c] { emit playbackCycle(c); }, Qt::QueuedConnection);
    };
    cb.onFinished = [this](bool ok, const std::string& reason) {
        const QString r = QString::fromStdString(reason);
        QMetaObject::invokeMethod(this, [this, ok, r] { onPlaybackFinished(ok, r); }, Qt::QueuedConnection);
    };
    if (!play_.start(std::move(pendingPlay_), cb, pendingIntervalMs_ * 1000)) {
        pendingPlay_.clear();
        activeRecordId_ = -1;
        emit activeRecordChanged(-1);
        setState(AppState::Idle);
        emit errorOccurred("Cannot play record", "The record contains no events.");
        return;
    }
    pendingPlay_.clear();
    emit playbackCycle(0);
    setState(AppState::Playing);
}

void MacroController::onPlaybackFinished(bool ok, const QString& reason) {
    if (state_ != AppState::Playing && state_ != AppState::PlaybackPaused) return;
    play_.stop();  // join worker, buttons already released
    activeRecordId_ = -1;
    emit activeRecordChanged(-1);
    setState(AppState::Idle);
    if (!ok) emit errorOccurred("Playback stopped", reason);
}

void MacroController::pauseResumePlayback() {
    if (state_ == AppState::Playing) {
        play_.pause();
        setState(AppState::PlaybackPaused);
    } else if (state_ == AppState::PlaybackPaused) {
        play_.resume();
        setState(AppState::Playing);
    }
}

void MacroController::stopPlayback() {
    if (state_ == AppState::PlaybackCountdown) {
        countdownTimer_.stop();
        pendingPlay_.clear();
        activeRecordId_ = -1;
        emit activeRecordChanged(-1);
        setState(AppState::Idle);
        return;
    }
    if (state_ != AppState::Playing && state_ != AppState::PlaybackPaused) return;
    setState(AppState::Stopping);
    play_.stop();  // wakes the worker immediately, releases injected buttons, joins
    activeRecordId_ = -1;
    emit activeRecordChanged(-1);
    setState(AppState::Idle);
}

void MacroController::pauseResumeAny() {
    if (isRecordingState(state_)) pauseResumeRecording();
    else if (isPlaybackState(state_)) pauseResumePlayback();
}

void MacroController::stopAny() {
    if (isRecordingState(state_)) stopRecording();
    else if (isPlaybackState(state_)) stopPlayback();
}

}  // namespace mmr
