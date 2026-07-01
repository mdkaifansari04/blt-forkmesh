#pragma once

#include <QSize>
#include <QWidget>

class QPaintEvent;

// A small inline calibration target for the region screenshot tool, shown right
// inside Settings instead of as a full-screen window. It paints a dark square
// with a bright L-bracket in each corner (the "square with corners"), a light
// grid, a centre crosshair, and a label of its own pixel size. Grab it with the
// screenshot tool and confirm the captured pixels line up with the corners and
// the size shown — a quick way to check a region capture isn't offset or scaled.
// The screenshot itself is queued as the next new-agent attachment (see
// MainWindow::captureScreenRegion).
class ScreenAlignmentTarget : public QWidget
{
public:
    explicit ScreenAlignmentTarget(QWidget *parent = nullptr);

    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent *event) override;
};
