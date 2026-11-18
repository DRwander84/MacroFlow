#include "RecorderWidget.h"

#include <QHBoxLayout>
#include <QVBoxLayout>

#include "UiState.h"

namespace mmr {

QString RecorderWidget::keyText(const BindingMap& b, ShortcutAction a) {
    auto it = b.find(a);
    return it == b.end() || it->second.isNone() ? QString("-") : QString::fromStdString(toString(it->second));
}

RecorderWidget::RecorderWidget(MacroController& controller, ShortcutManager& shortcuts, QWidget* parent)
    : QWidget(parent), c_(controller), shortcuts_(shortcuts) {
    indicator_ = new StatusIndicator;
    title_ = new QLabel;
    title_->setObjectName("stateTitle");
    title_->setAlignment(Qt::AlignCenter);
    subtitle_ = new QLabel;
    subtitle_->setObjectName("stateSubtitle");
    subtitle_->setAlignment(Qt::AlignCenter);
    time_ = new QLabel("00:00:00");
    time_->setObjectName("timeLabel");
    time_->setAlignment(Qt::AlignCenter);
    hint_ = new QLabel;
    hint_->setObjectName("hintLabel");
    hint_->setAlignment(Qt::AlignCenter);
    hint_->setWordWrap(true);

    start_ = new QPushButton(" Start");
    start_->setObjectName("primaryButton");
    start_->setIcon(QIcon(":/icons/record.svg"));
    pause_ = new QPushButton(" Pause");
    pause_->setObjectName("secondaryButton");
    pause_->setIcon(QIcon(":/icons/pause.svg"));
    stop_ = new QPushButton(" Stop");
    stop_->setObjectName("secondaryButton");
    stop_->setIcon(QIcon(":/icons/stop.svg"));
    for (auto* b : {start_, pause_, stop_}) {
        b->setMinimumHeight(40);
        b->setCursor(Qt::PointingHandCursor);
        b->setIconSize(QSize(16, 16));
    }

    auto* buttons = new QHBoxLayout;
    buttons->setSpacing(10);
    buttons->addWidget(start_, 3);
    buttons->addWidget(pause_, 2);
    buttons->addWidget(stop_, 2);

    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(24, 14, 24, 18);
    lay->setSpacing(4);
    lay->addWidget(indicator_, 0, Qt::AlignHCenter);
    lay->addWidget(title_);
    lay->addWidget(subtitle_);
    lay->addSpacing(6);
    lay->addWidget(time_);
    lay->addStretch(1);
    lay->addLayout(buttons);
    lay->addSpacing(6);
    lay->addWidget(hint_);

    connect(start_, &QPushButton::clicked, &c_, &MacroController::startRecording);
    connect(pause_, &QPushButton::clicked, &c_, &MacroController::pauseResumeAny);
    connect(stop_, &QPushButton::clicked, &c_, &MacroController::stopAny);
    connect(&c_, &MacroController::stateChanged, this, &RecorderWidget::onState);
    connect(&c_, &MacroController::countdownTick, this, [this](int left) { time_->setText(QString::number(left)); });
    connect(&c_, &MacroController::elapsedChanged, this, [this](qint64 ms) {
        if (state_ == AppState::Recording || state_ == AppState::RecordingPaused || state_ == AppState::Saving)
            time_->setText(formatMs(ms));
    });
    connect(&c_, &MacroController::playbackCycle, this, [this](quint64 n) {
        cycle_ = n;
        if (state_ == AppState::Playing || state_ == AppState::PlaybackPaused) time_->setText(QString("Loop %1").arg(n + 1));
    });
    connect(&shortcuts_, &ShortcutManager::bindingsChanged, this, &RecorderWidget::refreshHints);
    onState(AppState::Idle);
}

void RecorderWidget::refreshHints() {
    const auto& b = shortcuts_.active();
    hint_->setText(QString("Record %1   ·   Pause/Resume %2   ·   Start/Stop playback %3")
                       .arg(keyText(b, ShortcutAction::StartRecording), keyText(b, ShortcutAction::PauseResumeRecording),
                            keyText(b, ShortcutAction::StartStopPlayback)));
    start_->setToolTip("Start recording (" + keyText(b, ShortcutAction::StartRecording) + ")");
    pause_->setToolTip("Pause / Resume (" + keyText(b, ShortcutAction::PauseResumeRecording) + ")");
}

void RecorderWidget::onState(AppState s) {
    state_ = s;
    const StateStyle st = styleFor(s);
    title_->setText(st.title);
    subtitle_->setText(st.subtitle);
    indicator_->setState(st.accent, st.pulse);
    const bool rec = isRecordingState(s), play = isPlaybackState(s);
    start_->setEnabled(s == AppState::Idle);
    pause_->setEnabled(s == AppState::Recording || s == AppState::RecordingPaused || s == AppState::Playing ||
                       s == AppState::PlaybackPaused);
    stop_->setEnabled(rec || play);
    const bool paused = s == AppState::RecordingPaused || s == AppState::PlaybackPaused;
    pause_->setText(paused ? " Resume" : " Pause");
    pause_->setIcon(QIcon(paused ? ":/icons/play.svg" : ":/icons/pause.svg"));
    if (s == AppState::Idle) time_->setText("00:00:00");
    if (s == AppState::Playing && cycle_ == 0) time_->setText("Loop 1");
    refreshHints();
}

}  // namespace mmr
