#include "TestSupport.h"
#include "core/MacroTypes.h"
#include "core/RecordingEngine.h"
using namespace mmr;

static MacroEvent ev(int64_t t, EventType ty, MouseButton b, int x, int y, int d = 0) {
    MacroEvent e; e.timeUs = t; e.type = ty; e.button = b; e.x = x; e.y = y; e.delta = d; return e;
}

void roundTrip() {
    std::vector<MacroEvent> in = {
        ev(0, EventType::Move, MouseButton::None, -1920, 15),          // negative coords (left monitor)
        ev(10, EventType::ButtonDown, MouseButton::X2, 5, 6),
        ev(20, EventType::ButtonUp, MouseButton::X2, 5, 6),
        ev(30, EventType::Wheel, MouseButton::None, 7, 8, -120),       // scroll delta
        ev(40, EventType::HWheel, MouseButton::None, 7, 8, 240)};
    auto bytes = serializeEvents(in);
    CHECK(bytes.size() == in.size() * kEventSize);
    std::vector<MacroEvent> out; std::string err;
    CHECK(deserializeEvents(bytes.data(), bytes.size(), kFormatVersion, out, &err));
    CHECK(out == in);
}

void corruptData() {
    std::vector<MacroEvent> out; std::string err;
    std::vector<uint8_t> bad(25, 1);
    CHECK(!deserializeEvents(bad.data(), bad.size(), kFormatVersion, out, &err));   // truncated
    auto bytes = serializeEvents({ev(0, EventType::Move, MouseButton::None, 0, 0)});
    bytes[20] = 99;                                                                // bad type
    CHECK(!deserializeEvents(bytes.data(), bytes.size(), kFormatVersion, out, &err));
    bytes = serializeEvents({ev(0, EventType::Move, MouseButton::None, 0, 0)});
    CHECK(!deserializeEvents(bytes.data(), bytes.size(), 99, out, &err));          // bad version
    auto nonMono = serializeEvents({ev(50, EventType::Move, MouseButton::None, 0, 0), ev(10, EventType::Move, MouseButton::None, 1, 1)});
    CHECK(!deserializeEvents(nonMono.data(), nonMono.size(), kFormatVersion, out, &err)); // ordering
    CHECK(out.empty());
}

void recordingPauseExcludedFromDuration() {
    RecordingEngine r;
    const int64_t s = 1'000'000;
    CHECK(r.start(0, 10, 10));
    r.onMove(5 * s, 20, 20);
    CHECK(r.pause(20 * s));                       // 20 s active
    r.onMove(25 * s, 99, 99);                     // ignored while paused
    CHECK(r.activeDurationUs(40 * s) == 20 * s);  // counter frozen
    CHECK(r.resume(50 * s));                      // 30 s paused
    r.onMove(55 * s, 30, 30);                     // active t = 25 s
    auto res = r.stop(65 * s);                    // 15 s more -> 35 s
    CHECK(res.activeDurationUs == 35 * s);
    CHECK(res.events.size() == 3);                // start pos, (20,20), (30,30)
    CHECK(res.events.back().timeUs == 25 * s);    // no 30 s gap in playback
    CHECK(res.events.back().x == 30);
}

void eventOrderingAndDedupe() {
    RecordingEngine r; r.start(0, 0, 0);
    r.onMove(100, 1, 1); r.onMove(200, 1, 1); r.onMove(300, 2, 2); r.onWheel(400, false, 120, 2, 2);
    auto res = r.stop(500);
    CHECK(res.events.size() == 4);
    for (size_t i = 1; i < res.events.size(); ++i) CHECK(res.events[i].timeUs >= res.events[i - 1].timeUs);
    std::string err; CHECK(validateEvents(res.events, &err));
}

void buttonStateHandling() {
    RecordingEngine r; r.start(0, 0, 0);
    r.onButton(10, MouseButton::Left, false, 0, 0);   // up with no recorded down: ignored
    r.onButton(20, MouseButton::Left, true, 1, 1);
    r.onButton(30, MouseButton::Right, true, 1, 1);
    r.pause(40);
    r.onButton(50, MouseButton::Left, false, 2, 2);   // release while paused is kept (no stuck button)
    r.onButton(55, MouseButton::Middle, true, 2, 2);  // press while paused is dropped
    r.resume(60);
    auto res = r.stop(100);                           // Right still held -> synthetic release
    int downs = 0, ups = 0;
    for (auto& e : res.events) { downs += e.type == EventType::ButtonDown; ups += e.type == EventType::ButtonUp; }
    CHECK(downs == 2 && ups == 2);
    CHECK(res.events.back().type == EventType::ButtonUp && res.events.back().button == MouseButton::Right);
}

void stopWhilePausedKeepsEvents() {
    RecordingEngine r; r.start(0, 0, 0); r.onMove(10, 5, 5); r.pause(20);
    auto res = r.stop(1'000'000);
    CHECK(res.events.size() == 2);
    CHECK(res.activeDurationUs == 20);
}

static KeyInfo ki(int vk, int scan, bool ext = false, bool uni = false) {
    KeyInfo k; k.vk = uint16_t(vk); k.scan = uint16_t(scan); k.extended = ext; k.unicode = uni; return k;
}

void keyEventsRoundTrip() {
    std::vector<MacroEvent> in = {
        makeKeyEvent(0, true, ki(0xA2, 0x1D)),                 // Left Ctrl down
        makeKeyEvent(5, true, ki(0x43, 0x2E)),                 // C down
        makeKeyEvent(9, false, ki(0x43, 0x2E)),
        makeKeyEvent(12, false, ki(0xA2, 0x1D)),
        makeKeyEvent(20, true, ki(0x0D, 0x1C, true)),          // numpad Enter (extended)
        makeKeyEvent(30, false, ki(0x0D, 0x1C, true)),
        makeKeyEvent(40, true, ki(0xE7, 0x0645, false, true)), // unicode char injected as VK_PACKET
        makeKeyEvent(41, false, ki(0xE7, 0x0645, false, true))};
    auto bytes = serializeEvents(in);
    std::vector<MacroEvent> out; std::string err;
    CHECK(deserializeEvents(bytes.data(), bytes.size(), kFormatVersion, out, &err));
    CHECK(out == in);
    CHECK(keyInfo(out[4]) == ki(0x0D, 0x1C, true));
    CHECK(keyInfo(out[6]) == ki(0xE7, 0x0645, false, true));
    CHECK(keyIdentity(ki(0x0D, 0x1C, true)) != keyIdentity(ki(0x0D, 0x1C)));      // numpad Enter != Enter
    CHECK(keyIdentity(ki(0xE7, 0x41, false, true)) != keyIdentity(ki(0xE7, 0x42, false, true)));
}

void keyEventValidation() {
    std::string err; std::vector<MacroEvent> out;
    auto bad = makeKeyEvent(0, true, ki(0x41, 0x1E)); bad.x = 0;                        // vk 0
    CHECK(!validateEvents({bad}, &err));
    bad = makeKeyEvent(0, true, ki(0x41, 0x1E)); bad.y = 5;
    CHECK(!validateEvents({bad}, &err));
    bad = makeKeyEvent(0, true, ki(0x41, 0x1E)); bad.delta |= 1 << 20;                   // reserved bit
    CHECK(!validateEvents({bad}, &err));
    bad = makeKeyEvent(0, true, ki(0x41, 0x1E)); bad.button = MouseButton::Left;
    CHECK(!validateEvents({bad}, &err));
    auto good = makeKeyEvent(0, true, ki(0x41, 0x1E));
    CHECK(validateEvents({good}, &err));
    // A v1 (mouse-only) record can never contain keys; v1 mouse data is still readable.
    auto bytes = serializeEvents({good});
    CHECK(!deserializeEvents(bytes.data(), bytes.size(), 1, out, &err));
    auto mouse = serializeEvents({ev(0, EventType::Move, MouseButton::None, 1, 2)});
    CHECK(deserializeEvents(mouse.data(), mouse.size(), 1, out, &err) && out.size() == 1);
}

void keyboardRecording() {
    RecordingEngine r;
    CHECK(r.start(0, 100, 100, false));                       // keyboard-only: no cursor event
    r.onKey(10, ki(0xA2, 0x1D), false);                      // UP without DOWN: ignored
    r.onKey(20, ki(0xA2, 0x1D), true);                       // Ctrl
    r.onKey(30, ki(0x41, 0x1E), true);                       // A
    r.onKey(40, ki(0x41, 0x1E), true);                       // auto-repeat, kept
    r.onKey(50, ki(0x41, 0x1E), false);
    r.pause(60);
    r.onKey(70, ki(0x42, 0x30), true);                       // pressed while paused: dropped
    r.onKey(80, ki(0xA2, 0x1D), false);                      // release of a recorded DOWN while paused: kept
    r.onKey(85, ki(0x42, 0x30), false);                      // UP of a dropped DOWN: dropped
    r.resume(90);
    r.onKey(100, ki(0x20, 0x39), true);                      // Space, still held at stop
    auto res = r.stop(120);
    CHECK(res.events.size() == 7);
    CHECK(res.events.front().type == EventType::KeyDown);     // no initial Move
    CHECK(res.events.back().type == EventType::KeyUp && keyInfo(res.events.back()).vk == 0x20);
    int downs = 0, ups = 0;
    for (auto& e : res.events) { downs += e.type == EventType::KeyDown; ups += e.type == EventType::KeyUp; }
    CHECK(downs == 4 && ups == 3);
    std::string err; CHECK(validateEvents(res.events, &err));
    // pause time is excluded for keys too: Space was pressed 90-60=... active t = 100 - 30 paused
    CHECK(res.events[5].timeUs == 70);
    // combined mouse + keyboard keeps global ordering
    RecordingEngine m; m.start(0, 1, 2);
    m.onMove(10, 5, 5); m.onKey(15, ki(0x41, 0x1E), true); m.onButton(20, MouseButton::Left, true, 5, 5);
    m.onKey(25, ki(0x41, 0x1E), false); m.onButton(30, MouseButton::Left, false, 5, 5);
    auto r2 = m.stop(40);
    CHECK(r2.events.size() == 6);
    for (size_t i = 1; i < r2.events.size(); ++i) CHECK(r2.events[i].timeUs >= r2.events[i - 1].timeUs);
}

void anchorEvents() {
    WindowAnchor wa; wa.left = -1900; wa.top = 120; wa.hash = 0xDEADBEEFu;
    std::vector<MacroEvent> in = {
        makeFocusAnchorEvent(0, 0x1234u),
        makeKeyEvent(1, true, ki(0x41, 0x1E)), makeKeyEvent(2, false, ki(0x41, 0x1E)),
        makeWindowAnchorEvent(10, wa), ev(11, EventType::ButtonDown, MouseButton::Left, -1800, 150),
        ev(12, EventType::ButtonUp, MouseButton::Left, -1800, 150)};
    auto bytes = serializeEvents(in);
    std::vector<MacroEvent> out; std::string err;
    CHECK(deserializeEvents(bytes.data(), bytes.size(), kFormatVersion, out, &err));
    CHECK(out == in);
    CHECK(uint32_t(out[3].delta) == 0xDEADBEEFu && out[3].x == -1900 && out[3].y == 120);
    CHECK(!deserializeEvents(bytes.data(), bytes.size(), 2, out, &err));          // v2 records never hold anchors
    CHECK(!validateEvents({makeFocusAnchorEvent(0, 0)}, &err));                   // empty class hash
    auto bad = makeFocusAnchorEvent(0, 5); bad.x = 3;
    CHECK(!validateEvents({bad}, &err));
    // recording: anchor stored right before an accepted DOWN, focus anchor only when focus changes
    RecordingEngine r; r.start(0, 0, 0);
    r.onKey(10, ki(0x41, 0x1E), true, 0x77u);                                    // first key: focus anchor
    r.onKey(20, ki(0x41, 0x1E), false);
    r.onKey(30, ki(0x42, 0x30), true, 0x77u);                                    // same focus: none
    r.onKey(35, ki(0x42, 0x30), false);
    r.onButton(40, MouseButton::Left, true, 5, 5, &wa);                          // window anchor + down
    r.onButton(50, MouseButton::Left, false, 5, 5, &wa);                         // UP: no anchor
    r.onKey(60, ki(0x43, 0x2E), true, wa.hash);                                  // click already moved focus here: none
    auto res = r.stop(100);
    int fa = 0, wn = 0;
    for (auto& e : res.events) { fa += e.type == EventType::FocusAnchor; wn += e.type == EventType::WindowAnchor; }
    CHECK(fa == 1 && wn == 1);
    for (size_t i = 0; i < res.events.size(); ++i)
        if (res.events[i].type == EventType::ButtonDown) CHECK(i > 0 && res.events[i - 1].type == EventType::WindowAnchor);
    CHECK(validateEvents(res.events, &err));
}

int main() {
    RUN(anchorEvents);
    RUN(keyEventsRoundTrip); RUN(keyEventValidation); RUN(keyboardRecording);
    RUN(roundTrip); RUN(corruptData); RUN(recordingPauseExcludedFromDuration);
    RUN(eventOrderingAndDedupe); RUN(buttonStateHandling); RUN(stopWhilePausedKeepsEvents);
    TEST_MAIN_END();
}
