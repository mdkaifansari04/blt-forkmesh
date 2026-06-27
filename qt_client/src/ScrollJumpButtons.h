#pragma once

#include <QObject>

class QAbstractScrollArea;
class QEvent;
class QString;
class JumpArrowButton;

// Floating ▲ / ▼ "jump to top / bottom" buttons overlaid on the bottom-right
// corner of a scroll area's viewport. Each button shows only while there is room
// to scroll that way. Shared by the rich agent transcript and the raw-output log
// so both agent-detail surfaces get the same affordance.
//
// The arrows are *painted* (not drawn from a font glyph) so they always show
// even when the UI font lacks the Unicode triangle characters — otherwise the
// circle renders but the arrow inside it is blank.
class ScrollJumpButtons : public QObject
{
    Q_OBJECT
public:
    // Attaches to `area` (becomes its child); the buttons live over its viewport.
    explicit ScrollJumpButtons(QAbstractScrollArea *area);

    // Restyle both buttons (circle background/border). Callers that follow the
    // system color scheme re-apply this on scheme changes; others let the global
    // stylesheet theme them by the "scrollJump" object name. The painted arrow
    // colour tracks the system scheme automatically.
    void setButtonStyle(const QString &css);

signals:
    void topClicked();    // ▲ pressed: jump to the start
    void bottomClicked(); // ▼ pressed: jump to the end

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
