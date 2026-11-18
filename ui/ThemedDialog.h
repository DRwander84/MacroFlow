#pragma once
#include <QString>
#include <QtGlobal>

class QWidget;

namespace mmr::dialogs {

enum class Kind { Info, Warning, Error };

// App-themed replacements for the native Windows message boxes. Every confirmation puts
// "No" (cancel) on the left and "Yes" (confirm) on the right.
void message(QWidget* parent, const QString& title, const QString& text, Kind kind = Kind::Info);
bool confirm(QWidget* parent, const QString& title, const QString& text, const QString& yesText = "Yes",
             const QString& noText = "No", bool defaultYes = false);
QString getText(QWidget* parent, const QString& title, const QString& label, const QString& initial, bool* ok);


// "How should this record run?": without a pause, or repeated every N seconds / minutes / hours.
// accepted=false when cancelled; intervalMs == 0 means no pause between loops.
struct PlaybackChoice {
    bool accepted = false;
    qint64 intervalMs = 0;
};
PlaybackChoice choosePlaybackMode(QWidget* parent);

}  // namespace mmr::dialogs
