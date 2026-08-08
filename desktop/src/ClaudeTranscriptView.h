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
    // The user answered an AskUserQuestion clarifying-question card. toolUseId is
    // the pending tool_use to satisfy; answer is the assembled reply text. The
    // host sends it back to the CLI, while sensitive keeps secrets out of the
    // persisted transcript.
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

    static QString failureReason(const QJsonObject &ev);
    static bool resultIsError(const QJsonObject &ev);

    void setCodexStyle(bool on);

    // Host-supplied context for the "session started" divider: the
    // worktree branch the run works on, the permission mode it was launched
    // under ("Auto" / "Plan" / …), its reasoning strength ("high" / "low") and
    // the name of the provider account it is logged in as — the
    // CLI never says which of the configured logins is spending the tokens.
    // Only the CLI's own model and cwd ride in the init event, so the host sets
    // this from the AgentSession before the session's events are replayed or
    // streamed. Empty parts are omitted; an empty mode falls back to the
    // permissionMode the event itself reports (surfaced external sessions).
    void setSessionContext(const QString &branch, const QString &mode,
                           const QString &strength,
                           const QString &account = QString());

    void setBulkPopulate(bool on) { m_bulkPopulate = on; }
    void accumulateStatsOnly(const QJsonObject &ev);
    void addSkippedNotice(int count);
    void prependEarlierEvents(const QList<QJsonObject> &events, int stillSkipped);

    int search(const QString &query);
    void searchNext();
    void searchPrev();
    void clearSearch();

    static bool parseInlineChoices(const QString &markdown, QStringList &options);

    static QString linkifyReferences(const QString &markdown);

signals:
    void searchResultsChanged(int current, int total);
    void referenceActivated(const QString &href);

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
    void enableReferenceLinks(QLabel *label);
    QString referenceHtml(const QString &plainText) const;

    Collapsible *makeCollapsible(const QString &header, QWidget *body,
                                 bool expanded);
    void addAssistantBlocks(const QJsonObject &message);
    bool addAssistantText(const QString &markdown);
    QWidget *dotHeader(const QString &name, const QString &subtitle,
                       bool bareSubtitle = false, bool html = false);
    QWidget *connectorRow(QWidget *content, const QString &glyph = QString());
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

    struct ToolCard; // defined among the members below
    QVBoxLayout *codexResultColumn(ToolCard &tc);
    bool addCodexExplore(const QString &id, const QString &name,
                         const QJsonObject &input);
    void addCodexCommand(const QString &id, const QString &command);
    void addCodexCommandResult(const QString &id, const QString &text,
                               bool isError);
    QString codexCommandHtml(const QString &line) const;
    QWidget *codexMoreLines(int n);

    QString toolSubtitle(const QString &name, const QJsonObject &input) const;
    QWidget *toolBody(const QString &name, const QJsonObject &input);
    QWidget *makeDiff(const QString &oldText, const QString &newText);
    QWidget *makeCode(const QString &text, bool collapsedIfLong = false);

    Palette m_p;
    QWidget *m_container = nullptr;
    QVBoxLayout *m_col = nullptr;
    QWidget *m_bottomSpacer = nullptr; // small tail so the last row isn't flush
    bool m_stickBottom = true;
    ScrollJumpButtons *m_jumpButtons = nullptr; // floating ▲/▼ jump corner
    bool m_splitDiffs = false;            // side-by-side vs unified diffs
    QWidget *m_activity = nullptr;        // live "what it's doing" ticker row
    QLabel *m_activityLabel = nullptr;
    QTimer *m_activityTimer = nullptr;
    qint64 m_activityStartMs = 0;         // Codex ticks elapsed, not gerunds

    bool m_codexStyle = false;
    QPointer<QWidget> m_exploreBlock;
    QVBoxLayout *m_exploreLines = nullptr;
    bool m_keepExplore = false; // addRow() guard: this row IS the block/ticker

    int m_prependAt = -1;
    QPointer<QWidget> m_skippedNotice; // the "Load N earlier events" row, if any
    int m_skippedCount = 0;
    bool m_loadEarlierPending = false; // a request is in flight; don't re-emit
    bool m_prependCompensationPending = false;
    int m_prependOldMax = 0;
    int m_prependOldValue = 0;

    struct ToolCard {
        QVBoxLayout *io = nullptr;   // column: header, body, then ⎿ result rows
        bool hasResult = false;      // a result row was appended
        bool codexCommand = false;
        QPointer<QLabel> exploreLine;
        bool summarizeResult = false;
        bool suppressResult = false;
        QPointer<QLabel> liveOutput; // incrementally streamed command output
        QString liveOutputText;
        QVBoxLayout *codexResult = nullptr; // see codexResultColumn()
    };
    QHash<QString, ToolCard> m_toolCards;
    QHash<QString, QPointer<QLabel>> m_liveAgentText;
    QHash<QString, QString> m_liveAgentTextValue;

    struct AskCard {
        QPointer<QWidget> buttons; // disabled on answer
        QPointer<QLabel> status;   // "✓ You answered: …"
        bool sensitive = false;    // never reveal or persist secret input
    };
    QHash<QString, AskCard> m_askCards;
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
    QString m_ctxBranch, m_ctxMode, m_ctxStrength, m_ctxAccount;

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
