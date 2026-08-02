#pragma once

// A horizontal QHeaderView that paints a small "three dots" (kebab) button on
// the right edge of every column header and, when clicked, pops up a per-column
// menu of actions -- Sort ascending/descending, Select column, Hide field, and
// Move left/right/to start/to end (adhoc #73). It mirrors the spreadsheet-style
// column menu people expect from tools like Airtable/Sheets, so any of the app's
// data tables can offer the same affordances by swapping in this header via the
// installColumnHeaderMenu() helper below.

#include <QHeaderView>
#include <QSize>

class QMouseEvent;
class QPainter;
class QRect;
class QTableWidget;

namespace forkmesh::ui {

class KebabHeaderView : public QHeaderView
{
    Q_OBJECT
public:
    explicit KebabHeaderView(Qt::Orientation orientation, QWidget *parent = nullptr);

protected:
    void paintSection(QPainter *painter, const QRect &rect,
                      int logicalIndex) const override;
    QSize sectionSizeFromContents(int logicalIndex) const override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void leaveEvent(QEvent *event) override;

private:
    // The clickable ellipsis hotspot at the right edge of `sectionRect`.
    QRect kebabRect(const QRect &sectionRect) const;
    void showColumnMenu(int logicalIndex, const QPoint &globalPos);

    int m_hoverSection = -1;
};

// Swaps `table`'s horizontal header for a KebabHeaderView and enables the
// section-move + sort machinery the column menu drives. Call right after the
// table is constructed (before per-section resize modes are set), so later
// table->horizontalHeader() calls see the new header.
void installColumnHeaderMenu(QTableWidget *table);

} // namespace forkmesh::ui
