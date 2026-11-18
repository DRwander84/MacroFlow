#pragma once
#include <QObject>
#include <QString>
#include <QTimer>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <thread>
#include <utility>
#include <vector>

#include "platform/windows/MouseInput.h"

namespace mmr {

enum class AutoClickState { Idle, Running, Paused };

// Everything the engine needs for one run. Plain data: the UI builds it from the saved Auto Clicker settings.
struct AutoClickPlan {
    std::vector<std::pair<int, int>> points;  // physical virtual-desktop pixels, clicked in this order every round
    int button = 0;                           // 0 = left, 1 = right, 2 = middle
    bool doubleClick = false;
    bool infinite = true;
    uint64_t totalClicks = 0;                 // used when !infinite (every single click counts)
    bool noDelay = false;                     // rounds back to back, as fast as the system accepts input
    int64_t intervalUs = 0;                   // start-to-start time of two rounds (ignored when noDelay)
    int jitterPercent = 0;                    // random +/- variation of intervalUs
    int64_t locationGapUs = 0;                // pause between two locations of one round
    bool restoreCursor = true;                // put the cursor back where it was when the run ends
};

// Auto Clicker engine. One worker thread (condition-variable waits on absolute deadlines: no drift, no polling,
// zero CPU while paused / idle). Input is injected with the same tagged SendInput path as playback, so a running
// recording never captures it. start/stop/pauseResume are UI-thread calls.
class AutoClicker : public QObject {
    Q_OBJECT
public:
    explicit AutoClicker(QObject* parent = nullptr);
    ~AutoClicker() override;

    AutoClickState state() const { return state_; }
    bool isActive() const { return state_ != AutoClickState::Idle; }
    quint64 clicks() const { return clicks_.load(); }

    bool start(const AutoClickPlan& plan);  // false when already active or the plan has no location
    void stop();                            // blocking (joins the worker); restores the cursor
    void pauseResume();

signals:
    void stateChanged(mmr::AutoClickState state);
    void progress(quint64 clicks);                         // ~7 times per second while running
    void finished(quint64 clicks, bool completedByCount);  // run ended (by count, or stopped)
    void errorOccurred(const QString& message);

private:
    using Clock = std::chrono::steady_clock;

    void run(AutoClickPlan plan, quint64 generation);
    bool sleepUntil(Clock::time_point& t);  // false = stop requested; pauses extend the deadline
    bool clickOnce(const AutoClickPlan& p, int x, int y);
    void endRun(bool completed);            // UI thread
    void setState(AutoClickState s);

    MouseInput input_;
    std::thread thread_;
    std::mutex m_;
    std::condition_variable cv_;
    std::atomic<bool> stop_{false};
    bool paused_ = false;                  // guarded by m_
    Clock::duration pauseShift_{};         // worker thread only: pause time inside the current round
    std::atomic<quint64> clicks_{0};
    AutoClickState state_ = AutoClickState::Idle;
    quint64 generation_ = 0;               // UI thread only: ignores late messages of an older run
    QTimer progressTimer_;
};

}  // namespace mmr
