#pragma once

#include <QColor>
#include <QImage>
#include <QPoint>
#include <QRect>
#include <QSize>
#include <QVector>
#include <QWidget>

class QPainter;

// The kind of annotation the user is drawing on a captured screenshot.
enum class MarkupTool { Pencil, Rect, Ellipse, Line, Arrow };

// A single annotation: either a freehand stroke (a list of points) or a shape
// outline (a rectangle/ellipse bounded by rect, or a directional line/arrow
// running from p1 to p2).
struct MarkupOp {
    enum class Kind { Stroke, Shape };
    Kind kind = Kind::Stroke;
    QColor color;
    QVector<QPoint> points; // used when kind == Stroke
    MarkupTool shapeType = MarkupTool::Rect; // used when kind == Shape
    QRect rect; // used when shapeType is Rect or Ellipse
    QPoint p1, p2; // used when shapeType is Line or Arrow (p2 is the arrow's head)
};

// The drawable viewport over the screenshot: renders the base image and lets the
// user scribble freehand ink or drop shape outlines on top, then flattens the
// markup back onto the image.
class MarkupCanvas : public QWidget
{
    Q_OBJECT
public:
    explicit MarkupCanvas(const QImage &base, QWidget *parent = nullptr);

    void setTool(MarkupTool tool);
    void setColor(const QColor &color);
    void undo();

    // Composite all markup onto the base image and return the flattened result.
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

    QImage m_base;
    QVector<MarkupOp> m_ops;
    MarkupOp m_current;
    bool m_drawing = false;
    QPoint m_dragOrigin;
    MarkupTool m_tool = MarkupTool::Pencil;
    QColor m_color;
};
