#pragma once
// Pure shortcut model: parsing, formatting, validation, conflict detection.
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace mmr {

enum class ShortcutAction : int {
    StartRecording = 0,   // toggles: starts when idle, stops while recording
    PauseResumeRecording,
    StartStopPlayback,    // toggles: plays the selected record when idle, stops while playing
    PauseResumePlayback,
    AutoClickStartStop,   // toggles: starts the Auto Clicker when idle, stops it while it runs
    AutoClickPauseResume,
    Count
};

// Values intentionally equal Win32 MOD_ALT/MOD_CONTROL/MOD_SHIFT/MOD_WIN.
inline constexpr uint32_t kModAlt = 1, kModCtrl = 2, kModShift = 4, kModWin = 8;

struct KeyCombo {
    uint32_t mods = 0;
    uint32_t vk = 0;  // Windows virtual-key code, 0 = unassigned
    bool isNone() const { return vk == 0; }
    bool operator==(const KeyCombo&) const = default;
    bool operator<(const KeyCombo& o) const { return mods != o.mods ? mods < o.mods : vk < o.vk; }
};

using BindingMap = std::map<ShortcutAction, KeyCombo>;

std::string toString(const KeyCombo& c);                         // "Ctrl+Alt+F5", "None"
std::optional<KeyCombo> keyComboFromString(const std::string&);  // nullopt if malformed
std::string validateCombo(const KeyCombo& c);                    // "" if OK, else reason
std::string validateBindings(const BindingMap& m);               // "" if OK, else reason
std::vector<ShortcutAction> conflictingActions(const BindingMap& m);  // every action sharing a key with another one
bool isKnownKey(uint32_t vk);
bool isModifierVk(uint32_t vk);

const char* actionKey(ShortcutAction a);    // stable id used in the settings file
const char* actionLabel(ShortcutAction a);  // human label
BindingMap defaultBindings();               // First-run values only: F2 rec, F3 pause, F1 playback, F6/F7 auto clicker ...

// Backend that really registers hotkeys with the OS (RegisterHotKey on Windows).
class IHotkeyBackend {
public:
    virtual ~IHotkeyBackend() = default;
    // Transactional: on failure the previously active set is restored and false returned.
    virtual bool apply(const BindingMap& bindings, std::string* error) = 0;
    virtual void setSuspended(bool suspended) = 0;  // free all hotkeys while user captures a key
};

}  // namespace mmr
