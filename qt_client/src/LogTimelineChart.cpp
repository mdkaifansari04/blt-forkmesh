#include "LogTimelineChart.h"

#include <QDateTime>
#include <QLinearGradient>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPalette>
#include <QWheelEvent>

#include <algorithm>

namespace {
QString compactCount(int value)
{
    if (value >= 1000000)
        return QString::number(value / 1000000.0, 'f', 1) + QLatin1Char('m');
    if (value >= 1000)
        return QString::number(value / 1000.0, 'f', 1) + QLatin1Char('k');
    return QString::number(value);
}

QString axisTime(qint64 timestampMs, qint64 spanMs)
{
    const QDateTime value = QDateTime::fromMSecsSinceEpoch(timestampMs);
    if (spanMs <= 48LL * 60 * 60 * 1000)
        return value.toString(QStringLiteral("h:mm AP"));
    if (spanMs <= 10LL * 24 * 60 * 60 * 1000)
        return value.toString(QStringLiteral("ddd h AP"));
    return value.toString(QStringLiteral("MMM d"));
}
} // namespace

LogTimelineChart::LogTimelineChart(QWidget *parent) : QWidget(parent)
{
    setObjectName(QStringLiteral("logTimelineChart"));
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
    setAccessibleName(QStringLiteral("Log activity chart"));
    setAccessibleDescription(
        QStringLiteral("A histogram of logs over time. Drag across the chart or "
                       "use the mouse wheel to zoom; double-click to reset."));
}

void LogTimelineChart::setEntries(QVector<LogTimelineEntry> entries)
{
    std::sort(entries.begin(), entries.end(),
              [](const LogTimelineEntry &left, const LogTimelineEntry &right) {
                  return left.timestampMs < right.timestampMs;
              });
    m_entries = std::move(entries);
    update();
}

void LogTimelineChart::appendEntry(const LogTimelineEntry &entry)
{
    if (entry.timestampMs <= 0)
        return;
    const auto position = std::upper_bound(
        m_entries.begin(), m_entries.end(), entry.timestampMs,
        [](qint64 timestamp, const LogTimelineEntry &candidate) {
            return timestamp < candidate.timestampMs;
        });
    m_entries.insert(position, entry);
    update();
    notifyViewChanged();
}

void LogTimelineChart::setCategoryFilter(const QString &category,
                                         const QColor &accent)
{
    m_categoryFilter = category;
    m_accent = accent.isValid() ? accent : QColor(QStringLiteral("#58a6ff"));
    update();
    notifyViewChanged();
}

void LogTimelineChart::setRange(qint64 fromMs, qint64 toMs)
{
    if (toMs <= fromMs)
        toMs = fromMs + 60 * 1000;
    m_rangeFromMs = fromMs;
    m_rangeToMs = toMs;
    m_viewFromMs = fromMs;
    m_viewToMs = toMs;
    m_dragging = false;
    update();
    notifyViewChanged();
}

void LogTimelineChart::resetZoom()
{
    if (!isZoomed())
        return;
    m_viewFromMs = m_rangeFromMs;
    m_viewToMs = m_rangeToMs;
    update();
    notifyViewChanged();
}

int LogTimelineChart::visibleEntryCount() const
{
    return int(std::count_if(m_entries.cbegin(), m_entries.cend(),
                             [this](const LogTimelineEntry &entry) {
                                 return entryIsVisible(entry);
                             }));
}

bool LogTimelineChart::isZoomed() const
{
    return m_viewFromMs != m_rangeFromMs || m_viewToMs != m_rangeToMs;
}

QSize LogTimelineChart::sizeHint() const
{
    return QSize(900, 330);
}

QSize LogTimelineChart::minimumSizeHint() const
{
    return QSize(320, 220);
}

QRectF LogTimelineChart::plotRect() const
{
    return QRectF(rect()).adjusted(56.0, 18.0, -18.0, -42.0);
}

qint64 LogTimelineChart::timeAtX(qreal x) const
{
    const QRectF plot = plotRect();
    const qreal ratio = qBound(0.0, (x - plot.left()) / plot.width(), 1.0);
    return m_viewFromMs + qRound64(ratio * (m_viewToMs - m_viewFromMs));
}

bool LogTimelineChart::entryIsVisible(const LogTimelineEntry &entry) const
{
    return entry.timestampMs >= m_viewFromMs && entry.timestampMs <= m_viewToMs &&
           (m_categoryFilter.isEmpty() || entry.category == m_categoryFilter);
}

void LogTimelineChart::notifyViewChanged()
{
    if (viewChanged)
        viewChanged();
}

void LogTimelineChart::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    const QPalette colors = palette();
    const QColor canvas = colors.color(QPalette::Base);
    const QColor text = colors.color(QPalette::Text);
    const QColor muted = colors.color(QPalette::PlaceholderText);
    QColor border = colors.color(QPalette::Mid);
    border.setAlpha(150);
    QColor grid = border;
    grid.setAlpha(80);

    painter.setPen(QPen(border, 1));
    painter.setBrush(canvas);
    painter.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5),
                            8.0, 8.0);

    const QRectF plot = plotRect();
    if (plot.width() <= 1 || plot.height() <= 1)
        return;

    const int bucketCount = qBound(16, int(plot.width() / 11.0), 180);
    QVector<int> buckets(bucketCount, 0);
    const qint64 span = qMax<qint64>(1, m_viewToMs - m_viewFromMs);
    for (const LogTimelineEntry &entry : std::as_const(m_entries)) {
        if (!entryIsVisible(entry))
            continue;
        const int bucket = qBound(
            0, int((entry.timestampMs - m_viewFromMs) * bucketCount / span),
            bucketCount - 1);
        ++buckets[bucket];
    }
    const int maximum = qMax(1, *std::max_element(buckets.cbegin(), buckets.cend()));

    QFont axisFont = font();
    axisFont.setPointSizeF(qMax(8.0, axisFont.pointSizeF() - 1.0));
    painter.setFont(axisFont);
    const QFontMetrics metrics(axisFont);

    constexpr int kHorizontalGridLines = 4;
    painter.setPen(QPen(grid, 1));
    for (int line = 0; line <= kHorizontalGridLines; ++line) {
        const qreal y = plot.bottom() -
                        (qreal(line) / kHorizontalGridLines) * plot.height();
        painter.drawLine(QPointF(plot.left(), y), QPointF(plot.right(), y));
        const int count = qRound(qreal(line) / kHorizontalGridLines * maximum);
        painter.setPen(muted);
        painter.drawText(QRectF(4, y - metrics.height() / 2.0, 45,
                                metrics.height()),
                         Qt::AlignRight | Qt::AlignVCenter, compactCount(count));
        painter.setPen(QPen(grid, 1));
    }

    constexpr int kTimeTicks = 4;
    for (int tick = 0; tick <= kTimeTicks; ++tick) {
        const qreal ratio = qreal(tick) / kTimeTicks;
        const qreal x = plot.left() + ratio * plot.width();
        painter.setPen(QPen(grid, 1));
        painter.drawLine(QPointF(x, plot.top()), QPointF(x, plot.bottom()));
        const QString label = axisTime(m_viewFromMs + qRound64(ratio * span), span);
        QRectF labelRect(x - 70, plot.bottom() + 9, 140, metrics.height() + 2);
        Qt::Alignment alignment = Qt::AlignHCenter | Qt::AlignTop;
        if (tick == 0) {
            labelRect.moveLeft(plot.left());
            alignment = Qt::AlignLeft | Qt::AlignTop;
        } else if (tick == kTimeTicks) {
            labelRect.moveRight(plot.right());
            alignment = Qt::AlignRight | Qt::AlignTop;
        }
        painter.setPen(muted);
        painter.drawText(labelRect, alignment, label);
    }

    QPainterPath linePath;
    for (int index = 0; index < bucketCount; ++index) {
        const qreal x = plot.left() +
                        (index + 0.5) * plot.width() / bucketCount;
        const qreal y = plot.bottom() -
                        qreal(buckets.at(index)) / maximum * plot.height();
        if (index == 0)
            linePath.moveTo(x, y);
        else
            linePath.lineTo(x, y);
    }

    QPainterPath fillPath = linePath;
    fillPath.lineTo(plot.right(), plot.bottom());
    fillPath.lineTo(plot.left(), plot.bottom());
    fillPath.closeSubpath();
    QColor fillTop = m_accent;
    fillTop.setAlpha(105);
    QColor fillBottom = m_accent;
    fillBottom.setAlpha(12);
    QLinearGradient fill(plot.topLeft(), plot.bottomLeft());
    fill.setColorAt(0, fillTop);
    fill.setColorAt(1, fillBottom);
    painter.save();
    painter.setClipRect(plot);
    painter.fillPath(fillPath, fill);
    painter.setPen(QPen(m_accent, 2.0));
    painter.drawPath(linePath);
    painter.restore();

    const int total = visibleEntryCount();
    if (total == 0) {
        painter.setPen(muted);
        painter.drawText(plot, Qt::AlignCenter,
                         m_categoryFilter.isEmpty()
                             ? QStringLiteral("No logs in this timeframe")
                             : QStringLiteral("No %1 logs in this timeframe")
                                   .arg(m_categoryFilter));
    }

    if (m_pointerInside && !m_dragging && plot.contains(m_pointerPosition)) {
        const int bucket = qBound(
            0, int((m_pointerPosition.x() - plot.left()) / plot.width() *
                   bucketCount),
            bucketCount - 1);
        const qreal x = plot.left() +
                        (bucket + 0.5) * plot.width() / bucketCount;
        painter.setPen(QPen(m_accent, 1, Qt::DashLine));
        painter.drawLine(QPointF(x, plot.top()), QPointF(x, plot.bottom()));

        const qint64 bucketFrom =
            m_viewFromMs + bucket * span / bucketCount;
        const qint64 bucketTo =
            m_viewFromMs + (bucket + 1) * span / bucketCount;
        const QString tooltip =
            QStringLiteral("%1 log%2  ·  %3 – %4")
                .arg(buckets.at(bucket))
                .arg(buckets.at(bucket) == 1 ? QString() : QStringLiteral("s"))
                .arg(QDateTime::fromMSecsSinceEpoch(bucketFrom)
                         .toString(QStringLiteral("MMM d, h:mm AP")))
                .arg(QDateTime::fromMSecsSinceEpoch(bucketTo)
                         .toString(QStringLiteral("h:mm AP")));
        const int tipWidth = qMin(metrics.horizontalAdvance(tooltip) + 18,
                                  qMax(80, int(plot.width() - 8)));
        const QString visibleTooltip = metrics.elidedText(
            tooltip, Qt::ElideRight, qMax(40, tipWidth - 18));
        const QSize tipSize(tipWidth, metrics.height() + 12);
        const qreal tipX = qBound(plot.left(), x - tipSize.width() / 2.0,
                                  plot.right() - tipSize.width());
        QRectF tipRect(tipX, plot.top() + 8, tipSize.width(), tipSize.height());
        QColor tipBackground = colors.color(QPalette::Window);
        painter.setPen(border);
        painter.setBrush(tipBackground);
        painter.drawRoundedRect(tipRect, 5, 5);
        painter.setPen(text);
        painter.drawText(tipRect, Qt::AlignCenter, visibleTooltip);
    }

    if (m_dragging) {
        const qreal left = qBound(plot.left(), qMin(m_dragStartX, m_dragCurrentX),
                                  plot.right());
        const qreal right = qBound(plot.left(), qMax(m_dragStartX, m_dragCurrentX),
                                   plot.right());
        QRectF selection(QPointF(left, plot.top()),
                         QPointF(right, plot.bottom()));
        QColor selectionFill = m_accent;
        selectionFill.setAlpha(45);
        painter.setBrush(selectionFill);
        painter.setPen(QPen(m_accent, 1));
        painter.drawRect(selection);
    }
}

void LogTimelineChart::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton && plotRect().contains(event->position())) {
        m_dragStartX = event->position().x();
        m_dragCurrentX = m_dragStartX;
        m_dragging = true;
        setCursor(Qt::CrossCursor);
        update();
        event->accept();
        return;
    }
    QWidget::mousePressEvent(event);
}

void LogTimelineChart::mouseMoveEvent(QMouseEvent *event)
{
    m_pointerInside = true;
    m_pointerPosition = event->position();
    if (m_dragging)
        m_dragCurrentX = event->position().x();
    update();
    QWidget::mouseMoveEvent(event);
}

void LogTimelineChart::mouseReleaseEvent(QMouseEvent *event)
{
    if (!m_dragging || event->button() != Qt::LeftButton) {
        QWidget::mouseReleaseEvent(event);
        return;
    }
    m_dragCurrentX = event->position().x();
    m_dragging = false;
    unsetCursor();
    if (qAbs(m_dragCurrentX - m_dragStartX) >= 10.0) {
        const qint64 from = timeAtX(qMin(m_dragStartX, m_dragCurrentX));
        const qint64 to = timeAtX(qMax(m_dragStartX, m_dragCurrentX));
        if (to - from >= 60 * 1000) {
            m_viewFromMs = from;
            m_viewToMs = to;
            notifyViewChanged();
        }
    }
    update();
    event->accept();
}

void LogTimelineChart::mouseDoubleClickEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton && plotRect().contains(event->position())) {
        resetZoom();
        event->accept();
        return;
    }
    QWidget::mouseDoubleClickEvent(event);
}

void LogTimelineChart::leaveEvent(QEvent *event)
{
    m_pointerInside = false;
    update();
    QWidget::leaveEvent(event);
}

void LogTimelineChart::wheelEvent(QWheelEvent *event)
{
    const QRectF plot = plotRect();
    if (!plot.contains(event->position()) || event->angleDelta().y() == 0) {
        QWidget::wheelEvent(event);
        return;
    }
    const qint64 oldSpan = qMax<qint64>(1, m_viewToMs - m_viewFromMs);
    const qreal factor = event->angleDelta().y() > 0 ? 0.72 : 1.38;
    const qint64 newSpan = qBound<qint64>(60 * 1000,
                                         qRound64(oldSpan * factor),
                                         m_rangeToMs - m_rangeFromMs);
    const qreal anchorRatio =
        qBound(0.0, (event->position().x() - plot.left()) / plot.width(), 1.0);
    const qint64 anchor = timeAtX(event->position().x());
    qint64 from = anchor - qRound64(anchorRatio * newSpan);
    qint64 to = from + newSpan;
    if (from < m_rangeFromMs) {
        from = m_rangeFromMs;
        to = from + newSpan;
    }
    if (to > m_rangeToMs) {
        to = m_rangeToMs;
        from = to - newSpan;
    }
    m_viewFromMs = from;
    m_viewToMs = to;
    update();
    notifyViewChanged();
    event->accept();
}
