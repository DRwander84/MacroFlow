#include "ShortcutTypes.h"

#include <algorithm>
#include <cctype>
#include <vector>

namespace mmr {
namespace {

struct KeyInfo { uint32_t vk; std::string name; bool bareOk; };

const std::vector<KeyInfo>& keys() {
    static const std::vector<KeyInfo> k = [] {
        std::vector<KeyInfo> v;
        for (int i = 1; i <= 24; ++i) v.push_back({uint32_t(0x6F + i), "F" + std::to_string(i), true});
        for (char c = 'A'; c <= 'Z'; ++c) v.push_back({uint32_t(c), std::string(1, c), false});
        for (char c = '0'; c <= '9'; ++c) v.push_back({uint32_t(c), std::string(1, c), false});
        for (int i = 0; i <= 9; ++i) v.push_back({uint32_t(0x60 + i), "Num" + std::to_string(i), false});
        v.push_back({0x20, "Space", false});
        v.push_back({0x0D, "Enter", false});
        v.push_back({0x09, "Tab", false});
        v.push_back({0x13, "Pause", true});
        v.push_back({0x21, "PageUp", true});
        v.push_back({0x22, "PageDown", true});
        v.push_back({0x23, "End", true});
        v.push_back({0x24, "Home", true});
        v.push_back({0x25, "Left", false});
        v.push_back({0x26, "Up", false});
        v.push_back({0x27, "Right", false});
        v.push_back({0x28, "Down", false});
        v.push_back({0x2D, "Insert", true});
        v.push_back({0x2E, "Delete", true});
        return v;
    }();
    return k;
}

const KeyInfo* find(uint32_t vk) {
    for (const auto& k : keys()) if (k.vk == vk) return &k;
    return nullptr;
}

std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    return s;
}

bool isPauseGroup(ShortcutAction a) {
    return a == ShortcutAction::PauseResumeRecording || a == ShortcutAction::PauseResumePlayback;
}

}  // namespace

bool isKnownKey(uint32_t vk) { return find(vk) != nullptr; }
bool isModifierVk(uint32_t vk) {
    return vk == 0x10 || vk == 0x11 || vk == 0x12 || vk == 0x5B || vk == 0x5C ||
           (vk >= 0xA0 && vk <= 0xA5);
}

std::string toString(const KeyCombo& c) {
    if (c.isNone()) return "None";
    std::string s;
    if (c.mods & kModCtrl) s += "Ctrl+";
    if (c.mods & kModAlt) s += "Alt+";
    if (c.mods & kModShift) s += "Shift+";
    if (c.mods & kModWin) s += "Win+";
    const KeyInfo* k = find(c.vk);
    s += k ? k->name : ("VK" + std::to_string(c.vk));
    return s;
}

std::optional<KeyCombo> keyComboFromString(const std::string& text) {
    std::string t = text;
    t.erase(std::remove_if(t.begin(), t.end(), [](unsigned char c) { return std::isspace(c); }), t.end());
    if (t.empty() || lower(t) == "none") return KeyCombo{};
    KeyCombo c;
    size_t pos = 0;
    bool haveKey = false;
    while (pos <= t.size()) {
        size_t next = t.find('+', pos);
        std::string part = t.substr(pos, next == std::string::npos ? std::string::npos : next - pos);
        if (part.empty()) return std::nullopt;
        const std::string lp = lower(part);
        if (lp == "ctrl" || lp == "control") c.mods |= kModCtrl;
        else if (lp == "alt") c.mods |= kModAlt;
        else if (lp == "shift") c.mods |= kModShift;
        else if (lp == "win") c.mods |= kModWin;
        else {
            if (haveKey) return std::nullopt;
            const KeyInfo* found = nullptr;
            for (const auto& k : keys()) if (lower(k.name) == lp) { found = &k; break; }
            if (!found) return std::nullopt;
            c.vk = found->vk;
            haveKey = true;
        }
        if (next == std::string::npos) break;
        pos = next + 1;
    }
    if (!haveKey) return std::nullopt;
    return c;
}

std::string validateCombo(const KeyCombo& c) {
    if (c.isNone()) return "";
    const KeyInfo* k = find(c.vk);
    if (!k) return "This key is not supported for shortcuts.";
    const std::string name = toString(c);
    if (c.vk == 0x7B) return "F12 is reserved by Windows for debuggers.";
    if (c.mods & kModWin) return "Shortcuts using the Windows key are reserved by the system.";
    if (c.mods == 0 && !k->bareOk) return "Add Ctrl, Alt or Shift: a plain " + k->name + " would break normal typing.";
    if (c.mods == kModAlt && c.vk == 0x73) return "Alt+F4 is reserved by Windows.";
    if (c.mods == kModAlt && c.vk == 0x09) return "Alt+Tab is reserved by Windows.";
    if (c.mods == (kModCtrl | kModAlt) && c.vk == 0x2E) return "Ctrl+Alt+Delete is reserved by Windows.";
    return "";
}

std::vector<ShortcutAction> conflictingActions(const BindingMap& m) {
    std::vector<ShortcutAction> out;
    for (auto i = m.begin(); i != m.end(); ++i) {
        if (i->second.isNone()) continue;
        for (auto j = std::next(i); j != m.end(); ++j) {
            if (i->second != j->second) continue;
            if (isPauseGroup(i->first) && isPauseGroup(j->first)) continue;  // smart shared Pause/Resume
            for (auto a : {i->first, j->first})
                if (std::find(out.begin(), out.end(), a) == out.end()) out.push_back(a);
        }
    }
    return out;
}

std::string validateBindings(const BindingMap& m) {
    for (const auto& [a, c] : m) {
        std::string e = validateCombo(c);
        if (!e.empty()) return std::string(actionLabel(a)) + ": " + e;
    }
    for (auto i = m.begin(); i != m.end(); ++i) {
        if (i->second.isNone()) continue;
        for (auto j = std::next(i); j != m.end(); ++j) {
            if (i->second == j->second) {
                if (isPauseGroup(i->first) && isPauseGroup(j->first)) continue;
                return toString(i->second) + " is already used for \"" + actionLabel(i->first) + "\", so it can't also be used for \"" +
                       actionLabel(j->first) + "\". Pick a different key for one of them.";
            }
        }
    }
    return "";
}

const char* actionKey(ShortcutAction a) {
    switch (a) {
        case ShortcutAction::StartRecording: return "startRecording";
        case ShortcutAction::PauseResumeRecording: return "pauseResumeRecording";
        case ShortcutAction::StartStopPlayback: return "startStopPlayback";
        case ShortcutAction::PauseResumePlayback: return "pauseResumePlayback";
        case ShortcutAction::AutoClickStartStop: return "autoClickStartStop";
        case ShortcutAction::AutoClickPauseResume: return "autoClickPauseResume";
        default: return "";
    }
}

const char* actionLabel(ShortcutAction a) {
    switch (a) {
        case ShortcutAction::StartRecording: return "Start / Stop Recording";
        case ShortcutAction::PauseResumeRecording: return "Pause / Resume Recording";
        case ShortcutAction::StartStopPlayback: return "Start / Stop Playback";
        case ShortcutAction::PauseResumePlayback: return "Pause / Resume Playback";
        case ShortcutAction::AutoClickStartStop: return "Start / Stop Auto Clicker";
        case ShortcutAction::AutoClickPauseResume: return "Pause / Resume Auto Clicker";
        default: return "";
    }
}

BindingMap defaultBindings() {
    BindingMap m;
    m[ShortcutAction::StartRecording] = {0, 0x71};        // F2
    m[ShortcutAction::PauseResumeRecording] = {0, 0x72};  // F3
    m[ShortcutAction::StartStopPlayback] = {0, 0x70};     // F1
    m[ShortcutAction::PauseResumePlayback] = {0, 0x72};   // F3 (smart, shared)
    m[ShortcutAction::AutoClickStartStop] = {0, 0x75};    // F6
    m[ShortcutAction::AutoClickPauseResume] = {0, 0x76};  // F7
    return m;
}

}  // namespace mmr
