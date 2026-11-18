#include "InputHook.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <future>
#include "WindowProbe.h"
#include "core/ShortcutTypes.h"

namespace mmr {

namespace {
InputHook* g_self = nullptr;

enum ModBit : uint8_t { LShift = 1, RShift = 2, LCtrl = 4, RCtrl = 8, LAlt = 16, RAlt = 32, LWin = 64, RWin = 128 };

uint8_t modBitFor(DWORD vk) {
    switch (vk) {
        case VK_LSHIFT: return LShift;
        case VK_RSHIFT: return RShift;
        case VK_LCONTROL: return LCtrl;
        case VK_RCONTROL: return RCtrl;
        case VK_LMENU: return LAlt;
        case VK_RMENU: return RAlt;
        case VK_LWIN: return LWin;
        case VK_RWIN: return RWin;
        default: return 0;
    }
}

uint32_t modsOf(uint8_t m) {
    uint32_t r = 0;
    if (m & (LShift | RShift)) r |= kModShift;
    if (m & (LCtrl | RCtrl)) r |= kModCtrl;
    if (m & (LAlt | RAlt)) r |= kModAlt;
    if (m & (LWin | RWin)) r |= kModWin;
    return r;
}

uint8_t readPhysicalModifiers() {
    uint8_t m = 0;
    auto down = [](int vk) { return (GetAsyncKeyState(vk) & 0x8000) != 0; };
    if (down(VK_LSHIFT)) m |= LShift;
    if (down(VK_RSHIFT)) m |= RShift;
    if (down(VK_LCONTROL)) m |= LCtrl;
    if (down(VK_RCONTROL)) m |= RCtrl;
    if (down(VK_LMENU)) m |= LAlt;
    if (down(VK_RMENU)) m |= RAlt;
    if (down(VK_LWIN)) m |= LWin;
    if (down(VK_RWIN)) m |= RWin;
    return m;
}
}  // namespace

struct InputHook::Impl {
    // ------------------------------------------------------------------ mouse
    static bool overExcluded(const InputHook& h, const POINT& pt) {
        HWND w = static_cast<HWND>(h.excluded_.load());
        if (!w || !IsWindowVisible(w) || IsIconic(w)) return false;
        RECT r;
        if (!GetWindowRect(w, &r)) return false;
        const int inset = h.inset_.load();
        InflateRect(&r, -inset, -inset);
        if (!PtInRect(&r, pt)) return false;
        // The app's rectangle only counts while the app is really what is on screen there. If another program
        // was brought in front of it (e.g. a full-screen editor, or a window opened on that spot), input there
        // belongs to that program and must be recorded. The Z-order lookup runs only for points inside the rect.
        return winprobe::ownWindowOnTopAt(pt.x, pt.y);
    }

    static void dispatchMouse(InputHook& h, WPARAM msg, const MSLLHOOKSTRUCT& i) {
        RawInputEvent e;
        e.x = i.pt.x;
        e.y = i.pt.y;
        switch (msg) {
            case WM_MOUSEMOVE: e.type = EventType::Move; break;
            case WM_LBUTTONDOWN: e.type = EventType::ButtonDown; e.button = MouseButton::Left; break;
            case WM_LBUTTONUP: e.type = EventType::ButtonUp; e.button = MouseButton::Left; break;
            case WM_RBUTTONDOWN: e.type = EventType::ButtonDown; e.button = MouseButton::Right; break;
            case WM_RBUTTONUP: e.type = EventType::ButtonUp; e.button = MouseButton::Right; break;
            case WM_MBUTTONDOWN: e.type = EventType::ButtonDown; e.button = MouseButton::Middle; break;
            case WM_MBUTTONUP: e.type = EventType::ButtonUp; e.button = MouseButton::Middle; break;
            case WM_XBUTTONDOWN:
            case WM_XBUTTONUP:
                e.type = msg == WM_XBUTTONDOWN ? EventType::ButtonDown : EventType::ButtonUp;
                e.button = HIWORD(i.mouseData) == XBUTTON1 ? MouseButton::X1 : MouseButton::X2;
                break;
            case WM_MOUSEWHEEL: e.type = EventType::Wheel; e.delta = static_cast<short>(HIWORD(i.mouseData)); break;
            case WM_MOUSEHWHEEL: e.type = EventType::HWheel; e.delta = static_cast<short>(HIWORD(i.mouseData)); break;
            default: return;
        }
        const bool excluded = overExcluded(h, i.pt);
        if (e.type == EventType::ButtonDown) {
            if (excluded) { h.excludedDown_ |= 1u << unsigned(e.button); return; }
        } else if (e.type == EventType::ButtonUp) {
            const unsigned bit = 1u << unsigned(e.button);
            if (h.excludedDown_ & bit) { h.excludedDown_ &= ~bit; return; }  // matching UP of an ignored DOWN
        } else if (excluded) {
            return;
        }
        if (e.type == EventType::ButtonDown) e.hasAnchor = winprobe::windowAt(e.x, e.y, e.anchor);
        h.handler_(e);
    }

    static LRESULT CALLBACK mouseProc(int code, WPARAM wp, LPARAM lp) {
        if (code == HC_ACTION && g_self && g_self->handler_) {
            const auto* i = reinterpret_cast<const MSLLHOOKSTRUCT*>(lp);
            // Skip input injected by this very app (playback); real + foreign input is kept.
            if (i->dwExtraInfo != static_cast<ULONG_PTR>(kInjectedTag)) dispatchMouse(*g_self, wp, *i);
        }
        return CallNextHookEx(nullptr, code, wp, lp);
    }

    // --------------------------------------------------------------- keyboard
    static bool foregroundIsOwn(const InputHook& h) {
        HWND fg = GetForegroundWindow();
        if (!fg) return false;
        DWORD pid = 0;
        GetWindowThreadProcessId(fg, &pid);
        return pid == h.ownPid_;
    }

    static bool isControlKey(const InputHook& h, uint32_t vk, uint32_t mods) {
        for (const auto& c : h.controlKeys_)
            if (c.vk == vk && c.mods == mods) return true;  // RegisterHotKey needs the exact modifier set
        return false;
    }

    static void dispatchKey(InputHook& h, WPARAM msg, const KBDLLHOOKSTRUCT& k) {
        bool down;
        switch (msg) {
            case WM_KEYDOWN:
            case WM_SYSKEYDOWN: down = true; break;
            case WM_KEYUP:
            case WM_SYSKEYUP: down = false; break;
            default: return;
        }
        // AltGr makes Windows insert a fake Left-Ctrl (scan 0x21D) before Right-Alt. Replaying the real
        // Right-Alt recreates it by itself, so storing it would press Ctrl twice.
        if (k.vkCode == VK_LCONTROL && k.scanCode == 0x21D) return;

        const uint32_t vk = k.vkCode;
        if (vk == 0 || vk > 255) return;

        // The physical modifier state is tracked from EVERY key event, recorded or not.
        if (const uint8_t mb = modBitFor(vk)) {
            if (down) h.modKeys_ |= mb; else h.modKeys_ &= uint8_t(~mb);
        }

        RawInputEvent e;
        e.type = down ? EventType::KeyDown : EventType::KeyUp;
        e.key.vk = uint16_t(vk);
        e.key.extended = (k.flags & LLKHF_EXTENDED) != 0;
        if (vk == VK_PACKET) {  // unicode text injected by an IME / on-screen keyboard
            e.key.unicode = true;
            e.key.extended = false;
            e.key.scan = uint16_t(k.scanCode);  // carries the UTF-16 code unit
            if (e.key.scan == 0) return;
        } else {
            e.key.scan = uint16_t(k.scanCode & 0xFF);
        }

        const size_t slot = size_t(vk) | (e.key.extended ? 0x100u : 0u);
        if (down) {
            if (h.droppedKeys_.test(slot)) return;  // auto-repeat of an ignored press
            if (isControlKey(h, vk, modsOf(h.modKeys_)) || foregroundIsOwn(h)) {
                h.droppedKeys_.set(slot);  // the app's own hotkey / typing inside the app: never recorded
                return;
            }
        } else if (h.droppedKeys_.test(slot)) {
            h.droppedKeys_.reset(slot);  // matching UP of an ignored DOWN
            return;
        }
        if (down) e.focusHash = winprobe::foregroundHash();
        h.handler_(e);
    }

    static LRESULT CALLBACK keyboardProc(int code, WPARAM wp, LPARAM lp) {
        if (code == HC_ACTION && g_self && g_self->handler_) {
            const auto* k = reinterpret_cast<const KBDLLHOOKSTRUCT*>(lp);
            if (k->dwExtraInfo != static_cast<ULONG_PTR>(kInjectedTag)) dispatchKey(*g_self, wp, *k);
        }
        return CallNextHookEx(nullptr, code, wp, lp);
    }
};

// Starts the hook thread + its message loop. Reports the install result through `ready`.
bool InputHook::install(Handler handler, HookSources sources, std::vector<ControlKey> controlKeys,
                        unsigned long* lastError) {
    if (installed_) return true;
    if (!sources.mouse && !sources.keyboard) {
        if (lastError) *lastError = ERROR_INVALID_PARAMETER;
        return false;
    }
    handler_ = std::move(handler);
    controlKeys_ = std::move(controlKeys);
    excludedDown_ = 0;
    droppedKeys_.reset();
    ownPid_ = GetCurrentProcessId();
    g_self = this;

    std::promise<unsigned long> ready;
    auto result = ready.get_future();
    thread_ = std::thread([this, sources, &ready] {
        threadId_ = GetCurrentThreadId();
        SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);
        MSG msg;
        PeekMessageW(&msg, nullptr, WM_USER, WM_USER, PM_NOREMOVE);  // create the thread's message queue
        modKeys_ = sources.keyboard ? readPhysicalModifiers() : uint8_t(0);

        HHOOK hm = nullptr, hk = nullptr;
        DWORD err = 0;
        if (sources.mouse) {
            hm = SetWindowsHookExW(WH_MOUSE_LL, &Impl::mouseProc, GetModuleHandleW(nullptr), 0);
            if (!hm) err = GetLastError() ? GetLastError() : 1;
        }
        if (!err && sources.keyboard) {
            hk = SetWindowsHookExW(WH_KEYBOARD_LL, &Impl::keyboardProc, GetModuleHandleW(nullptr), 0);
            if (!hk) err = GetLastError() ? GetLastError() : 1;
        }
        if (err) {
            if (hm) UnhookWindowsHookEx(hm);
            ready.set_value(err);
            return;
        }
        ready.set_value(0);
        while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        if (hk) UnhookWindowsHookEx(hk);
        if (hm) UnhookWindowsHookEx(hm);
    });

    const unsigned long err = result.get();
    if (err != 0) {
        thread_.join();
        g_self = nullptr;
        handler_ = nullptr;
        if (lastError) *lastError = err;
        return false;
    }
    installed_ = true;
    return true;
}

void InputHook::uninstall() {
    if (installed_) {
        PostThreadMessageW(threadId_, WM_QUIT, 0, 0);
        if (thread_.joinable()) thread_.join();
        installed_ = false;
    }
    if (g_self == this) g_self = nullptr;
    handler_ = nullptr;
}

}  // namespace mmr
