#pragma once













#include <QSize>
#include <QStackedWidget>
#include <QWidget>

class CurrentPageStack : public QStackedWidget
{
public:
    explicit CurrentPageStack(QWidget *parent = nullptr)
        : QStackedWidget(parent)
    {


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
