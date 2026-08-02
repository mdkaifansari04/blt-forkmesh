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





    void handleEvent(const QJsonObject &ev, bool countStats = true);
    void addUserTurn(const QString &text);
    void clear();



    struct Palette {
        QString canvas, surface, border, text, muted, accent, add, del, addBg,
            delBg, userBg;
    };




    enum RowGlyph { GlyphDot, GlyphChevron, GlyphStar, GlyphNone };

signals:

    void usageChanged(const QString &kind, const QString &text, int percent);

    void statsChanged(qint64 tokens, double costUsd);




    void questionAnswered(const QString &toolUseId, const QString &answer,
                          bool sensitive);





    void inlineChoiceAnswered(const QString &answer);




    void loadEarlierRequested();

protected:


    void resizeEvent(QResizeEvent *e) override;

public:


    void scrollToTop();
    void scrollToBottom();





    void jumpToBottom();

    void setSplitDiffs(bool on);

    // Why a failed `result` event failed, in words: the CLI's own message when
    // it sent one, otherwise the machine-readable subtype spelled out. Shared
    // with the host so the transcript row, the status pill and the stored
    // session error all give the same reason instead of a bare "Failed".
    static QString failureReason(const QJsonObject &ev);
    // Whether a `result` event reports a failed run: is_error, or any "error_*"
    // subtype (error_max_turns / error_during_execution / …).
    static bool resultIsError(const QJsonObject &ev);

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



    void accumulateStatsOnly(const QJsonObject &ev);





    void addSkippedNotice(int count);








    void prependEarlierEvents(const QList<QJsonObject> &events, int stillSkipped);








    int search(const QString &query);
    void searchNext();
    void searchPrev();
    void clearSearch();






    static bool parseInlineChoices(const QString &markdown, QStringList &options);

signals:
    void searchResultsChanged(int current, int total);

private:
    void applyScheme();




    QWidget *addRow(QWidget *card, const QString &nodeColor = QString(),
                    RowGlyph glyph = GlyphDot,
                    const QString &bandColor = QString());

    void ensureActivity();
    void clearActivity();
    void cycleActivityWord();
    void smoothScrollTo(int value);
    void fadeIn(QWidget *card);
    QString accentFor(const QString &toolName) const;

    Collapsible *makeCollapsible(const QString &header, QWidget *body,
                                 bool expanded);
    void addAssistantBlocks(const QJsonObject &message);




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








    void addAskUserQuestion(const QString &id, const QJsonObject &input);
    void markAskAnswered(const QString &id, const QString &answer);




    QWidget *addInlineChoices(const QStringList &options);


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
    QWidget *m_bottomSpacer = nullptr;


    bool m_stickBottom = true;
    ScrollJumpButtons *m_jumpButtons = nullptr;
    bool m_splitDiffs = false;
    QWidget *m_activity = nullptr;
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





    int m_prependAt = -1;
    QPointer<QWidget> m_skippedNotice;
    int m_skippedCount = 0;
    bool m_loadEarlierPending = false;




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


        bool suppressResult = false;
        QPointer<QLabel> liveOutput;
        QString liveOutputText;
        QVBoxLayout *codexResult = nullptr; // see codexResultColumn()
    };
    QHash<QString, ToolCard> m_toolCards;
    QHash<QString, QPointer<QLabel>> m_liveAgentText;
    QHash<QString, QString> m_liveAgentTextValue;



    struct AskCard {
        QPointer<QWidget> buttons;
        QPointer<QLabel> status;
        bool sensitive = false;
    };
    QHash<QString, AskCard> m_askCards;



    QPointer<QWidget> m_openInlineChoices;

    Collapsible *m_liveThinking = nullptr;
    QLabel *m_thinkingBody = nullptr;
    QString m_thinkingText;
    QString m_lastFinalizedThinkingText;
    int m_thinkingTokens = 0;
    qint64 m_thinkingStartMs = 0;

    QPropertyAnimation *m_scrollAnim = nullptr;
    qint64 m_totalTokens = 0;
    double m_totalCost = 0.0;
    bool m_bulkPopulate = false; // see setBulkPopulate()
    // see setSessionContext(); folded into the "session started" divider
    QString m_ctxBranch, m_ctxMode, m_ctxStrength;

    struct LabelHit {
        QPointer<QLabel> label;
        Qt::TextFormat fmt;
        QString orig;
        int count;
    };
    void rebuildSearchMatches();
    void renderSearchHighlights();
    void restoreSearchOriginals();
    void scrollToCurrentMatch();
    void stepMatch(int delta);
    QLabel *currentMatchLabel() const;
    QString highlightedTextFor(const QString &orig, Qt::TextFormat fmt,
                               int currentLocalOcc) const;
    QString m_searchQuery;
    QVector<LabelHit> m_searchLabels;
    int m_searchTotal = 0;
    int m_searchCurrent = -1;
};
