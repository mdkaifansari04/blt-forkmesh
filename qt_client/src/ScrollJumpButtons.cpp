#include "ScrollJumpButtons.h"

#include <QAbstractScrollArea>
#include <QEvent>
#include <QPushButton>
#include <QScrollBar>

// How close (px) to an extreme counts as "already there", hiding that arrow.
static constexpr int kEdgeSlack = 4;

ScrollJumpButtons::ScrollJumpButtons(QAbstractScrollArea *area)
    : QObject(area), m_area(area)
{
    auto mkBtn = [this](const QString &glyph, const QString &tip) {
        auto *b = new QPushButton(glyph, m_area->viewport());
        b->setObjectName(QStringLiteral("scrollJump"));
        b->setCursor(Qt::PointingHandCursor);
        b->setToolTip(tip);
        b->setFixedSize(30, 30);
        b->hide();
        return b;
    };
    m_top = mkBtn(QString::fromUtf8("\xE2\x96\xB2"), QStringLiteral("Jump to top"));
    m_bottom = mkBtn(QString::fromUtf8("\xE2\x96\xBC"), QStringLiteral("Jump to bottom"));
    connect(m_top, &QPushButton::clicked, this, &ScrollJumpButtons::topClicked);
    connect(m_bottom, &QPushButton::clicked, this, &ScrollJumpButtons::bottomClicked);

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
