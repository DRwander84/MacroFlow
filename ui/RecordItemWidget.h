#pragma once
#include <QFrame>
#include <QLabel>
#include <QPushButton>

#include "core/MacroController.h"
#include "storage/MacroRepository.h"

namespace mmr {

class RecordItemWidget : public QFrame {
    Q_OBJECT
public:
    explicit RecordItemWidget(const RecordMeta& meta, QWidget* parent = nullptr);
    qint64 id() const { return meta_.id; }
    void applyState(AppState state, qint64 activeId);

signals:
    void playClicked(qint64 id);
    void pauseResumeClicked();
    void stopClicked();
    void renameClicked(qint64 id);
    void deleteClicked(qint64 id);

private:
    RecordMeta meta_;
    QLabel* name_;
    QPushButton *play_, *pause_, *stop_, *rename_, *del_;
};

}  // namespace mmr
