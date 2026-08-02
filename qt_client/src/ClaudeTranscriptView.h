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

// Renders a CLI coding-agent session as a native transcript styled after the
// Claude Code CLI itself: "> prompt" bands for user turns, ●-bulleted assistant
// and tool rows, ⎿-connected results, ✻ thinking rows — flat monospace columns
// rather than boxed cards. Claude stream-json events and the normalized Codex
// app-server events share this surface: assistant text, live reasoning, tool
// headers and output, diffs, questions, usage, and final results. A Codex run
// re-dresses those same rows in Codex's own idiom — see setCodexStyle().
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

    // The CLI-style marker a row leads with: a coloured ● (assistant text and
    // tools), the user turn's >, the thinking ✻, or nothing (muted meta lines).
    // Public so the embedded row helper can paint it.
    enum RowGlyph { GlyphDot, GlyphChevron, GlyphStar, GlyphNone };

signals:
    // kind is "5h" or "weekly"; from rate_limit_event.
    void usageChanged(const QString &kind, const QString &text, int percent);
    // Running totals as the session spends them (assistant usage + result).
    void statsChanged(qint64 tokens, double costUsd);
    // The user answered an AskUserQuestion clarifying-question card. toolUseId is
    // the pending tool_use to satisfy; answer is the assembled reply text. The
    // host sends it back to the CLI, while sensitive keeps secrets out of the
    // persisted transcript.
    void questionAnswered(const QString &toolUseId, const QString &answer,
                          bool sensitive);
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

    // Speak Codex's dialect instead of Claude Code's (adhoc #34). The two CLIs
    // narrate the same work differently, and a Codex run rendered in Claude's
    // idiom reads wrong: Codex says "Ran <command>" with the command inline and
    // an exit= line over its output, folds a burst of file reads/searches into a
    // single "Explored" block, titles a write "Edited <path>", and ticks
    // "Working (1m 00s)" while a turn runs. Set from the session's provider
    // before its events are replayed or streamed.
    void setCodexStyle(bool on);

    // Host-supplied context for the "session started" divider (adhoc #9): the
    // worktree branch the run works on, the permission mode it was launched
    // under ("Auto" / "Plan" / …) and its reasoning strength ("high" / "low").
    // Only the CLI's own model and cwd ride in the init event, so the host sets
    // this from the AgentSession before the session's events are replayed or
    // streamed. Empty parts are omitted; an empty mode falls back to the
    // permissionMode the event itself reports (surfaced external sessions).
    void setSessionContext(const QString &branch, const QString &mode,
                           const QString &strength);

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
    // Add a row to the transcript. nodeColor tints the leading glyph; bandColor,
    // when set, paints a full-width background band behind the row (the user
    // turn's grey prompt band). Returns the row wrapper so live rows (the
    // activity ticker) can be removed.
    QWidget *addRow(QWidget *card, const QString &nodeColor = QString(),
                    RowGlyph glyph = GlyphDot,
                    const QString &bandColor = QString());
    // The whimsical "what it's doing" ticker shown while the agent is working.
    void ensureActivity();
    void clearActivity();
    void cycleActivityWord();
    void smoothScrollTo(int value);
    void fadeIn(QWidget *card);
    QString accentFor(const QString &toolName) const;

    Collapsible *makeCollapsible(const QString &header, QWidget *body,
                                 bool expanded);
    void addAssistantBlocks(const QJsonObject &message);
    // Plain (un-boxed) assistant prose, like the Claude Code conversation view.
    // Returns true if the text also read as an inline clarifying question (see
    // parseInlineChoices), so the caller can treat the turn the same as a real
    // AskUserQuestion for the activity-ticker state.
    bool addAssistantText(const QString &markdown);
    // A "Name(args)" tool header line (the row's gutter supplies the ● glyph).
    // bareSubtitle drops the parentheses, for Codex's "Ran git status" /
    // "Edited src/foo.cpp" phrasing; html passes the subtitle through as markup
    // (the shell-highlighted command line) instead of escaping it.
    QWidget *dotHeader(const QString &name, const QString &subtitle,
                       bool bareSubtitle = false, bool html = false);
    // The CLI's "  ⎿  result" shape: an L-connector gutter beside the content.
    // glyph overrides the connector — Codex hangs a command's remaining script
    // lines off a "│" and saves the "└" for the block that closes the card.
    QWidget *connectorRow(QWidget *content, const QString &glyph = QString());
    // Monospace content with no panel background (it hangs off a tool header).
    QWidget *makeMono(const QString &text, bool collapsedIfLong);

    // Live "thinking" lifecycle.
    void ensureLiveThinking();
    void setThinkingTokens(int tokens);
    void appendThinkingDelta(const QString &text);
    void finalizeThinking(const QString &fullText);

    void addToolUse(const QString &id, const QString &name,
                    const QJsonObject &input);
    void addToolResult(const QString &id, const QString &text, bool isError);
    void appendToolOutput(const QString &id, const QString &text);
    void appendAgentText(const QString &id, const QString &text);
    void completeAgentText(const QString &id, const QString &text);
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

    // ---- Codex CLI dialect (see setCodexStyle) -----------------------------
    struct ToolCard; // defined among the members below
    // The one block hanging off a "Ran" card, created on first use: the exit=
    // line and the command's output share it, so the card closes with a single
    // "└" the way the CLI draws it.
    QVBoxLayout *codexResultColumn(ToolCard &tc);
    // Fold this tool call into the open "Explored" block (opening one if the
    // previous row isn't already a block), Codex's summary of a burst of
    // exploration. Returns false when the call isn't read-only.
    bool addCodexExplore(const QString &id, const QString &name,
                         const QJsonObject &input);
    // The "Ran <command>" card: the first command line in the header, any
    // further script lines under it (capped like the CLI's "… +N lines"), and
    // the exit code over the output once the result lands.
    void addCodexCommand(const QString &id, const QString &command);
    // The finished command's result: an exit= line (the transport folds the exit
    // code onto the end of the output) above a peek of what it printed.
    void addCodexCommandResult(const QString &id, const QString &text,
                               bool isError);
    // One command line as markup: the leading word of each pipeline stage and
    // the shell operators tinted, flags picked out, everything else plain.
    QString codexCommandHtml(const QString &line) const;
    // A muted "… +N lines" note, the CLI's elision marker.
    QWidget *codexMoreLines(int n);

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
    qint64 m_activityStartMs = 0;         // Codex ticks elapsed, not gerunds

    // ---- Codex dialect state (see setCodexStyle) ---------------------------
    bool m_codexStyle = false;
    // The "Explored" block still accepting lines: it stays open only while it
    // is the newest row, so addRow() closes it for anything else that lands.
    QPointer<QWidget> m_exploreBlock;
    QVBoxLayout *m_exploreLines = nullptr;
    bool m_keepExplore = false; // addRow() guard: this row IS the block/ticker

    // ---- incremental "load earlier" state (don't-truncate-the-transcript) --
    // While >= 0, addRow() inserts rows at this column index instead of the
    // usual append-before-the-spacer position, and advances it — so a batch of
    // earlier events lands, in order, above whatever was previously first.
    int m_prependAt = -1;
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
        QVBoxLayout *io = nullptr;   // column: header, body, then ⎿ result rows
        bool hasResult = false;      // a result row was appended
        // Codex "Ran" card: the result opens with an exit= line (parsed off the
        // trailing "Exit code: N" the transport appends) above the output.
        bool codexCommand = false;
        // Codex "Explored" entry: the call is a line in a shared block rather
        // than a card of its own, so its output is dropped — but a failure
        // still has to show, and it shows on that line.
        QPointer<QLabel> exploreLine;
        // Read/Grep/Glob: the header already names the file or pattern, so the
        // (often huge) raw result folds down to a muted "N lines" note.
        bool summarizeResult = false;
        // TodoWrite: the "Todos have been modified successfully" ack is noise —
        // the rendered checklist is the content.
        bool suppressResult = false;
        QPointer<QLabel> liveOutput; // incrementally streamed command output
        QString liveOutputText;
        QVBoxLayout *codexResult = nullptr; // see codexResultColumn()
    };
    QHash<QString, ToolCard> m_toolCards;
    QHash<QString, QPointer<QLabel>> m_liveAgentText;
    QHash<QString, QString> m_liveAgentTextValue;

    // Live AskUserQuestion cards, keyed by their tool_use id: the button area to
    // lock once answered and the status line that shows the chosen reply.
    struct AskCard {
        QPointer<QWidget> buttons; // disabled on answer
        QPointer<QLabel> status;   // "✓ You answered: …"
        bool sensitive = false;    // never reveal or persist secret input
    };
    QHash<QString, AskCard> m_askCards;
    // The single most-recently-added, still-unanswered inline-choice card (see
    // addInlineChoices), if any — locked the moment the conversation moves on
    // (a new assistant turn or a user reply arrives).
    QPointer<QWidget> m_openInlineChoices;

    Collapsible *m_liveThinking = nullptr;
    QLabel *m_thinkingBody = nullptr;
    QString m_thinkingText;
    QString m_lastFinalizedThinkingText;
    int m_thinkingTokens = 0;
    qint64 m_thinkingStartMs = 0; // wall-clock start, for "Thought for Ns"

    QPropertyAnimation *m_scrollAnim = nullptr; // smooth scrolling
    qint64 m_totalTokens = 0;
    double m_totalCost = 0.0;
    bool m_bulkPopulate = false; // see setBulkPopulate()
    // see setSessionContext(); folded into the "session started" divider
    QString m_ctxBranch, m_ctxMode, m_ctxStrength;

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
