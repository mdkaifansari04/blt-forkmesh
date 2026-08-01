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

    QWidget *dotHeader(const QString &name, const QString &subtitle);

    QWidget *connectorRow(QWidget *content);

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





    int m_prependAt = -1;
    QPointer<QWidget> m_skippedNotice;
    int m_skippedCount = 0;
    bool m_loadEarlierPending = false;




    bool m_prependCompensationPending = false;
    int m_prependOldMax = 0;
    int m_prependOldValue = 0;

    struct ToolCard {
        QVBoxLayout *io = nullptr;
        bool hasResult = false;


        bool summarizeResult = false;


        bool suppressResult = false;
        QPointer<QLabel> liveOutput;
        QString liveOutputText;
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
    bool m_bulkPopulate = false;




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
