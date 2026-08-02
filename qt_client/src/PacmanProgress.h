#pragma once

// A tiny Pac-Man-shaped progress chart (adhoc #56): a circle whose green body
// fills as `progress` climbs from 0 to 1, leaving a wedge "mouth" that opens to
// the right and closes shut once the value reaches 100%. Used in the PR review
// page's sticky file header to show how much of the current file has scrolled
// past. Deliberately dependency-light (no Q_OBJECT/signals, so no moc) — it is
// a pure painter driven by setProgress().

#include <QColor>
#include <QPainter>
#include <QRectF>
#include <QWidget>

#include <algorithm>

class PacmanProgress : public QWidget
{
public:
    explicit PacmanProgress(QWidget *parent = nullptr) : QWidget(parent)
    {
        setFixedSize(16, 16);
    }

    // 0.0 = nothing seen (empty track), 1.0 = whole file seen (full circle).
    void setProgress(double f)
    {
        f = std::clamp(f, 0.0, 1.0);
        if (qFuzzyCompare(f + 1.0, m_progress + 1.0))
            return;
        m_progress = f;
        update();
    }
    double progress() const { return m_progress; }

    void setColor(const QColor &c)
    {
        if (m_color != c) {
            m_color = c;
            update();
        }
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        const qreal m = 1.5;
        const QRectF r(m, m, width() - 2 * m, height() - 2 * m);

        // Faint full-circle track so the chart still reads near 0%.
        p.setPen(Qt::NoPen);
        QColor track = m_color;
        track.setAlpha(48);
        p.setBrush(track);
        p.drawEllipse(r);

        // Pac-Man body: a pie sweeping `progress` of the full turn, centred so
        // its mouth (the unfilled remainder) opens to the right (3 o'clock).
        // Qt pie angles are 1/16-degree, 0 at 3 o'clock, counter-clockwise.
        const int full = qRound(360.0 * 16.0 * m_progress);
        if (full > 0) {
            const int gap = 360 * 16 - full;
            p.setBrush(m_color);
            p.drawPie(r, gap / 2, full);
        }
    }

private:
    double m_progress = 0.0;
    QColor m_color{0x3f, 0xb9, 0x50};
};
