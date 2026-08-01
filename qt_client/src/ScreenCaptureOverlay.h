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
    void captured(const QImage &image);
    void cancelled();

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

    void beginCapture(const QRect &sel);
    QImage compositeScreens();
    QImage grabViaScreens(const QRect &sel);
    void grabViaPortal(const QRect &sel);
    QImage cropDesktop(const QImage &full, const QRect &sel) const;
    void popOverrideCursor();
    void finish(const QImage &image);

    QRect m_virtualGeom;
    qreal m_dpr = 1.0;
    QImage m_frozen;


    QPoint m_originGlobal;
    QPoint m_currentGlobal;
    QRect m_pendingSel;
    bool m_dragging = false;
    bool m_cursorPushed = false;
    bool m_done = false;
    bool m_awaitingFreezeGrab = false;
    QList<QScreen *> m_screens;
    QVector<PerScreenPanel *> m_panels;

    friend class PerScreenPanel;
};
