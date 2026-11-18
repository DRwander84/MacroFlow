#include "AutoClicker.h"

#include <QMetaObject>
#include <algorithm>
#include <climits>
#include <random>

namespace mmr {
namespace {
using std::chrono::microseconds;

constexpr int64_t kFastHoldUs = 1'000;       // press length in "no delay" mode
constexpr int64_t kNormalHoldUs = 10'000;    // press length otherwise (reliable for every program)
constexpr int64_t kFastRoundGapUs = 1'000;   // keeps "no delay" from starving the system
constexpr int64_t kDoubleClickGapUs = 25'000;
constexpr int kMaxConsecutiveFailures = 50;  // Windows refuses input (secure desktop / UAC prompt)

MouseButton toButton(int b) { return b == 1 ? MouseButton::Right : b == 2 ? MouseButton::Middle : MouseButton::Left; }
}  // namespace

AutoClicker::AutoClicker(QObject* parent) : QObject(parent) {
    qRegisterMetaType<mmr::AutoClickState>("mmr::AutoClickState");
    progressTimer_.setInterval(150);
    connect(&progressTimer_, &QTimer::timeout, this, [this] { emit progress(clicks_.load()); });
}

AutoClicker::~AutoClicker() {
    {
        std::lock_guard lk(m_);
        stop_ = true;
        paused_ = false;
    }
    cv_.notify_all();
    if (thread_.joinable()) thread_.join();
}

void AutoClicker::setState(AutoClickState s) {
    if (s == state_) return;
    state_ = s;
    emit stateChanged(s);
}

bool AutoClicker::start(const AutoClickPlan& plan) {
    if (state_ != AutoClickState::Idle || plan.points.empty()) return false;
    if (thread_.joinable()) thread_.join();
    {
        std::lock_guard lk(m_);
        stop_ = false;
        paused_ = false;
    }
    clicks_ = 0;
    ++generation_;
    thread_ = std::thread([this, plan, gen = generation_] { run(plan, gen); });
    progressTimer_.start();
    setState(AutoClickState::Running);
    emit progress(0);
    return true;
}

void AutoClicker::pauseResume() {
    if (state_ == AutoClickState::Idle) return;
    bool nowPaused;
    {
        std::lock_guard lk(m_);
        paused_ = !paused_;
        nowPaused = paused_;
    }
    cv_.notify_all();
    if (nowPaused) {
        progressTimer_.stop();
        emit progress(clicks_.load());
        setState(AutoClickState::Paused);
    } else {
        progressTimer_.start();
        setState(AutoClickState::Running);
    }
}

void AutoClicker::stop() { endRun(false); }

void AutoClicker::endRun(bool completed) {
    if (state_ == AutoClickState::Idle) return;
    {
        std::lock_guard lk(m_);
        stop_ = true;
        paused_ = false;
    }
    cv_.notify_all();
    if (thread_.joinable()) thread_.join();  // the worker releases the button and restores the cursor first
    progressTimer_.stop();
    ++generation_;
    const quint64 n = clicks_.load();
    emit progress(n);
    setState(AutoClickState::Idle);
    emit finished(n, completed);
}

// Waits until t. While paused it waits for the resume and moves t (and the round's pause total) by the pause length,
// so the rhythm continues exactly where it stopped.
bool AutoClicker::sleepUntil(Clock::time_point& t) {
    std::unique_lock lk(m_);
    for (;;) {
        if (stop_) return false;
        if (paused_) {
            const auto p0 = Clock::now();
            cv_.wait(lk, [this] { return stop_.load() || !paused_; });
            if (stop_) return false;
            const auto d = Clock::now() - p0;
            t += d;
            pauseShift_ += d;
            continue;
        }
        if (Clock::now() >= t) return true;
        cv_.wait_until(lk, t, [this] { return stop_.load() || paused_; });
    }
}

bool AutoClicker::clickOnce(const AutoClickPlan& p, int x, int y) {
    // Always put the cursor on the target first: if the user nudged the mouse, the click still lands exactly there.
    int cx = 0, cy = 0;
    if (!(MouseInput::cursorPos(cx, cy) && cx == x && cy == y)) {
        input_.moveTo(x, y);
        std::this_thread::sleep_for(microseconds(1000));  // let the target window see the move before the press
    }
    const MouseButton b = toButton(p.button);
    const auto hold = microseconds(p.noDelay ? kFastHoldUs : kNormalHoldUs);
    bool ok = true;
    for (int i = 0; i < (p.doubleClick ? 2 : 1); ++i) {
        ok = input_.button(b, true, x, y) && ok;
        std::this_thread::sleep_for(hold);  // never interrupted: a pressed button is always released
        ok = input_.button(b, false, x, y) && ok;
        if (i == 0 && p.doubleClick) std::this_thread::sleep_for(microseconds(kDoubleClickGapUs));
    }
    return ok;
}

void AutoClicker::run(AutoClickPlan p, quint64 gen) {
    input_.begin();
    int startX = 0, startY = 0;
    const bool haveStart = MouseInput::cursorPos(startX, startY);
    std::mt19937_64 rng{std::random_device{}()};
    std::uniform_real_distribution<double> unit(-1.0, 1.0);

    uint64_t done = 0;
    int failures = 0;
    bool completed = false, aborted = false;
    QString error;
    auto next = Clock::now();  // the first round starts immediately

    while (!completed && !aborted) {
        if (!sleepUntil(next)) break;
        const auto roundStart = next;
        pauseShift_ = Clock::duration::zero();

        for (size_t i = 0; i < p.points.size(); ++i) {
            if (i > 0) {
                auto t = Clock::now() + microseconds(p.locationGapUs);
                if (!sleepUntil(t)) { aborted = true; break; }
            } else if (stop_) { aborted = true; break; }

            if (clickOnce(p, p.points[i].first, p.points[i].second)) failures = 0;
            else if (++failures >= kMaxConsecutiveFailures) {
                error = QStringLiteral("Windows is not accepting mouse input right now (for example a UAC prompt or the lock screen is open). The Auto Clicker was stopped.");
                aborted = true;
                break;
            }
            clicks_.store(++done);
            if (!p.infinite && done >= p.totalClicks) { completed = true; break; }
        }
        if (completed || aborted) break;

        if (p.noDelay) {
            next = Clock::now() + microseconds(kFastRoundGapUs);
        } else {
            int64_t iv = p.intervalUs;
            if (p.jitterPercent > 0) iv = std::max<int64_t>(1000, int64_t(double(iv) * (1.0 + unit(rng) * p.jitterPercent / 100.0)));
            next = roundStart + pauseShift_ + microseconds(iv);
            const auto now = Clock::now();
            if (next < now) next = now;  // a round longer than the interval: go on at once, never "catch up" in a burst
        }
    }

    if (p.restoreCursor && haveStart) input_.moveTo(startX, startY);
    input_.end();

    if (completed || !error.isEmpty()) {
        QMetaObject::invokeMethod(this, [this, gen, completed, error] {
            if (gen != generation_ || state_ == AutoClickState::Idle) return;  // already stopped by the user
            if (!error.isEmpty()) emit errorOccurred(error);
            endRun(completed);
        }, Qt::QueuedConnection);
    }
}

}  // namespace mmr
