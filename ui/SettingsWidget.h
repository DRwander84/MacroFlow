#pragma once
#include <QCheckBox>
#include <QLabel>
#include <QPushButton>
#include <QWidget>
#include <map>

#include "ShortcutEdit.h"
#include "core/MacroController.h"
#include "settings/SettingsManager.h"
#include "settings/ShortcutManager.h"

namespace mmr {

class SettingsWidget : public QWidget {
    Q_OBJECT
public:
    SettingsWidget(SettingsManager& settings, ShortcutManager& shortcuts, MacroController& controller,
                   QWidget* parent = nullptr);

signals:
    void generalChanged();

private:
    BindingMap staged() const;
    void loadEditors(const BindingMap& b);
    void refreshButtons();
    void showStatus(const QString& text, bool error);
    void save();
    void reset();
    void saveGeneral();

    SettingsManager& settings_;
    ShortcutManager& shortcuts_;
    MacroController& c_;
    std::map<ShortcutAction, ShortcutEdit*> edits_;
    QPushButton *saveBtn_, *resetBtn_;
    QLabel* status_;
    void refreshInputHint();
    QCheckBox *autostart_, *restoreTab_, *confirmDel_, *overlay_, *recMouse_, *recKeyboard_;
    QLabel* inputHint_;
};

}  // namespace mmr
