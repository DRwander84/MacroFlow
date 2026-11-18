#include "MacroTypes.h"

#include <cstdlib>

namespace mmr {
namespace {
void put64(uint8_t* p, uint64_t v) { for (int i = 0; i < 8; ++i) p[i] = uint8_t(v >> (8 * i)); }
void put32(uint8_t* p, uint32_t v) { for (int i = 0; i < 4; ++i) p[i] = uint8_t(v >> (8 * i)); }
uint64_t get64(const uint8_t* p) { uint64_t v = 0; for (int i = 7; i >= 0; --i) v = (v << 8) | p[i]; return v; }
uint32_t get32(const uint8_t* p) { uint32_t v = 0; for (int i = 3; i >= 0; --i) v = (v << 8) | p[i]; return v; }
constexpr int64_t kMaxCoord = 1'000'000;
}  // namespace

std::vector<uint8_t> serializeEvents(const std::vector<MacroEvent>& events) {
    std::vector<uint8_t> out(events.size() * kEventSize, 0);
    uint8_t* p = out.data();
    for (const auto& e : events) {
        put64(p, uint64_t(e.timeUs));
        put32(p + 8, uint32_t(e.x));
        put32(p + 12, uint32_t(e.y));
        put32(p + 16, uint32_t(e.delta));
        p[20] = uint8_t(e.type);
        p[21] = uint8_t(e.button);
        p += kEventSize;
    }
    return out;
}

bool validateEvents(const std::vector<MacroEvent>& events, std::string* error, int formatVersion) {
    auto fail = [&](size_t i, const char* msg) {
        if (error) *error = "Event #" + std::to_string(i) + ": " + msg;
        return false;
    };
    const bool allowKeys = formatVersion >= 2, allowAnchors = formatVersion >= 3;
    int64_t last = 0;
    for (size_t i = 0; i < events.size(); ++i) {
        const auto& e = events[i];
        if (uint8_t(e.type) > uint8_t(EventType::FocusAnchor)) return fail(i, "unknown event type");
        if (e.timeUs < 0 || e.timeUs < last) return fail(i, "timestamps are not monotonic");
        last = e.timeUs;
        if (isKeyEvent(e.type)) {
            if (!allowKeys) return fail(i, "key event in a mouse-only record");
            if (e.button != MouseButton::None) return fail(i, "button set on key event");
            if (e.y != 0) return fail(i, "malformed key event");
            if (e.delta < 0 || (e.delta & ~kKeyDeltaMask) != 0) return fail(i, "malformed key event");
            if (e.x < 1 || e.x > 255) return fail(i, "virtual-key code out of range");
            if (keyInfo(e).unicode && keyInfo(e).scan == 0) return fail(i, "empty unicode key event");
            continue;
        }
        if (isAnchorEvent(e.type)) {
            if (!allowAnchors) return fail(i, "anchor event in an older record");
            if (e.button != MouseButton::None) return fail(i, "button set on anchor event");
            if (e.delta == 0) return fail(i, "anchor without window class");
            if (e.type == EventType::FocusAnchor && (e.x != 0 || e.y != 0)) return fail(i, "malformed focus anchor");
            if (std::llabs(e.x) > kMaxCoord || std::llabs(e.y) > kMaxCoord) return fail(i, "coordinate out of range");
            continue;
        }
        if (uint8_t(e.button) > uint8_t(MouseButton::X2)) return fail(i, "unknown button");
        const bool isBtn = e.type == EventType::ButtonDown || e.type == EventType::ButtonUp;
        if (isBtn && e.button == MouseButton::None) return fail(i, "button event without button");
        if (!isBtn && e.button != MouseButton::None) return fail(i, "button set on non-button event");
        if (std::llabs(e.x) > kMaxCoord || std::llabs(e.y) > kMaxCoord) return fail(i, "coordinate out of range");
    }
    return true;
}

bool deserializeEvents(const uint8_t* data, size_t size, int formatVersion,
                       std::vector<MacroEvent>& out, std::string* error) {
    out.clear();
    if (formatVersion < kMinFormatVersion || formatVersion > kFormatVersion) {
        if (error) *error = "Unsupported record format version " + std::to_string(formatVersion);
        return false;
    }
    if (size % kEventSize != 0) {
        if (error) *error = "Record data is truncated or corrupt";
        return false;
    }
    const size_t n = size / kEventSize;
    out.reserve(n);
    for (size_t i = 0; i < n; ++i) {
        const uint8_t* p = data + i * kEventSize;
        MacroEvent e;
        e.timeUs = int64_t(get64(p));
        e.x = int32_t(get32(p + 8));
        e.y = int32_t(get32(p + 12));
        e.delta = int32_t(get32(p + 16));
        e.type = EventType(p[20]);
        e.button = MouseButton(p[21]);
        out.push_back(e);
    }
    if (!validateEvents(out, error, formatVersion)) { out.clear(); return false; }
    return true;
}

}  // namespace mmr
