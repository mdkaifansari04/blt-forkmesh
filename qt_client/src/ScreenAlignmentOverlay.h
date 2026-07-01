#pragma once

#include <QRect>
#include <QWidget>

class QKeyEvent;
class QMouseEvent;
class QPaintEvent;
class QPainter;

// A full-virtual-desktop calibration target for lining up the region screenshot
// tool. Unlike the capture/draw overlays this one is opaque: it paints a known
// reference pattern over the whole desktop so a grab of it can be checked against
// what it should contain. It draws:
//   * bright L-brackets in all four corners, each labelled with its exact
//     virtual-desktop pixel coordinate,
//   * ruler ticks (labelled every 100px) down the top and left edges,
//   * a light 50px grid,
//   * a centre crosshair with concentric alignment rings, plus smaller ring
//     markers at the quarter points.
// Capture any part of it with the screenshot tool and confirm the pixels line up
// with the labelled coordinates — if the grab is offset or scaled, the numbers
// and markers won't match. Esc, any click, or any key dismisses it. Spans the
// union of every screen geometry so it works across multi-monitor setups.
class ScreenAlignmentOverlay : public QWidget
{
    Q_OBJECT
public:
    // Shows the target over the whole virtual desktop. Returns nullptr if there
    // are no screens. The widget deletes itself once it is dismissed.
    static ScreenAlignmentOverlay *begin();

signals:
    void dismissed();

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;

private:
    explicit ScreenAlignmentOverlay(const QRect &virtualGeom);
    void paintScreen(QPainter &painter, const QRect &local);
    void paintMarker(QPainter &painter, const QPoint &c, int radius);
    void finish();

    QRect m_virtualGeom; // union of all screen geometries, in logical coords
    bool m_done = false; // guards against emitting twice
};
