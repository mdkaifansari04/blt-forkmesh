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
//
// Capture path depends on the display server. On X11/macOS the region is grabbed
// directly off the screens. On Wayland direct grabs return an all-black image
// (the security model forbids it), so we route through the XDG Desktop Portal's
// Screenshot interface instead and crop the returned desktop image — which is why
// the grab can complete asynchronously.
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

private slots:
    // XDG portal Screenshot reply: response==0 carries a "uri" to the desktop
    // image, which we crop to the pending selection. Anything else is a cancel.
    void onPortalResponse(uint response, const QVariantMap &results);

private:
    ScreenCaptureOverlay(const QRect &virtualGeom, qreal dpr);
    QRect selectionRect() const;            // widget-local marquee rect
    QRect captureRect() const;              // selection in virtual-desktop coords
    void beginCapture(const QRect &sel);    // hides overlay, then grabs the region
    QImage grabViaScreens(const QRect &sel);// X11/macOS: live composite + crop
    void grabViaPortal(const QRect &sel);   // Wayland: XDG portal (async)
    QImage cropDesktop(const QImage &full, const QRect &sel) const;
    void popOverrideCursor();               // restore the snip cursor once
    void finish(const QImage &image);

    QRect m_virtualGeom;     // union of all screen geometries, in logical coords
    qreal m_dpr = 1.0;       // device-pixel ratio to grab/crop the snapshot at
    QPoint m_origin;         // drag start, in widget (logical) coords
    QPoint m_current;        // latest drag point, in widget (logical) coords
    QRect m_pendingSel;      // selection awaiting an async (portal) capture
    bool m_dragging = false;
    bool m_cursorPushed = false; // an override "snip" cursor is on the stack
    bool m_done = false;     // guards against emitting twice
};
