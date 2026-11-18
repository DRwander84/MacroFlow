# Architecture
```
app/        main + Application (composition root, hotkey -> state machine dispatch)
core/       pure C++20: MacroTypes (+binary format v1), EventBuffer, RecordingEngine, PlaybackEngine, ShortcutTypes
            MacroController (Qt): state machine tying hook, engines, storage together
platform/windows/  InputHook (WH_MOUSE_LL + WH_KEYBOARD_LL on one thread), MouseInput (SendInput: mouse + keyboard), GlobalHotkeys (RegisterHotKey), DisplayMonitor, AutoStart
storage/    DatabaseManager (SQLite, WAL, migration, corruption recovery), MacroRepository (prepared statements + transactions)
settings/   SettingsManager (QSettings INI), ShortcutManager (validate -> OS register -> persist, with rollback)
ui/         MainWindow, Recorder/Records/Settings widgets, ShortcutEdit, StatusIndicator
tests/      core tests (no deps) + Qt tests (repository, settings/shortcuts)
```
State machine: Idle, RecordingCountdown, Recording, RecordingPaused, Saving, PlaybackCountdown, Playing, PlaybackPaused, Stopping.
Recording and playback never overlap; shortcut changes are locked outside Idle.

Threads: UI thread (hook callback only appends to a chunked buffer) + one playback worker (condition-variable waits on
absolute deadlines). Idle = no timers running, no polling.

Event format v2 (v1 records still load): 24 bytes little-endian per event (time us i64, x i32, y i32, delta i32, type u8, button u8, 2 reserved).
Key events (type 5 KeyDown / 6 KeyUp) reuse the slot: x = VK, y = 0, delta = scan | extended<<16 | unicode<<17.
Validated on load (type/button ranges, monotonic time, coordinate bounds, length, count).
