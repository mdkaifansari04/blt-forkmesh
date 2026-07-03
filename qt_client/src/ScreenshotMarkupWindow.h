#pragma once

#include <QColor>
#include <QDialog>
#include <QImage>
#include <QPoint>
#include <QRect>
#include <QVector>

class MarkupCanvas;

// A lightweight post-capture annotation window. Shows the freshly-captured
// screenshot in a scrollable viewport, lets the user scribble freehand ink or
// drop shape outlines on top, then sends the flattened result to the prompt.
//
// Workflow: ScreenCaptureOverlay (or ScreenDrawOverlay) emits captured() →
// caller constructs a ScreenshotMarkupWindow, connects imageAccepted() to the
// save/queue path, and shows the dialog. On "Add to Prompt" the markup is
// composited onto the base image and imageAccepted() fires; on "Discard" the
// dialog closes without emitting.
class ScreenshotMarkupWindow : public QDialog
{
    Q_OBJECT
public:
    explicit ScreenshotMarkupWindow(const QImage &screenshot, QWidget *parent = nullptr);

signals:
    void imageAccepted(const QImage &image); // base + annotations, ready to save

private:
    void onAccept();

    MarkupCanvas *m_canvas = nullptr;
};
