#include "ScreenAlignmentOverlay.h"

#include <QColor>
#include <QFont>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QList>
#include <QMouseEvent>
#include <QPainter>
#include <QPaintEvent>
#include <QPen>
#include <QPoint>
#include <QScreen>
#include <QString>

namespace {

const QColor kBackground(13, 17, 23);   // #0d1117 solid backdrop
const QColor kGridMinor(255, 255, 255, 16);
const QColor kGridMajor(255, 255, 255, 40);
const QColor kAccent(47, 129, 247);     // #2f81f7 (matches the marquee blue)
const QColor kInk(230, 237, 243);       // near-white text/markers

constexpr int kGridStep = 50;   // px between grid lines
constexpr int kTickStep = 100;  // px between labelled ruler ticks
constexpr int kCornerArm = 60;  // length of each corner bracket arm

} // namespace

ScreenAlignmentOverlay *ScreenAlignmentOverlay::begin()
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

    auto *overlay = new ScreenAlignmentOverlay(virtualGeom);
    overlay->show();
    overlay->raise();
    overlay->activateWindow();
    overlay->setFocus();
    return overlay;
}

ScreenAlignmentOverlay::ScreenAlignmentOverlay(const QRect &virtualGeom)
    : QWidget(nullptr), m_virtualGeom(virtualGeom)
{
    // Frameless, always-on-top, and bypassing the window manager so nothing
    // repositions the target away from full virtual-desktop coverage.
    setWindowFlags(Qt::Window | Qt::FramelessWindowHint |
                   Qt::WindowStaysOnTopHint | Qt::BypassWindowManagerHint);
    setAttribute(Qt::WA_DeleteOnClose);
    setCursor(Qt::CrossCursor);
    setGeometry(virtualGeom);
}

// Small ring-and-plus marker used for the quarter alignment points.
void ScreenAlignmentOverlay::paintMarker(QPainter &painter, const QPoint &c,
                                         int radius)
{
    painter.setBrush(Qt::NoBrush);
    QPen pen(kAccent);
    pen.setWidth(2);
    painter.setPen(pen);
    painter.drawEllipse(c, radius, radius);
    painter.setPen(QPen(kInk, 1));
    painter.drawLine(c.x() - radius, c.y(), c.x() + radius, c.y());
    painter.drawLine(c.x(), c.y() - radius, c.x(), c.y() + radius);
}

// Paint the full calibration target for one screen. `local` is that screen's
// rect in widget-local coordinates; a point's virtual-desktop coordinate (what
// the screenshot tool grabs at) is `localPoint + m_virtualGeom.topLeft()`.
void ScreenAlignmentOverlay::paintScreen(QPainter &painter, const QRect &local)
{
    const QPoint gorigin = m_virtualGeom.topLeft();
    const auto label = [&](const QPoint &lp) {
        const QPoint g = lp + gorigin;
        return QStringLiteral("%1, %2").arg(g.x()).arg(g.y());
    };

    painter.fillRect(local, kBackground);

    // --- 50px grid, brighter every 100px, aligned to virtual-desktop coords ---
    for (int gx = ((local.left() + gorigin.x() + kGridStep - 1) / kGridStep) * kGridStep;
         gx <= local.right() + gorigin.x(); gx += kGridStep) {
        const int x = gx - gorigin.x();
        painter.setPen(gx % kTickStep == 0 ? kGridMajor : kGridMinor);
        painter.drawLine(x, local.top(), x, local.bottom());
    }
    for (int gy = ((local.top() + gorigin.y() + kGridStep - 1) / kGridStep) * kGridStep;
         gy <= local.bottom() + gorigin.y(); gy += kGridStep) {
        const int y = gy - gorigin.y();
        painter.setPen(gy % kTickStep == 0 ? kGridMajor : kGridMinor);
        painter.drawLine(local.left(), y, local.right(), y);
    }

    QFont mono = painter.font();
    mono.setFamily(QStringLiteral("monospace"));
    mono.setPointSize(9);
    painter.setFont(mono);

    // --- Ruler ticks: labelled every 100px down the top and left edges. ---
    painter.setPen(QPen(kInk, 1));
    for (int gx = ((local.left() + gorigin.x() + kTickStep - 1) / kTickStep) * kTickStep;
         gx <= local.right() + gorigin.x(); gx += kTickStep) {
        const int x = gx - gorigin.x();
        painter.drawLine(x, local.top(), x, local.top() + 12);
        painter.drawText(x + 3, local.top() + 22, QString::number(gx));
    }
    for (int gy = ((local.top() + gorigin.y() + kTickStep - 1) / kTickStep) * kTickStep;
         gy <= local.bottom() + gorigin.y(); gy += kTickStep) {
        const int y = gy - gorigin.y();
        painter.drawLine(local.left(), y, local.left() + 12, y);
        painter.drawText(local.left() + 4, y - 3, QString::number(gy));
    }

    // --- Four corner L-brackets, each labelled with its exact coordinate. ---
    QPen bracket(kAccent);
    bracket.setWidth(3);
    bracket.setCapStyle(Qt::FlatCap);
    painter.setPen(bracket);
    const int a = kCornerArm;
    const auto corner = [&](const QPoint &p, int dx, int dy) {
        painter.drawLine(p, p + QPoint(dx * a, 0));
        painter.drawLine(p, p + QPoint(0, dy * a));
    };
    // Inset by a couple of pixels so the strokes aren't clipped at the very edge.
    const QRect in = local.adjusted(1, 1, -2, -2);
    corner(in.topLeft(), 1, 1);
    corner(in.topRight(), -1, 1);
    corner(in.bottomLeft(), 1, -1);
    corner(in.bottomRight(), -1, -1);

    painter.setPen(QPen(kInk, 1));
    painter.drawText(in.left() + a + 6, in.top() + 16, label(local.topLeft()));
    painter.drawText(in.right() - a - 90, in.top() + 16, label(local.topRight()));
    painter.drawText(in.left() + a + 6, in.bottom() - 6, label(local.bottomLeft()));
    painter.drawText(in.right() - a - 90, in.bottom() - 6, label(local.bottomRight()));

    // --- Centre crosshair with concentric alignment rings. ---
    const QPoint centre = local.center();
    painter.setPen(QPen(kAccent, 1, Qt::DashLine));
    painter.drawLine(local.left(), centre.y(), local.right(), centre.y());
    painter.drawLine(centre.x(), local.top(), centre.x(), local.bottom());
    painter.setPen(QPen(kAccent, 2));
    painter.setBrush(Qt::NoBrush);
    for (int r : {20, 40, 80})
        painter.drawEllipse(centre, r, r);
    painter.setPen(QPen(kInk, 1));
    painter.drawText(centre.x() + 8, centre.y() - 8, label(centre));

    // --- Quarter-point alignment markers. ---
    for (double fx : {0.25, 0.75})
        for (double fy : {0.25, 0.75}) {
            const QPoint m(local.left() + int(local.width() * fx),
                           local.top() + int(local.height() * fy));
            paintMarker(painter, m, 16);
        }

    // --- Instructions plaque under the centre crosshair. ---
    const QString help =
        QStringLiteral("Screenshot alignment target\n"
                       "Grab any part of this with the screenshot tool and check "
                       "the captured pixels line up with the coordinates shown.\n"
                       "Press Esc or click to close.");
    QFont hf = painter.font();
    hf.setPointSize(11);
    painter.setFont(hf);
    const QRect textRect(centre.x() - 240, centre.y() + 96, 480, 90);
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(0, 0, 0, 170));
    painter.drawRoundedRect(textRect.adjusted(-10, -8, 10, 8), 8, 8);
    painter.setPen(kInk);
    painter.drawText(textRect, Qt::AlignHCenter | Qt::AlignTop | Qt::TextWordWrap,
                     help);
}

void ScreenAlignmentOverlay::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.fillRect(rect(), kBackground); // paints any gaps between screens too

    // Draw a self-contained target on each physical screen so corners, rulers and
    // the crosshair are correct per-monitor rather than only for the union rect.
    for (QScreen *s : QGuiApplication::screens()) {
        const QRect localScreen = s->geometry().translated(-m_virtualGeom.topLeft());
        paintScreen(painter, localScreen);
    }
}

void ScreenAlignmentOverlay::mousePressEvent(QMouseEvent *)
{
    finish();
}

void ScreenAlignmentOverlay::keyPressEvent(QKeyEvent *)
{
    finish(); // any key dismisses (Esc included)
}

void ScreenAlignmentOverlay::finish()
{
    if (m_done)
        return;
    m_done = true;
    emit dismissed();
    close(); // WA_DeleteOnClose frees the overlay
}
