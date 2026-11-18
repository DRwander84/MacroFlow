#include <QCoreApplication>
#include <QFile>
#include <QTemporaryDir>

#include "TestSupport.h"
#include "settings/SettingsManager.h"
#include "settings/ShortcutManager.h"
using namespace mmr;

struct FakeBackend : IHotkeyBackend {
    BindingMap active; std::string failOn;  // fail when this combo string is requested
    bool apply(const BindingMap& b, std::string* e) override {
        for (auto& [a, c] : b) if (!failOn.empty() && toString(c) == failOn) { if (e) *e = failOn + " is already in use by Windows or another application."; return false; }
        active = b; return true;
    }
    void setSuspended(bool) override {}
};

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    QTemporaryDir dir;
    const QString path = dir.filePath("settings.ini");
    QString err;
    {
        SettingsManager s(path);
        CHECK(s.shortcuts() == defaultBindings());
        CHECK(!s.general().startWithWindows);                  // default OFF
        FakeBackend be; ShortcutManager sm(s, be);
        CHECK(sm.initialize().isEmpty());
        BindingMap m = defaultBindings();
        m[ShortcutAction::StartRecording] = {kModCtrl | kModShift, 'R'};
        CHECK(sm.apply(m).ok);                                 // custom shortcut saved
        auto bad = defaultBindings(); bad[ShortcutAction::PauseResumeRecording] = bad[ShortcutAction::StartStopPlayback];
        CHECK(!sm.apply(bad).ok);                              // duplicate detected
        be.failOn = "Ctrl+Alt+F9";
        auto os = m; os[ShortcutAction::StartStopPlayback] = {kModCtrl | kModAlt, 0x78};
        auto r = sm.apply(os);
        CHECK(!r.ok && !r.error.isEmpty());                    // OS refused
        CHECK(sm.active() == m && s.shortcuts() == m);         // previous kept (memory + disk)
        sm.setLocked(true);
        CHECK(!sm.apply(defaultBindings()).ok);                // locked mid-operation
        sm.setLocked(false);
        CHECK(s.general().recordMouse && !s.general().recordKeyboard);   // defaults: mouse on, keyboard off
        GeneralSettings g; g.confirmDelete = false; g.restoreLastTab = false;
        g.recordMouse = false; g.recordKeyboard = true;
        CHECK(s.setGeneral(g, &err)); CHECK(s.setLastTab(2));
    }
    {   // restart: everything restored and re-registered
        SettingsManager s(path);
        CHECK(s.shortcuts().at(ShortcutAction::StartRecording) == (KeyCombo{kModCtrl | kModShift, 'R'}));
        CHECK(!s.general().confirmDelete && !s.general().restoreLastTab && s.lastTab() == 2);
        CHECK(!s.general().recordMouse && s.general().recordKeyboard);   // input mode survives a restart
        FakeBackend be; ShortcutManager sm(s, be);
        sm.initialize();
        CHECK(be.active == s.shortcuts());
        CHECK(sm.resetToDefaults().ok && s.shortcuts() == defaultBindings());
    }
    {   // invalid values fall back to defaults
        QFile f(path); CHECK(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
        f.write("[shortcuts]\nstartRecording=Ctrl+Nonsense\nstartStopPlayback=A\n[ui]\nlastTab=99\n"); f.close();
        SettingsManager s(path);
        CHECK(s.shortcuts() == defaultBindings() && s.lastTab() == 0 && s.recoveredFromProblem());
    }
    {   // binary garbage must not crash
        QFile f(path); CHECK(f.open(QIODevice::WriteOnly | QIODevice::Truncate)); f.write(QByteArray(2000, '\xff')); f.close();
        SettingsManager s(path);
        CHECK(s.shortcuts() == defaultBindings());
    }
    TEST_MAIN_END();
}
