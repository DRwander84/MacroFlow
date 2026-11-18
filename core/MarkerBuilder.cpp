#include "MarkerBuilder.h"

#include <cstdlib>

namespace mmr {

void MarkerBuilder::clear() {
    markers_.clear();
    for (int& p : pending_) p = -1;
    held_ = 0;
}

void MarkerBuilder::buildAll(const std::vector<MacroEvent>& events) {
    clear();
    for (const auto& e : events) onEvent(e);
}

std::optional<size_t> MarkerBuilder::onEvent(const MacroEvent& e) {
    switch (e.type) {
        case EventType::KeyDown:
        case EventType::KeyUp:
        case EventType::WindowAnchor:
        case EventType::FocusAnchor:
            return std::nullopt;  // the overlay only visualises mouse activity
        case EventType::Move: {
            if (!held_) return std::nullopt;
            std::optional<size_t> changed;
            for (int b = 1; b <= 5; ++b) {
                if (pending_[b] < 0) continue;
                Marker& m = markers_[size_t(pending_[b])];
                const MarkerPoint last = m.path.back();
                if (std::abs(e.x - last.x) < 2 && std::abs(e.y - last.y) < 2) continue;
                if (m.path.size() < kMaxPathPoints) m.path.push_back({e.x, e.y});
                m.endX = e.x;
                m.endY = e.y;
                if (m.kind == MarkerKind::Click &&
                    (std::abs(e.x - m.x) > kDragThresholdPx || std::abs(e.y - m.y) > kDragThresholdPx))
                    m.kind = MarkerKind::Drag;
                m.lastTimeUs = e.timeUs;
                changed = size_t(pending_[b]);
            }
            return changed;
        }
        case EventType::ButtonDown: {
            const int b = int(e.button);
            if (b < 1 || b > 5 || markers_.size() >= kMaxMarkers) return std::nullopt;
            Marker m;
            m.kind = MarkerKind::Click;
            m.button = e.button;
            m.x = m.endX = e.x;
            m.y = m.endY = e.y;
            m.active = true;
            m.index = int(markers_.size()) + 1;
            m.lastTimeUs = e.timeUs;
            m.path.push_back({e.x, e.y});
            markers_.push_back(std::move(m));
            pending_[b] = int(markers_.size()) - 1;
            held_ |= uint8_t(1u << b);
            return size_t(pending_[b]);
        }
        case EventType::ButtonUp: {
            const int b = int(e.button);
            if (b < 1 || b > 5 || pending_[b] < 0) return std::nullopt;
            const size_t idx = size_t(pending_[b]);
            Marker& m = markers_[idx];
            m.active = false;
            m.endX = e.x;
            m.endY = e.y;
            if (m.kind == MarkerKind::Drag && m.path.size() < kMaxPathPoints) m.path.push_back({e.x, e.y});
            m.lastTimeUs = e.timeUs;
            pending_[b] = -1;
            held_ &= uint8_t(~(1u << b));
            return idx;
        }
        case EventType::Wheel:
        case EventType::HWheel: {
            const bool horiz = e.type == EventType::HWheel;
            if (e.delta == 0) return std::nullopt;
            if (!markers_.empty()) {  // merge a burst of notches into one marker
                Marker& last = markers_.back();
                if (last.kind == MarkerKind::Scroll && last.horizontal == horiz &&
                    ((last.delta > 0) == (e.delta > 0)) && e.timeUs - last.lastTimeUs <= kScrollMergeUs &&
                    std::abs(e.x - last.x) <= kScrollMergePx && std::abs(e.y - last.y) <= kScrollMergePx) {
                    last.delta += e.delta;
                    last.lastTimeUs = e.timeUs;
                    return markers_.size() - 1;
                }
            }
            if (markers_.size() >= kMaxMarkers) return std::nullopt;
            Marker m;
            m.kind = MarkerKind::Scroll;
            m.button = MouseButton::None;
            m.x = m.endX = e.x;
            m.y = m.endY = e.y;
            m.delta = e.delta;
            m.horizontal = horiz;
            m.index = int(markers_.size()) + 1;
            m.lastTimeUs = e.timeUs;
            markers_.push_back(std::move(m));
            return markers_.size() - 1;
        }
    }
    return std::nullopt;
}

}  // namespace mmr
