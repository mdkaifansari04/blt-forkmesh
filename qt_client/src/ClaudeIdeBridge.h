#pragma once

#include <QHash>
#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QStringList>

class QTcpServer;
class QTcpSocket;

// Makes ForkMesh *be* the IDE that the `claude` CLI connects to, without
// embedding VS Code. Anthropic's editor integration is a thin shell over the
// local CLI: the IDE runs a localhost WebSocket server speaking the MCP variant
// of JSON-RPC 2.0, advertises it via a lockfile at ~/.claude/ide/<port>.lock,
// and the CLI auto-connects when CLAUDE_CODE_SSE_PORT / ENABLE_IDE_INTEGRATION
// are set in its environment. The IDE acts as the MCP *server*; the CLI is the
// client and invokes tools (openDiff, getCurrentSelection, ...) via tools/call.
//
// To avoid a new system dependency (qt6-websockets-dev), the WebSocket transport
// is implemented directly on top of Qt6::Network (RFC 6455 handshake + framing),
// which the app already links. See PROTOCOL.md of coder/claudecode.nvim, the
// clean-room reference this follows.
class ClaudeIdeBridge : public QObject
{
    Q_OBJECT
public:
    explicit ClaudeIdeBridge(QObject *parent = nullptr);
    ~ClaudeIdeBridge() override;

    // Start listening on 127.0.0.1:<ephemeral> and write the lockfile pointing at
    // workspaceFolder. Idempotent: if already listening, just updates the
    // workspace folder. Returns true once a port is bound.
    bool start(const QString &workspaceFolder);
    void stop();
    bool isListening() const;
    quint16 port() const { return m_port; }

    // "KEY=VALUE" entries to inject when launching the CLI so it auto-connects.
    // Empty when not listening.
    QStringList env() const;

    void setWorkspaceFolder(const QString &path);

    // --- editor state the read-only tools answer from ---------------------
    // The host keeps these current so getCurrentSelection/getOpenEditors return
    // something truthful for an app that is not a full code editor.
    void setActiveFile(const QString &filePath); // "" clears
    void setSelection(const QString &filePath, const QString &text, int startLine,
                      int startChar, int endLine, int endChar);
    void clearSelection();

    // --- editor -> CLI notifications --------------------------------------
    void notifySelectionChanged();
    // "Send selection to Claude": pushes an @-mention of a file/line range.
    void notifyAtMentioned(const QString &filePath, int lineStart, int lineEnd);

    // --- openDiff resolution ----------------------------------------------
    // The host shows a diff for openDiffRequested, then calls this with the
    // outcome. On accept the bridge writes finalContents to the file and replies
    // FILE_SAVED; on reject it replies DIFF_REJECTED.
    void resolveDiff(const QString &tabName, bool accepted,
                     const QString &finalContents);

signals:
    // The CLI proposes a change; the host should display a diff and call
    // resolveDiff(tabName, ...). This is a blocking tool: the CLI waits for the
    // reply until the user acts.
    void openDiffRequested(const QString &tabName, const QString &oldPath,
                           const QString &newPath, const QString &newContents);
    void openFileRequested(const QString &filePath);
    void closeAllDiffTabsRequested();
    void clientConnected();
    void clientDisconnected();
    void log(const QString &line); // diagnostic trace, surfaced into the agent log

private slots:
    void onNewConnection();

private:
    struct Conn {
        QTcpSocket *sock = nullptr;
        bool upgraded = false;
        QByteArray buf;        // raw bytes pending parse (handshake or frames)
        QByteArray fragment;   // reassembled payload across continuation frames
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

    // JSON-RPC
    void dispatch(QTcpSocket *sock, const QJsonObject &msg);
    void handleToolCall(QTcpSocket *sock, const QJsonValue &id, const QString &name,
                        const QJsonObject &args);
    void sendResult(QTcpSocket *sock, const QJsonValue &id, const QJsonObject &result);
    void sendError(QTcpSocket *sock, const QJsonValue &id, int code,
                   const QString &message);
    void sendNotification(const QString &method, const QJsonObject &params);
    QJsonObject toolDescriptors() const;     // tools/list payload
    static QJsonObject mcpText(const QString &text); // {content:[{type,text}]}
    static QJsonObject mcpText(const QStringList &texts);

    void writeLockfile();
    void removeLockfile();
    QString lockfilePath() const;

    QTcpServer *m_server = nullptr;
    quint16 m_port = 0;
    QString m_authToken;
    QString m_workspaceFolder;
    QHash<QTcpSocket *, Conn> m_conns;

    // active editor surface (best-effort for a non-editor app)
    QString m_activeFile;
    QString m_selFile;
    QString m_selText;
    int m_selStartLine = 0, m_selStartChar = 0, m_selEndLine = 0, m_selEndChar = 0;
    bool m_hasSelection = false;

    QHash<QString, PendingDiff> m_pendingDiffs; // tab_name -> awaiting resolution
};
