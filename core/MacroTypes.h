#pragma once
// Pure C++ (no Qt / no Windows headers) so the core can be unit-tested anywhere.
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace mmr {

enum class EventType : uint8_t { Move = 0, ButtonDown = 1, ButtonUp = 2, Wheel = 3, HWheel = 4, KeyDown = 5, KeyUp = 6,
                                WindowAnchor = 7,  // x,y = top-left of the window clicked next, delta = its class hash
                                FocusAnchor = 8 };  // delta = class hash of the window that must have focus for the next keys
enum class MouseButton : uint8_t { None = 0, Left = 1, Right = 2, Middle = 3, X1 = 4, X2 = 5 };

// One recorded input event (mouse or keyboard). timeUs is ACTIVE recording time (pauses excluded).
//
// Mouse events: x / y are physical pixels in Virtual Desktop space (may be negative on multi-monitor
// setups); delta is the wheel delta (120 = one notch).
//
// Key events (KeyDown / KeyUp) reuse the same 24-byte slot, so the binary layout does not grow:
//   x     = Windows virtual-key code (VK_*)
//   y     = 0
//   delta = scan code (bits 0-15) | kKeyExtendedBit | kKeyUnicodeBit
// For unicode events (VK_PACKET, e.g. text injected by IMEs / on-screen keyboards) the low 16 bits hold
// the UTF-16 code unit instead of a scan code. Use makeKeyEvent() / keyInfo(), never the raw fields.
struct MacroEvent {
    int64_t timeUs = 0;
    int32_t x = 0;
    int32_t y = 0;
    int32_t delta = 0;  // wheel delta (120 = one notch), 0 for other types
    EventType type = EventType::Move;
    MouseButton button = MouseButton::None;
    bool operator==(const MacroEvent&) const = default;
};

// Format history: v1 = mouse events only, v2 = adds KeyDown / KeyUp, v3 = adds WindowAnchor / FocusAnchor
// (older records stay playable).
inline constexpr int kFormatVersion = 3;
inline constexpr int kMinFormatVersion = 1;
inline constexpr size_t kEventSize = 24;
// Marker placed in dwExtraInfo of every input this app injects ('MMRR').
inline constexpr uint64_t kInjectedTag = 0x4D4D5252ULL;

inline constexpr int32_t kKeyExtendedBit = 1 << 16;
inline constexpr int32_t kKeyUnicodeBit = 1 << 17;
inline constexpr int32_t kKeyDeltaMask = 0x3FFFF;

// Playback synchronisation points. Recorded just before a mouse press (WindowAnchor) and when keyboard
// focus moves to a different kind of window (FocusAnchor). During playback the engine waits (bounded)
// until that window really exists / has focus, and shifts the clicks by the window's position difference.
struct WindowAnchor {
    int32_t left = 0, top = 0;  // top-left of the top-level window under the cursor when recorded
    uint32_t hash = 0;          // FNV-1a of its window class name (never 0)
};

enum class AnchorState { Unsupported, Ready, NotReady };

inline bool isAnchorEvent(EventType t) { return t == EventType::WindowAnchor || t == EventType::FocusAnchor; }

inline MacroEvent makeWindowAnchorEvent(int64_t timeUs, const WindowAnchor& a) {
    MacroEvent e;
    e.timeUs = timeUs;
    e.type = EventType::WindowAnchor;
    e.x = a.left;
    e.y = a.top;
    e.delta = int32_t(a.hash);
    return e;
}

inline MacroEvent makeFocusAnchorEvent(int64_t timeUs, uint32_t hash) {
    MacroEvent e;
    e.timeUs = timeUs;
    e.type = EventType::FocusAnchor;
    e.delta = int32_t(hash);
    return e;
}

inline bool isKeyEvent(EventType t) { return t == EventType::KeyDown || t == EventType::KeyUp; }

// Decoded view of a key event.
struct KeyInfo {
    uint16_t vk = 0;
    uint16_t scan = 0;  // hardware scan code, or the UTF-16 unit when unicode
    bool extended = false;
    bool unicode = false;
    bool operator==(const KeyInfo&) const = default;
};

inline MacroEvent makeKeyEvent(int64_t timeUs, bool down, const KeyInfo& k) {
    MacroEvent e;
    e.timeUs = timeUs;
    e.type = down ? EventType::KeyDown : EventType::KeyUp;
    e.x = k.vk;
    e.y = 0;
    e.delta = int32_t(k.scan) | (k.extended ? kKeyExtendedBit : 0) | (k.unicode ? kKeyUnicodeBit : 0);
    return e;
}

inline KeyInfo keyInfo(const MacroEvent& e) {
    KeyInfo k;
    k.vk = uint16_t(e.x);
    k.scan = uint16_t(e.delta & 0xFFFF);
    k.extended = (e.delta & kKeyExtendedBit) != 0;
    k.unicode = (e.delta & kKeyUnicodeBit) != 0;
    return k;
}

// Identity of a physical key (used to match an UP with its DOWN): the same VK on the main block and the
// numpad (extended flag) are different keys; every unicode character is its own identity.
inline uint32_t keyIdentity(const KeyInfo& k) {
    return k.unicode ? (0x200u | (uint32_t(k.scan) << 10)) : (uint32_t(k.vk) | (k.extended ? 0x100u : 0u));
}

inline int64_t nowMicros() {
    using namespace std::chrono;
    return duration_cast<microseconds>(steady_clock::now().time_since_epoch()).count();
}

std::vector<uint8_t> serializeEvents(const std::vector<MacroEvent>& events);
bool deserializeEvents(const uint8_t* data, size_t size, int formatVersion,
                       std::vector<MacroEvent>& out, std::string* error);
// formatVersion decides which event types are legal (keys need v2, anchors need v3).
bool validateEvents(const std::vector<MacroEvent>& events, std::string* error, int formatVersion = kFormatVersion);

}  // namespace mmr
