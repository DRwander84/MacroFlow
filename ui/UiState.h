#pragma once
#include <QColor>
#include <QString>

#include "core/MacroController.h"

namespace mmr {

struct StateStyle {
    QString title;
    QString subtitle;
    QColor accent;
    bool pulse;
};

inline StateStyle styleFor(AppState s) {
    const QColor red(255, 45, 70), amber(255, 176, 46), green(70, 224, 160), idle(120, 124, 140);
    switch (s) {
        case AppState::Idle: return {"Ready", "Press the record shortcut or Start", idle, false};
        case AppState::RecordingCountdown: return {"Get ready", "Recording starts in a moment", amber, false};
        case AppState::Recording: return {"Recording", "Your input is being captured", red, true};
        case AppState::RecordingPaused: return {"Recording paused", "Nothing is captured until you resume", amber, false};
        case AppState::Saving: return {"Saving", "Storing your recording", idle, false};
        case AppState::PlaybackCountdown: return {"Playback starts in", "Hands off the mouse and keyboard", amber, false};
        case AppState::Playing: return {"Playing", "Looping until you stop it", green, true};
        case AppState::PlaybackPaused: return {"Playback paused", "Resume continues from the same point", amber, false};
        case AppState::Stopping: return {"Stopping", "Releasing held keys and buttons", idle, false};
    }
    return {"", "", idle, false};
}

// Hours:minutes:seconds, no milliseconds.
inline QString formatMs(qint64 ms) {
    if (ms < 0) ms = 0;
    const qint64 totalSec = ms / 1000;
    return QString("%1:%2:%3")
        .arg(totalSec / 3600, 2, 10, QChar('0'))
        .arg((totalSec / 60) % 60, 2, 10, QChar('0'))
        .arg(totalSec % 60, 2, 10, QChar('0'));
}

}  // namespace mmr
