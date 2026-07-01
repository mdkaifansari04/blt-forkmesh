#include "ScreenAlignmentTarget.h"

#include <QColor>
#include <QFont>
#include <QPainter>
#include <QPaintEvent>
#include <QPen>
#include <QPoint>
#include <QRect>
#include <QString>

namespace {

const QColor kBackground(13, 17, 23);   // #0d1117 solid backdrop
const QColor kGrid(255, 255, 255, 20);
const QColor kAccent(47, 129, 247);     // #2f81f7 (matches the marquee blue)
const QColor kInk(230, 237, 243);       // near-white text/markers

constexpr int kSide = 240;      // fixed square side, in logical px
constexpr int kGridStep = 20;   // px between grid lines
constexpr int kCornerArm = 34;  // length of each corner bracket arm

} // namespace

ScreenAlignmentTarget::ScreenAlignmentTarget(QWidget *parent) : QWidget(parent)
{
    // A fixed, known size so a grab of it can be checked against the label.
    setFixedSize(kSide, kSide);
}

QSize ScreenAlignmentTarget::sizeHint() const
{
    return QSize(kSide, kSide);
}

void ScreenAlignmentTarget::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    const QRect full(0, 0, width(), height());
    // Inset by a pixel so the corner strokes aren't clipped at the very edge.
    const QRect in = full.adjusted(1, 1, -2, -2);

    // --- Dark backdrop. ---
    painter.setPen(Qt::NoPen);
    painter.setBrush(kBackground);
    painter.drawRoundedRect(full, 8, 8);

    // --- Light grid so scaling/offset is easy to eyeball. ---
    painter.setPen(kGrid);
    for (int x = in.left() + kGridStep; x < in.right(); x += kGridStep)
        painter.drawLine(x, in.top(), x, in.bottom());
    for (int y = in.top() + kGridStep; y < in.bottom(); y += kGridStep)
        painter.drawLine(in.left(), y, in.right(), y);

    // --- Four corner L-brackets: the "square with corners". ---
    QPen bracket(kAccent);
    bracket.setWidth(3);
    bracket.setCapStyle(Qt::FlatCap);
    painter.setPen(bracket);
    const int a = kCornerArm;
    const auto corner = [&](const QPoint &p, int dx, int dy) {
        painter.drawLine(p, p + QPoint(dx * a, 0));
        painter.drawLine(p, p + QPoint(0, dy * a));
    };
    corner(in.topLeft(), 1, 1);
    corner(in.topRight(), -1, 1);
    corner(in.bottomLeft(), 1, -1);
    corner(in.bottomRight(), -1, -1);

    // --- Centre crosshair with a small ring. ---
    const QPoint c = in.center();
    painter.setPen(QPen(kAccent, 1, Qt::DashLine));
    painter.drawLine(in.left(), c.y(), in.right(), c.y());
    painter.drawLine(c.x(), in.top(), c.x(), in.bottom());
    painter.setPen(QPen(kAccent, 2));
    painter.setBrush(Qt::NoBrush);
    painter.drawEllipse(c, 18, 18);

    // --- Size label so a grab can be checked against a known pixel size. Use
    // fromUtf8 for the multiplication sign (QStringLiteral mangles byte escapes).
    QFont mono = painter.font();
    mono.setFamily(QStringLiteral("monospace"));
    mono.setPointSize(9);
    painter.setFont(mono);
    painter.setPen(kInk);
    const QString label =
        QString::fromUtf8("%1 \xC3\x97 %2 px").arg(width()).arg(height());
    painter.drawText(QRect(in.left(), in.top() + 8, in.width(), 20),
                     Qt::AlignHCenter | Qt::AlignVCenter, label);
}
