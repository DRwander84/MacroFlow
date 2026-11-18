#pragma once
#include "core/PlaybackEngine.h"

namespace mmr {

// IInputSink backed by SendInput (mouse and keyboard). Every injected event carries kInjectedTag in dwExtraInfo.
class MouseInput final : public IInputSink {
public:
    bool moveTo(int x, int y) override;
    bool button(MouseButton b, bool down, int x, int y) override;
    bool wheel(bool horizontal, int delta, int x, int y) override;
    bool key(const KeyInfo& key, bool down) override;  // keyboard injection (SendInput), same tag
    AnchorState checkWindow(const WindowAnchor& a, int& dx, int& dy) override;
    AnchorState checkFocus(uint32_t classHash) override;
    AnchorState checkTarget(int x, int y, bool discrete) override;
    void begin() override;  // 1 ms timer resolution while playing
    void end() override;

    static bool cursorPos(int& x, int& y);
};

}  // namespace mmr
