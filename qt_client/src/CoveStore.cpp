#include "CoveStore.h"

#include "CoveCrypto.h"
#include "ForkMeshIdentity.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QProcess>
#include <QUuid>

namespace {

constexpr int kGitTimeoutMs = 10000;
constexpr int kCoveVersion = 1;

// Run git in `dir`, capturing stdout. Mirrors IssueStore's local helper.
bool runGit(const QString &dir, const QStringList &args, QByteArray *output = nullptr,
            QString *errText = nullptr, int timeoutMs = kGitTimeoutMs)
{
    QProcess process;
    process.start("git", QStringList{"-C", dir} + args);
    if (!process.waitForFinished(timeoutMs)) {
        if (errText)
            *errText = QStringLiteral("git timed out");
        return false;
    }
    if (output)
        *output = process.readAllStandardOutput();
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
        if (errText)
            *errText =
                QString::fromUtf8(process.readAllStandardError()).trimmed().left(200);
        return false;
    }
    return true;
}

QString newId()
{
    return QUuid::createUuid().toString(QUuid::WithoutBraces);
}

// An opaque, content-free file stem (32 hex chars). Cove files are named by this
// alone so a repo's tree/history never hints at what a cove holds — the name is
// kept inside the encrypted payload, not in the filename.
QString obscureSlug()
{
    return QUuid::createUuid().toString(QUuid::Id128);
}

// Build the on-disk envelope JSON for a cove whose cipher is already computed.
// The human-readable name is deliberately NOT stored here: it lives inside the
// encrypted payload so the repo never reveals what a cove is about. The file is
// identified on disk only by an obscure random slug.
QJsonObject toEnvelope(const Cove &cove)
{
    return {{"kind", "cove"},
            {"v", kCoveVersion},
            {"id", cove.id},
            {"creator", cove.creator},
            {"createdAtMs", double(cove.createdAtMs)},
            {"notifyOnOpen", cove.notifyOnOpen},
            {"kdf", QJsonObject{{"algo", "pbkdf2-sha256"},
                                {"rounds", cove.rounds},
                                {"salt", QString::fromLatin1(cove.salt.toBase64())}}},
            {"cipher", cove.cipher}};
}

// Parse envelope metadata (everything readable before unlock). Returns false if
// the bytes are not a recognizable cove envelope.
bool fromEnvelope(const QByteArray &bytes, Cove &out)
{
    const QJsonDocument doc = QJsonDocument::fromJson(bytes);
    if (!doc.isObject())
        return false;
    const QJsonObject obj = doc.object();
    if (obj.value("kind").toString() != "cove")
        return false;
    out.id = obj.value("id").toString();
    // Legacy coves stored the name in plaintext here; newer ones keep it inside
    // the encrypted payload (recovered by unlock()). Read it for back-compat.
    out.name = obj.value("name").toString();
    out.creator = obj.value("creator").toString();
    out.createdAtMs = qint64(obj.value("createdAtMs").toDouble());
    out.notifyOnOpen = obj.value("notifyOnOpen").toBool();
    const QJsonObject kdf = obj.value("kdf").toObject();
    out.salt = QByteArray::fromBase64(kdf.value("salt").toString().toLatin1());
    out.rounds = kdf.value("rounds").toInt();
    out.cipher = obj.value("cipher").toObject();
    return !out.id.isEmpty() && !out.salt.isEmpty() && out.rounds > 0;
}

} // namespace

// ---- CoveDocument / CoveAccessEntry JSON -----------------------------------

QJsonObject CoveDocument::toJson() const
{
    return {{"id", id}, {"name", name}, {"mime", mime}, {"body", body},
            {"updatedAtMs", double(updatedAtMs)}};
}

CoveDocument CoveDocument::fromJson(const QJsonObject &obj)
{
    CoveDocument d;
    d.id = obj.value("id").toString();
    d.name = obj.value("name").toString();
    d.mime = obj.value("mime").toString(QStringLiteral("text/markdown"));
    d.body = obj.value("body").toString();
    d.updatedAtMs = qint64(obj.value("updatedAtMs").toDouble());
    return d;
}

QJsonObject CoveAccessEntry::toJson() const
{
    return {{"who", who}, {"name", name}, {"node", node},
            {"ts", double(ts)}, {"action", action}};
}

CoveAccessEntry CoveAccessEntry::fromJson(const QJsonObject &obj)
{
    CoveAccessEntry e;
    e.who = obj.value("who").toString();
    e.name = obj.value("name").toString();
    e.node = obj.value("node").toString();
    e.ts = qint64(obj.value("ts").toDouble());
    e.action = obj.value("action").toString(QStringLiteral("open"));
    return e;
}

bool Cove::createdByMe(const ForkMeshIdentity *identity) const
{
    return identity && identity->isValid() && !creator.isEmpty() &&
           creator == identity->publicKey();
}

// ---- CoveStore -------------------------------------------------------------

CoveStore::CoveStore(QString workTreePath, QString mirrorPath,
                     const ForkMeshIdentity *identity, QString authorName)
    : m_workTree(std::move(workTreePath)), m_mirror(std::move(mirrorPath)),
      m_identity(identity), m_authorName(std::move(authorName))
{
}

bool CoveStore::canWrite() const
{
    if (m_workTree.isEmpty() || !m_identity || !m_identity->isValid())
        return false;
    return QFileInfo::exists(m_workTree + "/.git");
}

QString CoveStore::covesDir() const { return m_workTree + "/" + covesDirRel(); }

QString CoveStore::mirrorRef() const
{
    QByteArray output;
    if (runGit(m_mirror, {"rev-parse", "--verify", "-q", "HEAD"}, &output) &&
        !output.trimmed().isEmpty())
        return QStringLiteral("HEAD");
    if (runGit(m_mirror,
               {"for-each-ref", "--format=%(refname)", "--count=1", "refs/heads/"},
               &output)) {
        const QString ref = QString::fromUtf8(output).trimmed();
        if (!ref.isEmpty())
            return ref;
    }
    return QString();
}

QByteArray CoveStore::readCoveBytes(const QString &relPath, bool *ok) const
{
    // Prefer the live working tree; fall back to the bare mirror via `git show`.
    if (!m_workTree.isEmpty()) {
        QFile file(m_workTree + "/" + relPath);
        if (file.open(QIODevice::ReadOnly)) {
            if (ok)
                *ok = true;
            return file.readAll();
        }
    }
    if (!m_mirror.isEmpty()) {
        const QString ref = mirrorRef();
        QByteArray output;
        if (!ref.isEmpty() &&
            runGit(m_mirror, {"show", ref + ":" + relPath}, &output)) {
            if (ok)
                *ok = true;
            return output;
        }
    }
    if (ok)
        *ok = false;
    return {};
}

QList<Cove> CoveStore::listCoves(QString *error) const
{
    QList<Cove> coves;
    QStringList relPaths;

    if (!m_workTree.isEmpty() && QFileInfo::exists(covesDir())) {
        const QStringList files = QDir(covesDir()).entryList(
            {QStringLiteral("*.cove")}, QDir::Files, QDir::Name);
        for (const QString &f : files)
            relPaths << covesDirRel() + "/" + f;
    } else if (!m_mirror.isEmpty()) {
        const QString ref = mirrorRef();
        QByteArray listing;
        if (!ref.isEmpty() &&
            runGit(m_mirror, {"ls-tree", ref, covesDirRel() + "/"}, &listing)) {
            for (const QString &line :
                 QString::fromUtf8(listing).split('\n', Qt::SkipEmptyParts)) {
                const int tab = line.indexOf('\t');
                if (tab < 0)
                    continue;
                const QString path = line.mid(tab + 1);
                if (path.endsWith(QStringLiteral(".cove")))
                    relPaths << path;
            }
        }
    }

    for (const QString &relPath : std::as_const(relPaths)) {
        Cove cove;
        if (loadEnvelope(relPath, cove))
            coves << cove;
    }
    if (coves.isEmpty() && error)
        error->clear();
    return coves;
}

bool CoveStore::loadEnvelope(const QString &relPath, Cove &out, QString *error) const
{
    bool ok = false;
    const QByteArray bytes = readCoveBytes(relPath, &ok);
    if (!ok) {
        if (error)
            *error = QStringLiteral("Could not read %1").arg(relPath);
        return false;
    }
    if (!fromEnvelope(bytes, out)) {
        if (error)
            *error = QStringLiteral("%1 is not a valid cove file").arg(relPath);
        return false;
    }
    out.relPath = relPath;
    out.slug = QFileInfo(relPath).completeBaseName();
    return true;
}

bool CoveStore::unlock(Cove &cove, const QString &password)
{
    CoveCrypto crypto(password, cove.salt, cove.rounds);
    if (!crypto.isValid())
        return false;
    const QByteArray plain = crypto.decrypt(cove.cipher);
    if (plain.isEmpty())
        return false; // wrong password (GCM tag mismatch) or corrupt payload
    const QJsonDocument doc = QJsonDocument::fromJson(plain);
    if (!doc.isObject())
        return false;
    const QJsonObject payload = doc.object();
    // The name lives in the encrypted payload (kept out of the repo). Fall back to
    // any plaintext envelope name for coves written before this change.
    cove.name = payload.value("name").toString(cove.name);
    cove.documents.clear();
    for (const QJsonValue &v : payload.value("documents").toArray())
        cove.documents << CoveDocument::fromJson(v.toObject());
    cove.accessLog.clear();
    for (const QJsonValue &v : payload.value("accessLog").toArray())
        cove.accessLog << CoveAccessEntry::fromJson(v.toObject());
    cove.unlocked = true;
    return true;
}

void CoveStore::appendAccess(Cove &cove, const CoveAccessEntry &entry)
{
    cove.accessLog.append(entry);
    // Keep the in-cove log bounded so it never bloats the ciphertext.
    constexpr int kMaxAccess = 500;
    while (cove.accessLog.size() > kMaxAccess)
        cove.accessLog.removeFirst();
}

QString CoveStore::uniqueSlug() const
{
    QString slug = obscureSlug();
    while (QFileInfo::exists(covesDir() + "/" + slug + ".cove"))
        slug = obscureSlug();
    return slug;
}

bool CoveStore::createCove(const QString &name, const QString &password,
                           bool notifyOnOpen, const QList<CoveDocument> &documents,
                           Cove *out, QString *error)
{
    if (!canWrite()) {
        if (error)
            *error = QStringLiteral("This repository has no local working tree to "
                                    "store a cove in.");
        return false;
    }
    if (name.trimmed().isEmpty() || password.isEmpty()) {
        if (error)
            *error = QStringLiteral("A cove needs a name and a password.");
        return false;
    }

    Cove cove;
    cove.id = newId();
    cove.name = name.trimmed();
    cove.slug = uniqueSlug();
    cove.relPath = covesDirRel() + "/" + cove.slug + ".cove";
    cove.creator = m_identity ? m_identity->publicKey() : QString();
    cove.createdAtMs = QDateTime::currentMSecsSinceEpoch();
    cove.notifyOnOpen = notifyOnOpen;
    cove.salt = CoveCrypto::randomSalt();
    cove.rounds = CoveCrypto::defaultRounds();
    cove.documents = documents;
    cove.unlocked = true;

    if (!save(cove, password, error))
        return false;
    if (out)
        *out = cove;
    return true;
}

bool CoveStore::save(const Cove &cove, const QString &password, QString *error)
{
    if (!canWrite()) {
        if (error)
            *error = QStringLiteral("No local working tree to save the cove to.");
        return false;
    }
    if (cove.relPath.isEmpty() || cove.salt.isEmpty() || cove.rounds <= 0) {
        if (error)
            *error = QStringLiteral("The cove is missing its encryption parameters.");
        return false;
    }

    QJsonArray docs;
    for (const CoveDocument &d : cove.documents)
        docs.append(d.toJson());
    QJsonArray log;
    for (const CoveAccessEntry &e : cove.accessLog)
        log.append(e.toJson());
    // The name rides inside the ciphertext (not the envelope) so it never appears
    // in the repo's tree, file contents or history.
    const QByteArray payload =
        QJsonDocument(
            QJsonObject{{"name", cove.name}, {"documents", docs}, {"accessLog", log}})
            .toJson(QJsonDocument::Compact);

    CoveCrypto crypto(password, cove.salt, cove.rounds);
    if (!crypto.isValid()) {
        if (error)
            *error = crypto.errorString();
        return false;
    }
    Cove sealed = cove;
    sealed.cipher = crypto.encrypt(payload);
    if (sealed.cipher.isEmpty()) {
        if (error)
            *error = QStringLiteral("Could not encrypt the cove.");
        return false;
    }

    QDir().mkpath(covesDir());
    const QString absPath = m_workTree + "/" + sealed.relPath;
    QFile file(absPath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (error)
            *error = QStringLiteral("Could not write %1").arg(absPath);
        return false;
    }
    file.write(QJsonDocument(toEnvelope(sealed)).toJson(QJsonDocument::Indented));
    file.close();

    // Commit by slug, never the name, so git history stays free of cove names.
    return commit(QStringLiteral("cove: %1").arg(sealed.slug), sealed.relPath, error);
}

bool CoveStore::commit(const QString &message, const QString &relPath, QString *error) const
{
    QString err;
    if (!runGit(m_workTree, {"add", "--", relPath}, nullptr, &err)) {
        if (error)
            *error = "git add failed: " + err;
        return false;
    }
    if (!runGit(m_workTree, {"commit", "-m", message, "--", relPath}, nullptr, &err)) {
        if (err.contains("nothing to commit") || err.isEmpty())
            return true; // no change to persist — not an error
        if (error)
            *error = "git commit failed: " + err;
        return false;
    }
    return true;
}
