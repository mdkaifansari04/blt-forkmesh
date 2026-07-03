#pragma once

#include <QImage>
#include <QObject>
#include <QPoint>
#include <QRect>
#include <QVector>

class QScreen;
class PerScreenPanel;

// Coordinator for a multi-monitor region screenshot. Creates one transparent
// panel per connected screen so the crosshair and selection marquee appear on
// every monitor. The user can start the drag on any screen; the coordinator
// composites all screens on release and emits the cropped image.
//
// On Wayland, direct screen grabs return all-black; the capture is routed
// through the XDG Desktop Portal instead (async). The object deletes itself
// after emitting captured() or cancelled().
class ScreenCaptureOverlay : public QObject
{
    Q_OBJECT
public:
    // Shows panels on every connected screen. Returns nullptr if there are no
    // screens. The object deletes itself once it emits.
    static ScreenCaptureOverlay *begin();

signals:
    void captured(const QImage &image); // a non-empty region was selected
    void cancelled();                   // Esc, right-click, or an empty drag

private slots:
    // XDG portal Screenshot reply: response==0 carries a "uri" to the desktop
    // image, which we crop to the pending selection. Anything else is a cancel.
    void onPortalResponse(uint response, const QVariantMap &results);

private:
    ScreenCaptureOverlay(const QRect &virtualGeom, qreal dpr,
                         const QList<QScreen *> &screens);

    // Input callbacks from PerScreenPanel — all positions are in global coords:
    void onPress(Qt::MouseButton button, QPoint globalPos);
    void onMove(QPoint globalPos);
    void onRelease(Qt::MouseButton button, QPoint globalPos);
    void onEscape();

    void beginCapture(const QRect &sel); // sel in virtual-desktop-relative coords
    QImage compositeScreens();           // X11/macOS: live composite of all screens
    QImage grabViaScreens(const QRect &sel);
    void grabViaPortal(const QRect &sel); // Wayland: XDG portal (async)
    QImage cropDesktop(const QImage &full, const QRect &sel) const;
    void popOverrideCursor();
    void finish(const QImage &image);

    QRect m_virtualGeom;      // union of all screen geometries, in logical coords
    qreal m_dpr = 1.0;        // device-pixel ratio to grab/crop the snapshot at
    QImage m_frozen;          // desktop grabbed before the panels appeared; the
                              // panels paint slices of it (no translucency
                              // needed). Null → translucent live overlay.
    QPoint m_originGlobal;    // drag start in global screen coords
    QPoint m_currentGlobal;   // latest drag point in global screen coords
    QRect m_pendingSel;       // selection awaiting an async (portal) capture
    bool m_dragging = false;
    bool m_cursorPushed = false;
    bool m_done = false;      // guards against emitting twice
    QVector<PerScreenPanel *> m_panels; // one per connected screen

    friend class PerScreenPanel;
};
