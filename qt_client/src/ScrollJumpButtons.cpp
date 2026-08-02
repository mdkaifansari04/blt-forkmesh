#include "ScrollJumpButtons.h"

#include <QAbstractScrollArea>
#include <QEvent>
#include <QGuiApplication>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QScrollBar>
#include <QStyleHints>

// How close (px) to an extreme counts as "already there", hiding that arrow.
static constexpr int kEdgeSlack = 4;

// A round jump button whose triangle is painted, not rendered from a font glyph.
// Relying on a ▲/▼ Unicode character left the circle blank on systems whose UI
// font lacks the Geometric Shapes block; drawing the triangle ourselves makes
// the arrow show everywhere. The circle background/border still comes from the
// stylesheet (object name "scrollJump"), so theming stays centralised.
class JumpArrowButton : public QPushButton
{
public:
    JumpArrowButton(bool up, QWidget *parent) : QPushButton(parent), m_up(up) {}

    void setArrowColor(const QColor &c)
    {
        if (m_arrow != c) {
            m_arrow = c;
            update();
        }
    }

protected:
    void paintEvent(QPaintEvent *e) override
    {
        QPushButton::paintEvent(e); // styled circle + border (no text)

        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        const QPointF c(width() / 2.0, height() / 2.0);
        const qreal hw = 5.0;  // half-width of the triangle
        const qreal hh = 3.5;  // half-height
        QPainterPath tri;
        if (m_up) {
            tri.moveTo(c.x(), c.y() - hh);
            tri.lineTo(c.x() - hw, c.y() + hh);
            tri.lineTo(c.x() + hw, c.y() + hh);
        } else {
            tri.moveTo(c.x() - hw, c.y() - hh);
            tri.lineTo(c.x() + hw, c.y() - hh);
            tri.lineTo(c.x(), c.y() + hh);
        }
        tri.closeSubpath();
        p.fillPath(tri, m_arrow);
    }

private:
    bool m_up;
    QColor m_arrow{0xe6, 0xed, 0xf3}; // sensible dark-theme default
};

ScrollJumpButtons::ScrollJumpButtons(QAbstractScrollArea *area)
    : QObject(area), m_area(area)
{
    auto mkBtn = [this](bool up, const QString &tip) {
        auto *b = new JumpArrowButton(up, m_area->viewport());
        b->setObjectName(QStringLiteral("scrollJump"));
        b->setCursor(Qt::PointingHandCursor);
        b->setToolTip(tip);
        b->setFixedSize(30, 30);
        b->hide();
        return b;
    };
    m_top = mkBtn(true, QStringLiteral("Jump to top"));
    m_bottom = mkBtn(false, QStringLiteral("Jump to bottom"));
    connect(m_top, &QPushButton::clicked, this, &ScrollJumpButtons::topClicked);
    connect(m_bottom, &QPushButton::clicked, this, &ScrollJumpButtons::bottomClicked);
    applyArrowColor();
    if (QStyleHints *h = QGuiApplication::styleHints())
        connect(h, &QStyleHints::colorSchemeChanged, this,
                [this](Qt::ColorScheme) { applyArrowColor(); });

    // Re-corner the buttons whenever the viewport resizes or first appears.
    m_area->viewport()->installEventFilter(this);

    QScrollBar *sb = m_area->verticalScrollBar();
    connect(sb, &QScrollBar::valueChanged, this, [this] { updateVisibility(); });
    connect(sb, &QScrollBar::rangeChanged, this, [this] { updateVisibility(); });
    updateVisibility();
}

void ScrollJumpButtons::setButtonStyle(const QString &css)
{
    m_top->setStyleSheet(css);
    m_bottom->setStyleSheet(css);
}

// Keep the painted arrows readable against the circle in either color scheme.
void ScrollJumpButtons::applyArrowColor()
{
    bool dark = true;
    if (QStyleHints *h = QGuiApplication::styleHints())
        dark = h->colorScheme() != Qt::ColorScheme::Light;
    const QColor c = dark ? QColor(0xe6, 0xed, 0xf3) : QColor(0x1f, 0x23, 0x28);
    m_top->setArrowColor(c);
    m_bottom->setArrowColor(c);
}

bool ScrollJumpButtons::eventFilter(QObject *obj, QEvent *e)
{
    if (obj == m_area->viewport() &&
        (e->type() == QEvent::Resize || e->type() == QEvent::Show))
        reposition();
    return QObject::eventFilter(obj, e);
}

void ScrollJumpButtons::reposition()
{
    QWidget *vp = m_area->viewport();
    const int m = 12, w = m_bottom->width(), h = m_bottom->height();
    const int x = vp->width() - w - m;
    m_bottom->move(x, vp->height() - h - m);
    m_top->move(x, vp->height() - 2 * h - m - 6);
    m_top->raise();
    m_bottom->raise();
}

void ScrollJumpButtons::updateVisibility()
{
    QScrollBar *sb = m_area->verticalScrollBar();
    const bool scrollable = sb->maximum() > sb->minimum();
    m_top->setVisible(scrollable && sb->value() > sb->minimum() + kEdgeSlack);
    m_bottom->setVisible(scrollable && sb->value() < sb->maximum() - kEdgeSlack);
    reposition();
}
