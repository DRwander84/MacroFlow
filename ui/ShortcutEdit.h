#pragma once
#include <QPushButton>

#include "core/ShortcutTypes.h"

namespace mmr {

// Click, then press the desired key/combination. Reads the real Windows virtual-key code.
class ShortcutEdit : public QPushButton {
    Q_OBJECT
public:
    explicit ShortcutEdit(QWidget* parent = nullptr);
    KeyCombo combo() const { return combo_; }
    void setCombo(const KeyCombo& c);
    bool isCapturing() const { return capturing_; }
    void setConflict(bool on);  // red outline while this key is used twice

signals:
    void captureStarted();
    void captureFinished();
    void comboChanged();
    void rejected(const QString& reason);  // key pressed was not acceptable

protected:
    void keyPressEvent(QKeyEvent* e) override;
    void keyReleaseEvent(QKeyEvent* e) override;
    void focusOutEvent(QFocusEvent* e) override;
    bool focusNextPrevChild(bool) override { return false; }  // let Tab be captured
    void mousePressEvent(QMouseEvent* e) override;

private:
    void beginCapture();
    void endCapture();
    void refreshText();
    KeyCombo combo_;
    uint32_t pendingMods_ = 0;
    bool capturing_ = false;
    bool conflict_ = false;
};

}  // namespace mmr
