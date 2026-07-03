#include "MarkupCanvas.h"

#include <QMouseEvent>
#include <QPaintEvent>
#include <QPainter>
#include <QPen>

MarkupCanvas::MarkupCanvas(const QImage &base, QWidget *parent)
    : QWidget(parent), m_base(base), m_color(QColor(255, 50, 50))
{
    setMouseTracking(true);
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    setCursor(Qt::CrossCursor);
}

void MarkupCanvas::setTool(MarkupTool tool)
{
    m_tool = tool;
}

void MarkupCanvas::setColor(const QColor &color)
{
    m_color = color;
}

void MarkupCanvas::undo()
{
    if (!m_ops.isEmpty()) {
        m_ops.removeLast();
        update();
    }
}

QImage MarkupCanvas::flattenedImage() const
{
    QImage result = m_base.convertToFormat(QImage::Format_ARGB32);
    QPainter p(&result);
    renderOps(p, m_ops);
    return result;
}

QSize MarkupCanvas::sizeHint() const
{
    return m_base.size();
}

void MarkupCanvas::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.drawImage(0, 0, m_base);
    renderOps(painter, m_ops);
    if (m_drawing)
        renderOp(painter, m_current);
}

void MarkupCanvas::mousePressEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton)
        return;
    m_drawing = true;
    m_current = MarkupOp{};
    m_current.color = m_color;
    if (m_tool == MarkupTool::Pencil) {
        m_current.kind = MarkupOp::Kind::Stroke;
        m_current.points.append(event->position().toPoint());
    } else {
        m_current.kind = MarkupOp::Kind::Shape;
        m_current.shapeType = m_tool;
        m_dragOrigin = event->position().toPoint();
        m_current.rect = QRect(m_dragOrigin, m_dragOrigin);
    }
    update();
}

void MarkupCanvas::mouseMoveEvent(QMouseEvent *event)
{
    if (!m_drawing)
        return;
    const QPoint pos = event->position().toPoint();
    if (m_current.kind == MarkupOp::Kind::Stroke)
        m_current.points.append(pos);
    else
        m_current.rect = QRect(m_dragOrigin, pos).normalized();
    update();
}

void MarkupCanvas::mouseReleaseEvent(QMouseEvent *event)
{
    if (!m_drawing || event->button() != Qt::LeftButton)
        return;
    m_drawing = false;
    const QPoint pos = event->position().toPoint();
    if (m_current.kind == MarkupOp::Kind::Stroke) {
        m_current.points.append(pos);
        if (!m_current.points.isEmpty())
            m_ops.append(m_current);
    } else {
        m_current.rect = QRect(m_dragOrigin, pos).normalized();
        if (m_current.rect.width() >= 3 && m_current.rect.height() >= 3)
            m_ops.append(m_current);
    }
    m_current = MarkupOp{};
    update();
}

void MarkupCanvas::renderOp(QPainter &painter, const MarkupOp &op)
{
    painter.save();
    QPen pen(op.color);
    pen.setWidth(3);
    pen.setCapStyle(Qt::RoundCap);
    pen.setJoinStyle(Qt::RoundJoin);
    painter.setPen(pen);
    painter.setBrush(Qt::NoBrush);
    painter.setRenderHint(QPainter::Antialiasing, true);

    if (op.kind == MarkupOp::Kind::Stroke) {
        if (op.points.size() == 1)
            painter.drawPoint(op.points.first());
        else if (op.points.size() > 1)
            painter.drawPolyline(op.points.constData(), op.points.size());
    } else if (op.shapeType == MarkupTool::Rect) {
        painter.drawRect(op.rect);
    } else {
        painter.drawEllipse(op.rect);
    }
    painter.restore();
}

void MarkupCanvas::renderOps(QPainter &painter, const QVector<MarkupOp> &ops)
{
    for (const MarkupOp &op : ops)
        renderOp(painter, op);
}
