#pragma once
// Pure C++: turns a stream of mouse events into on-screen "markers" (clicks, drags, scrolls)
// used by the optional accuracy overlay. The same builder serves live recording and the
// preview shown before playback, so both always look identical.
#include <optional>
#include <vector>

#include "MacroTypes.h"

namespace mmr {

struct MarkerPoint { int x = 0, y = 0; };
enum class MarkerKind : uint8_t { Click, Drag, Scroll };

struct Marker {
    MarkerKind kind = MarkerKind::Click;
    MouseButton button = MouseButton::Left;
    int x = 0, y = 0;            // where it started (press / scroll position)
    int endX = 0, endY = 0;      // where it ended (drag release / current drag point)
    int delta = 0;               // scroll: summed wheel delta (120 = 1 notch), sign = direction
    bool horizontal = false;     // scroll orientation
    bool active = false;         // button still held (live drag in progress)
    int index = 0;               // 1-based order shown next to the marker
    int64_t lastTimeUs = 0;
    std::vector<MarkerPoint> path;  // drag trail (decimated)
};

class MarkerBuilder {
public:
    static constexpr int kDragThresholdPx = 6;
    static constexpr int64_t kScrollMergeUs = 600'000;
    static constexpr int kScrollMergePx = 40;
    static constexpr size_t kMaxMarkers = 20'000;
    static constexpr size_t kMaxPathPoints = 4096;

    MarkerBuilder() { clear(); }
    void clear();
    // Returns the index of the marker that was added or changed (needs repainting), if any.
    std::optional<size_t> onEvent(const MacroEvent& e);
    void buildAll(const std::vector<MacroEvent>& events);
    const std::vector<Marker>& markers() const { return markers_; }

private:
    std::vector<Marker> markers_;
    int pending_[6];
    uint8_t held_ = 0;
};

}  // namespace mmr
