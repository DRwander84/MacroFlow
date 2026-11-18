#include "TestSupport.h"
#include "core/MarkerBuilder.h"
using namespace mmr;

static MacroEvent e(int64_t t, EventType ty, MouseButton b, int x, int y, int d = 0) {
    MacroEvent v; v.timeUs = t; v.type = ty; v.button = b; v.x = x; v.y = y; v.delta = d; return v;
}

void clickDragScroll() {
    MarkerBuilder b;
    b.onEvent(e(0, EventType::Move, MouseButton::None, 0, 0));
    CHECK(b.onEvent(e(10, EventType::ButtonDown, MouseButton::Left, 100, 100)).has_value());  // shown at press time
    CHECK(b.markers()[0].kind == MarkerKind::Click && b.markers()[0].active);
    b.onEvent(e(20, EventType::ButtonUp, MouseButton::Left, 101, 100));
    CHECK(b.markers()[0].kind == MarkerKind::Click && !b.markers()[0].active);   // tiny jitter stays a click

    b.onEvent(e(30, EventType::ButtonDown, MouseButton::Right, 200, 200));
    b.onEvent(e(40, EventType::Move, MouseButton::None, 260, 240));
    CHECK(b.markers()[1].kind == MarkerKind::Drag && b.markers()[1].button == MouseButton::Right);
    b.onEvent(e(50, EventType::ButtonUp, MouseButton::Right, 300, 250));
    CHECK(b.markers()[1].endX == 300 && b.markers()[1].path.size() == 3);

    b.onEvent(e(100, EventType::Wheel, MouseButton::None, 50, 50, -120));
    b.onEvent(e(200, EventType::Wheel, MouseButton::None, 52, 50, -120));        // merged
    b.onEvent(e(5'000'000, EventType::Wheel, MouseButton::None, 50, 50, -120));  // too late: new
    b.onEvent(e(5'000'100, EventType::HWheel, MouseButton::None, 50, 50, 120));  // other axis: new
    CHECK(b.markers().size() == 5);
    CHECK(b.markers()[2].delta == -240 && b.markers()[3].delta == -120 && b.markers()[4].horizontal);
    for (size_t i = 0; i < b.markers().size(); ++i) CHECK(b.markers()[i].index == int(i) + 1);
}

void buildAllMatchesLive() {
    std::vector<MacroEvent> ev = {e(0, EventType::Move, MouseButton::None, 1, 1),
        e(5, EventType::ButtonDown, MouseButton::Middle, 10, 10), e(6, EventType::ButtonUp, MouseButton::Middle, 10, 10),
        e(7, EventType::Wheel, MouseButton::None, 10, 10, 120)};
    MarkerBuilder a, live; a.buildAll(ev);
    for (auto& x : ev) live.onEvent(x);
    CHECK(a.markers().size() == 2 && live.markers().size() == 2);
    CHECK(a.markers()[0].x == live.markers()[0].x && a.markers()[1].delta == live.markers()[1].delta);
    a.clear(); CHECK(a.markers().empty());
}

int main() { RUN(clickDragScroll); RUN(buildAllMatchesLive); TEST_MAIN_END(); }
