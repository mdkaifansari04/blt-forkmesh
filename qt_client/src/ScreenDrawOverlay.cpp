#include "ScreenDrawOverlay.h"

#include "ScreenCaptureOverlay.h"

#include <QCursor>
#include <QFont>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPaintEvent>
#include <QPixmap>
#include <QScreen>

namespace {

// A little pen-nib cursor so the pointer clearly reads as a drawing tool. Drawn
// with a dark halo under a bright core so it stays visible over both light and
// dark content; the hotspot is the nib tip at the bottom-left.
QCursor makePenCursor()
{
    constexpr int n = 32;
    QPixmap pm(n, n);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);

    const QPointF tip(3, n - 3);   // nib touches here — the hotspot
    const QPointF back(n - 8, 8);  // top of the pen body
    const auto drawPen = [&](const QColor &col, int w) {
        QPen pen(col);
        pen.setWidth(w);
        pen.setCapStyle(Qt::RoundCap);
        p.setPen(pen);
        p.drawLine(tip, back);
        // A short cross-stroke near the tip to suggest a nib.
        p.drawLine(tip + QPointF(0, -6), tip + QPointF(6, 0));
    };
    drawPen(QColor(0, 0, 0, 200), 5); // halo
    drawPen(QColor(255, 255, 255), 2); // core
    p.end();
    return QCursor(pm, 3, n - 3);
}

} // namespace

ScreenDrawOverlay *ScreenDrawOverlay::begin()
{
    const QList<QScreen *> screens = QGuiApplication::screens();
    if (screens.isEmpty())
        return nullptr;

    // Cover every monitor: work in the union of all screen geometries.
    QRect virtualGeom;
    for (QScreen *s : screens)
        virtualGeom = virtualGeom.united(s->geometry());
    if (virtualGeom.isEmpty())
        return nullptr;

    auto *overlay = new ScreenDrawOverlay(virtualGeom);
    overlay->show();
    overlay->raise();
    overlay->activateWindow();
    overlay->setFocus();
    return overlay;
}

ScreenDrawOverlay::ScreenDrawOverlay(const QRect &virtualGeom)
    : QWidget(nullptr), m_virtualGeom(virtualGeom), m_penColor(QColor("#ff3b30"))
{
    // Frameless, always-on-top, and bypassing the window manager so nothing
    // repositions the overlay away from full virtual-desktop coverage.
    setWindowFlags(Qt::Window | Qt::FramelessWindowHint |
                   Qt::WindowStaysOnTopHint | Qt::BypassWindowManagerHint);
    setAttribute(Qt::WA_DeleteOnClose);
    // Transparent overlay: the live desktop stays visible under the ink.
    setAttribute(Qt::WA_TranslucentBackground);
    setMouseTracking(true);
    setGeometry(virtualGeom);

    // Swap the pointer to a pen so it's obvious you're drawing. Set it on the
    // widget and push it as an application override too, so it takes effect even
    // where a per-widget cursor is ignored (e.g. a bypass-WM surface on Wayland).
    const QCursor pen = makePenCursor();
    setCursor(pen);
    QGuiApplication::setOverrideCursor(pen);
    m_cursorPushed = true;
}

void ScreenDrawOverlay::popOverrideCursor()
{
    if (!m_cursorPushed)
        return;
    m_cursorPushed = false;
    QGuiApplication::restoreOverrideCursor();
}

void ScreenDrawOverlay::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    // A near-invisible veil (alpha 1/255) over the whole desktop guarantees the
    // transparent overlay still receives the drag everywhere, without visibly
    // changing the background.
    painter.fillRect(rect(), QColor(0, 0, 0, 1));

    painter.setRenderHint(QPainter::Antialiasing, true);
    QPen pen(m_penColor);
    pen.setWidth(3);
    pen.setCapStyle(Qt::RoundCap);
    pen.setJoinStyle(Qt::RoundJoin);
    painter.setPen(pen);
    painter.setBrush(Qt::NoBrush);

    const auto drawStroke = [&](const QVector<QPoint> &stroke) {
        if (stroke.size() == 1)
            painter.drawPoint(stroke.first()); // a tap leaves a dot
        else if (stroke.size() > 1)
            painter.drawPolyline(stroke.constData(), stroke.size());
    };
    for (const QVector<QPoint> &stroke : m_strokes)
        drawStroke(stroke);
    drawStroke(m_current);

    paintScreenshotButton(painter);
}

// One "Screenshot" pill rect per connected screen, centred at the top of each.
// Widget-local coordinates (overlay origin = m_virtualGeom.topLeft()).
QVector<QRect> ScreenDrawOverlay::screenshotButtonRects() const
{
    const QList<QScreen *> screens = QGuiApplication::screens();
    QVector<QRect> rects;
    rects.reserve(screens.size());
    const int w = 150, h = 40;
    for (QScreen *s : screens) {
        const QRect local = s->geometry().translated(-m_virtualGeom.topLeft());
        const int x = local.x() + (local.width() - w) / 2;
        const int y = local.y() + 24;
        rects.append(QRect(x, y, w, h));
    }
    return rects;
}

void ScreenDrawOverlay::paintScreenshotButton(QPainter &painter)
{
    if (m_capturing)
        return; // keep the button out of the grabbed screenshot

    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);

    for (const QRect &r : screenshotButtonRects()) {
        // Pill background with a faint border so it reads as a control over any wallpaper.
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(20, 22, 28, 225));
        painter.drawRoundedRect(r, 9, 9);
        painter.setBrush(Qt::NoBrush);
        painter.setPen(QPen(QColor(255, 255, 255, 45), 1));
        painter.drawRoundedRect(r.adjusted(0, 0, -1, -1), 9, 9);

        // A crop-frame glyph (corner brackets) echoing the nav screenshot icon.
        const QRect icon(r.left() + 12, r.center().y() - 7, 16, 14);
        QPen ip(QColor(255, 255, 255, 235));
        ip.setWidth(2);
        ip.setCapStyle(Qt::RoundCap);
        painter.setPen(ip);
        const int a = 5; // bracket arm length
        const auto corner = [&](const QPoint &c, int dx, int dy) {
            painter.drawLine(c, c + QPoint(dx, 0));
            painter.drawLine(c, c + QPoint(0, dy));
        };
        corner(icon.topLeft(), a, a);
        corner(icon.topRight(), -a, a);
        corner(icon.bottomLeft(), a, -a);
        corner(icon.bottomRight(), -a, -a);

        // Label.
        painter.setPen(QColor(255, 255, 255, 240));
        QFont f = painter.font();
        f.setBold(true);
        painter.setFont(f);
        const QRect textRect(icon.right() + 10, r.top(), r.right() - icon.right() - 18,
                             r.height());
        painter.drawText(textRect, Qt::AlignVCenter | Qt::AlignLeft,
                         QStringLiteral("Screenshot"));
    }

    painter.restore();
}

// Hand off to a region screenshot. This overlay stays shown underneath the capture
// overlay, so the ink is part of the live desktop the region grab composites; the
// button is hidden first so it isn't captured. On success the image is emitted and
// the overlay dismisses; a cancel returns to drawing.
void ScreenDrawOverlay::startCapture()
{
    if (m_capturing)
        return;
    m_capturing = true;
    m_drawing = false;
    m_current.clear();
    update();

    ScreenCaptureOverlay *cap = ScreenCaptureOverlay::begin();
    if (!cap) {
        m_capturing = false;
        update();
        return;
    }
    connect(cap, &ScreenCaptureOverlay::captured, this,
            [this](const QImage &image) {
                emit captured(image);
                finish();
            });
    connect(cap, &ScreenCaptureOverlay::cancelled, this, [this] {
        m_capturing = false;
        update();
    });
}

void ScreenDrawOverlay::setScreenshotHotzone(const QRect &globalRect)
{
    m_screenshotHotzone = globalRect; // stored in global screen coordinates
}

void ScreenDrawOverlay::mousePressEvent(QMouseEvent *event)
{
    const QPoint pos = event->position().toPoint();
    // mapToGlobal() uses the widget's actual on-screen position and is reliable
    // even when the bypass-WM window isn't placed exactly at m_virtualGeom.topLeft().
    const QPoint globalPos = mapToGlobal(pos);
    const bool inButton = [&] {
        for (const QRect &r : screenshotButtonRects())
            if (r.contains(pos))
                return true;
        return false;
    }();
    if (event->button() == Qt::LeftButton && !m_capturing &&
        (inButton ||
         (!m_screenshotHotzone.isNull() && m_screenshotHotzone.contains(globalPos)))) {
        startCapture(); // click any floating button or nav icon -> region screenshot with ink
        return;
    }
    if (event->button() != Qt::LeftButton) {
        finish(); // right/middle click clears and dismisses
        return;
    }
    m_drawing = true;
    m_current.clear();
    m_current.append(event->position().toPoint());
    update();
}

void ScreenDrawOverlay::mouseMoveEvent(QMouseEvent *event)
{
    if (!m_drawing)
        return;
    m_current.append(event->position().toPoint());
    update();
}

void ScreenDrawOverlay::mouseReleaseEvent(QMouseEvent *event)
{
    if (!m_drawing || event->button() != Qt::LeftButton)
        return;
    m_drawing = false;
    if (!m_current.isEmpty())
        m_strokes.append(m_current);
    m_current.clear();
    update();
}

void ScreenDrawOverlay::keyPressEvent(QKeyEvent *event)
{
    switch (event->key()) {
    case Qt::Key_Escape:
        finish();
        return;
    case Qt::Key_Backspace:
    case Qt::Key_Delete:
        // Wipe the ink but keep drawing.
        m_strokes.clear();
        m_current.clear();
        update();
        return;
    default:
        break;
    }
    QWidget::keyPressEvent(event);
}

void ScreenDrawOverlay::finish()
{
    if (m_done)
        return;
    m_done = true;
    popOverrideCursor();
    emit dismissed();
    close(); // WA_DeleteOnClose frees the overlay
}
