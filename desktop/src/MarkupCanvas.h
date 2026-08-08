#pragma once

#include <QColor>
#include <QImage>
#include <QPoint>
#include <QRect>
#include <QSize>
#include <QString>
#include <QVector>
#include <QWidget>

class QPainter;

enum class MarkupTool { Pencil, Rect, Ellipse, Line, Arrow, Text, Move };

struct MarkupOp {
    enum class Kind { Stroke, Shape, Text };
    Kind kind = Kind::Stroke;
    QColor color;
    QVector<QPoint> points; // used when kind == Stroke
    MarkupTool shapeType = MarkupTool::Rect; // used when kind == Shape
    QRect rect; // used when shapeType is Rect or Ellipse
    QPoint p1, p2; // used when shapeType is Line or Arrow (p2 is the arrow's head)
    QPoint textPos; // used when kind == Text (baseline origin)
    QString text; // used when kind == Text
    int textPointSize = 18; // used when kind == Text
};

class MarkupCanvas : public QWidget
{
    Q_OBJECT
public:
    explicit MarkupCanvas(const QImage &base, QWidget *parent = nullptr);

    void setTool(MarkupTool tool);
    void setColor(const QColor &color);
    void setTextPointSize(int pointSize);
    void undo();

    QImage flattenedImage() const;

    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;

private:
    static void renderOp(QPainter &painter, const MarkupOp &op);
    static void renderOps(QPainter &painter, const QVector<MarkupOp> &ops);
    static QRect textBounds(const MarkupOp &op);
    int textAt(const QPoint &position) const;
    void pushUndoState();

    QImage m_base;
    QVector<MarkupOp> m_ops;
    QVector<QVector<MarkupOp>> m_undoStack;
    MarkupOp m_current;
    bool m_drawing = false;
    QPoint m_dragOrigin;
    MarkupTool m_tool = MarkupTool::Pencil;
    QColor m_color;
    int m_textPointSize = 18;
    int m_movingTextIndex = -1;
    int m_selectedTextIndex = -1;
    QPoint m_textDragOffset;
    bool m_moveChanged = false;
};
