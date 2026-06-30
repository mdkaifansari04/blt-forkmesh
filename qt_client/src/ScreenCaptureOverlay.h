#pragma once

#include <QImage>
#include <QPoint>
#include <QRect>
#include <QWidget>

class QMouseEvent;
class QKeyEvent;
class QPaintEvent;

// A full-virtual-desktop overlay for region screenshots. It stays transparent so
// the live desktop shows through — the cursor becomes a crosshair "snippet" tool
// and the only thing drawn is a dotted marquee around the drag. Press starts the
// selection, drag rubber-bands it, release hides the overlay and grabs just that
// region. Esc / right-click cancels.
//
// Spans the union of all screen geometries (so it works on multi-monitor setups)
// and bypasses the window manager so nothing repositions it. Nothing dims or
// freezes the screen: the capture is grabbed live on release, after the overlay
// hides, so neither the marquee nor a dim layer can bleed into it.
class ScreenCaptureOverlay : public QWidget
{
    Q_OBJECT
public:
    // Shows the overlay over the whole virtual desktop. Returns nullptr if there
    // are no screens. The widget deletes itself once it emits.
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
    ScreenCaptureOverlay(const QRect &virtualGeom, qreal dpr);
    QRect selectionRect() const;
    QImage grabSelection(const QRect &sel); // hides overlay, grabs live region
    void finish(const QImage &image);

    QRect m_virtualGeom;     // union of all screen geometries, in logical coords
    qreal m_dpr = 1.0;       // device-pixel ratio to grab/crop the snapshot at
    QPoint m_origin;         // drag start, in widget (logical) coords
    QPoint m_current;        // latest drag point, in widget (logical) coords
    bool m_dragging = false;
    bool m_done = false;     // guards against emitting twice
};
