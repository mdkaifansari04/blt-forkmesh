#pragma once

#include <QHash>
#include <QJsonObject>
#include <QScrollArea>
#include <QString>

class QVBoxLayout;
class QWidget;
class QLabel;
class QPropertyAnimation;
class QResizeEvent;
class Collapsible;
class ScrollJumpButtons;

// Renders a Claude Code session as a native, extension-style chat transcript by
// consuming the stream-json events from ClaudeStreamSession: assistant text,
// foldable "thinking" (with a live token counter while it streams), tool-call
// cards (Bash command, Edit/Write diffs, Read, TodoWrite, …) with their results
// attached, plus the final result and rate-limit (usage) banner. Follows the
// system light/dark color scheme.
class ClaudeTranscriptView : public QScrollArea
{
    Q_OBJECT
public:
    explicit ClaudeTranscriptView(QWidget *parent = nullptr);

    void handleEvent(const QJsonObject &ev); // one parsed stream-json event
    void addUserTurn(const QString &text);
    void clear();

    // Colors, chosen from the system color scheme; public so the embedded
    // Collapsible helper can style itself consistently.
    struct Palette {
        QString canvas, surface, border, text, muted, accent, add, del, addBg,
            delBg, userBg;
    };

signals:
    // kind is "5h" or "weekly"; from rate_limit_event.
    void usageChanged(const QString &kind, const QString &text, int percent);
    // Running totals as the session spends them (assistant usage + result).
    void statsChanged(qint64 tokens, double costUsd);

protected:
    // Keep the bottom spacer tall enough that the newest card can scroll to the
    // top of the viewport (chat-style "latest pinned up").
    void resizeEvent(QResizeEvent *e) override;

public:
    // Jump the view to the start / end of the transcript (driven by the floating
    // ▲/▼ ScrollJumpButtons in the corner).
    void scrollToTop();
    void scrollToBottom();
    // Land on the latest content immediately (no smooth animation) and keep it
    // pinned through any pending relayout — used when a session is opened so a
    // click always shows the bottom of its transcript (adhoc #128). A plain
    // scrollToBottom() animation would be overridden by, and fight, the deferred
    // rangeChanged pin after the freshly-rebuilt rows lay out.
    void jumpToBottom();
    // Render Edit/MultiEdit diffs side-by-side (old | new) instead of unified.
    void setSplitDiffs(bool on);

private:
    void applyScheme();
    // Add a row to the timeline. nodeColor tints the connecting-line node dot.
    // Returns the row wrapper so live rows (the activity ticker) can be removed.
    QWidget *addRow(QWidget *card, const QString &nodeColor = QString());
    // The whimsical "what it's doing" ticker shown while the agent is working.
    void ensureActivity();
    void clearActivity();
    void cycleActivityWord();
    void smoothScrollTo(int value);
    void fadeIn(QWidget *card);
    QString accentFor(const QString &toolName) const;

    QWidget *makeBubble(const QString &title, const QString &markdown,
                        const QString &accent);
    Collapsible *makeCollapsible(const QString &header, QWidget *body,
                                 bool expanded);
    void addAssistantBlocks(const QJsonObject &message);
    // Plain (un-boxed) assistant prose, like the Claude Code conversation view.
    void addAssistantText(const QString &markdown);
    // A "Name  subtitle" tool header line (the timeline rail supplies the dot).
    QWidget *dotHeader(const QString &name, const QString &subtitle);
    // One labelled row ("IN"/"OUT") inside a tool card's box.
    QWidget *ioRow(const QString &label, QWidget *content);
    // Monospace content with no panel background (it lives inside a card box).
    QWidget *makeMono(const QString &text, bool collapsedIfLong);

    // Live "thinking" lifecycle.
    void ensureLiveThinking();
    void setThinkingTokens(int tokens);
    void appendThinkingDelta(const QString &text);
    void finalizeThinking(const QString &fullText);

    void addToolUse(const QString &id, const QString &name,
                    const QJsonObject &input);
    void addToolResult(const QString &id, const QString &text, bool isError);
    void addResult(const QJsonObject &ev);

    QString toolSubtitle(const QString &name, const QJsonObject &input) const;
    QWidget *toolBody(const QString &name, const QJsonObject &input);
    QWidget *makeDiff(const QString &oldText, const QString &newText);
    QWidget *makeCode(const QString &text, bool collapsedIfLong = false);

    Palette m_p;
    QWidget *m_container = nullptr;
    QVBoxLayout *m_col = nullptr;
    QWidget *m_bottomSpacer = nullptr; // small tail so the last row isn't flush
    // Follow mode: while the view is at the bottom, new content keeps it pinned
    // there; scrolling up releases it until the user returns to the bottom.
    bool m_stickBottom = true;
    ScrollJumpButtons *m_jumpButtons = nullptr; // floating ▲/▼ jump corner
    bool m_splitDiffs = false;            // side-by-side vs unified diffs
    QWidget *m_activity = nullptr;        // live "what it's doing" ticker row
    QLabel *m_activityLabel = nullptr;
    QTimer *m_activityTimer = nullptr;

    struct ToolCard {
        QFrame *box = nullptr;       // the bordered IN/OUT box
        QVBoxLayout *io = nullptr;   // rows: IN, then OUT once the result lands
        bool hasResult = false;      // an OUT row was appended
    };
    QHash<QString, ToolCard> m_toolCards;

    Collapsible *m_liveThinking = nullptr;
    QLabel *m_thinkingBody = nullptr;
    QString m_thinkingText;
    int m_thinkingTokens = 0;
    qint64 m_thinkingStartMs = 0; // wall-clock start, for "Thought for Ns"

    QPropertyAnimation *m_scrollAnim = nullptr; // smooth scrolling
    qint64 m_totalTokens = 0;
    double m_totalCost = 0.0;
};
