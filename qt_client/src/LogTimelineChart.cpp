#include "LogTimelineChart.h"

#include <QDateTime>
#include <QMouseEvent>
#include <QPainter>
#include <QPalette>
#include <QWheelEvent>

#include <algorithm>

LogTimelineChart::LogTimelineChart(QWidget *parent) : QWidget(parent)
{
    setObjectName(QStringLiteral("logTimelineChart"));
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
    setAccessibleName(QStringLiteral("Log activity chart"));
    setAccessibleDescription(
        QStringLiteral("A compact log activity rail. Drag across the rail or "
                       "use the mouse wheel to zoom; double-click to reset."));
}

void LogTimelineChart::setEntries(QVector<LogTimelineEntry> entries)
{
    // Log history is appended in timestamp order. Avoid sorting the entire
    // saved history whenever the user opens the Log page; a sort of thousands
    // of entries made the new timeline noticeably delay the actual log view.
    // Keep the defensive sort for callers that supply an out-of-order batch.
    const auto before = [](const LogTimelineEntry &left,
                           const LogTimelineEntry &right) {
        return left.timestampMs < right.timestampMs;
    };
    if (!std::is_sorted(entries.cbegin(), entries.cend(), before))
        std::sort(entries.begin(), entries.end(), before);
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

void LogTimelineChart::removeEntry(qint64 timestampMs,
                                   const QString &category)
{
    if (timestampMs <= 0)
        return;
    auto it = std::lower_bound(
        m_entries.begin(), m_entries.end(), timestampMs,
        [](const LogTimelineEntry &candidate, qint64 timestamp) {
            return candidate.timestampMs < timestamp;
        });
    for (; it != m_entries.end() && it->timestampMs == timestampMs; ++it) {
        if (it->category != category)
            continue;
        m_entries.erase(it);
        update();
        return;
    }
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
    const auto first = std::lower_bound(
        m_entries.cbegin(), m_entries.cend(), m_viewFromMs,
        [](const LogTimelineEntry &entry, qint64 timestamp) {
            return entry.timestampMs < timestamp;
        });
    const auto last = std::upper_bound(
        first, m_entries.cend(), m_viewToMs,
        [](qint64 timestamp, const LogTimelineEntry &entry) {
            return timestamp < entry.timestampMs;
        });
    if (m_categoryFilter.isEmpty())
        return int(std::distance(first, last));
    return int(std::count_if(first, last, [this](const LogTimelineEntry &entry) {
        return entry.category == m_categoryFilter;
    }));
}

bool LogTimelineChart::isZoomed() const
{
    return m_viewFromMs != m_rangeFromMs || m_viewToMs != m_rangeToMs;
}

QSize LogTimelineChart::sizeHint() const
{
    return QSize(900, 68);
}

QSize LogTimelineChart::minimumSizeHint() const
{
    return QSize(320, 48);
}

QRectF LogTimelineChart::plotRect() const
{
    return QRectF(rect()).adjusted(8.0, 8.0, -8.0, -8.0);
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

    const int bucketCount = qBound(16, int(plot.width() / 7.0), 180);
    QVector<int> buckets(bucketCount, 0);
    const qint64 span = qMax<qint64>(1, m_viewToMs - m_viewFromMs);
    const auto first = std::lower_bound(
        m_entries.cbegin(), m_entries.cend(), m_viewFromMs,
        [](const LogTimelineEntry &entry, qint64 timestamp) {
            return entry.timestampMs < timestamp;
        });
    const auto last = std::upper_bound(
        first, m_entries.cend(), m_viewToMs,
        [](qint64 timestamp, const LogTimelineEntry &entry) {
            return timestamp < entry.timestampMs;
        });
    int total = 0;
    for (auto it = first; it != last; ++it) {
        const LogTimelineEntry &entry = *it;
        if (!entryIsVisible(entry))
            continue;
        const int bucket = qBound(
            0, int((entry.timestampMs - m_viewFromMs) * bucketCount / span),
            bucketCount - 1);
        ++buckets[bucket];
        ++total;
    }
    const int maximum = qMax(1, *std::max_element(buckets.cbegin(), buckets.cend()));
    painter.setPen(QPen(grid, 1));
    painter.drawLine(QPointF(plot.left(), plot.bottom()),
                     QPointF(plot.right(), plot.bottom()));

    // One upright line per bucket rather than a joined area curve: each line is
    // the count in its own slice of time, so a quiet minute reads as a gap
    // instead of a slope drawn between the two busy minutes on either side.
    const qreal slot = plot.width() / bucketCount;
    const qreal lineWidth = qBound(1.0, slot - 2.0, 3.0);
    QColor emptyTick = m_accent;
    emptyTick.setAlpha(38);
    const QPen barPen(m_accent, lineWidth, Qt::SolidLine, Qt::FlatCap);
    const QPen emptyPen(emptyTick, lineWidth, Qt::SolidLine, Qt::FlatCap);
    painter.save();
    painter.setClipRect(plot);
    painter.setRenderHint(QPainter::Antialiasing, false);
    for (int index = 0; index < bucketCount; ++index) {
        const qreal x = plot.left() + (index + 0.5) * slot;
        const int count = buckets.at(index);
        // A stub tick keeps an empty slice legible as part of the same rail
        // rather than an unexplained blank stretch.
        painter.setPen(count > 0 ? barPen : emptyPen);
        const qreal height =
            count > 0 ? qMax(2.0, qreal(count) / maximum * plot.height()) : 1.5;
        painter.drawLine(QPointF(x, plot.bottom()),
                         QPointF(x, plot.bottom() - height));
    }
    painter.restore();
    painter.setRenderHint(QPainter::Antialiasing, true);

    if (total == 0) {
        QFont railFont = font();
        railFont.setPointSizeF(qMax(8.0, railFont.pointSizeF() - 1.0));
        painter.setFont(railFont);
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
        const QFontMetrics metrics(font());
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
