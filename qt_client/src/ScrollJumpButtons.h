#pragma once

#include <QObject>

class QAbstractScrollArea;
class QEvent;
class QString;
class JumpArrowButton;









class ScrollJumpButtons : public QObject
{
    Q_OBJECT
public:

    explicit ScrollJumpButtons(QAbstractScrollArea *area);





    void setButtonStyle(const QString &css);

signals:
    void topClicked();
    void bottomClicked();

protected:
    bool eventFilter(QObject *obj, QEvent *e) override;

private:
    void reposition();
    void updateVisibility();
    void applyArrowColor();

    QAbstractScrollArea *m_area = nullptr;
    JumpArrowButton *m_top = nullptr;
    JumpArrowButton *m_bottom = nullptr;
};
