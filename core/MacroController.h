#pragma once
#include <QObject>
#include <QString>
#include <QTimer>
#include <functional>
#include <mutex>
#include <optional>
#include <vector>

#include "PlaybackEngine.h"
#include "RecordingEngine.h"
#include "platform/windows/InputHook.h"
#include "platform/windows/MouseInput.h"
#include "storage/MacroRepository.h"

namespace mmr {

enum class AppState {
    Idle, RecordingCountdown, Recording, RecordingPaused, Saving,
    PlaybackCountdown, Playing, PlaybackPaused, Stopping
};
const char* toString(AppState s);
inline bool isRecordingState(AppState s) {
    return s == AppState::RecordingCountdown || s == AppState::Recording || s == AppState::RecordingPaused;
}
inline bool isPlaybackState(AppState s) {
    return s == AppState::PlaybackCountdown || s == AppState::Playing || s == AppState::PlaybackPaused;
}

// What a recording captures. Chosen in Settings, read once when a recording is requested.
struct RecordInputMode {
    bool mouse = true;
    bool keyboard = false;
    bool any() const { return mouse || keyboard; }
};

// The one place that owns the application state machine and wires hook/engines/storage together.
class MacroController : public QObject {
    Q_OBJECT
public:
    static constexpr int kCountdownSeconds = 3;

    explicit MacroController(MacroRepository& repo, QObject* parent = nullptr);
    ~MacroController() override;

    AppState state() const { return state_; }
    qint64 activeRecordId() const { return activeRecordId_; }
    void setExcludedWindow(void* hwnd, int insetPx) { hook_.setExcludedWindow(hwnd, insetPx); }
    // Which devices to record (Settings). Without a query: mouse only.
    void setInputModeQuery(std::function<RecordInputMode()> q) { inputModeQuery_ = std::move(q); }
    // The app's own shortcuts: pressing them to start/stop/pause is never recorded as keyboard input.
    void setControlKeysQuery(std::function<std::vector<ControlKey>()> q) { controlKeysQuery_ = std::move(q); }
    // Asked once at the start of each recording / playback: should the accuracy overlay be fed?
    void setOverlayEnabledQuery(std::function<bool()> q) { overlayQuery_ = std::move(q); }

public slots:
    void startRecording();
    void stopRecording();
    void pauseResumeRecording();
    void playRecord(qint64 id, qint64 loopIntervalMs = 0);  // 0 = loop without a pause
    void stopPlayback();
    void pauseResumePlayback();
    void pauseResumeAny();  // smart Pause/Resume for whatever is active
    void stopAny();
    bool commitRecording(const QString& name);  // false: DB error, recording is kept in memory for a retry
    void discardRecording();
    void shutdown();

signals:
    void stateChanged(mmr::AppState state);
    void countdownTick(int secondsLeft);
    void elapsedChanged(qint64 activeMs);
    void recordingReady(qint64 durationMs, int eventCount, const QString& suggestedName);
    void recordSaved(qint64 id);
    void playbackCycle(quint64 cycle);
    void activeRecordChanged(qint64 id);  // -1 = none
    void errorOccurred(const QString& title, const QString& message);
    // Recording was requested while both mouse and keyboard recording are switched off in Settings.
    void noInputSelected();
    // Only emitted when the overlay setting is ON. liveEvent comes from the hook thread (queued).
    void liveEvent(const mmr::MacroEvent& e);
    void playbackPreview(const std::vector<mmr::MacroEvent>& events);

private:
    void setState(AppState s);
    void onCountdown();
    void beginRecording();
    void beginPlayback();
    void onRaw(const RawInputEvent& e);
    void onPlaybackFinished(bool ok, const QString& reason);

    MacroRepository& repo_;
    InputHook hook_;
    MouseInput input_;
    std::mutex recMutex_;  // rec_ is touched by the hook thread and the UI thread
    RecordingEngine rec_;
    PlaybackEngine play_;
    QTimer countdownTimer_, elapsedTimer_;
    AppState state_ = AppState::Idle;
    int countdownLeft_ = 0;
    qint64 activeRecordId_ = -1;
    std::vector<MacroEvent> pendingPlay_;
    qint64 pendingIntervalMs_ = 0;
    std::optional<RecordingResult> pendingSave_;
    QString pendingGeometry_;
    QString pendingMeta_;
    bool autoStopQueued_ = false;
    qint64 lastElapsedSec_ = -1;
    bool liveEvents_ = false;
    std::function<bool()> overlayQuery_;
    std::function<RecordInputMode()> inputModeQuery_;
    std::function<std::vector<ControlKey>()> controlKeysQuery_;
    RecordInputMode recMode_;  // snapshot taken when the recording was requested
};

}  // namespace mmr
