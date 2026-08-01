#pragma once

#include <QHash>
#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QStringList>

class QTcpServer;
class QTcpSocket;













class ClaudeIdeBridge : public QObject
{
    Q_OBJECT
public:
    explicit ClaudeIdeBridge(QObject *parent = nullptr);
    ~ClaudeIdeBridge() override;




    bool start(const QString &workspaceFolder);
    void stop();
    bool isListening() const;
    quint16 port() const { return m_port; }



    QStringList env() const;

    void setWorkspaceFolder(const QString &path);




    void setActiveFile(const QString &filePath);
    void setSelection(const QString &filePath, const QString &text, int startLine,
                      int startChar, int endLine, int endChar);
    void clearSelection();


    void notifySelectionChanged();

    void notifyAtMentioned(const QString &filePath, int lineStart, int lineEnd);





    void resolveDiff(const QString &tabName, bool accepted,
                     const QString &finalContents);

signals:



    void openDiffRequested(const QString &tabName, const QString &oldPath,
                           const QString &newPath, const QString &newContents);
    void openFileRequested(const QString &filePath);
    void closeAllDiffTabsRequested();
    void clientConnected();
    void clientDisconnected();
    void log(const QString &line);

private slots:
    void onNewConnection();

private:
    struct Conn {
        QTcpSocket *sock = nullptr;
        bool upgraded = false;
        QByteArray buf;
        QByteArray fragment;
        int fragmentOpcode = 0;
    };
    struct PendingDiff {
        QTcpSocket *sock = nullptr;
        QJsonValue id;
        QString oldPath;
        QString newPath;
    };

    void onReadyRead(QTcpSocket *sock);
    void onDisconnected(QTcpSocket *sock);
    bool tryHandshake(Conn &c);
    void processFrames(QTcpSocket *sock);
    void sendText(QTcpSocket *sock, const QByteArray &payload);
    void sendClose(QTcpSocket *sock);


    void dispatch(QTcpSocket *sock, const QJsonObject &msg);
    void handleToolCall(QTcpSocket *sock, const QJsonValue &id, const QString &name,
                        const QJsonObject &args);
    void sendResult(QTcpSocket *sock, const QJsonValue &id, const QJsonObject &result);
    void sendError(QTcpSocket *sock, const QJsonValue &id, int code,
                   const QString &message);
    void sendNotification(const QString &method, const QJsonObject &params);
    QJsonObject toolDescriptors() const;
    static QJsonObject mcpText(const QString &text);
    static QJsonObject mcpText(const QStringList &texts);

    void writeLockfile();
    void removeLockfile();
    QString lockfilePath() const;

    QTcpServer *m_server = nullptr;
    quint16 m_port = 0;
    QString m_authToken;
    QString m_workspaceFolder;
    QHash<QTcpSocket *, Conn> m_conns;


    QString m_activeFile;
    QString m_selFile;
    QString m_selText;
    int m_selStartLine = 0, m_selStartChar = 0, m_selEndLine = 0, m_selEndChar = 0;
    bool m_hasSelection = false;

    QHash<QString, PendingDiff> m_pendingDiffs;
};
