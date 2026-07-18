#pragma once

// A QStackedWidget that reports the size of only its *current* page instead of
// the tallest page across the whole stack (QStackedWidget's default). The
// default "size to the tallest page" behaviour makes a short page, e.g. the
// Code overview, inherit a taller sibling tab's height, so the visible page
// scrolled (or overflowed) as a whole even though every visible pane already
// had its own scroll (adhoc #96). Sizing to the current page keeps each page
// fitted to the viewport so only its inner scroll areas (lists, tables, the
// README view) scroll, not the whole middle window. Used for the top-level
// section stack, the repo-detail tab stack and the nested overview stacks,
// which sit directly in their layouts since the outer page-wide QScrollAreas
// were removed (adhoc #108).

#include <QSize>
#include <QStackedWidget>
#include <QWidget>

class CurrentPageStack : public QStackedWidget
{
public:
    explicit CurrentPageStack(QWidget *parent = nullptr)
        : QStackedWidget(parent)
    {
        // The reported size hint changes with the current page, so nudge the
        // layout (and the wrapping scroll area) to re-fit on every switch.
        connect(this, &QStackedWidget::currentChanged, this,
                [this](int) { updateGeometry(); });
    }

    QSize sizeHint() const override
    {
        if (QWidget *w = currentWidget())
            return w->sizeHint();
        return QStackedWidget::sizeHint();
    }

    QSize minimumSizeHint() const override
    {
        if (QWidget *w = currentWidget())
            return w->minimumSizeHint();
        return QStackedWidget::minimumSizeHint();
    }
};
