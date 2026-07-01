#pragma once

#include <QColor>
#include <QPoint>
#include <QRect>
#include <QVector>
#include <QWidget>

class QMouseEvent;
class QKeyEvent;
class QPaintEvent;

// A full-virtual-desktop overlay for freehand drawing on the screen. Like
// ScreenCaptureOverlay it stays transparent so the live desktop shows through and
// bypasses the window manager so nothing repositions it; the cursor becomes a pen
// and left-drag lays down ink anywhere on the computer. Nothing is captured or
// saved — this is a throwaway scratch layer for pointing things out on screen.
//
// Esc, right-click, or the Backspace/Delete keys clear and dismiss the overlay;
// while it's up, the whole screen belongs to the pen. Spans the union of every
// screen geometry so it works across multi-monitor setups.
class ScreenDrawOverlay : public QWidget
{
    Q_OBJECT
public:
    // Shows the overlay over the whole virtual desktop. Returns nullptr if there
    // are no screens. The widget deletes itself once it is dismissed.
    static ScreenDrawOverlay *begin();

signals:
    void dismissed();

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;

private:
    explicit ScreenDrawOverlay(const QRect &virtualGeom);
    void popOverrideCursor(); // restore the pen cursor once
    void finish();

    QRect m_virtualGeom;                // union of all screen geometries
    QColor m_penColor;                  // ink colour
    QVector<QVector<QPoint>> m_strokes; // completed freehand strokes
    QVector<QPoint> m_current;          // the stroke being drawn right now
    bool m_drawing = false;
    bool m_cursorPushed = false; // an override "pen" cursor is on the stack
    bool m_done = false;         // guards against emitting twice
};
