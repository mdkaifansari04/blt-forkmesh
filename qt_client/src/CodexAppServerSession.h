#pragma once

#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QObject>
#include <QSet>
#include <QString>
#include <QStringList>

class QProcess;

// Owns a Codex app-server process and adapts its JSONL protocol to the
// Claude-shaped events consumed by ForkMesh's native transcript.
class CodexAppServerSession : public QObject
{
    Q_OBJECT
public:
    explicit CodexAppServerSession(QObject *parent = nullptr);
    ~CodexAppServerSession() override;

    // Launch `codex app-server` in cwd. extraEnv entries use KEY=VALUE to set
    // and KEY to unset, matching the embedded terminal's environment handling.
    // memoryLimitMb > 0 jails the launch (adhoc #236): the shell caps the
    // process tree's data memory at that many MB before exec'ing codex. Only
    // applies to the default bash launch — an injected setAppServerCommand
    // program (tests) runs as-is.
    void start(const QString &cwd, const QStringList &extraEnv,
               const QString &initialPrompt,
               const QString &resumeThreadId = QString(),
               const QString &model = QString(),
               const QString &mode = QString(),
               const QString &effort = QString(),
               int memoryLimitMb = 0);
    void sendUserText(const QString &text);
    void respondToRequest(const QString &token, const QString &answer);
    void setTurnOptions(const QString &model, const QString &mode,
                        const QString &effort);
    void interrupt();
    void stop();
    bool running() const;
    // PID of the running app-server (0 when not running), so the UI can count
    // the build processes the agent spawned below it (adhoc #57).
    qint64 processId() const;
    // Whether a turn is actively executing right now. The app-server process
    // stays alive between turns, so running() alone can't tell "busy" from
    // "idle waiting for the next turn" — this flips true on turn/started and
    // false on turn/completed (adhoc #157).
    bool turnActive() const { return m_turnActive; }
    QString threadId() const;

    // Primarily useful for deterministic protocol tests and packaged Codex
    // binaries. Empty values restore the default `codex app-server` command.
    void setAppServerCommand(const QString &program,
                             const QStringList &arguments = QStringList());

signals:
    void started();
    void finished(int exitCode);
    void event(const QJsonObject &normalized);
    void rawLine(const QString &line);
    void stderrText(const QString &text);
    void modelsListed(const QJsonArray &models);

private:
    struct PendingServerRequest {
        QJsonValue rpcId;
        QString method;
        QJsonObject params;
        QStringList questionIds;
        QHash<QString, QString> questionIdsByText;
        QHash<QString, QJsonValue> choices;
    };

    void resetProtocolState();
    void onProcessStarted();
    void onProcessFinished(int exitCode);
    void onStdout(bool flushRemainder = false);
    void onStderr();
    void handleLine(const QByteArray &line);
    void handleMessage(const QJsonObject &message);
    void handleResponse(const QJsonObject &message);
    void handleNotification(const QString &method, const QJsonObject &params);
    void handleServerRequest(const QJsonValue &id, const QString &method,
                             const QJsonObject &params);

    qint64 sendRequest(const QString &method, const QJsonObject &params);
    void sendNotification(const QString &method, const QJsonObject &params);
    void sendObject(const QJsonObject &message);
    void sendThreadRequest();
    void acceptThread(const QJsonObject &result);
    void beginTurn(const QString &text);
    void flushQueuedUserText();
    QJsonArray userInputs(const QString &text) const;

    void emitSystemInit();
    void emitLocalNotice(const QString &level, const QString &text);
    void emitAgentDelta(const QJsonObject &params);
    void emitReasoningDelta(const QJsonObject &params);
    void emitToolDelta(const QString &method, const QJsonObject &params);
    void emitItemStarted(const QJsonObject &item);
    void emitItemCompleted(const QJsonObject &item);
    void emitSpecialItemNotice(const QJsonObject &item);
    void emitToolUse(const QJsonObject &item);
    void emitToolResult(const QJsonObject &item);
    void finishSyntheticTools(const QString &turnId);
    void emitTurnResult(const QJsonObject &params);
    void emitUsage(const QJsonObject &params);

    static bool isToolItem(const QString &type);
    static QString toolName(const QJsonObject &item);
    static QJsonObject toolInput(const QJsonObject &item);
    static QString toolOutput(const QJsonObject &item);
    static bool toolFailed(const QJsonObject &item);
    static QString messageText(const QJsonValue &value);
    static QString decisionLabel(const QJsonValue &decision);

    QProcess *m_proc = nullptr;
    QByteArray m_stdoutBuffer;
    qint64 m_nextRequestId = 1;
    quint64 m_nextQuestionToken = 1;
    QHash<qint64, QString> m_pendingCalls;
    QHash<qint64, QString> m_pendingSteerTexts;
    QHash<QString, PendingServerRequest> m_pendingServerRequests;
    QSet<QString> m_toolUses;
    QSet<QString> m_toolResults;
    QSet<QString> m_reasoningDeltas;
    QSet<QString> m_noticeItems;
    QSet<QString> m_resultTurns;
    QHash<QString, QJsonObject> m_syntheticTools;

    QString m_program;
    QStringList m_arguments;
    QString m_cwd;
    QString m_initialPrompt;
    QString m_resumeThreadId;
    QString m_model;
    QString m_effectiveModel;
    QString m_mode;
    QString m_effort;
    QString m_threadId;
    QString m_turnId;
    QString m_lastAgentText;
    QStringList m_queuedUserTexts;
    QJsonArray m_models;
    QJsonObject m_lastTokenUsage;
    bool m_threadReady = false;
    bool m_initialTurnSent = false;
    bool m_turnActive = false;
    bool m_systemInitEmitted = false;
};
