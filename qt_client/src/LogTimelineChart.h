#ifndef FORKMESH_LOG_TIMELINE_CHART_H
#define FORKMESH_LOG_TIMELINE_CHART_H

#include <QColor>
#include <QVector>
#include <QWidget>

#include <functional>

struct LogTimelineEntry {
    qint64 timestampMs = 0;
    QString category;
};

// A compact activity rail for the persisted application log. Dragging across
// the rail zooms to that interval; the wheel zooms around the pointer and a
// double-click returns to the selected timeframe.
class LogTimelineChart : public QWidget {
public:
    explicit LogTimelineChart(QWidget *parent = nullptr);

    void setEntries(QVector<LogTimelineEntry> entries);
    void appendEntry(const LogTimelineEntry &entry);
    void removeEntry(qint64 timestampMs, const QString &category);
    void setCategoryFilter(const QString &category, const QColor &accent);
    void setRange(qint64 fromMs, qint64 toMs);
    void resetZoom();

    qint64 viewFromMs() const { return m_viewFromMs; }
    qint64 viewToMs() const { return m_viewToMs; }
    int visibleEntryCount() const;
    bool isZoomed() const;

    std::function<void()> viewChanged;

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void leaveEvent(QEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;

private:
    QRectF plotRect() const;
    qint64 timeAtX(qreal x) const;
    bool entryIsVisible(const LogTimelineEntry &entry) const;
    void notifyViewChanged();

    QVector<LogTimelineEntry> m_entries;
    QString m_categoryFilter;
    QColor m_accent = QColor(QStringLiteral("#58a6ff"));
    qint64 m_rangeFromMs = 0;
    qint64 m_rangeToMs = 1;
    qint64 m_viewFromMs = 0;
    qint64 m_viewToMs = 1;
    QPointF m_pointerPosition;
    qreal m_dragStartX = -1.0;
    qreal m_dragCurrentX = -1.0;
    bool m_dragging = false;
    bool m_pointerInside = false;
};

#endif
