#include "ClickOverlay.h"

#include <QGuiApplication>
#include <QPaintEvent>
#include <QPainter>
#include <QPainterPath>
#include <QScreen>
#include <QWidget>
#include <algorithm>
#include <cmath>

namespace mmr {
namespace {

QColor colorFor(const Marker& m) {
    if (m.kind == MarkerKind::Scroll) return QColor(255, 176, 46);
    switch (m.button) {
        case MouseButton::Left: return QColor(255, 45, 70);
        case MouseButton::Right: return QColor(60, 170, 255);
        case MouseButton::Middle: return QColor(70, 224, 160);
        case MouseButton::X1: return QColor(190, 120, 255);
        default: return QColor(255, 120, 200);
    }
}

// Native-pixel bounds (generous) used for dirty-rect updates.
QRect boundsOf(const Marker& m) {
    int l = m.x, t = m.y, r = m.x, b = m.y;
    for (const auto& p : m.path) { l = std::min(l, p.x); t = std::min(t, p.y); r = std::max(r, p.x); b = std::max(b, p.y); }
    l = std::min(l, m.endX); t = std::min(t, m.endY); r = std::max(r, m.endX); b = std::max(b, m.endY);
    return QRect(l - 60, t - 60, (r - l) + 120, (b - t) + 120);
}

}  // namespace

class OverlayWidget : public QWidget {
public:
    OverlayWidget(QScreen* screen, const MarkerBuilder& builder)
        : QWidget(nullptr, Qt::FramelessWindowHint | Qt::Tool | Qt::WindowStaysOnTopHint | Qt::WindowTransparentForInput |
                               Qt::WindowDoesNotAcceptFocus | Qt::NoDropShadowWindowHint),
          builder_(builder) {
        setAttribute(Qt::WA_TranslucentBackground);
        setAttribute(Qt::WA_ShowWithoutActivating);
        setAttribute(Qt::WA_TransparentForMouseEvents);
        setScreen(screen);
        // Qt: a screen's geometry().topLeft() is its native pixel origin; its size is native size / dpr.
        origin_ = screen->geometry().topLeft();
        dpr_ = screen->devicePixelRatio();
        setGeometry(screen->geometry());
    }

    // Native rect of this screen (physical pixels, virtual-desktop coordinates).
    QRect nativeRect() const { return QRect(origin_, QSize(int(std::lround(width() * dpr_)), int(std::lround(height() * dpr_)))); }

    void updateNative(const QRect& nativeBounds) {
        const QRect n = nativeBounds.intersected(nativeRect());
        if (n.isEmpty()) return;
        const QPoint tl(int((n.left() - origin_.x()) / dpr_), int((n.top() - origin_.y()) / dpr_));
        update(QRect(tl, QSize(int(n.width() / dpr_) + 2, int(n.height() / dpr_) + 2)));
    }

protected:
    void paintEvent(QPaintEvent* ev) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        p.setClipRect(ev->rect());
        p.scale(1.0 / dpr_, 1.0 / dpr_);  // from here on 1 unit == 1 physical pixel
        p.translate(-origin_.x() * 1.0, -origin_.y() * 1.0);  // virtual-desktop coordinates directly
        QFont f = font();
        f.setPixelSize(12);
        f.setBold(true);
        p.setFont(f);
        const QRect view = nativeRect().adjusted(-80, -80, 80, 80);
        for (const Marker& m : builder_.markers()) {
            if (!view.intersects(boundsOf(m))) continue;
            drawMarker(p, m);
        }
    }

private:
    static void label(QPainter& p, const QPointF& at, const QString& text, const QColor& c) {
        const QFontMetrics fm(p.font());
        const QRectF r(at.x(), at.y(), fm.horizontalAdvance(text) + 10, 17);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(10, 10, 14, 215));
        p.drawRoundedRect(r, 8, 8);
        p.setPen(c);
        p.drawText(r, Qt::AlignCenter, text);
    }

    static void drawMarker(QPainter& p, const Marker& m) {
        const QColor c = colorFor(m);
        QColor fill = c, ring = c;
        fill.setAlpha(55);
        const QPointF s(m.x, m.y);
        if (m.kind == MarkerKind::Click) {
            p.setPen(QPen(ring, 2.5));
            p.setBrush(fill);
            p.drawEllipse(s, 15, 15);
            p.setPen(Qt::NoPen);
            p.setBrush(c);
            p.drawEllipse(s, 3.2, 3.2);
            label(p, s + QPointF(14, -30), QString::number(m.index), c);
        } else if (m.kind == MarkerKind::Drag) {
            QPainterPath path;
            path.moveTo(s);
            for (size_t i = 1; i < m.path.size(); ++i) path.lineTo(m.path[i].x, m.path[i].y);
            QColor line = c;
            line.setAlpha(190);
            p.setPen(QPen(line, 3, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            p.setBrush(Qt::NoBrush);
            p.drawPath(path);
            p.setPen(QPen(ring, 2.5));
            p.setBrush(fill);
            p.drawEllipse(s, 13, 13);                       // press point
            p.drawEllipse(QPointF(m.endX, m.endY), m.active ? 9 : 15, m.active ? 9 : 15);  // release point
            p.setPen(Qt::NoPen);
            p.setBrush(c);
            p.drawEllipse(s, 3.2, 3.2);
            p.drawEllipse(QPointF(m.endX, m.endY), 3.2, 3.2);
            label(p, s + QPointF(14, -30), QString("%1 drag").arg(m.index), c);
        } else {
            const int notches = std::max(1, int(std::lround(std::abs(m.delta) / 120.0)));
            const double dir = m.delta > 0 ? 1.0 : -1.0;       // wheel up / wheel right = positive
            QPainterPath arrow;
            if (!m.horizontal) {
                const double d = -dir;                          // screen y grows downward
                arrow.moveTo(s.x(), s.y() + d * 22);
                arrow.lineTo(s.x() - 13, s.y() + d * 2);
                arrow.lineTo(s.x() + 13, s.y() + d * 2);
            } else {
                arrow.moveTo(s.x() + dir * 22, s.y());
                arrow.lineTo(s.x() + dir * 2, s.y() - 13);
                arrow.lineTo(s.x() + dir * 2, s.y() + 13);
            }
            arrow.closeSubpath();
            p.setPen(QPen(ring, 2));
            p.setBrush(fill.lighter(130));
            p.drawPath(arrow);
            p.setPen(Qt::NoPen);
            p.setBrush(c);
            p.drawEllipse(s, 3.2, 3.2);
            label(p, s + QPointF(16, -30), QString("%1 scroll x%2").arg(m.index).arg(notches), c);
        }
    }

    const MarkerBuilder& builder_;
    QPoint origin_;
    qreal dpr_ = 1.0;
};

ClickOverlay::ClickOverlay(QObject* parent) : QObject(parent) {
    auto rebuild = [this] { if (visible_) { destroyWidgets(); createWidgets(); } };
    connect(qGuiApp, &QGuiApplication::screenAdded, this, rebuild);
    connect(qGuiApp, &QGuiApplication::screenRemoved, this, rebuild);
}

ClickOverlay::~ClickOverlay() { destroyWidgets(); }

void ClickOverlay::createWidgets() {
    for (QScreen* s : QGuiApplication::screens()) {
        auto* w = new OverlayWidget(s, builder_);
        widgets_.push_back(w);
        w->show();
    }
}

void ClickOverlay::destroyWidgets() {
    for (auto* w : widgets_) { w->hide(); delete w; }
    widgets_.clear();
}

void ClickOverlay::beginLive() {
    builder_.clear();
    destroyWidgets();
    visible_ = true;
    createWidgets();
}

void ClickOverlay::repaintMarker(size_t index) {
    if (index >= builder_.markers().size()) return;
    const QRect b = boundsOf(builder_.markers()[index]);
    for (auto* w : widgets_) w->updateNative(b);
}

void ClickOverlay::feed(const mmr::MacroEvent& e) {
    if (!visible_) return;  // late queued event after the recording ended
    if (const auto idx = builder_.onEvent(e)) repaintMarker(*idx);
}

void ClickOverlay::showRecord(const std::vector<mmr::MacroEvent>& events) {
    builder_.buildAll(events);
    destroyWidgets();
    visible_ = true;
    createWidgets();
    for (auto* w : widgets_) w->update();
}

void ClickOverlay::hideAll() {
    visible_ = false;
    destroyWidgets();
    builder_.clear();
}

}  // namespace mmr
