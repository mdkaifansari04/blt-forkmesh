#pragma once

#include <QJsonObject>
#include <QList>
#include <QString>
#include <QStringList>

class ForkMeshIdentity;

// A pull request: a signed proposal to merge `head` into `base`, carrying a
// unified diff/patch. Lives in the repo's pulls/ folder; submitted cross-node
// through the relay inbox (like issue submissions).
struct PullRequest {
    int number = 0;
    QString title;
    QString description;
    QString base;
    QString head;
    QString status = "open"; // open | merged | closed
    qint64 ts = 0;
    QString author;          // signer pubkey (base64url)
    QString authorName;
    QString sig;
    QString patch;           // unified diff (from changes.patch)
    int filesChanged = 0;
    int additions = 0;
    int deletions = 0;

    QJsonObject toJson() const; // wire format for the relay inbox (includes patch)
    static PullRequest fromJson(const QJsonObject &obj);
};

// Repo-scoped pull-request store backed by the on-disk pulls/ folder. Mirrors
// IssueStore: read/write/commit for repos with a working tree, read-only via the
// bare mirror otherwise.
class PullStore
{
public:
    PullStore(QString workTreePath, QString mirrorPath,
              const ForkMeshIdentity *identity, QString authorName = QString());

    bool canWrite() const; // has a working tree (can author/merge)

    QList<PullRequest> loadAll(QString *error = nullptr) const;

    // Owner-side: create a PR locally from an already-computed diff.
    int createPull(const QString &title, const QString &description,
                   const QString &base, const QString &head, const QString &patch,
                   QString *error = nullptr);
    bool setStatus(int number, const QString &status, QString *error = nullptr);
    bool isBranchBehindBase(int number, bool *behind, QString *error = nullptr) const;
    bool updateBranchFromBase(int number, QString *error = nullptr);
    // Apply the PR's patch into the working tree, commit, mark merged.
    bool mergePull(int number, QString *error = nullptr);
    // Merge a signed PR received from the relay inbox into pulls/.
    bool applyRemotePull(const PullRequest &pr, QString *error = nullptr);

    // Sign a PR with the node identity (fills author/authorName/ts/sig). The
    // signature commits to title/base/head/patch (not the number, which the
    // owner assigns on merge).
    PullRequest makeSignedPull(PullRequest pr) const;
    static QByteArray canonicalString(const PullRequest &pr);

    // Count files/additions/deletions from a unified diff.
    static void computeStats(PullRequest &pr);

private:
    QString pullsDir() const;
    QString pullDir(int number) const;
    int nextNumber() const;
    bool writePull(const PullRequest &pr, QString *error) const;
    bool readPull(int number, PullRequest &out) const;
    bool commit(const QString &message, QString *error) const;
    QString mirrorRef() const;
    QByteArray showFromMirror(const QString &repoRelPath, bool *ok) const;
    QList<PullRequest> loadFromMirror(QString *error) const;

    QString m_workTree;
    QString m_mirror;
    const ForkMeshIdentity *m_identity;
    QString m_authorName;
};
