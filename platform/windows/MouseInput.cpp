#include "MouseInput.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <timeapi.h>
#include "WindowProbe.h"

namespace mmr {
namespace {

// Cursor placement is done with SetCursorPos: it takes exact physical pixels in virtual-desktop
// space. (The previous SendInput ABSOLUTE 0..65535 normalisation is only an approximation whose
// rounding differs between Windows versions / monitor layouts / DPI, so the replayed path
// slowly deviated from the recorded one.) Buttons and wheel are still injected with SendInput
// and tagged with kInjectedTag so the recorder ignores them.
INPUT makeInput(DWORD flags, DWORD data = 0) {
    INPUT in{};
    in.type = INPUT_MOUSE;
    in.mi.dwFlags = flags;  // no MOVE flag, no coordinates: position is already exact
    in.mi.mouseData = data;
    in.mi.dwExtraInfo = static_cast<ULONG_PTR>(kInjectedTag);
    return in;
}

bool placeCursor(int x, int y) { return SetCursorPos(x, y) != FALSE; }

bool send(INPUT& in) { return SendInput(1, &in, sizeof(INPUT)) == 1; }

}  // namespace

bool MouseInput::moveTo(int x, int y) { return placeCursor(x, y); }

bool MouseInput::button(MouseButton b, bool down, int x, int y) {
    DWORD flag = 0, data = 0;
    switch (b) {
        case MouseButton::Left: flag = down ? MOUSEEVENTF_LEFTDOWN : MOUSEEVENTF_LEFTUP; break;
        case MouseButton::Right: flag = down ? MOUSEEVENTF_RIGHTDOWN : MOUSEEVENTF_RIGHTUP; break;
        case MouseButton::Middle: flag = down ? MOUSEEVENTF_MIDDLEDOWN : MOUSEEVENTF_MIDDLEUP; break;
        case MouseButton::X1: flag = down ? MOUSEEVENTF_XDOWN : MOUSEEVENTF_XUP; data = XBUTTON1; break;
        case MouseButton::X2: flag = down ? MOUSEEVENTF_XDOWN : MOUSEEVENTF_XUP; data = XBUTTON2; break;
        default: return true;
    }
    if (!placeCursor(x, y)) return false;
    INPUT in = makeInput(flag, data);
    return send(in);
}

bool MouseInput::wheel(bool horizontal, int delta, int x, int y) {
    if (!placeCursor(x, y)) return false;
    INPUT in = makeInput(horizontal ? MOUSEEVENTF_HWHEEL : MOUSEEVENTF_WHEEL, static_cast<DWORD>(delta));
    return send(in);
}

// Keyboard: when the recorded scan code maps back to the recorded virtual key on this system, the key is
// injected by SCAN CODE (what real hardware sends: layout independent, understood by games / remote
// desktops / low-level hooks). Otherwise (Pause, media keys, NumLock-dependent numpad keys, ...) it is
// injected by virtual key with the recorded scan code and extended flag. Unicode events (IME text) are
// injected as UTF-16 units.
bool MouseInput::key(const KeyInfo& k, bool down) {
    INPUT in{};
    in.type = INPUT_KEYBOARD;
    in.ki.dwExtraInfo = static_cast<ULONG_PTR>(kInjectedTag);
    DWORD flags = down ? 0 : KEYEVENTF_KEYUP;
    if (k.unicode) {
        in.ki.wScan = k.scan;
        in.ki.dwFlags = flags | KEYEVENTF_UNICODE;
        return send(in);
    }
    if (k.extended) flags |= KEYEVENTF_EXTENDEDKEY;
    bool byScan = false;
    if (k.scan != 0) {
        const UINT sc = (k.extended ? 0xE000u : 0u) | k.scan;
        byScan = MapVirtualKeyW(sc, MAPVK_VSC_TO_VK_EX) == k.vk;
    }
    if (byScan) {
        in.ki.wScan = k.scan;
        in.ki.dwFlags = flags | KEYEVENTF_SCANCODE;
    } else {
        in.ki.wVk = k.vk;
        in.ki.wScan = k.scan;
        in.ki.dwFlags = flags;
    }
    return send(in);
}

AnchorState MouseInput::checkWindow(const WindowAnchor& a, int& dx, int& dy) { return winprobe::locateWindow(a, dx, dy); }
AnchorState MouseInput::checkFocus(uint32_t classHash) { return winprobe::focusMatches(classHash); }
AnchorState MouseInput::checkTarget(int x, int y, bool discrete) { return winprobe::targetReady(x, y, discrete); }

void MouseInput::begin() {
    // Runs on the playback worker thread. Keep timing exact even when this app is in the background:
    // Windows 10/11 otherwise ignores the 1 ms timer request for occluded/minimized processes and
    // may throttle them (EcoQoS), which bunched events together and made scrolls overshoot.
    timeBeginPeriod(1);
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);
    PROCESS_POWER_THROTTLING_STATE st{};
    st.Version = PROCESS_POWER_THROTTLING_CURRENT_VERSION;
    st.ControlMask = PROCESS_POWER_THROTTLING_EXECUTION_SPEED | PROCESS_POWER_THROTTLING_IGNORE_TIMER_RESOLUTION;
    st.StateMask = 0;  // 0 = throttling OFF for the controlled bits
    SetProcessInformation(GetCurrentProcess(), ProcessPowerThrottling, &st, sizeof(st));
}
void MouseInput::end() { timeEndPeriod(1); }

bool MouseInput::cursorPos(int& x, int& y) {
    POINT p;
    if (!GetCursorPos(&p)) return false;
    x = p.x;
    y = p.y;
    return true;
}

}  // namespace mmr
