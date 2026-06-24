#pragma once

#include <QJsonObject>
#include <QList>
#include <QPair>
#include <QString>

class ForkMeshIdentity;

// A signed comment attached to a specific commit (by full 40-char hash). Stored
// under commits/<sha>/NNNN-comment.md, mirroring the issue/PR event layout so it
// travels with every clone and can be submitted cross-node through the relay.
struct CommitComment {
    QString id;
    QString author;     // signer pubkey (base64url)
    QString authorName;
    qint64 ts = 0;
    QString body;       // markdown
    QString sig;

    QJsonObject toJson() const; // wire format for the relay inbox
    static CommitComment fromJson(const QJsonObject &obj);
};

// Repo-scoped store for per-commit conversations, backed by the on-disk commits/
// folder. Like IssueStore/PullStore: read/write/commit for repos with a working
// tree, read-only via the bare mirror otherwise.
class CommitCommentStore
{
public:
    CommitCommentStore(QString workTreePath, QString mirrorPath,
                       const ForkMeshIdentity *identity,
                       QString authorName = QString());

    bool canWrite() const; // has a working tree (can author)

    QList<CommitComment> loadFor(const QString &sha) const;

    // Every commit that carries comments, paired with its thread. Reads the
    // working tree's commits/ folder when we can author, else the bare mirror.
    // Used to scan all commit conversations for @mentions.
    QList<QPair<QString, QList<CommitComment>>> loadAll() const;

    // Append a signed comment for a commit, then commit the commits/ folder.
    bool addComment(const QString &sha, const QString &body, QString *error = nullptr);
    // Merge a signed comment received from the relay inbox.
    bool applyRemoteComment(const QString &sha, const CommitComment &c,
                            QString *error = nullptr);

    // Sign a comment with the node identity (fills id/author/authorName/ts/sig).
    CommitComment makeSignedComment(const QString &sha, CommitComment c) const;
    // The exact bytes the signature commits to; mirrors the worker verifier.
    static QByteArray canonicalString(const QString &sha, const CommitComment &c);

private:
    QString commitsDir() const;            // <workTree>/commits
    QString commitDir(const QString &sha) const; // <workTree>/commits/<sha>
    int nextIndex(const QString &sha) const;
    bool writeComment(const QString &sha, int index, const CommitComment &c,
                      QString *error) const;
    bool commit(const QString &message, QString *error) const;

    // Read-only access from a bare mirror via `git show <ref>:commits/...`.
    QString mirrorRef() const;
    QList<CommitComment> loadFromMirror(const QString &sha) const;

    QString m_workTree;
    QString m_mirror;
    const ForkMeshIdentity *m_identity;
    QString m_authorName;
};
