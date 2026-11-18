#pragma once
#include <QColor>
#include <QVariantAnimation>
#include <QWidget>

namespace mmr {

// Round status light. Animates (soft pulse) only while recording/playing, so Idle costs no CPU.
class StatusIndicator : public QWidget {
    Q_OBJECT
public:
    explicit StatusIndicator(QWidget* parent = nullptr);
    void setState(const QColor& color, bool pulse);

protected:
    void paintEvent(QPaintEvent*) override;

private:
    QColor color_;
    qreal phase_ = 0;
    QVariantAnimation pulse_;
};

}  // namespace mmr
