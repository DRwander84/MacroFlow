#include "StatusIndicator.h"

#include <QPainter>
#include <QRadialGradient>

namespace mmr {

StatusIndicator::StatusIndicator(QWidget* parent) : QWidget(parent), color_(120, 124, 140) {
    setFixedSize(120, 120);
    pulse_.setStartValue(0.0);
    pulse_.setEndValue(1.0);
    pulse_.setDuration(1300);
    pulse_.setLoopCount(-1);
    pulse_.setEasingCurve(QEasingCurve::InOutSine);
    connect(&pulse_, &QVariantAnimation::valueChanged, this, [this](const QVariant& v) {
        phase_ = v.toReal();
        update();
    });
}

void StatusIndicator::setState(const QColor& color, bool pulse) {
    color_ = color;
    if (pulse && pulse_.state() != QAbstractAnimation::Running) pulse_.start();
    if (!pulse) { pulse_.stop(); phase_ = 0; }
    update();
}

void StatusIndicator::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QPointF c(width() / 2.0, height() / 2.0);
    const qreal glowR = 38 + 18 * phase_;
    QRadialGradient g(c, glowR);
    QColor a = color_, b = color_;
    a.setAlpha(int(110 + 60 * phase_));
    b.setAlpha(0);
    g.setColorAt(0.35, a);
    g.setColorAt(1.0, b);
    p.setPen(Qt::NoPen);
    p.setBrush(g);
    p.drawEllipse(c, glowR, glowR);
    p.setBrush(QColor(12, 12, 16));
    p.drawEllipse(c, 24, 24);
    p.setBrush(color_);
    p.drawEllipse(c, 17 - 1.5 * phase_, 17 - 1.5 * phase_);
    QColor ring = color_;
    ring.setAlpha(150);
    p.setBrush(Qt::NoBrush);
    p.setPen(QPen(ring, 2));
    p.drawEllipse(c, 26, 26);
}

}  // namespace mmr
