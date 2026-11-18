#pragma once
#include <QLabel>
#include <QListWidget>
#include <QWidget>
#include <functional>

#include "core/MacroController.h"
#include "settings/SettingsManager.h"
#include "settings/ShortcutManager.h"
#include "storage/MacroRepository.h"

namespace mmr {

class RecordsWidget : public QWidget {
    Q_OBJECT
public:
    RecordsWidget(MacroRepository& repo, MacroController& controller, SettingsManager& settings,
                  ShortcutManager& shortcuts, QWidget* parent = nullptr);
    void refresh();
    qint64 selectedId() const;  // -1 when nothing selected

signals:
    void playRequested(qint64 id);  // MainWindow validates (monitor layout) then starts playback

private:
    void applyStates();
    void refreshEmptyText();
    void rename(qint64 id);
    void remove(qint64 id);

    MacroRepository& repo_;
    MacroController& c_;
    SettingsManager& settings_;
    ShortcutManager& shortcuts_;
    QListWidget* list_;
    QLabel *empty_, *count_;
};

}  // namespace mmr
