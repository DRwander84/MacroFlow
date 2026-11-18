#pragma once
#include <QLabel>
#include <QPushButton>
#include <QWidget>

#include "StatusIndicator.h"
#include "core/MacroController.h"
#include "settings/ShortcutManager.h"

namespace mmr {

class RecorderWidget : public QWidget {
    Q_OBJECT
public:
    RecorderWidget(MacroController& controller, ShortcutManager& shortcuts, QWidget* parent = nullptr);

private:
    void onState(AppState s);
    void refreshHints();
    static QString keyText(const BindingMap& b, ShortcutAction a);

    MacroController& c_;
    ShortcutManager& shortcuts_;
    StatusIndicator* indicator_;
    QLabel *title_, *subtitle_, *time_, *hint_;
    QPushButton *start_, *pause_, *stop_;
    AppState state_ = AppState::Idle;
    quint64 cycle_ = 0;
};

}  // namespace mmr
