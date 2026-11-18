#pragma once
#include <atomic>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "MacroTypes.h"

namespace mmr {

// Output side of playback (SendInput on Windows, a fake in tests).
class IInputSink {
public:
    virtual ~IInputSink() = default;
    virtual bool moveTo(int x, int y) = 0;
    virtual bool button(MouseButton b, bool down, int x, int y) = 0;
    virtual bool wheel(bool horizontal, int delta, int x, int y) = 0;
    virtual bool key(const KeyInfo& key, bool down) { (void)key; (void)down; return true; }
    // Synchronisation with the real desktop. Defaults: unsupported = no waiting, no shifting.
    // checkWindow: does a window like the recorded one exist? dx/dy = how far it moved since recording.
    virtual AnchorState checkWindow(const WindowAnchor& a, int& dx, int& dy) { (void)a; dx = dy = 0; return AnchorState::Unsupported; }
    virtual AnchorState checkFocus(uint32_t classHash) { (void)classHash; return AnchorState::Unsupported; }
    // Is the window under the screen point able to take input right now (not hung / still starting up)?
    // NotReady = it is busy: the engine waits instead of driving the cursor / clicks into it.
    // `discrete` = a click or wheel tick (always check fresh); false = a plain cursor move (may use a short cache).
    virtual AnchorState checkTarget(int x, int y, bool discrete) { (void)x; (void)y; (void)discrete; return AnchorState::Unsupported; }
    virtual void begin() {}  // e.g. raise timer resolution
    virtual void end() {}
};

// Infinite-loop playback on one worker thread. Uses absolute deadlines (no drift
// accumulation) and condition-variable waits (no busy waiting). Pause shifts the
// timeline by the pause length so playback resumes exactly where it stopped.
class PlaybackEngine {
public:
    struct Callbacks {
        std::function<void(uint64_t cycle)> onCycle;                        // worker thread
        std::function<void(bool ok, const std::string& reason)> onFinished; // worker thread, only when it ends by itself
    };
    static constexpr int64_t kLoopGapUs = 10'000;  // minimum gap between loop cycles
    // Key / button / wheel events (discrete, timing-critical) finish their wait with a short busy-wait so
    // they fire within microseconds of their deadline instead of the ~1 ms granularity of a sleep.
    // Moves keep the pure sleep: 1 ms is invisible there and it keeps CPU use near zero.
    static constexpr int64_t kSpinUs = 1'500;
    // Anchors: before a click / the first key in a different window the engine waits (bounded) until that
    // window exists / has focus, so a slow-opening program can no longer make later clicks land on the
    // wrong thing. The wait shifts the rest of the timeline, so recorded spacing is kept.
    static constexpr int64_t kWindowWaitUs = 20'000'000;  // a cold-starting program can take well over 5 s
    static constexpr int64_t kFocusWaitUs = 8'000'000;
    // Busy-target gate: while the window under the cursor is not responding (a program that is still
    // starting), no cursor move / click is injected into it. Bounded, then playback carries on.
    static constexpr int64_t kBusyWaitUs = 15'000'000;
    static constexpr int64_t kBusySettleUs = 150'000;
    static constexpr int64_t kAnchorPollUs = 20'000;
    static constexpr int64_t kAnchorSettleUs = 250'000;  // let a window that just appeared finish drawing

    explicit PlaybackEngine(IInputSink& sink) : sink_(sink) {}
    ~PlaybackEngine() { stop(); }
    PlaybackEngine(const PlaybackEngine&) = delete;
    PlaybackEngine& operator=(const PlaybackEngine&) = delete;

    // loopIntervalUs: 0 = loop without a pause. Otherwise, when a cycle ends the cursor returns to where playback
    // started and only THEN the interval is counted; the next cycle starts when it has elapsed (the first cycle
    // runs at once).
    bool start(std::vector<MacroEvent> events, Callbacks cb, int64_t loopIntervalUs = 0);
    void pause();
    void resume();
    void stop();  // blocking; releases held buttons

    bool isRunning() const { return running_; }
    bool isPaused() const { return paused_; }
    size_t position() const { return position_; }
    uint64_t cycles() const { return cycles_; }

private:
    void run();

    IInputSink& sink_;
    std::vector<MacroEvent> events_;
    Callbacks cb_;
    int64_t intervalUs_ = 0;
    std::thread thread_;
    std::mutex m_;
    std::condition_variable cv_;
    std::atomic<bool> stopFlag_{false};  // written under m_ (for the cv), readable lock-free by the spin wait
    std::atomic<bool> paused_{false};
    std::atomic<bool> running_{false};
    std::atomic<size_t> position_{0};
    std::atomic<uint64_t> cycles_{0};
};

}  // namespace mmr
