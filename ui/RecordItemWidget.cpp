#include "RecordItemWidget.h"

#include <QHBoxLayout>
#include <QLocale>
#include <QStyle>
#include <QVBoxLayout>

#include "UiState.h"

namespace mmr {

static QPushButton* iconButton(const QString& icon, const QString& tip, const QString& objName) {
    auto* b = new QPushButton;
    b->setObjectName(objName);
    b->setIcon(QIcon(icon));
    b->setIconSize(QSize(16, 16));
    b->setFixedSize(32, 30);
    b->setToolTip(tip);
    b->setCursor(Qt::PointingHandCursor);
    return b;
}

RecordItemWidget::RecordItemWidget(const RecordMeta& meta, QWidget* parent) : QFrame(parent), meta_(meta) {
    setObjectName("recordItem");
    setFixedHeight(70);
    name_ = new QLabel(meta.name);
    name_->setObjectName("recordName");
    name_->setToolTip(meta.name);
    auto* info = new QLabel(QString("%1  ·  %2  ·  %3 events")
                                .arg(meta.created.toString("yyyy-MM-dd HH:mm"), formatMs(meta.durationUs / 1000),
                                     QLocale().toString(meta.eventCount)));
    info->setObjectName("recordInfo");

    play_ = iconButton(":/icons/play.svg", "Play (3 s countdown, loops until stopped)", "iconPrimary");
    pause_ = iconButton(":/icons/pause.svg", "Pause / Resume", "iconAmber");
    stop_ = iconButton(":/icons/stop.svg", "Stop", "iconDanger");
    rename_ = iconButton(":/icons/edit.svg", "Rename", "iconButton");
    del_ = iconButton(":/icons/trash.svg", "Delete", "iconButton");

    auto* text = new QVBoxLayout;
    text->setSpacing(3);
    text->addWidget(name_);
    text->addWidget(info);
    auto* row = new QHBoxLayout(this);
    row->setContentsMargins(12, 6, 10, 6);
    row->setSpacing(4);
    row->addLayout(text, 1);
    for (auto* b : {play_, pause_, stop_, rename_, del_}) row->addWidget(b);

    connect(play_, &QPushButton::clicked, this, [this] { emit playClicked(meta_.id); });
    connect(pause_, &QPushButton::clicked, this, &RecordItemWidget::pauseResumeClicked);
    connect(stop_, &QPushButton::clicked, this, &RecordItemWidget::stopClicked);
    connect(rename_, &QPushButton::clicked, this, [this] { emit renameClicked(meta_.id); });
    connect(del_, &QPushButton::clicked, this, [this] { emit deleteClicked(meta_.id); });
    applyState(AppState::Idle, -1);
}

void RecordItemWidget::applyState(AppState s, qint64 activeId) {
    const bool mine = activeId == meta_.id && isPlaybackState(s);
    const bool idle = s == AppState::Idle;
    play_->setVisible(!mine);
    pause_->setVisible(mine);
    stop_->setVisible(mine);
    play_->setEnabled(idle);
    pause_->setEnabled(s == AppState::Playing || s == AppState::PlaybackPaused);
    pause_->setIcon(QIcon(s == AppState::PlaybackPaused ? ":/icons/play.svg" : ":/icons/pause.svg"));
    pause_->setToolTip(s == AppState::PlaybackPaused ? "Resume" : "Pause");
    rename_->setEnabled(idle);
    del_->setEnabled(idle);
    setProperty("active", mine);
    style()->unpolish(this);
    style()->polish(this);
}

}  // namespace mmr
