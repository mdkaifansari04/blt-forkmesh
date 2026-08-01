#pragma once








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


        p.setPen(Qt::NoPen);
        QColor track = m_color;
        track.setAlpha(48);
        p.setBrush(track);
        p.drawEllipse(r);




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
