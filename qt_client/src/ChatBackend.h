#pragma once

#include <QByteArray>
#include <QJsonObject>
#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>




struct CommitIdentity {
    QString subject;
    QString author;
    qint64 committedAtMs = 0;
};



constexpr int kMaxCommitSubjectChars = 120;
constexpr int kMaxCommitAuthorChars = 64;




struct MirrorAdvert {
    QString ownerName;



    QString source;
    QString commit;
    QString branch;



    CommitIdentity commitIdentity;
    qint64 updatedMs = 0;
    qint64 sizeBytes = 0;
    int issueCount = -1;


    int commitCount = -1;
    int branchCount = -1;
    int pullCount = -1;
    int discussionCount = -1;
    int worktreeCount = -1;



    int artifactCount = -1;
};



struct MemberInfo {
    QString id;
    QString name;
    QString nodeName;
    QString ownerUser;





    QString accountKind;
    QString note;


    qint64 createdAtMs = 0;
    bool self = false;
    bool online = false;
    QString solanaAddress;
    QString solanaBalance;
    QString platform;
    QString version;
    QStringList mirrors;


    QList<MirrorAdvert> mirrorDetails;



    qint64 memUsedBytes = 0;
    qint64 memTotalBytes = 0;
    qint64 diskUsedBytes = 0;
    qint64 diskTotalBytes = 0;
    double cpuPercent = -1.0;
};





constexpr qint64 kChatMessageRetentionMs = qint64(7) * 24 * 60 * 60 * 1000;




struct ChatMessage {
    QString id;
    QString conversation;
    // Empty for a top-level message. A non-empty value identifies the
    // top-level message this record replies to. The wire field is `rootId`,
    // shared with web chat and World's embedded chat.
    QString threadRootId;
    QString senderId;
    QString senderName;
    QString text;
    qint64 timestampMs = 0;
    QString fileName;
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
    // Reply inside an existing channel thread. Backends that predate threads
    // can safely ignore this; ServerNode implements the shared thread-reply
    // envelope used by every first-party client.
    virtual void sendThreadReply(const QString &channel,
                                 const QString &rootMessageId,
                                 const QString &text) {
        Q_UNUSED(channel);
        Q_UNUSED(rootMessageId);
        Q_UNUSED(text);
    }
    // Direct (one-to-one) message to the member with the given id.
    virtual void sendDirect(const QString &targetId, const QString &text) = 0;

    virtual void sendFile(const QString &conversation, const QString &fileName,
                          const QString &mimeType, const QByteArray &data) = 0;

    virtual void sendReaction(const QString &conversation, const QString &messageId,
                              const QString &emoji) = 0;

    virtual void editMessage(const QString &conversation, const QString &messageId,
                             const QString &newText) = 0;
    virtual void deleteMessage(const QString &conversation, const QString &messageId) = 0;




    virtual void setAccountKind(const QString &kind) { Q_UNUSED(kind); }






    virtual void setRoomPassphrase(const QString &passphrase) { Q_UNUSED(passphrase); }



    virtual void sendBotChat(const QString &channel, const QString &text) {
        Q_UNUSED(channel);
        Q_UNUSED(text);
    }



    virtual void sendAdminDelete(const QString &conversation, const QString &messageId,
                                 qint64 ts, const QString &signature) {
        Q_UNUSED(conversation);
        Q_UNUSED(messageId);
        Q_UNUSED(ts);
        Q_UNUSED(signature);
    }


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
                                      const QString &ownerName) {
        Q_UNUSED(source);
        Q_UNUSED(ownerName);
    }


    virtual void advertiseMirrorsNow() {}




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

    void mirrorRefreshRequested(const QString &source, const QString &requesterName);


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
