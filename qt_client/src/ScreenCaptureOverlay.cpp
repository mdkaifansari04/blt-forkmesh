#include "ScreenCaptureOverlay.h"

#include <QCursor>
#include <QEventLoop>
#include <QFile>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPaintEvent>
#include <QPixmap>
#include <QRandomGenerator>
#include <QScreen>
#include <QTimer>
#include <QUrl>

#ifdef FORKMESH_HAVE_PORTAL
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCall>
#endif

namespace {

// True when running on a Wayland compositor, where grabbing the screen directly
// is forbidden (it comes back all-black) and we must go through the XDG portal.
bool runningOnWayland()
{
    return QGuiApplication::platformName().startsWith(QLatin1String("wayland"),
                                                      Qt::CaseInsensitive);
}

// A deliberate crosshair "snip" reticle so the pointer clearly reads as a
// screenshot tool. Drawn with a dark halo under a bright core so it stays
// visible over both light and dark content, with a small gap at the centre so
// the exact target pixel isn't covered. The hotspot is that centre.
QCursor makeSnipCursor()
{
    constexpr int n = 32;       // logical cursor size
    constexpr int c = n / 2;    // centre / hotspot
    constexpr int gap = 3;      // clear space around the exact point
    constexpr int arm = c - 1;  // how far each arm reaches

    QPixmap pm(n, n);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, false);
    const auto drawCross = [&](const QColor &col, int w) {
        QPen pen(col);
        pen.setWidth(w);
        p.setPen(pen);
        p.drawLine(c - arm, c, c - gap, c); // left arm
        p.drawLine(c + gap, c, c + arm, c); // right arm
        p.drawLine(c, c - arm, c, c - gap); // top arm
        p.drawLine(c, c + gap, c, c + arm); // bottom arm
    };
    drawCross(QColor(0, 0, 0, 200), 3); // halo
    drawCross(QColor(255, 255, 255), 1); // core
    p.end();
    return QCursor(pm, c, c);
}

} // namespace

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
    setMouseTracking(true);
    setGeometry(virtualGeom);

    // Swap the pointer to a crosshair snip reticle so it's obvious a screenshot
    // is in progress. Set it on the widget and push it as an application override
    // too, so it takes effect even where a per-widget cursor is ignored (e.g. a
    // bypass-WM surface on Wayland).
    const QCursor snip = makeSnipCursor();
    setCursor(snip);
    QGuiApplication::setOverrideCursor(snip);
    m_cursorPushed = true;
}

void ScreenCaptureOverlay::popOverrideCursor()
{
    if (!m_cursorPushed)
        return;
    m_cursorPushed = false;
    QGuiApplication::restoreOverrideCursor();
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
    beginCapture(sel);
}

void ScreenCaptureOverlay::beginCapture(const QRect &sel)
{
    // The selection is locked in, so drop the snip cursor and hide the overlay
    // before grabbing so neither the veil nor the marquee can bleed into it.
    popOverrideCursor();
    hide();

#ifdef FORKMESH_HAVE_PORTAL
    if (runningOnWayland()) {
        // Defer the portal call a beat so the compositor has actually dropped
        // our (now hidden) surface before it snapshots the desktop.
        m_pendingSel = sel;
        QTimer::singleShot(150, this, [this] { grabViaPortal(m_pendingSel); });
        return;
    }
#endif
    finish(grabViaScreens(sel));
}

QImage ScreenCaptureOverlay::cropDesktop(const QImage &full, const QRect &sel) const
{
    if (full.isNull())
        return QImage();
    // The desktop image covers m_virtualGeom in logical coords; map the (logical)
    // selection into its pixels using the image's own scale, so fractional and
    // mixed-DPI scaling line up without assuming a particular ratio.
    const double sx = double(full.width()) / m_virtualGeom.width();
    const double sy = double(full.height()) / m_virtualGeom.height();
    QRect dev(qRound(sel.x() * sx), qRound(sel.y() * sy),
              qRound(sel.width() * sx), qRound(sel.height() * sy));
    dev = dev.intersected(full.rect());
    if (dev.isEmpty())
        return QImage();
    return full.copy(dev);
}

QImage ScreenCaptureOverlay::grabViaScreens(const QRect &sel)
{
    // Let the compositor repaint the now-hidden overlay away before grabbing.
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
    return cropDesktop(shot.toImage(), sel);
}

void ScreenCaptureOverlay::grabViaPortal(const QRect &sel)
{
#ifdef FORKMESH_HAVE_PORTAL
    QDBusConnection bus = QDBusConnection::sessionBus();

    // Predict the Request object path from a handle token so we can subscribe to
    // its Response signal *before* issuing the call, closing the connect/emit
    // race the portal API warns about.
    const QString token =
        QStringLiteral("forkmesh_%1").arg(QRandomGenerator::global()->generate());
    QString sender = bus.baseService(); // ":1.xx"
    if (sender.startsWith(QLatin1Char(':')))
        sender.remove(0, 1);
    sender.replace(QLatin1Char('.'), QLatin1Char('_'));
    const QString reqPath =
        QStringLiteral("/org/freedesktop/portal/desktop/request/%1/%2")
            .arg(sender, token);

    bus.connect(QStringLiteral("org.freedesktop.portal.Desktop"), reqPath,
                QStringLiteral("org.freedesktop.portal.Request"),
                QStringLiteral("Response"), this,
                SLOT(onPortalResponse(uint, QVariantMap)));

    QVariantMap opts;
    opts.insert(QStringLiteral("handle_token"), token);
    opts.insert(QStringLiteral("interactive"), false); // grab the whole desktop
    QDBusMessage call = QDBusMessage::createMethodCall(
        QStringLiteral("org.freedesktop.portal.Desktop"),
        QStringLiteral("/org/freedesktop/portal/desktop"),
        QStringLiteral("org.freedesktop.portal.Screenshot"),
        QStringLiteral("Screenshot"));
    call << QString() << opts; // parent_window (none) + options
    bus.asyncCall(call);

    // Safety net: never leave the overlay alive forever if the portal (or a
    // stuck permission dialog) never answers.
    QTimer::singleShot(8000, this, [this] {
        if (!m_done)
            finish(QImage());
    });
#else
    finish(grabViaScreens(sel));
#endif
}

void ScreenCaptureOverlay::onPortalResponse(uint response, const QVariantMap &results)
{
    if (response != 0) { // 1 = user cancelled, 2 = ended some other way
        finish(QImage());
        return;
    }
    const QString path = QUrl(results.value(QStringLiteral("uri")).toString()).toLocalFile();
    QImage full(path);
    if (!path.isEmpty())
        QFile::remove(path); // the portal drops a temp PNG; don't litter
    finish(cropDesktop(full, m_pendingSel));
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
    popOverrideCursor(); // no-op if already restored when the grab began
    if (image.isNull())
        emit cancelled();
    else
        emit captured(image);
    close(); // WA_DeleteOnClose frees the overlay
}
