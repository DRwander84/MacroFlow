#include "SettingsWidget.h"

#include <algorithm>

#include <QFormLayout>
#include <QHBoxLayout>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QStyle>
#include <QVBoxLayout>

#include "ThemedDialog.h"
#include "platform/windows/AutoStart.h"

namespace mmr {

SettingsWidget::SettingsWidget(SettingsManager& settings, ShortcutManager& shortcuts, MacroController& controller,
                               QWidget* parent)
    : QWidget(parent), settings_(settings), shortcuts_(shortcuts), c_(controller) {
    auto* shortcutsTitle = new QLabel("KEYBOARD SHORTCUTS");
    shortcutsTitle->setObjectName("sectionLabel");
    auto* grid = new QFormLayout;
    grid->setHorizontalSpacing(12);
    grid->setVerticalSpacing(8);
    grid->setLabelAlignment(Qt::AlignLeft);
    for (int i = 0; i < int(ShortcutAction::Count); ++i) {
        const auto a = ShortcutAction(i);
        auto* e = new ShortcutEdit;
        edits_[a] = e;
        auto* label = new QLabel(QString::fromLatin1(actionLabel(a)));
        label->setObjectName("rowLabel");
        grid->addRow(label, e);
        connect(e, &ShortcutEdit::captureStarted, &shortcuts_, &ShortcutManager::beginCapture);
        connect(e, &ShortcutEdit::captureFinished, &shortcuts_, &ShortcutManager::endCapture);
        connect(e, &ShortcutEdit::comboChanged, this, [this] { refreshButtons(); });
        connect(e, &ShortcutEdit::rejected, this, [this](const QString& why) { showStatus(why, true); });
    }
    saveBtn_ = new QPushButton("Save Changes");
    saveBtn_->setObjectName("primaryButton");
    resetBtn_ = new QPushButton("Reset to Defaults");
    resetBtn_->setObjectName("secondaryButton");
    for (auto* b : {saveBtn_, resetBtn_}) { b->setMinimumHeight(34); b->setCursor(Qt::PointingHandCursor); }
    status_ = new QLabel;
    status_->setObjectName("statusLine");
    status_->setWordWrap(true);
    status_->setMinimumHeight(34);
    auto* btnRow = new QHBoxLayout;
    btnRow->addWidget(saveBtn_, 1);
    btnRow->addWidget(resetBtn_, 1);

    auto* generalTitle = new QLabel("GENERAL");
    generalTitle->setObjectName("sectionLabel");
    generalTitle->setStyleSheet("padding-top: 0px;");  // sits right under the line above it
    autostart_ = new QCheckBox("Launch when I sign in to Windows");
    restoreTab_ = new QCheckBox("Open on the last used tab");
    confirmDel_ = new QCheckBox("Ask before deleting a record");
    overlay_ = new QCheckBox("Show click / drag / scroll markers on screen (accuracy test)");
    overlay_->setToolTip("While recording, marks every click, drag and scroll where it happens. "
                         "Playing a record shows all its markers first, then starts after the 3 s countdown.");

    auto* inputTitle = new QLabel("RECORDING INPUT");
    inputTitle->setObjectName("sectionLabel");
    recMouse_ = new QCheckBox("Record mouse (movement, clicks, drags, scrolling)");
    recKeyboard_ = new QCheckBox("Record keyboard (every key, shortcut, hold and pause)");
    recKeyboard_->setToolTip("Captures every key you press, including passwords typed while recording. "
                             "Keys are replayed with the exact timing, order and hold length.");
    inputHint_ = new QLabel;
    inputHint_->setObjectName("statusLine");
    inputHint_->setProperty("error", true);  // only ever shown as the red warning
    inputHint_->setFixedHeight(16);          // one constant line: showing/hiding the text never moves the page
    inputHint_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);  // never widens the page

    auto* inner = new QWidget;
    auto* lay = new QVBoxLayout(inner);
    lay->setContentsMargins(18, 8, 18, 10);
    lay->setSpacing(8);
    lay->addWidget(shortcutsTitle);
    lay->addLayout(grid);
    lay->addLayout(btnRow);
    lay->addWidget(status_);
    lay->addWidget(inputTitle);
    lay->addWidget(recMouse_);
    lay->addWidget(recKeyboard_);
    lay->addWidget(inputHint_);
    lay->addWidget(generalTitle);
    for (auto* c : {autostart_, restoreTab_, confirmDel_, overlay_}) lay->addWidget(c);
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

    loadEditors(shortcuts_.active().empty() ? settings_.shortcuts() : shortcuts_.active());
    const auto& g = settings_.general();
    autostart_->setChecked(g.startWithWindows);
    restoreTab_->setChecked(g.restoreLastTab);
    confirmDel_->setChecked(g.confirmDelete);
    overlay_->setChecked(g.showClickOverlay);
    recMouse_->setChecked(g.recordMouse);
    recKeyboard_->setChecked(g.recordKeyboard);
    refreshInputHint();

    connect(saveBtn_, &QPushButton::clicked, this, &SettingsWidget::save);
    connect(resetBtn_, &QPushButton::clicked, this, &SettingsWidget::reset);
    for (auto* c : {autostart_, restoreTab_, confirmDel_, overlay_, recMouse_, recKeyboard_})
        connect(c, &QCheckBox::toggled, this, &SettingsWidget::saveGeneral);
    connect(&shortcuts_, &ShortcutManager::bindingsChanged, this, [this] { loadEditors(shortcuts_.active()); });
    connect(&c_, &MacroController::stateChanged, this, [this](AppState s) {
        shortcuts_.setLocked(s != AppState::Idle);  // never change shortcuts mid-operation
        recMouse_->setEnabled(s == AppState::Idle);  // the input mode of a recording is fixed when it starts
        recKeyboard_->setEnabled(s == AppState::Idle);
        refreshButtons();
    });
    refreshButtons();
    recMouse_->setEnabled(c_.state() == AppState::Idle);
    recKeyboard_->setEnabled(c_.state() == AppState::Idle);
}

BindingMap SettingsWidget::staged() const {
    BindingMap m;
    for (const auto& [a, e] : edits_) m[a] = e->combo();
    return m;
}

void SettingsWidget::loadEditors(const BindingMap& b) {
    for (const auto& [a, e] : edits_) {
        auto it = b.find(a);
        e->setCombo(it == b.end() ? KeyCombo{} : it->second);
    }
    refreshButtons();
}

void SettingsWidget::refreshButtons() {
    const BindingMap s = staged();
    const bool changed = s != shortcuts_.active();
    const QString problem = shortcuts_.check(s);
    const bool idle = c_.state() == AppState::Idle;
    const auto clashing = conflictingActions(s);  // duplicated keys turn red until one is changed
    for (const auto& [a, e] : edits_) e->setConflict(std::find(clashing.begin(), clashing.end(), a) != clashing.end());
    saveBtn_->setEnabled(changed && problem.isEmpty() && idle);
    resetBtn_->setEnabled(idle);
    if (!idle) showStatus("Shortcuts are locked while recording or playing.", false);
    else if (!problem.isEmpty()) showStatus(problem, true);
    else if (changed) showStatus("Unsaved changes. Press Save Changes to activate them.", false);
    else if (status_->property("sticky").toBool()) {}
    else showStatus("Click a shortcut, then press the new key combination. Esc cancels.", false);
}

void SettingsWidget::showStatus(const QString& text, bool error) {
    status_->setText(text);
    status_->setProperty("error", error);
    status_->setProperty("sticky", false);
    status_->style()->unpolish(status_);
    status_->style()->polish(status_);
}

void SettingsWidget::save() {
    const ApplyResult r = shortcuts_.apply(staged());
    if (r.ok) {
        showStatus("Shortcuts saved and active.", false);
        status_->setProperty("sticky", true);
        saveBtn_->setEnabled(false);
    } else {
        showStatus(r.error, true);
    }
}

void SettingsWidget::reset() {
    if (!dialogs::confirm(this, "Reset shortcuts", "Restore all keyboard shortcuts to their original keys?\nYour current shortcuts will be replaced."))
        return;
    const ApplyResult r = shortcuts_.resetToDefaults();
    if (r.ok) {
        loadEditors(shortcuts_.active());
        showStatus("Shortcuts restored to defaults.", false);
    } else {
        showStatus(r.error, true);
    }
}

void SettingsWidget::saveGeneral() {
    GeneralSettings g;
    g.startWithWindows = autostart_->isChecked();
    g.restoreLastTab = restoreTab_->isChecked();
    g.confirmDelete = confirmDel_->isChecked();
    g.showClickOverlay = overlay_->isChecked();
    g.recordMouse = recMouse_->isChecked();
    g.recordKeyboard = recKeyboard_->isChecked();
    QString err;
    const bool autoChanged = g.startWithWindows != settings_.general().startWithWindows;
    if (autoChanged && !setAutoStart(g.startWithWindows, &err)) {
        showStatus(err, true);
        QSignalBlocker b(autostart_);
        autostart_->setChecked(settings_.general().startWithWindows);
        return;
    }
    if (!settings_.setGeneral(g, &err)) {
        showStatus(err, true);
        return;
    }
    refreshInputHint();
    emit generalChanged();
}

void SettingsWidget::refreshInputHint() {
    // Only the "nothing selected" warning exists; with at least one option on the text is simply empty.
    const bool none = !recMouse_->isChecked() && !recKeyboard_->isChecked();
    inputHint_->setText(none ? "Both are off: recording can't start. Turn on at least one." : QString());
}

}  // namespace mmr