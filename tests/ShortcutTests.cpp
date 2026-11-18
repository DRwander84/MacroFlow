#include "TestSupport.h"
#include "core/ShortcutTypes.h"
using namespace mmr;

void defaults() {
    auto d = defaultBindings();
    CHECK(d[ShortcutAction::StartRecording].vk == 0x71);   // F2
    CHECK(d[ShortcutAction::StartStopPlayback].vk == 0x70);     // F1
    CHECK(d[ShortcutAction::PauseResumeRecording].vk == 0x72 && d[ShortcutAction::PauseResumePlayback].vk == 0x72);
    CHECK(validateBindings(d).empty());                    // shared F3 is the allowed smart pair
}

void parseFormat() {
    auto c = keyComboFromString("Ctrl+Alt+F5");
    CHECK(c && c->mods == (kModCtrl | kModAlt) && c->vk == 0x74);
    CHECK(toString(*c) == "Ctrl+Alt+F5");
    CHECK(keyComboFromString("shift + a")->vk == 'A');
    CHECK(keyComboFromString("None")->isNone());
    CHECK(!keyComboFromString("Ctrl+"));
    CHECK(!keyComboFromString("Ctrl+Foo"));
    CHECK(!keyComboFromString("F1+F2"));
    for (int i = 0; i < int(ShortcutAction::Count); ++i) {     // every default round-trips
        auto k = defaultBindings()[ShortcutAction(i)];
        CHECK(*keyComboFromString(toString(k)) == k);
    }
}

void invalidCombos() {
    CHECK(!validateCombo({0, 'A'}).empty());                  // bare letter
    CHECK(!validateCombo({0, 0x7B}).empty());                 // F12
    CHECK(!validateCombo({kModAlt, 0x73}).empty());           // Alt+F4
    CHECK(!validateCombo({kModWin, 'A'}).empty());
    CHECK(!validateCombo({0, 0x1234}).empty());
    CHECK(validateCombo({kModCtrl | kModShift, 'R'}).empty());
    CHECK(validateCombo({0, 0x70}).empty());                  // bare F1 ok
}

void conflicts() {
    auto m = defaultBindings();
    m[ShortcutAction::StartStopPlayback] = m[ShortcutAction::StartRecording];   // F2 twice
    CHECK(!validateBindings(m).empty());
    m = defaultBindings();
    m[ShortcutAction::StartStopPlayback] = m[ShortcutAction::PauseResumePlayback]; // F3 for start/stop+pause
    CHECK(!validateBindings(m).empty());
    m = defaultBindings();
    m[ShortcutAction::PauseResumeRecording] = {kModCtrl, 'P'};
    m[ShortcutAction::PauseResumePlayback] = {kModCtrl, 'P'};               // shared pair is fine
    CHECK(validateBindings(m).empty());
    m = defaultBindings();
    m[ShortcutAction::PauseResumeRecording] = {};                           // unassigned never conflicts
    m[ShortcutAction::StartStopPlayback] = {};
    CHECK(validateBindings(m).empty());
}

int main() { RUN(defaults); RUN(parseFormat); RUN(invalidCombos); RUN(conflicts); TEST_MAIN_END(); }
