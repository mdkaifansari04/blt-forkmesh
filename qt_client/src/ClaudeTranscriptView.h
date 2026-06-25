#pragma once

#include <QHash>
#include <QJsonObject>
#include <QScrollArea>
#include <QString>

class QVBoxLayout;
class QWidget;
class QLabel;
class QPropertyAnimation;
class Collapsible;

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

private:
    void applyScheme();
    void addRow(QWidget *card, bool animate = true);
    bool isAtBottom() const;
    void scrollToBottomSoon();
    void smoothScrollTo(int value);
    void fadeIn(QWidget *card);
    QString accentFor(const QString &toolName) const;

    QWidget *makeBubble(const QString &title, const QString &markdown,
                        const QString &accent);
    Collapsible *makeCollapsible(const QString &header, QWidget *body,
                                 bool expanded);
    void addAssistantBlocks(const QJsonObject &message);

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

    struct ToolCard {
        Collapsible *card = nullptr;
        QVBoxLayout *body = nullptr; // where the tool_result gets appended
    };
    QHash<QString, ToolCard> m_toolCards;

    Collapsible *m_liveThinking = nullptr;
    QLabel *m_thinkingBody = nullptr;
    QString m_thinkingText;
    int m_thinkingTokens = 0;

    QPropertyAnimation *m_scrollAnim = nullptr; // smooth scrolling
    qint64 m_totalTokens = 0;
    double m_totalCost = 0.0;
};
