#include "WindowProbe.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <dwmapi.h>
#include <cstdlib>

namespace mmr::winprobe {

namespace {

uint32_t classHash(HWND w) {
    wchar_t name[256];
    const int n = GetClassNameW(w, name, 256);
    if (n <= 0) return 0;
    uint32_t h = 2166136261u;  // FNV-1a over the UTF-16 units
    for (int i = 0; i < n; ++i) {
        h ^= uint32_t(name[i]);
        h *= 16777619u;
    }
    return h ? h : 1u;
}

bool isOwnWindow(HWND w) {
    DWORD pid = 0;
    GetWindowThreadProcessId(w, &pid);
    return pid == GetCurrentProcessId();
}

bool isCloaked(HWND w) {  // hidden UWP / virtual-desktop windows are "visible" but not on screen
    DWORD cloaked = 0;
    return SUCCEEDED(DwmGetWindowAttribute(w, DWMWA_CLOAKED, &cloaked, sizeof(cloaked))) && cloaked != 0;
}

// True when the window's thread does not pump messages (hung / still starting). Runs ONLY on the playback worker
// thread, never inside a hook: it may block, but only for `timeoutMs`. Windows we are not allowed to message
// (elevated, UIPI) are treated as responsive: we cannot tell, and waiting on them forever would be wrong.
bool isBusy(HWND w, UINT timeoutMs) {
    if (IsHungAppWindow(w)) return true;
    DWORD_PTR res = 0;
    SetLastError(0);
    if (SendMessageTimeoutW(w, WM_NULL, 0, 0, SMTO_ABORTIFHUNG | SMTO_BLOCK, timeoutMs, &res) != 0) return false;
    const DWORD err = GetLastError();
    return err != ERROR_ACCESS_DENIED;
}

struct Search {
    uint32_t hash = 0;
    int left = 0, top = 0;
    bool found = false;
    long long bestDist = 0;
    int bestLeft = 0, bestTop = 0;
};

BOOL CALLBACK onWindow(HWND w, LPARAM lp) {
    auto* s = reinterpret_cast<Search*>(lp);
    if (!IsWindowVisible(w) || IsIconic(w)) return TRUE;
    if (classHash(w) != s->hash) return TRUE;
    if (isOwnWindow(w) || isCloaked(w)) return TRUE;
    if (isBusy(w, 30)) return TRUE;  // exists but is not answering yet: not "ready"
    RECT r;
    if (!GetWindowRect(w, &r)) return TRUE;
    const long long d = std::llabs(static_cast<long long>(r.left) - s->left) + std::llabs(static_cast<long long>(r.top) - s->top);
    if (!s->found || d < s->bestDist) {  // strict <: on a tie the window higher in the Z-order wins
        s->found = true;
        s->bestDist = d;
        s->bestLeft = r.left;
        s->bestTop = r.top;
    }
    return s->bestDist != 0 || !s->found ? TRUE : FALSE;  // exact match: stop looking
}

// Runs inside the low-level hook callback, which Windows abandons after ~300 ms: so NO call here may send
// a message to another process (WindowFromPoint sends WM_NCHITTEST and could wait on a frozen program).
// EnumWindows + GetWindowRect/GetClassName only read window data and never block.
struct PointSearch {
    POINT pt{};
    WindowAnchor result;
    bool found = false;
};

BOOL CALLBACK onWindowAtPoint(HWND w, LPARAM lp) {
    auto* s = reinterpret_cast<PointSearch*>(lp);
    if (!IsWindowVisible(w) || IsIconic(w)) return TRUE;
    RECT r;
    if (!GetWindowRect(w, &r) || !PtInRect(&r, s->pt)) return TRUE;
    const LONG_PTR ex = GetWindowLongPtrW(w, GWL_EXSTYLE);
    if (ex & WS_EX_TRANSPARENT) return TRUE;  // click-through overlays never receive the click
    if (isOwnWindow(w) || isCloaked(w)) return TRUE;
    const uint32_t h = classHash(w);
    if (h == 0) return TRUE;
    s->result.left = r.left;
    s->result.top = r.top;
    s->result.hash = h;
    s->found = true;
    return FALSE;  // first hit in Z-order = the window the click lands on
}

struct TopSearch {
    POINT pt{};
    HWND hwnd = nullptr;
};

BOOL CALLBACK onTopAtPoint(HWND w, LPARAM lp) {
    auto* s = reinterpret_cast<TopSearch*>(lp);
    if (!IsWindowVisible(w) || IsIconic(w)) return TRUE;
    RECT r;
    if (!GetWindowRect(w, &r) || !PtInRect(&r, s->pt)) return TRUE;
    if (GetWindowLongPtrW(w, GWL_EXSTYLE) & WS_EX_TRANSPARENT) return TRUE;  // click-through: never receives input
    if (isCloaked(w)) return TRUE;
    s->hwnd = w;
    return FALSE;  // first hit in Z-order = topmost
}

}  // namespace

bool ownWindowOnTopAt(int x, int y) {
    TopSearch s;
    s.pt = POINT{x, y};
    EnumWindows(&onTopAtPoint, reinterpret_cast<LPARAM>(&s));
    return s.hwnd && isOwnWindow(s.hwnd);
}

bool windowAt(int x, int y, WindowAnchor& out) {
    PointSearch s;
    s.pt = POINT{x, y};
    EnumWindows(&onWindowAtPoint, reinterpret_cast<LPARAM>(&s));
    if (!s.found) return false;
    out = s.result;
    return true;
}

uint32_t foregroundHash() {
    HWND w = GetForegroundWindow();
    if (!w || isOwnWindow(w)) return 0;
    return classHash(w);
}

AnchorState locateWindow(const WindowAnchor& a, int& dx, int& dy) {
    Search s;
    s.hash = a.hash;
    s.left = a.left;
    s.top = a.top;
    EnumWindows(&onWindow, reinterpret_cast<LPARAM>(&s));  // top-level windows, topmost first
    if (!s.found) return AnchorState::NotReady;
    dx = s.bestLeft - a.left;
    dy = s.bestTop - a.top;
    return AnchorState::Ready;
}

namespace {
struct HwndAtPoint {
    POINT pt{};
    HWND hwnd = nullptr;
};
BOOL CALLBACK onHwndAtPoint(HWND w, LPARAM lp) {
    auto* s = reinterpret_cast<HwndAtPoint*>(lp);
    if (!IsWindowVisible(w) || IsIconic(w)) return TRUE;
    RECT r;
    if (!GetWindowRect(w, &r) || !PtInRect(&r, s->pt)) return TRUE;
    if (GetWindowLongPtrW(w, GWL_EXSTYLE) & WS_EX_TRANSPARENT) return TRUE;
    if (isCloaked(w)) return TRUE;
    s->hwnd = w;
    return FALSE;
}
}  // namespace

AnchorState targetReady(int x, int y, bool discrete) {
    // Plain moves reuse the last verdict for a few ms (a 1 kHz mouse stream must stay cheap); clicks and wheel
    // ticks always look again. A "busy" verdict is never cached: it is re-checked on every poll.
    static thread_local ULONGLONG validUntil = 0;
    const ULONGLONG now = GetTickCount64();
    if (!discrete && now < validUntil) return AnchorState::Ready;

    HwndAtPoint s;
    s.pt = POINT{x, y};
    EnumWindows(&onHwndAtPoint, reinterpret_cast<LPARAM>(&s));
    if (!s.hwnd || isOwnWindow(s.hwnd)) {
        validUntil = now + 30;
        return AnchorState::Ready;
    }
    if (isBusy(s.hwnd, 40)) return AnchorState::NotReady;
    validUntil = now + 30;
    return AnchorState::Ready;
}

AnchorState focusMatches(uint32_t classHash) {
    return foregroundHash() == classHash ? AnchorState::Ready : AnchorState::NotReady;
}

}  // namespace mmr::winprobe
