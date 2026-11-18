#pragma once
#include <vector>

#include "EventBuffer.h"
#include "MacroTypes.h"

namespace mmr {

struct RecordingResult {
    std::vector<MacroEvent> events;
    int64_t activeDurationUs = 0;
};

// Pure recording logic. Time is injected (nowUs) so pause/resume maths is fully testable.
// Event timestamps use ACTIVE time: time spent paused never counts.
class RecordingEngine {
public:
    enum class State { Idle, Recording, Paused };

    // recordInitialPosition=false (keyboard-only recordings): no cursor Move is stored, so playback never
    // touches the mouse.
    bool start(int64_t nowUs, int x, int y, bool recordInitialPosition = true);
    bool pause(int64_t nowUs);
    bool resume(int64_t nowUs);
    RecordingResult stop(int64_t nowUs);
    void reset();

    void onMove(int64_t nowUs, int x, int y);
    // `anchor` (optional) describes the window under the cursor; it is stored right before an accepted DOWN.
    void onButton(int64_t nowUs, MouseButton b, bool down, int x, int y, const WindowAnchor* anchor = nullptr);
    void onWheel(int64_t nowUs, bool horizontal, int delta, int x, int y);

    // Keyboard. A DOWN is stored only while Recording (auto-repeat DOWNs included: they are replayed 1:1).
    // An UP is stored only if its DOWN was stored (also while paused), so no key can stay stuck.
    // `focusHash` (optional, DOWN only): class hash of the focused window; a FocusAnchor is stored when it
    // differs from the focus last seen by this recording.
    void onKey(int64_t nowUs, const KeyInfo& key, bool down, uint32_t focusHash = 0);
    int64_t activeDurationUs(int64_t nowUs) const;
    State state() const { return state_; }
    size_t eventCount() const { return buf_.size(); }
    bool full() const { return buf_.full(); }

private:
    int64_t activeAt(int64_t nowUs) const;
    void push(MacroEvent e, int64_t nowUs);

    State state_ = State::Idle;
    int64_t startUs_ = 0, pausedTotalUs_ = 0, pauseBeganUs_ = 0, frozenActiveUs_ = 0, lastT_ = 0;
    uint8_t heldMask_ = 0;  // buttons whose DOWN was recorded and UP not yet
    std::vector<KeyInfo> heldKeys_;  // keys whose DOWN was recorded and UP not yet, in press order
    int lastX_ = 0, lastY_ = 0;
    uint32_t lastFocusHash_ = 0;
    EventBuffer buf_;
};

}  // namespace mmr
