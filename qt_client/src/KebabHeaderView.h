#pragma once









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

    QRect kebabRect(const QRect &sectionRect) const;
    void showColumnMenu(int logicalIndex, const QPoint &globalPos);

    int m_hoverSection = -1;
};





void installColumnHeaderMenu(QTableWidget *table);

}
