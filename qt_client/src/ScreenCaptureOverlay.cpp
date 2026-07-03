#include "ScreenCaptureOverlay.h"

#include <QByteArray>
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
#include <QWidget>

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
    if (QGuiApplication::platformName().startsWith(QLatin1String("wayland"),
                                                   Qt::CaseInsensitive))
        return true;
    // Under XWayland we present to Qt as an ordinary X11 client (platform "xcb"),
    // yet the compositor still blocks direct screen grabs — they come back black.
    // The session type is the reliable tell, so route those through the portal too.
    return qgetenv("XDG_SESSION_TYPE").compare("wayland", Qt::CaseInsensitive) == 0;
}

// A direct screen grab that comes back as a uniform black frame almost always
// means the compositor refused it (a Wayland/XWayland session we slipped into as
// an X client, or an X compositor that hadn't repainted the desktop yet) rather
// than a desktop that is genuinely all black — a real desktop always has *some*
// non-black pixel (a panel, window chrome, the cursor). Sample a coarse grid and
// report a failed grab only when every probe is exactly black, so a legitimately
// dark region is never mistaken for one.
bool looksLikeFailedGrab(const QImage &img)
{
    if (img.isNull())
        return true;
    const int cols = qMin(img.width(), 48);
    const int rows = qMin(img.height(), 48);
    if (cols <= 0 || rows <= 0)
        return true;
    for (int iy = 0; iy < rows; ++iy) {
        const int y = (iy * (img.height() - 1)) / qMax(1, rows - 1);
        for (int ix = 0; ix < cols; ++ix) {
            const int x = (ix * (img.width() - 1)) / qMax(1, cols - 1);
            const QRgb px = img.pixel(x, y);
            if (qRed(px) != 0 || qGreen(px) != 0 || qBlue(px) != 0)
                return false;
        }
    }
    return true;
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

// PerScreenPanel: a frameless transparent overlay covering one physical screen.
// It forwards all input to the ScreenCaptureOverlay coordinator, which updates
// shared drag state and repaints every panel so the selection marquee tracks
// across monitor boundaries regardless of where the drag started.
class PerScreenPanel : public QWidget
{
public:
    PerScreenPanel(ScreenCaptureOverlay *owner, const QRect &screenGeom,
                   const QCursor &cursor)
        : QWidget(nullptr), m_owner(owner), m_screenGeom(screenGeom)
    {
        setWindowFlags(Qt::Window | Qt::FramelessWindowHint |
                       Qt::WindowStaysOnTopHint | Qt::BypassWindowManagerHint);
        setAttribute(Qt::WA_DeleteOnClose);
        // Freeze-frame mode paints an opaque pre-grabbed shot of this screen,
        // so it needs no translucency. The translucent live overlay is only
        // used when no such shot exists — and WA_TranslucentBackground only
        // works under a compositing window manager: on a bare X11 session the
        // "transparent" panel renders as solid black, blacking out the screen.
        if (owner->m_frozen.isNull())
            setAttribute(Qt::WA_TranslucentBackground);
        setMouseTracking(true);
        setGeometry(screenGeom);
        setCursor(cursor);
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        if (!m_owner->m_frozen.isNull()) {
            // Freeze-frame mode: paint this screen's slice of the desktop shot
            // taken before the panels appeared. Visually indistinguishable from
            // the live desktop, but independent of compositor translucency.
            const QImage &frozen = m_owner->m_frozen;
            const QRect &vg = m_owner->m_virtualGeom;
            const double sx = double(frozen.width()) / vg.width();
            const double sy = double(frozen.height()) / vg.height();
            painter.drawImage(rect(), frozen,
                              QRectF((m_screenGeom.x() - vg.x()) * sx,
                                     (m_screenGeom.y() - vg.y()) * sy,
                                     m_screenGeom.width() * sx,
                                     m_screenGeom.height() * sy));
        } else {
            // A near-invisible veil (alpha 1/255) over this screen guarantees
            // the panel receives mouse events everywhere, without visibly
            // dimming content.
            painter.fillRect(rect(), QColor(0, 0, 0, 1));
        }
        if (!m_owner->m_dragging)
            return;
        // Map the global drag endpoints to this panel's local coordinate system.
        // Qt clips the rect at the widget boundary, so only the portion of the
        // selection that falls on this screen is drawn here — the rest shows on
        // the neighbouring panel(s).
        const QPoint origin  = m_owner->m_originGlobal  - m_screenGeom.topLeft();
        const QPoint current = m_owner->m_currentGlobal - m_screenGeom.topLeft();
        const QRect sel = QRect(origin, current).normalized();
        if (sel.isNull())
            return;
        QPen pen(QColor("#2f81f7"));
        pen.setWidth(1);
        pen.setStyle(Qt::DashLine);
        painter.setPen(pen);
        painter.setBrush(Qt::NoBrush);
        painter.drawRect(sel.adjusted(0, 0, -1, -1));
    }

    // Global cursor position derived from this panel's screen anchor rather
    // than event->globalPosition(). On Wayland there are no true global
    // coordinates: Qt synthesises them from where it *believes* the window
    // sits, and the compositor — not our setGeometry() — decides the real
    // placement, so the synthesised values can be shifted by an arbitrary
    // offset and the capture lands on a different part of the screen. The
    // panel is pinned to exactly one screen, so local position + that
    // screen's origin is always the true global point.
    QPoint globalFromLocal(const QPointF &local) const
    {
        return m_screenGeom.topLeft() + local.toPoint();
    }

    void mousePressEvent(QMouseEvent *event) override
    {
        m_owner->onPress(event->button(), globalFromLocal(event->position()));
    }

    void mouseMoveEvent(QMouseEvent *event) override
    {
        m_owner->onMove(globalFromLocal(event->position()));
    }

    void mouseReleaseEvent(QMouseEvent *event) override
    {
        m_owner->onRelease(event->button(), globalFromLocal(event->position()));
    }

    void keyPressEvent(QKeyEvent *event) override
    {
        if (event->key() == Qt::Key_Escape)
            m_owner->onEscape();
        else
            QWidget::keyPressEvent(event);
    }

private:
    ScreenCaptureOverlay *m_owner;
    QRect m_screenGeom; // this screen's position in global coords
};

ScreenCaptureOverlay *ScreenCaptureOverlay::begin()
{
    const QList<QScreen *> screens = QGuiApplication::screens();
    if (screens.isEmpty())
        return nullptr;

    QRect virtualGeom;
    for (QScreen *s : screens)
        virtualGeom = virtualGeom.united(s->geometry());
    if (virtualGeom.isEmpty())
        return nullptr;

    const qreal dpr = QGuiApplication::primaryScreen()
                          ? QGuiApplication::primaryScreen()->devicePixelRatio()
                          : 1.0;

    return new ScreenCaptureOverlay(virtualGeom, dpr, screens);
}

ScreenCaptureOverlay::ScreenCaptureOverlay(const QRect &virtualGeom, qreal dpr,
                                            const QList<QScreen *> &screens)
    : QObject(nullptr), m_virtualGeom(virtualGeom), m_dpr(dpr)
{
    // Grab the desktop *before* any panel exists, and let the panels paint
    // slices of that frozen shot instead of relying on translucency. Without a
    // compositing window manager (common on bare X11 setups) translucent
    // top-levels can't work — Qt renders them as solid black and every monitor
    // goes dark the moment the tool opens. The frozen shot is opaque, so it
    // looks like the desktop on any setup. Skip it on Wayland (direct grabs
    // come back black there, and Wayland always composites, so the translucent
    // live overlay is safe); likewise fall back to the live overlay if the
    // grab was refused, which equally implies a compositor is present.
    if (!runningOnWayland()) {
        const QImage shot = compositeScreens();
        if (!looksLikeFailedGrab(shot))
            m_frozen = shot;
    }

    const QCursor snip = makeSnipCursor();
    // Push an application-wide override so the snip cursor shows immediately
    // even between panel surfaces or before the first paint.
    QGuiApplication::setOverrideCursor(snip);
    m_cursorPushed = true;

    for (QScreen *s : screens) {
        auto *panel = new PerScreenPanel(this, s->geometry(), snip);
        m_panels.append(panel);
        // Pin the panel to its screen explicitly. On Wayland, clients cannot
        // position top-level windows — setGeometry() is silently ignored and
        // the compositor drops the panel wherever it likes, which used to
        // shift every coordinate derived from it. Fullscreen-on-a-screen is
        // the one placement Wayland does guarantee, so use it there; on X11
        // keep the bypass-WM geometry, which already covers the screen.
        panel->setScreen(s);
        if (runningOnWayland())
            panel->showFullScreen();
        else
            panel->show();
        panel->raise();
    }
    // Activate the first panel so keyboard events (e.g. Escape) work immediately
    // without requiring the user to click first.
    if (!m_panels.isEmpty()) {
        m_panels.first()->activateWindow();
        m_panels.first()->setFocus();
    }
}

void ScreenCaptureOverlay::popOverrideCursor()
{
    if (!m_cursorPushed)
        return;
    m_cursorPushed = false;
    QGuiApplication::restoreOverrideCursor();
}

void ScreenCaptureOverlay::onPress(Qt::MouseButton button, QPoint globalPos)
{
    if (button != Qt::LeftButton) {
        finish(QImage()); // right/middle click cancels
        return;
    }
    m_dragging = true;
    m_originGlobal = globalPos;
    m_currentGlobal = globalPos;
    for (auto *p : m_panels)
        p->update();
}

void ScreenCaptureOverlay::onMove(QPoint globalPos)
{
    if (!m_dragging)
        return;
    m_currentGlobal = globalPos;
    // Repaint all panels: the selection may now extend onto a neighbouring screen.
    for (auto *p : m_panels)
        p->update();
}

void ScreenCaptureOverlay::onRelease(Qt::MouseButton button, QPoint globalPos)
{
    if (!m_dragging || button != Qt::LeftButton)
        return;
    m_dragging = false;
    m_currentGlobal = globalPos;
    // Convert global coords to virtual-desktop-relative for the capture path.
    const QPoint a = m_originGlobal  - m_virtualGeom.topLeft();
    const QPoint b = m_currentGlobal - m_virtualGeom.topLeft();
    const QRect sel = QRect(a, b).normalized();
    if (sel.width() < 3 || sel.height() < 3) {
        finish(QImage()); // a click without a real drag is a cancel
        return;
    }
    beginCapture(sel);
}

void ScreenCaptureOverlay::onEscape()
{
    finish(QImage());
}

void ScreenCaptureOverlay::beginCapture(const QRect &sel)
{
    // Freeze-frame mode: the capture comes from the shot taken before the
    // panels ever appeared, so nothing of the overlay can bleed into it — crop
    // and finish directly, no hide-and-regrab round trip.
    if (!m_frozen.isNull()) {
        finish(cropDesktop(m_frozen, sel));
        return;
    }

    // Selection is locked in — drop the snip cursor and hide all panels before
    // grabbing so neither the veil nor the marquee can bleed into the screenshot.
    popOverrideCursor();
    for (auto *p : m_panels)
        p->hide();

#ifdef FORKMESH_HAVE_PORTAL
    if (runningOnWayland()) {
        // Defer the portal call a beat so the compositor has actually dropped
        // our (now hidden) surfaces before it snapshots the desktop.
        m_pendingSel = sel;
        QTimer::singleShot(150, this, [this] { grabViaPortal(m_pendingSel); });
        return;
    }

    // Even off Wayland the direct grab can hand back a uniform black frame (a
    // compositor that refused it, or one we couldn't pin as Wayland). Rather than
    // save a black screenshot, fall back to the portal, which is allowed to read
    // the real desktop. Checking the *whole* composite (not just the selection)
    // keeps a legitimately dark region from misfiring the fallback.
    const QImage desktop = compositeScreens();
    if (looksLikeFailedGrab(desktop)) {
        m_pendingSel = sel;
        QTimer::singleShot(150, this, [this] { grabViaPortal(m_pendingSel); });
        return;
    }
    finish(cropDesktop(desktop, sel));
#else
    finish(grabViaScreens(sel));
#endif
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

QImage ScreenCaptureOverlay::compositeScreens()
{
    // Let the compositor repaint the now-hidden panels away before grabbing.
    QGuiApplication::processEvents(QEventLoop::ExcludeUserInputEvents, 50);

    // Composite each screen's grab into one snapshot of the virtual desktop at
    // the primary ratio (drawPixmap honours each grab's own ratio, so mixed-DPI
    // setups still line up in logical coords).
    QPixmap shot(m_virtualGeom.size() * m_dpr);
    shot.setDevicePixelRatio(m_dpr);
    shot.fill(Qt::black);
    {
        QPainter painter(&shot);
        for (QScreen *s : QGuiApplication::screens()) {
            QPixmap grab = s->grabWindow(0);
            const QRect g = s->geometry();
            // On X11, grabWindow(0) returns the ENTIRE root window — the whole
            // virtual desktop — no matter which screen it was called on. With
            // more than one monitor, placing that full-desktop image at this
            // screen's offset shifted the composite and the final crop showed
            // a different area of the screen. When the grab is clearly bigger
            // than this screen, cut this screen's own slice out of it.
            const QSize native(qRound(g.width() * s->devicePixelRatio()),
                               qRound(g.height() * s->devicePixelRatio()));
            if (grab.width() > native.width() + 2 ||
                grab.height() > native.height() + 2) {
                const qreal rx = qreal(grab.width()) / m_virtualGeom.width();
                const qreal ry = qreal(grab.height()) / m_virtualGeom.height();
                const QRect slice(qRound((g.x() - m_virtualGeom.x()) * rx),
                                  qRound((g.y() - m_virtualGeom.y()) * ry),
                                  qRound(g.width() * rx),
                                  qRound(g.height() * ry));
                const QRect bounded = slice.intersected(grab.rect());
                if (!bounded.isEmpty())
                    grab = grab.copy(bounded);
            }
            // grabWindow() isn't guaranteed to tag the pixmap with the screen's
            // own ratio (some platform plugins hand back the raw buffer at
            // ratio 1 even on a HiDPI screen). drawPixmap below places it by
            // its *logical* size, so an untagged HiDPI grab would be placed at
            // its full physical size — several times too large — and every
            // screenshot on that screen would come out wildly mis-cropped.
            // Force the tag explicitly so placement is always correct.
            grab.setDevicePixelRatio(s->devicePixelRatio());
            painter.drawPixmap(QPointF(g.x() - m_virtualGeom.x(),
                                       g.y() - m_virtualGeom.y()),
                               grab);
        }
    }
    return shot.toImage();
}

QImage ScreenCaptureOverlay::grabViaScreens(const QRect &sel)
{
    return cropDesktop(compositeScreens(), sel);
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

void ScreenCaptureOverlay::finish(const QImage &image)
{
    if (m_done)
        return;
    m_done = true;
    popOverrideCursor();
    // Close all panels before emitting, so they're gone before callers react.
    const auto panels = m_panels;
    m_panels.clear();
    for (auto *p : panels)
        p->close(); // WA_DeleteOnClose frees each panel
    if (image.isNull())
        emit cancelled();
    else
        emit captured(image);
    deleteLater(); // free the coordinator after the current call stack unwinds
}
