#include "TaskbarBadge.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shobjidl.h>

#include <QColor>
#include <QCoreApplication>
#include <QImage>
#include <QPainter>
#include <QPolygonF>
#include <QRadialGradient>
#include <cstring>

namespace mmr {
namespace {

constexpr int kIconPx = 32;  // Windows scales the overlay to 16 px at 100% DPI; 32 keeps it crisp when scaled up

// Glowing dot: soft halo, bright core with a highlight, thin light rim.
QImage drawDot(const QColor& base) {
    QImage img(kIconPx, kIconPx, QImage::Format_ARGB32);
    img.fill(Qt::transparent);
    QPainter p(&img);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(Qt::NoPen);
    const QPointF c(kIconPx / 2.0, kIconPx / 2.0);

    QColor glow = base;
    QRadialGradient halo(c, kIconPx / 2.0);
    glow.setAlpha(170); halo.setColorAt(0.50, glow);
    glow.setAlpha(60);  halo.setColorAt(0.75, glow);
    glow.setAlpha(0);   halo.setColorAt(1.00, glow);
    p.setBrush(halo);
    p.drawEllipse(c, kIconPx / 2.0, kIconPx / 2.0);

    constexpr double r = 9.0;
    QRadialGradient core(QPointF(c.x() - 2.5, c.y() - 3.0), r * 1.5);
    core.setColorAt(0.0, base.lighter(170));
    core.setColorAt(0.55, base);
    core.setColorAt(1.0, base.darker(130));
    p.setBrush(core);
    p.setPen(QPen(QColor(255, 255, 255, 170), 1.2));
    p.drawEllipse(c, r, r);
    return img;
}

// Mouse pointer (arrow) with a soft green glow; paused = the same pointer with a slanted red line across it.
QImage drawPointer(bool paused) {
    QImage img(kIconPx, kIconPx, QImage::Format_ARGB32);
    img.fill(Qt::transparent);
    QPainter p(&img);
    p.setRenderHint(QPainter::Antialiasing);
    const QPointF c(kIconPx / 2.0, kIconPx / 2.0);

    QColor glow = paused ? QColor(255, 176, 46) : QColor(34, 230, 110);
    QRadialGradient halo(c, kIconPx / 2.0);
    glow.setAlpha(120); halo.setColorAt(0.55, glow);
    glow.setAlpha(0);   halo.setColorAt(1.00, glow);
    p.setPen(Qt::NoPen);
    p.setBrush(halo);
    p.drawEllipse(c, kIconPx / 2.0, kIconPx / 2.0);

    QPolygonF arrow;
    arrow << QPointF(9, 4) << QPointF(9, 25) << QPointF(14.5, 19.8) << QPointF(18.2, 28) << QPointF(21.6, 26.4)
          << QPointF(17.9, 18.4) << QPointF(25, 18.4);
    p.setPen(QPen(QColor(18, 14, 18), 2.2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.setBrush(QColor(255, 255, 255));
    p.drawPolygon(arrow);

    if (paused) {
        p.setPen(QPen(QColor(10, 6, 8, 220), 6.2, Qt::SolidLine, Qt::RoundCap));  // dark under-stroke keeps it readable
        p.drawLine(QPointF(5, 27), QPointF(27, 5));
        p.setPen(QPen(QColor(255, 45, 70), 3.6, Qt::SolidLine, Qt::RoundCap));
        p.drawLine(QPointF(5, 27), QPointF(27, 5));
    }
    return img;
}

HICON makeIcon(const QImage& src) {
    const QImage img = src.convertToFormat(QImage::Format_ARGB32);  // straight alpha, BGRA in memory
    BITMAPV5HEADER bi{};
    bi.bV5Size = sizeof(bi);
    bi.bV5Width = img.width();
    bi.bV5Height = -img.height();  // top-down
    bi.bV5Planes = 1;
    bi.bV5BitCount = 32;
    bi.bV5Compression = BI_BITFIELDS;
    bi.bV5RedMask = 0x00FF0000;
    bi.bV5GreenMask = 0x0000FF00;
    bi.bV5BlueMask = 0x000000FF;
    bi.bV5AlphaMask = 0xFF000000;

    void* bits = nullptr;
    HDC dc = GetDC(nullptr);
    HBITMAP color = CreateDIBSection(dc, reinterpret_cast<BITMAPINFO*>(&bi), DIB_RGB_COLORS, &bits, nullptr, 0);
    ReleaseDC(nullptr, dc);
    HBITMAP mask = CreateBitmap(img.width(), img.height(), 1, 1, nullptr);
    HICON icon = nullptr;
    if (color && bits && mask) {
        std::memcpy(bits, img.constBits(), size_t(img.sizeInBytes()));
        ICONINFO ii{};
        ii.fIcon = TRUE;
        ii.hbmColor = color;
        ii.hbmMask = mask;
        icon = CreateIconIndirect(&ii);
    }
    if (color) DeleteObject(color);
    if (mask) DeleteObject(mask);
    return icon;
}

ITaskbarList3* tb(void* p) { return static_cast<ITaskbarList3*>(p); }

}  // namespace

TaskbarBadge::TaskbarBadge(QWidget* window) : window_(window) {
    buttonCreatedMsg_ = RegisterWindowMessageW(L"TaskbarButtonCreated");
    QCoreApplication::instance()->installNativeEventFilter(this);
}

TaskbarBadge::~TaskbarBadge() {
    if (auto* app = QCoreApplication::instance()) app->removeNativeEventFilter(this);
    if (taskbar_) {
        if (window_ && window_->testAttribute(Qt::WA_WState_Created))
            tb(taskbar_)->SetOverlayIcon(reinterpret_cast<HWND>(window_->winId()), nullptr, nullptr);
        tb(taskbar_)->Release();
    }
    for (void* i : icons_)
        if (i) DestroyIcon(static_cast<HICON>(i));
}

void* TaskbarBadge::icon(Mode mode) {
    const int i = mode == Mode::Recording ? 0 : mode == Mode::Paused ? 1 : mode == Mode::AutoClick ? 2 : 3;
    if (!icons_[i]) {
        if (i == 0) icons_[i] = makeIcon(drawDot(QColor(34, 230, 110)));
        else if (i == 1) icons_[i] = makeIcon(drawDot(QColor(255, 200, 30)));
        else icons_[i] = makeIcon(drawPointer(i == 3));
    }
    return icons_[i];
}

bool TaskbarBadge::apply() {
    if (!window_ || !window_->testAttribute(Qt::WA_WState_Created)) return false;  // no native window yet
    const HWND hwnd = reinterpret_cast<HWND>(window_->winId());
    if (!taskbar_) {
        ITaskbarList3* t = nullptr;
        if (FAILED(CoCreateInstance(__uuidof(TaskbarList), nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&t))) || !t) return false;
        if (FAILED(t->HrInit())) { t->Release(); return false; }
        taskbar_ = t;
    }
    if (!filterAllowed_ && buttonCreatedMsg_) {  // keep working if the app runs elevated (UIPI)
        ChangeWindowMessageFilterEx(hwnd, buttonCreatedMsg_, MSGFLT_ALLOW, nullptr);
        filterAllowed_ = true;
    }
    if (mode_ == Mode::None) return SUCCEEDED(tb(taskbar_)->SetOverlayIcon(hwnd, nullptr, nullptr));
    const wchar_t* text = mode_ == Mode::Recording ? L"Recording"
                          : mode_ == Mode::Paused ? L"Recording paused"
                          : mode_ == Mode::AutoClick ? L"Auto Clicker running" : L"Auto Clicker paused";
    return SUCCEEDED(tb(taskbar_)->SetOverlayIcon(hwnd, static_cast<HICON>(icon(mode_)), text));
}

void TaskbarBadge::set(Mode mode) {
    if (mode == mode_) return;
    mode_ = mode;
    if (mode == Mode::None && !taskbar_) return;  // nothing was ever shown
    apply();
}

bool TaskbarBadge::nativeEventFilter(const QByteArray& eventType, void* message, qintptr*) {
    if (mode_ == Mode::None || !buttonCreatedMsg_ || eventType != "windows_generic_MSG") return false;
    const MSG* m = static_cast<const MSG*>(message);
    if (m->message == buttonCreatedMsg_ && window_ && window_->testAttribute(Qt::WA_WState_Created) &&
        m->hwnd == reinterpret_cast<HWND>(window_->winId()))
        apply();  // the button was (re)created: the overlay must be set again
    return false;  // never swallow the message
}

}  // namespace mmr
