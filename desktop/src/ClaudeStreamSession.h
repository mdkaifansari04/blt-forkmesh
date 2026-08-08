#pragma once

#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QStringList>

class QProcess;

class ClaudeStreamSession : public QObject
{
    Q_OBJECT
public:
    explicit ClaudeStreamSession(QObject *parent = nullptr);
    ~ClaudeStreamSession() override;

    void start(const QString &cwd, const QStringList &extraEnv,
               const QString &initialPrompt, bool skipPermissions = true,
               const QString &resumeSessionId = QString(),
               const QString &model = QString(),
               const QString &effort = QString(),
               const QString &fallbackModels = QString(),
               int memoryLimitMb = 0,
               const QString &resumeFallbackPrompt = QString());
    bool sendUserText(const QString &text);
    void sendToolResult(const QString &toolUseId, const QString &content);
    void stop();
    bool running() const;
    bool acceptsInput() const;
    qint64 processId() const;

signals:
    void started();
    void finished(int exitCode);
    void event(const QJsonObject &ev); // one parsed stream-json event
    void rawLine(const QString &line); // raw stdout line (for the debug view)
    void stderrText(const QString &text);

private:
    void launch();
    void onStdout();
    void onStderr();
    bool writeUserTurn(const QString &text);
    bool writeLine(const QJsonObject &msg);
    void reportExit(int exitCode);

    QProcess *m_proc = nullptr;
    QByteArray m_buf; // accumulates partial stdout lines
    QString m_cwd;
    QStringList m_extraEnv;
    QString m_initialPrompt;
    bool m_skipPermissions = true;
    QString m_resumeSessionId;
    QString m_model;
    QString m_effort;
    QString m_fallbackModels;
    int m_memoryLimitMb = 0;
    QString m_resumeFallbackPrompt;
    bool m_conversationOpened = false;
    bool m_resumeFallbackUsed = false; // one retry per start(), never a loop
    bool m_exitReported = false;       // one finished() per launch, never two
};
