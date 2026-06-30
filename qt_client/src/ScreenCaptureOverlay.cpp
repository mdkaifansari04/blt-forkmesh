#include "ScreenCaptureOverlay.h"

#include <QEventLoop>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPaintEvent>
#include <QPixmap>
#include <QScreen>

ScreenCaptureOverlay *ScreenCaptureOverlay::begin()
{
    const QList<QScreen *> screens = QGuiApplication::screens();
    if (screens.isEmpty())
        return nullptr;

    // The overlay must cover every monitor, so work in the union of all screen
    // geometries (the "virtual desktop").
    QRect virtualGeom;
    for (QScreen *s : screens)
        virtualGeom = virtualGeom.united(s->geometry());
    if (virtualGeom.isEmpty())
        return nullptr;

    // Grab/crop at the primary screen's device-pixel ratio later, on release.
    const qreal dpr = QGuiApplication::primaryScreen()
                          ? QGuiApplication::primaryScreen()->devicePixelRatio()
                          : 1.0;

    auto *overlay = new ScreenCaptureOverlay(virtualGeom, dpr);
    overlay->show();
    overlay->raise();
    overlay->activateWindow();
    overlay->setFocus();
    return overlay;
}

ScreenCaptureOverlay::ScreenCaptureOverlay(const QRect &virtualGeom, qreal dpr)
    : QWidget(nullptr), m_virtualGeom(virtualGeom), m_dpr(dpr)
{
    // Frameless, always-on-top, and bypassing the window manager so nothing
    // repositions or resizes the overlay away from full virtual-desktop coverage.
    setWindowFlags(Qt::Window | Qt::FramelessWindowHint |
                   Qt::WindowStaysOnTopHint | Qt::BypassWindowManagerHint);
    setAttribute(Qt::WA_DeleteOnClose);
    // Transparent overlay: the live desktop stays visible underneath. We never
    // freeze a snapshot or dim anything — just a dotted marquee while dragging —
    // so picking a region doesn't black out the screen.
    setAttribute(Qt::WA_TranslucentBackground);
    setCursor(Qt::CrossCursor);
    setMouseTracking(true);
    setGeometry(virtualGeom);
}

QRect ScreenCaptureOverlay::selectionRect() const
{
    return QRect(m_origin, m_current).normalized();
}

void ScreenCaptureOverlay::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    // A near-invisible veil (alpha 1/255) over the whole desktop guarantees the
    // transparent overlay still receives the drag everywhere, without visibly
    // changing the background.
    painter.fillRect(rect(), QColor(0, 0, 0, 1));
    if (!m_dragging)
        return;
    const QRect sel = selectionRect();
    if (sel.isNull())
        return;
    // Dotted marquee around the selection, no fill — the desktop shows through
    // and nothing is dimmed.
    QPen pen(QColor("#2f81f7"));
    pen.setWidth(1);
    pen.setStyle(Qt::DashLine);
    painter.setPen(pen);
    painter.setBrush(Qt::NoBrush);
    painter.drawRect(sel.adjusted(0, 0, -1, -1));
}

void ScreenCaptureOverlay::mousePressEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton) {
        finish(QImage()); // right/middle click cancels
        return;
    }
    m_dragging = true;
    m_origin = event->position().toPoint();
    m_current = m_origin;
    update();
}

void ScreenCaptureOverlay::mouseMoveEvent(QMouseEvent *event)
{
    if (!m_dragging)
        return;
    m_current = event->position().toPoint();
    update();
}

void ScreenCaptureOverlay::mouseReleaseEvent(QMouseEvent *event)
{
    if (!m_dragging || event->button() != Qt::LeftButton)
        return;
    m_dragging = false;
    m_current = event->position().toPoint();
    const QRect sel = selectionRect();
    if (sel.width() < 3 || sel.height() < 3) {
        finish(QImage()); // a click without a real drag is a cancel
        return;
    }
    finish(grabSelection(sel));
}

QImage ScreenCaptureOverlay::grabSelection(const QRect &sel)
{
    // Hide the transparent overlay first so neither the veil nor the marquee can
    // bleed into the live grab, then let the compositor repaint the desktop.
    hide();
    QGuiApplication::processEvents(QEventLoop::ExcludeUserInputEvents, 50);

    // Composite each screen's grab into one snapshot of the virtual desktop at
    // the primary ratio (drawPixmap honours each grab's own ratio, so mixed-DPI
    // setups still line up in logical coords), then crop the selection.
    QPixmap shot(m_virtualGeom.size() * m_dpr);
    shot.setDevicePixelRatio(m_dpr);
    shot.fill(Qt::black);
    {
        QPainter painter(&shot);
        for (QScreen *s : QGuiApplication::screens()) {
            const QPixmap grab = s->grabWindow(0);
            const QRect g = s->geometry();
            painter.drawPixmap(QPointF(g.x() - m_virtualGeom.x(),
                                       g.y() - m_virtualGeom.y()),
                               grab);
        }
    }
    const QRect devRect(QPoint(qRound(sel.x() * m_dpr), qRound(sel.y() * m_dpr)),
                        QSize(qRound(sel.width() * m_dpr),
                              qRound(sel.height() * m_dpr)));
    return shot.copy(devRect).toImage();
}

void ScreenCaptureOverlay::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Escape) {
        finish(QImage());
        return;
    }
    QWidget::keyPressEvent(event);
}

void ScreenCaptureOverlay::finish(const QImage &image)
{
    if (m_done)
        return;
    m_done = true;
    if (image.isNull())
        emit cancelled();
    else
        emit captured(image);
    close(); // WA_DeleteOnClose frees the overlay
}
