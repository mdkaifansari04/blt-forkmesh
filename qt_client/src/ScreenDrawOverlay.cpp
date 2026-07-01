#include "ScreenDrawOverlay.h"

#include <QCursor>
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
}

void ScreenDrawOverlay::mousePressEvent(QMouseEvent *event)
{
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
