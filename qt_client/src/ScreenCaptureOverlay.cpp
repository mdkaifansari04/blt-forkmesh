#include "ScreenCaptureOverlay.h"

#include <QGuiApplication>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPaintEvent>
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

    // Composite each screen's grab into one snapshot at the primary screen's
    // device-pixel ratio. drawPixmap honours each grab's own ratio, so mixed-DPI
    // setups still line up in logical coordinates.
    const qreal dpr = QGuiApplication::primaryScreen()
                          ? QGuiApplication::primaryScreen()->devicePixelRatio()
                          : 1.0;
    QPixmap shot(virtualGeom.size() * dpr);
    shot.setDevicePixelRatio(dpr);
    shot.fill(Qt::black);
    {
        QPainter painter(&shot);
        for (QScreen *s : screens) {
            const QPixmap grab = s->grabWindow(0);
            const QRect g = s->geometry();
            painter.drawPixmap(QPointF(g.x() - virtualGeom.x(),
                                       g.y() - virtualGeom.y()),
                               grab);
        }
    }

    auto *overlay = new ScreenCaptureOverlay(shot, virtualGeom, dpr);
    overlay->show();
    overlay->raise();
    overlay->activateWindow();
    overlay->setFocus();
    return overlay;
}

ScreenCaptureOverlay::ScreenCaptureOverlay(const QPixmap &shot,
                                           const QRect &virtualGeom, qreal dpr)
    : QWidget(nullptr), m_shot(shot), m_dpr(dpr)
{
    // Frameless, always-on-top, and bypassing the window manager so nothing
    // repositions or resizes the overlay away from full virtual-desktop coverage.
    setWindowFlags(Qt::Window | Qt::FramelessWindowHint |
                   Qt::WindowStaysOnTopHint | Qt::BypassWindowManagerHint);
    setAttribute(Qt::WA_DeleteOnClose);
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
    // The frozen desktop underneath.
    painter.drawPixmap(0, 0, m_shot);
    // Dim everything, then punch a clear hole over the live selection.
    const QColor dim(0, 0, 0, 110);
    const QRect sel = m_dragging ? selectionRect() : QRect();
    if (sel.isNull()) {
        painter.fillRect(rect(), dim);
        return;
    }
    QRegion outside(rect());
    outside -= QRegion(sel);
    painter.setClipRegion(outside);
    painter.fillRect(rect(), dim);
    painter.setClipping(false);

    // A bright marquee border around the selection.
    QPen pen(QColor("#2f81f7"));
    pen.setWidth(1);
    painter.setPen(pen);
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
    // Map the logical selection to device pixels in the snapshot and crop.
    const QRect devRect(QPoint(qRound(sel.x() * m_dpr), qRound(sel.y() * m_dpr)),
                        QSize(qRound(sel.width() * m_dpr),
                              qRound(sel.height() * m_dpr)));
    finish(m_shot.copy(devRect).toImage());
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
