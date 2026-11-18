#include "AutoClickerWidget.h"

#include <QFrame>
#include <QIcon>
#include <QHBoxLayout>
#include <QListView>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QTimer>
#include <QVBoxLayout>
#include <algorithm>
#include <limits>

#include "ThemedDialog.h"

namespace mmr {
namespace {

constexpr qint64 kUnitMs[4] = {1, 1000, 60'000, 3'600'000};
constexpr int kUnitMax[4] = {3'600'000, 86'400, 1'440, 168};  // <= 1 h / 24 h / 24 h / 7 days

QLineEdit* makeField(int width, int lo, int hi, const QString& placeholder = QString()) {
    auto* e = new QLineEdit;
    e->setValidator(new QIntValidator(lo, hi, e));
    e->setFixedWidth(width);
    e->setAlignment(Qt::AlignCenter);
    e->setPlaceholderText(placeholder);
    return e;
}

QComboBox* makeCombo(const QStringList& items, int width) {
    auto* c = new QComboBox;
    c->setView(new QListView(c));  // styled by the app stylesheet like everything else
    c->addItems(items);
    c->setFixedWidth(width);
    c->setCursor(Qt::PointingHandCursor);
    return c;
}

// Glass card with a section title, same look as the record cards.
QFrame* makeCard(const QString& title, QVBoxLayout** body) {
    auto* card = new QFrame;
    card->setObjectName("autoCard");
    auto* lay = new QVBoxLayout(card);
    lay->setContentsMargins(14, 8, 14, 10);
    lay->setSpacing(4);
    if (!title.isEmpty()) {
        auto* t = new QLabel(title);
        t->setObjectName("sectionLabel");
        t->setStyleSheet("padding-top: 0px;");
        lay->addWidget(t);
    }
    *body = lay;
    return card;
}

QString keyText(const BindingMap& b, ShortcutAction a) {
    auto it = b.find(a);
    return it == b.end() || it->second.isNone() ? QString("-") : QString::fromStdString(toString(it->second));
}

}  // namespace

AutoClickerWidget::AutoClickerWidget(AutoClicker& clicker, MacroController& controller, SettingsManager& settings,
                                     ShortcutManager& shortcuts, QWidget* parent)
    : QWidget(parent), ac_(clicker), c_(controller), settings_(settings), shortcuts_(shortcuts),
      picker_(std::make_unique<ClickPointPicker>()) {
    // ---- Start (centre) + status + pause / stop -----------------------------------------------------------------
    start_ = new QPushButton("Start");
    start_->setObjectName("autoStartButton");
    start_->setIcon(QIcon(":/icons/play.svg"));
    start_->setIconSize(QSize(16, 16));
    start_->setFixedSize(104, 104);
    start_->setCursor(Qt::PointingHandCursor);
    status_ = new QLabel("Ready");
    status_->setObjectName("autoStatus");
    status_->setAlignment(Qt::AlignCenter);
    pause_ = new QPushButton(" Pause");
    pause_->setObjectName("secondaryButton");
    pause_->setIcon(QIcon(":/icons/pause.svg"));
    stop_ = new QPushButton(" Stop");
    stop_->setObjectName("secondaryButton");
    stop_->setIcon(QIcon(":/icons/stop.svg"));
    for (auto* b : {pause_, stop_}) {
        b->setMinimumHeight(32);
        b->setFixedWidth(116);
        b->setIconSize(QSize(14, 14));
        b->setCursor(Qt::PointingHandCursor);
    }
    auto* heroButtons = new QHBoxLayout;
    heroButtons->setSpacing(10);
    heroButtons->addStretch(1);
    heroButtons->addWidget(pause_);
    heroButtons->addWidget(stop_);
    heroButtons->addStretch(1);

    // ---- Number of clicks ---------------------------------------------------------------------------------------
    QVBoxLayout* clicksBody = nullptr;
    QFrame* clicksCard = makeCard("NUMBER OF CLICKS", &clicksBody);
    cbInfinite_ = new QCheckBox("Infinite");
    cbCount_ = new QCheckBox("This many clicks");
    clickGroup_ = new QButtonGroup(this);  // exclusive: ticking one unticks the other
    clickGroup_->setExclusive(true);
    clickGroup_->addButton(cbInfinite_, 0);
    clickGroup_->addButton(cbCount_, 1);
    countEdit_ = makeField(110, 1, AutoClickSettings::kMaxClicks, "clicks");
    auto* r1 = new QHBoxLayout;
    r1->addWidget(cbInfinite_);
    r1->addStretch(1);
    auto* r2 = new QHBoxLayout;
    r2->addWidget(cbCount_);
    r2->addStretch(1);
    r2->addWidget(countEdit_);
    clicksBody->addLayout(r1);
    clicksBody->addLayout(r2);

    // ---- Click locations ----------------------------------------------------------------------------------------
    QVBoxLayout* locBody = nullptr;
    QFrame* locCard = makeCard("CLICK LOCATIONS", &locBody);
    auto* locLabel = new QLabel("How many places to click");
    locLabel->setObjectName("rowLabel");
    locMinus_ = new QPushButton("−");
    locPlus_ = new QPushButton("+");
    for (auto* b : {locMinus_, locPlus_}) {
        b->setObjectName("iconButton");
        b->setFixedSize(28, 28);
        b->setCursor(Qt::PointingHandCursor);
        b->setFocusPolicy(Qt::NoFocus);
    }
    locEdit_ = makeField(52, 1, AutoClickSettings::kMaxLocations);
    placeBtn_ = new QPushButton("Show circles on screen");
    placeBtn_->setObjectName("secondaryButton");
    placeBtn_->setMinimumHeight(30);
    placeBtn_->setCursor(Qt::PointingHandCursor);
    placeHint_ = new QLabel;
    placeHint_->setObjectName("statusLine");
    placeHint_->setFixedHeight(16);  // constant height: showing the text never moves the page
    placeHint_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    auto* r3 = new QHBoxLayout;
    r3->addWidget(locLabel);
    r3->addStretch(1);
    r3->addWidget(locMinus_);
    r3->addWidget(locEdit_);
    r3->addWidget(locPlus_);
    locBody->addLayout(r3);
    locBody->addWidget(placeBtn_);
    locBody->addWidget(placeHint_);

    // ---- Time between clicks ------------------------------------------------------------------------------------
    QVBoxLayout* timeBody = nullptr;
    QFrame* timeCard = makeCard("TIME BETWEEN CLICKS", &timeBody);
    cbNoDelay_ = new QCheckBox("No delay (as fast as possible)");
    cbEvery_ = new QCheckBox("Once every");
    intervalGroup_ = new QButtonGroup(this);
    intervalGroup_->setExclusive(true);
    intervalGroup_->addButton(cbNoDelay_, 0);
    intervalGroup_->addButton(cbEvery_, 1);
    everyEdit_ = makeField(80, 1, kUnitMax[1]);
    everyValidator_ = new QIntValidator(1, kUnitMax[1], everyEdit_);  // its range follows the chosen unit
    everyEdit_->setValidator(everyValidator_);
    unit_ = makeCombo({"Milliseconds", "Seconds", "Minutes", "Hours"}, 120);
    auto* r4 = new QHBoxLayout;
    r4->addWidget(cbNoDelay_);
    r4->addStretch(1);
    auto* r5 = new QHBoxLayout;
    r5->addWidget(cbEvery_);
    r5->addStretch(1);
    r5->addWidget(everyEdit_);
    r5->addWidget(unit_);
    timeBody->addLayout(r4);
    timeBody->addLayout(r5);

    // ---- Advanced -----------------------------------------------------------------------------------------------
    advToggle_ = new QPushButton("MORE OPTIONS  ▾");
    advToggle_->setObjectName("linkButton");
    advToggle_->setCursor(Qt::PointingHandCursor);
    advToggle_->setFocusPolicy(Qt::NoFocus);
    QVBoxLayout* advLay = nullptr;
    advBody_ = makeCard(QString(), &advLay);
    advBody_->setVisible(false);
    button_ = makeCombo({"Left button", "Right button", "Middle button"}, 140);
    clickType_ = makeCombo({"Single click", "Double click"}, 140);
    gapEdit_ = makeField(80, 0, 60'000, "ms");
    jitterEdit_ = makeField(80, 0, 50, "%");
    cbRestore_ = new QCheckBox("Put the mouse back where it was when finished");
    cbMinimize_ = new QCheckBox("Minimize MacroFlow when it starts");
    auto addRow = [&](const QString& text, QWidget* w) {
        auto* row = new QHBoxLayout;
        auto* l = new QLabel(text);
        l->setObjectName("rowLabel");
        row->addWidget(l);
        row->addStretch(1);
        row->addWidget(w);
        advLay->addLayout(row);
    };
    addRow("Mouse button", button_);
    addRow("Click type", clickType_);
    addRow("Pause between locations (ms)", gapEdit_);
    addRow("Random timing variation (±%)", jitterEdit_);
    advLay->addWidget(cbRestore_);
    advLay->addWidget(cbMinimize_);

    hint_ = new QLabel;
    hint_->setObjectName("hintLabel");
    hint_->setAlignment(Qt::AlignCenter);
    hint_->setWordWrap(true);

    // ---- Page ---------------------------------------------------------------------------------------------------
    content_ = new QWidget;  // everything the user can edit: disabled while the Auto Clicker runs
    auto* contentLay = new QVBoxLayout(content_);
    contentLay->setContentsMargins(0, 0, 0, 0);
    contentLay->setSpacing(8);
    contentLay->addWidget(clicksCard);
    contentLay->addWidget(locCard);
    contentLay->addWidget(timeCard);
    contentLay->addWidget(advToggle_);
    contentLay->addWidget(advBody_);

    auto* inner = new QWidget;
    auto* lay = new QVBoxLayout(inner);
    lay->setContentsMargins(20, 6, 20, 10);
    lay->setSpacing(6);
    lay->addWidget(start_, 0, Qt::AlignHCenter);
    lay->addWidget(status_);
    lay->addLayout(heroButtons);
    lay->addSpacing(6);
    lay->addWidget(content_);
    lay->addWidget(hint_);
    lay->addStretch(1);

    auto* scroll = new QScrollArea;
    scroll->setObjectName("settingsScroll");
    scroll->setWidget(inner);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->addWidget(scroll);

    // ---- Values + wiring ----------------------------------------------------------------------------------------
    loadFromSettings();

    connect(start_, &QPushButton::clicked, this, [this] { requestStart(); });
    connect(pause_, &QPushButton::clicked, &ac_, &AutoClicker::pauseResume);
    connect(stop_, &QPushButton::clicked, &ac_, &AutoClicker::stop);

    connect(clickGroup_, &QButtonGroup::idToggled, this, [this](int, bool on) { if (on && !loading_) { refreshEnabled(); commit(); } });
    connect(intervalGroup_, &QButtonGroup::idToggled, this, [this](int, bool on) { if (on && !loading_) { refreshEnabled(); commit(); } });
    for (auto* e : {countEdit_, everyEdit_, gapEdit_, jitterEdit_})
        connect(e, &QLineEdit::editingFinished, this, [this] { commit(); });
    connect(locEdit_, &QLineEdit::editingFinished, this, [this] { setLocationCount(readInt(locEdit_, picker_->count(), 1, AutoClickSettings::kMaxLocations)); });
    connect(locMinus_, &QPushButton::clicked, this, [this] { setLocationCount(picker_->count() - 1); });
    connect(locPlus_, &QPushButton::clicked, this, [this] { setLocationCount(picker_->count() + 1); });
    connect(placeBtn_, &QPushButton::clicked, this, [this] { showCircles(!picker_->isShown()); });
    connect(unit_, &QComboBox::currentIndexChanged, this, [this](int u) {
        if (loading_) return;
        everyValidator_->setRange(1, kUnitMax[std::clamp(u, 0, 3)]);
        if (readInt(everyEdit_, 1, 1, std::numeric_limits<int>::max()) > kUnitMax[std::clamp(u, 0, 3)])
            everyEdit_->setText(QString::number(kUnitMax[std::clamp(u, 0, 3)]));
        commit();
    });
    for (auto* c : {button_, clickType_}) connect(c, &QComboBox::currentIndexChanged, this, [this] { if (!loading_) commit(); });
    for (auto* c : {cbRestore_, cbMinimize_}) connect(c, &QCheckBox::toggled, this, [this] { if (!loading_) commit(); });
    connect(picker_.get(), &ClickPointPicker::pointsChanged, this, [this] { commit(); });
    connect(advToggle_, &QPushButton::clicked, this, [this] {
        const bool open = !advBody_->isVisible();
        advBody_->setVisible(open);
        advToggle_->setText(open ? "MORE OPTIONS  ▴" : "MORE OPTIONS  ▾");
    });

    connect(&ac_, &AutoClicker::stateChanged, this, &AutoClickerWidget::applyState);
    connect(&ac_, &AutoClicker::progress, this, [this](quint64 n) { clicks_ = n; refreshStatus(); });
    connect(&ac_, &AutoClicker::finished, this, [this](quint64 n, bool completed) {
        lastResult_ = QString("%1  ·  %2 click%3").arg(completed ? QStringLiteral("Finished") : QStringLiteral("Stopped"))
                          .arg(n).arg(n == 1 ? QStringLiteral("") : QStringLiteral("s"));
        refreshStatus();
    });
    connect(&ac_, &AutoClicker::errorOccurred, this, [this](const QString& m) {
        dialogs::message(window(), "Auto Clicker stopped", m, dialogs::Kind::Warning);
    });
    connect(&shortcuts_, &ShortcutManager::bindingsChanged, this, &AutoClickerWidget::refreshHints);

    refreshEnabled();
    refreshHints();
    applyState(AutoClickState::Idle);
}

AutoClickerWidget::~AutoClickerWidget() = default;

int AutoClickerWidget::readInt(const QLineEdit* e, int fallback, int lo, int hi) {
    bool ok = false;
    const int v = e->text().trimmed().toInt(&ok);
    return ok ? std::clamp(v, lo, hi) : fallback;
}

AutoClickSettings AutoClickerWidget::stage() const {
    AutoClickSettings s = settings_.autoClick();  // anything not shown on the page keeps its stored value
    s.infinite = cbInfinite_->isChecked();
    s.clickCount = readInt(countEdit_, s.clickCount, 1, AutoClickSettings::kMaxClicks);
    s.locationCount = picker_->count();
    s.points = picker_->points();
    s.noDelay = cbNoDelay_->isChecked();
    s.intervalUnit = std::clamp(unit_->currentIndex(), 0, 3);
    s.intervalValue = readInt(everyEdit_, s.intervalValue, 1, kUnitMax[s.intervalUnit]);
    s.button = std::clamp(button_->currentIndex(), 0, 2);
    s.doubleClick = clickType_->currentIndex() == 1;
    s.locationGapMs = readInt(gapEdit_, s.locationGapMs, 0, 60'000);
    s.jitterPercent = readInt(jitterEdit_, s.jitterPercent, 0, 50);
    s.restoreCursor = cbRestore_->isChecked();
    s.minimizeOnStart = cbMinimize_->isChecked();
    return s;
}

void AutoClickerWidget::loadFromSettings() {
    loading_ = true;
    const AutoClickSettings& s = settings_.autoClick();
    cbInfinite_->setChecked(s.infinite);
    cbCount_->setChecked(!s.infinite);
    countEdit_->setText(QString::number(s.clickCount));
    picker_->setPoints(s.points);
    picker_->setCount(s.locationCount);
    locEdit_->setText(QString::number(picker_->count()));
    cbNoDelay_->setChecked(s.noDelay);
    cbEvery_->setChecked(!s.noDelay);
    unit_->setCurrentIndex(s.intervalUnit);
    everyValidator_->setRange(1, kUnitMax[s.intervalUnit]);
    everyEdit_->setText(QString::number(s.intervalValue));
    button_->setCurrentIndex(s.button);
    clickType_->setCurrentIndex(s.doubleClick ? 1 : 0);
    gapEdit_->setText(QString::number(s.locationGapMs));
    jitterEdit_->setText(QString::number(s.jitterPercent));
    cbRestore_->setChecked(s.restoreCursor);
    cbMinimize_->setChecked(s.minimizeOnStart);
    loading_ = false;
}

void AutoClickerWidget::commit() {
    if (loading_) return;
    QString err;
    if (!settings_.setAutoClick(stage(), &err)) emit notice(err);
    // Show the clamped values the engine will really use (an empty or out-of-range field is corrected here).
    const QSignalBlocker b1(countEdit_), b2(everyEdit_), b3(gapEdit_), b4(jitterEdit_);
    const AutoClickSettings& s = settings_.autoClick();
    countEdit_->setText(QString::number(s.clickCount));
    everyEdit_->setText(QString::number(s.intervalValue));
    gapEdit_->setText(QString::number(s.locationGapMs));
    jitterEdit_->setText(QString::number(s.jitterPercent));
    refreshEnabled();
}

void AutoClickerWidget::refreshEnabled() {
    countEdit_->setEnabled(cbCount_->isChecked());
    everyEdit_->setEnabled(cbEvery_->isChecked());
    unit_->setEnabled(cbEvery_->isChecked());
    jitterEdit_->setEnabled(cbEvery_->isChecked());  // "no delay" has no timing to vary
    gapEdit_->setEnabled(picker_->count() > 1);
    locMinus_->setEnabled(picker_->count() > 1);
    locPlus_->setEnabled(picker_->count() < AutoClickSettings::kMaxLocations);
}

void AutoClickerWidget::setLocationCount(int n) {
    n = std::clamp(n, 1, AutoClickSettings::kMaxLocations);
    const bool changed = n != picker_->count();
    picker_->setCount(n);
    locEdit_->setText(QString::number(n));
    if (changed) showCircles(true);  // choosing the number hands out that many circles to place
    commit();
}

void AutoClickerWidget::showCircles(bool on) {
    if (on) picker_->show();
    else picker_->hide();
    placeBtn_->setText(on ? "Done placing" : "Show circles on screen");
    placeHint_->setText(on ? QString("Drag the numbered circles onto the spots you want clicked.") : QString());
}

void AutoClickerWidget::hideEvent(QHideEvent* e) {
    if (picker_ && picker_->isShown()) showCircles(false);  // leaving the tab: nothing stays on the screen
    QWidget::hideEvent(e);
}

AutoClickPlan AutoClickerWidget::makePlan(const AutoClickSettings& s) const {
    AutoClickPlan p;
    for (int i = 0; i < s.locationCount && size_t(i) < s.points.size(); ++i) p.points.emplace_back(s.points[size_t(i)].x(), s.points[size_t(i)].y());
    p.button = s.button;
    p.doubleClick = s.doubleClick;
    p.infinite = s.infinite;
    p.totalClicks = uint64_t(s.clickCount);
    p.noDelay = s.noDelay;
    p.intervalUs = int64_t(s.intervalValue) * kUnitMs[s.intervalUnit] * 1000;
    p.jitterPercent = s.jitterPercent;
    p.locationGapUs = int64_t(s.locationGapMs) * 1000;
    p.restoreCursor = s.restoreCursor;
    return p;
}

bool AutoClickerWidget::requestStart() {
    if (ac_.isActive() || starting_) return false;
    if (c_.state() != AppState::Idle) {
        emit notice("Stop recording or playback before starting the Auto Clicker.");
        return false;
    }
    // Whatever is typed right now counts, even if the field was never left with Enter / Tab.
    if (!loading_) {
        QString err;
        if (!settings_.setAutoClick(stage(), &err)) { emit notice(err); return false; }
    }
    const AutoClickSettings s = settings_.autoClick();
    if (s.points.size() < size_t(s.locationCount)) {
        emit notice("Place the click circles first.");
        return false;
    }
    showCircles(false);  // the circles must never sit in the way of the clicks
    total_ = s.infinite ? 0 : uint64_t(s.clickCount);
    lastResult_.clear();
    const AutoClickPlan plan = makePlan(s);

    QWidget* w = window();
    if (s.minimizeOnStart && w && w->isVisible() && !w->isMinimized()) {
        starting_ = true;  // get out of the way first: a click point under the app window would hit the app itself
        w->showMinimized();
        QTimer::singleShot(180, this, [this, plan] {
            starting_ = false;
            if (!ac_.isActive()) ac_.start(plan);
        });
    } else {
        ac_.start(plan);
    }
    return true;
}

void AutoClickerWidget::applyState(AutoClickState s) {
    const bool idle = s == AutoClickState::Idle;
    start_->setEnabled(idle);
    pause_->setEnabled(!idle);
    stop_->setEnabled(!idle);
    content_->setEnabled(idle);  // settings are locked while it works
    const bool paused = s == AutoClickState::Paused;
    pause_->setText(paused ? " Resume" : " Pause");
    pause_->setIcon(QIcon(paused ? ":/icons/play.svg" : ":/icons/pause.svg"));
    if (!idle) clicks_ = ac_.clicks();
    refreshStatus();
}

void AutoClickerWidget::refreshStatus() {
    const AutoClickState s = ac_.state();
    if (s == AutoClickState::Idle) {
        status_->setText(lastResult_.isEmpty() ? QString("Ready") : lastResult_);
        return;
    }
    const QString count = total_ > 0 ? QString("%1 / %2").arg(clicks_).arg(total_) : QString::number(clicks_);
    const QString verb = s == AutoClickState::Paused ? QStringLiteral("Paused") : QStringLiteral("Clicking");
    const QString plural = (total_ == 0 && clicks_ == 1) ? QStringLiteral("") : QStringLiteral("s");
    status_->setText(QString("%1  ·  %2 click%3").arg(verb, count, plural));
}

void AutoClickerWidget::refreshHints() {
    const auto& b = shortcuts_.active();
    hint_->setText(QString("Start / Stop %1   ·   Pause / Resume %2")
                       .arg(keyText(b, ShortcutAction::AutoClickStartStop), keyText(b, ShortcutAction::AutoClickPauseResume)));
    start_->setToolTip("Start (" + keyText(b, ShortcutAction::AutoClickStartStop) + ")");
    pause_->setToolTip("Pause / Resume (" + keyText(b, ShortcutAction::AutoClickPauseResume) + ")");
}

}  // namespace mmr
