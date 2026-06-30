#pragma once

#include <QPixmap>
#include <QPoint>
#include <QRect>
#include <QWidget>

class QMouseEvent;
class QKeyEvent;
class QPaintEvent;

// A full-virtual-desktop overlay for region screenshots. Freezes a snapshot of
// every screen, then dims it and lets the user drag a rectangle anywhere on the
// computer: press starts the selection, drag rubber-bands it, release captures
// that region and emits captured(). Esc / right-click cancels.
//
// Spans the union of all screen geometries (so it works on multi-monitor setups)
// and bypasses the window manager so nothing repositions it. The snapshot is
// taken before the overlay is shown, so the dimming layer never ends up in the
// capture.
class ScreenCaptureOverlay : public QWidget
{
    Q_OBJECT
public:
    // Grabs the whole virtual desktop and shows the overlay. Returns nullptr if
    // nothing could be grabbed. The widget deletes itself once it emits.
    static ScreenCaptureOverlay *begin();

signals:
    void captured(const QImage &image); // a non-empty region was selected
    void cancelled();                   // Esc, right-click, or an empty drag

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;

private:
    ScreenCaptureOverlay(const QPixmap &shot, const QRect &virtualGeom,
                         qreal dpr);
    QRect selectionRect() const;
    void finish(const QImage &image);

    QPixmap m_shot;          // frozen snapshot of the whole virtual desktop
    qreal m_dpr = 1.0;       // device-pixel ratio the snapshot was grabbed at
    QPoint m_origin;         // drag start, in widget (logical) coords
    QPoint m_current;        // latest drag point, in widget (logical) coords
    bool m_dragging = false;
    bool m_done = false;     // guards against emitting twice
};
