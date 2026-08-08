#pragma once

#include <QImage>
#include <QObject>
#include <QPoint>
#include <QRect>
#include <QVector>

class QScreen;
class PerScreenPanel;

class ScreenCaptureOverlay : public QObject
{
    Q_OBJECT
public:
    static ScreenCaptureOverlay *begin();

signals:
    void captured(const QImage &image); // a non-empty region was selected
    void cancelled();                   // Esc, right-click, or an empty drag

private slots:
    void onPortalResponse(uint response, const QVariantMap &results);

private:
    ScreenCaptureOverlay(const QRect &virtualGeom, qreal dpr,
                         const QList<QScreen *> &screens);

    void createPanels();

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
    QPoint m_originGlobal;    // drag start in global screen coords
    QPoint m_currentGlobal;   // latest drag point in global screen coords
    QRect m_pendingSel;       // selection awaiting an async (portal) capture
    bool m_dragging = false;
    bool m_cursorPushed = false;
    bool m_done = false;      // guards against emitting twice
    bool m_awaitingFreezeGrab = false; // portal pre-grab (Wayland) still pending
    QList<QScreen *> m_screens; // screens to cover, captured at begin() time
    QVector<PerScreenPanel *> m_panels; // one per connected screen

    friend class PerScreenPanel;
};
