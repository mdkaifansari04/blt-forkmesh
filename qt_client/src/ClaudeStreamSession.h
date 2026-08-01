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
               int memoryLimitMb = 0);

    void sendUserText(const QString &text);





    void sendToolResult(const QString &toolUseId, const QString &content);
    void stop();
    bool running() const;



    qint64 processId() const;

signals:
    void started();
    void finished(int exitCode);
    void event(const QJsonObject &ev);
    void rawLine(const QString &line);
    void stderrText(const QString &text);

private:
    void onStdout();
    void onStderr();
    void writeUserTurn(const QString &text);
    void writeLine(const QJsonObject &msg);

    QProcess *m_proc = nullptr;
    QByteArray m_buf;
};
