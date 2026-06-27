#pragma once

#include <QObject>

class QAbstractScrollArea;
class QEvent;
class QPushButton;
class QString;

// Floating ▲ / ▼ "jump to top / bottom" buttons overlaid on the bottom-right
// corner of a scroll area's viewport. Each button shows only while there is room
// to scroll that way. Shared by the rich agent transcript and the raw-output log
// so both agent-detail surfaces get the same affordance.
class ScrollJumpButtons : public QObject
{
    Q_OBJECT
public:
    // Attaches to `area` (becomes its child); the buttons live over its viewport.
    explicit ScrollJumpButtons(QAbstractScrollArea *area);

    // Restyle both buttons. Callers that follow the system color scheme re-apply
    // this on scheme changes; others let the global stylesheet theme them by the
    // "scrollJump" object name.
    void setButtonStyle(const QString &css);

signals:
    void topClicked();    // ▲ pressed: jump to the start
    void bottomClicked(); // ▼ pressed: jump to the end

protected:
    bool eventFilter(QObject *obj, QEvent *e) override;

private:
    void reposition();
    void updateVisibility();

    QAbstractScrollArea *m_area = nullptr;
    QPushButton *m_top = nullptr;
    QPushButton *m_bottom = nullptr;
};
