#pragma once

#include "NodeDiagnostics.h"

#include <QByteArray>
#include <QJsonObject>
#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>

struct CommitIdentity {
    QString subject;           // first line of the commit message
    QString author;            // author name as recorded in the commit
    qint64 committedAtMs = 0;  // commit time; 0 = unknown/not advertised
};

constexpr int kMaxCommitSubjectChars = 120;
constexpr int kMaxCommitAuthorChars = 64;

struct MirrorAdvert {
    QString ownerName;    // this node's clone identity "<account>/name" (host routing)
    QString source;
    QString commit;       // full HEAD commit hash of the node's mirror (may be empty)
    QString branch;       // branch HEAD points to
    QString refsFingerprint;
    CommitIdentity commitIdentity;
    qint64 updatedMs = 0; // when the node last synced this repo from its source
    qint64 sizeBytes = 0; // on-disk size of this node's bare mirror (git objects)
    int issueCount = -1;  // issues this node's mirror holds; -1 = not advertised
    int commitCount = -1;     // commits on the served branch
    int branchCount = -1;     // local branches (refs/heads) the mirror holds
    int pullCount = -1;       // pull requests the mirror holds
    int discussionCount = -1; // discussions the mirror holds
    int worktreeCount = -1;   // git worktrees on this node's working copy
    int artifactCount = -1;
};

struct MemberInfo {
    QString id;
    QString name;      // chat/user display name
    QString nodeName;  // registered node account name, when known
    QString ownerUser; // user account that owns this node, when linked/known
    // "node" (default) or "user" — stamped from the sender's accountKind, so a
    // user-only profile (e.g. a bot's relayed chat, or a desktop client signed
    // in as a plain user account, not a linked node) can be told apart from a
    // real serving node. Empty for older peers that never advertised it, which
    // callers should treat the same as "node" for backward compatibility.
    QString accountKind;
    QString note;     // e.g. "(discovered)"
    qint64 createdAtMs = 0;
    bool self = false;
    bool online = false; // live link right now (green dot)
    QString solanaAddress;
    QString solanaBalance;  // reserved for a later balance indexer/API
    QString platform;    // linux | macos | windows | android | ios | web
    QString version;     // ForkMesh app version advertised by the node
    QStringList mirrors; // "owner/name" of repos this node mirrors
    QList<MirrorAdvert> mirrorDetails;
    qint64 memUsedBytes = 0;
    qint64 memTotalBytes = 0;
    qint64 diskUsedBytes = 0;
    qint64 diskTotalBytes = 0;
    double cpuPercent = -1.0; // whole-host CPU utilization, 0..100
    QList<NodeDiagnostics::Finding> diagnostics;
    qint64 diagnosticsMs = 0;
};

constexpr qint64 kChatMessageRetentionMs = qint64(7) * 24 * 60 * 60 * 1000;

struct ChatMessage {
    QString id;
    QString conversation;
    QString threadRootId;
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

class ChatBackend : public QObject
{
    Q_OBJECT
public:
    using QObject::QObject;

    virtual void sendChat(const QString &channel, const QString &text) = 0;
    virtual void sendThreadReply(const QString &channel,
                                 const QString &rootMessageId,
                                 const QString &text) {
        Q_UNUSED(channel);
        Q_UNUSED(rootMessageId);
        Q_UNUSED(text);
    }
    virtual void sendDirect(const QString &targetId, const QString &text) = 0;
    virtual void sendFile(const QString &conversation, const QString &fileName,
                          const QString &mimeType, const QByteArray &data) = 0;
    virtual void sendReaction(const QString &conversation, const QString &messageId,
                              const QString &emoji) = 0;
    virtual void editMessage(const QString &conversation, const QString &messageId,
                             const QString &newText) = 0;
    virtual void deleteMessage(const QString &conversation, const QString &messageId) = 0;
    virtual void setAccountKind(const QString &kind) { Q_UNUSED(kind); }
    // Re-key room encryption once the server-issued shared passphrase arrives.
    // The passphrase fetch is async and often resolves after the backend is
    // already connected (it starts from the first heartbeat, which fires after
    // the initial connect), so a backend seeded with a fallback key must be
    // able to switch to the real one without a reconnect. Default no-op for
    // backends without room encryption.
    virtual void setRoomPassphrase(const QString &passphrase) { Q_UNUSED(passphrase); }
    virtual void sendBotChat(const QString &channel, const QString &text) {
        Q_UNUSED(channel);
        Q_UNUSED(text);
    }
    // Admin moderation: broadcast a signed request to delete any message (the
    // signature, made by the admin's identity key over a canonical string, lets
    // every peer authenticate it). Default no-op for backends without it.
    virtual void sendAdminDelete(const QString &conversation, const QString &messageId,
                                 qint64 ts, const QString &signature) {
        Q_UNUSED(conversation);
        Q_UNUSED(messageId);
        Q_UNUSED(ts);
        Q_UNUSED(signature);
    }
    // Apply an admin delete that the caller has already verified (good signature
    // from a confirmed admin). Removes the message and emits messageDeleted.
    virtual void applyAdminDelete(const QString &conversation,
                                  const QString &messageId) {
        Q_UNUSED(conversation);
        Q_UNUSED(messageId);
    }
    virtual void setAvatar(const QByteArray &pngData) = 0;
    virtual void setUserName(const QString &name) { Q_UNUSED(name); }
    virtual void setNodeIdentity(const QString &nodeName, const QString &ownerUser) {
        Q_UNUSED(nodeName);
        Q_UNUSED(ownerUser);
    }
    virtual void forgetMember(const QString &peerId) { Q_UNUSED(peerId); }
    virtual void sendTyping(const QString &conversation, bool active) = 0;
    virtual void addChannel(const QString &channel) = 0;
    virtual void removeChannel(const QString &channel) { Q_UNUSED(channel); }
    virtual void createPrivateChannel(const QString &channel) { Q_UNUSED(channel); }
    virtual void inviteToChannel(const QString &peerId, const QString &channel) {
        Q_UNUSED(peerId);
        Q_UNUSED(channel);
    }
    virtual void setMirroredRepos(const QList<MirrorAdvert> &repos) { Q_UNUSED(repos); }
    virtual void notifyMirrorUpdated(const QString &ownerName,
                                     const QString &commit = QString())
    {
        Q_UNUSED(ownerName);
        Q_UNUSED(commit);
    }
    virtual void notifyMirrorSynced(const QString &ownerName,
                                    const QString &commit = QString())
    {
        Q_UNUSED(ownerName);
        Q_UNUSED(commit);
    }
    virtual void requestMirrorRefresh(const QString &source,
                                      const QString &ownerName,
                                      const QString &toNodeId = QString(),
                                      bool sync = false) {
        Q_UNUSED(source);
        Q_UNUSED(ownerName);
        Q_UNUSED(toNodeId);
        Q_UNUSED(sync);
    }
    virtual void advertiseMirrorsNow() {}
    // Announce that this node just opened an encrypted cove. The creator's node
    // (creatorKey) recognizes itself and raises a notification; everyone else
    // ignores it. The opener signs a canonical string so the creator can trust
    // who opened it. Ephemeral — not retained or replayed by the relay.
    virtual void notifyCoveOpened(const QString &creatorKey, const QString &coveId,
                                  const QString &coveName, const QString &openerKey,
                                  const QString &openerName, qint64 ts,
                                  const QString &signature)
    {
        Q_UNUSED(creatorKey);
        Q_UNUSED(coveId);
        Q_UNUSED(coveName);
        Q_UNUSED(openerKey);
        Q_UNUSED(openerName);
        Q_UNUSED(ts);
        Q_UNUSED(signature);
    }
    // Announce that `inviteeAccount` was just granted access to an account-scoped
    // cove. Every online node checks inviteeAccount against its own account and
    // raises a notification if it matches; everyone else ignores it. Advisory
    // only (it grants nothing by itself — the actual access grant already lives
    // in the cove's encrypted invitedAccounts list), so unlike notifyCoveOpened
    // it isn't signed.
    virtual void notifyCoveInvited(const QString &inviteeAccount, const QString &coveId,
                                   const QString &coveName, const QString &inviterName,
                                   qint64 ts)
    {
        Q_UNUSED(inviteeAccount);
        Q_UNUSED(coveId);
        Q_UNUSED(coveName);
        Q_UNUSED(inviterName);
        Q_UNUSED(ts);
    }
    // Live WebSocket / Durable Object diagnostics for the Network tab. Backends
    // without a socket return an empty list.
    virtual QList<QJsonObject> networkDiagnostics() const { return {}; }
    virtual void setNetworkAvailable(bool available) { Q_UNUSED(available); }
    virtual void shutdown() = 0;
    virtual QString modeName() const = 0;

signals:
    void messageArrived(const ChatMessage &message);
    void reactionChanged(const QString &conversation, const QString &messageId,
                         const QString &emoji, const QString &reactorName, bool added);
    void messageEdited(const QString &conversation, const QString &messageId,
                       const QString &newText);
    void messageDeleted(const QString &conversation, const QString &messageId);
    // An admin moderation delete arrived from a peer. NOT applied yet — the UI
    // must verify the signature and the sender's admin status, then call
    // applyAdminDelete() to actually remove it.
    void adminDeleteRequested(const QString &conversation, const QString &messageId,
                              const QString &adminId, const QString &adminName,
                              qint64 ts, const QString &signature);
    void avatarChanged(const QString &peerId, const QByteArray &pngData);
    void typingChanged(const QString &conversation, const QString &peerId,
                       const QString &peerName, bool active);
    void systemMessage(const QString &text);
    void firewallBlocking(const QString &displayCommand,
                          const QString &privilegedCommand);
    void firewallHealthy();
    void channelsChanged(const QStringList &channels);
    void privateChannelJoined(const QString &channel);
    void rosterChanged(const QList<MemberInfo> &members);
    void mirrorUpdated(const QString &ownerName, const QString &peerName,
                       const QString &commit);
    void mirrorSynced(const QString &ownerName, const QString &peerName,
                      const QString &commit);
    void mirrorRefreshRequested(const QString &source, const QString &requesterName,
                                bool sync);
    // A peer opened an encrypted cove. The UI verifies the opener's signature and,
    // if this node created the cove (creatorKey), raises an "opened" notification.
    void coveOpened(const QString &creatorKey, const QString &coveId,
                    const QString &coveName, const QString &openerKey,
                    const QString &openerName, qint64 ts, const QString &signature);
    void coveInvited(const QString &inviteeAccount, const QString &coveId,
                     const QString &coveName, const QString &inviterName, qint64 ts);
    void networkDiagnosticsChanged();
    void latencySampled(int ms);
    void statusChanged(const QString &status);
    void fatalError(const QString &message);
};
