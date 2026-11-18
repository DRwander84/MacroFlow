#include <chrono>
#include <mutex>
#include <thread>

#include "TestSupport.h"
#include "core/PlaybackEngine.h"
using namespace mmr;
using namespace std::chrono;

struct FakeSink : IInputSink {
    struct Rec { char kind; int btn; bool down; int x, y; int d; double ms; };
    std::mutex m; std::vector<Rec> log; steady_clock::time_point t0 = steady_clock::now(); bool fail = false;
    double now() const { return duration<double, std::milli>(steady_clock::now() - t0).count(); }
    bool moveTo(int x, int y) override { std::lock_guard l(m); log.push_back({'m', 0, false, x, y, 0, now()}); return !fail; }
    bool button(MouseButton b, bool dn, int x, int y) override { std::lock_guard l(m); log.push_back({'b', int(b), dn, x, y, 0, now()}); return !fail; }
    bool wheel(bool h, int d, int x, int y) override { std::lock_guard l(m); log.push_back({h ? 'h' : 'w', 0, false, x, y, d, now()}); return !fail; }
    // anchors: the window "appears" notReadyMs after the first query and is then dx/dy away from the recording
    double notReadyMs = 0, firstQuery = -1; int dx = 0, dy = 0; bool focusOk = true;
    AnchorState checkWindow(const WindowAnchor&, int& ox, int& oy) override {
        std::lock_guard l(m);
        if (firstQuery < 0) firstQuery = now();
        if (now() - firstQuery < notReadyMs) return AnchorState::NotReady;
        ox = dx; oy = dy; return AnchorState::Ready;
    }
    double busyMs = 0, firstTarget = -1;  // the window under the cursor is unresponsive for busyMs
    AnchorState checkTarget(int, int, bool) override {
        std::lock_guard l(m);
        if (busyMs <= 0) return AnchorState::Ready;
        if (firstTarget < 0) firstTarget = now();
        return now() - firstTarget < busyMs ? AnchorState::NotReady : AnchorState::Ready;
    }
    AnchorState checkFocus(uint32_t) override { return focusOk ? AnchorState::Ready : AnchorState::NotReady; }
    struct KeyRec { int vk; bool down; double ms; };
    std::vector<KeyRec> keys;
    bool key(const KeyInfo& k, bool dn) override { std::lock_guard l(m); keys.push_back({k.vk, dn, now()}); return !fail; }
    std::vector<KeyRec> snapKeys() { std::lock_guard l(m); return keys; }
    std::vector<Rec> snap() { std::lock_guard l(m); return log; }
};
static MacroEvent mv(int64_t tMs, int x) { MacroEvent e; e.timeUs = tMs * 1000; e.x = x; return e; }
static MacroEvent bt(int64_t tMs, bool dn, int x = 0) { MacroEvent e; e.timeUs = tMs * 1000; e.type = dn ? EventType::ButtonDown : EventType::ButtonUp; e.button = MouseButton::Left; e.x = x; return e; }
static void sleepMs(int ms) { std::this_thread::sleep_for(milliseconds(ms)); }

void timingAndLoop() {
    FakeSink s; PlaybackEngine p(s);
    std::atomic<int> cycles{0};
    PlaybackEngine::Callbacks cb; cb.onCycle = [&](uint64_t c) { cycles = int(c); };
    CHECK(p.start({mv(0, 0), mv(40, 1), mv(80, 2)}, cb));
    sleepMs(500);
    p.stop();
    auto log = s.snap();
    CHECK(cycles >= 4);                       // loops forever (no repeat limit)
    CHECK(log.size() >= 12);
    for (size_t i = 0; i < log.size(); ++i) CHECK(log[i].x == int(i % 3));           // order preserved
    // absolute deadlines: event k of cycle c must fire at c*(80+10)+{0,40,80} ms, never early, small lateness
    double maxLate = 0;
    for (size_t i = 0; i < log.size(); ++i) {
        double due = (i / 3) * 90.0 + (i % 3) * 40.0;
        double late = log[i].ms - due;
        CHECK(late > -2.0);
        if (late > maxLate) maxLate = late;
    }
    std::printf("  max lateness %.2f ms over %zu events (no drift accumulation)\n", maxLate, log.size());
    CHECK(maxLate < 60.0);                    // generous bound for loaded CI machines
}

void pauseResumeKeepsPosition() {
    FakeSink s; PlaybackEngine p(s);
    CHECK(p.start({mv(0, 0), mv(50, 1), mv(100, 2), mv(150, 3)}, {}));
    sleepMs(70);                              // events 0,1 done
    p.pause(); sleepMs(30);
    size_t atPause = s.snap().size();
    CHECK(atPause == 2 && p.isPaused());
    sleepMs(300);
    CHECK(s.snap().size() == atPause);        // nothing sent while paused
    p.resume();
    sleepMs(20);
    auto log = s.snap();
    CHECK(log.size() == atPause);             // event 2 not due yet: pause time did not cause an instant burst
    sleepMs(70);
    log = s.snap();
    CHECK(log.size() >= 3 && log[2].x == 2);  // continues at next event, not from the start
    double gap = log[2].ms - log[1].ms;       // ~100 ms pause-shifted timeline: 50ms spacing preserved
    std::printf("  gap across pause: %.1f ms\n", gap);
    CHECK(gap > 330 && gap < 460);            // 30 ms + 300 ms pause + remaining spacing
    p.stop();
}

void heldButtonsReleasedOnPauseAndStop() {
    FakeSink s; PlaybackEngine p(s);
    p.start({bt(0, true), mv(100, 5), bt(200, false)}, {});
    sleepMs(40);
    p.pause(); sleepMs(30);
    auto log = s.snap();
    CHECK(log.size() == 2 && log[1].kind == 'b' && !log[1].down);   // auto release on pause
    p.resume(); sleepMs(30);
    log = s.snap();
    CHECK(log.size() == 3 && log[2].kind == 'b' && log[2].down);    // restored on resume, no extra click
    p.stop();
    log = s.snap();
    CHECK(!log.back().down);                                         // released on stop
    FakeSink s2; PlaybackEngine p2(s2);
    p2.start({bt(0, true), mv(1000, 5)}, {}); sleepMs(30);
    p2.pause(); sleepMs(20); p2.stop();
    int ups = 0; for (auto& r : s2.snap()) ups += (r.kind == 'b' && !r.down);
    CHECK(ups == 1);                                                 // stop-while-paused: no double release
}

void sinkFailureEndsPlayback() {
    FakeSink s; s.fail = true; PlaybackEngine p(s);
    std::atomic<bool> done{false}, ok{true};
    PlaybackEngine::Callbacks cb; cb.onFinished = [&](bool o, const std::string&) { ok = o; done = true; };
    p.start({mv(0, 0), mv(10, 1)}, cb);
    for (int i = 0; i < 50 && !done; ++i) sleepMs(10);
    CHECK(done && !ok);
    p.stop();
}

static MacroEvent kd(int64_t tMs, bool dn, int vk) {
    KeyInfo k; k.vk = uint16_t(vk); k.scan = uint16_t(vk);
    return makeKeyEvent(tMs * 1000, dn, k);
}

void keyboardPlaybackTimingAndRelease() {
    FakeSink s; PlaybackEngine p(s);
    // Ctrl+C then a key held longer than the test: stop must release what is held (last pressed first)
    CHECK(p.start({kd(0, true, 0xA2), kd(30, true, 0x43), kd(45, false, 0x43), kd(60, false, 0xA2),
                   kd(200, true, 0x41), kd(210, true, 0x42), kd(3000, false, 0x42)}, {}));
    sleepMs(260);
    p.stop();
    auto k = s.snapKeys();
    CHECK(k.size() >= 6);
    CHECK(k[0].vk == 0xA2 && k[0].down && k[1].vk == 0x43 && k[1].down);
    CHECK(k[2].vk == 0x43 && !k[2].down && k[3].vk == 0xA2 && !k[3].down);
    double maxErr = 0;
    const double due[] = {0, 30, 45, 60, 200, 210};
    for (int i = 0; i < 6; ++i) {
        double d = k[size_t(i)].ms - k[0].ms - due[i];
        CHECK(d > -0.5);                                   // never early
        if (d > maxErr) maxErr = d;
    }
    std::printf("  key timing max lateness %.3f ms\n", maxErr);
    CHECK(maxErr < 8.0);
    CHECK(k.size() == 8);                                  // two synthetic UPs: B (last) then A
    CHECK(k[6].vk == 0x42 && !k[6].down && k[7].vk == 0x41 && !k[7].down);
    CHECK(s.snap().empty());                               // keyboard-only record never touches the mouse
}

void pauseReleasesKeysAndRestores() {
    FakeSink s; PlaybackEngine p(s);
    p.start({kd(0, true, 0x11), kd(400, false, 0x11)}, {});
    sleepMs(40);
    p.pause(); sleepMs(30);
    auto k = s.snapKeys();
    CHECK(k.size() == 2 && !k[1].down);                    // released while paused
    p.resume(); sleepMs(20);
    k = s.snapKeys();
    CHECK(k.size() == 3 && k[2].down);                     // restored on resume
    p.stop();
}

void windowAnchorWaitsAndShifts() {
    FakeSink s; s.notReadyMs = 300; s.dx = 100; s.dy = -50;
    WindowAnchor wa; wa.left = 10; wa.top = 10; wa.hash = 42;
    PlaybackEngine p(s);
    // click due at 50 ms, but the program only shows up after ~300 ms; the release follows 40 ms after the press
    CHECK(p.start({mv(0, 5), makeWindowAnchorEvent(50'000, wa), bt(50, true, 200), bt(90, false, 200)}, {}));
    sleepMs(900);
    p.stop();
    auto log = s.snap();
    CHECK(log.size() >= 3);
    CHECK(log[0].kind == 'm' && log[0].x == 5);               // moves before the anchor use recorded coordinates
    CHECK(log[1].kind == 'b' && log[1].down && log[1].x == 300 && log[1].y == -50);   // shifted into the new window
    CHECK(log[1].ms >= 300 && log[1].ms < 700);               // waited for the window (+ settle), not clicked blindly
    CHECK(log[2].kind == 'b' && !log[2].down);
    double gap = log[2].ms - log[1].ms;
    std::printf("  press->release gap %.1f ms (recorded 40)\n", gap);
    CHECK(gap > 38 && gap < 46);                              // spacing after the wait is the recorded one
}

void windowAnchorTimesOutAndFallsBack() {
    FakeSink s; s.notReadyMs = 1e9;                           // never appears
    WindowAnchor wa; wa.left = 0; wa.top = 0; wa.hash = 7;
    PlaybackEngine p(s);
    CHECK(p.start({makeWindowAnchorEvent(0, wa), bt(0, true, 77), bt(10, false, 77)}, {}));
    sleepMs(300);
    CHECK(s.snap().empty());                                  // still waiting, can be stopped at any moment
    auto t0 = steady_clock::now();
    p.stop();
    const double stopMs = duration<double, std::milli>(steady_clock::now() - t0).count();
    CHECK(stopMs < 200);
}

void focusAnchorWaits() {
    FakeSink s; s.focusOk = false;
    PlaybackEngine p(s);
    KeyInfo k; k.vk = 0x41; k.scan = 0x1E;
    CHECK(p.start({makeFocusAnchorEvent(0, 9), makeKeyEvent(0, true, k), makeKeyEvent(20'000, false, k)}, {}));
    sleepMs(150);
    CHECK(s.snapKeys().empty());                              // typing waits for the right window
    s.m.lock(); s.focusOk = true; s.m.unlock();
    sleepMs(500);
    CHECK(s.snapKeys().size() >= 2);
    p.stop();
}

void busyTargetGateWaitsAndShifts() {
    FakeSink s; s.busyMs = 300; PlaybackEngine p(s);
    CHECK(p.start({mv(0, 0), bt(40, true, 1), bt(80, false, 1)}, {}));
    sleepMs(200);
    CHECK(s.snap().empty());                  // nothing is pushed into the busy window
    sleepMs(500);
    p.stop();
    auto log = s.snap();
    CHECK(log.size() >= 3);
    CHECK(log[0].ms >= 295.0);                // starts only once the target answers
    CHECK(log[1].ms - log[0].ms >= 37.0);     // recorded spacing is kept after the wait
    CHECK(log[2].ms - log[1].ms >= 37.0);
}

void loopIntervalReturnsToStartThenWaits() {
    FakeSink s; PlaybackEngine p(s);
    // record: cursor starts at x=5, ends at x=9. Interval mode: after each cycle the cursor goes back to x=5,
    // and the 200 ms pause is counted from that return.
    CHECK(p.start({mv(0, 5), mv(20, 9)}, {}, 200'000));
    sleepMs(700);
    p.stop();
    auto log = s.snap();
    CHECK(log.size() >= 9);                             // [5, 9, back to 5] per cycle
    CHECK(log[0].ms < 20.0);                            // first cycle runs at once
    for (size_t i = 0; i + 2 < log.size(); i += 3) {
        CHECK(log[i].x == 5);
        CHECK(log[i + 1].x == 9);
        CHECK(log[i + 2].x == 5);                       // returned to the start position
        if (i + 3 < log.size()) {
            CHECK(log[i + 3].ms - log[i + 2].ms >= 195.0);  // the interval is counted from the return
            CHECK(log[i + 3].ms - log[i + 2].ms < 260.0);
        }
    }
}

int main() {
    RUN(loopIntervalReturnsToStartThenWaits);
    RUN(busyTargetGateWaitsAndShifts);
    RUN(windowAnchorWaitsAndShifts); RUN(windowAnchorTimesOutAndFallsBack); RUN(focusAnchorWaits);
    RUN(keyboardPlaybackTimingAndRelease); RUN(pauseReleasesKeysAndRestores);
    RUN(timingAndLoop); RUN(pauseResumeKeepsPosition); RUN(heldButtonsReleasedOnPauseAndStop); RUN(sinkFailureEndsPlayback);
    TEST_MAIN_END();
}
