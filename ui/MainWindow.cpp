#include "MainWindow.h"

#include <QCloseEvent>
#include <QDateTime>
#include <QGraphicsOpacityEffect>
#include <QGuiApplication>
#include <QWindow>
#include <QHBoxLayout>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QDesktopServices>
#include <QUrl>
#include <QPushButton>
#include <QScreen>
#include <QStyleHints>
#include <QTimer>
#include <QVBoxLayout>
#include <memory>
#include <utility>

#include "ThemedDialog.h"
#include "UiState.h"
#include "platform/windows/DisplayMonitor.h"

namespace mmr {
namespace {
constexpr int kMargin = 12;   // room for the soft shadow
constexpr int kRadius = 20;

// Looks like a plain label (same style, no underline/colour change); only the hand cursor hints at the link.
class LinkLabel : public QLabel {
public:
    LinkLabel(const QString& text, QUrl url) : QLabel(text), url_(std::move(url)) { setCursor(Qt::PointingHandCursor); }

protected:
    // A click opens the link; pressing and dragging moves the window like the rest of the title bar.
    void mousePressEvent(QMouseEvent* e) override {
        if (e->button() == Qt::LeftButton) { pressed_ = true; pressPos_ = e->position().toPoint(); e->accept(); }
        else QLabel::mousePressEvent(e);
    }
    void mouseMoveEvent(QMouseEvent* e) override {
        if (pressed_ && (e->position().toPoint() - pressPos_).manhattanLength() >= QGuiApplication::styleHints()->startDragDistance()) {
            pressed_ = false;
            if (auto* w = window()->windowHandle()) w->startSystemMove();
        }
    }
    void mouseReleaseEvent(QMouseEvent* e) override {
        if (pressed_ && e->button() == Qt::LeftButton && rect().contains(e->position().toPoint())) QDesktopServices::openUrl(url_);
        pressed_ = false;
    }

private:
    QUrl url_;
    QPoint pressPos_;
    bool pressed_ = false;
};
}  // namespace

MainWindow::MainWindow(MacroController& controller, MacroRepository& repo, SettingsManager& settings,
                       ShortcutManager& shortcuts, AutoClicker& autoClicker, QWidget* parent)
    : QWidget(parent, Qt::FramelessWindowHint | Qt::WindowMinimizeButtonHint), c_(controller), repo_(repo), settings_(settings),
      autoClicker_(autoClicker) {
    setAttribute(Qt::WA_TranslucentBackground);
    setWindowTitle("MacroFlow");
    setWindowIcon(QIcon(":/icons/logo.png"));
    setFixedSize(460 + 2 * kMargin, 560 + 2 * kMargin);

    auto* title = new LinkLabel("MACROFLOW", QUrl("https://github.com/DRwander84"));
    title->setObjectName("appTitle");
    auto* minBtn = new QPushButton;
    minBtn->setObjectName("titleButton");
    minBtn->setIcon(QIcon(":/icons/minimize.svg"));
    auto* closeBtn = new QPushButton;
    closeBtn->setObjectName("titleCloseButton");
    closeBtn->setIcon(QIcon(":/icons/close.svg"));
    for (auto* b : {minBtn, closeBtn}) {
        b->setFixedSize(30, 26);
        b->setCursor(Qt::PointingHandCursor);
        b->setFocusPolicy(Qt::NoFocus);  // no dotted focus rectangle when the window gets focus
    }
    connect(minBtn, &QPushButton::clicked, this, &QWidget::showMinimized);
    connect(closeBtn, &QPushButton::clicked, this, &QWidget::close);

    auto* bar = new QHBoxLayout;
    bar->setContentsMargins(18, 10, 12, 0);
    bar->addWidget(title);
    bar->addStretch(1);
    bar->addWidget(minBtn);
    bar->addWidget(closeBtn);

    tabs_ = new QTabBar;
    tabs_->setObjectName("mainTabs");
    tabs_->setExpanding(true);
    tabs_->setDrawBase(false);
    tabs_->addTab("Auto Clicker");
    tabs_->addTab("Recorder");
    tabs_->addTab("Records");
    tabs_->addTab("Settings");
    tabs_->setCursor(Qt::PointingHandCursor);

    pages_ = new QStackedWidget;
    autoPage_ = new AutoClickerWidget(autoClicker_, c_, settings_, shortcuts);
    pages_->addWidget(autoPage_);
    pages_->addWidget(new RecorderWidget(c_, shortcuts));
    records_ = new RecordsWidget(repo_, c_, settings_, shortcuts);
    pages_->addWidget(records_);
    auto* settingsPage = new SettingsWidget(settings_, shortcuts, c_);
    pages_->addWidget(settingsPage);

    notice_ = new QLabel;
    notice_->setObjectName("noticeBar");
    notice_->setWordWrap(true);
    notice_->hide();

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(kMargin + 2, kMargin + 2, kMargin + 2, kMargin + 6);
    root->setSpacing(6);
    root->addLayout(bar);
    root->addWidget(tabs_);
    root->addWidget(notice_);
    root->addWidget(pages_, 1);

    accentAnim_ = new QPropertyAnimation(this, "accent", this);
    accentAnim_->setDuration(260);

    badge_ = std::make_unique<TaskbarBadge>(this);
    connect(tabs_, &QTabBar::currentChanged, this, &MainWindow::switchTab);
    connect(&c_, &MacroController::stateChanged, this, &MainWindow::onState);
    connect(&autoClicker_, &AutoClicker::stateChanged, this, &MainWindow::onAutoClickState);
    connect(autoPage_, &AutoClickerWidget::notice, this, &MainWindow::showNotice);
    // Playback started from anywhere (Records buttons or the shortcut): show the countdown/clock on the Recorder tab.
    connect(&c_, &MacroController::stateChanged, this, [this](AppState s) {
        if (s == AppState::PlaybackCountdown && tabs_->currentIndex() != 1) tabs_->setCurrentIndex(1);
    });
    connect(&c_, &MacroController::recordingReady, this, [this] { QTimer::singleShot(0, this, &MainWindow::askNameAndSave); });
    connect(&c_, &MacroController::errorOccurred, this, [this](const QString& t, const QString& m) {
        dialogs::message(this, t, m, dialogs::Kind::Warning);
    });
    // Both Mouse and Keyboard recording are off: recording does not start, one OK-only alarm explains why.
    auto alarmOpen = std::make_shared<bool>(false);  // a second hotkey press must not stack dialogs
    connect(&c_, &MacroController::noInputSelected, this, [this, alarmOpen] {
        if (*alarmOpen) return;
        *alarmOpen = true;
        if (isMinimized()) showNormal();
        show();
        raise();
        activateWindow();
        dialogs::message(this, "Can't start recording",
                         "Mouse and Keyboard recording are both turned off.\n"
                         "Turn on at least one of them in Settings to start recording.",
                         dialogs::Kind::Warning);
        *alarmOpen = false;
    });
    connect(records_, &RecordsWidget::playRequested, this, &MainWindow::requestPlay);

    // Warn when the monitor layout changes (can make recorded coordinates land in the wrong place).
    auto layoutChanged = [this] { showNotice("Display layout changed. Recorded coordinates may not match this setup."); };
    connect(qGuiApp, &QGuiApplication::screenAdded, this, layoutChanged);
    connect(qGuiApp, &QGuiApplication::screenRemoved, this, layoutChanged);
    connect(qGuiApp, &QGuiApplication::primaryScreenChanged, this, layoutChanged);

    if (settings_.general().restoreLastTab) tabs_->setCurrentIndex(settings_.lastTab());
    pages_->setCurrentIndex(tabs_->currentIndex());
    onState(AppState::Idle);
}

void MainWindow::showNotice(const QString& text) {
    notice_->setText(text);
    notice_->show();
    QTimer::singleShot(9000, notice_, &QWidget::hide);
}

void MainWindow::switchTab(int index) {
    pages_->setCurrentIndex(index);
    if (index == 2) records_->refresh();
    QString err;
    settings_.setLastTab(index, &err);  // writes only when it actually changed
    auto* eff = new QGraphicsOpacityEffect(pages_->currentWidget());
    pages_->currentWidget()->setGraphicsEffect(eff);
    auto* a = new QPropertyAnimation(eff, "opacity", eff);
    a->setDuration(180);
    a->setStartValue(0.0);
    a->setEndValue(1.0);
    connect(a, &QPropertyAnimation::finished, pages_->currentWidget(), [w = pages_->currentWidget()] { w->setGraphicsEffect(nullptr); });
    a->start();
}

void MainWindow::onState(AppState s) {
    badge_->set(s == AppState::Recording ? TaskbarBadge::Mode::Recording
                : s == AppState::RecordingPaused ? TaskbarBadge::Mode::Paused : TaskbarBadge::Mode::None);
    accentAnim_->stop();
    accentAnim_->setStartValue(accent_);
    accentAnim_->setEndValue(styleFor(s).accent);
    accentAnim_->start();
}

// Auto Clicker: pointer icon on the taskbar button (red slash while paused), accent colour like Playing / paused,
// and Recorder / Records are locked until it stops (recording or playing at the same time would fight over the mouse).
void MainWindow::onAutoClickState(AutoClickState s) {
    badge_->set(s == AutoClickState::Running ? TaskbarBadge::Mode::AutoClick
                : s == AutoClickState::Paused ? TaskbarBadge::Mode::AutoClickPaused : TaskbarBadge::Mode::None);
    const StateStyle running = styleFor(AppState::Playing), paused = styleFor(AppState::PlaybackPaused), idle = styleFor(AppState::Idle);
    accentAnim_->stop();
    accentAnim_->setStartValue(accent_);
    accentAnim_->setEndValue(s == AutoClickState::Running ? running.accent : s == AutoClickState::Paused ? paused.accent : idle.accent);
    accentAnim_->start();
    const bool active = s != AutoClickState::Idle;
    tabs_->setTabEnabled(1, !active);
    tabs_->setTabEnabled(2, !active);
    if (active && (tabs_->currentIndex() == 1 || tabs_->currentIndex() == 2)) tabs_->setCurrentIndex(0);
}

void MainWindow::startAutoClicker() {
    if (autoClicker_.isActive()) return;
    autoPage_->requestStart();
}

void MainWindow::askNameAndSave() {
    const QString suggested = "Record " + QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss");
    show();
    raise();
    for (;;) {
        bool ok = false;
        QString name = dialogs::getText(this, "Name your recording", "Record name:", suggested, &ok);
        if (!ok || name.trimmed().isEmpty()) name = suggested;  // never lose a recording
        if (c_.commitRecording(name)) return;
        if (!dialogs::confirm(this, "Save failed", "The recording could not be saved. Try again?", "Retry", "Discard", true)) {
            c_.discardRecording();
            return;
        }
    }
}

void MainWindow::startSelectedPlayback() {
    if (c_.state() != AppState::Idle) return;
    const qint64 id = records_->selectedId();
    if (id < 0) { showNotice("Select a record in the Records tab first."); return; }
    requestPlay(id);
}

void MainWindow::requestPlay(qint64 id) {
    QString err;
    const auto m = repo_.meta(id, &err);
    if (!m) { dialogs::message(this, "Play", err, dialogs::Kind::Warning); return; }
    if (!m->monitorGeometry.isEmpty() && m->monitorGeometry != QString::fromStdString(currentLayoutSignature())) {
        if (!dialogs::confirm(this, "Monitor layout changed",
                              "This record was made with a different monitor layout or scaling.\n"
                              "Clicks may land in the wrong place. Play anyway?"))
            return;
    }
    if (isMinimized()) showNormal();  // the dialog below must be visible (this is also reached from the hotkey)
    show();
    raise();
    activateWindow();
    const auto choice = dialogs::choosePlaybackMode(this);
    if (!choice.accepted) return;
    if (c_.state() != AppState::Idle) return;  // state changed while the dialog was open
    c_.playRecord(id, choice.intervalMs);
}

void MainWindow::updateExcludedWindow() {
    // The app's own window is never recorded; the shadow margin stays clickable/recordable.
    c_.setExcludedWindow(reinterpret_cast<void*>(winId()), int(kMargin * devicePixelRatioF()));
}

void MainWindow::showEvent(QShowEvent* e) {
    QWidget::showEvent(e);
    updateExcludedWindow();
}

void MainWindow::closeEvent(QCloseEvent* e) {
    if (c_.state() == AppState::Saving) { e->ignore(); return; }
    autoClicker_.stop();
    c_.shutdown();
    e->accept();
    QGuiApplication::quit();
}

void MainWindow::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QRectF body = QRectF(rect()).adjusted(kMargin, kMargin, -kMargin, -kMargin);
    for (int i = 0; i < kMargin; ++i) {  // soft shadow
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(0, 0, 0, 5 + i));
        p.drawRoundedRect(body.adjusted(-kMargin + i, -kMargin + i + 3, kMargin - i, kMargin - i + 3), kRadius + 6, kRadius + 6);
    }
    QLinearGradient bg(body.topLeft(), body.bottomRight());
    bg.setColorAt(0, QColor(30, 22, 27, 246));
    bg.setColorAt(0.55, QColor(15, 14, 18, 248));
    bg.setColorAt(1, QColor(22, 10, 14, 248));
    p.setBrush(bg);
    p.setPen(Qt::NoPen);
    p.drawRoundedRect(body, kRadius, kRadius);
    QLinearGradient sheen(body.topLeft(), QPointF(body.left(), body.top() + 120));  // glass highlight
    sheen.setColorAt(0, QColor(255, 255, 255, 26));
    sheen.setColorAt(1, QColor(255, 255, 255, 0));
    p.setBrush(sheen);
    p.drawRoundedRect(body, kRadius, kRadius);
    QColor edge = accent_;
    edge.setAlpha(120);
    p.setBrush(Qt::NoBrush);
    p.setPen(QPen(edge, 1.4));
    p.drawRoundedRect(body, kRadius, kRadius);
}

// Native window move: the OS keeps the grab point under the cursor, so the window never jumps.
void MainWindow::mousePressEvent(QMouseEvent* e) {
    if (e->button() == Qt::LeftButton && e->position().y() < kMargin + 46) {
        if (auto* w = windowHandle()) w->startSystemMove();
        e->accept();
        return;
    }
    QWidget::mousePressEvent(e);
}

}  // namespace mmr