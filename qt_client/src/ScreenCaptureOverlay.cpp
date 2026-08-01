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



bool runningOnWayland()
{
    if (QGuiApplication::platformName().startsWith(QLatin1String("wayland"),
                                                   Qt::CaseInsensitive))
        return true;



    return qgetenv("XDG_SESSION_TYPE").compare("wayland", Qt::CaseInsensitive) == 0;
}








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





QCursor makeSnipCursor()
{
    constexpr int n = 32;
    constexpr int c = n / 2;
    constexpr int gap = 3;
    constexpr int arm = c - 1;

    QPixmap pm(n, n);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, false);
    const auto drawCross = [&](const QColor &col, int w) {
        QPen pen(col);
        pen.setWidth(w);
        p.setPen(pen);
        p.drawLine(c - arm, c, c - gap, c);
        p.drawLine(c + gap, c, c + arm, c);
        p.drawLine(c, c - arm, c, c - gap);
        p.drawLine(c, c + gap, c, c + arm);
    };
    drawCross(QColor(0, 0, 0, 200), 3);
    drawCross(QColor(255, 255, 255), 1);
    p.end();
    return QCursor(pm, c, c);
}

}





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



            painter.fillRect(rect(), QColor(0, 0, 0, 1));
        }



        const QColor shade(0, 0, 0, 90);
        if (!m_owner->m_dragging) {
            painter.fillRect(rect(), shade);
            return;
        }




        const QPoint origin  = m_owner->m_originGlobal  - m_screenGeom.topLeft();
        const QPoint current = m_owner->m_currentGlobal - m_screenGeom.topLeft();
        const QRect sel = QRect(origin, current).normalized();
        if (sel.isNull()) {
            painter.fillRect(rect(), shade);
            return;
        }
        for (const QRect &r : QRegion(rect()) - QRegion(sel))
            painter.fillRect(r, shade);
        QPen pen(QColor("#2f81f7"));
        pen.setWidth(2);
        painter.setPen(pen);
        painter.setBrush(Qt::NoBrush);
        painter.drawRect(sel.adjusted(0, 0, -1, -1));


        if (m_screenGeom.contains(m_owner->m_currentGlobal)) {
            const QRect globalSel =
                QRect(m_owner->m_originGlobal, m_owner->m_currentGlobal).normalized();
            const QString label = QStringLiteral("%1 × %2")
                                      .arg(globalSel.width())
                                      .arg(globalSel.height());
            const QFontMetrics fm = painter.fontMetrics();
            QRect box = fm.boundingRect(label).adjusted(-6, -3, 6, 3);
            box.moveTopLeft(sel.topLeft() + QPoint(0, -box.height() - 6));
            if (box.top() < 0)
                box.moveTopLeft(sel.topLeft() + QPoint(6, 6));
            painter.setPen(Qt::NoPen);
            painter.setBrush(QColor(0, 0, 0, 180));
            painter.drawRoundedRect(box, 4, 4);
            painter.setPen(Qt::white);
            painter.drawText(box, Qt::AlignCenter, label);
        }
    }









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
    QRect m_screenGeom;
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
    : QObject(nullptr), m_virtualGeom(virtualGeom), m_dpr(dpr), m_screens(screens)
{






    if (!runningOnWayland()) {
        const QImage shot = compositeScreens();
        if (!looksLikeFailedGrab(shot))
            m_frozen = shot;
    }
#ifdef FORKMESH_HAVE_PORTAL
    else {





        m_awaitingFreezeGrab = true;
        grabViaPortal(QRect());
        return;
    }
#endif

    createPanels();
}

void ScreenCaptureOverlay::createPanels()
{
    const QCursor snip = makeSnipCursor();


    QGuiApplication::setOverrideCursor(snip);
    m_cursorPushed = true;

    for (QScreen *s : m_screens) {
        auto *panel = new PerScreenPanel(this, s->geometry(), snip);
        m_panels.append(panel);






        panel->setScreen(s);
        if (runningOnWayland())
            panel->showFullScreen();
        else
            panel->show();
        panel->raise();
    }


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
        finish(QImage());
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

    for (auto *p : m_panels)
        p->update();
}

void ScreenCaptureOverlay::onRelease(Qt::MouseButton button, QPoint globalPos)
{
    if (!m_dragging || button != Qt::LeftButton)
        return;
    m_dragging = false;
    m_currentGlobal = globalPos;

    const QPoint a = m_originGlobal  - m_virtualGeom.topLeft();
    const QPoint b = m_currentGlobal - m_virtualGeom.topLeft();
    const QRect sel = QRect(a, b).normalized();
    if (sel.width() < 3 || sel.height() < 3) {
        finish(QImage());
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



    if (!m_frozen.isNull()) {
        finish(cropDesktop(m_frozen, sel));
        return;
    }



    popOverrideCursor();
    for (auto *p : m_panels)
        p->hide();

#ifdef FORKMESH_HAVE_PORTAL
    if (runningOnWayland()) {


        m_pendingSel = sel;
        QTimer::singleShot(150, this, [this] { grabViaPortal(m_pendingSel); });
        return;
    }






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

    QGuiApplication::processEvents(QEventLoop::ExcludeUserInputEvents, 50);




    QPixmap shot(m_virtualGeom.size() * m_dpr);
    shot.setDevicePixelRatio(m_dpr);
    shot.fill(Qt::black);
    {
        QPainter painter(&shot);
        for (QScreen *s : QGuiApplication::screens()) {
            QPixmap grab = s->grabWindow(0);
            const QRect g = s->geometry();






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




    const QString token =
        QStringLiteral("forkmesh_%1").arg(QRandomGenerator::global()->generate());
    QString sender = bus.baseService();
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
    opts.insert(QStringLiteral("interactive"), false);
    QDBusMessage call = QDBusMessage::createMethodCall(
        QStringLiteral("org.freedesktop.portal.Desktop"),
        QStringLiteral("/org/freedesktop/portal/desktop"),
        QStringLiteral("org.freedesktop.portal.Screenshot"),
        QStringLiteral("Screenshot"));
    call << QString() << opts;
    bus.asyncCall(call);





    const bool freezePhase = m_awaitingFreezeGrab;
    QTimer::singleShot(8000, this, [this, freezePhase] {
        if (m_done)
            return;
        if (freezePhase) {
            if (m_awaitingFreezeGrab) {
                m_awaitingFreezeGrab = false;
                createPanels();
            }
            return;
        }
        finish(QImage());
    });
#else
    finish(grabViaScreens(sel));
#endif
}

void ScreenCaptureOverlay::onPortalResponse(uint response, const QVariantMap &results)
{
    QImage full;
    if (response == 0) {
        const QString path =
            QUrl(results.value(QStringLiteral("uri")).toString()).toLocalFile();
        full = QImage(path);
        if (!path.isEmpty())
            QFile::remove(path);
    }





    if (m_awaitingFreezeGrab) {
        m_awaitingFreezeGrab = false;
        if (response == 1) {
            finish(QImage());
            return;
        }
        if (!looksLikeFailedGrab(full))
            m_frozen = full;
        createPanels();
        return;
    }

    if (response != 0) {
        finish(QImage());
        return;
    }
    finish(cropDesktop(full, m_pendingSel));
}

void ScreenCaptureOverlay::finish(const QImage &image)
{
    if (m_done)
        return;
    m_done = true;
    popOverrideCursor();

    const auto panels = m_panels;
    m_panels.clear();
    for (auto *p : panels)
        p->close();
    if (image.isNull())
        emit cancelled();
    else
        emit captured(image);
    deleteLater();
}
