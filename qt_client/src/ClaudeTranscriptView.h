#pragma once

#include <QHash>
#include <QJsonObject>
#include <QPointer>
#include <QScrollArea>
#include <QString>
#include <QStringList>
#include <QVector>

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

    // One parsed stream-json event. countStats is false when replaying an event
    // whose token/cost totals were already folded in via accumulateStatsOnly()
    // (the "load earlier" path below promotes a stats-only event to a rendered
    // row and must not add it to the running totals twice).
    void handleEvent(const QJsonObject &ev, bool countStats = true);
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
    // The user answered an AskUserQuestion clarifying-question card. toolUseId is
    // the pending tool_use to satisfy; answer is the assembled reply text. The
    // host sends it back to the CLI as a tool_result (see sendToolResult).
    void questionAnswered(const QString &toolUseId, const QString &answer);
    // The user clicked one of the options on a heuristically-detected inline
    // clarifying question (issue #212) — plain assistant prose that lays out a
    // numbered list of choices instead of going through the AskUserQuestion
    // tool. Unlike questionAnswered() there's no tool_use_id to satisfy; the
    // host just sends `answer` as a normal follow-up prompt.
    void inlineChoiceAnswered(const QString &answer);
    // The "Load N earlier events" notice was clicked, or the view was scrolled
    // near its top — the host should slice the next batch of earlier events off
    // its buffer and hand them to prependEarlierEvents(). Only emitted once per
    // batch (re-armed once prependEarlierEvents() lands the new content).
    void loadEarlierRequested();

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

    // ---- bulk rebuild support (replaying a stored session) -----------------
    // While on, addRow() skips the per-row fade-in animation: replaying hundreds
    // of buffered events created one QGraphicsOpacityEffect + animation per row
    // and repolished each mid-rebuild, freezing the click that opened the session.
    void setBulkPopulate(bool on) { m_bulkPopulate = on; }
    // Fold an event that will NOT be rendered into the token/cost totals, so a
    // tail-capped replay still reports the run's true numbers. No signal is
    // emitted; call addSkippedNotice() (or render further events) to publish.
    void accumulateStatsOnly(const QJsonObject &ev);
    // A clickable "Load N earlier events" row at the top of a tail-capped
    // replay; also publishes the totals gathered above. Replacing an existing
    // notice (count updated after a load) removes the old row first. Clicking
    // it, or scrolling near the top of the view, emits loadEarlierRequested()
    // so the host can reveal the next batch — see prependEarlierEvents().
    void addSkippedNotice(int count);
    // Reveal a batch of previously-skipped events at the very top of the
    // transcript, preserving the user's current scroll anchor so the content
    // they're reading doesn't jump. `events` is the newly-revealed slice,
    // oldest first; their stats were already folded in via
    // accumulateStatsOnly() when they were first skipped, so they render with
    // countStats=false. stillSkipped is however many older events remain
    // hidden — a fresh notice is added above the batch when > 0, otherwise the
    // notice is dropped and the full transcript is now visible.
    void prependEarlierEvents(const QList<QJsonObject> &events, int stillSkipped);

    // ---- transcript search (adhoc #201) ------------------------------------
    // Find query (case-insensitive) across the rendered transcript, highlighting
    // every match, selecting the first and scrolling it into view; returns the
    // total number of matches. clearSearch() removes all highlighting.
    // searchNext()/searchPrev() step through the matches (wrapping at the ends).
    // searchResultsChanged() reports "current of total" (current is 1-based, 0
    // when there are no matches) so the host can show a counter.
    int search(const QString &query);
    void searchNext();
    void searchPrev();
    void clearSearch();

    // Heuristic detection of a clarifying question the CLI asked in plain prose
    // rather than through the AskUserQuestion tool — a numbered list of two or
    // more options alongside a question mark (issue #212), e.g. "do you want
    // me to: 1. ... or 2. ...?". Shared with MainWindow so the same rule flags
    // the session "Waiting" (hand icon) as a real AskUserQuestion turn does.
    static bool parseInlineChoices(const QString &markdown, QStringList &options);

signals:
    void searchResultsChanged(int current, int total);

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
    // Returns true if the text also read as an inline clarifying question (see
    // parseInlineChoices), so the caller can treat the turn the same as a real
    // AskUserQuestion for the activity-ticker state.
    bool addAssistantText(const QString &markdown);
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

    // Claude Code's AskUserQuestion tool: instead of a passive tool card, render
    // an interactive multiple-choice card (one group per question, each option a
    // clickable button, plus a free-text "Other" field) so the user can answer
    // the clarifying question in place. On submit it emits questionAnswered().
    // markAskAnswered() locks the card and shows the chosen reply — driven by a
    // synthetic "_local_ask_answer" event so the answered state survives a
    // transcript rebuild/replay.
    void addAskUserQuestion(const QString &id, const QJsonObject &input);
    void markAskAnswered(const QString &id, const QString &answer);

    // A lighter card for a heuristically-detected inline clarifying question
    // (see parseInlineChoices): one clickable row per option, no tool_use_id
    // involved. Clicking emits inlineChoiceAnswered() and locks the card.
    QWidget *addInlineChoices(const QStringList &options);
    // Disable a (possibly already-answered) inline-choice card in place, e.g.
    // once the conversation has moved past it.
    void lockInlineChoices(QWidget *box);

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

    // ---- incremental "load earlier" state (don't-truncate-the-transcript) --
    // While >= 0, addRow() inserts rows at this column index instead of the
    // usual append-before-the-spacer position, and advances it — so a batch of
    // earlier events lands, in order, above whatever was previously first.
    int m_prependAt = -1;
    // The row currently at column index 0, so a newly-prepended row landing
    // there can flatten the old one's rail line (RailItem::setFirst).
    QPointer<QWidget> m_priorFirstRow;
    QPointer<QWidget> m_skippedNotice; // the "Load N earlier events" row, if any
    int m_skippedCount = 0;
    bool m_loadEarlierPending = false; // a request is in flight; don't re-emit
    // Set just before a prepend batch starts; the ctor's rangeChanged handler
    // uses the resulting range growth to shift the scrollbar by the same
    // amount, so newly-inserted content above the viewport doesn't yank the
    // rows the user is currently reading.
    bool m_prependCompensationPending = false;
    int m_prependOldMax = 0;
    int m_prependOldValue = 0;

    struct ToolCard {
        QFrame *box = nullptr;       // the bordered IN/OUT box
        QVBoxLayout *io = nullptr;   // rows: IN, then OUT once the result lands
        bool hasResult = false;      // an OUT row was appended
    };
    QHash<QString, ToolCard> m_toolCards;

    // Live AskUserQuestion cards, keyed by their tool_use id: the button area to
    // lock once answered and the status line that shows the chosen reply.
    struct AskCard {
        QPointer<QWidget> buttons; // disabled on answer
        QPointer<QLabel> status;   // "✓ You answered: …"
    };
    QHash<QString, AskCard> m_askCards;
    // The single most-recently-added, still-unanswered inline-choice card (see
    // addInlineChoices), if any — locked the moment the conversation moves on
    // (a new assistant turn or a user reply arrives).
    QPointer<QWidget> m_openInlineChoices;

    Collapsible *m_liveThinking = nullptr;
    QLabel *m_thinkingBody = nullptr;
    QString m_thinkingText;
    int m_thinkingTokens = 0;
    qint64 m_thinkingStartMs = 0; // wall-clock start, for "Thought for Ns"

    QPropertyAnimation *m_scrollAnim = nullptr; // smooth scrolling
    qint64 m_totalTokens = 0;
    double m_totalCost = 0.0;
    bool m_bulkPopulate = false; // see setBulkPopulate()

    // ---- transcript search state ------------------------------------------
    // Each label that contains at least one match, in top-to-bottom order, with
    // its pristine (un-highlighted) text/format so highlighting is reversible.
    struct LabelHit {
        QPointer<QLabel> label;
        Qt::TextFormat fmt;
        QString orig;
        int count; // matches in this label
    };
    void rebuildSearchMatches();   // recompute m_searchLabels from m_searchQuery
    void renderSearchHighlights(); // (re)apply highlights, marking the current one
    void restoreSearchOriginals(); // put every modified label back as it was
    void scrollToCurrentMatch();
    void stepMatch(int delta);
    QLabel *currentMatchLabel() const;
    QString highlightedTextFor(const QString &orig, Qt::TextFormat fmt,
                               int currentLocalOcc) const;
    QString m_searchQuery;
    QVector<LabelHit> m_searchLabels;
    int m_searchTotal = 0;    // sum of all per-label counts
    int m_searchCurrent = -1; // global match index, -1 = none selected
};
