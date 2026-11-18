#include "RecordingEngine.h"

#include <algorithm>

namespace mmr {
namespace {
uint8_t bit(MouseButton b) { return uint8_t(1u << uint8_t(b)); }
}

int64_t RecordingEngine::activeAt(int64_t nowUs) const {
    if (state_ == State::Paused) return frozenActiveUs_;
    return std::max<int64_t>(0, nowUs - startUs_ - pausedTotalUs_);
}

void RecordingEngine::push(MacroEvent e, int64_t nowUs) {
    e.timeUs = std::max(activeAt(nowUs), lastT_);  // never go backwards
    lastT_ = e.timeUs;
    buf_.push(e);
}

bool RecordingEngine::start(int64_t nowUs, int x, int y, bool recordInitialPosition) {
    if (state_ != State::Idle) return false;
    reset();
    state_ = State::Recording;
    startUs_ = nowUs;
    lastX_ = x;
    lastY_ = y;
    heldKeys_.reserve(32);  // the hook thread never allocates for realistic key rollover
    if (recordInitialPosition) {
        MacroEvent e;  // initial cursor position so playback begins at the right place
        e.type = EventType::Move;
        e.x = x;
        e.y = y;
        push(e, nowUs);
    }
    return true;
}

bool RecordingEngine::pause(int64_t nowUs) {
    if (state_ != State::Recording) return false;
    frozenActiveUs_ = activeAt(nowUs);
    pauseBeganUs_ = nowUs;
    state_ = State::Paused;
    return true;
}

bool RecordingEngine::resume(int64_t nowUs) {
    if (state_ != State::Paused) return false;
    pausedTotalUs_ += nowUs - pauseBeganUs_;
    state_ = State::Recording;
    return true;
}

void RecordingEngine::onMove(int64_t nowUs, int x, int y) {
    if (state_ != State::Recording) return;
    if (x == lastX_ && y == lastY_) return;  // exact duplicate: lossless to drop
    lastX_ = x;
    lastY_ = y;
    MacroEvent e;
    e.type = EventType::Move;
    e.x = x;
    e.y = y;
    push(e, nowUs);
}

void RecordingEngine::onButton(int64_t nowUs, MouseButton b, bool down, int x, int y, const WindowAnchor* anchor) {
    if (b == MouseButton::None) return;
    if (down) {
        if (state_ != State::Recording) return;
        heldMask_ |= bit(b);
    } else {
        // An UP is recorded only if its DOWN was recorded (even while paused, so no button sticks).
        if (state_ == State::Idle || !(heldMask_ & bit(b))) return;
        heldMask_ &= uint8_t(~bit(b));
    }
    lastX_ = x;
    lastY_ = y;
    if (down && anchor && anchor->hash != 0) {
        push(makeWindowAnchorEvent(0, *anchor), nowUs);
        lastFocusHash_ = anchor->hash;  // a click normally moves focus to that window
    }
    MacroEvent e;
    e.type = down ? EventType::ButtonDown : EventType::ButtonUp;
    e.button = b;
    e.x = x;
    e.y = y;
    push(e, nowUs);
}

void RecordingEngine::onWheel(int64_t nowUs, bool horizontal, int delta, int x, int y) {
    if (state_ != State::Recording || delta == 0) return;
    lastX_ = x;
    lastY_ = y;
    MacroEvent e;
    e.type = horizontal ? EventType::HWheel : EventType::Wheel;
    e.delta = delta;
    e.x = x;
    e.y = y;
    push(e, nowUs);
}

void RecordingEngine::onKey(int64_t nowUs, const KeyInfo& key, bool down, uint32_t focusHash) {
    if (key.vk == 0) return;
    const uint32_t id = keyIdentity(key);
    auto it = heldKeys_.begin();
    for (; it != heldKeys_.end(); ++it)
        if (keyIdentity(*it) == id) break;
    if (down) {
        if (state_ != State::Recording) return;
        if (it == heldKeys_.end()) heldKeys_.push_back(key);  // else: auto-repeat, recorded again
    } else {
        if (state_ == State::Idle || it == heldKeys_.end()) return;
        heldKeys_.erase(it);
    }
    if (down && focusHash != 0 && focusHash != lastFocusHash_) {
        push(makeFocusAnchorEvent(0, focusHash), nowUs);
        lastFocusHash_ = focusHash;
    }
    push(makeKeyEvent(0, down, key), nowUs);
}

int64_t RecordingEngine::activeDurationUs(int64_t nowUs) const {
    return state_ == State::Idle ? 0 : activeAt(nowUs);
}

RecordingResult RecordingEngine::stop(int64_t nowUs) {
    RecordingResult r;
    if (state_ == State::Idle) return r;
    const int64_t finalT = std::max(activeAt(nowUs), lastT_);
    for (uint8_t b = 1; b <= uint8_t(MouseButton::X2); ++b) {  // release anything still held
        if (heldMask_ & bit(MouseButton(b))) {
            MacroEvent e;
            e.type = EventType::ButtonUp;
            e.button = MouseButton(b);
            e.x = lastX_;
            e.y = lastY_;
            e.timeUs = finalT;
            lastT_ = finalT;
            buf_.push(e);
        }
    }
    for (size_t i = heldKeys_.size(); i-- > 0;) {  // release keys still held, last pressed first
        lastT_ = finalT;
        buf_.push(makeKeyEvent(finalT, false, heldKeys_[i]));
    }
    r.activeDurationUs = finalT;
    r.events = buf_.take();
    state_ = State::Idle;
    heldMask_ = 0;
    heldKeys_.clear();
    return r;
}

void RecordingEngine::reset() {
    state_ = State::Idle;
    startUs_ = pausedTotalUs_ = pauseBeganUs_ = frozenActiveUs_ = lastT_ = 0;
    heldMask_ = 0;
    heldKeys_.clear();
    lastX_ = lastY_ = 0;
    lastFocusHash_ = 0;
    buf_.clear();
}

}  // namespace mmr
