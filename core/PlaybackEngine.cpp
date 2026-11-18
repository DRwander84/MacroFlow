#include "PlaybackEngine.h"

#include <algorithm>
#include <chrono>
#include <thread>
#if defined(_MSC_VER) && (defined(_M_X64) || defined(_M_IX86) || defined(_M_ARM64))
#include <intrin.h>
#endif

namespace mmr {
using Clock = std::chrono::steady_clock;
namespace {
constexpr int64_t kResyncLateUs = 40'000;

inline void cpuRelax() {
#if defined(_MSC_VER) && (defined(_M_X64) || defined(_M_IX86))
    _mm_pause();
#elif defined(_MSC_VER) && defined(_M_ARM64)
    __yield();
#elif defined(__x86_64__) || defined(__i386__)
    __builtin_ia32_pause();
#else
    std::this_thread::yield();
#endif
}

// Discrete events are the ones where a late/early fire is observable (a key press, a click, a scroll tick).
inline bool isTimingCritical(EventType t) { return t != EventType::Move; }
}  // namespace

bool PlaybackEngine::start(std::vector<MacroEvent> events, Callbacks cb, int64_t loopIntervalUs) {
    stop();
    if (events.empty()) return false;
    events_ = std::move(events);
    cb_ = std::move(cb);
    intervalUs_ = loopIntervalUs > 0 ? loopIntervalUs : 0;
    stopFlag_ = false;
    paused_ = false;
    position_ = 0;
    cycles_ = 0;
    running_ = true;
    thread_ = std::thread([this] { run(); });
    return true;
}

void PlaybackEngine::pause() {
    std::lock_guard lk(m_);
    if (!running_ || paused_) return;
    paused_ = true;
    cv_.notify_all();
}

void PlaybackEngine::resume() {
    std::lock_guard lk(m_);
    if (!running_ || !paused_) return;
    paused_ = false;
    cv_.notify_all();
}

void PlaybackEngine::stop() {
    {
        std::lock_guard lk(m_);
        stopFlag_ = true;
        cv_.notify_all();
    }
    if (thread_.joinable()) thread_.join();
    running_ = false;
    paused_ = false;
}

void PlaybackEngine::run() {
    sink_.begin();
    std::unique_lock lk(m_);

    bool failed = false;
    std::string reason;
    bool held[6] = {false, false, false, false, false, false};
    std::vector<KeyInfo> heldKeys;  // keys currently pressed by this playback, in press order
    heldKeys.reserve(32);
    auto findKey = [&](const KeyInfo& k) {
        const uint32_t id = keyIdentity(k);
        auto it = heldKeys.begin();
        while (it != heldKeys.end() && keyIdentity(*it) != id) ++it;
        return it;
    };
    bool releasedForPause = false;
    int lastX = 0, lastY = 0;  // last MOUSE position (key events never touch it)
    bool hasMouse = false;     // the record has mouse events (keyboard-only records never touch the cursor)
    for (const auto& e : events_)
        if (!isKeyEvent(e.type)) { lastX = e.x; lastY = e.y; hasMouse = true; break; }
    const int startX = lastX, startY = lastY;  // where playback begins: the cursor goes back here between cycles
    auto cycleStart = Clock::now();
    auto us = [](int64_t v) { return std::chrono::microseconds(v); };

    auto releaseHeld = [&] {
        for (int b = 1; b <= 5; ++b)
            if (held[b]) sink_.button(MouseButton(b), false, lastX, lastY);
        for (size_t i = heldKeys.size(); i-- > 0;) sink_.key(heldKeys[i], false);  // last pressed, first released
    };
    auto restoreHeld = [&] {
        for (int b = 1; b <= 5; ++b)
            if (held[b]) sink_.button(MouseButton(b), true, lastX, lastY);
        for (const auto& k : heldKeys) sink_.key(k, true);
    };

    // Returns false when stop was requested. Handles pause by shifting the timeline.
    auto waitUntil = [&](Clock::time_point& deadline, bool precise) -> bool {
        for (;;) {
            if (stopFlag_) return false;
            if (paused_) {
                const auto pausedAt = Clock::now();
                releaseHeld();
                releasedForPause = true;
                cv_.wait(lk, [&] { return stopFlag_ || !paused_; });
                if (stopFlag_) return false;
                const auto shift = Clock::now() - pausedAt;
                cycleStart += shift;
                deadline += shift;
                restoreHeld();
                releasedForPause = false;
                continue;
            }
            const auto now = Clock::now();
            if (now >= deadline) return true;
            if (!precise) {
                cv_.wait_until(lk, deadline);
                continue;
            }
            const auto coarseEnd = deadline - us(kSpinUs);
            if (now < coarseEnd) {  // sleep (interruptible) until shortly before the deadline ...
                cv_.wait_until(lk, coarseEnd);
                continue;
            }
            // ... then spin the last stretch without the lock; stop()/pause() are still noticed immediately.
            lk.unlock();
            while (Clock::now() < deadline && !stopFlag_.load(std::memory_order_acquire) &&
                   !paused_.load(std::memory_order_acquire))
                cpuRelax();
            lk.lock();
        }
    };

    // Waits until check() reports Ready (or the budget is spent), polling at a low rate. Pause/stop stay
    // responsive. Returns false only when stop was requested. If it had to wait, the timeline is shifted
    // so the events after the anchor keep their recorded spacing.
    auto awaitAnchor = [&](Clock::time_point& deadline, int64_t budgetUs, auto&& check, int64_t settleUs) -> bool {
        lk.unlock();
        AnchorState st = check();
        lk.lock();
        if (stopFlag_) return false;
        if (st != AnchorState::NotReady) return true;
        const auto originalDeadline = deadline;
        while (st == AnchorState::NotReady && budgetUs > 0) {
            auto until = Clock::now() + us(kAnchorPollUs);
            const auto before = cycleStart;
            if (!waitUntil(until, false)) return false;
            deadline += cycleStart - before;  // time spent paused is not part of the wait
            budgetUs -= kAnchorPollUs;
            lk.unlock();
            st = check();
            lk.lock();
            if (stopFlag_) return false;
        }
        auto resume = Clock::now();
        if (st == AnchorState::Ready) resume += us(settleUs);
        cycleStart += resume - originalDeadline;  // everything after this point moves back by what we waited
        deadline = resume;
        return waitUntil(deadline, false);
    };

    bool gateOff = false;    // busy-target gate gave up for the current cycle
    int offX = 0, offY = 0;  // how far the last anchored window moved since recording
    size_t idx = 0;
    while (!stopFlag_ && !failed) {
        if (idx >= events_.size()) {
            offX = offY = 0;
            gateOff = false;
            if (intervalUs_ > 0) {
                // Interval mode: first put the cursor back at the start position, and count the pause from that
                // moment (not from the last event), so the full interval is spent "reset and waiting".
                if (hasMouse) {
                    auto d = Clock::now();
                    if (!awaitAnchor(d, kBusyWaitUs, [&] { return sink_.checkTarget(startX, startY, true); },
                                     kBusySettleUs))
                        break;
                    if (!sink_.moveTo(startX, startY)) {
                        failed = true;
                        reason = "Windows rejected the simulated mouse input (SendInput failed).";
                        break;
                    }
                    lastX = startX;
                    lastY = startY;
                }
                cycleStart = Clock::now() + us(std::max<int64_t>(intervalUs_, kLoopGapUs));
            } else {
                cycleStart += us(events_.back().timeUs + kLoopGapUs);
            }
            idx = 0;
            position_ = 0;
            const uint64_t c = ++cycles_;
            if (cb_.onCycle) {
                lk.unlock();
                cb_.onCycle(c);
                lk.lock();
                if (stopFlag_) break;
            }
        }
        const MacroEvent& ev = events_[idx];
        auto deadline = cycleStart + us(ev.timeUs);
        if (!waitUntil(deadline, isTimingCritical(ev.type))) break;
        // After a stall (system hiccup, sleep, heavy load) do NOT fire the backlog in one burst:
        // bursts distort click/scroll spacing (apps accelerate scrolling on fast ticks). Shift the
        // timeline instead so the recorded spacing of the remaining events is preserved.
        const auto lateness = Clock::now() - deadline;
        if (lateness > us(kResyncLateUs)) {
            cycleStart += lateness;
            deadline += lateness;
        }

        if (isAnchorEvent(ev.type)) {
            if (ev.type == EventType::WindowAnchor) {
                const WindowAnchor a{ev.x, ev.y, uint32_t(ev.delta)};
                int dx = 0, dy = 0;
                AnchorState st = AnchorState::Unsupported;
                const bool go = awaitAnchor(deadline, kWindowWaitUs, [&] {
                    dx = dy = 0;
                    return st = sink_.checkWindow(a, dx, dy);
                }, kAnchorSettleUs);
                if (!go) break;
                offX = st == AnchorState::Ready ? dx : 0;  // not found in time: fall back to the recorded position
                offY = st == AnchorState::Ready ? dy : 0;
            } else {
                const uint32_t h = uint32_t(ev.delta);
                if (!awaitAnchor(deadline, kFocusWaitUs, [&] { return sink_.checkFocus(h); }, kAnchorSettleUs)) break;
            }
            ++idx;
            position_ = idx;
            continue;
        }
        // Busy-target gate: never push the cursor / a click into a window that is not responding yet (a program
        // that was just launched). Doing so makes Windows' input thread block on that window, which freezes the
        // whole cursor for seconds and then lets the queued input land late / get lost. Wait (bounded, pause and
        // stop stay responsive) and shift the timeline so the recorded spacing of the remaining events is kept.
        if (!gateOff && !isKeyEvent(ev.type)) {
            const int tx = ev.x + offX, ty = ev.y + offY;
            const bool discrete = ev.type != EventType::Move;
            AnchorState gs = AnchorState::Unsupported;
            if (!awaitAnchor(deadline, kBusyWaitUs, [&] { return gs = sink_.checkTarget(tx, ty, discrete); },
                             kBusySettleUs))
                break;
            if (gs == AnchorState::NotReady) gateOff = true;  // still busy after the whole budget: stop waiting this cycle
        }

        bool ok = true;
        switch (ev.type) {
            case EventType::Move: ok = sink_.moveTo(ev.x + offX, ev.y + offY); break;
            case EventType::ButtonDown:
                ok = sink_.button(ev.button, true, ev.x + offX, ev.y + offY);
                held[int(ev.button)] = true;
                break;
            case EventType::ButtonUp:
                ok = sink_.button(ev.button, false, ev.x + offX, ev.y + offY);
                held[int(ev.button)] = false;
                break;
            case EventType::Wheel: ok = sink_.wheel(false, ev.delta, ev.x + offX, ev.y + offY); break;
            case EventType::HWheel: ok = sink_.wheel(true, ev.delta, ev.x + offX, ev.y + offY); break;
            case EventType::KeyDown: {
                const KeyInfo k = keyInfo(ev);
                ok = sink_.key(k, true);
                if (findKey(k) == heldKeys.end()) heldKeys.push_back(k);  // auto-repeat DOWNs are replayed, not re-added
                break;
            }
            case EventType::WindowAnchor:
            case EventType::FocusAnchor: break;  // handled above
            case EventType::KeyUp: {
                const KeyInfo k = keyInfo(ev);
                ok = sink_.key(k, false);
                const auto it = findKey(k);
                if (it != heldKeys.end()) heldKeys.erase(it);
                break;
            }
        }
        if (!isKeyEvent(ev.type)) {
            lastX = ev.x + offX;
            lastY = ev.y + offY;
        }
        if (!ok) {
            failed = true;
            reason = "Windows rejected the simulated mouse input (SendInput failed).";
        }
        ++idx;
        position_ = idx;
    }

    if (!releasedForPause) releaseHeld();  // never leave a button stuck down
    sink_.end();
    const bool byItself = !stopFlag_;
    running_ = false;
    lk.unlock();
    if (byItself && cb_.onFinished) cb_.onFinished(!failed, reason);
}

}  // namespace mmr
