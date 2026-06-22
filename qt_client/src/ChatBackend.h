#pragma once

#include <QByteArray>
#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>

// One repo a node mirrors, with the HEAD that node currently holds. Advertised
// to peers so the network can show which nodes mirror a given repo and how fresh
// each one's copy is (a lagging node reveals itself by an older commit/time).
struct MirrorAdvert {
    QString ownerName;    // this node's clone identity "<account>/name" (host routing)
    // Shared upstream identity "<sourceOwner>/name" — the SAME across every node
    // mirroring the same logical repo, used to group them in the mirror-nodes
    // view. The source-of-truth node is the one whose ownerName equals its source.
    QString source;
    QString commit;       // full HEAD commit hash of the node's mirror (may be empty)
    QString branch;       // branch HEAD points to
    qint64 updatedMs = 0; // when the node last synced this repo from its source
};

// A chat participant as shown in the member list. `id` is the stable node id
// used to address direct messages.
struct MemberInfo {
    QString id;
    QString name;
    QString note;     // e.g. "(discovered)"
    bool self = false;
    bool online = false; // live link right now (green dot)
    QString solanaAddress;
    QString solanaBalance;  // reserved for a later balance indexer/API
    QString platform;    // linux | macos | windows | android | ios | web
    QString version;     // ForkMesh app version advertised by the node
    QStringList mirrors; // "owner/name" of repos this node mirrors
    // Per-repo HEAD detail for the repos in `mirrors` (same owner/name keys).
    // Carried alongside `mirrors` so older peers that only read names still work.
    QList<MirrorAdvert> mirrorDetails;
};

// A single chat message delivered to the UI. `conversation` is either a
// channel ("#general") or a direct chat keyed by the other party ("@<peerId>").
// A message may carry text, a file attachment, or both.
struct ChatMessage {
    QString id;
    QString conversation;
    QString senderId;
    QString senderName;
    QString text;
    qint64 timestampMs = 0;
    QString fileName;   // non-empty => has a file attachment
    QString fileMime;
    QByteArray fileData;
    bool self = false;
    bool edited = false;
    bool deleted = false;

    bool hasFile() const { return !fileName.isEmpty(); }
};

// Common interface for chat networking. The desktop app runs as a mesh-only
// LAN node; keeping the interface small keeps the UI decoupled from it.
class ChatBackend : public QObject
{
    Q_OBJECT
public:
    using QObject::QObject;

    virtual void sendChat(const QString &channel, const QString &text) = 0;
    // Direct (one-to-one) message to the member with the given id.
    virtual void sendDirect(const QString &targetId, const QString &text) = 0;
    // Share a file in a conversation ("#channel" or "@peerId").
    virtual void sendFile(const QString &conversation, const QString &fileName,
                          const QString &mimeType, const QByteArray &data) = 0;
    // Toggle an emoji reaction on a previously seen message.
    virtual void sendReaction(const QString &conversation, const QString &messageId,
                              const QString &emoji) = 0;
    // Edit or delete a message authored by this node.
    virtual void editMessage(const QString &conversation, const QString &messageId,
                             const QString &newText) = 0;
    virtual void deleteMessage(const QString &conversation, const QString &messageId) = 0;
    // Publish this node's avatar (PNG bytes; empty clears it).
    virtual void setAvatar(const QByteArray &pngData) = 0;
    // Update the single visible/account name used in outgoing messages.
    virtual void setUserName(const QString &name) { Q_UNUSED(name); }
    // Forget a stale member locally; a later live hello can add them again.
    virtual void forgetMember(const QString &peerId) { Q_UNUSED(peerId); }
    // Tell peers we started/stopped typing in a conversation.
    virtual void sendTyping(const QString &conversation, bool active) = 0;
    virtual void addChannel(const QString &channel) = 0;
    // Advertise to other nodes which repos this node mirrors, each with the
    // HEAD commit/branch it currently holds so peers can see mirror freshness.
    virtual void setMirroredRepos(const QList<MirrorAdvert> &repos) { Q_UNUSED(repos); }
    // Announce that this node just refreshed a repo's mirror from its source of
    // truth, so peers mirroring the same repo can be notified (and refresh).
    virtual void notifyMirrorUpdated(const QString &ownerName) { Q_UNUSED(ownerName); }
    virtual void shutdown() = 0;
    virtual QString modeName() const = 0;

signals:
    // A chat or direct message to display (includes our own messages, and
    // replayed history from peers).
    void messageArrived(const ChatMessage &message);
    // An emoji reaction was added or removed on a message.
    void reactionChanged(const QString &conversation, const QString &messageId,
                         const QString &emoji, const QString &reactorName, bool added);
    void messageEdited(const QString &conversation, const QString &messageId,
                       const QString &newText);
    void messageDeleted(const QString &conversation, const QString &messageId);
    // A peer's avatar became available or changed (PNG bytes).
    void avatarChanged(const QString &peerId, const QByteArray &pngData);
    // A peer started (active=true) or stopped typing in a conversation.
    void typingChanged(const QString &conversation, const QString &peerId,
                       const QString &peerName, bool active);
    // Diagnostic/log line (discovery, dials, errors). Shown in Settings, not chat.
    void systemMessage(const QString &text);
    // A host firewall is blocking ForkMesh's inbound ports. `displayCommand`
    // is the terminal command to fix it; `privilegedCommand` is the same
    // without sudo, for the UI to run via pkexec when the user clicks "Allow".
    void firewallBlocking(const QString &displayCommand,
                          const QString &privilegedCommand);
    void firewallHealthy();
    void channelsChanged(const QStringList &channels);
    void rosterChanged(const QList<MemberInfo> &members);
    // A peer refreshed its mirror of "owner/name" from the source of truth.
    void mirrorUpdated(const QString &ownerName, const QString &peerName);
    // Short status line for the UI.
    void statusChanged(const QString &status);
    // Unrecoverable failure; the UI returns to the setup screen.
    void fatalError(const QString &message);
};
