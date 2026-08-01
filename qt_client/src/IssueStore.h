#pragma once

#include <QJsonObject>
#include <QList>
#include <QString>
#include <QStringList>

#include <functional>

class ForkMeshIdentity;




struct IssueEvent {
    QString type;
    QString id;
    QString author;
    QString authorName;
    qint64 ts = 0;
    QString title;
    QString body;
    QStringList attachments;
    QString target;
    QString status;
    QStringList labels;
    QString milestone;
    qint64 startDate = 0;
    qint64 endDate = 0;
    int priority = 0;
    int progress = 0;
    QStringList assignees;
    QString agentProvider;
    int agentSessionId = 0;
    QString agentStatus;
    bool agentCreatePr = false;
    double bountyUsd = 0.0;
    QString bountyAddress;
    QString bountyStatus;
    QString sig;

    QJsonObject toJson() const;
    static IssueEvent fromJson(const QJsonObject &obj);
};


struct Issue {
    int number = 0;
    QString title;
    QString status = "open";
    QStringList labels;
    QString milestone;
    qint64 startDate = 0;
    qint64 endDate = 0;
    int priority = 0;
    int progress = 0;
    QStringList assignees;
    qint64 createdAt = 0;
    QString author;
    QString authorName;
    int votes = 0;

    double bountyUsd = 0.0;
    QString bountyAddress;
    QString bountyStatus;
    QList<IssueEvent> events;

    QJsonObject toJson() const;
    static Issue fromJson(const QJsonObject &obj);




    bool isDeleted() const;
    bool hasUnauthorizedDeleteAttempt() const;
};




struct RemoteIssueMeta {
    QStringList labels;
    QString milestone;
    int priority = 0;
    QStringList assignees;



    bool wantsAgent = false;



    QString wantsAgentModel;



    QString wantsAgentProvider;
};








struct RemoteAttachment {
    QString name;
    QByteArray data;
};

struct IssueLabel {
    QString name;
    QString color;
};

struct IssueMilestone {
    QString title;
    qint64 due = 0;
    QString status = "open";
    QString description;
};









class IssueStore
{
public:



    IssueStore(QString workTreePath, QString mirrorPath,
               const ForkMeshIdentity *identity, QString authorName = QString());



    bool canWrite() const;




    QList<Issue> loadAll(QString *error = nullptr,
                         const std::function<void()> &tick = {}) const;
    QList<Issue> loadAllStrict(QString *error = nullptr) const;
    QList<Issue> loadAllStrictAtRef(const QString &ref,
                                    QString *error = nullptr) const;
    QList<IssueLabel> loadLabels() const;
    QList<IssueMilestone> loadMilestones() const;








    QString contentSignature() const;







    int createIssue(const QString &title, const QString &body,
                    const QStringList &labels, const QString &milestone,
                    int priority,
                    const QStringList &assignees,
                    const QStringList &attachmentSrcPaths, QString *error = nullptr,
                    Issue *createdOut = nullptr);
    int createIssue(const QString &title, const QString &body,
                    const QStringList &labels, const QString &milestone,
                    int priority,
                    const QStringList &assignees,
                    const QStringList &attachmentSrcPaths,
                    const QStringList &attachmentPlaceholders,
                    QString *error = nullptr, Issue *createdOut = nullptr);
    bool addComment(int number, const QString &body,
                    const QStringList &attachmentSrcPaths, QString *error = nullptr);
    bool addComment(int number, const QString &body,
                    const QStringList &attachmentSrcPaths,
                    const QStringList &attachmentPlaceholders,
                    QString *error = nullptr);

    bool addVote(int number, QString *error = nullptr);


    bool editEvent(int number, const QString &eventId, const QString &newBody,
                   const QStringList &keepAttachments = {},
                   const QStringList &newAttachmentSrcPaths = {},
                   QString *error = nullptr);
    bool editEvent(int number, const QString &eventId, const QString &newBody,
                   const QStringList &keepAttachments,
                   const QStringList &newAttachmentSrcPaths,
                   const QStringList &newAttachmentPlaceholders,
                   QString *error = nullptr);

    bool setTitle(int number, const QString &newTitle, QString *error = nullptr);
    bool setStatus(int number, const QString &status, QString *error = nullptr);
    bool setLabels(int number, const QStringList &labels, QString *error = nullptr);
    bool setMilestone(int number, const QString &milestone, QString *error = nullptr);

    bool setDates(int number, qint64 startDate, qint64 endDate,
                  QString *error = nullptr);
    bool setPriority(int number, int priority, QString *error = nullptr);
    bool setProgress(int number, int progress, QString *error = nullptr);


    bool setBounty(int number, double amountUsd, const QString &address,
                   const QString &status, QString *error = nullptr);
    bool setAssignees(int number, const QStringList &assignees, QString *error = nullptr);
    bool assignAgent(int number, const QString &provider, int sessionId,
                     bool createPr, const QString &status, QString *error = nullptr);
    bool deleteEvent(int number, const QString &eventId, QString *error = nullptr);




    bool tombstoneIssue(int number, QString *error = nullptr);



    bool deleteIssue(int number, QString *error = nullptr);

    bool saveLabels(const QList<IssueLabel> &labels, QString *error = nullptr);
    bool saveMilestones(const QList<IssueMilestone> &milestones, QString *error = nullptr);



    IssueEvent makeSignedEvent(int number, IssueEvent ev) const;







    bool applyRemoteEvent(int number, const IssueEvent &ev, const QString &titleIfNew,
                          QString *error = nullptr,
                          const RemoteIssueMeta &meta = {},
                          const QList<RemoteAttachment> &attachments = {});







    static QList<RemoteAttachment> readAttachmentsForRemoteSubmit(
        const QStringList &srcPaths);




    static QString substituteAttachmentPlaceholders(
        QString body, const QStringList &srcPaths, const QStringList &placeholders,
        const QStringList &attachmentNames);



    static QByteArray canonicalString(int number, const IssueEvent &ev);
    static QString contentForSigning(const IssueEvent &ev);






    static QString issueDirPath(const QString &workTree, int number);

private:
    QString issuesDir() const;
    QString issueDir(int number) const;
    QString issueFilePath(int number) const;



    void migrateLegacyLayout() const;
    bool readIssueFile(int number, Issue &out) const;
    bool writeIssueFile(const Issue &issue, QString *error) const;
    void recomputeMetadata(Issue &issue) const;
    QStringList copyAttachments(int number, const QStringList &srcPaths) const;




    void writeRemoteAttachmentFiles(int number, const QStringList &names,
                                    const QList<RemoteAttachment> &attachments) const;
    int nextNumber() const;
    bool commit(const QString &message, QString *error) const;


    QList<Issue> loadFromMirror(QString *error, bool strict = false,
                                const QString &refOverride = QString()) const;
    QByteArray showFromMirror(const QString &repoRelPath, bool *ok,
                              const QString &refOverride = QString()) const;
    QString mirrorRef() const;

    QString m_workTree;
    QString m_mirror;
    const ForkMeshIdentity *m_identity;
    QString m_authorName;



    mutable bool m_mirrorRefResolved = false;
    mutable QString m_cachedMirrorRef;
};
