#pragma once

#include <QJsonObject>
#include <QList>
#include <QString>
#include <QStringList>

class ForkMeshIdentity;





struct PullEvent {
    QString type;
    QString id;
    QString author;
    QString authorName;
    qint64 ts = 0;
    QString body;
    QString state;


    QString path;
    QString side;
    int line = 0;


    QString threadId;
    QString parentId;
    int lineStart = 0;
    int lineEnd = 0;
    QString suggestionPatch;
    QString targetPath;
    QString appliedCommit;
    QString sig;

    QJsonObject toJson() const;
    static PullEvent fromJson(const QJsonObject &obj);
};




struct PullRequest {
    int number = 0;
    QString title;
    QString description;
    QString base;
    QString head;
    QString status = "open";
    qint64 ts = 0;
    QString author;
    QString authorName;
    QString sig;
    QString patch;




    QString commits;





    bool branchBacked = false;



    QString creationBaseOid;
    QString creationHeadOid;



    QString mergeBase;
    QString mergeHead;
    int filesChanged = 0;
    int additions = 0;
    int deletions = 0;
    QList<PullEvent> events;

    QJsonObject toJson() const;
    static PullRequest fromJson(const QJsonObject &obj);




    QString reviewSummary() const;



    int independentApprovalCount() const;
    bool hasIndependentChangesRequested() const;
    bool independentReviewGateSatisfied() const;
};




class PullStore
{
public:
    PullStore(QString workTreePath, QString mirrorPath,
              const ForkMeshIdentity *identity, QString authorName = QString());

    bool canWrite() const;

    QList<PullRequest> loadAll(QString *error = nullptr) const;
    QList<PullRequest> loadAllStrict(QString *error = nullptr) const;
    QList<PullRequest> loadAllStrictAtRef(const QString &ref,
                                          QString *error = nullptr) const;








    int createPull(const QString &title, const QString &description,
                   const QString &base, const QString &head, const QString &patch,
                   const QString &commits, bool branchBacked = false,
                   QString *error = nullptr);
    bool setStatus(int number, const QString &status, QString *error = nullptr);




    bool isBranchBehindBase(int number, bool *behind, QString *error = nullptr,
                            int *behindCount = nullptr) const;
    bool updateBranchFromBase(int number, QString *error = nullptr);

    bool mergePull(int number, QString *error = nullptr,
                   bool requirePeerReview = true);








    bool checkMergeable(int number, bool *clean,
                        QStringList *conflictFiles = nullptr,
                        QString *error = nullptr,
                        bool keepGuiAlive = false) const;




    QString baseTip() const;














    bool startConflictMerge(int number, QStringList *conflicted,
                            bool *resolvedClean, QString *error = nullptr);



    bool startConflictAgentEdit(int number, QStringList *conflicted,
                                bool *resolvedClean,
                                QString *error = nullptr);
    bool finishConflictMerge(int number, QString *error = nullptr);


    void abortConflictMerge();

    bool conflictMergeInProgress() const;









    bool startPullFileEdit(int number, const QString &relPath, QString *content,
                           QString *error = nullptr);
    bool finishPullFileEdit(int number, const QString &relPath,
                            const QString &content, QString *error = nullptr);




    bool deletePullFile(int number, const QString &relPath, QString *error = nullptr);













    bool startPullAgentEdit(int number, QString *error = nullptr);
    bool finishPullAgentEdit(int number, const QString &commitMsg,
                             QString *error = nullptr);


    QString agentEditWorkTree() const;


    static QString syntheticMbox(const PullRequest &pr);


    bool applyRemotePull(const PullRequest &pr, QString *error = nullptr);




    bool deletePull(int number, bool rewriteHistory, QString *error = nullptr);



    bool addComment(int number, const QString &body, QString *error = nullptr);
    bool addReview(int number, const QString &state, const QString &body,
                   QString *error = nullptr);

    bool addLineComment(int number, const QString &path, const QString &side,
                        int line, const QString &body, QString *error = nullptr);
    bool addThreadComment(int number, const QString &path, const QString &side,
                          int lineStart, int lineEnd, const QString &body,
                          const QString &suggestionPatch = QString(),
                          QString *error = nullptr);
    bool addThreadReply(int number, const QString &threadId,
                        const QString &parentId, const QString &body,
                        QString *error = nullptr);
    bool setThreadState(int number, const QString &threadId, const QString &state,
                        const QString &body = QString(),
                        QString *error = nullptr);
    bool setSuggestionState(int number, const QString &threadId,
                            const QString &state,
                            const QString &appliedCommit = QString(),
                            const QString &body = QString(),
                            QString *error = nullptr);


    bool applyRemoteEvent(int number, const PullEvent &ev, QString *error = nullptr);




    PullEvent makeSignedEvent(int number, PullEvent ev) const;
    static QByteArray canonicalString(int number, const PullEvent &ev);
    static QString contentForSigning(const PullEvent &ev);




    PullRequest makeSignedPull(PullRequest pr) const;
    static QByteArray canonicalString(const PullRequest &pr);
    static QByteArray legacyCanonicalString(const PullRequest &pr);


    static void computeStats(PullRequest &pr);




    QString metaWorkTree() const;

private:





    bool beginPullBranch(int number, QStringList *conflicted, bool *cleanApply,
                         QString *error);
    bool finalizeOnPullBranch(int number, const QString &commitMsg,
                              QString *error);





    bool beginPullEditWorkTree(int number, QString *error);
    bool beginPullConflictWorkTree(int number, QStringList *conflicted,
                                   bool *cleanApply, QString *error);
    void discardPullEditWorkTree();

    bool moveBranchToEditTip(QString *error);

    QString pullsDir() const;
    QString pullDir(int number) const;





    bool materializePullRef(const PullRequest &pr, QString *error) const;
    int nextNumber() const;
    bool writePull(const PullRequest &pr, QString *error) const;
    bool readPull(int number, PullRequest &out) const;

    QList<PullEvent> readEvents(int number) const;
    int nextEventIndex(int number) const;
    bool writeEventFile(int number, int index, const PullEvent &ev,
                        QString *error) const;
    bool appendEvent(int number, const PullEvent &ev, const QString &commitMsg,
                     QString *error);
    bool commit(const QString &message, QString *error) const;
    QString mirrorRef() const;
    QByteArray showFromMirror(const QString &repoRelPath, bool *ok,
                              const QString &refOverride = QString()) const;
    QList<PullRequest> loadFromMirror(
        QString *error, bool strict = false,
        const QString &refOverride = QString()) const;

    QString m_workTree;
    QString m_mirror;
    const ForkMeshIdentity *m_identity;
    QString m_authorName;





    QString m_amBranch;
    QString m_amBase;
    QString m_amRestoreRef;




    QString m_editWorkTree;
};
