#include "ShortcutEdit.h"

#include <QKeyEvent>
#include <QStyle>

namespace mmr {

ShortcutEdit::ShortcutEdit(QWidget* parent) : QPushButton(parent) {
    setObjectName("shortcutEdit");
    setFocusPolicy(Qt::StrongFocus);
    setCursor(Qt::PointingHandCursor);
    setMinimumWidth(150);
    refreshText();
    connect(this, &QPushButton::clicked, this, [this] { if (!capturing_) beginCapture(); });
}

void ShortcutEdit::setCombo(const KeyCombo& c) {
    combo_ = c;
    refreshText();
}

void ShortcutEdit::setConflict(bool on) {
    if (on == conflict_) return;
    conflict_ = on;
    setProperty("conflict", on);
    style()->unpolish(this);
    style()->polish(this);
}

void ShortcutEdit::refreshText() {
    if (capturing_) {
        QString t;
        if (pendingMods_ & kModCtrl) t += "Ctrl+";
        if (pendingMods_ & kModAlt) t += "Alt+";
        if (pendingMods_ & kModShift) t += "Shift+";
        setText(t.isEmpty() ? "Press a key..." : t + "...");
    } else {
        setText(combo_.isNone() ? "Not set" : QString::fromStdString(toString(combo_)));
    }
    setProperty("capturing", capturing_);
    style()->unpolish(this);
    style()->polish(this);
}

void ShortcutEdit::beginCapture() {
    capturing_ = true;
    pendingMods_ = 0;
    emit captureStarted();  // owner suspends OS hotkeys so the pressed key reaches us
    setFocus();
    grabKeyboard();
    refreshText();
}

void ShortcutEdit::endCapture() {
    if (!capturing_) return;
    capturing_ = false;
    releaseKeyboard();
    refreshText();
    emit captureFinished();
}

void ShortcutEdit::mousePressEvent(QMouseEvent* e) {
    if (capturing_) { endCapture(); return; }  // clicking again cancels
    QPushButton::mousePressEvent(e);
}

void ShortcutEdit::focusOutEvent(QFocusEvent* e) {
    endCapture();
    QPushButton::focusOutEvent(e);
}

void ShortcutEdit::keyReleaseEvent(QKeyEvent* e) {
    if (capturing_) {
        pendingMods_ = 0;
        const auto m = e->modifiers();
        if (m & Qt::ControlModifier) pendingMods_ |= kModCtrl;
        if (m & Qt::AltModifier) pendingMods_ |= kModAlt;
        if (m & Qt::ShiftModifier) pendingMods_ |= kModShift;
        refreshText();
        e->accept();
        return;
    }
    QPushButton::keyReleaseEvent(e);
}

void ShortcutEdit::keyPressEvent(QKeyEvent* e) {
    if (!capturing_) {
        if (e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter || e->key() == Qt::Key_Space) beginCapture();
        else QPushButton::keyPressEvent(e);
        return;
    }
    e->accept();
    if (e->isAutoRepeat()) return;
    if (e->key() == Qt::Key_Escape) { endCapture(); return; }

    uint32_t mods = 0;
    const auto m = e->modifiers();
    if (m & Qt::ControlModifier) mods |= kModCtrl;
    if (m & Qt::AltModifier) mods |= kModAlt;
    if (m & Qt::ShiftModifier) mods |= kModShift;

    const uint32_t vk = e->nativeVirtualKey();  // real Windows VK_* code (independent of keyboard layout)
    if (isModifierVk(vk) || e->key() == Qt::Key_Control || e->key() == Qt::Key_Shift ||
        e->key() == Qt::Key_Alt || e->key() == Qt::Key_Meta) {
        pendingMods_ = mods;  // only a modifier so far: keep waiting
        refreshText();
        return;
    }
    const KeyCombo candidate{mods, vk};
    const std::string why = validateCombo(candidate);
    if (!why.empty() || candidate.isNone()) {
        emit rejected(QString::fromStdString(why.empty() ? "That key cannot be used." : why));
        pendingMods_ = 0;
        refreshText();
        return;  // stay in capture mode so the user can try another key
    }
    combo_ = candidate;
    endCapture();
    emit comboChanged();
}

}  // namespace mmr
