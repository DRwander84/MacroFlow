#include "ClickPointPicker.h"

#include <QGuiApplication>
#include <QMouseEvent>
#include <QPainter>
#include <QScreen>
#include <QWidget>
#include <algorithm>
#include <cmath>
#include <functional>

namespace mmr {
namespace {

constexpr int kMarkerSize = 72;  // logical pixels

// Same convention as ClickOverlay: a screen's geometry().topLeft() is its native pixel origin,
// its size is native size / devicePixelRatio.
QRect nativeRectOf(const QScreen* s) {
    const qreal dpr = s->devicePixelRatio();
    return QRect(s->geometry().topLeft(), QSize(int(std::lround(s->geometry().width() * dpr)), int(std::lround(s->geometry().height() * dpr))));
}

QPoint nativeToLogical(const QPoint& n) {
    for (const QScreen* s : QGuiApplication::screens()) {
        if (!nativeRectOf(s).contains(n)) continue;
        const QPoint o = s->geometry().topLeft();
        const qreal dpr = s->devicePixelRatio();
        return o + QPoint(int(std::lround((n.x() - o.x()) / dpr)), int(std::lround((n.y() - o.y()) / dpr)));
    }
    return n;
}

QPoint logicalToNative(const QPoint& l) {
    const QScreen* s = QGuiApplication::screenAt(l);
    if (!s) s = QGuiApplication::primaryScreen();
    if (!s) return l;
    const QPoint o = s->geometry().topLeft();
    const qreal dpr = s->devicePixelRatio();
    return o + QPoint(int(std::lround((l.x() - o.x()) * dpr)), int(std::lround((l.y() - o.y()) * dpr)));
}

// A saved point can end up off-screen (monitor unplugged / layout changed): pull it back onto a screen.
QPoint clampToScreens(const QPoint& n) {
    for (const QScreen* s : QGuiApplication::screens())
        if (nativeRectOf(s).contains(n)) return n;
    const QScreen* ps = QGuiApplication::primaryScreen();
    if (!ps) return n;
    const QRect r = nativeRectOf(ps);
    return QPoint(std::clamp(n.x(), r.left() + 40, r.right() - 40), std::clamp(n.y(), r.top() + 40, r.bottom() - 40));
}

}  // namespace

// One circle. A frameless, always-on-top tool window that never takes keyboard focus; dragging moves it.
class PointMarker : public QWidget {
public:
    PointMarker(int number, std::function<void()> onMoved)
        : QWidget(nullptr, Qt::FramelessWindowHint | Qt::Tool | Qt::WindowStaysOnTopHint | Qt::WindowDoesNotAcceptFocus |
                               Qt::NoDropShadowWindowHint),
          number_(number), onMoved_(std::move(onMoved)) {
        setAttribute(Qt::WA_TranslucentBackground);
        setAttribute(Qt::WA_ShowWithoutActivating);
        setFixedSize(kMarkerSize, kMarkerSize);
        setCursor(Qt::SizeAllCursor);
        setToolTip("Drag to the spot to click");
    }

    QPoint centerNative() const { return logicalToNative(geometry().center()); }
    void setCenterNative(const QPoint& n) {
        const QPoint c = nativeToLogical(n);
        move(c.x() - width() / 2, c.y() - height() / 2);
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        const QPointF c(width() / 2.0, height() / 2.0);
        const QColor accent(255, 45, 70);
        QColor fill = accent, glow = accent;
        fill.setAlpha(62);
        glow.setAlpha(40);
        p.setPen(Qt::NoPen);
        p.setBrush(glow);
        p.drawEllipse(c, 30, 30);  // soft halo
        p.setBrush(fill);
        p.setPen(QPen(dragging_ ? QColor(255, 120, 135) : accent, 2.5));
        p.drawEllipse(c, 21, 21);
        p.setPen(QPen(QColor(255, 255, 255, 190), 1.4));  // crosshair: the exact click point is the centre
        p.drawLine(QPointF(c.x() - 11, c.y()), QPointF(c.x() - 5, c.y()));
        p.drawLine(QPointF(c.x() + 5, c.y()), QPointF(c.x() + 11, c.y()));
        p.drawLine(QPointF(c.x(), c.y() - 11), QPointF(c.x(), c.y() - 5));
        p.drawLine(QPointF(c.x(), c.y() + 5), QPointF(c.x(), c.y() + 11));
        p.setPen(Qt::NoPen);
        p.setBrush(accent);
        p.drawEllipse(c, 2.8, 2.8);

        QFont f = font();  // number badge, same look as the recording markers
        f.setPixelSize(12);
        f.setBold(true);
        p.setFont(f);
        const QString text = QString::number(number_);
        const QRectF badge(c.x() + 12, 4, p.fontMetrics().horizontalAdvance(text) + 10, 17);
        p.setBrush(QColor(10, 10, 14, 225));
        p.drawRoundedRect(badge, 8, 8);
        p.setPen(accent);
        p.drawText(badge, Qt::AlignCenter, text);
    }
    void mousePressEvent(QMouseEvent* e) override {
        if (e->button() != Qt::LeftButton) return;
        grab_ = e->globalPosition().toPoint() - frameGeometry().topLeft();
        dragging_ = true;
        update();
    }
    void mouseMoveEvent(QMouseEvent* e) override {
        if (dragging_) move(e->globalPosition().toPoint() - grab_);
    }
    void mouseReleaseEvent(QMouseEvent* e) override {
        if (e->button() != Qt::LeftButton || !dragging_) return;
        dragging_ = false;
        update();
        if (onMoved_) onMoved_();
    }

private:
    int number_;
    std::function<void()> onMoved_;
    QPoint grab_;
    bool dragging_ = false;
};

ClickPointPicker::ClickPointPicker(QObject* parent) : QObject(parent) {
    auto relayout = [this] { if (shown_) rebuild(); };
    connect(qGuiApp, &QGuiApplication::screenAdded, this, relayout);
    connect(qGuiApp, &QGuiApplication::screenRemoved, this, relayout);
}

ClickPointPicker::~ClickPointPicker() { destroyMarkers(); }

void ClickPointPicker::setPoints(const std::vector<QPoint>& nativePoints) {
    points_ = nativePoints;
    setCount(count_);
}

void ClickPointPicker::setCount(int count) {
    count_ = std::clamp(count, 1, 20);
    while (int(points_.size()) < count_) points_.push_back(defaultPoint(int(points_.size()), count_));
    if (shown_) rebuild();
}

// New circles start in a neat row around the centre of the primary screen.
QPoint ClickPointPicker::defaultPoint(int index, int total) const {
    const QScreen* s = QGuiApplication::primaryScreen();
    if (!s) return QPoint(200 + 90 * index, 300);
    const QRect r = nativeRectOf(s);
    const double step = 90.0 * s->devicePixelRatio();
    const QPoint p(int(r.center().x() + (index - (total - 1) / 2.0) * step), r.center().y() + int(120 * s->devicePixelRatio()));
    return clampToScreens(p);
}

void ClickPointPicker::destroyMarkers() {
    for (PointMarker* m : markers_) { m->hide(); delete m; }
    markers_.clear();
}

void ClickPointPicker::rebuild() {
    destroyMarkers();
    for (int i = 0; i < count_; ++i) {
        points_[size_t(i)] = clampToScreens(points_[size_t(i)]);
        auto* m = new PointMarker(i + 1, [this, i] {
            if (size_t(i) < markers_.size()) points_[size_t(i)] = markers_[size_t(i)]->centerNative();
            emit pointsChanged();
        });
        m->setCenterNative(points_[size_t(i)]);
        markers_.push_back(m);
        m->show();
        m->raise();
    }
}

void ClickPointPicker::show() {
    shown_ = true;
    rebuild();
}

void ClickPointPicker::hide() {
    shown_ = false;
    destroyMarkers();
}

}  // namespace mmr
