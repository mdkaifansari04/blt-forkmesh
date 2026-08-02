#include "KebabHeaderView.h"

#include <QAction>
#include <QEvent>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QTableWidget>
#include <QTimer>

namespace forkmesh::ui {

namespace {
// Width of the right-edge zone reserved for the ellipsis button. Wide enough to
// be an easy click target and to keep the dots clear of the header label, which
// gets this much extra room via sectionSizeFromContents().
constexpr int kKebabWidth = 20;
constexpr int kDotRadius = 1;  // 2px dots
constexpr int kDotGap = 4;     // centre-to-centre spacing
} // namespace

KebabHeaderView::KebabHeaderView(Qt::Orientation orientation, QWidget *parent)
    : QHeaderView(orientation, parent)
{
    setMouseTracking(true);
    setSectionsClickable(true);
}

QRect KebabHeaderView::kebabRect(const QRect &sectionRect) const
{
    return QRect(sectionRect.right() - kKebabWidth + 1, sectionRect.top(),
                 kKebabWidth, sectionRect.height());
}

QSize KebabHeaderView::sectionSizeFromContents(int logicalIndex) const
{
    // Reserve room for the dots so a content-fitted column never draws its
    // label underneath the ellipsis.
    QSize size = QHeaderView::sectionSizeFromContents(logicalIndex);
    size.setWidth(size.width() + kKebabWidth);
    return size;
}

void KebabHeaderView::paintSection(QPainter *painter, const QRect &rect,
                                   int logicalIndex) const
{
    QHeaderView::paintSection(painter, rect, logicalIndex);
    if (!rect.isValid() || rect.width() <= kKebabWidth)
        return;

    const QRect zone = kebabRect(rect);
    const bool hovered = logicalIndex == m_hoverSection;
    // Muted to match the header label (#8b949e); brightens on hover (#c9d1d9)
    // so it reads as an interactive button under the cursor.
    const QColor color = hovered ? QColor(0xc9, 0xd1, 0xd9)
                                 : QColor(0x8b, 0x94, 0x9e);

    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);
    painter->setPen(Qt::NoPen);
    painter->setBrush(color);
    const int cx = zone.center().x();
    const int cy = zone.center().y();
    for (int i = -1; i <= 1; ++i)
        painter->drawEllipse(QPoint(cx + i * kDotGap, cy), kDotRadius, kDotRadius);
    painter->restore();
}

void KebabHeaderView::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        const QPoint pos = event->position().toPoint();
        const int logical = logicalIndexAt(pos);
        if (logical >= 0) {
            const QRect sect(sectionViewportPosition(logical), 0,
                             sectionSize(logical), height());
            if (kebabRect(sect).contains(pos)) {
                // Pop the menu just under the header section; consume the press
                // so it doesn't also trigger a sort-by-header-click.
                showColumnMenu(logical, mapToGlobal(QPoint(sect.left(), height())));
                event->accept();
                return;
            }
        }
    }
    QHeaderView::mousePressEvent(event);
}

void KebabHeaderView::mouseMoveEvent(QMouseEvent *event)
{
    const int logical = logicalIndexAt(event->position().toPoint());
    if (logical != m_hoverSection) {
        m_hoverSection = logical;
        viewport()->update();
    }
    QHeaderView::mouseMoveEvent(event);
}

void KebabHeaderView::leaveEvent(QEvent *event)
{
    if (m_hoverSection != -1) {
        m_hoverSection = -1;
        viewport()->update();
    }
    QHeaderView::leaveEvent(event);
}

void KebabHeaderView::showColumnMenu(int logicalIndex, const QPoint &globalPos)
{
    auto *view = qobject_cast<QTableView *>(parentWidget());

    int visibleCount = 0;
    for (int i = 0; i < count(); ++i)
        if (!isSectionHidden(i))
            ++visibleCount;
    bool anyHidden = visibleCount < count();
    const int visual = visualIndex(logicalIndex);

    QMenu menu(this);

    QAction *asc = menu.addAction(tr("Sort ascending"));
    QAction *desc = menu.addAction(tr("Sort descending"));
    asc->setCheckable(true);
    desc->setCheckable(true);
    if (sortIndicatorSection() == logicalIndex) {
        // The built-in indicator arrow is hidden on these headers, so the tick
        // is the only cue for which way the column is currently sorted.
        if (sortIndicatorOrder() == Qt::AscendingOrder)
            asc->setChecked(true);
        else
            desc->setChecked(true);
    }

    menu.addSeparator();
    QAction *select = menu.addAction(tr("Select column"));
    QAction *hide = menu.addAction(tr("Hide field"));
    hide->setEnabled(visibleCount > 1); // never hide the last visible column
    QAction *showAll = anyHidden ? menu.addAction(tr("Show all fields")) : nullptr;

    menu.addSeparator();
    QAction *left = menu.addAction(tr("Move left"));
    QAction *right = menu.addAction(tr("Move right"));
    QAction *start = menu.addAction(tr("Move to start"));
    QAction *end = menu.addAction(tr("Move to end"));
    left->setEnabled(visual > 0);
    start->setEnabled(visual > 0);
    right->setEnabled(visual < count() - 1);
    end->setEnabled(visual < count() - 1);

    QAction *chosen = menu.exec(globalPos);
    if (!chosen)
        return;

    if (chosen == asc && view)
        view->sortByColumn(logicalIndex, Qt::AscendingOrder);
    else if (chosen == desc && view)
        view->sortByColumn(logicalIndex, Qt::DescendingOrder);
    else if (chosen == select && view)
        view->selectColumn(logicalIndex);
    else if (chosen == hide)
        setSectionHidden(logicalIndex, true);
    else if (chosen == showAll) {
        for (int i = 0; i < count(); ++i)
            setSectionHidden(i, false);
    } else if (chosen == left)
        moveSection(visual, visual - 1);
    else if (chosen == right)
        moveSection(visual, visual + 1);
    else if (chosen == start)
        moveSection(visual, 0);
    else if (chosen == end)
        moveSection(visual, count() - 1);
}

void installColumnHeaderMenu(QTableWidget *table)
{
    if (!table)
        return;
    auto *header = new KebabHeaderView(Qt::Horizontal, table);
    // Let the "Move …" actions (and user drags) reorder columns.
    header->setSectionsMovable(true);
    table->setHorizontalHeader(header);
    // Drop the built-in sort arrow -- the menu's checkmarks convey sort state
    // instead, and it would otherwise collide with the dots at the section's
    // right edge. Deferred because callers typically enable sorting *after* this
    // (QTableView::setSortingEnabled re-shows the indicator), so hiding it now
    // would be undone; the next event-loop turn lands after that.
    QTimer::singleShot(0, header, [header] { header->setSortIndicatorShown(false); });
}

} // namespace forkmesh::ui
