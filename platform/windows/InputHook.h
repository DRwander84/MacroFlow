#pragma once
#include <atomic>
#include <bitset>
#include <cstdint>
#include <functional>
#include <thread>
#include <vector>
#include "core/MacroTypes.h"

namespace mmr {

// One raw input event as seen by the hooks. Mouse events use x/y/delta/button like MacroEvent.
// Key events carry their decoded KeyInfo in `key` (type = KeyDown / KeyUp).
struct RawInputEvent {
    EventType type = EventType::Move;
    MouseButton button = MouseButton::None;
    int x = 0, y = 0, delta = 0;
    KeyInfo key;
    bool hasAnchor = false;  // ButtonDown: the window under the cursor
    WindowAnchor anchor;
    uint32_t focusHash = 0;  // KeyDown: class hash of the focused window (0 = unknown)
};

// Which devices to watch. A device that is not requested gets NO hook at all (zero overhead).
struct HookSources {
    bool mouse = false;
    bool keyboard = false;
};

// A shortcut the app itself owns (start/stop/pause hotkeys). Pressing it is never recorded.
// `mods` uses the kMod* bits of ShortcutTypes.h (Alt=1, Ctrl=2, Shift=4, Win=8).
struct ControlKey {
    uint32_t vk = 0;
    uint32_t mods = 0;
};

// Global WH_MOUSE_LL and/or WH_KEYBOARD_LL hooks on ONE dedicated time-critical thread with its own
// message loop. Reasons:
//  - a low-level hook runs on the thread that installed it; on the UI thread any stall delayed events
//    (late timestamps, bunching) and past Windows' hook timeout the hook was silently REMOVED;
//  - mouse and keyboard callbacks on the same thread are strictly serialised, so the relative order of
//    a key press and a click in the recording is exactly the order Windows delivered them.
// The handler is called on the hook thread and must be thread-safe and tiny.
class InputHook {
public:
    using Handler = std::function<void(const RawInputEvent&)>;

    InputHook() = default;
    ~InputHook() { uninstall(); }
    InputHook(const InputHook&) = delete;
    InputHook& operator=(const InputHook&) = delete;

    bool install(Handler handler, HookSources sources, std::vector<ControlKey> controlKeys,
                 unsigned long* lastError = nullptr);
    void uninstall();  // blocks until the hook thread has finished; never call from the handler
    bool isInstalled() const { return installed_; }

    // Mouse events over this window and key events while this process owns the foreground window
    // (the app's own UI) are never recorded.
    void setExcludedWindow(void* hwnd, int insetPx) { inset_ = insetPx; excluded_ = hwnd; }

private:
    struct Impl;
    friend struct Impl;

    std::thread thread_;
    unsigned long threadId_ = 0;
    std::atomic<bool> installed_{false};
    std::atomic<void*> excluded_{nullptr};
    std::atomic<int> inset_{0};
    Handler handler_;
    std::vector<ControlKey> controlKeys_;  // written before the hook thread starts, then read-only

    // --- hook thread only ---
    unsigned excludedDown_ = 0;           // mouse buttons whose DOWN was ignored
    std::bitset<512> droppedKeys_;        // keys whose DOWN was ignored (id = vk | extended << 8)
    uint8_t modKeys_ = 0;                 // physical modifier state: LShift,RShift,LCtrl,RCtrl,LAlt,RAlt,LWin,RWin
    unsigned long ownPid_ = 0;
};

}  // namespace mmr
