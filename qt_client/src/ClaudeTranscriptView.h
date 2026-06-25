#pragma once

#include <QHash>
#include <QJsonObject>
#include <QScrollArea>
#include <QString>

class QVBoxLayout;
class QWidget;
class QLabel;

// Renders a Claude Code session as a native, extension-style chat transcript by
// consuming the stream-json events from ClaudeStreamSession: assistant text,
// collapsible "thinking" blocks, tool-call cards (Bash command, Edit/Write
// diffs, Read, TodoWrite, …) with their results attached, plus the final result
// and rate-limit (usage) banner. Tool results are matched to their call by
// tool_use_id.
class ClaudeTranscriptView : public QScrollArea
{
    Q_OBJECT
public:
    explicit ClaudeTranscriptView(QWidget *parent = nullptr);

    // Feed one parsed stream-json event (object with a "type" field).
    void handleEvent(const QJsonObject &ev);
    // Render a user turn (the initial prompt or a steered follow-up).
    void addUserTurn(const QString &text);
    void clear();

signals:
    // Surfaced from rate_limit_event so the host can show a usage banner.
    void usageChanged(const QString &text);

private:
    void addRow(QWidget *card);
    void scrollToBottom();

    QWidget *makeBubble(const QString &title, const QString &markdown,
                        const QString &accent);
    void addAssistantBlocks(const QJsonObject &message);
    void addThinking(const QString &text);
    void addToolUse(const QString &id, const QString &name,
                    const QJsonObject &input);
    void addToolResult(const QString &id, const QString &text, bool isError);
    void addResult(const QJsonObject &ev);

    // Per-tool body rendering.
    QString toolSubtitle(const QString &name, const QJsonObject &input) const;
    QWidget *toolBody(const QString &name, const QJsonObject &input);
    QWidget *makeDiff(const QString &oldText, const QString &newText);
    QWidget *makeCode(const QString &text, bool collapsedIfLong = false);

    QWidget *m_container = nullptr;
    QVBoxLayout *m_col = nullptr; // cards stacked top->bottom, stretch at the end

    struct ToolCard {
        QWidget *frame = nullptr;
        QVBoxLayout *body = nullptr; // where the tool_result gets appended
    };
    QHash<QString, ToolCard> m_toolCards; // tool_use_id -> card
};
