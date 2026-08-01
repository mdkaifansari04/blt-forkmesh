#pragma once

#include <QColor>
#include <QDialog>
#include <QImage>
#include <QPoint>
#include <QRect>
#include <QVector>

class MarkupCanvas;











class ScreenshotMarkupWindow : public QDialog
{
    Q_OBJECT
public:
    explicit ScreenshotMarkupWindow(const QImage &screenshot, QWidget *parent = nullptr);

signals:
    void imageAccepted(const QImage &image);

private:
    void onAccept();

    MarkupCanvas *m_canvas = nullptr;
};
