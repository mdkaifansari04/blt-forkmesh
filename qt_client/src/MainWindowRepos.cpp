// MainWindowRepos: MainWindow feature methods, split out of MainWindow.cpp.
// Repositories list and repo settings.
//
// These are MainWindow member functions defined in their own translation unit;
// the class itself is declared in MainWindow.h. Shared helpers live in
// MainWindowInternal.h / MainWindowShared.cpp (namespace forkmesh::ui).

#include "MainWindow.h"
#include "MainWindowInternal.h"
#include "KebabHeaderView.h"
#include "ControlNode.h"
#include "NodeEventSocket.h"
#include "PrivateMirrorRuntime.h"
#include "PublicMirrorRuntime.h"
#include "UpstreamCheckoutSync.h"

#include <QCryptographicHash>
#include <QFutureWatcher>
#include <QRegularExpression>
#include <QStandardPaths>

#include <QtConcurrent/QtConcurrentRun>

#include <cmath>

using namespace forkmesh::ui;

namespace {
constexpr qint64 kCatalogPublishDebounceMs = 1000;
constexpr qint64 kCatalogPublishMinIntervalMs = 30LL * 1000;
// An unchanged record is still re-published this often so its catalog row
// can't age out server-side (MAX_CATALOG_REPOS prunes by updatedAt).
constexpr qint64 kCatalogPublishRefreshMs = 6LL * 60 * 60 * 1000;
constexpr qint64 kCatalogPublishRateLimitRetryMs = 60LL * 1000;
constexpr qint64 kCatalogPublishMaxRetryAfterMs = 10LL * 60 * 1000;
constexpr qint64 kContributionScanCapacityRetryMs = 1000;

QString privateReplicaRoot()
{
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) +
           QStringLiteral("/private-replicas");
}

QString privateIdentityVaultPath()
{
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) +
           QStringLiteral("/identity/private-mirror-vault.json");
}

QString publicArchiveRoot()
{
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) +
           QStringLiteral("/public-mirror-archives");
}

QString publicIdentityVaultPath()
{
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) +
           QStringLiteral("/identity/public-age-vault.json");
}

QByteArray publicIdentityVaultSecret(const ForkMeshIdentity &identity)
{
    if (!identity.isValid())
        return {};
    const QByteArray canonical =
        QByteArrayLiteral("forkmesh-public-age-vault-unlock-v1\n") +
        identity.publicKey().toUtf8();
    return QByteArray::fromBase64(
        identity.signData(canonical).toLatin1(),
        QByteArray::Base64UrlEncoding);
}

QByteArray privateIdentityVaultSecret(const ForkMeshIdentity &identity)
{
    if (!identity.isValid())
        return {};
    const QByteArray canonical =
        QByteArrayLiteral("forkmesh-private-mirror-vault-unlock-v1\n") +
        identity.publicKey().toUtf8();
    return QByteArray::fromBase64(
        identity.signData(canonical).toLatin1(),
        QByteArray::Base64UrlEncoding);
}

QString privateOpaqueRepositoryId(const QString &opaqueReplicaId)
{
    if (!PrivateMirrorStore::isOpaqueId(opaqueReplicaId))
        return {};
    return QString::fromLatin1(
               QCryptographicHash::hash(
                   QByteArrayLiteral("forkmesh-private-repository-id-v1\n") +
                       opaqueReplicaId.toUtf8(),
                   QCryptographicHash::Sha256)
                   .toHex())
        .left(32);
}

QString configuredPrivateMirrorNode()
{
    return QSettings()
        .value(QStringLiteral("control/cloudflareNodeName"))
        .toString()
        .trimmed()
        .toLower();
}

struct PrivateSyncWorkerResult {
    PrivateMirrorRuntime::SyncResult sync;
    std::shared_ptr<PrivateMirrorMaterialization> materialization;
    QString error;
    QString notice;
    bool legacyRemoved = true;
};

struct PrivateDownloadWorkerResult {
    QString opaqueId;
    PrivateMirrorStore::Metadata metadata;
    std::shared_ptr<PrivateMirrorMaterialization> materialization;
    QString error;
};

struct PublicSyncWorkerResult {
    PublicMirrorRuntime::Metadata metadata;
    std::shared_ptr<PublicMirrorMaterialization> materialization;
    QString error;
    QString notice;
    QString upstreamSummary;
    bool created = false;
    bool legacyRemoved = true;
};

QString cleanCatalogString(const QJsonObject &object, const QString &key,
                           int maximumCodePoints)
{
    const QString value = object.value(key).toString().trimmed();
    QString result;
    result.reserve(qMin(value.size(), maximumCodePoints));
    int points = 0;
    for (qsizetype i = 0;
         i < value.size() && points < maximumCodePoints; ++i, ++points) {
        const QChar current = value.at(i);
        result.append(current);
        if (current.isHighSurrogate() && i + 1 < value.size() &&
            value.at(i + 1).isLowSurrogate()) {
            result.append(value.at(++i));
        }
    }
    return result;
}

qint64 boundedCatalogInteger(const QJsonValue &value, qint64 maximum)
{
    bool ok = false;
    qint64 number = 0;
    if (value.isString())
        number = value.toString().toLongLong(&ok);
    else if (value.isDouble()) {
        const double raw = value.toDouble();
        number = qint64(raw);
        ok = std::isfinite(raw) && double(number) == raw;
    }
    return ok ? qBound<qint64>(0, number, maximum) : 0;
}

QJsonArray normalizedActivityWeeks(const QJsonValue &value)
{
    const QJsonArray input = value.toArray();
    QJsonArray normalized;
    const qsizetype start = qMax<qsizetype>(0, input.size() - 52);
    for (qsizetype i = start; i < input.size(); ++i)
        normalized.append(
            double(boundedCatalogInteger(input.at(i), 1000000)));
    while (normalized.size() < 52)
        normalized.prepend(0);
    return normalized;
}

QJsonArray normalizedCatalogLogoLabels(const QJsonValue &value, int limit,
                                       int maximumCodePoints)
{
    QJsonArray normalized;
    for (const QJsonValue &entry : value.toArray()) {
        QJsonObject wrapper{{QStringLiteral("value"), entry}};
        const QString label =
            cleanCatalogString(wrapper, QStringLiteral("value"),
                               maximumCodePoints);
        if (!label.isEmpty())
            normalized.append(label);
        if (normalized.size() >= limit)
            break;
    }
    return normalized;
}

QJsonObject normalizedCatalogLogoMetadata(const QJsonValue &value)
{
    const QJsonObject input = value.toObject();
    QJsonObject languages;
    int languageCount = 0;
    const QJsonObject inputLanguages =
        input.value(QStringLiteral("languages")).toObject();
    for (auto it = inputLanguages.constBegin();
         it != inputLanguages.constEnd() && languageCount < 12; ++it) {
        QJsonObject wrapper{{QStringLiteral("value"), it.key()}};
        const QString language =
            cleanCatalogString(wrapper, QStringLiteral("value"), 80);
        if (language.isEmpty())
            continue;
        languages.insert(
            language,
            double(boundedCatalogInteger(it.value(), qint64(1) << 50)));
        ++languageCount;
    }
    return {
        {QStringLiteral("description"),
         cleanCatalogString(input, QStringLiteral("description"), 500)},
        {QStringLiteral("languages"), languages},
        {QStringLiteral("topics"),
         normalizedCatalogLogoLabels(
             input.value(QStringLiteral("topics")), 12, 80)},
        {QStringLiteral("fileStructure"),
         normalizedCatalogLogoLabels(
             input.value(QStringLiteral("fileStructure")), 24, 120)},
        {QStringLiteral("frameworks"),
         normalizedCatalogLogoLabels(
             input.value(QStringLiteral("frameworks")), 12, 80)},
        {QStringLiteral("projectCategory"),
         cleanCatalogString(input, QStringLiteral("projectCategory"), 80)},
    };
}

QString normalizedCatalogSolanaAddress(const QJsonObject &data)
{
    const QString address =
        cleanCatalogString(data, QStringLiteral("solana"), 64);
    static const QRegularExpression publicAddressPattern(
        QStringLiteral("^[1-9A-HJ-NP-Za-km-z]{32,44}$"));
    return publicAddressPattern.match(address).hasMatch()
               ? address
               : QString();
}

QJsonObject normalizedCatalogV2Record(const QJsonObject &data)
{
    const bool privateRepository =
        data.value(QStringLiteral("visibility")).toString() !=
        QLatin1String("public");
    const QJsonObject hostTelemetry =
        forkmesh::control::normalizedCatalogHostTelemetry(data);
    QJsonObject record{
        {QStringLiteral("owner"),
         cleanCatalogString(data, QStringLiteral("owner"), 80)},
        {QStringLiteral("name"),
         cleanCatalogString(data, QStringLiteral("name"), 80)},
        {QStringLiteral("visibility"),
         privateRepository ? QStringLiteral("private")
                           : QStringLiteral("public")},
        {QStringLiteral("mirrorEncryption"),
         data.value(QStringLiteral("mirrorEncryption")).toString() ==
                 QLatin1String("owner-sealed-v1")
             ? QStringLiteral("owner-sealed-v1")
             : QString()},
        {QStringLiteral("opaqueRepoId"),
         cleanCatalogString(data, QStringLiteral("opaqueRepoId"), 64)
             .toLower()},
        {QStringLiteral("keyEpoch"),
         double(boundedCatalogInteger(
             data.value(QStringLiteral("keyEpoch")), qint64(1) << 31))},
        {QStringLiteral("encryptedManifestHash"),
         cleanCatalogString(
             data, QStringLiteral("encryptedManifestHash"), 64)
             .toLower()},
        {QStringLiteral("encryptedManifestSig"),
         cleanCatalogString(
             data, QStringLiteral("encryptedManifestSig"), 220)},
        {QStringLiteral("sizeBytes"),
         double(boundedCatalogInteger(
             data.value(QStringLiteral("sizeBytes")), qint64(1) << 50))},
        {QStringLiteral("description"),
         cleanCatalogString(data, QStringLiteral("description"), 240)},
        {QStringLiteral("logoMetadata"),
         normalizedCatalogLogoMetadata(
             data.value(QStringLiteral("logoMetadata")))},
        {QStringLiteral("cloneUrl"),
         cleanCatalogString(data, QStringLiteral("cloneUrl"), 2048)},
        {QStringLiteral("solana"),
         normalizedCatalogSolanaAddress(data)},
        {QStringLiteral("channel"),
         cleanCatalogString(data, QStringLiteral("channel"), 120)},
        {QStringLiteral("hostedSince"),
         cleanCatalogString(data, QStringLiteral("hostedSince"), 32)},
        {QStringLiteral("lastSync"),
         cleanCatalogString(data, QStringLiteral("lastSync"), 32)},
        {QStringLiteral("updatedAt"),
         cleanCatalogString(data, QStringLiteral("updatedAt"), 32)},
        {QStringLiteral("rootCommit"),
         cleanCatalogString(data, QStringLiteral("rootCommit"), 64)},
        {QStringLiteral("source"),
         cleanCatalogString(data, QStringLiteral("source"), 40)},
        {QStringLiteral("commit"),
         cleanCatalogString(data, QStringLiteral("commit"), 64)},
        {QStringLiteral("branch"),
         cleanCatalogString(data, QStringLiteral("branch"), 120)},
        {QStringLiteral("issueCount"),
         cleanCatalogString(data, QStringLiteral("issueCount"), 12)},
        {QStringLiteral("issueMaxNumber"),
         cleanCatalogString(data, QStringLiteral("issueMaxNumber"), 12)},
        {QStringLiteral("commitCount"),
         cleanCatalogString(data, QStringLiteral("commitCount"), 12)},
        {QStringLiteral("branchCount"),
         cleanCatalogString(data, QStringLiteral("branchCount"), 12)},
        {QStringLiteral("pullCount"),
         cleanCatalogString(data, QStringLiteral("pullCount"), 12)},
        {QStringLiteral("discussionCount"),
         cleanCatalogString(data, QStringLiteral("discussionCount"), 12)},
        {QStringLiteral("activityWeeks"),
         normalizedActivityWeeks(
             data.value(QStringLiteral("activityWeeks")))},
        {QStringLiteral("worktreeCount"),
         cleanCatalogString(data, QStringLiteral("worktreeCount"), 12)},
        {QStringLiteral("artifactCount"),
         cleanCatalogString(data, QStringLiteral("artifactCount"), 12)},
        {QStringLiteral("platform"),
         cleanCatalogString(data, QStringLiteral("platform"), 16)},
        {QStringLiteral("runtimeMode"),
         cleanCatalogString(data, QStringLiteral("runtimeMode"), 12)
             .toLower()},
        {QStringLiteral("version"),
         cleanCatalogString(data, QStringLiteral("version"), 32)},
        {QStringLiteral("nodeId"),
         cleanCatalogString(data, QStringLiteral("nodeId"), 64)},
        {QStringLiteral("clonesServed"),
         cleanCatalogString(data, QStringLiteral("clonesServed"), 12)},
        {QStringLiteral("websiteServed"),
         cleanCatalogString(data, QStringLiteral("websiteServed"), 12)},
        {QStringLiteral("maintainer"),
         cleanCatalogString(data, QStringLiteral("maintainer"), 120)},
        {QStringLiteral("signature"),
         cleanCatalogString(data, QStringLiteral("signature"), 220)},
        {QStringLiteral("stateHash"),
         cleanCatalogString(data, QStringLiteral("stateHash"), 64)},
        {QStringLiteral("stateSig"),
         cleanCatalogString(data, QStringLiteral("stateSig"), 220)},
    };
    QJsonArray changedFiles;
    for (const QJsonValue &value :
         data.value(QStringLiteral("changedFiles")).toArray()) {
        QString path =
            QDir::fromNativeSeparators(value.toString()).trimmed().left(160);
        if (path.isEmpty() || path.startsWith(QLatin1Char('/')) ||
            path == QLatin1String("..") ||
            path.startsWith(QLatin1String("../")) ||
            changedFiles.contains(path)) {
            continue;
        }
        bool safe = true;
        for (const QChar ch : path) {
            if (ch.unicode() < 0x20 || ch.unicode() == 0x7f) {
                safe = false;
                break;
            }
        }
        if (safe)
            changedFiles.append(path);
        if (changedFiles.size() >= 8)
            break;
    }
    if (!changedFiles.isEmpty())
        record.insert(QStringLiteral("changedFiles"), changedFiles);
    // Optional extension fields are omitted when not shared. Besides preserving
    // a truthful "unknown", this keeps catalog-v2 signatures from older clients
    // valid after the Worker learns about host telemetry.
    for (const QString &key :
         {QStringLiteral("cpuPercent"), QStringLiteral("memUsedBytes"),
          QStringLiteral("memTotalBytes"), QStringLiteral("diskUsedBytes"),
          QStringLiteral("diskTotalBytes")}) {
        const QJsonValue value = hostTelemetry.value(key);
        if (!value.isNull() && !value.isUndefined())
            record.insert(key, value);
    }
    // The machine's node name is another optional extension: absent (never
    // empty) when unset, so older clients' catalog-v2 signatures stay valid.
    const QString machineName =
        cleanCatalogString(data, QStringLiteral("machineName"), 63);
    if (!machineName.isEmpty())
        record.insert(QStringLiteral("machineName"), machineName);
    QJsonArray agentProviders;
    const QJsonArray rawAgentProviders =
        data.value(QStringLiteral("agentProviders")).toArray();
    for (const QJsonValue &value : rawAgentProviders) {
        const QString provider = value.toString().trimmed().toLower();
        if ((provider == QLatin1String("claude-code") ||
             provider == QLatin1String("codex")) &&
            !agentProviders.contains(provider)) {
            agentProviders.append(provider);
        }
    }
    if (!agentProviders.isEmpty())
        record.insert(QStringLiteral("agentProviders"), agentProviders);
    // Latest-commit subject/author/date: same optional-extension rule again, so
    // a node that can't read them (or an older client) publishes no key at all
    // rather than an empty one that would change the signed record.
    const QString commitSubject =
        cleanCatalogString(data, QStringLiteral("commitSubject"),
                           kMaxCommitSubjectChars);
    if (!commitSubject.isEmpty())
        record.insert(QStringLiteral("commitSubject"), commitSubject);
    const QString commitAuthorName =
        cleanCatalogString(data, QStringLiteral("commitAuthorName"),
                           kMaxCommitAuthorChars);
    if (!commitAuthorName.isEmpty())
        record.insert(QStringLiteral("commitAuthorName"), commitAuthorName);
    const QString commitAt =
        cleanCatalogString(data, QStringLiteral("commitAt"), 16);
    if (!commitAt.isEmpty())
        record.insert(QStringLiteral("commitAt"), commitAt);
    return record;
}

QString privateControlPlaneKey(const QUrl &catalogUrl,
                               const QString &repositoryKey,
                               const QString &ownerKeyId)
{
    QUrl origin = catalogUrl;
    origin.setPath(QString());
    origin.setQuery(QString());
    origin.setFragment(QString());
    return origin.toString(QUrl::RemoveUserInfo |
                           QUrl::StripTrailingSlash) +
           QLatin1Char('|') + repositoryKey + QLatin1Char('|') + ownerKeyId;
}

struct RepoRemoteRow {
    QString name;
    QString fetchUrl;
    QString pushUrl;
};

QString catalogPublishOwnerFromKey(const QString &key)
{
    const int slash = key.indexOf(QLatin1Char('/'));
    return slash > 0 ? key.left(slash) : key;
}

qint64 catalogPublishRetryDelayMs(const QNetworkReply *reply, int status,
                                  int consecutiveFailures)
{
    // 429 (rate limit) AND 5xx (Worker overload — Cloudflare 1101/1102) both
    // warrant a cooldown before re-queuing. Previously only 429 delayed the
    // retry, so during the 2026-07-11 outage every 500/503 rescheduled with
    // zero delay and the desktop re-POSTed the catalog every ~30s for 100
    // minutes straight, amplifying the very overload it was hitting.
    if (status != 429 && !(status >= 500 && status <= 599))
        return 0;
    // A fixed 60s retry still re-POSTs a persistently-overloaded relay once a
    // minute forever (a 1102 worker never recovers while it's being hammered).
    // Escalate exponentially per consecutive failure — 60s, 2m, 4m, … — so the
    // desktop backs off and gives the Worker room to recover, then caps out.
    const int shift = qBound(0, consecutiveFailures, 8);
    qint64 delay = qMin(kCatalogPublishRateLimitRetryMs << shift,
                        kCatalogPublishMaxRetryAfterMs);
    const QByteArray retryAfter = reply ? reply->rawHeader("Retry-After") : QByteArray();
    bool ok = false;
    const qint64 seconds =
        QString::fromLatin1(retryAfter).trimmed().toLongLong(&ok);
    if (ok && seconds > 0)
        delay = qMax(delay, seconds * 1000);
    return qMin(delay, kCatalogPublishMaxRetryAfterMs);
}

bool mirrorHasServedCommit(const QString &mirrorPath)
{
    if (mirrorPath.trimmed().isEmpty() || !QDir(mirrorPath).exists())
        return false;
    const QString branch = mirrorHeadBranch(mirrorPath);
    return !mirrorBranchCommit(mirrorPath, branch).isEmpty();
}

QString repoRemoteGitDir(const RepositoryRecord &repo)
{
    const QString local = repo.localPath.trimmed();
    if (!local.isEmpty() && QDir(local).exists(QStringLiteral(".git")))
        return local;
    const QString mirror = repo.mirrorPath.trimmed();
    if (!mirror.isEmpty() && QDir(mirror).exists())
        return mirror;
    return {};
}

QList<RepoRemoteRow> readRepoRemotes(const QString &gitDir)
{
    QList<RepoRemoteRow> remotes;
    if (gitDir.isEmpty())
        return remotes;
    QByteArray out;
    if (!runGitCapture(gitDir,
                       {QStringLiteral("remote"), QStringLiteral("-v")},
                       &out, nullptr))
        return remotes;
    QMap<QString, RepoRemoteRow> byName;
    const QStringList lines =
        QString::fromUtf8(out).split('\n', Qt::SkipEmptyParts);
    for (const QString &line : lines) {
        const int tab = line.indexOf('\t');
        if (tab <= 0)
            continue;
        const QString name = line.left(tab).trimmed();
        QString rest = line.mid(tab + 1).trimmed();
        const bool isFetch = rest.endsWith(QStringLiteral("(fetch)"));
        const bool isPush = rest.endsWith(QStringLiteral("(push)"));
        rest.remove(QStringLiteral("(fetch)"));
        rest.remove(QStringLiteral("(push)"));
        rest = rest.trimmed();
        RepoRemoteRow row = byName.value(name);
        row.name = name;
        if (isFetch)
            row.fetchUrl = rest;
        else if (isPush)
            row.pushUrl = rest;
        byName.insert(name, row);
    }
    for (auto it = byName.constBegin(); it != byName.constEnd(); ++it)
        remotes.append(it.value());
    std::sort(remotes.begin(), remotes.end(),
              [](const RepoRemoteRow &a, const RepoRemoteRow &b) {
                  return a.name < b.name;
              });
    return remotes;
}
} // namespace

// ------------------------------------------------------------- repositories

QString MainWindow::repositoryMirrorRoot() const
{
    const QString configured =
        QSettings().value(kMirrorRootSetting).toString().trimmed();
    if (!configured.isEmpty())
        return configured;
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) +
           "/mirrors";
}

QString MainWindow::repositoryPreviewRoot() const
{
    const QString configured =
        QSettings().value(kPreviewCacheRootSetting).toString().trimmed();
    if (!configured.isEmpty())
        return configured;
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) +
           "/repo-preview-cache";
}

QString MainWindow::repositoryPreviewPath(const QString &owner,
                                          const QString &name) const
{
    return repositoryPreviewRoot() + "/" +
           repoSegment(owner, QStringLiteral("owner")) + "-" +
           repoSegment(name, QStringLiteral("repository")) + ".git";
}

QString MainWindow::repositoryNetworkCloneUrl(const QString &owner,
                                              const QString &name) const
{
    QUrl url = catalogApiUrl();
    url.setPath("/" + repoSegment(owner, QStringLiteral("owner")) + "/" +
                repoSegment(name, QStringLiteral("repository")));
    url.setQuery(QString());
    url.setFragment(QString());
    return url.toString();
}

void MainWindow::changeMirrorLocation()
{
    const QString chosen = QFileDialog::getExistingDirectory(
        this, "Choose where to store mirrored repositories",
        repositoryMirrorRoot());
    if (chosen.isEmpty())
        return;
    QSettings().setValue(kMirrorRootSetting, chosen);
    if (m_mirrorRootEdit)
        m_mirrorRootEdit->setText(chosen);
    logSystem("Mirror storage folder set to " + chosen +
              " (applies to newly added repositories).");
    QMessageBox::information(
        this, "Mirror storage",
        "New mirrors will be stored in:\n" + chosen +
            "\n\nExisting mirrors stay where they are. A local fork will push "
            "into its repository's mirror here.");
}

void MainWindow::changePreviewCacheLocation()
{
    const QString chosen = QFileDialog::getExistingDirectory(
        this, "Choose where to cache repository previews",
        repositoryPreviewRoot());
    if (chosen.isEmpty())
        return;
    QSettings().setValue(kPreviewCacheRootSetting, chosen);
    if (m_previewCacheRootEdit)
        m_previewCacheRootEdit->setText(chosen);
    logSystem("Preview cache folder set to " + chosen + ".");
    QMessageBox::information(
        this, "Preview cache",
        "Temporary repository previews will be stored in:\n" + chosen +
            "\n\nExisting preview caches stay where they are.");
}

QString MainWindow::repositoryChannel(const RepositoryRecord &repo) const
{
    // Repo activity is routed to the single shared #general channel rather than a
    // per-repo room, so the network doesn't fragment into a room per node/repo.
    Q_UNUSED(repo);
    return QStringLiteral("#general");
}

QString MainWindow::repositorySource(const RepositoryRecord &repo) const
{
    return repo.localPath.trimmed().isEmpty() ? repo.cloneUrl.trimmed()
                                             : repo.localPath.trimmed();
}

QStringList MainWindow::viewAuthGitArgs(const RepositoryRecord &repo,
                                        const QString &source) const
{
    if (!repo.isPrivate)
        return {};
    const QUrl src(source);
    // Only ever attach the token to mainnode requests, and only for https/http
    // (a local working-copy path has no host and needs no token).
    if (!src.isValid() || src.host().isEmpty() ||
        src.host().compare(catalogApiUrl().host(), Qt::CaseInsensitive) != 0)
        return {};
    // Sign the exact owner/name the relay parses from this same URL path so the
    // canonical string matches on both sides.
    const QStringList segs =
        src.path().split('/', Qt::SkipEmptyParts);
    if (segs.size() < 2)
        return {};
    const QString owner = segs.at(segs.size() - 2);
    const QString name = segs.at(segs.size() - 1);
    if (!m_profileIdentity.isValid())
        return {};
    const QString viewer = accountOwner();
    if (viewer.isEmpty() || !hasOwnerSigningCapability(viewer))
        return {};
    const QString ts = QString::number(QDateTime::currentMSecsSinceEpoch());
    // Two read paths (issue #9): when this node owns the repo it signs the owner
    // view token (Basic username = owner); when it's a collaborator the repo was
    // shared with, it signs the share-view token with its OWN key (Basic username
    // = this node's account) so the relay verifies against the grantee's key and
    // checks the share ACL. The username tells the relay which path to take.
    const bool asOwner = viewer.isEmpty() || viewer == owner;
    const QString user = asOwner ? owner : viewer;
    const QByteArray canonical =
        asOwner
            ? ("forkmesh-view-v1\n" + owner + "\n" + name + "\n" + ts).toUtf8()
            : ("forkmesh-share-view-v1\n" + viewer + "\n" + owner + "\n" + name +
               "\n" + ts).toUtf8();
    const QString password = ts + "." + m_profileIdentity.signData(canonical);
    const QByteArray basic =
        (user + ":" + password).toUtf8().toBase64();
    return {QStringLiteral("-c"),
            QStringLiteral("http.extraHeader=Authorization: Basic ") +
                QString::fromLatin1(basic)};
}

QByteArray MainWindow::privateReplicaAuthorization(
    const RepositoryRecord &repo) const
{
    if (!repo.isPrivate || !m_profileIdentity.isValid())
        return {};
    const QString owner =
        repoSegment(repo.owner, QStringLiteral("owner"));
    const QString name =
        repoSegment(repo.name, QStringLiteral("repository"));
    const QString viewer = accountOwner().trimmed().toLower();
    if (owner.isEmpty() || name.isEmpty() || viewer.isEmpty() ||
        !hasOwnerSigningCapability(viewer)) {
        return {};
    }
    const QString ts =
        QString::number(QDateTime::currentMSecsSinceEpoch());
    const bool asOwner = viewer == owner;
    const QByteArray canonical =
        asOwner
            ? ("forkmesh-view-v1\n" + owner + "\n" + name + "\n" +
               ts)
                  .toUtf8()
            : ("forkmesh-share-view-v1\n" + viewer + "\n" + owner +
               "\n" + name + "\n" + ts)
                  .toUtf8();
    const QString signature =
        m_profileIdentity.signData(canonical);
    if (signature.isEmpty())
        return {};
    const QByteArray credentials =
        (asOwner ? owner : viewer).toUtf8() + ':' + ts.toUtf8() + '.' +
        signature.toUtf8();
    return QByteArrayLiteral("Basic ") + credentials.toBase64();
}

void MainWindow::ensurePrivateMirrorRecipientIdentityRegistered(
    std::function<void(bool, QString)> onDone)
{
    auto finish =
        [onDone = std::move(onDone)](
            bool ok, const QString &error = QString()) mutable {
            if (onDone)
                onDone(ok, error);
        };
    if (!m_networkAccess || m_accountSessionToken.trimmed().isEmpty() ||
        !m_profileIdentity.isValid() ||
        !hasOwnerSigningCapability(accountOwner())) {
        finish(false,
               QStringLiteral(
                   "an authenticated desktop-capable account is required"));
        return;
    }
    MirrorCrypto::Identity mirrorIdentity;
    QString localError;
    if (!loadOwnerEncryptionIdentity(&mirrorIdentity, &localError)) {
        finish(false,
               QStringLiteral(
                   "the local encryption identity vault is unavailable"));
        return;
    }
    const QString keyId = mirrorIdentity.keyId();
    const QJsonObject publicBundle = mirrorIdentity.publicBundle();
    mirrorIdentity.x25519Priv.fill('\0');
    mirrorIdentity.mlkemPriv.fill('\0');
    mirrorIdentity = {};

    QUrl url = catalogApiUrl();
    url.setPath(QStringLiteral("/api/security/owner-keys"));
    url.setQuery(QString());
    url.setFragment(QString());
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader,
                      QStringLiteral("application/json"));
    request.setRawHeader(
        "Authorization",
        QByteArrayLiteral("Bearer ") +
            m_accountSessionToken.toUtf8());
    QNetworkReply *reply = m_networkAccess->post(
        request,
        QJsonDocument(QJsonObject{
                          {QStringLiteral("publicBundle"), publicBundle},
                      })
            .toJson(QJsonDocument::Compact));
    connect(
        reply, &QNetworkReply::finished, this,
        [reply, keyId, finish = std::move(finish)]() mutable {
            const QByteArray body = reply->readAll();
            const int status =
                reply->attribute(
                         QNetworkRequest::HttpStatusCodeAttribute)
                    .toInt();
            const QJsonObject object =
                QJsonDocument::fromJson(body).object();
            const bool ok =
                reply->error() == QNetworkReply::NoError &&
                status >= 200 && status < 300 &&
                object.value(QStringLiteral("ok")).toBool() &&
                object.value(QStringLiteral("keyId")).toString() ==
                    keyId &&
                !object.value(QStringLiteral("privateKeysStored"))
                     .toBool(true);
            reply->deleteLater();
            finish(ok,
                   ok ? QString()
                      : QStringLiteral(
                            "the relay did not accept the public-only "
                            "encryption key"));
        });
}

bool MainWindow::loadOwnerEncryptionIdentity(
    MirrorCrypto::Identity *identity, QString *error) const
{
    if (!identity) {
        if (error)
            *error = QStringLiteral("No encryption identity destination.");
        return false;
    }
    *identity = {};
    QByteArray vaultSecret =
        privateIdentityVaultSecret(m_profileIdentity);
    if (vaultSecret.size() < 32) {
        vaultSecret.fill('\0');
        vaultSecret.clear();
        if (error)
            *error = QStringLiteral(
                "The local device identity cannot unlock the owner vault.");
        return false;
    }
    const bool ok = PrivateMirrorRuntime::loadOrCreateIdentity(
        privateIdentityVaultPath(), vaultSecret, identity, error);
    vaultSecret.fill('\0');
    vaultSecret.clear();
    if (!ok)
        *identity = {};
    return ok;
}

void MainWindow::loadRepositories()
{
    m_repositories.clear();
    QSettings settings;
    const int count = settings.beginReadArray(kRepositoriesArray);
    for (int i = 0; i < count; ++i) {
        settings.setArrayIndex(i);
        RepositoryRecord repo;
        repo.owner = settings.value("owner").toString();
        repo.name = settings.value("name").toString();
        repo.description = settings.value("description").toString();
        repo.cloneUrl = settings.value("cloneUrl").toString();
        repo.localPath = settings.value("localPath").toString();
        repo.solanaAddress = settings.value("solanaAddress").toString();
        repo.mirrorPath = settings.value("mirrorPath").toString();
        repo.privateReplicaId =
            settings.value("privateReplicaId").toString().trimmed();
        if (!PrivateMirrorStore::isOpaqueId(repo.privateReplicaId))
            repo.privateReplicaId.clear();
        repo.publicArchiveId =
            settings.value("publicArchiveId").toString().trimmed();
        if (!PublicMirrorRuntime::isArchiveId(repo.publicArchiveId))
            repo.publicArchiveId.clear();
        repo.publishToNetwork = settings.value("publishToNetwork").toBool();
        repo.isPrivate = settings.value("isPrivate").toBool();
        // Default off for mirrored repos (owner isn't this node); on for repos
        // this node owns. Explicitly stored values always win.
        repo.actionsEnabled =
            settings.value("actionsEnabled", repo.owner == accountOwner())
                .toBool();
        repo.externallyManagedActions =
            settings.value("externallyManagedActions", false).toBool();
        repo.externalActionsSource =
            settings.value("externalActionsSource").toString().trimmed();
        repo.externalActionsRef =
            settings.value("externalActionsRef").toString().trimmed();
        repo.secretScanningEnabled =
            settings.value("secretScanningEnabled", true).toBool();
        repo.disabledWorkflows = settings.value("disabledWorkflows").toStringList();
        repo.workflowNodes = settings.value("workflowNodes").toStringList();
        repo.hostedSinceMs = settings.value("hostedSinceMs").toLongLong();
        repo.lastSyncMs = settings.value("lastSyncMs").toLongLong();
        repo.publishedAtMs = settings.value("publishedAtMs").toLongLong();
        if (!repo.name.isEmpty() &&
            (!repositorySource(repo).isEmpty() ||
             PublicMirrorRuntime::isArchiveId(repo.publicArchiveId) ||
             PrivateMirrorStore::isOpaqueId(repo.privateReplicaId))) {
            m_repositories.append(repo);
            // Seed the serving state publishRepositoryNow compares against, so
            // a repo that was already shared before this run keeps publishing
            // as a heartbeat rather than looking like a fresh user decision
            // (see the publishIntent comment there).
            const QString publishKey = catalogPublishKey(repo);
            if (!publishKey.isEmpty())
                m_catalogPublishServeState.insert(publishKey,
                                                  repo.publishToNetwork);
        }
    }
    settings.endArray();
    // Re-attach records whose mirror directory moved out from under them (an
    // owner rename re-derives mirrorPath without migrating the directory).
    bool migrated = false;
    for (RepositoryRecord &repo : m_repositories)
        migrated = reconcileMirrorPath(repo) || migrated;
    if (migrated)
        saveRepositories();
    loadRepoStats();
}

bool MainWindow::reconcileMirrorPath(RepositoryRecord &repo)
{
    if (repo.previewOnly || repo.mirrorPath.trimmed().isEmpty() ||
        QDir(repo.mirrorPath).exists())
        return false;
    // Only a working-copy holder knows which bare mirror its pushes land in:
    // ensurePushHook points the copy's push URL at the served mirror, so that
    // remote is the ground truth for where the old directory lives.
    const QString localPath = repo.localPath.trimmed();
    if (localPath.isEmpty() || !QDir(localPath).exists(QStringLiteral(".git")))
        return false;
    QByteArray out;
    if (!runGitCapture(localPath,
                       {QStringLiteral("remote"), QStringLiteral("get-url"),
                        QStringLiteral("--push"), QStringLiteral("origin")},
                       &out, nullptr))
        return false;
    const QString oldMirror = QString::fromUtf8(out).trimmed();
    // Adopt only a real local bare repository, never a URL remote.
    if (oldMirror.isEmpty() || oldMirror.contains(QLatin1String("://")) ||
        QDir::cleanPath(oldMirror) == QDir::cleanPath(repo.mirrorPath) ||
        !QFileInfo(oldMirror).isDir() ||
        !QFileInfo(QDir(oldMirror).filePath(QStringLiteral("HEAD"))).isFile())
        return false;
    QDir().mkpath(QFileInfo(repo.mirrorPath).absolutePath());
    if (QDir().rename(oldMirror, repo.mirrorPath)) {
        logSystem(QStringLiteral(
                      "Mirror: moved %1/%2's served mirror from %3 to %4 "
                      "(record and on-disk mirror had diverged).")
                      .arg(repo.owner, repo.name, oldMirror, repo.mirrorPath));
        // The hook and push URL inside the working copy still name the old
        // path; ensurePushHook rewrites both now that the target exists.
        ensurePushHook(repo);
        return false; // record unchanged; only the directory moved
    }
    // Could not move (permissions, cross-device): serve the mirror where it
    // actually is instead of attesting a path that doesn't exist.
    repo.mirrorPath = oldMirror;
    logSystem(QStringLiteral(
                  "Mirror: %1/%2's recorded mirror path was missing; using the "
                  "existing mirror at %3.")
                  .arg(repo.owner, repo.name, oldMirror));
    return true;
}

void MainWindow::adoptMaterializedMirror(RepositoryRecord &repo,
                                         const QString &repositoryPath)
{
    if (repositoryPath.trimmed().isEmpty() ||
        QDir::cleanPath(repo.mirrorPath) == QDir::cleanPath(repositoryPath)) {
        repo.mirrorPath = repositoryPath;
        return;
    }
    const int carried = carryMirrorReleaseCas(repo.mirrorPath, repositoryPath);
    if (carried > 0)
        logSystem(QStringLiteral(
                      "Mirror: carried %1 release artifact blob(s) for %2/%3 "
                      "into the freshly sealed mirror.")
                      .arg(carried)
                      .arg(repo.owner, repo.name));
    repo.mirrorPath = repositoryPath;
}

void MainWindow::saveRepositories() const
{
    QSettings settings;
    const int permanentCount =
        int(std::count_if(m_repositories.cbegin(), m_repositories.cend(),
                          [](const RepositoryRecord &repo) {
                              return !repo.previewOnly;
                          }));
    settings.beginWriteArray(kRepositoriesArray, permanentCount);
    int saved = 0;
    for (int i = 0; i < m_repositories.size(); ++i) {
        const RepositoryRecord &repo = m_repositories.at(i);
        if (repo.previewOnly)
            continue;
        settings.setArrayIndex(saved++);
        settings.setValue("owner", repo.owner);
        settings.setValue("name", repo.name);
        settings.setValue("description", repo.description);
        settings.setValue("cloneUrl", repo.cloneUrl);
        settings.setValue("localPath", repo.localPath);
        settings.setValue("solanaAddress", repo.solanaAddress);
        // A private mirror path is an owner-only runtime materialization. Once
        // an opaque encrypted replica exists, never persist that temporary
        // plaintext path. A legacy path is retained only until the first
        // authenticated sealing pass can migrate and remove it safely.
        settings.setValue(
            "mirrorPath",
            (!repo.privateReplicaId.isEmpty() ||
             !repo.publicArchiveId.isEmpty())
                ? QString()
                : repo.mirrorPath);
        settings.setValue("privateReplicaId", repo.privateReplicaId);
        settings.setValue("publicArchiveId", repo.publicArchiveId);
        settings.setValue("publishToNetwork", repo.publishToNetwork);
        settings.setValue("isPrivate", repo.isPrivate);
        settings.setValue("actionsEnabled", repo.actionsEnabled);
        settings.setValue("externallyManagedActions",
                          repo.externallyManagedActions);
        settings.setValue("externalActionsSource",
                          repo.externalActionsSource);
        settings.setValue("externalActionsRef", repo.externalActionsRef);
        settings.setValue("secretScanningEnabled", repo.secretScanningEnabled);
        settings.setValue("disabledWorkflows", repo.disabledWorkflows);
        settings.setValue("workflowNodes", repo.workflowNodes);
        settings.setValue("hostedSinceMs", repo.hostedSinceMs);
        settings.setValue("lastSyncMs", repo.lastSyncMs);
        settings.setValue("publishedAtMs", repo.publishedAtMs);
    }
    settings.endArray();
}

QStringList MainWindow::mentionCandidateNames() const
{
    QSet<QString> seen;
    QStringList names;
    auto add = [&seen, &names](const QString &raw) {
        const QString n = raw.trimmed();
        if (n.isEmpty() || seen.contains(n.toLower()))
            return;
        seen.insert(n.toLower());
        names.append(n);
    };

    // Every node the relay knows about: connected, discovered, and any that
    // advertise mirroring/sharing a repo. This is the bulk of the list and is
    // already in memory, so building it is cheap.
    for (const MemberInfo &m : m_homeRoster)
        add(m.name);

    // Contributors to the issues/PRs currently loaded for this repo, so authors
    // who opened/commented but aren't online right now are still suggestible.
    for (const Issue &iss : m_currentIssues) {
        add(iss.authorName);
        for (const IssueEvent &ev : iss.events)
            add(ev.authorName);
    }
    for (const PullRequest &pr : m_currentPulls) {
        add(pr.authorName);
        for (const PullEvent &ev : pr.events)
            add(ev.authorName);
    }

    std::sort(names.begin(), names.end(), [](const QString &a, const QString &b) {
        return a.localeAwareCompare(b) < 0;
    });
    return names;
}

void MainWindow::refreshRepositoryList()
{
    // Re-entrancy guard (adhoc #247): the periodic m_homeStatsTimer fires this once
    // a minute, which can land inside another heavy refresh's GitKeepAlive pump.
    // Running the per-repo git reads (mirror head/commit/size) plus
    // refreshRepoSyncIndicators nested in that pump stacks synchronous git work and
    // stalls the GUI. Coalesce + defer to a fresh event-loop turn instead; the
    // deferred call re-checks the guard and re-arms if the pump is still active.
    if (m_heavyRefreshInFlight) {
        if (!m_repoListRefreshQueued) {
            m_repoListRefreshQueued = true;
            QTimer::singleShot(250, this, [this] {
                m_repoListRefreshQueued = false;
                refreshRepositoryList();
            });
        }
        return;
    }
    const ScopedFlag refreshGuard(m_heavyRefreshInFlight);

    m_repoMenuEntries.clear();
    m_nodeMenuEntries.clear();

    // Repos grouped by node (owner).
    QHash<QString, QList<int>> reposByNode;
    for (int i = 0; i < m_repositories.size(); ++i)
        reposByNode[m_repositories.at(i).owner].append(i);

    // Node order: connected nodes first (the old leaderboard ranking — you, then
    // online, then by name), then any repo owners that aren't connected. Each
    // node is shown with its repos nested underneath.
    struct NodeInfo {
        bool inRoster = false;
        bool online = false;
        bool self = false;
        QString solana;
        QString balance;
        QString platform;
        QStringList mirrors;
    };
    QHash<QString, NodeInfo> nodes;
    QStringList nodeOrder;
    QList<MemberInfo> ranked = m_homeRoster;
    std::sort(ranked.begin(), ranked.end(),
              [](const MemberInfo &a, const MemberInfo &b) {
                  if (a.self != b.self)
                      return a.self;
                  if (a.online != b.online)
                      return a.online;
                  return a.name.localeAwareCompare(b.name) < 0;
              });
    for (const MemberInfo &m : std::as_const(ranked)) {
        // Temporary world/website chat visitors are humans passing through the
        // public room, not serving nodes — never turn them into node entries
        // (adhoc #308: "World Guest fb9d" rows in the Nodes list / dropdown).
        if (isTemporaryChatGuest(m))
            continue;
        // Key by the node's stable identity, not just its chat display name, so a
        // headless mirror that shares/omits the owner's chat name still gets its
        // own row instead of collapsing into the owner (adhoc: mirror2/mirror3
        // missing from the Nodes list).
        const QString nodeKey = nodeListIdentityKey(m);
        if (nodeKey.isEmpty())
            continue;
        if (!nodes.contains(nodeKey)) {
            NodeInfo ni;
            ni.inRoster = true;
            ni.online = m.online;
            ni.self = m.self;
            ni.solana = m.solanaAddress.trimmed();
            if (ni.self && ni.solana.isEmpty())
                ni.solana = savedSolanaAddress();
            ni.balance = m.solanaBalance.trimmed();
            ni.platform = m.platform;
            ni.mirrors = m.mirrors;
            nodes.insert(nodeKey, ni);
            nodeOrder.append(nodeKey);
        } else {
            NodeInfo &ni = nodes[nodeKey];
            if (m.online)
                ni.online = true;
            if (ni.platform.isEmpty())
                ni.platform = m.platform;
            if (!m.mirrors.isEmpty())
                ni.mirrors = m.mirrors;
        }
    }
    for (int i = 0; i < m_repositories.size(); ++i) {
        const RepositoryRecord &repo = m_repositories.at(i);
        const QString owner = repo.owner;
        if (!nodes.contains(owner)) {
            if (repo.previewOnly)
                continue;
            nodes.insert(owner, NodeInfo());
            nodeOrder.append(owner);
        }
    }

    // Repos already shown locally by "owner/name", so advertised mirrors are
    // not duplicated.
    QSet<QString> shownLocalRepoKeys;
    for (const RepositoryRecord &repo : std::as_const(m_repositories)) {
        const QString key = repo.owner + "/" + repo.name;
        shownLocalRepoKeys.insert(key);
    }
    QSet<QString> shownAdvertised; // dedupe a repo advertised by several nodes

    // --- Nodes dropdown: one entry per node for the top-bar node switcher. The
    // OS badge (Linux/Windows/mac) signals online (tinted) vs offline (grey).
    bool selectedStillExists = false;
    for (const QString &node : std::as_const(nodeOrder)) {
        const NodeInfo info = nodes.value(node);
        NodeMenuEntry entry;
        entry.name = node;
        entry.platform = info.platform;
        entry.online = info.inRoster && info.online;
        entry.self = info.self;
        entry.repoCount = reposByNode.value(node).size();
        m_nodeMenuEntries.append(entry);
        if (node == m_selectedNode)
            selectedStillExists = true;
    }

    // Default the selection to the first node (the ranking puts you first) when
    // nothing is selected yet or the previously-selected node went away.
    if (!selectedStillExists)
        m_selectedNode = nodeOrder.isEmpty() ? QString() : nodeOrder.first();
    updateNodeSwitcher();

    // --- Repositories dropdown: the repos owned by the selected node, plus any
    // repos that node advertises mirroring that we don't already have.
    const NodeInfo selInfo = nodes.value(m_selectedNode);
    for (int i : reposByNode.value(m_selectedNode)) {
        const RepositoryRecord &repo = m_repositories.at(i);
        const bool online = repo.publishedAtMs > 0;
        QString label = repo.name;
        if (repo.previewOnly)
            label += "  \xC2\xB7 preview";
        else if (!repo.publishToNetwork)
            label += "  \xC2\xB7 local only";
        else if (repo.isPrivate && !accountOwner().isEmpty() &&
                 repo.owner != accountOwner())
            // A private repo we don't own can only be here because its owner
            // shared it with us (issue #9).
            label += "  \xC2\xB7 shared";
        else
            label += repo.isPrivate ? "  \xC2\xB7 private"
                                    : "  \xC2\xB7 public";
        if (m_syncingRepos.contains(i))
            label += repo.previewOnly ? "  \xC2\xB7 caching" : "  \xC2\xB7 syncing";
        // Two entries can share the same owner+name when a catalog preview of a
        // repo coexists with a locally hosted copy of it (issue: dropdown showed
        // duplicate-looking names with no explanation). Spell out the difference
        // in a tooltip since the label alone has no room for it.
        QString detail;
        if (repo.previewOnly) {
            detail = QStringLiteral("Cached preview \xE2\x80\x94 browsed from the "
                                    "network, not added to this device");
            if (!repo.cloneUrl.isEmpty())
                detail += QStringLiteral("\nSource: %1").arg(repo.cloneUrl);
        } else {
            detail = QStringLiteral("Hosted on this device");
            if (!repo.localPath.isEmpty())
                detail += QStringLiteral("\nLocal path: %1").arg(repo.localPath);
            if (!repo.cloneUrl.isEmpty())
                detail += QStringLiteral("\nImported from: %1").arg(repo.cloneUrl);
        }
        RepoMenuEntry entry;
        entry.label = label;
        entry.index = i;
        entry.detail = detail;
        // A repo glyph: green when published+online on the web, grey otherwise.
        entry.icon = themedOcticon(
            repo.previewOnly ? QStringLiteral("cloud") : QStringLiteral("repo"),
            repo.previewOnly ? QColor("#58a6ff")
                             : (repo.publishToNetwork && online ? QColor("#2ea043")
                                                                : QColor("#6e7681")),
            14);
        m_repoMenuEntries.append(entry);
    }
    for (const QString &ownerName : selInfo.mirrors) {
        if (shownLocalRepoKeys.contains(ownerName) ||
            shownAdvertised.contains(ownerName))
            continue;
        shownAdvertised.insert(ownerName);
        RepoMenuEntry entry;
        entry.label = QString::fromUtf8("\xE2\x86\x93 ") + ownerName +
                      QString::fromUtf8("   \xC2\xB7 browse");
        entry.index = -2; // advertised mirror marker
        entry.advertised = ownerName;
        entry.icon = themedOcticon("cloud", QColor("#58a6ff"), 14);
        m_repoMenuEntries.append(entry);
    }
    updateRepoSwitcher();
    refreshRepoSyncIndicators();
    updateRepoDetailStatus();
    updateRepoActionMenus();
    updateHomeStats();

    // Auto-select the selected node's first repository when nothing is open yet
    // (e.g. on a fresh install where repos arrive asynchronously) so the user
    // lands on real content instead of an empty panel. A node switch runs its own
    // first-repo open, so skip while one is in flight. Held off until deferred
    // startup so it never races the last-open-repository restore. When the node
    // has no repos at all, clear the panel — updateRepoSwitcher hides the section.
    int firstRepo = -1;
    for (const RepoMenuEntry &entry : std::as_const(m_repoMenuEntries))
        if (entry.index >= 0) {
            firstRepo = entry.index;
            break;
        }
    if (m_deferredStartupRun && m_pendingRestoreRepoIndex < 0 &&
        !m_nodeSwitching && !m_repoDetailLoading) {
        if (m_repoDetailIndex < 0 && firstRepo >= 0)
            openRepoDetailDeferred(firstRepo);
        else if (firstRepo < 0 && m_repoDetailIndex >= 0)
            clearRepoDetail();
    }

    refreshMirrorAdverts();
}

void MainWindow::refreshMirrorAdverts()
{
    if (!m_backend)
        return;

    // This signature is intentionally filesystem-only. The old "skip" check
    // launched several synchronous Git subprocesses just to decide whether the
    // expensive snapshot could be skipped, defeating its own purpose.
    QString inputSignature;
    struct AdvertInput {
        RepositoryRecord repo;
        QString catalogOwner;
    };
    QList<AdvertInput> repositories;
    for (const RepositoryRecord &repo : std::as_const(m_repositories)) {
        if (repo.previewOnly)
            continue;
        repositories.append({repo, catalogOwner(repo)});
        const QFileInfo mirrorHead(
            QDir(repo.mirrorPath).filePath(QStringLiteral("HEAD")));
        const QFileInfo mirrorRefs(
            QDir(repo.mirrorPath).filePath(QStringLiteral("refs")));
        const QFileInfo workHead(
            QDir(repo.localPath).filePath(QStringLiteral(".git/HEAD")));
        const QFileInfo worktrees(
            QDir(repo.localPath).filePath(QStringLiteral(".git/worktrees")));
        inputSignature +=
            repo.owner + QLatin1Char('|') + repo.name + QLatin1Char('|') +
            repo.cloneUrl + QLatin1Char('|') +
            QString::number(repo.publishToNetwork) + QLatin1Char('|') +
            repo.mirrorPath + QLatin1Char('|') +
            QString::number(repo.lastSyncMs) + QLatin1Char('|') +
            repo.localPath + QLatin1Char('|') +
            QString::number(mirrorHead.lastModified().toMSecsSinceEpoch()) +
            QLatin1Char('|') +
            QString::number(mirrorRefs.lastModified().toMSecsSinceEpoch()) +
            QLatin1Char('|') +
            QString::number(workHead.lastModified().toMSecsSinceEpoch()) +
            QLatin1Char('|') +
            QString::number(worktrees.lastModified().toMSecsSinceEpoch()) +
            QLatin1Char('\n');
    }

    if (m_mirrorAdvertRefreshInFlight)
        return;
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    constexpr qint64 kAdvertRefreshMs = 60 * 1000;
    if (!m_mirrorAdvertSig.isEmpty() &&
        inputSignature == m_mirrorAdvertInputSig &&
        m_mirrorAdvertCompletedAtMs > 0 &&
        now - m_mirrorAdvertCompletedAtMs < kAdvertRefreshMs) {
        return;
    }

    m_mirrorAdvertRefreshInFlight = true;
    m_mirrorAdvertInputSig = inputSignature;
    auto adverts = std::make_shared<QList<MirrorAdvert>>();
    QThread *worker = QThread::create([repositories, adverts] {
        const forkmesh::BackgroundScope activity(
            QStringLiteral("mirrors"),
            QStringLiteral("refresh %1 repository advert(s)")
                .arg(repositories.size()),
            forkmesh::ActionTelemetry::Execution::Worker);
        for (const AdvertInput &input : repositories) {
            const RepositoryRecord &repo = input.repo;
            MirrorAdvert advert;
            advert.ownerName =
                input.catalogOwner + "/" +
                repoSegment(repo.name, QStringLiteral("repository"));
            advert.source =
                repoSegment(repo.owner, QStringLiteral("owner")) + "/" +
                repoSegment(repo.name, QStringLiteral("repository"));
            const MirrorBranchTip primaryTip =
                mirrorPrimaryBranchTip(repo.mirrorPath, repo.localPath);
            advert.branch = primaryTip.branch;
            advert.commit = primaryTip.commit;
            if (advert.commit.isEmpty())
                continue;
            advert.commitIdentity = mirrorCommitIdentity(
                repo.mirrorPath, repo.localPath, advert.commit);
            advert.updatedMs = repo.lastSyncMs;
            advert.sizeBytes = mirrorRepoSizeBytes(repo.mirrorPath);
            advert.issueCount =
                mirrorIssueCount(repo.mirrorPath, advert.branch);
            advert.commitCount =
                mirrorCommitCount(repo.mirrorPath, advert.branch);
            advert.branchCount = mirrorBranchCount(repo.mirrorPath);
            advert.pullCount =
                mirrorPullCount(repo.mirrorPath, advert.branch);
            advert.discussionCount =
                mirrorDiscussionCount(repo.mirrorPath, advert.branch);
            advert.worktreeCount = mirrorWorktreeCount(repo.localPath);
            advert.artifactCount = mirrorArtifactCount(repo.mirrorPath);
            adverts->append(advert);
        }
    });
    connect(worker, &QThread::finished, worker, &QObject::deleteLater);
    connect(worker, &QThread::finished, this,
            [this, adverts, inputSignature] {
                m_mirrorAdvertRefreshInFlight = false;
                m_mirrorAdvertCompletedAtMs =
                    QDateTime::currentMSecsSinceEpoch();
                m_mirrorAdvertSig = inputSignature;
                if (m_backend)
                    m_backend->setMirroredRepos(*adverts);
                // If a sync landed while this worker was reading, immediately
                // queue the newer snapshot; unchanged data remains throttled.
                refreshMirrorAdverts();
            });
    worker->start();
}

void MainWindow::mirrorAdvertisedRepo(const QString &ownerName)
{
    const int slash = ownerName.indexOf('/');
    if (slash <= 0)
        return;
    const QString owner = ownerName.left(slash);
    const QString name = ownerName.mid(slash + 1);
    int previewIndex = -1;
    for (int i = 0; i < m_repositories.size(); ++i) {
        const RepositoryRecord &r = m_repositories.at(i);
        if (r.owner != owner || r.name != name)
            continue;
        if (r.previewOnly) {
            previewIndex = i;
            continue;
        }
        QMessageBox::information(this, "Mirror",
                                 "You already mirror this repository.");
        return;
    }
    if (previewIndex >= 0) {
        mirrorPreviewRepository(previewIndex);
        return;
    }
    const QString cloneUrl = repositoryNetworkCloneUrl(owner, name);
    if (cloneUrl.isEmpty())
        return;

    if (QMessageBox::question(
            this, "Mirror it too",
            QStringLiteral("Mirror %1 into your local mirrors?\n\nIt will be cloned "
                           "from %2.")
                .arg(ownerName, cloneUrl)) != QMessageBox::Yes)
        return;

    RepositoryRecord repo;
    repo.owner = owner;
    repo.name = name;
    repo.cloneUrl = cloneUrl;
    repo.publishToNetwork = true;
    // Mirrored repos start with actions off; opt in per repo on Settings.
    repo.actionsEnabled = false;
    repo.hostedSinceMs = QDateTime::currentMSecsSinceEpoch();
    repo.mirrorPath = repositoryMirrorRoot() + "/" +
                      repoSegment(owner, QStringLiteral("owner")) + "-" +
                      repoSegment(name, QStringLiteral("repository")) + ".git";
    m_repositories.append(repo);
    saveRepositories();
    if (m_backend)
        m_backend->addChannel(repositoryChannel(repo));
    refreshRepositoryList();
    logSystem("Mirroring " + ownerName + " from " + cloneUrl);
    syncRepository(m_repositories.size() - 1); // clone from the network mirror
}

void MainWindow::previewAdvertisedRepo(const QString &ownerName)
{
    const int slash = ownerName.indexOf('/');
    if (slash <= 0)
        return;
    const QString owner = ownerName.left(slash);
    const QString name = ownerName.mid(slash + 1);

    for (int i = 0; i < m_repositories.size(); ++i) {
        const RepositoryRecord &repo = m_repositories.at(i);
        if (repo.owner == owner && repo.name == name) {
            openRepoDetail(i);
            if (repo.previewOnly && !m_syncingRepos.contains(i) &&
                !QDir(repo.mirrorPath).exists())
                syncRepository(i);
            return;
        }
    }

    RepositoryRecord repo;
    repo.owner = owner;
    repo.name = name;
    repo.cloneUrl = repositoryNetworkCloneUrl(owner, name);
    repo.previewOnly = true;
    // Mirrored repos start with actions off (also applies once promoted).
    repo.actionsEnabled = false;
    repo.hostedSinceMs = QDateTime::currentMSecsSinceEpoch();
    repo.mirrorPath = repositoryPreviewPath(owner, name);
    m_repositories.append(repo);
    const int index = m_repositories.size() - 1;
    refreshRepositoryList();
    logSystem("Preview: caching " + ownerName + " from " + repo.cloneUrl);
    openRepoDetail(index);
    syncRepository(index);
}

void MainWindow::mirrorPreviewRepository(int index)
{
    if (index < 0 || index >= m_repositories.size())
        return;
    const RepositoryRecord preview = m_repositories.at(index);
    if (!preview.previewOnly)
        return;

    for (int i = 0; i < m_repositories.size(); ++i) {
        if (i == index)
            continue;
        const RepositoryRecord &repo = m_repositories.at(i);
        if (!repo.previewOnly && repo.owner == preview.owner &&
            repo.name == preview.name) {
            QMessageBox::information(
                this, "Mirror repository",
                QStringLiteral("You already mirror %1/%2.")
                    .arg(preview.owner, preview.name));
            return;
        }
    }

    const QString permanentPath = repositoryMirrorRoot() + "/" +
                                  repoSegment(preview.owner, QStringLiteral("owner")) +
                                  "-" +
                                  repoSegment(preview.name,
                                              QStringLiteral("repository")) +
                                  ".git";
    const QString source =
        (!preview.mirrorPath.isEmpty() && QDir(preview.mirrorPath).exists())
            ? preview.mirrorPath
            : preview.cloneUrl;
    if (source.isEmpty()) {
        setRepoDetailNotice("Preview is not cached yet; try again after it loads.",
                            true);
        return;
    }

    if (!QDir().mkpath(QFileInfo(permanentPath).absolutePath())) {
        QMessageBox::warning(this, "Mirror repository",
                             "Could not create " +
                                 QFileInfo(permanentPath).absolutePath());
        return;
    }

    if (QDir(permanentPath).exists()) {
        RepositoryRecord &repo = m_repositories[index];
        repo.previewOnly = false;
        repo.publishToNetwork = true;
        repo.hostedSinceMs = QDateTime::currentMSecsSinceEpoch();
        repo.mirrorPath = permanentPath;
        if (repo.lastSyncMs <= 0)
            repo.lastSyncMs = QDateTime::currentMSecsSinceEpoch();
        saveRepositories();
        ensurePushHook(repo);
        if (m_backend)
            m_backend->addChannel(repositoryChannel(repo));
        publishRepositoryAfterMirrorRefresh(index, false);
        startRepoHosts();
        refreshRepositoryList();
        openRepoDetail(index);
        setRepoDetailNotice("Mirroring " + repo.owner + "/" + repo.name + ".");
        return;
    }

    m_syncingRepos.insert(index, false);
    refreshRepositoryList();
    setRepoDetailNotice("Creating permanent mirror...");
    logSystem(QStringLiteral("Mirror: promoting preview %1/%2 from %3 to %4.")
                  .arg(preview.owner, preview.name, source, permanentPath));

    auto *process = new QProcess(this);
    connect(process, &QProcess::finished, this,
            [this, process, index, permanentPath](int exitCode,
                                                  QProcess::ExitStatus status) {
                const QString errors =
                    QString::fromUtf8(process->readAllStandardError()).trimmed();
                process->deleteLater();
                m_syncingRepos.remove(index);
                if (index < 0 || index >= m_repositories.size()) {
                    refreshRepositoryList();
                    return;
                }
                RepositoryRecord &repo = m_repositories[index];
                if (status == QProcess::NormalExit && exitCode == 0) {
                    repo.previewOnly = false;
                    repo.publishToNetwork = true;
                    repo.hostedSinceMs = QDateTime::currentMSecsSinceEpoch();
                    repo.lastSyncMs = QDateTime::currentMSecsSinceEpoch();
                    repo.mirrorPath = permanentPath;
                    saveRepositories();
                    ensurePushHook(repo);
                    if (m_backend)
                        m_backend->addChannel(repositoryChannel(repo));
                    publishRepositoryAfterMirrorRefresh(index, false);
                    startRepoHosts();
                    refreshRepositoryList();
                    openRepoDetail(index);
                    logSystem("Mirror: added " + repo.owner + "/" + repo.name +
                              " from preview cache.");
                    setRepoDetailNotice("Mirroring " + repo.owner + "/" +
                                        repo.name + ".");
                } else {
                    refreshRepositoryList();
                    logSystem("Mirror: could not promote preview: " +
                              errors.right(300));
                    setRepoDetailNotice(
                        "Could not create mirror" +
                            (errors.isEmpty() ? QString() :
                                                ": " + errors.right(160)),
                        true);
                }
            });
    connect(process, &QProcess::errorOccurred, this,
            [this, process, index](QProcess::ProcessError) {
                process->deleteLater();
                m_syncingRepos.remove(index);
                refreshRepositoryList();
                setRepoDetailNotice("Could not run git. Install Git and try again.",
                                    true);
            });
    trackProcessActivity(process, QStringLiteral("fork"),
                         QStringLiteral("Mirroring %1/%2")
                             .arg(preview.owner, preview.name));
    process->start(QStringLiteral("git"),
                   {QStringLiteral("clone"), QStringLiteral("--mirror"), source,
                    permanentPath});
}

void MainWindow::promptAddRepository()
{
    // Simple flow: pick a local Git repository. Everything else is derived.
    // The folder is mirrored locally and only signed metadata is published to
    // the website; the .git data never leaves this machine unless this node is
    // online and serving clone/browse requests.
    const QString path = QFileDialog::getExistingDirectory(
        this, "Choose a local Git repository to mirror and publish");
    if (path.isEmpty())
        return;

    const bool looksLikeGit =
        QDir(path).exists(".git") || QDir(path).exists("HEAD");
    if (!looksLikeGit) {
        QMessageBox::warning(
            this, "Add repository",
            "That folder is not a Git repository. Choose a folder created by "
            "\"git init\" or \"git clone\".");
        return;
    }

    const QString name = repoNameFromUrl(path);
    if (repoIndexFor(accountOwner(), name) >= 0) {
        QMessageBox::warning(
            this, "Add repository",
            QStringLiteral("You already have a repository named \"%1\".").arg(name));
        return;
    }

    QMessageBox visibility(this);
    visibility.setWindowTitle(QStringLiteral("Repository visibility"));
    visibility.setIcon(QMessageBox::Question);
    visibility.setText(QStringLiteral("Mirror %1 as a public or private repository?")
                           .arg(name));
    visibility.setInformativeText(QStringLiteral(
        "Public repositories appear in the catalog. Private repositories are "
        "hidden from public browse and clone routes unless shared."));
    QPushButton *publicButton =
        visibility.addButton(QStringLiteral("Public"), QMessageBox::AcceptRole);
    QPushButton *privateButton =
        visibility.addButton(QStringLiteral("Private"), QMessageBox::AcceptRole);
    visibility.addButton(QMessageBox::Cancel);
    visibility.setDefaultButton(publicButton);
    visibility.exec();
    if (visibility.clickedButton() != publicButton &&
        visibility.clickedButton() != privateButton)
        return;
    const bool isPrivate = visibility.clickedButton() == privateButton;

    RepositoryRecord repo;
    repo.localPath = path;
    repo.name = name;
    // Repos are namespaced under the single account name.
    repo.owner = accountOwner();
    repo.solanaAddress = savedSolanaAddress();
    // Selecting a local repo publishes it to the website so it shows up online
    // and others can discover and mirror it. No public clone URL is sent.
    repo.publishToNetwork = true;
    repo.isPrivate = isPrivate;
    repo.hostedSinceMs = QDateTime::currentMSecsSinceEpoch();
    repo.mirrorPath = repositoryMirrorRoot() + "/" +
                      repoSegment(repo.owner, QStringLiteral("owner")) + "-" +
                      repoSegment(repo.name, QStringLiteral("repository")) + ".git";

    m_repositories.append(repo);
    saveRepositories();
    refreshRepositoryList();
    if (m_backend)
        m_backend->addChannel(repositoryChannel(repo));

    const QJsonObject metadata{{"owner", repo.owner},
                               {"name", repo.name},
                               {"channel", repositoryChannel(repo)},
                               {"mirrorPath", repo.mirrorPath},
                               {"hostedSince", QString::number(repo.hostedSinceMs)},
                               {"visibility", repo.isPrivate
                                                  ? QStringLiteral("private")
                                                  : QStringLiteral("public")},
                               {"maintainer", m_profileIdentity.publicKey()}};
    logSystem("Repository: signed mirror metadata for " + repo.owner + "/" +
              repo.name + " with signature " +
              m_profileIdentity.signJson(metadata).left(16) + "...");
    publishRepositoryAfterMirrorRefresh(m_repositories.size() - 1, false);
}

void MainWindow::createNewRepository()
{
    // A single "new repository" screen: name + description + an optional first
    // prompt + public/private visibility + a README choice + where on disk to
    // create it. Everything past the dialog (git init, seeding, mirror +
    // publish) lives in provisionNewRepository so it can be exercised without
    // the UI.
    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("New repository"));

    auto *nameEdit = new QLineEdit(&dialog);
    nameEdit->setPlaceholderText(QStringLiteral("my-project"));

    auto *descriptionEdit = new QPlainTextEdit(&dialog);
    descriptionEdit->setPlaceholderText(
        QStringLiteral("What is this repository about?"));
    descriptionEdit->setMaximumHeight(72);

    auto *promptEdit = new QPlainTextEdit(&dialog);
    promptEdit->setPlaceholderText(QStringLiteral(
        "Optional: the first thing you want done here. Filed as issue #1 so an "
        "agent can pick it up."));
    promptEdit->setMaximumHeight(96);

    // Visibility is decided up front so a repo that should never be public is
    // never published as one: provisionNewRepository seals the private replica
    // before any catalog record exists.
    auto *publicRadio = new QRadioButton(QStringLiteral("Public"), &dialog);
    auto *privateRadio = new QRadioButton(QStringLiteral("Private"), &dialog);
    publicRadio->setCursor(Qt::PointingHandCursor);
    privateRadio->setCursor(Qt::PointingHandCursor);
    publicRadio->setChecked(true);
    publicRadio->setToolTip(
        QStringLiteral("Listed in the catalog; anyone can browse and clone it."));
    privateRadio->setToolTip(
        QStringLiteral("Hidden from the public catalog and clone routes. Only "
                       "you and people you share it with can read it."));
    auto *visibilityHint = new QLabel(
        QStringLiteral("Public repos appear in the catalog. Private repos stay "
                       "hidden until you share them."),
        &dialog);
    visibilityHint->setObjectName(QStringLiteral("statusLine"));
    visibilityHint->setWordWrap(true);
    auto *visibilityRow = new QHBoxLayout;
    visibilityRow->setContentsMargins(0, 0, 0, 0);
    visibilityRow->addWidget(publicRadio);
    visibilityRow->addWidget(privateRadio);
    visibilityRow->addStretch(1);
    auto *visibilityBox = new QVBoxLayout;
    visibilityBox->setContentsMargins(0, 0, 0, 0);
    visibilityBox->setSpacing(2);
    visibilityBox->addLayout(visibilityRow);
    visibilityBox->addWidget(visibilityHint);
    auto *visibilityWidget = new QWidget(&dialog);
    visibilityWidget->setLayout(visibilityBox);

    auto *readmeBox =
        new QCheckBox(QStringLiteral("Add a README on the main branch"), &dialog);
    readmeBox->setChecked(true);

    // Where the working copy is created. Defaults to the home folder; the folder
    // the repo lands in is <location>/<name>.
    auto *locationEdit = new QLineEdit(QDir::homePath(), &dialog);
    auto *browseButton = new QPushButton(QStringLiteral("Browse\xE2\x80\xA6"), &dialog);
    browseButton->setCursor(Qt::PointingHandCursor);
    connect(browseButton, &QPushButton::clicked, &dialog, [&dialog, locationEdit] {
        const QString picked = QFileDialog::getExistingDirectory(
            &dialog, QStringLiteral("Choose where to create the repository"),
            locationEdit->text().isEmpty() ? QDir::homePath()
                                           : locationEdit->text());
        if (!picked.isEmpty())
            locationEdit->setText(picked);
    });
    auto *locationRow = new QHBoxLayout;
    locationRow->setContentsMargins(0, 0, 0, 0);
    locationRow->addWidget(locationEdit, 1);
    locationRow->addWidget(browseButton);
    auto *locationWidget = new QWidget(&dialog);
    locationWidget->setLayout(locationRow);

    auto *form = new QFormLayout;
    form->addRow(QStringLiteral("Name"), nameEdit);
    form->addRow(QStringLiteral("Description"), descriptionEdit);
    form->addRow(QStringLiteral("First prompt"), promptEdit);
    form->addRow(QStringLiteral("Visibility"), visibilityWidget);
    form->addRow(QString(), readmeBox);
    form->addRow(QStringLiteral("Location"), locationWidget);

    auto *buttons =
        new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel,
                             Qt::Horizontal, &dialog);
    buttons->button(QDialogButtonBox::Ok)->setText(QStringLiteral("Create"));
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    auto *layout = new QVBoxLayout(&dialog);
    layout->addLayout(form);
    layout->addWidget(buttons);

    connect(buttons, &QDialogButtonBox::accepted, &dialog, [&] {
        const QString rawName = nameEdit->text().trimmed();
        if (rawName.isEmpty()) {
            QMessageBox::warning(&dialog, "New repository",
                                 QStringLiteral("Enter a repository name."));
            return;
        }
        // Keep the on-disk folder name in step with the published name: both go
        // through repoSegment so a slash or odd character can't escape the path.
        const QString name = repoSegment(rawName, QStringLiteral("repository"));
        if (repoIndexFor(accountOwner(), name) >= 0) {
            QMessageBox::warning(&dialog, "New repository",
                                 QStringLiteral(
                                     "You already have a repository named \"%1\".")
                                     .arg(name));
            return;
        }
        const QString parent = locationEdit->text().trimmed();
        if (parent.isEmpty()) {
            QMessageBox::warning(&dialog, "New repository",
                                 QStringLiteral("Choose where to create it."));
            return;
        }
        const QString dest = QDir(parent).filePath(name);
        if (QDir(dest).exists() && !QDir(dest).isEmpty()) {
            QMessageBox::warning(
                &dialog, "New repository",
                QStringLiteral("%1 already exists and is not empty. Choose "
                               "another name or location.")
                    .arg(dest));
            return;
        }
        QString error;
        const int index = provisionNewRepository(
            dest, name, descriptionEdit->toPlainText(), promptEdit->toPlainText(),
            readmeBox->isChecked(), privateRadio->isChecked(), &error);
        if (index < 0) {
            QMessageBox::warning(&dialog, "New repository",
                                 error.isEmpty()
                                     ? QStringLiteral("Could not create the "
                                                      "repository.")
                                     : error);
            return;
        }
        dialog.accept();
    });

    nameEdit->setFocus();
    dialog.exec();
}

int MainWindow::provisionNewRepository(const QString &dest, const QString &name,
                                       const QString &description,
                                       const QString &firstPrompt, bool addReadme,
                                       bool isPrivate, QString *error)
{
    const auto fail = [&](const QString &message) -> int {
        if (error)
            *error = message;
        return -1;
    };

    if (!QDir(dest).exists() && !QDir().mkpath(dest))
        return fail(QStringLiteral("Could not create %1.").arg(dest));

    // A small git runner scoped to the new working copy. All the seeding here is
    // local; the mirror + catalog record come later via publish.
    const auto runGit = [&dest](const QStringList &args, QString *errOut) -> bool {
        QProcess git;
        git.setWorkingDirectory(dest);
        git.start(QStringLiteral("git"), args);
        git.waitForFinished(30000);
        if (git.exitStatus() != QProcess::NormalExit || git.exitCode() != 0) {
            if (errOut)
                *errOut =
                    QString::fromUtf8(git.readAllStandardError()).trimmed().right(200);
            return false;
        }
        return true;
    };

    // `git init -b main` gives the new repo a conventional default branch so the
    // first push lands on refs/heads/main like everywhere else.
    QString gitErr;
    if (!runGit({QStringLiteral("init"), QStringLiteral("-b"),
                 QStringLiteral("main")},
                &gitErr)) {
        logSystem("New repository: git init failed in " + dest + ": " + gitErr);
        return fail(QStringLiteral("git init failed: %1").arg(gitErr));
    }

    const QString about = description.trimmed();
    const QDir repoDir(dest);
    bool seeded = false;

    // README on main: a title + the description so the repo is never a blank
    // page on the network.
    if (addReadme) {
        QString body = QStringLiteral("# %1\n").arg(name);
        if (!about.isEmpty())
            body += QStringLiteral("\n%1\n").arg(about);
        QSaveFile readme(repoDir.filePath(QStringLiteral("README.md")));
        if (readme.open(QIODevice::WriteOnly)) {
            const QByteArray data = body.toUtf8();
            if (readme.write(data) == data.size() && readme.commit())
                seeded = true;
        }
        if (!seeded)
            logSystem("New repository: could not write README.md in " + dest);
    }

    // Description lives in .forkmesh/info.json, the same canonical spot the
    // About editor writes, so the browse UI and catalog pick it up unchanged.
    if (!about.isEmpty() && repoDir.mkpath(QStringLiteral(".forkmesh"))) {
        QJsonObject info;
        info.insert(QStringLiteral("about"), about);
        const QByteArray data =
            QJsonDocument(info).toJson(QJsonDocument::Indented);
        QSaveFile file(repoDir.filePath(kRepoInfoPath));
        if (file.open(QIODevice::WriteOnly) && file.write(data) == data.size() &&
            file.commit())
            seeded = true;
        else
            logSystem("New repository: could not write " + QString(kRepoInfoPath) +
                      " in " + dest);
    }

    // Initial commit on main. Prefer the user's configured git identity; fall
    // back to an account-scoped identity so a fresh box with no global config
    // still gets a valid first commit.
    if (seeded) {
        if (!runGit({QStringLiteral("add"), QStringLiteral("-A")}, &gitErr))
            logSystem("New repository: git add failed in " + dest + ": " + gitErr);
        const QStringList commitArgs{QStringLiteral("commit"), QStringLiteral("-m"),
                                     QStringLiteral("Initial commit")};
        if (!runGit(commitArgs, &gitErr)) {
            const QString who = accountOwner().isEmpty() ? QStringLiteral("forkmesh")
                                                         : accountOwner();
            const QStringList fallback{
                QStringLiteral("-c"),
                QStringLiteral("user.name=%1").arg(who),
                QStringLiteral("-c"),
                QStringLiteral("user.email=%1@forkmesh.local").arg(who)};
            if (!runGit(fallback + commitArgs, &gitErr))
                logSystem("New repository: initial commit failed in " + dest +
                          ": " + gitErr);
        }
    }

    RepositoryRecord repo;
    repo.localPath = dest;
    repo.name = name;
    repo.owner = accountOwner();
    repo.description = about;
    repo.solanaAddress = savedSolanaAddress();
    repo.publishToNetwork = true;
    // A repo created private never has a public catalog record: publish below
    // routes through syncRepository, which seals the private replica instead.
    repo.isPrivate = isPrivate;
    repo.hostedSinceMs = QDateTime::currentMSecsSinceEpoch();
    repo.mirrorPath = repositoryMirrorRoot() + "/" +
                      repoSegment(repo.owner, QStringLiteral("owner")) + "-" +
                      repoSegment(repo.name, QStringLiteral("repository")) + ".git";

    m_repositories.append(repo);
    const int index = m_repositories.size() - 1;
    saveRepositories();
    refreshRepositoryList();
    if (m_backend)
        m_backend->addChannel(repositoryChannel(repo));

    // The first prompt becomes issue #1 so the repo lands with a task an agent
    // can immediately act on. Its own commit rides along in the same mirror.
    const QString prompt = firstPrompt.trimmed();
    if (!prompt.isEmpty()) {
        const QString title = prompt.section('\n', 0, 0).trimmed().left(120);
        const QString body = prompt == title ? QString() : prompt;
        QString issueErr;
        IssueStore store(repo.localPath, repo.mirrorPath, &m_profileIdentity,
                         chatDisplayName());
        if (store.createIssue(title, body, {}, QString(), 0, {}, {}, &issueErr) < 0)
            logSystem("New repository: could not file first-prompt issue for " +
                      repo.owner + "/" + repo.name + ": " + issueErr);
    }

    publishRepositoryAfterMirrorRefresh(index, false);
    logSystem("New repository: created " +
              QString(repo.isPrivate ? "private " : "public ") + repo.owner + "/" +
              repo.name + " in " + dest + ".");
    flashMessage(QStringLiteral("Created %1/%2.").arg(repo.owner, repo.name));
    return index;
}

QStringList MainWindow::importAuthGitArgs(const QString &url) const
{
    const QString host = QUrl(url).host().toLower();
    QString token, user;
    if (host.contains(QLatin1String("github"))) {
        token = QSettings().value(kGithubTokenSetting).toString().trimmed();
        user = QStringLiteral("x-access-token"); // GitHub PATs auth as this user
    } else if (host.contains(QLatin1String("gitlab"))) {
        token = QSettings().value(kGitlabTokenSetting).toString().trimmed();
        user = QStringLiteral("oauth2"); // GitLab PATs auth as oauth2:<token>
    }
    if (token.isEmpty())
        return {};
    // Carry the token in a one-shot Authorization header rather than baking it
    // into the cloned repo's origin URL, so the secret is never persisted on disk.
    const QByteArray basic = (user + ":" + token).toUtf8().toBase64();
    return {QStringLiteral("-c"),
            QStringLiteral("http.extraHeader=Authorization: Basic ") +
                QString::fromLatin1(basic)};
}

void MainWindow::importRemoteRepository()
{
    if (!m_importUrlEdit || !m_importButton)
        return;
    const QString url = m_importUrlEdit->text().trimmed();
    auto setStatus = [this](const QString &text, bool error) {
        if (!m_importStatus)
            return;
        m_importStatus->setText(text);
        m_importStatus->setStyleSheet(error ? QStringLiteral("color:#f85149;")
                                            : QString());
        m_importStatus->setVisible(!text.isEmpty());
    };

    const QUrl parsed(url);
    if (url.isEmpty() || !parsed.isValid() ||
        (parsed.scheme() != QLatin1String("https") &&
         parsed.scheme() != QLatin1String("http"))) {
        setStatus("Enter an https URL to a GitHub or GitLab repository.", true);
        return;
    }

    const QString name = repoNameFromUrl(url);
    if (repoIndexFor(accountOwner(), name) >= 0) {
        setStatus(QStringLiteral("You already have a repository named \"%1\".")
                      .arg(name),
                  true);
        return;
    }

    // Clone into a folder the user picks, so the working copy lives where they
    // expect it (and can be opened in their own editor), defaulting to home.
    const QString parent = QFileDialog::getExistingDirectory(
        this, QStringLiteral("Choose where to clone \"%1\"").arg(name),
        QDir::homePath());
    if (parent.isEmpty())
        return; // cancelled
    const QString dest = QDir(parent).filePath(name);
    if (QDir(dest).exists() && !QDir(dest).isEmpty()) {
        setStatus(QStringLiteral("%1 already exists and is not empty. Choose "
                                 "another location.")
                      .arg(dest),
                  true);
        return;
    }

    m_importButton->setEnabled(false);
    m_importUrlEdit->setEnabled(false);
    setStatus(QStringLiteral("Cloning %1 into %2…").arg(url, dest), false);
    logSystem("Import: cloning " + url + " into " + dest + ".");

    const QStringList args =
        importAuthGitArgs(url) + QStringList{"clone", url, dest};
    auto *process = new QProcess(this);
    process->setProgram(QStringLiteral("git"));
    process->setArguments(args);
    connect(process, &QProcess::finished, this,
            [this, process, dest, name, url, setStatus](int exitCode,
                                                        QProcess::ExitStatus) {
                const QString errors =
                    QString::fromUtf8(process->readAllStandardError()).trimmed();
                process->deleteLater();
                if (m_importButton)
                    m_importButton->setEnabled(true);
                if (m_importUrlEdit)
                    m_importUrlEdit->setEnabled(true);

                if (exitCode != 0) {
                    // The token (if any) is only ever a header, so it can't leak
                    // into this stderr; still, keep the tail short and readable.
                    setStatus(QStringLiteral("Clone failed: %1")
                                  .arg(errors.right(200)),
                              true);
                    logSystem("Import: clone failed for " + url + ": " +
                              errors.right(300));
                    return;
                }

                // Mirror & publish the freshly cloned working copy under this
                // node, exactly like adding a local repository.
                RepositoryRecord repo;
                repo.localPath = dest;
                repo.name = name;
                repo.owner = accountOwner();
                repo.solanaAddress = savedSolanaAddress();
                repo.publishToNetwork = true;
                repo.hostedSinceMs = QDateTime::currentMSecsSinceEpoch();
                repo.mirrorPath =
                    repositoryMirrorRoot() + "/" +
                    repoSegment(repo.owner, QStringLiteral("owner")) + "-" +
                    repoSegment(repo.name, QStringLiteral("repository")) + ".git";

                m_repositories.append(repo);
                saveRepositories();
                refreshRepositoryList();
                if (m_backend)
                    m_backend->addChannel(repositoryChannel(repo));
                const int index = m_repositories.size() - 1;
                publishRepositoryAfterMirrorRefresh(index, false);

                setStatus(QStringLiteral("Imported %1/%2 — mirroring and "
                                         "publishing now.")
                              .arg(repo.owner, repo.name),
                          false);
                logSystem("Import: cloned " + url + " as " + repo.owner + "/" +
                          repo.name + ".");
                if (m_importUrlEdit)
                    m_importUrlEdit->clear();
            });
    trackProcessActivity(process, QStringLiteral("clone"),
                         QStringLiteral("Cloning %1").arg(url));
    process->start();
}

QString MainWindow::repositoryWebUrl(const QString &owner,
                                     const QString &name) const
{
    // Clean repository route on the public website, derived from the same host
    // that serves the catalog API. Static Assets routes this to the catalog SPA.
    // Key it by the repo's OWN owner — the node that actually hosts it (and the
    // one selected in the top bar when viewing it) — not this node's account.
    // Using catalogOwner here would point every repo at the local account and
    // open the wrong node's page for repos mirrored from other nodes.
    QUrl url = catalogApiUrl();
    url.setPath("/" + repoSegment(owner, QStringLiteral("owner")) + "/" +
                repoSegment(name, QStringLiteral("repository")));
    url.setFragment(QString());
    return url.toString();
}

QString MainWindow::repositoryWebUrl(const RepositoryRecord &repo) const
{
    return repositoryWebUrl(repo.owner, repo.name);
}

void MainWindow::updateRepoActionMenus()
{
    if (!m_mirrorMenu || !m_sourceMenu)
        return;
    m_mirrorMenu->clear();
    m_sourceMenu->clear();

    if (m_repoDetailIndex < 0 || m_repoDetailIndex >= m_repositories.size()) {
        for (QMenu *menu : {m_mirrorMenu, m_sourceMenu}) {
            QAction *empty = menu->addAction("No repository selected");
            empty->setEnabled(false);
        }
        return;
    }

    const RepositoryRecord &repo = m_repositories.at(m_repoDetailIndex);
    const bool hasMirror = !repo.mirrorPath.isEmpty() && QDir(repo.mirrorPath).exists();
    const bool online = !repo.previewOnly && repo.publishedAtMs > 0;
    const QString webUrl = repositoryWebUrl(repo);

    // Mirror owns network availability, public browse URL, local storage and
    // destructive removal. This keeps those details next to the action they
    // describe instead of permanently expanding the repository header.
    if (repo.previewOnly) {
        m_mirrorMenu->addSection(hasMirror ? "PREVIEW CACHED" : "PREVIEW PENDING");
        QAction *keep = m_mirrorMenu->addAction("Keep as a local mirror");
        connect(keep, &QAction::triggered, this, [this] {
            if (m_repoDetailIndex >= 0)
                mirrorPreviewRepository(m_repoDetailIndex);
        });
    } else {
        m_mirrorMenu->addSection("MIRROR STATUS");
        if (online) {
            QAction *browse = m_mirrorMenu->addAction(
                QStringLiteral("Online ") + QChar(0x00B7) +
                " browsable at " + webUrl);
            browse->setToolTip(webUrl);
            connect(browse, &QAction::triggered, this,
                    [webUrl] { QDesktopServices::openUrl(QUrl(webUrl)); });
        } else {
            QAction *status = m_mirrorMenu->addAction(
                repo.publishToNetwork ? "Publishing to ForkMesh..." : "Local only");
            status->setEnabled(false);
        }
        m_mirrorMenu->addSection("MIRROR LOCATION");
        QAction *location = m_mirrorMenu->addAction(
            hasMirror ? repo.mirrorPath : "Mirror has not been created yet");
        location->setEnabled(false);
        QAction *sync = m_mirrorMenu->addAction(hasMirror ? "Sync mirror now"
                                                           : "Create mirror now");
        connect(sync, &QAction::triggered, this, [this] {
            if (m_repoDetailIndex >= 0)
                syncRepository(m_repoDetailIndex);
        });
        if (hasMirror) {
            QAction *copy = m_mirrorMenu->addAction("Copy mirror location");
            connect(copy, &QAction::triggered, this, [this] {
                if (m_repoDetailIndex < 0 ||
                    m_repoDetailIndex >= m_repositories.size())
                    return;
                QApplication::clipboard()->setText(
                    m_repositories.at(m_repoDetailIndex).mirrorPath);
                setRepoDetailNotice("Copied mirror location.");
            });
        }
        m_mirrorMenu->addSeparator();
        QAction *remove = m_mirrorMenu->addAction("Delete mirror...");
        connect(remove, &QAction::triggered, this, &MainWindow::deleteCurrentMirror);
    }

    const bool hasWorktree = !repo.localPath.isEmpty() && QDir(repo.localPath).exists();

    // Source holds distribution and Git-remote details.
    m_sourceMenu->addSection("LOCAL REMOTE");
    QAction *remote = m_sourceMenu->addAction(
        hasMirror ? repo.mirrorPath : "Sync first to create a local remote");
    remote->setEnabled(false);
    if (hasMirror) {
        QAction *copyRemote = m_sourceMenu->addAction("Copy local remote");
        connect(copyRemote, &QAction::triggered, this, [this] {
            if (m_repoDetailIndex < 0 ||
                m_repoDetailIndex >= m_repositories.size())
                return;
            const QString path = m_repositories.at(m_repoDetailIndex).mirrorPath;
            QApplication::clipboard()->setText(path);
            setRepoDetailNotice("Copied local remote path.");
            logSystem("Copied local remote path to clipboard: " + path);
        });
        QAction *copyCommand = m_sourceMenu->addAction(
            "Copy git remote add command");
        connect(copyCommand, &QAction::triggered, this, [this] {
            if (m_repoDetailIndex < 0 ||
                m_repoDetailIndex >= m_repositories.size())
                return;
            const QString path = m_repositories.at(m_repoDetailIndex).mirrorPath;
            QApplication::clipboard()->setText(
                QStringLiteral("git remote add forkmesh \"") + path + "\"");
            setRepoDetailNotice("Copied git remote add command.");
        });
    }
    m_sourceMenu->addSeparator();
    QAction *zip = m_sourceMenu->addAction("Download ZIP");
    zip->setEnabled(hasMirror || hasWorktree);
    connect(zip, &QAction::triggered, this, &MainWindow::downloadCurrentRepoZip);
}

void MainWindow::deleteCurrentMirror()
{
    // The repo Settings tab deletes the open repository and stays in the repo
    // view afterwards, landing on whatever repository takes the deleted one's
    // place in the list.
    deleteRepositoryAt(m_repoDetailIndex, true);
}

// Delete `index` from this machine: mirror on disk, catalog entry, sealed
// archives and the ForkMesh record itself. `reopenRepoDetail` is for callers
// that were already inside the repo view; the Repos list passes false so
// deleting a row doesn't yank the user into a repository page.
void MainWindow::deleteRepositoryAt(int index, bool reopenRepoDetail)
{
    if (index < 0 || index >= m_repositories.size())
        return;
    const RepositoryRecord repo = m_repositories.at(index);
    const QString rawPath = repo.mirrorPath.trimmed();
    const QString rawWorktree = repo.localPath.trimmed();
    const QString path = rawPath.isEmpty() ? QString() : QDir::cleanPath(rawPath);
    const QString worktree = rawWorktree.isEmpty()
                                 ? QString()
                                 : QDir::cleanPath(rawWorktree);

    QString message = QStringLiteral(
        "Delete repository %1/%2 from this machine?\n\nMirror: %3")
                          .arg(repo.owner, repo.name,
                               path.isEmpty() ? QStringLiteral("not created") : path);
    if (!repo.localPath.isEmpty())
        message += QStringLiteral("\n\nYour working directory will be kept on disk "
                                  "but no longer tracked by ForkMesh:\n%1")
                       .arg(repo.localPath);
    if (!repo.previewOnly && repo.publishToNetwork)
        message += QStringLiteral("\n\nThe repository will also be removed from "
                                  "the public ForkMesh catalog.");
    message += QStringLiteral(
        "\n\nThis frees up the name so you can create a new repository "
        "called \"%1\" again.").arg(repo.name);

    QMessageBox box(QMessageBox::Warning, "Delete repository", message,
                     QMessageBox::Yes | QMessageBox::Cancel, this);
    box.setDefaultButton(QMessageBox::Cancel);
    QCheckBox *keepFilesCheck = nullptr;
    if (!path.isEmpty()) {
        // Opt-in, off by default: the plain delete matches the historical
        // behavior of also removing the bare mirror from disk. Ticking this
        // just untracks the repository from ForkMesh and leaves every file
        // (mirror included) in place.
        keepFilesCheck = new QCheckBox(
            QStringLiteral("Keep the repository files on disk (only remove from ForkMesh)"));
        keepFilesCheck->setChecked(false);
        box.setCheckBox(keepFilesCheck); // QMessageBox takes ownership
    }
    if (box.exec() != QMessageBox::Yes)
        return;
    const bool keepFiles = keepFilesCheck && keepFilesCheck->isChecked();
    // exec() above pumped the event loop, so a sync that landed while the
    // confirmation was up may have shifted m_repositories. Re-find the record
    // by owner/name rather than deleting whatever now sits at the old index.
    index = findNetworkRepoIndex(repo.owner, repo.name, true);
    if (index < 0)
        return;

    if (!keepFiles && !path.isEmpty() && path == worktree) {
        QMessageBox::warning(
            this, "Delete repository",
            "The mirror path matches the working directory, so nothing was deleted.");
        return;
    }

    stopRepoHosts();
    if (!keepFiles && !path.isEmpty() && QDir(path).exists() &&
        !QDir(path).removeRecursively()) {
        startRepoHosts();
        QMessageBox::warning(this, "Delete repository",
                             "Could not delete the mirror at:\n" + path);
        return;
    }
    if (!repo.previewOnly && repo.publishToNetwork)
        deleteCatalogRepository(catalogOwner(repo), repo.name);
    if (PublicMirrorRuntime::isArchiveId(repo.publicArchiveId)) {
        m_publicMirrorMaterializations.remove(repo.publicArchiveId);
        if (!keepFiles) {
            QFile::remove(
                PublicMirrorRuntime::ciphertextPath(
                    publicArchiveRoot(), repo.publicArchiveId));
            QFile::remove(
                QDir(publicArchiveRoot())
                    .filePath(repo.publicArchiveId +
                              QStringLiteral(".json")));
        }
    }
    if (PrivateMirrorStore::isOpaqueId(repo.privateReplicaId)) {
        m_privateMirrorMaterializations.remove(repo.privateReplicaId);
        if (!keepFiles) {
            QFile::remove(
                QDir(privateReplicaRoot())
                    .filePath(repo.privateReplicaId +
                              QStringLiteral(".fm-private")));
        }
    }
    // Deleting a repository must remove it completely from this node so its
    // name is free to reuse. The working directory on disk is left alone
    // (only the bare mirror above is removed, unless "keep files" was
    // checked), but the app no longer keeps a record of it — leaving a stale
    // local-only record behind used to block creating a new repository with
    // the same owner/name indefinitely.
    m_repositories.removeAt(index);
    saveRepositories();
    QString gatewayError;
    if (!rebuildDirectMirrorGatewayConfiguration(
            &gatewayError, true)) {
        logSystem(
            QStringLiteral(
                "Direct gateway refresh after repository deletion failed: %1")
                .arg(gatewayError));
    }
    startRepoHosts();
    refreshRepositoryList();
    // Removing a record shifts every later index down by one: keep the open
    // repository pointing at the same record, and only clear the selection when
    // the repository that just went is the one on screen.
    if (m_repoDetailIndex == index)
        m_repoDetailIndex = -1;
    else if (m_repoDetailIndex > index)
        --m_repoDetailIndex;
    if (reopenRepoDetail) {
        if (!m_repositories.isEmpty())
            openRepoDetail(qMin(index, m_repositories.size() - 1));
        else if (m_repoDetailStack)
            m_repoDetailStack->setCurrentIndex(0); // Code (empty)
    }
    logSystem("Deleted repository " + repo.owner + "/" + repo.name + ".");
    setRepoDetailNotice(keepFiles ? "Removed repository from ForkMesh. Files kept on disk."
                                   : "Deleted repository.");
}

// Per-repo Settings tab: flip visibility (public/private) and delete the repo.
QWidget *MainWindow::buildRepoSettingsTab()
{
    auto *page = new QWidget;
    auto *outer = new QVBoxLayout(page);
    outer->setContentsMargins(24, 22, 24, 22);
    outer->setSpacing(14);

    auto *heading = new QLabel("Repository settings");
    heading->setObjectName("channelTitle");
    outer->addWidget(heading);

    // --- About --------------------------------------------------------------
    auto *aboutHeading = new QLabel("About");
    aboutHeading->setObjectName("sectionLabel");
    outer->addWidget(aboutHeading);

    auto *aboutHint = new QLabel(
        "The description, website link and topics shown on this repository's "
        "overview (and on its public ForkMesh page).");
    aboutHint->setObjectName("statusLine");
    aboutHint->setWordWrap(true);
    outer->addWidget(aboutHint);

    auto *aboutEditBtn = new QPushButton("Edit description & website\xE2\x80\xA6");
    aboutEditBtn->setProperty("buttonSize", "sm");
    aboutEditBtn->setCursor(Qt::PointingHandCursor);
    connect(aboutEditBtn, &QPushButton::clicked, this, &MainWindow::editRepoAbout);
    auto *aboutRow = new QHBoxLayout;
    aboutRow->setContentsMargins(0, 0, 0, 0);
    aboutRow->addWidget(aboutEditBtn);
    aboutRow->addStretch();
    outer->addLayout(aboutRow);

    outer->addSpacing(10);

    // --- Visibility -------------------------------------------------------
    auto *visHeading = new QLabel("Visibility");
    visHeading->setObjectName("sectionLabel");
    outer->addWidget(visHeading);

    m_repoPrivateCheck = new QCheckBox("Private repository");
    m_repoPrivateCheck->setCursor(Qt::PointingHandCursor);
    m_repoPrivateCheck->setToolTip(
        "Hide this repo from the public catalog and require your node's key to "
        "browse or clone it through the mainnode. Only you can read it.");
    connect(m_repoPrivateCheck, &QCheckBox::toggled, this, [this](bool on) {
        if (m_repoDetailIndex < 0 || m_repoDetailIndex >= m_repositories.size())
            return;
        if (m_repositories[m_repoDetailIndex].isPrivate == on)
            return;
        m_repositories[m_repoDetailIndex].isPrivate = on;
        saveRepositories();
        // Remove the old visibility from the direct gateway immediately. The
        // current temporary materialization stays alive only long enough to
        // seal the replacement format; it is never promoted to durable
        // plaintext.
        QString gatewayError;
        rebuildDirectMirrorGatewayConfiguration(
            &gatewayError, true);
        logSystem(QStringLiteral("%1 %2/%3.")
                      .arg(on ? "Made private" : "Made public",
                           m_repositories.at(m_repoDetailIndex).owner,
                           m_repositories.at(m_repoDetailIndex).name));
        // A private transition must seal/migrate before any private catalog
        // record exists. Public transition rebuilds an official-age archive.
        if (on)
            syncPrivateRepository(m_repoDetailIndex, /*quiet=*/false);
        else
            syncRepository(m_repoDetailIndex, /*quiet=*/false);
        startRepoHosts();
        refreshRepositoryList();
        refreshRepoSettings();
    });
    outer->addWidget(m_repoPrivateCheck);

    m_repoVisibilityHint = new QLabel;
    m_repoVisibilityHint->setObjectName("statusLine");
    m_repoVisibilityHint->setWordWrap(true);
    outer->addWidget(m_repoVisibilityHint);

    outer->addSpacing(10);

    // --- Collaborators (private repos, issue #9) --------------------------
    // Share a private repo with other accounts: they see it in their catalog
    // once logged in and clone it with their own key. Only shown for a private
    // repo this node owns and has published (refreshRepoCollaborators toggles
    // visibility and loads the current list from the relay).
    m_collabSection = new QWidget;
    auto *collabLayout = new QVBoxLayout(m_collabSection);
    collabLayout->setContentsMargins(0, 0, 0, 0);
    collabLayout->setSpacing(8);

    auto *collabHeading = new QLabel("Collaborators");
    collabHeading->setObjectName("sectionLabel");
    collabLayout->addWidget(collabHeading);

    auto *collabHint = new QLabel(
        "Accounts you share this private repository with. They can see it in "
        "their catalog (once signed in) and clone it with their own key. The "
        "repo stays hidden from the public website.");
    collabHint->setObjectName("statusLine");
    collabHint->setWordWrap(true);
    collabLayout->addWidget(collabHint);

    m_collabList = new QListWidget;
    m_collabList->setObjectName("collabList");
    m_collabList->setMaximumHeight(140);
    collabLayout->addWidget(m_collabList);

    m_collabEmptyHint = new QLabel("No collaborators yet.");
    m_collabEmptyHint->setObjectName("statusLine");
    collabLayout->addWidget(m_collabEmptyHint);

    auto *collabRow = new QHBoxLayout;
    m_collabEdit = new QLineEdit;
    m_collabEdit->setPlaceholderText("account name to add");
    collabRow->addWidget(m_collabEdit, 1);
    auto *collabAddBtn = new QPushButton("Add");
    collabAddBtn->setProperty("buttonSize", "sm");
    collabAddBtn->setCursor(Qt::PointingHandCursor);
    collabRow->addWidget(collabAddBtn);
    auto *collabRemoveBtn = new QPushButton("Remove selected");
    collabRemoveBtn->setProperty("buttonSize", "sm");
    collabRemoveBtn->setCursor(Qt::PointingHandCursor);
    collabRow->addWidget(collabRemoveBtn);
    collabLayout->addLayout(collabRow);

    auto addCollab = [this] {
        if (m_collabEdit)
            addRepoCollaborator(m_collabEdit->text());
    };
    connect(collabAddBtn, &QPushButton::clicked, this, addCollab);
    connect(m_collabEdit, &QLineEdit::returnPressed, this, addCollab);
    connect(collabRemoveBtn, &QPushButton::clicked, this, [this] {
        if (m_collabList && m_collabList->currentItem())
            removeRepoCollaborator(m_collabList->currentItem()->text());
    });

    outer->addWidget(m_collabSection);
    outer->addSpacing(10);

    // --- Source -----------------------------------------------------------
    auto *sourceHeading = new QLabel("Source");
    sourceHeading->setObjectName("sectionLabel");
    outer->addWidget(sourceHeading);

    m_repoSourceEdit = new QLineEdit;
    m_repoSourceEdit->setPlaceholderText(
        "https://forkmesh.com/<node>/<owner>/<name>");
    m_repoSourceEdit->setToolTip(
        "The upstream clone URL this mirror was forked from. Edit it to repoint "
        "the mirror at a live node when the original location goes stale.");
    auto *sourceUpdateBtn = new QPushButton("Update");
    sourceUpdateBtn->setProperty("buttonSize", "sm");
    sourceUpdateBtn->setCursor(Qt::PointingHandCursor);
    connect(sourceUpdateBtn, &QPushButton::clicked, this,
            &MainWindow::updateRepoSource);
    connect(m_repoSourceEdit, &QLineEdit::returnPressed, this,
            &MainWindow::updateRepoSource);
    auto *sourceRow = new QHBoxLayout;
    sourceRow->addWidget(m_repoSourceEdit, 1);
    sourceRow->addWidget(sourceUpdateBtn);
    outer->addLayout(sourceRow);

    m_repoSourceHint = new QLabel;
    m_repoSourceHint->setObjectName("statusLine");
    m_repoSourceHint->setWordWrap(true);
    outer->addWidget(m_repoSourceHint);

    m_repoForkLocation = new QLabel;
    m_repoForkLocation->setObjectName("statusLine");
    m_repoForkLocation->setWordWrap(true);
    m_repoForkLocation->setTextInteractionFlags(Qt::TextSelectableByMouse);
    auto *forkLocationButton = new QPushButton("Set");
    forkLocationButton->setProperty("buttonSize", "sm");
    forkLocationButton->setCursor(Qt::PointingHandCursor);
    forkLocationButton->setToolTip(
        "Choose an existing local Git working copy to use as this repo's fork "
        "location.");
    connect(forkLocationButton, &QPushButton::clicked, this,
            &MainWindow::promptSetRepoForkLocation);
    auto *forkLocationRow = new QHBoxLayout;
    forkLocationRow->setContentsMargins(0, 0, 0, 0);
    forkLocationRow->addWidget(m_repoForkLocation, 1);
    forkLocationRow->addWidget(forkLocationButton);
    outer->addLayout(forkLocationRow);

    m_repoMirrorLocation = new QLabel;
    m_repoMirrorLocation->setObjectName("statusLine");
    m_repoMirrorLocation->setWordWrap(true);
    m_repoMirrorLocation->setTextInteractionFlags(Qt::TextSelectableByMouse);
    outer->addWidget(m_repoMirrorLocation);

    auto *remotesHeading = new QLabel("Git remotes");
    remotesHeading->setObjectName("sectionLabel");
    outer->addWidget(remotesHeading);

    m_repoRemotesTable = new QTableWidget(0, 3);
    installColumnHeaderMenu(m_repoRemotesTable);
    m_repoRemotesTable->setHorizontalHeaderLabels({"Name", "Fetch URL", "Push URL"});
    m_repoRemotesTable->horizontalHeader()->setStretchLastSection(true);
    m_repoRemotesTable->verticalHeader()->setVisible(false);
    m_repoRemotesTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_repoRemotesTable->setSelectionMode(QAbstractItemView::SingleSelection);
    m_repoRemotesTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_repoRemotesTable->setMaximumHeight(150);
    makeColumnsResizable(m_repoRemotesTable);
    outer->addWidget(m_repoRemotesTable);

    auto *remoteAddButton = new QPushButton("Add");
    auto *remoteEditButton = new QPushButton("Edit");
    auto *remoteDeleteButton = new QPushButton("Delete");
    for (QPushButton *b : {remoteAddButton, remoteEditButton, remoteDeleteButton}) {
        b->setProperty("buttonSize", "sm");
        b->setCursor(Qt::PointingHandCursor);
    }
    connect(remoteAddButton, &QPushButton::clicked, this,
            &MainWindow::promptAddRepoRemote);
    connect(remoteEditButton, &QPushButton::clicked, this,
            &MainWindow::promptEditRepoRemote);
    connect(remoteDeleteButton, &QPushButton::clicked, this,
            &MainWindow::deleteSelectedRepoRemote);
    connect(m_repoRemotesTable, &QTableWidget::cellDoubleClicked, this,
            [this](int, int) { promptEditRepoRemote(); });
    auto *remoteButtonRow = new QHBoxLayout;
    remoteButtonRow->setContentsMargins(0, 0, 0, 0);
    remoteButtonRow->addWidget(remoteAddButton);
    remoteButtonRow->addWidget(remoteEditButton);
    remoteButtonRow->addWidget(remoteDeleteButton);
    remoteButtonRow->addStretch();
    outer->addLayout(remoteButtonRow);

    auto *gitIdentityBtn = new QPushButton("Use ForkMesh git identity");
    gitIdentityBtn->setProperty("buttonSize", "sm");
    gitIdentityBtn->setCursor(Qt::PointingHandCursor);
    gitIdentityBtn->setToolTip(
        "Set this repository's local git user.name and user.email from your "
        "ForkMesh username.");
    connect(gitIdentityBtn, &QPushButton::clicked, this,
            &MainWindow::setRepoGitIdentityFromForkMesh);
    auto *gitIdentityRow = new QHBoxLayout;
    gitIdentityRow->setContentsMargins(0, 0, 0, 0);
    gitIdentityRow->addWidget(gitIdentityBtn);
    gitIdentityRow->addStretch();
    outer->addLayout(gitIdentityRow);

    outer->addSpacing(10);

    // --- Actions ----------------------------------------------------------
    auto *actionsHeading = new QLabel("Actions");
    actionsHeading->setObjectName("sectionLabel");
    outer->addWidget(actionsHeading);

    m_settingsActionsCheck = new QCheckBox("Run actions on push");
    m_settingsActionsCheck->setCursor(Qt::PointingHandCursor);
    m_settingsActionsCheck->setToolTip(
        "When a fork pushes to this repo's local mirror, run its .forkmesh/ "
        "workflows. Off by default for mirrored repos. Changed workflows still "
        "require your approval before they run.");
    connect(m_settingsActionsCheck, &QCheckBox::toggled, this,
            [this](bool on) { setRepoActionsEnabled(on); });
    outer->addWidget(m_settingsActionsCheck);

    auto *actionsHint = new QLabel(
        "Mirrored repositories start with actions disabled. Enable this only for "
        "repos whose workflows you trust to run on this machine.");
    actionsHint->setObjectName("statusLine");
    actionsHint->setWordWrap(true);
    outer->addWidget(actionsHint);

    outer->addSpacing(10);

    // --- Secret scanning --------------------------------------------------
    auto *secretHeading = new QLabel("Secret scanning");
    secretHeading->setObjectName("sectionLabel");
    outer->addWidget(secretHeading);

    m_secretScanCheck = new QCheckBox("Block push if secrets are detected");
    m_secretScanCheck->setCursor(Qt::PointingHandCursor);
    m_secretScanCheck->setToolTip(
        "Before each push ForkMesh scans the commits being pushed for API keys, "
        "private keys, and other high-confidence secrets. If any are found you "
        "will be warned and can cancel or push anyway.");
    connect(m_secretScanCheck, &QCheckBox::toggled, this,
            [this](bool on) { setRepoSecretScanningEnabled(on); });
    outer->addWidget(m_secretScanCheck);

    auto *secretHint = new QLabel(
        "Detects GitHub tokens, AWS access keys, Slack tokens, and PEM private "
        "keys. Rotate any exposed credentials immediately.");
    secretHint->setObjectName("statusLine");
    secretHint->setWordWrap(true);
    outer->addWidget(secretHint);

    outer->addSpacing(10);

    // --- Coves (encrypted vaults) -----------------------------------------
    outer->addWidget(buildCoveSection());

    outer->addSpacing(10);

    // --- Danger zone ------------------------------------------------------
    auto *dangerHeading = new QLabel("Danger zone");
    dangerHeading->setObjectName("sectionLabel");
    outer->addWidget(dangerHeading);

    auto *deleteHint = new QLabel(
        "Delete this repository from this machine completely. Your working "
        "directory, if any, is kept on disk but no longer tracked by "
        "ForkMesh. Published repos are also removed from the public "
        "ForkMesh catalog, freeing up the name for reuse. You can also "
        "choose to keep the mirror files on disk and just untrack the "
        "repository from ForkMesh.");
    deleteHint->setObjectName("statusLine");
    deleteHint->setWordWrap(true);
    outer->addWidget(deleteHint);

    auto *deleteBtn = new QPushButton("Delete repository");
    deleteBtn->setObjectName("dangerButton");
    deleteBtn->setProperty("buttonSize", "sm");
    deleteBtn->setCursor(Qt::PointingHandCursor);
    setOcticon(deleteBtn, "trash", 16);
    connect(deleteBtn, &QPushButton::clicked, this, &MainWindow::deleteCurrentMirror);
    auto *deleteRow = new QHBoxLayout;
    deleteRow->addWidget(deleteBtn);
    deleteRow->addStretch();
    outer->addLayout(deleteRow);

    outer->addStretch();
    return page;
}

void MainWindow::setRepoActionsEnabled(bool on)
{
    if (m_repoDetailIndex < 0 || m_repoDetailIndex >= m_repositories.size())
        return;
    if (m_repositories[m_repoDetailIndex].actionsEnabled == on)
        return;
    m_repositories[m_repoDetailIndex].actionsEnabled = on;
    saveRepositories();
    // The hook stays installed regardless (it powers the live Code refresh);
    // just make sure it exists when enabling.
    ensurePushHook(m_repositories.at(m_repoDetailIndex));
    logSystem(QStringLiteral("Actions %1 for %2/%3.")
                  .arg(on ? "enabled" : "disabled",
                       m_repositories.at(m_repoDetailIndex).owner,
                       m_repositories.at(m_repoDetailIndex).name));
    // Keep both toggles (Actions tab + Settings tab) in sync.
    if (m_actionsEnabledCheck) {
        QSignalBlocker block(m_actionsEnabledCheck);
        m_actionsEnabledCheck->setChecked(on);
    }
    if (m_settingsActionsCheck) {
        QSignalBlocker block(m_settingsActionsCheck);
        m_settingsActionsCheck->setChecked(on);
    }
}

void MainWindow::setRepoSecretScanningEnabled(bool on)
{
    if (m_repoDetailIndex < 0 || m_repoDetailIndex >= m_repositories.size())
        return;
    if (m_repositories[m_repoDetailIndex].secretScanningEnabled == on)
        return;
    m_repositories[m_repoDetailIndex].secretScanningEnabled = on;
    saveRepositories();
    logSystem(QStringLiteral("Secret scanning push protection %1 for %2/%3.")
                  .arg(on ? "enabled" : "disabled",
                       m_repositories.at(m_repoDetailIndex).owner,
                       m_repositories.at(m_repoDetailIndex).name));
    if (m_secretScanCheck) {
        QSignalBlocker block(m_secretScanCheck);
        m_secretScanCheck->setChecked(on);
    }
}

bool MainWindow::isWorkflowDisabled(const QString &path) const
{
    if (m_repoDetailIndex < 0 || m_repoDetailIndex >= m_repositories.size())
        return false;
    return m_repositories.at(m_repoDetailIndex).disabledWorkflows.contains(path);
}

void MainWindow::promptSetRepoForkLocation()
{
    if (m_repoDetailIndex < 0 || m_repoDetailIndex >= m_repositories.size())
        return;

    const RepositoryRecord current = m_repositories.at(m_repoDetailIndex);
    const QString startDir = current.localPath.trimmed().isEmpty()
                                 ? QDir::homePath()
                                 : current.localPath.trimmed();
    const QString path = QFileDialog::getExistingDirectory(
        this, "Choose existing fork location", startDir);
    if (path.isEmpty())
        return;

    const bool looksLikeGit =
        QDir(path).exists(QStringLiteral(".git")) ||
        QDir(path).exists(QStringLiteral("HEAD"));
    if (!looksLikeGit) {
        QMessageBox::warning(
            this, "Fork location",
            "That folder is not a Git working copy. Choose a folder created by "
            "\"git clone\" or \"git init\".");
        return;
    }

    const QString cleanPath = QDir::cleanPath(path);
    const QString mirrorPath =
        QDir::cleanPath(m_repositories.at(m_repoDetailIndex).mirrorPath.trimmed());
    if (!mirrorPath.isEmpty() && cleanPath == mirrorPath) {
        QMessageBox::warning(
            this, "Fork location",
            "The fork location cannot be the same folder as the bare mirror.");
        return;
    }

    RepositoryRecord &repo = m_repositories[m_repoDetailIndex];
    if (QDir::cleanPath(repo.localPath.trimmed()) == cleanPath) {
        refreshRepoSettings();
        return;
    }
    repo.localPath = cleanPath;
    saveRepositories();
    ensurePushHook(repo);
    refreshRepositoryList();
    refreshRepoSettings();
    refreshSourceControl();
    logSystem(QStringLiteral("Fork location for %1/%2 set to %3.")
                  .arg(repo.owner, repo.name, cleanPath));
    syncRepository(m_repoDetailIndex);
}

void MainWindow::setRepoGitIdentityFromForkMesh()
{
    if (m_repoDetailIndex < 0 || m_repoDetailIndex >= m_repositories.size())
        return;
    const RepositoryRecord &repo = m_repositories.at(m_repoDetailIndex);
    const QString dir = repo.localPath.trimmed();
    if (dir.isEmpty() || !QDir(dir).exists(QStringLiteral(".git"))) {
        QMessageBox::warning(
            this, "Git identity",
            "Set a local fork location first. Git identity is stored in the "
            "working copy's local config.");
        return;
    }

    const QString user = settingsAccountName().trimmed();
    if (user.isEmpty()) {
        QMessageBox::warning(
            this, "Git identity",
            "Join or log in to ForkMesh before setting a repository git identity.");
        return;
    }
    const QString email = user.toLower() + QStringLiteral("@users.forkmesh.local");

    QString error;
    const bool okName =
        runGitCapture(dir,
                      {QStringLiteral("config"), QStringLiteral("--local"),
                       QStringLiteral("user.name"), user},
                      nullptr, &error);
    const bool okEmail =
        runGitCapture(dir,
                      {QStringLiteral("config"), QStringLiteral("--local"),
                       QStringLiteral("user.email"), email},
                      nullptr, &error);
    if (!okName || !okEmail) {
        QMessageBox::warning(
            this, "Git identity",
            QStringLiteral("Could not update git config: %1")
                .arg(error.trimmed().right(240)));
        return;
    }

    updateFooterGitIdentity();
    logSystem(QStringLiteral("Git identity for %1/%2 set to %3 <%4>.")
                  .arg(repo.owner, repo.name, user, email));
    flashMessage(QStringLiteral("Git identity set to %1 <%2>.").arg(user, email));
}

void MainWindow::setWorkflowDisabled(const QString &path, bool disabled)
{
    if (path.isEmpty() || m_repoDetailIndex < 0 ||
        m_repoDetailIndex >= m_repositories.size())
        return;
    QStringList &off = m_repositories[m_repoDetailIndex].disabledWorkflows;
    if (disabled == off.contains(path))
        return; // already in the desired state
    if (disabled)
        off.append(path);
    else
        off.removeAll(path);
    saveRepositories();
    logSystem(QStringLiteral("Actions: workflow %1 %2 for %3/%4.")
                  .arg(path, disabled ? QStringLiteral("disabled")
                                      : QStringLiteral("enabled"),
                       m_repositories.at(m_repoDetailIndex).owner,
                       m_repositories.at(m_repoDetailIndex).name));
    // A disabled workflow can't be triggered by hand either.
    updateManualRunBar();
}

void MainWindow::refreshRepoSettings()
{
    // Reload the cove list up front so it stays in sync even when the visibility
    // hints below take one of this function's early returns.
    rebuildRepoCovesList();
    const bool haveRepo =
        m_repoDetailIndex >= 0 && m_repoDetailIndex < m_repositories.size();
    if (m_repoPrivateCheck) {
        QSignalBlocker block(m_repoPrivateCheck);
        m_repoPrivateCheck->setEnabled(haveRepo);
        m_repoPrivateCheck->setChecked(
            haveRepo && m_repositories.at(m_repoDetailIndex).isPrivate);
    }
    if (m_settingsActionsCheck) {
        QSignalBlocker block(m_settingsActionsCheck);
        m_settingsActionsCheck->setEnabled(haveRepo);
        m_settingsActionsCheck->setChecked(
            haveRepo && m_repositories.at(m_repoDetailIndex).actionsEnabled);
    }
    if (m_secretScanCheck) {
        QSignalBlocker block(m_secretScanCheck);
        m_secretScanCheck->setEnabled(haveRepo);
        m_secretScanCheck->setChecked(
            !haveRepo || m_repositories.at(m_repoDetailIndex).secretScanningEnabled);
    }
    // Show/hide + reload the collaborator list for the open repo (issue #9).
    refreshRepoCollaborators();
    if (m_repoSourceEdit) {
        QSignalBlocker block(m_repoSourceEdit);
        m_repoSourceEdit->setEnabled(haveRepo);
        m_repoSourceEdit->setText(
            haveRepo ? m_repositories.at(m_repoDetailIndex).cloneUrl.trimmed()
                     : QString());
    }
    if (m_repoSourceHint) {
        if (!haveRepo) {
            m_repoSourceHint->clear();
        } else {
            const RepositoryRecord &r = m_repositories.at(m_repoDetailIndex);
            if (!r.localPath.trimmed().isEmpty())
                m_repoSourceHint->setText(
                    "This repo is backed by a local working copy at " +
                    r.localPath.trimmed() +
                    "; syncs read from there. The clone URL above is the "
                    "published/fork location.");
            else if (r.cloneUrl.trimmed().isEmpty())
                m_repoSourceHint->setText(
                    "No upstream set — this machine hosts the repo directly.");
            else
                m_repoSourceHint->setText(
                    "The mirror fetches from this URL. Update it to repoint the "
                    "fork at a different node, then sync to pull from it.");
        }
    }
    if (m_repoForkLocation) {
        if (!haveRepo) {
            m_repoForkLocation->clear();
        } else {
            const QString path =
                m_repositories.at(m_repoDetailIndex).localPath.trimmed();
            m_repoForkLocation->setText(
                QStringLiteral("Fork location: %1")
                    .arg(path.isEmpty()
                             ? QStringLiteral("No local working copy")
                             : QDir::toNativeSeparators(path)));
        }
    }
    if (m_repoMirrorLocation) {
        if (!haveRepo) {
            m_repoMirrorLocation->clear();
        } else {
            const QString path =
                m_repositories.at(m_repoDetailIndex).mirrorPath.trimmed();
            m_repoMirrorLocation->setText(
                QStringLiteral("Mirror location: %1")
                    .arg(path.isEmpty()
                             ? QStringLiteral("Mirror has not been created yet")
                             : QDir::toNativeSeparators(path)));
        }
    }
    reloadRepoRemotesTable();
    if (!m_repoVisibilityHint)
        return;
    if (!haveRepo) {
        m_repoVisibilityHint->clear();
        return;
    }
    const RepositoryRecord &repo = m_repositories.at(m_repoDetailIndex);
    if (!repo.publishToNetwork)
        m_repoVisibilityHint->setText(
            "This repository is local only — it isn't published to the network "
            "yet. The visibility choice applies once you publish it.");
    else if (repo.isPrivate)
        m_repoVisibilityHint->setText(
            "Private: hidden from the public catalog. Only this machine's key "
            "can browse or clone it through the mainnode.");
    else
        m_repoVisibilityHint->setText(
            "Public: listed in the catalog and anyone can browse or clone it "
            "through the mainnode.");
}

void MainWindow::updateRepoSource()
{
    if (!m_repoSourceEdit)
        return;
    if (m_repoDetailIndex < 0 || m_repoDetailIndex >= m_repositories.size())
        return;
    const QString newUrl = m_repoSourceEdit->text().trimmed();
    RepositoryRecord &repo = m_repositories[m_repoDetailIndex];
    if (newUrl == repo.cloneUrl.trimmed()) {
        refreshRepoSettings();
        return;
    }
    repo.cloneUrl = newUrl;
    saveRepositories();
    // Repoint the bare mirror's origin so the next sync fetches from the new
    // location. A repo backed by a local working copy fetches from that path
    // instead (see repositorySource()), so leave its remote alone.
    if (repo.localPath.trimmed().isEmpty() && !newUrl.isEmpty() &&
        !repo.mirrorPath.isEmpty() && QDir(repo.mirrorPath).exists())
        runGitCapture(repo.mirrorPath,
                      {QStringLiteral("remote"), QStringLiteral("set-url"),
                       QStringLiteral("origin"), newUrl},
                      nullptr, nullptr);
    logSystem(QStringLiteral("Source for %1/%2 set to %3.")
                  .arg(repo.owner, repo.name,
                       newUrl.isEmpty() ? QStringLiteral("(none)") : newUrl));
    refreshRepoSettings();
}

void MainWindow::reloadRepoRemotesTable()
{
    if (!m_repoRemotesTable)
        return;
    const bool haveRepo =
        m_repoDetailIndex >= 0 && m_repoDetailIndex < m_repositories.size();
    const QString gitDir = haveRepo ? repoRemoteGitDir(m_repositories.at(m_repoDetailIndex))
                                    : QString();
    const QList<RepoRemoteRow> remotes = readRepoRemotes(gitDir);
    QSignalBlocker block(m_repoRemotesTable);
    TableRepaintGuard repaintGuard(m_repoRemotesTable);
    m_repoRemotesTable->setEnabled(!gitDir.isEmpty());
    m_repoRemotesTable->setRowCount(0);
    for (const RepoRemoteRow &remote : remotes) {
        const int row = m_repoRemotesTable->rowCount();
        m_repoRemotesTable->insertRow(row);
        m_repoRemotesTable->setItem(row, 0, new QTableWidgetItem(remote.name));
        m_repoRemotesTable->setItem(row, 1, new QTableWidgetItem(remote.fetchUrl));
        m_repoRemotesTable->setItem(row, 2, new QTableWidgetItem(remote.pushUrl));
    }
}

void MainWindow::promptAddRepoRemote()
{
    if (m_repoDetailIndex < 0 || m_repoDetailIndex >= m_repositories.size())
        return;
    const QString gitDir = repoRemoteGitDir(m_repositories.at(m_repoDetailIndex));
    if (gitDir.isEmpty()) {
        QMessageBox::information(
            this, "Git remotes",
            "Create a local fork or mirror before editing git remotes.");
        return;
    }

    QDialog dialog(this);
    dialog.setWindowTitle("Add git remote");
    auto *layout = new QVBoxLayout(&dialog);
    auto *form = new QFormLayout;
    auto *nameEdit = new QLineEdit;
    auto *fetchEdit = new QLineEdit;
    auto *pushEdit = new QLineEdit;
    nameEdit->setPlaceholderText("origin");
    fetchEdit->setPlaceholderText("https://example.com/owner/repo.git");
    pushEdit->setPlaceholderText("leave blank to match fetch URL");
    form->addRow("Name", nameEdit);
    form->addRow("Fetch URL", fetchEdit);
    form->addRow("Push URL", pushEdit);
    layout->addLayout(form);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok |
                                         QDialogButtonBox::Cancel);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if (dialog.exec() != QDialog::Accepted)
        return;

    const QString name = nameEdit->text().trimmed();
    const QString fetchUrl = fetchEdit->text().trimmed();
    const QString pushUrl = pushEdit->text().trimmed();
    if (name.isEmpty() || fetchUrl.isEmpty()) {
        QMessageBox::warning(this, "Git remotes",
                             "Remote name and fetch URL are required.");
        return;
    }

    QString error;
    if (!runGitCapture(gitDir,
                       {QStringLiteral("remote"), QStringLiteral("add"), name,
                        fetchUrl},
                       nullptr, &error)) {
        QMessageBox::warning(this, "Git remotes",
                             "Could not add remote: " +
                                 error.trimmed().right(240));
        return;
    }
    if (!pushUrl.isEmpty() && pushUrl != fetchUrl)
        runGitCapture(gitDir,
                      {QStringLiteral("remote"), QStringLiteral("set-url"),
                       QStringLiteral("--push"), name, pushUrl},
                      nullptr, nullptr);
    reloadRepoRemotesTable();
    logSystem(QStringLiteral("Added git remote %1 for %2/%3.")
                  .arg(name, m_repositories.at(m_repoDetailIndex).owner,
                       m_repositories.at(m_repoDetailIndex).name));
}

void MainWindow::promptEditRepoRemote()
{
    if (!m_repoRemotesTable || m_repoDetailIndex < 0 ||
        m_repoDetailIndex >= m_repositories.size())
        return;
    const int row = m_repoRemotesTable->currentRow();
    if (row < 0 || !m_repoRemotesTable->item(row, 0)) {
        QMessageBox::information(this, "Git remotes",
                                 "Select a remote to edit.");
        return;
    }
    const QString gitDir = repoRemoteGitDir(m_repositories.at(m_repoDetailIndex));
    if (gitDir.isEmpty())
        return;
    const QString oldName = m_repoRemotesTable->item(row, 0)->text();
    const QString oldFetch = m_repoRemotesTable->item(row, 1)
                                 ? m_repoRemotesTable->item(row, 1)->text()
                                 : QString();
    const QString oldPush = m_repoRemotesTable->item(row, 2)
                                ? m_repoRemotesTable->item(row, 2)->text()
                                : QString();

    QDialog dialog(this);
    dialog.setWindowTitle("Edit git remote");
    auto *layout = new QVBoxLayout(&dialog);
    auto *form = new QFormLayout;
    auto *nameEdit = new QLineEdit(oldName);
    auto *fetchEdit = new QLineEdit(oldFetch);
    auto *pushEdit = new QLineEdit(oldPush);
    pushEdit->setPlaceholderText("leave blank to match fetch URL");
    form->addRow("Name", nameEdit);
    form->addRow("Fetch URL", fetchEdit);
    form->addRow("Push URL", pushEdit);
    layout->addLayout(form);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok |
                                         QDialogButtonBox::Cancel);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if (dialog.exec() != QDialog::Accepted)
        return;

    const QString name = nameEdit->text().trimmed();
    const QString fetchUrl = fetchEdit->text().trimmed();
    const QString pushUrl = pushEdit->text().trimmed();
    if (name.isEmpty() || fetchUrl.isEmpty()) {
        QMessageBox::warning(this, "Git remotes",
                             "Remote name and fetch URL are required.");
        return;
    }

    QString error;
    if (name != oldName &&
        !runGitCapture(gitDir,
                       {QStringLiteral("remote"), QStringLiteral("rename"),
                        oldName, name},
                       nullptr, &error)) {
        QMessageBox::warning(this, "Git remotes",
                             "Could not rename remote: " +
                                 error.trimmed().right(240));
        return;
    }
    if (!runGitCapture(gitDir,
                       {QStringLiteral("remote"), QStringLiteral("set-url"),
                        name, fetchUrl},
                       nullptr, &error)) {
        QMessageBox::warning(this, "Git remotes",
                             "Could not update fetch URL: " +
                                 error.trimmed().right(240));
        return;
    }
    const QString effectivePush = pushUrl.isEmpty() ? fetchUrl : pushUrl;
    if (!runGitCapture(gitDir,
                       {QStringLiteral("remote"), QStringLiteral("set-url"),
                        QStringLiteral("--push"), name, effectivePush},
                       nullptr, &error)) {
        QMessageBox::warning(this, "Git remotes",
                             "Could not update push URL: " +
                                 error.trimmed().right(240));
        return;
    }
    reloadRepoRemotesTable();
    logSystem(QStringLiteral("Updated git remote %1 for %2/%3.")
                  .arg(name, m_repositories.at(m_repoDetailIndex).owner,
                       m_repositories.at(m_repoDetailIndex).name));
}

void MainWindow::deleteSelectedRepoRemote()
{
    if (!m_repoRemotesTable || m_repoDetailIndex < 0 ||
        m_repoDetailIndex >= m_repositories.size())
        return;
    const int row = m_repoRemotesTable->currentRow();
    if (row < 0 || !m_repoRemotesTable->item(row, 0))
        return;
    const QString name = m_repoRemotesTable->item(row, 0)->text();
    if (QMessageBox::question(
            this, "Delete git remote",
            QStringLiteral("Remove remote \"%1\" from this repository?").arg(name),
            QMessageBox::Cancel | QMessageBox::Yes,
            QMessageBox::Cancel) != QMessageBox::Yes)
        return;
    const QString gitDir = repoRemoteGitDir(m_repositories.at(m_repoDetailIndex));
    QString error;
    if (!runGitCapture(gitDir,
                       {QStringLiteral("remote"), QStringLiteral("remove"), name},
                       nullptr, &error)) {
        QMessageBox::warning(this, "Git remotes",
                             "Could not remove remote: " +
                                 error.trimmed().right(240));
        return;
    }
    reloadRepoRemotesTable();
    logSystem(QStringLiteral("Removed git remote %1 for %2/%3.")
                  .arg(name, m_repositories.at(m_repoDetailIndex).owner,
                       m_repositories.at(m_repoDetailIndex).name));
}

void MainWindow::updateRepoDetailStatus()
{
    if (!m_repoDetailStatus)
        return;
    if (m_repoDetailIndex < 0 || m_repoDetailIndex >= m_repositories.size()) {
        m_repoDetailStatus->clear();
        return;
    }
    const RepositoryRecord &repo = m_repositories.at(m_repoDetailIndex);
    const QString repoKey = repo.owner + "/" + repo.name;
    const QPair<int, int> stats = m_repoStats.value(repoKey);
    if (repo.previewOnly) {
        const bool cached =
            !repo.mirrorPath.isEmpty() && QDir(repo.mirrorPath).exists();
        QStringList bits;
        bits << (cached ? QStringLiteral("<b>Preview cached</b>")
                        : QStringLiteral("<b>Preview cache pending</b>"));
        bits << QStringLiteral("<b>Temporary</b>");
        bits << QStringLiteral("<b>%1</b> served").arg(stats.first);
        bits << QStringLiteral("<b>%1</b> clone%2")
                    .arg(stats.second)
                    .arg(stats.second == 1 ? QString()
                                           : QStringLiteral("s"));
        QString details = bits.join(QString::fromUtf8(" \xC2\xB7 "));
        details += QString::fromUtf8("<br><span style='color:#8b949e'>Cache %1 "
                                  "\xC2\xB7 Last refresh %2</span>")
                       .arg(repo.mirrorPath.toHtmlEscaped(),
                            formatRepoDate(repo.lastSyncMs));
        m_repoDetailStatus->setText(details);
        return;
    }
    QStringList bits;
    bits << QStringLiteral("<b>%1</b> served").arg(stats.first);
    bits << QStringLiteral("<b>%1</b> clone%2")
                .arg(stats.second)
                .arg(stats.second == 1 ? QString() : QStringLiteral("s"));

    QString details = bits.join(QString::fromUtf8(" \xC2\xB7 "));
    details += QString::fromUtf8("<br><span style='color:#8b949e'>Hosted since %1 "
                              "\xC2\xB7 Last sync %2</span>")
                   .arg(formatRepoDate(repo.hostedSinceMs),
                        formatRepoDate(repo.lastSyncMs));
    m_repoDetailStatus->setText(details);
}

QUrl MainWindow::catalogApiUrl() const
{
    QUrl url(canonicalServerUrl(m_serverUrlEdit ? m_serverUrlEdit->text() : QString()));
    if (!url.isValid() || url.host().isEmpty())
        url = QUrl(kDefaultServerUrl);
    if (url.scheme() == "ws")
        url.setScheme(QStringLiteral("http"));
    else if (url.scheme() == "wss")
        url.setScheme(QStringLiteral("https"));
    url.setPath(QStringLiteral("/api/repositories"));
    url.setQuery(QString());
    url.setFragment(QString());
    return url;
}

void MainWindow::deleteCatalogRepository(const QString &owner, const QString &name)
{
    const QString safeOwner = repoSegment(owner, QStringLiteral("owner"));
    const QString safeName = repoSegment(name, QStringLiteral("repository"));
    if (safeOwner.isEmpty() || safeName.isEmpty())
        return;
    if (!hasOwnerSigningCapability(safeOwner))
        return;
    if (!m_profileIdentity.isValid() && !m_profileIdentity.load()) {
        logSystem("Catalog: could not load identity to remove old website entry.");
        return;
    }

    const QString ts = QString::number(QDateTime::currentMSecsSinceEpoch());
    const QByteArray canonical =
        ("forkmesh-catalog-delete-v1\n" + safeOwner + "\n" + safeName + "\n" + ts)
            .toUtf8();
    QUrl url = catalogApiUrl();
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("owner"), safeOwner);
    query.addQueryItem(QStringLiteral("name"), safeName);
    query.addQueryItem(QStringLiteral("ts"), ts);
    query.addQueryItem(QStringLiteral("sig"), m_profileIdentity.signData(canonical));
    url.setQuery(query);

    QNetworkReply *reply = m_networkAccess->deleteResource(QNetworkRequest(url));
    connect(reply, &QNetworkReply::finished, this,
            [this, reply, safeOwner, safeName] {
                reply->deleteLater();
                if (reply->error() == QNetworkReply::NoError) {
                    logSystem("Catalog: removed old website entry " + safeOwner +
                              "/" + safeName + ".");
                } else {
                    logSystem("Catalog: could not remove old website entry " +
                              safeOwner + "/" + safeName + ": " +
                              reply->errorString());
                }
            });
}

void MainWindow::migrateReposForProfileName(const QString &oldOwner,
                                            const QString &newOwner)
{
    const QString oldName = repoSegment(oldOwner, QStringLiteral("owner"));
    const QString newName = repoSegment(newOwner, QStringLiteral("owner"));
    if (oldName.isEmpty() || newName.isEmpty() || oldName == newName)
        return;

    QList<int> toRepublish;
    bool changed = false;
    for (int i = 0; i < m_repositories.size(); ++i) {
        RepositoryRecord &repo = m_repositories[i];
        if (repo.previewOnly)
            continue;
        const QString repoName = repoSegment(repo.name, QStringLiteral("repository"));
        const bool wasPublished = repo.publishToNetwork || repo.publishedAtMs > 0;
        if (wasPublished)
            deleteCatalogRepository(oldName, repoName);
        if (repo.owner == oldName || wasPublished) {
            repo.owner = newName;
            changed = true;
            if (repo.publishToNetwork)
                toRepublish.append(i);
        }
    }

    if (!changed)
        return;
    saveRepositories();
    installAllPushHooks();
    refreshRepositoryList();
    startRepoHosts();
    for (int index : std::as_const(toRepublish))
        publishRepository(index, false);
    logSystem("Renamed local published repositories from " + oldName + " to " +
              newName + ".");
}

void MainWindow::stopRepoHosts()
{
    for (RepoHost *host : std::as_const(m_repoHosts)) {
        host->stop();
        host->deleteLater();
    }
    m_repoHosts.clear();
    m_repoHostKeys.clear();
    stopNodeEventSocket();
    refreshNetworkDiagnostics();
}

void MainWindow::startRepoHosts()
{
    // The per-repository persistent socket is retired. Public and encrypted
    // repository bytes use direct HTTPS, while the /api/sync drain carries
    // small control-plane changes — now pushed live over the per-owner node
    // event socket, with the bounded poll as the fallback. Publish/settings
    // call sites use this method as the serving-lifecycle hook, so it is also
    // where the event channel follows the node's online/offline state.
    if (!m_repoHosts.isEmpty())
        stopRepoHosts();
    m_repoHostKeys.clear();
    startNodeEventSocket();
    refreshNetworkDiagnostics();
}

// One WebSocket per signed-in owner account to the relay's ForkMeshNodes
// Durable Object. The relay pushes a payload-free {"type":"event","topic"}
// frame the instant a web submission lands for any owned repo; the node
// answers with its usual debounced signed GET /api/sync. While the channel is
// up the 5-minute fallback poll relaxes to 15 minutes — pushes carry the fast
// path, so steady-state HTTPS polling drops to a third.
void MainWindow::startNodeEventSocket()
{
    if (m_nodeOffline) {
        // Honour a node parked offline: no serving, no heartbeat, no live
        // event channel. The bounded sync poll still runs.
        stopNodeEventSocket();
        return;
    }
    if (!hasOwnerSigningCapability()) {
        stopNodeEventSocket();
        return;
    }
    if (m_nodeEventSocket)
        return; // already running; the socket reconnects on its own
    m_nodeEventSocket = new NodeEventSocket(this);
    m_nodeEventSocket->setConnectionAuthorizer([this](const QUrl &endpoint) {
        return authorizeFirewallConnection(QStringLiteral("WebSocket"),
                                           endpoint);
    });
    // Re-evaluated on every (re)connect attempt so each upgrade carries a
    // freshly-signed drain token and follows account/relay changes.
    m_nodeEventSocket->setUrlFactory([this]() -> QUrl {
        const QString account = m_accountName.isEmpty()
            ? QSettings().value(kAccountNameSetting).toString().trimmed()
            : m_accountName;
        if (!hasOwnerSigningCapability(account) || !m_profileIdentity.isValid())
            return QUrl();
        const QString owner =
            repoSegment(account, QStringLiteral("owner"));
        if (owner.isEmpty())
            return QUrl();
        QUrl url = catalogApiUrl();
        url.setScheme(url.scheme() == QLatin1String("http")
                          ? QStringLiteral("ws")
                          : QStringLiteral("wss"));
        url.setPath(QStringLiteral("/api/nodes/events"));
        url.setQuery(signedInboxQuery(owner));
        return url;
    });
    connect(m_nodeEventSocket, &NodeEventSocket::eventReceived, this,
            [this](const QString &topic, const QString &repo) {
                Q_UNUSED(topic);
                Q_UNUSED(repo);
                scheduleRelaySync();
            });
    connect(m_nodeEventSocket, &NodeEventSocket::connectedChanged, this,
            [this](bool connected) {
                if (!m_inboxPollTimer)
                    return;
                if (connected) {
                    // Catch up on anything queued while the channel was down,
                    // then let pushes carry the fast path.
                    scheduleRelaySync();
                    m_inboxPollTimer->setInterval(15 * 60 * 1000);
                } else {
                    m_inboxPollTimer->setInterval(5 * 60 * 1000);
                }
            });
    connect(m_nodeEventSocket, &NodeEventSocket::systemMessage, this,
            [this](const QString &text) { logSystem(text); });
    m_nodeEventSocket->start();
}

void MainWindow::stopNodeEventSocket()
{
    if (!m_nodeEventSocket)
        return;
    m_nodeEventSocket->stop();
    m_nodeEventSocket->deleteLater();
    m_nodeEventSocket = nullptr;
    if (m_inboxPollTimer)
        m_inboxPollTimer->setInterval(5 * 60 * 1000);
}

void MainWindow::onRequestServed(const QString &owner, const QString &name, bool clone)
{
    QPair<int, int> &stats = m_repoStats[owner + "/" + name];
    stats.first += 1; // served through the mainnode
    if (clone)
        stats.second += 1; // git clone
    // Everything below is coalesced. A clone/browse burst fires this slot dozens
    // of times a second, and re-running the full repository-list rebuild
    // (per-repo git reads) plus a QSettings write for each one stalled the GUI
    // for seconds (stall log: refreshRepositoryList <- onRequestServed). The one
    // per-event step used to be flashing our own dot on the activity strip above
    // the Mirror nodes tab; that strip is gone (adhoc #420).
    if (!m_requestServedFlushTimer) {
        m_requestServedFlushTimer = new QTimer(this);
        m_requestServedFlushTimer->setSingleShot(true);
        m_requestServedFlushTimer->setInterval(1000);
        connect(m_requestServedFlushTimer, &QTimer::timeout, this, [this] {
            saveRepoStats();
            refreshRepositoryList();
            if (m_repoDetailIndex >= 0 && m_repoDetailIndex < m_repositories.size())
                updateRepoDetailStatus();
            // Hosting stats live in the node profile; keep them current while
            // it is open.
            if (m_nodeProfilePanel && m_nodeProfilePanel->isVisible())
                refreshProfileHostingStats();
        });
    }
    // Not restarted while pending: under continuous traffic the flush still
    // lands once a second instead of being pushed out forever.
    if (!m_requestServedFlushTimer->isActive())
        m_requestServedFlushTimer->start();
}

void MainWindow::loadRepoStats()
{
    m_repoStats.clear();
    const QJsonObject obj =
        QJsonDocument::fromJson(
            QSettings().value(QStringLiteral("repositories/stats")).toByteArray())
            .object();
    for (auto it = obj.constBegin(); it != obj.constEnd(); ++it) {
        const QJsonObject entry = it.value().toObject();
        m_repoStats.insert(it.key(),
                           {entry.value("served").toInt(),
                            entry.value("clones").toInt()});
    }
}

void MainWindow::saveRepoStats() const
{
    QJsonObject obj;
    for (auto it = m_repoStats.constBegin(); it != m_repoStats.constEnd(); ++it) {
        obj.insert(it.key(), QJsonObject{{"served", it.value().first},
                                         {"clones", it.value().second}});
    }
    QSettings().setValue(QStringLiteral("repositories/stats"),
                         QJsonDocument(obj).toJson(QJsonDocument::Compact));
}

QString MainWindow::catalogPublishKey(const RepositoryRecord &repo) const
{
    const QString owner = catalogOwner(repo);
    const QString name = repoSegment(repo.name, QStringLiteral("repository"));
    if (owner.isEmpty() || name.isEmpty())
        return QString();
    return owner + "/" + name;
}

int MainWindow::repositoryIndexForCatalogPublishKey(const QString &key) const
{
    if (key.isEmpty())
        return -1;
    for (int i = 0; i < m_repositories.size(); ++i) {
        const RepositoryRecord &repo = m_repositories.at(i);
        if (!repo.previewOnly && catalogPublishKey(repo) == key)
            return i;
    }
    return -1;
}

void MainWindow::scheduleCatalogPublish(const QString &key,
                                        bool showDialogOnError,
                                        qint64 minDelayMs)
{
    if (key.isEmpty())
        return;

    if (showDialogOnError)
        m_catalogPublishDialogQueued.insert(key);

    if (m_catalogPublishInFlight.contains(key)) {
        m_catalogPublishQueued.insert(key);
        return;
    }

    if (m_catalogPublishTimers.contains(key))
        return;

    const QString owner = catalogPublishOwnerFromKey(key);
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    const qint64 lastAttempt = m_catalogPublishOwnerLastAttemptMs.value(owner, 0);
    const qint64 cooldownDelay =
        lastAttempt > 0
            ? qMax<qint64>(0, lastAttempt + kCatalogPublishMinIntervalMs - now)
            : 0;
    const qint64 delay =
        qMax(minDelayMs, qMax(kCatalogPublishDebounceMs, cooldownDelay));

    auto *timer = new QTimer(this);
    timer->setSingleShot(true);
    m_catalogPublishTimers.insert(key, timer);
    connect(timer, &QTimer::timeout, this, [this, key, timer] {
        if (m_catalogPublishTimers.value(key) == timer)
            m_catalogPublishTimers.remove(key);
        timer->deleteLater();

        const bool wantsDialog = m_catalogPublishDialogQueued.contains(key);
        m_catalogPublishDialogQueued.remove(key);

        if (m_catalogPublishInFlight.contains(key)) {
            m_catalogPublishQueued.insert(key);
            if (wantsDialog)
                m_catalogPublishDialogQueued.insert(key);
            return;
        }

        const QString owner = catalogPublishOwnerFromKey(key);
        const qint64 now = QDateTime::currentMSecsSinceEpoch();
        const qint64 lastAttempt =
            m_catalogPublishOwnerLastAttemptMs.value(owner, 0);
        const qint64 cooldownDelay =
            lastAttempt > 0
                ? qMax<qint64>(0,
                               lastAttempt + kCatalogPublishMinIntervalMs - now)
                : 0;
        if (cooldownDelay > 0) {
            scheduleCatalogPublish(key, wantsDialog, cooldownDelay);
            return;
        }

        const int index = repositoryIndexForCatalogPublishKey(key);
        if (index < 0)
            return;
        publishRepositoryNow(index, wantsDialog);
    });
    timer->start(static_cast<int>(
        qMin<qint64>(delay, std::numeric_limits<int>::max())));
}

void MainWindow::publishRepository(int index, bool showDialogOnError)
{
    if (index < 0 || index >= m_repositories.size())
        return;
    if (m_repositories.at(index).previewOnly)
        return;
    scheduleCatalogPublish(catalogPublishKey(m_repositories.at(index)),
                           showDialogOnError);
}

void MainWindow::publishRepositoryAfterMirrorRefresh(int index,
                                                     bool showDialogOnError,
                                                     bool quietSync)
{
    if (index < 0 || index >= m_repositories.size())
        return;
    const RepositoryRecord &repo = m_repositories.at(index);
    if (repo.previewOnly)
        return;

    // The website serves the bare mirror, not the working copy. Refresh that
    // mirror first whenever there is an upstream/source path; syncRepository's
    // success path publishes the catalog record with the freshly-served refs.
    if (!repositorySource(repo).isEmpty()) {
        if (m_syncingRepos.contains(index)) {
            if (!quietSync)
                logSystem("Catalog: mirror refresh already running for " +
                          repo.owner + "/" + repo.name +
                          "; publishing after it finishes.");
            return;
        }
        syncRepository(index, quietSync);
        return;
    }

    publishRepository(index, showDialogOnError);
}

void MainWindow::ensurePrivateRepositoryControlPlane(
    int index, bool showDialogOnError)
{
    if (index < 0 || index >= m_repositories.size() || !m_networkAccess)
        return;
    const RepositoryRecord repo = m_repositories.at(index);
    if (!repo.isPrivate ||
        !PrivateMirrorStore::isOpaqueId(repo.privateReplicaId))
        return;
    PrivateMirrorStore::Metadata metadata;
    QString localError;
    if (!PrivateMirrorStore::inspectReplica(
            privateReplicaRoot(), repo.privateReplicaId, &metadata,
            &localError)) {
        if (showDialogOnError)
            flashMessage(QStringLiteral(
                             "Private publication is blocked: the encrypted "
                             "replica could not be authenticated."),
                         true);
        return;
    }
    const QString node = configuredPrivateMirrorNode();
    if (node.isEmpty()) {
        const QString message = QStringLiteral(
            "Private publication is blocked until the Control Node page has "
            "the exact Cloudflare mirror node name used by the registered "
            "direct-HTTPS endpoint.");
        logSystem(QStringLiteral("Private mirror: ") + message);
        if (showDialogOnError)
            flashMessage(message, true);
        return;
    }
    if (m_accountSessionToken.trimmed().isEmpty()) {
        const QString message = QStringLiteral(
            "Private publication needs an authenticated owner account session "
            "to register the public encryption key and repository policy. Sign "
            "in from Account settings; private keys remain on this device.");
        logSystem(QStringLiteral("Private mirror: ") + message);
        if (showDialogOnError)
            flashMessage(message, true);
        return;
    }

    QByteArray vaultSecret =
        privateIdentityVaultSecret(m_profileIdentity);
    MirrorCrypto::Identity mirrorIdentity;
    if (!PrivateMirrorRuntime::loadOrCreateIdentity(
            privateIdentityVaultPath(), vaultSecret, &mirrorIdentity,
            &localError)) {
        vaultSecret.fill('\0');
        const QString message = QStringLiteral(
            "Private publication is blocked because the owner-only encryption "
            "identity vault could not be unlocked.");
        logSystem(QStringLiteral("Private mirror: ") + message);
        if (showDialogOnError)
            flashMessage(message, true);
        return;
    }
    vaultSecret.fill('\0');
    vaultSecret.clear();
    const QString ownerKeyId = mirrorIdentity.keyId();
    const QJsonObject publicBundle = mirrorIdentity.publicBundle();
    mirrorIdentity.x25519Priv.fill('\0');
    mirrorIdentity.mlkemPriv.fill('\0');
    mirrorIdentity = {};
    const QString repoKey = catalogPublishKey(repo);
    const QString controlKey =
        privateControlPlaneKey(catalogApiUrl(), repoKey, ownerKeyId);
    if (m_privateControlReady.contains(controlKey)) {
        publishRepository(index, showDialogOnError);
        return;
    }
    if (m_privateControlInFlight.contains(controlKey))
        return;
    m_privateControlInFlight.insert(controlKey);

    auto fail = [this, controlKey, showDialogOnError](
                    const QString &message) {
        m_privateControlInFlight.remove(controlKey);
        logSystem(QStringLiteral("Private mirror: ") + message);
        if (showDialogOnError)
            flashMessage(message, true);
    };

    QUrl ownerKeysUrl = catalogApiUrl();
    ownerKeysUrl.setPath(QStringLiteral("/api/security/owner-keys"));
    ownerKeysUrl.setQuery(QString());
    ownerKeysUrl.setFragment(QString());
    QNetworkRequest keyRequest(ownerKeysUrl);
    keyRequest.setHeader(QNetworkRequest::ContentTypeHeader,
                         QStringLiteral("application/json"));
    keyRequest.setRawHeader(
        "Authorization",
        QByteArrayLiteral("Bearer ") +
            m_accountSessionToken.toUtf8());
    QNetworkReply *keyReply = m_networkAccess->post(
        keyRequest,
        QJsonDocument(QJsonObject{
                          {QStringLiteral("publicBundle"), publicBundle},
                      })
            .toJson(QJsonDocument::Compact));
    connect(
        keyReply, &QNetworkReply::finished, this,
        [this, keyReply, index, repo, repoKey, controlKey, ownerKeyId,
         showDialogOnError, fail] {
            const QByteArray body = keyReply->readAll();
            const int status =
                keyReply->attribute(
                            QNetworkRequest::HttpStatusCodeAttribute)
                    .toInt();
            const QJsonObject response =
                QJsonDocument::fromJson(body).object();
            keyReply->deleteLater();
            if (status < 200 || status >= 300 ||
                !response.value(QStringLiteral("ok")).toBool() ||
                response.value(QStringLiteral("privateKeysStored"))
                    .toBool(true) ||
                response.value(QStringLiteral("keyId")).toString() !=
                    ownerKeyId) {
                fail(QStringLiteral(
                    "Private publication is blocked: the relay did not accept "
                    "the public-only owner encryption key."));
                return;
            }

            QUrl policyUrl = catalogApiUrl();
            policyUrl.setPath(
                QStringLiteral("/api/repo/") +
                repoSegment(catalogOwner(repo), QStringLiteral("owner")) +
                QLatin1Char('/') +
                repoSegment(repo.name, QStringLiteral("repository")) +
                QStringLiteral("/privacy"));
            policyUrl.setQuery(QString());
            policyUrl.setFragment(QString());
            QNetworkRequest policyRequest(policyUrl);
            policyRequest.setHeader(
                QNetworkRequest::ContentTypeHeader,
                QStringLiteral("application/json"));
            policyRequest.setRawHeader(
                "Authorization",
                QByteArrayLiteral("Bearer ") +
                    m_accountSessionToken.toUtf8());
            QNetworkReply *policyReply = m_networkAccess->post(
                policyRequest,
                QJsonDocument(QJsonObject{
                                  {QStringLiteral("ownerKeyId"),
                                   ownerKeyId},
                              })
                    .toJson(QJsonDocument::Compact));
            connect(
                policyReply, &QNetworkReply::finished, this,
                [this, policyReply, index, repoKey, controlKey, ownerKeyId,
                 showDialogOnError, fail] {
                    const QByteArray policyBody = policyReply->readAll();
                    const int policyStatus =
                        policyReply
                            ->attribute(
                                QNetworkRequest::
                                    HttpStatusCodeAttribute)
                            .toInt();
                    const QJsonObject policy =
                        QJsonDocument::fromJson(policyBody).object();
                    policyReply->deleteLater();
                    if (policyStatus < 200 || policyStatus >= 300 ||
                        !policy.value(QStringLiteral("ok")).toBool() ||
                        policy.value(QStringLiteral("ownerKeyId"))
                                .toString() != ownerKeyId ||
                        !policy
                             .value(QStringLiteral(
                                 "requireMirrorEncryption"))
                             .toBool() ||
                        !policy
                             .value(QStringLiteral("requireAgentE2EE"))
                             .toBool()) {
                        fail(QStringLiteral(
                            "Private publication is blocked: the relay did not "
                            "confirm the mandatory owner-only encryption policy."));
                        return;
                    }
                    m_privateControlInFlight.remove(controlKey);
                    m_privateControlReady.insert(controlKey);
                    if (repositoryIndexForCatalogPublishKey(repoKey) >= 0)
                        publishRepository(
                            repositoryIndexForCatalogPublishKey(repoKey),
                            showDialogOnError);
                });
        });
}

void MainWindow::registerPrivateReplicaRoute(int index)
{
    if (index < 0 || index >= m_repositories.size() || !m_networkAccess ||
        !m_profileIdentity.isValid())
        return;
    const RepositoryRecord repo = m_repositories.at(index);
    if (!repo.isPrivate ||
        !PrivateMirrorStore::isOpaqueId(repo.privateReplicaId))
        return;
    const QString node = configuredPrivateMirrorNode();
    if (node.isEmpty()) {
        logSystem(QStringLiteral(
            "Private mirror: route registration remains blocked until the "
            "configured Cloudflare node name matches a direct-HTTPS endpoint."));
        return;
    }
    PrivateMirrorStore::Metadata metadata;
    QString error;
    if (!PrivateMirrorStore::inspectReplica(
            privateReplicaRoot(), repo.privateReplicaId, &metadata, &error) ||
        metadata.replicaFileSha256.size() != 64) {
        logSystem(QStringLiteral(
            "Private mirror: route registration refused an unauthenticated "
            "local replica."));
        return;
    }
    const qint64 issuedAt =
        QDateTime::currentDateTimeUtc().toMSecsSinceEpoch();
    const QString owner = catalogOwner(repo);
    const QString name =
        repoSegment(repo.name, QStringLiteral("repository"));
    const QByteArray canonical =
        forkmesh::control::privateReplicaRouteSigningPayload(
            owner, name, node, repo.privateReplicaId,
            metadata.replicaFileSha256, metadata.keyEpoch, true, issuedAt,
            &error);
    const QString signature =
        canonical.isEmpty() ? QString()
                            : m_profileIdentity.signData(canonical);
    if (signature.isEmpty()) {
        logSystem(QStringLiteral(
            "Private mirror: route registration could not create the local "
            "owner signature."));
        return;
    }
    QUrl url = catalogApiUrl();
    url.setPath(QStringLiteral("/api/mirrors/private"));
    url.setQuery(QString());
    url.setFragment(QString());
    const QJsonObject payload{
        {QStringLiteral("owner"), owner},
        {QStringLiteral("repository"), name},
        {QStringLiteral("node"), node},
        {QStringLiteral("opaqueId"), repo.privateReplicaId},
        {QStringLiteral("replicaSha256"),
         metadata.replicaFileSha256},
        {QStringLiteral("keyEpoch"), double(metadata.keyEpoch)},
        {QStringLiteral("active"), true},
        {QStringLiteral("issuedAt"), double(issuedAt)},
        {QStringLiteral("signature"), signature},
    };
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader,
                      QStringLiteral("application/json"));
    QNetworkReply *reply = m_networkAccess->post(
        request, QJsonDocument(payload).toJson(QJsonDocument::Compact));
    connect(reply, &QNetworkReply::finished, this,
            [this, reply, metadata, expectedOpaqueId = repo.privateReplicaId] {
        const QByteArray body = reply->readAll();
        const int status =
            reply->attribute(QNetworkRequest::HttpStatusCodeAttribute)
                .toInt();
        const QJsonObject response =
            QJsonDocument::fromJson(body).object();
        reply->deleteLater();
        const QString expectedAccessPath =
            QStringLiteral("/api/private-replicas/") + expectedOpaqueId;
        if (status >= 200 && status < 300 &&
            response.value(QStringLiteral("ok")).toBool() &&
            response.value(QStringLiteral("active")).toBool() &&
            quint64(response.value(QStringLiteral("keyEpoch")).toDouble()) ==
                metadata.keyEpoch &&
            response.value(QStringLiteral("accessPath")).toString() ==
                expectedAccessPath) {
            logSystem(QStringLiteral(
                          "Private mirror: owner-signed opaque HTTPS route is "
                          "active for encrypted epoch %1.")
                          .arg(metadata.keyEpoch));
            return;
        }
        logSystem(QStringLiteral(
            "Private mirror: ciphertext remains local and undiscoverable "
            "because route registration was not accepted. Confirm the exact "
            "Cloudflare node endpoint is registered and healthy."));
    });
}

void MainWindow::publishRepositoryNow(int index, bool showDialogOnError)
{
    if (index < 0 || index >= m_repositories.size())
        return;
    if (m_repositories.at(index).previewOnly)
        return;
    RepositoryRecord &repo = m_repositories[index];
    const QString publishKey = catalogPublishKey(repo);
    if (publishKey.isEmpty())
        return;
    if (!hasOwnerSigningCapability(catalogOwner(repo))) {
        // Public hosting requires an already registered, locally signable node
        // identity. Reward configuration is separate and never creates an
        // account or guarantees a transfer.
        const QString message = QStringLiteral(
            "Mirroring this repo locally needs nothing extra. To host it on the "
            "network, register or sign in from Account settings. A healthy host "
            "with reward settings configured may be eligible for voluntary "
            "community incentives.");
        logSystem(message);
        if (showDialogOnError)
            flashMessage(message, /*error=*/true);
        return;
    }
    if (!m_profileIdentity.isValid() && !m_profileIdentity.load()) {
        logSystem("Catalog: could not load identity for repository publishing.");
        return;
    }

    PrivateMirrorStore::Metadata privateMetadata;
    if (repo.isPrivate) {
        QString privateError;
        if (!PrivateMirrorStore::isOpaqueId(repo.privateReplicaId) ||
            !PrivateMirrorStore::inspectReplica(
                privateReplicaRoot(), repo.privateReplicaId,
                &privateMetadata, &privateError) ||
            privateMetadata.keyEpoch == 0 ||
            privateMetadata.ciphertextSha256.size() != 64 ||
            privateMetadata.replicaFileSha256.size() != 64) {
            const QString message = QStringLiteral(
                "Private publication is blocked until this repository has an "
                "authenticated owner-sealed .fm-private replica.");
            logSystem(QStringLiteral("Private mirror: ") + message);
            if (showDialogOnError)
                flashMessage(message, true);
            return;
        }
        const QString controlKey = privateControlPlaneKey(
            catalogApiUrl(), publishKey, privateMetadata.ownerKeyId);
        if (!m_privateControlReady.contains(controlKey)) {
            ensurePrivateRepositoryControlPlane(index, showDialogOnError);
            return;
        }
    }

    const QString servedHeadBranch = mirrorHeadBranch(repo.mirrorPath);
    const QString servedHeadCommit =
        mirrorBranchCommit(repo.mirrorPath, servedHeadBranch);
    if (servedHeadCommit.isEmpty()) {
        if (!repositorySource(repo).isEmpty() && !m_syncingRepos.contains(index))
            syncRepository(index, /*quiet=*/true);
        logSystem("Catalog: delaying publish for " + repo.owner + "/" +
                  repo.name + " until its mirror has a served commit.");
        return;
    }

    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    // Always publish under the registered account name so the catalog dedups by
    // account/name (one entry per fork) and the server can verify ownership.
    // catalogOwner() is shared with the direct-HTTPS route and update channel
    // so every layer resolves the same logical repository.
    const QString owner = catalogOwner(repo);
    const QString name = repoSegment(repo.name, QStringLiteral("repository"));
    QString updatedAt = QString::number(now);
    QJsonObject contributionFields;
    QStringList changedFiles;
    // A stable identity for the logical repo: its first (root) commit, shared by
    // every node mirroring it. The network page groups mirrors by this so the same
    // repo under different owners shows as one card. Not part of the signature.
    QString rootCommit;
    {
        const QString gitDir =
            (!repo.localPath.trimmed().isEmpty() && QDir(repo.localPath).exists(".git"))
                ? repo.localPath
                : repo.mirrorPath;
        // Resolve the earliest root commit. Prefer HEAD; but a bare mirror cloned
        // from the relay can carry an unset/dangling HEAD (the relay serves
        // git-upload-pack without advertising a symref HEAD), so "rev-list ... HEAD"
        // fails and leaves rootCommit empty. A wrong/empty root drops that mirror
        // into a different group key (worker repo_mirror_group_key), so the owner's
        // mirror-nodes panel never lists it next to the source of truth — the node
        // shows up on the mirror but not on the source (issue #243, adhoc #134).
        auto firstRoot = [&](const QStringList &args) -> QString {
            QByteArray out;
            if (gitDir.trimmed().isEmpty() ||
                !runGitCapture(gitDir, args, &out, nullptr))
                return QString();
            const QStringList roots =
                QString::fromUtf8(out).split('\n', Qt::SkipEmptyParts);
            return roots.isEmpty() ? QString() : roots.last().trimmed();
        };
        rootCommit = firstRoot({"rev-list", "--max-parents=0", "HEAD"});
        if (rootCommit.isEmpty()) {
            // HEAD is unset. Compute the root of the branch this mirror actually
            // serves (mirrorHeadBranch: the same default branch the source's HEAD
            // points to), NOT a blind "--all" walk. This repo has several root
            // commits — one per independent history the mirror also holds (agent
            // branches, imported subtrees). "rev-list --max-parents=0 --all" returns
            // ALL of them and "roots.last()" would pick whichever an unrelated
            // history contributes, so the mirror published a different rootCommit
            // than the source and landed in its own group (adhoc #134). Resolving
            // the served branch yields the source's root regardless of HEAD's state.
            const QString branch = mirrorHeadBranch(gitDir);
            if (!branch.isEmpty())
                rootCommit = firstRoot({"rev-list", "--max-parents=0", branch});
        }
        // Last resort (no HEAD and no resolvable served branch, e.g. a truly empty
        // ref set): fall back to --all so a single-root mirror still groups.
        if (rootCommit.isEmpty())
            rootCommit = firstRoot({"rev-list", "--max-parents=0", "--all"});
    }
    // Owner-signed fingerprint of the refs this node serves (sha256 over the
    // canonical heads+tags advertisement). The relay pins this and refuses to
    // serve any mirror whose live advertisement doesn't hash to it, so a tampered
    // or rolled-back mirror can never be cloned. MUST match the worker's
    // advertised_refs_canonical(): "<sha> <refname>" lines for refs/heads/* and
    // refs/tags/* only, sorted, joined by '\n'.
    const QString stateHash = mirrorStateHash(repo.mirrorPath);
    // Repository details (about text + website) live in the committed
    // .forkmesh/info.json, the single source of truth (issue #232). Prefer it over
    // the locally-cached record fields so the website's About panel is filled from
    // info.json even for a mirror that cloned the repo but never had its
    // description typed in locally (adhoc #86).
    QString publishedWebsite;
    QString publishedDescription = repo.description;
    QString publishedPrimaryLanguage;
    QStringList publishedTopics;
    if (!repo.localPath.trimmed().isEmpty()) {
        QFile file(QDir(repo.localPath).filePath(kRepoInfoPath));
        if (file.open(QIODevice::ReadOnly)) {
            const QJsonObject info = QJsonDocument::fromJson(file.readAll()).object();
            publishedWebsite = info.value(QStringLiteral("website")).toString();
            publishedPrimaryLanguage =
                info.value(QStringLiteral("language")).toString().trimmed();
            for (const QJsonValue &topic :
                 info.value(QStringLiteral("topics")).toArray()) {
                const QString value = topic.toString().trimmed();
                if (!value.isEmpty())
                    publishedTopics.append(value);
            }
            const QString about =
                info.value(QStringLiteral("about")).toString().trimmed();
            if (!about.isEmpty())
                publishedDescription = about;
        }
    }
    if (index == m_repoDetailIndex) {
        if (publishedWebsite.isEmpty())
            publishedWebsite = m_repoInfo.website;
        if (publishedDescription.trimmed().isEmpty() &&
            !m_repoInfo.about.trimmed().isEmpty())
            publishedDescription = m_repoInfo.about;
        if (publishedPrimaryLanguage.isEmpty())
            publishedPrimaryLanguage = m_repoInfo.language;
        if (publishedTopics.isEmpty())
            publishedTopics = m_repoInfo.topics;
    }
    // Node facts the live Mirror nodes view shows per node (latest commit, issue
    // count, platform, version, node id). Published alongside the mirror so those
    // columns stay populated for a node that's offline or only intermittently in
    // the room — otherwise a catalog-backed row falls back to em-dashes for
    // everything but sync time and size (adhoc #56). The commit/issue figures
    // mirror the live advert (setMirroredRepos); platform/version/id come from our
    // own roster entry (the same values makeMessage broadcasts).
    const MirrorBranchTip primaryTip =
        mirrorPrimaryBranchTip(repo.mirrorPath, repo.localPath);
    QString headBranch = primaryTip.branch;
    QString headCommit = primaryTip.commit;
    if (headCommit.isEmpty()) {
        headBranch = servedHeadBranch;
        headCommit = servedHeadCommit;
    }
    RepoContributionSnapshotInput snapshotInput;
    snapshotInput.workTreePath = repo.localPath;
    snapshotInput.mirrorPath = repo.mirrorPath;
    snapshotInput.branch = headBranch;
    snapshotInput.head = headCommit;
    snapshotInput.publishingKey = m_profileIdentity.publicKey();
    snapshotInput.capturedAtMs = now;
    const QString contributionScanKey =
        RepoContributionPublicationCache::key(
            owner, name, headCommit, headBranch,
            m_profileIdentity.publicKey(),
            QStringLiteral("dependency-scan"));
    QString contributionSnapshotKey;
    QJsonObject contributionLogoPayload;
    if (repo.isPrivate) {
        const QString previousScanKey =
            m_catalogContributionScanKey.take(publishKey);
        const QString previousSnapshotKey =
            m_catalogContributionSnapshotKey.take(publishKey);
        m_contributionPublicationCache.invalidate(previousScanKey);
        m_contributionPublicationCache.invalidate(previousSnapshotKey);
        m_catalogContributionDependencyFingerprint.remove(publishKey);
        m_catalogContributionPreparedScanKey.remove(publishKey);
        m_catalogContributionPreparedSnapshotKey.remove(publishKey);
        m_contributionPublicationCache.clearStaleRetry(publishKey);
    } else {
        const QString previousScanKey =
            m_catalogContributionScanKey.value(publishKey);
        if (!previousScanKey.isEmpty() &&
            previousScanKey != contributionScanKey) {
            m_contributionPublicationCache.invalidate(previousScanKey);
        }
        m_catalogContributionScanKey.insert(publishKey,
                                            contributionScanKey);

        const QString preparedScanKey =
            m_catalogContributionPreparedScanKey.take(publishKey);
        const QString preparedSnapshotKey =
            m_catalogContributionPreparedSnapshotKey.take(publishKey);
        if (preparedScanKey == contributionScanKey)
            contributionSnapshotKey = preparedSnapshotKey;

        const QString previousSnapshotKey =
            m_catalogContributionSnapshotKey.value(publishKey);
        std::optional<RepoContributionSnapshot> cachedSnapshot;
        if (!contributionSnapshotKey.isEmpty()) {
            cachedSnapshot = m_contributionPublicationCache.lookup(
                contributionSnapshotKey, now);
        }
        if (!cachedSnapshot.has_value()) {
            const auto beginResult = m_contributionPublicationCache.begin(
                contributionScanKey, showDialogOnError);
            if (beginResult ==
                RepoContributionPublicationCache::BeginResult::Started) {
                const QString expectedDependencyFingerprint =
                    m_catalogContributionDependencyFingerprint.value(
                        publishKey);
                const bool cachedSnapshotAvailable =
                    !previousSnapshotKey.isEmpty() &&
                    m_contributionPublicationCache.lookup(
                        previousSnapshotKey, now).has_value();
                auto *watcher =
                    new QFutureWatcher<RepoContributionPreparation>(this);
                connect(
                    watcher,
                    &QFutureWatcher<RepoContributionPreparation>::finished,
                    this,
                    [this, watcher, contributionScanKey, publishKey,
                     previousSnapshotKey, owner, name, headCommit,
                     headBranch,
                     accountPublicKey = m_profileIdentity.publicKey()] {
                        const RepoContributionPreparation preparation =
                            watcher->result();
                        watcher->deleteLater();
                        const auto completion =
                            m_contributionPublicationCache.complete(
                                contributionScanKey);
                        if (completion.discarded ||
                            m_catalogContributionScanKey.value(publishKey) !=
                                contributionScanKey) {
                            scheduleCatalogPublish(
                                publishKey, completion.requestDialog);
                            return;
                        }

                        const QString snapshotKey =
                            RepoContributionPublicationCache::key(
                                owner, name, headCommit, headBranch,
                                accountPublicKey,
                                preparation.dependencyFingerprint);
                        const qint64 completedAt =
                            QDateTime::currentMSecsSinceEpoch();
                        if (preparation.rebuiltSnapshot.has_value()) {
                            m_contributionPublicationCache.store(
                                snapshotKey,
                                *preparation.rebuiltSnapshot,
                                completedAt);
                        }
                        if (!previousSnapshotKey.isEmpty() &&
                            previousSnapshotKey != snapshotKey) {
                            m_contributionPublicationCache.invalidate(
                                previousSnapshotKey);
                        }
                        m_catalogContributionDependencyFingerprint.insert(
                            publishKey,
                            preparation.dependencyFingerprint);
                        m_catalogContributionSnapshotKey.insert(
                            publishKey, snapshotKey);
                        if (m_contributionPublicationCache.lookup(
                                snapshotKey, completedAt).has_value()) {
                            m_catalogContributionPreparedScanKey.insert(
                                publishKey, contributionScanKey);
                            m_catalogContributionPreparedSnapshotKey.insert(
                                publishKey, snapshotKey);
                        }
                        scheduleCatalogPublish(
                            publishKey, completion.requestDialog);
                    });
                watcher->setFuture(QtConcurrent::run(
                    [snapshotInput, expectedDependencyFingerprint,
                     cachedSnapshotAvailable] {
                        const forkmesh::BackgroundScope activity(
                            QStringLiteral("scan"),
                            QStringLiteral("Preparing the contribution graph"));
                        return prepareRepoContributionSnapshot(
                            snapshotInput, expectedDependencyFingerprint,
                            cachedSnapshotAvailable);
                    }));
            } else if (
                beginResult == RepoContributionPublicationCache::BeginResult::
                                   CapacityExceeded) {
                scheduleCatalogPublish(publishKey, showDialogOnError,
                                       kContributionScanCapacityRetryMs);
            }
            return;
        }

        if (cachedSnapshot->complete && cachedSnapshot->error.isEmpty()) {
            changedFiles = cachedSnapshot->changedFiles;
            contributionLogoPayload = cachedSnapshot->payload;
            const qint64 capturedAt = qint64(
                cachedSnapshot->payload.value(QStringLiteral("capturedAt"))
                    .toDouble(-1));
            const QString snapshotUpdatedAt = QString::number(capturedAt);
            const QJsonObject signedFields = signedContributionFields(
                *cachedSnapshot, owner, name, snapshotUpdatedAt,
                m_profileIdentity);
            if (!signedFields.isEmpty()) {
                updatedAt = snapshotUpdatedAt;
                contributionFields = signedFields;
            }
        }
    }
    RepoLogoMetadataInput logoInput;
    logoInput.workTreePath = repo.localPath;
    logoInput.mirrorPath = repo.mirrorPath;
    logoInput.head = headCommit;
    logoInput.description = publishedDescription;
    logoInput.primaryLanguage = publishedPrimaryLanguage;
    logoInput.topics = publishedTopics;
    logoInput.contributionPayload = contributionLogoPayload;
    const QJsonObject logoMetadata = buildRepoLogoMetadata(logoInput);
    // What the advertised head commit actually says, and who wrote it, so the
    // Mirror nodes view and the World cabinets can name a node's latest commit
    // even while that node is offline.
    const CommitIdentity headIdentity =
        mirrorCommitIdentity(repo.mirrorPath, repo.localPath, headCommit);
    const int issueCount = mirrorIssueCount(repo.mirrorPath, headBranch);
    // Highest issue number ever assigned (not just the open count), so the
    // relay can propose the same next number the desktop would for a ForkBot-
    // filed issue and answer the chat with that number immediately.
    const int issueMaxNumber = mirrorIssueMaxNumber(repo.mirrorPath, headBranch);
    const int commitCount = mirrorCommitCount(repo.mirrorPath, headBranch);
    const int branchCount = mirrorBranchCount(repo.mirrorPath);
    const int pullCount = mirrorPullCount(repo.mirrorPath, headBranch);
    const int discussionCount = mirrorDiscussionCount(repo.mirrorPath, headBranch);
    const QJsonArray activityWeeks =
        mirrorCommitActivityWeeks(repo.mirrorPath, headBranch);
    const int worktreeCount = mirrorWorktreeCount(repo.localPath);
    const int artifactCount = mirrorArtifactCount(repo.mirrorPath);
    QString selfPlatform, selfVersion, selfNodeId;
    int selfCpuPercent = -1;
    qint64 selfMemUsedBytes = 0;
    qint64 selfMemTotalBytes = 0;
    qint64 selfDiskUsedBytes = 0;
    qint64 selfDiskTotalBytes = 0;
    const QSettings telemetrySettings;
    const bool shareCpu =
        telemetrySettings.value(TelemetrySettings::kReportCpu, false).toBool();
    const bool shareMemory =
        telemetrySettings.value(TelemetrySettings::kReportMemory, false).toBool();
    const bool shareDisk =
        telemetrySettings.value(TelemetrySettings::kReportDisk, false).toBool();
    for (const MemberInfo &member : std::as_const(m_homeRoster)) {
        if (member.self) {
            selfPlatform = member.platform;
            selfVersion = member.version;
            selfNodeId = member.id;
            if (shareCpu && std::isfinite(member.cpuPercent) &&
                member.cpuPercent >= 0.0) {
                selfCpuPercent =
                    qRound(qBound(0.0, member.cpuPercent, 100.0));
            }
            if (shareMemory && member.memTotalBytes > 0) {
                selfMemTotalBytes = member.memTotalBytes;
                selfMemUsedBytes =
                    qBound<qint64>(0, member.memUsedBytes,
                                   member.memTotalBytes);
            }
            if (shareDisk && member.diskTotalBytes > 0) {
                selfDiskTotalBytes = member.diskTotalBytes;
                selfDiskUsedBytes =
                    qBound<qint64>(0, member.diskUsedBytes,
                                   member.diskTotalBytes);
            }
            break;
        }
    }
    // Clone / website-serve tallies this node has accumulated for the repo. The
    // stored pair is (total served, clones); publish clones and website (browse)
    // serves separately so the Mirror nodes view can show each node's contribution
    // even while it's offline (the counters are otherwise purely local). Keyed the
    // same way onRequestServed writes them: catalogOwner()/raw repo name.
    const QPair<int, int> serveStats =
        m_repoStats.value(owner + "/" + repo.name);
    const int clonesServed = serveStats.second;
    const int websiteServed = qMax(0, serveStats.first - serveStats.second);
    const QString opaqueRepoId =
        repo.isPrivate
            ? privateOpaqueRepositoryId(repo.privateReplicaId)
            : QString();
    QString encryptedManifestSignature;
    if (repo.isPrivate) {
        const QByteArray manifestCanonical =
            QByteArrayLiteral("forkmesh-private-manifest-v1\n") +
            owner.toUtf8() + '\n' + name.toUtf8() + '\n' +
            opaqueRepoId.toUtf8() + '\n' +
            QByteArray::number(privateMetadata.keyEpoch) + '\n' +
            privateMetadata.ciphertextSha256.toUtf8() + '\n' +
            updatedAt.toUtf8();
        encryptedManifestSignature =
            m_profileIdentity.signData(manifestCanonical);
        if (opaqueRepoId.size() != 32 ||
            encryptedManifestSignature.isEmpty()) {
            const QString message = QStringLiteral(
                "Private publication is blocked because its encrypted manifest "
                "could not be signed locally.");
            logSystem(QStringLiteral("Private mirror: ") + message);
            if (showDialogOnError)
                flashMessage(message, true);
            return;
        }
    }
    const qint64 reportedMirrorBytes =
        repo.isPrivate
            ? QFileInfo(
                  QDir(privateReplicaRoot())
                      .filePath(repo.privateReplicaId +
                                QStringLiteral(".fm-private")))
                  .size()
            : mirrorRepoSizeBytes(repo.mirrorPath);
    QJsonObject metadata{{"owner", owner},
                         {"name", name},
                         {"mirrorEncryption",
                          repo.isPrivate
                              ? QStringLiteral("owner-sealed-v1")
                              : QString()},
                         {"opaqueRepoId", opaqueRepoId},
                         {"keyEpoch",
                          repo.isPrivate
                              ? double(privateMetadata.keyEpoch)
                              : 0},
                         {"encryptedManifestHash",
                          repo.isPrivate
                              ? privateMetadata.ciphertextSha256
                              : QString()},
                         {"encryptedManifestSig",
                          encryptedManifestSignature},
                         {"ownerUser", nodeOwnerDisplayName()},
                         // This machine's node name (node/machineName), so
                         // public mirror views can label the hardware
                         // distinctly from the owning account.
                         {"machineName", machineNodeName()},
                         {"commit", headCommit},
                         {"branch", headBranch},
                         {"commitSubject", headIdentity.subject},
                         {"commitAuthorName", headIdentity.author},
                         {"commitAt",
                          headIdentity.committedAtMs > 0
                              ? QString::number(headIdentity.committedAtMs)
                              : QString()},
                         {"issueCount", QString::number(issueCount)},
                         {"issueMaxNumber", QString::number(issueMaxNumber)},
                         {"commitCount", QString::number(commitCount)},
                         {"branchCount", QString::number(branchCount)},
                         {"pullCount", QString::number(pullCount)},
                         {"discussionCount", QString::number(discussionCount)},
                         {"activityWeeks", activityWeeks},
                         {"worktreeCount", QString::number(worktreeCount)},
                         {"artifactCount", QString::number(artifactCount)},
                         {"platform", selfPlatform},
                         // This signed capability separates an attended local
                         // Qt app from an unattended mirror. The Worker uses it
                         // when a platform administrator explicitly routes a
                         // web-created agent job to their own running desktop.
                         {"runtimeMode",
                          m_headless ? QStringLiteral("headless")
                                     : QStringLiteral("desktop")},
                         {"version", selfVersion},
                         {"nodeId", selfNodeId},
                         {"clonesServed", QString::number(clonesServed)},
                         {"websiteServed", QString::number(websiteServed)},
                         {"description", publishedDescription},
                         {"logoMetadata", logoMetadata},
                         {"website", publishedWebsite},
                         {"cloneUrl",
                          repo.isPrivate ? QString() : repo.cloneUrl},
                         {"solana", repo.solanaAddress},
                         {"channel", repositoryChannel(repo)},
                         {"hostedSince", QString::number(repo.hostedSinceMs)},
                         {"lastSync", QString::number(repo.lastSyncMs)},
                         {"updatedAt", updatedAt},
                         {"rootCommit", rootCommit},
                         // On-disk mirror size so the network page can show how
                         // much data each owner/repo is hosting. Not signed.
                         {"sizeBytes",
                          QString::number(qMax<qint64>(
                              0, reportedMirrorBytes))},
                         {"visibility", repo.isPrivate
                                            ? QStringLiteral("private")
                                            : QStringLiteral("public")},
                         {"source",
                          repo.isPrivate
                              ? QStringLiteral("owner-sealed-opaque")
                              : (repo.localPath.trimmed().isEmpty()
                                     ? QStringLiteral("remote-clone")
                                     : QStringLiteral("local-node"))},
                         {"maintainer", m_profileIdentity.publicKey()}};
    // Advertise only binaries this node can actually resolve. The complete
    // normalized record is catalog-v2 signed below, making this a bounded
    // capability lease for Worker-side agent routing rather than a relay hint.
    const QStringList agentSearchPaths{
        QDir::homePath() + QStringLiteral("/.local/bin"),
        QDir::homePath() + QStringLiteral("/.claude/bin"),
        QDir::homePath() + QStringLiteral("/.codex/bin"),
    };
    QJsonArray agentProviders;
    if (!QStandardPaths::findExecutable(
             QStringLiteral("claude"), agentSearchPaths).isEmpty() ||
        !QStandardPaths::findExecutable(QStringLiteral("claude")).isEmpty()) {
        agentProviders.append(QStringLiteral("claude-code"));
    }
    if (!QStandardPaths::findExecutable(
             QStringLiteral("codex"), agentSearchPaths).isEmpty() ||
        !QStandardPaths::findExecutable(QStringLiteral("codex")).isEmpty()) {
        agentProviders.append(QStringLiteral("codex"));
    }
    if (!agentProviders.isEmpty())
        metadata.insert(QStringLiteral("agentProviders"), agentProviders);
    // These values come from our self roster entry, which ServerNode populates
    // only for the per-metric telemetry toggles the operator enabled. Do not
    // insert disabled/unknown metrics: safe_catalog_record normalizes them to
    // null, preserving "not shared" through the signed catalog and public
    // mirror/World views.
    if (selfCpuPercent >= 0)
        metadata.insert(QStringLiteral("cpuPercent"), selfCpuPercent);
    if (selfMemTotalBytes > 0) {
        metadata.insert(QStringLiteral("memUsedBytes"),
                        double(selfMemUsedBytes));
        metadata.insert(QStringLiteral("memTotalBytes"),
                        double(selfMemTotalBytes));
    }
    if (selfDiskTotalBytes > 0) {
        metadata.insert(QStringLiteral("diskUsedBytes"),
                        double(selfDiskUsedBytes));
        metadata.insert(QStringLiteral("diskTotalBytes"),
                        double(selfDiskTotalBytes));
    }
    for (auto it = contributionFields.constBegin();
         it != contributionFields.constEnd(); ++it) {
        metadata.insert(it.key(), it.value());
    }
    if (!repo.isPrivate && !changedFiles.isEmpty()) {
        metadata.insert(
            QStringLiteral("changedFiles"),
            QJsonArray::fromStringList(changedFiles.mid(0, 8)));
    }
    const bool contributionSubmitted =
        metadata.contains(QStringLiteral("contributionPayload")) &&
        metadata.contains(QStringLiteral("contributionSig"));
    metadata.insert("signature", m_profileIdentity.signJson(metadata));
    // Attest the served refs so the relay can detect a tampered/stale mirror.
    // Signed with the same account key the relay verifies for the catalog write.
    if (!stateHash.isEmpty()) {
        metadata.insert("stateHash", stateHash);
        const QByteArray stateCanonical =
            ("forkmesh-repostate-v1\n" + owner + "\n" + name + "\n" + stateHash +
             "\n" + updatedAt)
                .toUtf8();
        metadata.insert("stateSig", m_profileIdentity.signData(stateCanonical));
    }
    // Catalog-v2 binds the complete normalized record for both public and
    // private publications. In particular, visibility, encryption metadata,
    // served-ref state, and the bounded native-logo factors cannot be altered
    // independently by a relay or replay holder.
    QString canonicalError;
    const QJsonObject normalized = normalizedCatalogV2Record(metadata);
    const QByteArray catalogCanonical =
        forkmesh::control::catalogV2SigningPayload(
            normalized, &canonicalError);
    const QString catalogSignature =
        catalogCanonical.isEmpty()
            ? QString()
            : m_profileIdentity.signData(catalogCanonical);
    if (catalogSignature.isEmpty()) {
        const QString message = QStringLiteral(
            "Repository publication is blocked because the complete catalog-v2 "
            "record could not be signed.");
        logSystem(QStringLiteral("Catalog: ") + message);
        if (showDialogOnError)
            flashMessage(message, true);
        return;
    }
    metadata.insert(QStringLiteral("catalogSigVersion"), 2);
    metadata.insert(QStringLiteral("catalogSig"), catalogSignature);
    // Tell the relay whether a person asked for this publish or a heartbeat
    // did. A repository the owner deleted from the website is tombstoned
    // there, and only a user-initiated publish lifts that tombstone — an
    // automatic republish must not quietly resurrect the deleted repo
    // (adhoc #91). Two things count as a person asking: a manual "Publish"
    // (showDialogOnError), and serving flipping off->on for this repo, which
    // only ever happens when the user adds/forks it or turns sharing back on.
    const bool wasServing =
        m_catalogPublishServeState.value(publishKey, false);
    const bool userIntent =
        showDialogOnError || (repo.publishToNetwork && !wasServing);
    m_catalogPublishServeState.insert(publishKey, repo.publishToNetwork);
    metadata.insert(QStringLiteral("publishIntent"),
                    userIntent ? QStringLiteral("user")
                               : QStringLiteral("auto"));

    // Skip the network write when nothing the catalog shows has changed since
    // the last successful publish. Roster presence flickers re-request a
    // publish continuously (setRoster -> syncMirrorsBehindRoster -> publish),
    // which used to re-POST an identical record every ~30s per repo. The
    // fingerprint covers everything readers see — refs (stateHash), counters,
    // description, visibility — and excludes only the per-attempt volatile
    // fields; a manual "Publish" (showDialogOnError) always goes through, and
    // an unchanged record still refreshes every kCatalogPublishRefreshMs.
    QJsonObject fingerprintSource = metadata;
    fingerprintSource.remove(QStringLiteral("updatedAt"));
    fingerprintSource.remove(QStringLiteral("lastSync"));
    fingerprintSource.remove(QStringLiteral("signature"));
    fingerprintSource.remove(QStringLiteral("catalogSig"));
    fingerprintSource.remove(QStringLiteral("stateSig"));
    fingerprintSource.remove(QStringLiteral("publishIntent"));
    const QByteArray fingerprint = QCryptographicHash::hash(
        QJsonDocument(fingerprintSource).toJson(QJsonDocument::Compact),
        QCryptographicHash::Sha256);
    const qint64 lastPublishedAt =
        m_catalogPublishedFingerprintAtMs.value(publishKey, 0);
    if (!showDialogOnError &&
        m_catalogPublishedFingerprint.value(publishKey) == fingerprint &&
        now - lastPublishedAt < kCatalogPublishRefreshMs) {
        return;
    }

    QNetworkRequest request(catalogApiUrl());
    request.setHeader(QNetworkRequest::ContentTypeHeader,
                      QStringLiteral("application/json"));
    request.setRawHeader("Accept", "application/json");
    m_catalogPublishInFlight.insert(publishKey);
    m_catalogPublishOwnerLastAttemptMs.insert(
        owner, QDateTime::currentMSecsSinceEpoch());
    QNetworkReply *reply =
        m_networkAccess->post(request, QJsonDocument(metadata).toJson(QJsonDocument::Compact));
    logSystem("Catalog: publishing " + repo.owner + "/" + repo.name + " to " +
              request.url().toString() + ".");

    connect(reply, &QNetworkReply::finished, this,
            [this, reply, index, publishKey, showDialogOnError, fingerprint,
             contributionSnapshotKey, contributionSubmitted] {
                const QByteArray body = reply->readAll();
                const int status =
                    reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
                const QNetworkReply::NetworkError error = reply->error();
                const int priorFailures =
                    m_catalogPublishConsecutiveFailures.value(publishKey, 0);
                const qint64 retryDelayMs =
                    catalogPublishRetryDelayMs(reply, status, priorFailures);
                reply->deleteLater();
                m_catalogPublishInFlight.remove(publishKey);
                const bool publishQueued =
                    m_catalogPublishQueued.contains(publishKey);
                m_catalogPublishQueued.remove(publishKey);
                const bool queuedDialog =
                    m_catalogPublishDialogQueued.contains(publishKey);
                m_catalogPublishDialogQueued.remove(publishKey);

                auto publishQueuedUpdate = [this, publishKey, publishQueued,
                                            queuedDialog](qint64 minDelayMs = 0) {
                    if (publishQueued)
                        scheduleCatalogPublish(publishKey, queuedDialog, minDelayMs);
                };

                if (index < 0 || index >= m_repositories.size()) {
                    publishQueuedUpdate();
                    return;
                }

                RepositoryRecord &repo = m_repositories[index];
                const QJsonObject responseObject =
                    QJsonDocument::fromJson(body).object();
                const QString responseCode =
                    responseObject.value(QStringLiteral("error")).toString();
                // The owner deleted this repository from the website. This
                // node still holds the mirror, so without acting on the
                // refusal it would keep republishing and the delete would
                // never stick (adhoc #91). Stop sharing it, keep the local
                // mirror, and say so once — turning sharing back on in repo
                // settings republishes it as a deliberate user action.
                if (status == 410 &&
                    responseCode == QLatin1String("repository_deleted")) {
                    m_catalogPublishedFingerprint.remove(publishKey);
                    m_catalogPublishedFingerprintAtMs.remove(publishKey);
                    m_catalogPublishConsecutiveFailures.remove(publishKey);
                    m_catalogPublishServeState.insert(publishKey, false);
                    const QString label = repo.owner + "/" + repo.name;
                    if (repo.publishToNetwork) {
                        repo.publishToNetwork = false;
                        saveRepositories();
                        refreshRepositoryList();
                    }
                    const QString message =
                        label +
                        " was deleted on forkmesh.com, so this node stopped "
                        "sharing it. The local mirror is untouched — turn "
                        "sharing back on in repo settings to publish it again.";
                    logSystem(QStringLiteral("Catalog: ") + message);
                    if (showDialogOnError || queuedDialog)
                        flashMessage(message, /*error=*/true);
                    return;
                }
                if (status == 409 &&
                    responseCode == QLatin1String("stale_update") &&
                    !contributionSnapshotKey.isEmpty()) {
                    m_contributionPublicationCache.invalidate(
                        contributionSnapshotKey);
                    if (m_catalogContributionSnapshotKey.value(publishKey) ==
                        contributionSnapshotKey) {
                        m_catalogContributionSnapshotKey.remove(publishKey);
                    }
                    const bool retry =
                        m_contributionPublicationCache.claimStaleRetry(
                            publishKey, contributionSnapshotKey);
                    const bool wantsDialog =
                        showDialogOnError || queuedDialog;
                    if (retry) {
                        logSystem(
                            "Catalog: stale contribution timestamp for " +
                            repo.owner + "/" + repo.name +
                            "; rebuilding one fresh snapshot.");
                        scheduleCatalogPublish(publishKey, wantsDialog);
                    } else {
                        const QString message =
                            "Catalog publish stopped for " + repo.owner + "/" +
                            repo.name +
                            ": the relay still reports stale_update after one "
                            "fresh snapshot retry.";
                        logSystem(message);
                        if (wantsDialog)
                            flashMessage(message, /*error=*/true);
                        publishQueuedUpdate();
                    }
                    return;
                }
                if (error == QNetworkReply::NoError && status >= 200 && status < 300) {
                    if (repoContributionResponseNeedsRefresh(
                            responseObject, contributionSubmitted)) {
                        m_contributionPublicationCache.invalidate(
                            contributionSnapshotKey);
                        if (m_catalogContributionSnapshotKey.value(publishKey) ==
                            contributionSnapshotKey) {
                            m_catalogContributionSnapshotKey.remove(publishKey);
                        }
                        const bool retry =
                            m_contributionPublicationCache.claimStaleRetry(
                                publishKey, contributionSnapshotKey);
                        const QString warning =
                            responseObject
                                .value(QStringLiteral("contributionWarning"))
                                .toString()
                                .simplified()
                                .left(240);
                        const bool wantsDialog =
                            showDialogOnError || queuedDialog;
                        if (retry) {
                            logSystem(
                                "Catalog: contribution was rejected for " +
                                repo.owner + "/" + repo.name +
                                "; rebuilding one fresh snapshot" +
                                (warning.isEmpty()
                                     ? QStringLiteral(".")
                                     : QStringLiteral(": ") + warning));
                            scheduleCatalogPublish(publishKey, wantsDialog);
                        } else {
                            const QString message =
                                "Catalog contribution stopped for " +
                                repo.owner + "/" + repo.name +
                                " after one fresh snapshot retry" +
                                (warning.isEmpty()
                                     ? QStringLiteral(".")
                                     : QStringLiteral(": ") + warning);
                            logSystem(message);
                            if (wantsDialog)
                                flashMessage(message, /*error=*/true);
                            publishQueuedUpdate();
                        }
                        return;
                    }
                    m_contributionPublicationCache.clearStaleRetry(publishKey);
                    m_catalogPublishConsecutiveFailures.remove(publishKey);
                    m_catalogPublishedFingerprint.insert(publishKey, fingerprint);
                    m_catalogPublishedFingerprintAtMs.insert(
                        publishKey, QDateTime::currentMSecsSinceEpoch());
                    repo.publishToNetwork = true;
                    repo.publishedAtMs = QDateTime::currentMSecsSinceEpoch();
                    saveRepositories();
                    refreshRepositoryList();
                    logSystem("Catalog: published " + repo.owner + "/" +
                              repo.name + " to forkmesh.com.");
                    if (showDialogOnError)
                        flashMessage("Published " + repo.owner + "/" + repo.name +
                                     " to forkmesh.com.");
                    // The pin now reflects our served refs again; clear any stale
                    // "clones are being rejected" banner for the open repo.
                    if (index == m_repoDetailIndex)
                        refreshRepoPinBanner();
                    if (repo.isPrivate)
                        registerPrivateReplicaRoute(index);
                    publishQueuedUpdate();
                    return;
                }

                const QString contentType =
                    reply->header(QNetworkRequest::ContentTypeHeader).toString();
                QString detail = QString::fromUtf8(body).trimmed();
                if (contentType.contains("text/html", Qt::CaseInsensitive) ||
                    detail.startsWith("<!doctype", Qt::CaseInsensitive) ||
                    detail.startsWith("<html", Qt::CaseInsensitive)) {
                    detail = status == 429 ? "rate limited" : "unexpected HTML response";
                } else {
                    detail = detail.left(500);
                }
                if (detail.isEmpty() && error != QNetworkReply::NoError)
                    detail = reply->errorString();
                const QString message =
                    "Catalog publish failed for " + repo.owner + "/" + repo.name +
                    (status > 0 ? " (HTTP " + QString::number(status) + ")" :
                                  QString()) +
                    (detail.isEmpty() ? QString() : ": " + detail);
                logSystem(message);
                if (showDialogOnError)
                    flashMessage(message, /*error=*/true);
                // Track consecutive retryable failures so the next retry backs
                // off further; a terminal (non-retryable) error clears the run
                // so a later transient failure starts from the base delay again.
                if (retryDelayMs > 0)
                    m_catalogPublishConsecutiveFailures.insert(
                        publishKey, priorFailures + 1);
                else
                    m_catalogPublishConsecutiveFailures.remove(publishKey);
                // A retryable failure (429/5xx -> retryDelayMs > 0) must
                // self-retry even when no follow-up edit queued another
                // publish: otherwise a lone failed publish silently waits
                // for the next unrelated change (or the 6h refresh).
                if (retryDelayMs > 0 && !publishQueued) {
                    scheduleCatalogPublish(publishKey,
                                           /*showDialogOnError=*/false,
                                           retryDelayMs);
                    return;
                }
                publishQueuedUpdate(retryDelayMs);
            });
}

namespace {

// A cheap digest of all refs in a bare mirror, so an automatic fetch can tell
// whether the owner's repo actually changed before announcing/republishing.
QString mirrorRefsDigest(const QString &mirrorPath)
{
    if (!QDir(mirrorPath).exists())
        return QString();
    QProcess p;
    p.start("git", {"-C", mirrorPath, "for-each-ref",
                    "--format=%(objectname) %(refname)"});
    if (!p.waitForFinished(5000))
        return QString();
    return QString::fromUtf8(p.readAllStandardOutput());
}

void repairMirrorHead(const QString &mirrorPath, const QString &sourcePath)
{
    QString preferred;
    if (QDir(sourcePath).exists(QStringLiteral(".git"))) {
        QProcess source;
        source.start("git", {"-C", sourcePath, "symbolic-ref", "--short", "HEAD"});
        if (source.waitForFinished(5000) && source.exitCode() == 0)
            preferred = QString::fromUtf8(source.readAllStandardOutput()).trimmed();
    }

    const QString current = mirrorHeadBranch(mirrorPath);
    QStringList candidates{preferred, QStringLiteral("main"),
                           QStringLiteral("master"), current};
    QString branch;
    for (const QString &candidate : std::as_const(candidates)) {
        if (!candidate.isEmpty() &&
            !mirrorBranchCommit(mirrorPath, candidate).isEmpty()) {
            branch = candidate;
            break;
        }
    }
    if (branch.isEmpty()) {
        QProcess refs;
        refs.start("git", {"-C", mirrorPath, "for-each-ref",
                           "--format=%(refname:short)", "--sort=-committerdate",
                           "--count=1", "refs/heads/"});
        if (refs.waitForFinished(5000) && refs.exitCode() == 0)
            branch = QString::fromUtf8(refs.readAllStandardOutput()).trimmed();
    }
    if (branch.isEmpty() || branch == current)
        return;

    QProcess setHead;
    setHead.start("git", {"-C", mirrorPath, "symbolic-ref", "HEAD",
                          "refs/heads/" + branch});
    setHead.waitForFinished(5000);
}

// Whether a failed mirror clone/fetch is a momentary host/relay hiccup that the
// next auto-sync will simply retry, rather than a real, persistent problem. Two
// families qualify: connectivity failures (HTTP 5xx, resets, DNS) and — the case
// that surfaced on fresh installs cloning a large repo — a truncated pack, where
// the direct HTTPS transfer gets cut mid-response and git reports "unexpected
// disconnect while reading sideband packet" / "early EOF" / "fetch-pack: invalid
// index-pack output". A partial clone leaves no mirror behind, so autoSyncMirrors
// re-attempts it; classifying it transient keeps that self-healing quiet instead
// of raising a scary permanent red error over what a retry fixes.
bool isTransientSyncError(const QString &errors)
{
    return errors.contains(QStringLiteral("HTTP 50")) ||
           errors.contains(QStringLiteral("RPC failed")) ||
           errors.contains(QStringLiteral("curl 22")) ||
           errors.contains(QStringLiteral("502")) ||
           errors.contains(QStringLiteral("503")) ||
           errors.contains(QStringLiteral("504")) ||
           errors.contains(QStringLiteral("Could not resolve"),
                           Qt::CaseInsensitive) ||
           errors.contains(QStringLiteral("Couldn't connect"),
                           Qt::CaseInsensitive) ||
           errors.contains(QStringLiteral("Connection reset"),
                           Qt::CaseInsensitive) ||
           // Truncated pack over the direct HTTPS clone response.
           errors.contains(QStringLiteral("early EOF"), Qt::CaseInsensitive) ||
           errors.contains(QStringLiteral("unexpected disconnect"),
                           Qt::CaseInsensitive) ||
           errors.contains(QStringLiteral("sideband"), Qt::CaseInsensitive) ||
           errors.contains(QStringLiteral("index-pack"), Qt::CaseInsensitive) ||
           errors.contains(QStringLiteral("fetch-pack"), Qt::CaseInsensitive) ||
           errors.contains(QStringLiteral("remote end hung up"),
                           Qt::CaseInsensitive);
}

} // namespace

void MainWindow::autoSyncMirrorsIfRelayHealthy()
{
    // Periodic safety-net path only — the explicit "sync now" action
    // (headlessSyncNow) calls autoSyncMirrors() directly and stays ungated.
    // The git fetch/clone subprocesses below never pass through
    // BackoffNetworkAccessManager, so honour its host-wide 429/5xx cooldown
    // here: when the relay's daily Cloudflare quota is already exhausted,
    // a fleet-wide fetch round would only burn more of the missing budget.
    const auto *network =
        qobject_cast<BackoffNetworkAccessManager *>(m_networkAccess);
    const QString relayHost = catalogApiUrl().host();
    if (network && !relayHost.isEmpty() && network->hostInCooldown(relayHost)) {
        logSystem(QStringLiteral("Mirror: skipping periodic auto-sync — relay "
                                 "%1 is in rate-limit cooldown.")
                      .arg(relayHost));
        return;
    }
    autoSyncMirrors();
}

void MainWindow::autoSyncMirrors()
{
    // Retry the flagship-repo bootstrap here too, not just the one-shot timer
    // shortly after launch: if the catalog wasn't reachable yet at that single
    // attempt (network still coming up right after a fresh install, relay
    // momentarily down), a long-running node — especially a headless daemon
    // that rarely restarts — would otherwise never end up mirroring the
    // project repo or joining the mirror network until its next relaunch.
    // ensureFlagshipRepo() is idempotent (no-op once the repo is present).
    ensureFlagshipRepo();

    // Quietly refresh every repo's mirror so it tracks the owner's repo.
    for (int i = 0; i < m_repositories.size(); ++i) {
        if (!m_syncingRepos.contains(i) &&
            !m_repositories.at(i).previewOnly &&
            !repositorySource(m_repositories.at(i)).isEmpty())
            syncRepository(i, /*quiet=*/true);
    }

    // While we're online, keep every repo we're the source of truth for pinned to
    // the refs it actually serves. Runs on the same cadence as the mirror sync so
    // a source repo whose pin drifted (and whose detail the owner never opened)
    // heals on its own instead of leaving clones rejected with a failing integrity
    // pin until a manual reset.
    reattestStalePins();
}

// True when the bare mirror already contains `commit`. Presence is effectively
// monotonic for advertised branch tips (a fetched commit stays reachable for
// the life of the session), so positive answers are memoized: the roster
// reconcile below re-asked git about the same converged tip on every peer
// hello — 100+ foreground `git cat-file -e` spawns per session on the GUI
// thread (adhoc #82). Negative answers are never cached; the next probe after
// a fetch flips to (memoized) present.
bool MainWindow::mirrorHasCommit(const QString &mirrorPath, const QString &commit)
{
    const QString key = mirrorPath + QLatin1Char('\x1f') + commit;
    if (m_mirrorCommitsPresent.contains(key))
        return true;
    if (!runGitCapture(mirrorPath,
                       {QStringLiteral("cat-file"), QStringLiteral("-e"),
                        commit + QStringLiteral("^{commit}")},
                       nullptr, nullptr))
        return false;
    m_mirrorCommitsPresent.insert(key);
    return true;
}

void MainWindow::syncMirrorsBehindRoster()
{
    // A peer just (re-)advertised its mirror set via hello. For every repo we
    // mirror, if any online peer advertises a commit our bare mirror does not
    // contain, pull it now rather than waiting for the three-minute auto-sync.
    // This backstops notifyMirrorUpdated (which is ephemeral and missed if we
    // were offline/just connected): the moment the roster shows the source
    // moved, we converge. syncRepository fetches refs/heads/* + refs/tags/*,
    // so issue/PR and commit-comment changes (which live on refs/heads) come
    // along too.
    // Release artifact blobs are advertised separately from git refs; if a peer
    // has more CAS blobs than we do, pull those bytes even when the git mirror is
    // already current.
    for (int i = 0; i < m_repositories.size(); ++i) {
        const RepositoryRecord &repo = m_repositories.at(i);
        if (repo.previewOnly || m_syncingRepos.contains(i))
            continue;
        if (repositorySource(repo).isEmpty())
            continue; // we are the source — nothing upstream to pull
        if (repo.mirrorPath.trimmed().isEmpty() || !QDir(repo.mirrorPath).exists())
            continue; // no local mirror yet; the periodic clone handles the first
        // Group every node's mirror of this repo by its shared upstream identity
        // (with a clone-name / legacy fallback for older peers), exactly as
        // loadMirrorNodesPanel does.
        const QString canonical =
            catalogOwner(repo) + "/" +
            repoSegment(repo.name, QStringLiteral("repository"));
        const QString source = repoSegment(repo.owner, QStringLiteral("owner")) +
                               "/" + repoSegment(repo.name, QStringLiteral("repository"));
        const QString legacy = repo.owner + "/" + repo.name;
        const int localArtifactCount = mirrorArtifactCount(repo.mirrorPath);
        bool behind = false;
        bool artifactsBehind = false;
        for (const MemberInfo &node : std::as_const(m_homeRoster)) {
            if (node.self || !node.online)
                continue;
            for (const MirrorAdvert &m : node.mirrorDetails) {
                if (m.source != source && m.ownerName != canonical &&
                    m.ownerName != legacy)
                    continue;
                // A peer advertises a commit our mirror lacks → we are behind.
                if (!m.commit.isEmpty() &&
                    !mirrorHasCommit(repo.mirrorPath, m.commit))
                    behind = true;
                if (m.artifactCount > localArtifactCount)
                    artifactsBehind = true;
                break; // one advert per node for this repo
            }
            if (behind || artifactsBehind)
                break;
        }
        if (behind)
            syncRepository(i, /*quiet=*/true);
        else if (artifactsBehind)
            replicateReleaseArtifacts(i);
    }
}

void MainWindow::propagateRepoUpdate(int index)
{
    if (index < 0 || index >= m_repositories.size())
        return;
    const RepositoryRecord &repo = m_repositories.at(index);
    // Only the node holding the working copy is the source of truth that can
    // push its mirror forward; previews and pure mirrors just pull.
    if (repo.previewOnly || repo.localPath.trimmed().isEmpty())
        return;
    if (m_syncingRepos.contains(index))
        return;
    // Repaint the sync indicators (rail spinner, "waiting to sync" commit markers)
    // right away rather than waiting on the mirror-fetch round-trip below (prep
    // thread + fetch subprocess + housekeeping thread) to reach
    // refreshRepositoryList's refreshRepoSyncIndicators() call: that left them
    // visibly lagging the "N commits not yet synced" banner, which loadCommits()
    // already paints synchronously the instant a commit lands.
    if (index == m_repoDetailIndex)
        refreshRepoSyncIndicators();
    // syncRepository fetches the bare mirror from the local working copy, so the
    // just-committed issue/PR lands in the mirror. On a detected change it
    // refreshes the open detail (updating the Issues/PR counts) and broadcasts
    // notifyMirrorUpdated, which mirroring peers act on via onPeerMirrorUpdated —
    // converging everyone in seconds rather than at the next three-minute tick.
    syncRepository(index, /*quiet=*/true);
    // The mirror fetch above is asynchronous; until it finishes our working copy
    // is ahead of the bare mirror we serve. Refresh the Mirror nodes panel now so
    // it surfaces the pending "↑N to push" state the instant the comment/commit
    // lands, rather than only after the fetch completes — but only while that panel
    // is actually on screen. It shells several synchronous git reads (mirror
    // HEAD/commit/size, issue count, the ahead-count walk) that would lag the commit
    // for nothing when the user is on another tab; switching to the tab reloads it
    // (see the repo-detail tab handler), so a hidden panel stays correct.
    if (index == m_repoDetailIndex && m_mirrorNodesTable &&
        m_mirrorNodesTable->isVisible())
        loadMirrorNodesPanel();
}

void MainWindow::onPeerMirrorUpdated(const QString &ownerName,
                                     const QString &peerName,
                                     const QString &commit)
{
    // Only surface it if we keep a real mirror of this repo (browse-only
    // previews don't count) — otherwise the peer's update isn't relevant here.
    // Match on both the local clone identity and the shared source identity. A
    // mirror installed under this node's account advertises "<this-node>/repo",
    // while the source-of-truth peer announces "<source-owner>/repo".
    int matchIndex = -1;
    for (int i = 0; i < m_repositories.size(); ++i) {
        const RepositoryRecord &repo = m_repositories.at(i);
        if (repo.previewOnly)
            continue;
        const QString canonical =
            catalogOwner(repo) + "/" +
            repoSegment(repo.name, QStringLiteral("repository"));
        const QString source =
            repoSegment(repo.owner, QStringLiteral("owner")) + "/" +
            repoSegment(repo.name, QStringLiteral("repository"));
        if (canonical == ownerName || source == ownerName ||
            (repo.owner + "/" + repo.name) == ownerName) {
            matchIndex = i;
            break;
        }
    }
    if (matchIndex < 0)
        return;

    const QString who =
        peerName.trimmed().isEmpty() ? QStringLiteral("A peer") : peerName.trimmed();
    const QString msg = who + " updated the mirror of " + ownerName +
                        " from its source.";
    logSystem(msg);
    flashMessage(msg);

    // The announcement named the peer's new HEAD; reflect it in the live roster
    // so the Mirror nodes panel shows the peer at that commit right away instead
    // of waiting for its next hello (the source of the >30s lag).
    applyPeerMirrorCommit(ownerName, peerName, commit);

    // The signal named the exact new commit. If our mirror already holds it we
    // are already converged — close the round trip instantly by reporting back
    // that we are up to date, with no redundant fetch.
    const QString target = commit.trimmed();
    const RepositoryRecord &matched = m_repositories.at(matchIndex);
    if (!target.isEmpty() && !matched.mirrorPath.trimmed().isEmpty() &&
        mirrorHasCommit(matched.mirrorPath, target)) {
        if (m_backend)
            m_backend->notifyMirrorSynced(ownerName, target);
        return;
    }

    // Converge promptly: pull the peer's advance into our own mirror now instead
    // of waiting for the next three-minute auto-sync. This fetches
    // refs/heads/* and refs/tags/*, so issues and pull requests (which live on
    // refs/heads) come along with the code. Quiet so it doesn't spam unless
    // something changed. The sync's completion broadcasts notifyMirrorSynced,
    // reporting back the moment the fetch lands the new commit.
    if (!m_syncingRepos.contains(matchIndex))
        syncRepository(matchIndex, /*quiet=*/true);
    if (notifyEnabled(kMirrorUpdateAlertSetting) && m_trayIcon &&
        QSystemTrayIcon::supportsMessages())
        m_trayIcon->showMessage("ForkMesh — mirror updated", msg,
                                QSystemTrayIcon::Information, 6000);
}

void MainWindow::onPeerMirrorSynced(const QString &ownerName,
                                    const QString &peerName,
                                    const QString &commit)
{
    // A peer reported it finished pulling a repo's mirror up to `commit` — the
    // closing half of the signal -> update -> done round trip. Only surface it
    // for a repo we actually hold (same match as onPeerMirrorUpdated), and keep
    // it to a quiet log line (no toast/tray) so a busy mesh's acknowledgements
    // don't spam the user.
    bool relevant = false;
    for (const RepositoryRecord &repo : std::as_const(m_repositories)) {
        if (repo.previewOnly)
            continue;
        const QString canonical =
            catalogOwner(repo) + "/" +
            repoSegment(repo.name, QStringLiteral("repository"));
        const QString source =
            repoSegment(repo.owner, QStringLiteral("owner")) + "/" +
            repoSegment(repo.name, QStringLiteral("repository"));
        if (canonical == ownerName || source == ownerName ||
            (repo.owner + "/" + repo.name) == ownerName) {
            relevant = true;
            break;
        }
    }
    if (!relevant)
        return;

    const QString who =
        peerName.trimmed().isEmpty() ? QStringLiteral("A peer") : peerName.trimmed();
    QString msg = who + " is up to date on the mirror of " + ownerName;
    if (!commit.trimmed().isEmpty())
        msg += " at " + commit.trimmed().left(10);
    msg += ".";
    logSystem(msg);

    // The ack named the exact commit the peer now holds. Reflect it in the live
    // roster right away so this node's Mirror nodes panel shows the peer as
    // converged the instant it reports back, rather than lagging until the
    // peer's next hello re-advertises the new HEAD.
    applyPeerMirrorCommit(ownerName, peerName, commit);
}

bool MainWindow::applyPeerMirrorCommit(const QString &ownerName,
                                       const QString &peerName,
                                       const QString &commit)
{
    const QString group = ownerName.trimmed();
    const QString target = commit.trimmed().left(64);
    const QString who = peerName.trimmed();
    if (group.isEmpty() || target.isEmpty() || who.isEmpty())
        return false;
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    bool changed = false;
    for (MemberInfo &node : m_homeRoster) {
        if (node.self)
            continue; // our own row reads the live local HEAD, never a peer ack
        // Identify the reporting peer by its advertised name (chat or node
        // account). A name we don't recognise leaves the roster untouched, so a
        // stray/misrouted ack can never mark the wrong node as converged.
        if (node.name.compare(who, Qt::CaseInsensitive) != 0 &&
            node.nodeName.compare(who, Qt::CaseInsensitive) != 0)
            continue;
        for (MirrorAdvert &m : node.mirrorDetails) {
            if (m.source.compare(group, Qt::CaseInsensitive) != 0 &&
                m.ownerName.compare(group, Qt::CaseInsensitive) != 0)
                continue;
            if (m.commit != target) {
                m.commit = target;
                if (m.updatedMs < now)
                    m.updatedMs = now;
                changed = true;
            }
        }
    }
    // Repaint the open Mirror nodes panel so the peer's row updates now. The
    // panel is rebuilt from m_homeRoster, so the patched advert flows straight
    // through; only reload while it is actually on screen (it shells several
    // synchronous git reads — see loadMirrorNodesPanel).
    if (changed && m_mirrorNodesTable && m_mirrorNodesTable->isVisible())
        loadMirrorNodesPanel();
    return changed;
}

void MainWindow::scanRepoMentionsFor(const RepositoryRecord &repo)
{
    // Only meaningful once we have a handle to match "@name" against.
    if (!isValidNodeName(accountNameFromInput(m_userName, QString())))
        return;
    if (repo.owner.isEmpty() || repo.name.isEmpty())
        return;

    const QString repoKey = repo.owner + "/" + repo.name;

    // Loading every issue and pull request thread off disk (each a
    // parse of many small files) is the heavy part: on a large repo — the flagship
    // forkmesh project runs to hundreds of issues/PRs — it froze the UI every time
    // a sync or inbox drain finished, which is what fired this scan. Do that I/O on
    // a worker thread, then match @mentions and raise notifications back on the main
    // thread. The stores are copied by value and only read on the worker (no event
    // signing), the same off-thread pattern deleteIssue uses for its git work.
    if (m_mentionScanInFlight.contains(repoKey))
        return; // a scan for this repo is already loading; don't double-notify
    m_mentionScanInFlight.insert(repoKey);

    auto loadedIssues = std::make_shared<QList<Issue>>();
    auto loadedPulls = std::make_shared<QList<PullRequest>>();
    IssueStore issueStore(repo.localPath, repo.mirrorPath, &m_profileIdentity,
                          m_userName);
    PullStore pullStore(repo.localPath, repo.mirrorPath, &m_profileIdentity,
                        m_userName);
    QThread *worker = QThread::create(
        [issueStore, pullStore, loadedIssues, loadedPulls]() mutable {
            *loadedIssues = issueStore.loadAll();
            *loadedPulls = pullStore.loadAll();
        });
    connect(worker, &QThread::finished, this,
            [this, worker, repo, repoKey, loadedIssues, loadedPulls]() {
                worker->deleteLater();
                m_mentionScanInFlight.remove(repoKey);
                applyRepoMentions(repo, *loadedIssues, *loadedPulls);
            });
    worker->start();
}

void MainWindow::applyRepoMentions(const RepositoryRecord &repo,
                                   const QList<Issue> &allIssues,
                                   const QList<PullRequest> &allPulls)
{
    const QString repoKey = repo.owner + "/" + repo.name;
    QSettings settings;
    const QStringList seenList =
        settings.value(QStringLiteral("mentions/seen")).toStringList();
    QSet<QString> seen(seenList.cbegin(), seenList.cend());
    QStringList seededRepos =
        settings.value(QStringLiteral("mentions/seededRepos")).toStringList();
    // The first time we scan a repo, silently record its existing mentions so a
    // fresh clone's back-history doesn't fire a flood of stale alerts; only
    // mentions that appear afterwards notify.
    const bool seeding = !seededRepos.contains(repoKey);
    const QString myKey = m_profileIdentity.publicKey();

    bool dirty = false;
    // Raise (and record) one mention alert. stableKey dedups across scans;
    // humanLocator is the "issue #12" / "PR #4" / "commit abc1234" phrase shown.
    auto notifyMention = [&](const QString &stableKey, const QString &authorKey,
                             const QString &authorName, const QString &text,
                             const QString &humanLocator,
                             const NotificationLink &link) {
        if (text.isEmpty() || !textMentionsNodeName(text, m_userName))
            return;
        if (!authorKey.isEmpty() && authorKey == myKey)
            return; // your own writing doesn't mention "you"
        if (seen.contains(stableKey))
            return;
        seen.insert(stableKey);
        dirty = true;
        if (seeding)
            return; // recorded, but no alert for pre-existing history
        const QString who = authorName.trimmed().isEmpty()
                                ? QStringLiteral("Someone")
                                : authorName.trimmed();
        QString snippet = text.simplified();
        if (snippet.size() > 160)
            snippet = snippet.left(157) + QString::fromUtf8("\xE2\x80\xA6");
        const QString body =
            QString::fromUtf8("%1 mentioned you in %2 %3: \xE2\x80\x9C%4\xE2\x80\x9D")
                .arg(who, repoKey, humanLocator, snippet);
        if (notifyEnabled(kMentionAlertSetting)) {
            QApplication::alert(this, 0);
            postNotification(who + QStringLiteral(" mentioned you"), body);
        }
        addNotification(QStringLiteral("Mention"), body, false, link);
    };
    // Issues/PRs: preserve the existing "<repo>#<kind><number>:<eventId>" dedup
    // key (so upgrades don't re-fire historical mentions) and "<kind> #<n>"
    // wording exactly.
    auto consider = [&](const QString &kind, int number, const QString &eventId,
                        const QString &authorKey, const QString &authorName,
                        const QString &text, const QString &context) {
        const QString key = QStringLiteral("%1#%2%3:%4")
                                .arg(repoKey, kind)
                                .arg(number)
                                .arg(eventId);
        NotificationLink link;
        link.kind = kind; // "issue" | "pull" — matches openNotificationLink
        link.owner = repo.owner;
        link.name = repo.name;
        link.number = number;
        notifyMention(key, authorKey, authorName, text,
                      context + QStringLiteral("#") + QString::number(number),
                      link);
    };

    for (const Issue &issue : allIssues) {
        for (const IssueEvent &ev : issue.events) {
            if (ev.type != QLatin1String("open") &&
                ev.type != QLatin1String("comment") &&
                ev.type != QLatin1String("edit"))
                continue;
            const QString text = (ev.title + QStringLiteral("\n") + ev.body).trimmed();
            consider(QStringLiteral("issue"), issue.number, ev.id, ev.author,
                     ev.authorName, text, QStringLiteral("issue "));
        }
    }

    for (const PullRequest &pr : allPulls) {
        const QString openText =
            (pr.title + QStringLiteral("\n") + pr.description).trimmed();
        consider(QStringLiteral("pull"), pr.number, QStringLiteral("open"), pr.author,
                 pr.authorName, openText, QStringLiteral("PR "));
        for (const PullEvent &ev : pr.events)
            consider(QStringLiteral("pull"), pr.number, ev.id, ev.author,
                     ev.authorName, ev.body, QStringLiteral("PR "));
    }

    if (dirty) {
        const QStringList keys(seen.cbegin(), seen.cend());
        settings.setValue(QStringLiteral("mentions/seen"), keys);
    }
    if (seeding) {
        seededRepos.append(repoKey);
        settings.setValue(QStringLiteral("mentions/seededRepos"), seededRepos);
    }
}

void MainWindow::syncPrivateRepository(int index, bool quiet)
{
    if (index < 0 || index >= m_repositories.size() ||
        m_syncingRepos.contains(index))
        return;
    const RepositoryRecord repo = m_repositories.at(index);
    const bool ownerCapable =
        hasOwnerSigningCapability(repo.owner);
    const QUrl sourceUrl(repo.cloneUrl.trimmed());
    const bool sourceIsForkMeshRoute =
        repo.localPath.trimmed().isEmpty() &&
        sourceUrl.isValid() && !sourceUrl.host().isEmpty() &&
        sourceUrl.host().compare(catalogApiUrl().host(),
                                Qt::CaseInsensitive) == 0;
    const bool hasPublicTransitionSource =
        PublicMirrorRuntime::isArchiveId(repo.publicArchiveId) &&
        QDir(repo.mirrorPath).exists();
    // Shared repositories—and an owner's route-backed copy without another
    // upstream—must use the authorized opaque archive endpoint. Private Git
    // smart HTTP intentionally returns 426 and never carries plaintext.
    if (!ownerCapable ||
        (sourceIsForkMeshRoute && !hasPublicTransitionSource)) {
        downloadPrivateReplica(index, quiet);
        return;
    }

    // Once a private catalog record exists, every content refresh first loads
    // the relay's exact public-only recipient set. Missing/malformed recipient
    // keys block rotation and publication instead of silently dropping or
    // retaining a collaborator.
    if (PrivateMirrorStore::isOpaqueId(repo.privateReplicaId) &&
        repo.publishToNetwork) {
        m_syncingRepos.insert(index, quiet);
        refreshRepositoryList();
        requestPrivateRecipientBundles(
            repo, /*updateCollaboratorList=*/false,
            [this, index, quiet](bool ready,
                                QList<QJsonObject> recipientBundles,
                                QStringList, QString error) {
                m_syncingRepos.remove(index);
                if (!ready) {
                    logSystem(QStringLiteral(
                        "Private mirror: recipient-key verification blocked "
                        "the refresh; the previous encrypted epoch remains "
                        "unchanged."));
                    if (!quiet)
                        flashMessage(
                            QStringLiteral(
                                "Private mirror refresh blocked: %1")
                                .arg(error),
                            true);
                    refreshRepositoryList();
                    return;
                }
                syncPrivateRepositoryWithRecipients(
                    index, quiet, recipientBundles);
            });
        return;
    }
    syncPrivateRepositoryWithRecipients(index, quiet, {});
}

void MainWindow::syncPrivateRepositoryWithRecipients(
    int index, bool quiet,
    const QList<QJsonObject> &recipientBundles)
{
    if (index < 0 || index >= m_repositories.size() ||
        m_syncingRepos.contains(index))
        return;
    if (!m_profileIdentity.isValid() && !m_profileIdentity.load()) {
        if (!quiet)
            flashMessage(
                QStringLiteral("The local owner identity is required before a "
                               "private mirror can be sealed."),
                true);
        return;
    }
    const QByteArray vaultSecret =
        privateIdentityVaultSecret(m_profileIdentity);
    if (vaultSecret.size() < 32) {
        if (!quiet)
            flashMessage(QStringLiteral(
                             "Could not unlock the owner-only private-mirror vault."),
                         true);
        return;
    }

    RepositoryRecord &repo = m_repositories[index];
    const QString existingOpaqueId = repo.privateReplicaId;
    QString runtimePath;
    const auto currentMaterialization =
        m_privateMirrorMaterializations.value(existingOpaqueId);
    if (currentMaterialization && currentMaterialization->isValid())
        runtimePath = currentMaterialization->repositoryPath();

    // A pre-encryption install may still have a persistent bare mirror under
    // the managed mirror root. It is a one-time migration source and is removed
    // only after both the encrypted replica and a fresh authenticated temporary
    // materialization have succeeded.
    QString legacyMirrorPath;
    if (!repo.mirrorPath.trimmed().isEmpty() &&
        repo.mirrorPath != runtimePath && QDir(repo.mirrorPath).exists()) {
        legacyMirrorPath = repo.mirrorPath;
    }

    QString source;
    if (!repo.localPath.trimmed().isEmpty() &&
        (QDir(repo.localPath).exists(QStringLiteral(".git")) ||
         QFileInfo(QDir(repo.localPath).filePath(QStringLiteral("HEAD")))
             .isFile())) {
        source = repo.localPath;
    } else if (!legacyMirrorPath.isEmpty()) {
        source = legacyMirrorPath;
    } else {
        source = repo.cloneUrl.trimmed();
    }
    QStringList authArgs = viewAuthGitArgs(repo, source);
    if (authArgs.isEmpty() &&
        QUrl(source).scheme().compare(QStringLiteral("https"),
                                     Qt::CaseInsensitive) == 0) {
        authArgs = importAuthGitArgs(source);
    }
    const QString replicaRoot = privateReplicaRoot();
    const QString vaultPath = privateIdentityVaultPath();
    const QString managedMirrorRoot = repositoryMirrorRoot();

    auto result = std::make_shared<PrivateSyncWorkerResult>();
    m_syncingRepos.insert(index, quiet);
    // Recompute publication state before encryption/migration; private
    // repository names remain absent from public presence and catalogs.
    startRepoHosts();
    refreshRepositoryList();
    if (!quiet)
        logSystem(QStringLiteral(
            "Private mirror: sealing an owner-only opaque replica."));

    QThread *worker = QThread::create(
        [result, source, authArgs, recipientBundles, replicaRoot, vaultPath,
         mutableVaultSecret = vaultSecret, existingOpaqueId,
         legacyMirrorPath, managedMirrorRoot]() mutable {
            if (!source.isEmpty()) {
                result->sync = PrivateMirrorRuntime::syncSource(
                    source, authArgs, replicaRoot, vaultPath,
                    mutableVaultSecret, existingOpaqueId,
                    recipientBundles, &result->error);
            }
            // Offline fallback: preserve the last authenticated Git archive but
            // still rotate it to the relay-verified exact recipient set. This
            // ensures a revocation is not postponed by an unavailable upstream.
            if (!result->sync.isValid() &&
                PrivateMirrorStore::isOpaqueId(existingOpaqueId)) {
                PrivateMirrorStore::Metadata metadata;
                QString resealError;
                if (PrivateMirrorRuntime::resealRecipients(
                        replicaRoot, vaultPath, mutableVaultSecret,
                        existingOpaqueId, recipientBundles, &metadata,
                        &resealError)) {
                    result->sync.opaqueId = existingOpaqueId;
                    result->sync.metadata = metadata;
                    result->error.clear();
                    result->notice = QStringLiteral(
                        "Source refresh was deferred; the last authenticated "
                        "archive was re-keyed to the verified recipient set.");
                }
            }
            if (result->sync.isValid()) {
                auto opened = PrivateMirrorRuntime::materialize(
                    replicaRoot, vaultPath, mutableVaultSecret,
                    result->sync.opaqueId, &result->error);
                if (opened)
                    result->materialization =
                        std::shared_ptr<PrivateMirrorMaterialization>(
                            std::move(opened));
            }
            if (result->sync.isValid() && result->materialization &&
                !legacyMirrorPath.isEmpty()) {
                const QString root =
                    QFileInfo(managedMirrorRoot).canonicalFilePath();
                const QString target =
                    QFileInfo(legacyMirrorPath).canonicalFilePath();
                if (!root.isEmpty() && !target.isEmpty() &&
                    target.startsWith(
                        QDir::cleanPath(root) + QLatin1Char('/'))) {
                    result->legacyRemoved =
                        PrivateMirrorRuntime::removeManagedPlaintextMirror(
                            legacyMirrorPath, managedMirrorRoot,
                            &result->error);
                }
            }
            mutableVaultSecret.fill('\0');
            mutableVaultSecret.clear();
        });
    connect(worker, &QThread::finished, this,
            [this, worker, result, index, quiet, legacyMirrorPath] {
                worker->deleteLater();
                m_syncingRepos.remove(index);
                if (index < 0 || index >= m_repositories.size()) {
                    refreshRepositoryList();
                    return;
                }
                if (!result->sync.isValid() || !result->materialization ||
                    !result->materialization->isValid() ||
                    !result->legacyRemoved) {
                    // Retain a legacy path in settings when cleanup failed so a
                    // later pass can retry; never silently declare a plaintext
                    // migration complete.
                    logSystem(QStringLiteral(
                                  "Private mirror: encryption/runtime setup failed: %1")
                                  .arg(result->error.isEmpty()
                                           ? QStringLiteral("unknown local error")
                                           : result->error.left(240)));
                    if (!quiet) {
                        flashMessage(
                            QStringLiteral(
                                "Private mirror was not activated: %1")
                                .arg(result->error.isEmpty()
                                         ? QStringLiteral(
                                               "could not authenticate the "
                                               "encrypted replica")
                                         : result->error.left(160)),
                            true);
                    }
                    refreshRepositoryList();
                    return;
                }

                RepositoryRecord &current = m_repositories[index];
                const QString previousPublicArchive =
                    current.publicArchiveId;
                current.privateReplicaId = result->sync.opaqueId;
                current.publicArchiveId.clear();
                adoptMaterializedMirror(
                    current, result->materialization->repositoryPath());
                current.lastSyncMs =
                    QDateTime::currentDateTimeUtc().toMSecsSinceEpoch();
                m_privateMirrorMaterializations.insert(
                    result->sync.opaqueId, result->materialization);
                if (PublicMirrorRuntime::isArchiveId(
                        previousPublicArchive)) {
                    m_publicMirrorMaterializations.remove(
                        previousPublicArchive);
                }
                saveRepositories();
                refreshRepositoryList();
                // Same as the public path: the materialization is temporary,
                // so the working copy's push URL/hook must track its path.
                ensurePushHook(current);
                if (index == m_repoDetailIndex)
                    refreshOpenRepoDetail();
                startRepoHosts(); // private repos remain categorically excluded
                logSystem(QStringLiteral(
                              "Private mirror: authenticated encrypted epoch %1 "
                              "is ready in owner-only temporary storage.")
                              .arg(result->sync.metadata.keyEpoch));
                if (!legacyMirrorPath.isEmpty()) {
                    logSystem(QStringLiteral(
                        "Private mirror: removed the migrated managed plaintext "
                        "bare replica."));
                }
                if (!result->notice.isEmpty())
                    logSystem(QStringLiteral("Private mirror: ") + result->notice);
                if (!quiet) {
                    flashMessage(
                        QStringLiteral(
                            "Private mirror sealed (encrypted epoch %1).")
                            .arg(result->sync.metadata.keyEpoch));
                }
                if (current.publishToNetwork) {
                    QString gatewayError;
                    if (rebuildDirectMirrorGatewayConfiguration(
                            &gatewayError, true)) {
                        publishRepository(index, false);
                    } else {
                        logSystem(
                            QStringLiteral(
                                "Private publication remains blocked until "
                                "the direct HTTPS gateway is configured: %1")
                                .arg(gatewayError));
                    }
                }
            });
    worker->start();
}

void MainWindow::resealPrivateRepositoryRecipients(
    const RepositoryRecord &repo,
    const QList<QJsonObject> &recipientBundles,
    std::function<void(bool, QString)> onDone)
{
    auto finish =
        [onDone = std::move(onDone)](
            bool ok, const QString &error = QString()) mutable {
            if (onDone)
                onDone(ok, error);
        };
    int index = -1;
    for (int candidate = 0; candidate < m_repositories.size();
         ++candidate) {
        const RepositoryRecord &current =
            m_repositories.at(candidate);
        if (current.isPrivate && current.owner == repo.owner &&
            current.name == repo.name &&
            current.privateReplicaId == repo.privateReplicaId) {
            index = candidate;
            break;
        }
    }
    if (index < 0 ||
        !PrivateMirrorStore::isOpaqueId(repo.privateReplicaId) ||
        m_syncingRepos.contains(index) ||
        !hasOwnerSigningCapability(repo.owner)) {
        finish(false,
               QStringLiteral(
                   "the local owner-sealed replica is unavailable"));
        return;
    }
    if (!m_profileIdentity.isValid() &&
        !m_profileIdentity.load()) {
        finish(false,
               QStringLiteral(
                   "the local owner identity is unavailable"));
        return;
    }
    QByteArray vaultSecret =
        privateIdentityVaultSecret(m_profileIdentity);
    if (vaultSecret.size() < 32) {
        finish(false,
               QStringLiteral(
                   "the local encryption identity vault is unavailable"));
        return;
    }

    const QString replicaRoot = privateReplicaRoot();
    const QString vaultPath = privateIdentityVaultPath();
    const QString opaqueId = repo.privateReplicaId;
    auto result = std::make_shared<PrivateDownloadWorkerResult>();
    m_syncingRepos.insert(index, false);
    refreshRepositoryList();
    QThread *worker = QThread::create(
        [result, replicaRoot, vaultPath, opaqueId, recipientBundles,
         mutableVaultSecret = std::move(vaultSecret)]() mutable {
            if (PrivateMirrorRuntime::resealRecipients(
                    replicaRoot, vaultPath, mutableVaultSecret,
                    opaqueId, recipientBundles, &result->metadata,
                    &result->error)) {
                result->opaqueId = opaqueId;
                auto opened = PrivateMirrorRuntime::materialize(
                    replicaRoot, vaultPath, mutableVaultSecret,
                    opaqueId, &result->error);
                if (opened) {
                    result->materialization =
                        std::shared_ptr<PrivateMirrorMaterialization>(
                            std::move(opened));
                }
            }
            mutableVaultSecret.fill('\0');
            mutableVaultSecret.clear();
        });
    connect(
        worker, &QThread::finished, this,
        [this, worker, result, index, opaqueId,
         finish = std::move(finish)]() mutable {
            worker->deleteLater();
            m_syncingRepos.remove(index);
            if (index < 0 || index >= m_repositories.size() ||
                m_repositories.at(index).privateReplicaId !=
                    opaqueId ||
                result->opaqueId != opaqueId ||
                !result->materialization ||
                !result->materialization->isValid()) {
                refreshRepositoryList();
                finish(
                    false,
                    QStringLiteral(
                        "the encrypted replica could not be authenticated "
                        "after rotation"));
                return;
            }
            RepositoryRecord &current = m_repositories[index];
            adoptMaterializedMirror(
                current, result->materialization->repositoryPath());
            current.lastSyncMs =
                QDateTime::currentDateTimeUtc().toMSecsSinceEpoch();
            m_privateMirrorMaterializations.insert(
                opaqueId, result->materialization);
            saveRepositories();
            refreshRepositoryList();
            if (index == m_repoDetailIndex)
                refreshOpenRepoDetail();
            startRepoHosts();
            if (current.publishToNetwork) {
                QString gatewayError;
                if (rebuildDirectMirrorGatewayConfiguration(
                        &gatewayError, true)) {
                    publishRepository(index, false);
                } else {
                    logSystem(
                        QStringLiteral(
                            "Private publication remains blocked until the "
                            "direct HTTPS gateway is configured: %1")
                            .arg(gatewayError));
                }
            }
            finish(true, QString());
        });
    worker->start();
}

void MainWindow::downloadPrivateReplica(int index, bool quiet)
{
    if (index < 0 || index >= m_repositories.size() ||
        m_syncingRepos.contains(index))
        return;
    const RepositoryRecord repo = m_repositories.at(index);
    auto unavailable = [this, index, quiet](const QString &logDetail) {
        m_syncingRepos.remove(index);
        if (!logDetail.isEmpty())
            logSystem(QStringLiteral(
                "Private mirror: authorized encrypted replica download "
                "was unavailable."));
        if (!quiet)
            flashMessage(
                QStringLiteral(
                    "Private repository is unavailable or this account is "
                    "not authorized."),
                true);
        refreshRepositoryList();
    };
    if (!repo.isPrivate || !m_networkAccess ||
        !m_profileIdentity.isValid() ||
        !hasOwnerSigningCapability(accountOwner())) {
        unavailable(QStringLiteral("local authorization unavailable"));
        return;
    }

    m_syncingRepos.insert(index, quiet);
    refreshRepositoryList();
    ensurePrivateMirrorRecipientIdentityRegistered(
        [this, index, repo, quiet, unavailable](
            bool registered, QString) {
            if (!registered || index < 0 ||
                index >= m_repositories.size() ||
                m_repositories.at(index).owner != repo.owner ||
                m_repositories.at(index).name != repo.name) {
                unavailable(
                    QStringLiteral("recipient identity unavailable"));
                return;
            }
            const QByteArray authorization =
                privateReplicaAuthorization(repo);
            QUrl url = catalogApiUrl();
            // The edge-visible route contains only a random opaque archive id.
            // Owner/repository names remain inside the existing signed
            // authorization proof and are resolved after ACL lookup; there is
            // deliberately no named-route or query-string fallback.
            url.setPath(
                QStringLiteral("/api/private-replicas/") +
                repo.privateReplicaId);
            url.setQuery(QString());
            url.setFragment(QString());
            if (authorization.isEmpty() ||
                url.scheme().compare(QStringLiteral("https"),
                                     Qt::CaseInsensitive) != 0) {
                unavailable(
                    QStringLiteral("secure route unavailable"));
                return;
            }

            QNetworkRequest request(url);
            request.setRawHeader("Authorization", authorization);
            request.setAttribute(
                QNetworkRequest::RedirectPolicyAttribute,
                QNetworkRequest::ManualRedirectPolicy);
            request.setAttribute(
                QNetworkRequest::CacheLoadControlAttribute,
                QNetworkRequest::AlwaysNetwork);
            request.setAttribute(
                QNetworkRequest::CacheSaveControlAttribute, false);
            QNetworkReply *reply = m_networkAccess->get(request);
            auto bytes = std::make_shared<QByteArray>();
            auto overflowed = std::make_shared<bool>(false);
            auto appendChunk =
                [reply, bytes, overflowed]() {
                    QByteArray chunk = reply->readAll();
                    const qint64 maximum =
                        PrivateMirrorRuntime::
                            maximumSerializedReplicaBytes();
                    if (*overflowed ||
                        qint64(bytes->size()) + chunk.size() >
                            maximum) {
                        chunk.fill('\0');
                        chunk.clear();
                        bytes->fill('\0');
                        bytes->clear();
                        *overflowed = true;
                        reply->abort();
                        return;
                    }
                    bytes->append(chunk);
                };
            connect(reply, &QIODevice::readyRead, this,
                    appendChunk);
            connect(
                reply, &QNetworkReply::finished, this,
                [this, reply, index, repo, quiet, unavailable,
                 bytes, overflowed, appendChunk]() mutable {
                    appendChunk();
                    const int status =
                        reply->attribute(
                                 QNetworkRequest::
                                     HttpStatusCodeAttribute)
                            .toInt();
                    const QString contentType =
                        reply->header(
                                 QNetworkRequest::ContentTypeHeader)
                            .toString()
                            .section(QLatin1Char(';'), 0, 0)
                            .trimmed()
                            .toLower();
                    const QByteArray etag =
                        reply->rawHeader("ETag").trimmed();
                    bool lengthOk = false;
                    const qint64 announcedLength =
                        reply->rawHeader("Content-Length")
                            .toLongLong(&lengthOk);
                    const bool networkOk =
                        reply->error() ==
                        QNetworkReply::NoError;
                    reply->deleteLater();

                    static const QRegularExpression etagPattern(
                        QStringLiteral(
                            "^\"sha256-([0-9a-f]{64})\"$"));
                    const QRegularExpressionMatch etagMatch =
                        etagPattern.match(
                            QString::fromLatin1(etag));
                    const bool responseValid =
                        networkOk && !*overflowed &&
                        status == 200 &&
                        contentType ==
                            QLatin1String(
                                "application/vnd.forkmesh."
                                "private-replica+json") &&
                        etagMatch.hasMatch() && lengthOk &&
                        announcedLength > 0 &&
                        announcedLength ==
                            qint64(bytes->size()) &&
                        announcedLength <=
                            PrivateMirrorRuntime::
                                maximumSerializedReplicaBytes();
                    if (!responseValid) {
                        bytes->fill('\0');
                        bytes->clear();
                        unavailable(
                            QStringLiteral(
                                "ciphertext response invalid"));
                        return;
                    }

                    const QString expectedDigest =
                        etagMatch.captured(1);
                    const QString replicaRoot =
                        privateReplicaRoot();
                    const QString vaultPath =
                        privateIdentityVaultPath();
                    QByteArray vaultSecret =
                        privateIdentityVaultSecret(
                            m_profileIdentity);
                    auto result =
                        std::make_shared<
                            PrivateDownloadWorkerResult>();
                    QThread *worker = QThread::create(
                        [result, replicaRoot, vaultPath,
                         serialized = std::move(*bytes),
                         expectedDigest,
                         mutableVaultSecret =
                             std::move(vaultSecret)]() mutable {
                            result->opaqueId =
                                PrivateMirrorStore::
                                    importReplica(
                                        replicaRoot, serialized,
                                        expectedDigest,
                                        &result->metadata,
                                        &result->error);
                            serialized.fill('\0');
                            serialized.clear();
                            if (!result->opaqueId.isEmpty()) {
                                auto opened =
                                    PrivateMirrorRuntime::
                                        materialize(
                                            replicaRoot,
                                            vaultPath,
                                            mutableVaultSecret,
                                            result->opaqueId,
                                            &result->error);
                                if (opened) {
                                    result->materialization =
                                        std::shared_ptr<
                                            PrivateMirrorMaterialization>(
                                            std::move(opened));
                                }
                            }
                            mutableVaultSecret.fill('\0');
                            mutableVaultSecret.clear();
                        });
                    connect(
                        worker, &QThread::finished, this,
                        [this, worker, result, index, repo,
                         quiet, unavailable] {
                            worker->deleteLater();
                            if (index < 0 ||
                                index >=
                                    m_repositories.size() ||
                                m_repositories.at(index).owner !=
                                    repo.owner ||
                                m_repositories.at(index).name !=
                                    repo.name ||
                                !PrivateMirrorStore::isOpaqueId(
                                    result->opaqueId) ||
                                !result->materialization ||
                                !result->materialization
                                     ->isValid()) {
                                unavailable(QStringLiteral(
                                    "ciphertext authentication "
                                    "failed"));
                                return;
                            }
                            RepositoryRecord &current =
                                m_repositories[index];
                            const QString previousId =
                                current.privateReplicaId;
                            current.privateReplicaId =
                                result->opaqueId;
                            adoptMaterializedMirror(
                                current,
                                result->materialization
                                    ->repositoryPath());
                            current.lastSyncMs =
                                QDateTime::
                                    currentDateTimeUtc()
                                        .toMSecsSinceEpoch();
                            if (!previousId.isEmpty() &&
                                previousId !=
                                    result->opaqueId) {
                                m_privateMirrorMaterializations
                                    .remove(previousId);
                            }
                            m_privateMirrorMaterializations
                                .insert(
                                    result->opaqueId,
                                    result->materialization);
                            m_syncingRepos.remove(index);
                            saveRepositories();
                            refreshRepositoryList();
                            if (index ==
                                m_repoDetailIndex) {
                                refreshOpenRepoDetail();
                            }
                            startRepoHosts();
                            logSystem(QStringLiteral(
                                          "Private mirror: "
                                          "authenticated "
                                          "encrypted epoch %1 "
                                          "opened in temporary "
                                          "recipient-only "
                                          "storage.")
                                          .arg(result->metadata
                                                   .keyEpoch));
                            if (!quiet)
                                flashMessage(
                                    QStringLiteral(
                                        "Private repository "
                                        "opened from encrypted "
                                        "epoch %1.")
                                        .arg(
                                            result->metadata
                                                .keyEpoch));
                        });
                    worker->start();
                });
        });
}

void MainWindow::syncPublicEncryptedRepository(int index, bool quiet)
{
    if (index < 0 || index >= m_repositories.size() ||
        m_syncingRepos.contains(index))
        return;
    const RepositoryRecord repo = m_repositories.at(index);
    if (repo.previewOnly || repo.isPrivate)
        return;

    QByteArray vaultSecret =
        publicIdentityVaultSecret(m_profileIdentity);
    if (vaultSecret.size() < 32) {
        if (!quiet)
            flashMessage(
                QStringLiteral(
                    "Encrypted public mirror unavailable: the local device "
                    "identity could not unlock its age-key vault."),
                true);
        return;
    }

    const QString existingArchiveId = repo.publicArchiveId;
    const QString legacyMirrorPath = repo.mirrorPath;
    const QString managedMirrorRoot = repositoryMirrorRoot();
    QString source = repositorySource(repo);
    // A sealed mirror deliberately does not persist its plaintext
    // materialization. On process startup mirrorPath is therefore empty while
    // publicArchiveId remains available. Reopen that authenticated archive
    // first instead of cloning and re-encrypting the entire upstream before
    // this node can serve or publish anything. A later normal sync sees the
    // live temporary mirrorPath and refreshes from the upstream as usual.
    const bool reopeningSealedArchive =
        PublicMirrorRuntime::isArchiveId(existingArchiveId) &&
        (legacyMirrorPath.trimmed().isEmpty() ||
         !QDir(legacyMirrorPath).exists());
    if (reopeningSealedArchive)
        source.clear();
    // During a private→public transition, the authenticated private
    // materialization is the only name-free source. Prefer it over the legacy
    // named relay clone URL, which is intentionally inert for private bytes.
    if (PrivateMirrorStore::isOpaqueId(repo.privateReplicaId) &&
        QDir(legacyMirrorPath).exists()) {
        source = legacyMirrorPath;
    }
    if (source.isEmpty() && QDir(legacyMirrorPath).exists())
        source = legacyMirrorPath;
    if (source.isEmpty() &&
        !PublicMirrorRuntime::isArchiveId(existingArchiveId)) {
        vaultSecret.fill('\0');
        vaultSecret.clear();
        if (!quiet)
            flashMessage(
                QStringLiteral(
                    "Encrypted public mirror unavailable: no authenticated "
                    "source or sealed archive exists."),
                true);
        return;
    }

    m_syncingRepos.insert(index, quiet);
    refreshRepositoryList();
    logSystem(QStringLiteral(
        "Public mirror: sealing %1/%2 with official age encryption; "
        "plaintext is limited to owner-only temporary storage.")
                  .arg(repo.owner, repo.name));

    // A headless fleet node seals from its service-managed agent checkout
    // (repositorySource prefers localPath), so that checkout must itself keep
    // tracking the relay or the node serves its install-time snapshot forever.
    // Resolve the upstream URL here; the fetch/fast-forward runs on the worker
    // thread just before sealing. Owned repos and desktop working copies never
    // qualify: their local state IS the source of truth.
    QString upstreamUrl;
    if (m_headless && source == repo.localPath.trimmed() &&
        serviceManagedCheckout(repo.localPath)) {
        const QUrl upstream(repo.cloneUrl.trimmed());
        if (upstream.isValid() && !upstream.host().isEmpty() &&
            upstream.host().compare(catalogApiUrl().host(),
                                    Qt::CaseInsensitive) == 0)
            upstreamUrl = repo.cloneUrl.trimmed();
    }

    auto result = std::make_shared<PublicSyncWorkerResult>();
    const QString archiveRoot = publicArchiveRoot();
    const QString vaultPath = publicIdentityVaultPath();
    const QString owner = repo.owner;
    const QString name = repo.name;
    QThread *worker = QThread::create(
        [result, source, upstreamUrl, archiveRoot, vaultPath,
         mutableVaultSecret = std::move(vaultSecret), existingArchiveId,
         legacyMirrorPath, managedMirrorRoot]() mutable {
            if (!upstreamUrl.isEmpty()) {
                result->upstreamSummary =
                    forkmesh::upstream::refreshManagedCheckoutFromUpstream(
                        source, upstreamUrl)
                        .summary();
            }
            if (source.isEmpty()) {
                result->metadata = PublicMirrorRuntime::readMetadata(
                    archiveRoot, existingArchiveId, &result->error);
                if (result->metadata.isValid()) {
                    std::unique_ptr<PublicMirrorMaterialization>
                        materialization =
                            PublicMirrorRuntime::materialize(
                                archiveRoot, vaultPath,
                                mutableVaultSecret, existingArchiveId,
                                PublicMirrorRuntime::Tools(),
                                &result->error);
                    if (materialization) {
                        result->materialization =
                            std::shared_ptr<PublicMirrorMaterialization>(
                                std::move(materialization));
                    }
                }
            } else {
                PublicMirrorRuntime::SyncResult sync =
                    PublicMirrorRuntime::syncSource(
                        source, {}, archiveRoot, vaultPath,
                        mutableVaultSecret, existingArchiveId,
                        PublicMirrorRuntime::Tools(), &result->error);
                result->metadata = sync.metadata;
                result->created = sync.created;
                if (sync.materialization) {
                    result->materialization =
                        std::shared_ptr<PublicMirrorMaterialization>(
                            std::move(sync.materialization));
                }
            }

            // A replacement must be sealed and successfully reopened before a
            // legacy durable bare repository may be removed. User working
            // copies (repo.localPath) are never considered legacy mirror data.
            if (result->metadata.isValid() &&
                result->materialization &&
                result->materialization->isValid() &&
                !legacyMirrorPath.isEmpty() &&
                QDir(legacyMirrorPath).exists() &&
                legacyMirrorPath !=
                    result->materialization->repositoryPath()) {
                const QString root =
                    QFileInfo(managedMirrorRoot).canonicalFilePath();
                const QString target =
                    QFileInfo(legacyMirrorPath).canonicalFilePath();
                // Runtime materializations and user working copies are outside
                // the managed durable mirror root and must never be deleted as
                // migration artifacts.
                if (!root.isEmpty() && !target.isEmpty() &&
                    target.startsWith(
                        QDir::cleanPath(root) + QLatin1Char('/'))) {
                    QString cleanupError;
                    result->legacyRemoved =
                        PublicMirrorRuntime::removeManagedPlaintextMirror(
                            legacyMirrorPath, managedMirrorRoot,
                            &cleanupError);
                    if (!result->legacyRemoved) {
                        result->notice = QStringLiteral(
                            "The encrypted replacement is ready, but the legacy "
                            "plaintext mirror could not be removed. Publication "
                            "remains blocked until it is cleaned up.");
                    }
                }
            }
            mutableVaultSecret.fill('\0');
            mutableVaultSecret.clear();
        });
    connect(
        worker, &QThread::finished, this,
        [this, worker, result, index, owner, name, existingArchiveId,
         quiet] {
            worker->deleteLater();
            m_syncingRepos.remove(index);
            if (!result->upstreamSummary.isEmpty())
                logSystem(QStringLiteral("Mirror: %1/%2 %3")
                              .arg(owner, name, result->upstreamSummary));
            if (index < 0 || index >= m_repositories.size() ||
                m_repositories.at(index).owner != owner ||
                m_repositories.at(index).name != name ||
                m_repositories.at(index).isPrivate ||
                m_repositories.at(index).publicArchiveId !=
                    existingArchiveId) {
                refreshRepositoryList();
                return;
            }
            RepositoryRecord &current = m_repositories[index];
            if (!result->metadata.isValid() ||
                !result->materialization ||
                !result->materialization->isValid()) {
                refreshRepositoryList();
                logSystem(QStringLiteral(
                              "Public mirror: encrypted sync failed for %1/%2. "
                              "No new durable plaintext mirror was created.")
                              .arg(owner, name));
                if (!quiet) {
                    flashMessage(
                        QStringLiteral(
                            "Encrypted public mirror failed for %1/%2: %3")
                            .arg(owner, name,
                                 result->error.isEmpty()
                                     ? QStringLiteral(
                                           "age, age-keygen, tar, Git, and a "
                                           "valid source are required")
                                     : result->error),
                        true);
                }
                return;
            }

            if (PublicMirrorRuntime::isArchiveId(existingArchiveId) &&
                existingArchiveId != result->metadata.archiveId) {
                m_publicMirrorMaterializations.remove(
                    existingArchiveId);
            }
            current.publicArchiveId = result->metadata.archiveId;
            const QString previousPrivateReplica =
                current.privateReplicaId;
            current.privateReplicaId.clear();
            adoptMaterializedMirror(
                current, result->materialization->repositoryPath());
            current.lastSyncMs =
                QDateTime::currentMSecsSinceEpoch();
            m_publicMirrorMaterializations.insert(
                result->metadata.archiveId, result->materialization);
            if (PrivateMirrorStore::isOpaqueId(
                    previousPrivateReplica)) {
                m_privateMirrorMaterializations.remove(
                    previousPrivateReplica);
            }
            saveRepositories(); // never persists the temporary mirrorPath
            refreshRepositoryList();
            // The served mirror now lives in a fresh temporary
            // materialization (a new path on every seal/app start), so the
            // working copy's push URL and the post-receive hook must follow
            // it — otherwise a plain `git push` keeps targeting the removed
            // plaintext mirror and fails.
            ensurePushHook(current);

            if (!result->legacyRemoved) {
                logSystem(QStringLiteral("Public mirror: ") +
                          result->notice);
                if (!quiet)
                    flashMessage(result->notice, true);
                return;
            }

            logSystem(
                QStringLiteral(
                    "Public mirror: %1/%2 is stored as age ciphertext %3; "
                    "the authenticated repository is temporary.")
                    .arg(owner, name,
                         result->metadata.archiveId.left(12)));
            if (!quiet)
                flashMessage(
                    QStringLiteral(
                        "Encrypted public mirror ready for %1/%2.")
                        .arg(owner, name));
            if (index == m_repoDetailIndex)
                refreshOpenRepoDetail();
            scanRepoMentionsFor(current);
            if (current.publishToNetwork) {
                QString gatewayError;
                if (!rebuildDirectMirrorGatewayConfiguration(
                        &gatewayError, true)) {
                    logSystem(
                        QStringLiteral(
                            "Public mirror has no direct HTTPS gateway yet; "
                            "publishing its signed catalog state so the node "
                            "remains visible while endpoint setup is pending: %1")
                            .arg(gatewayError));
                    if (!quiet)
                        flashMessage(
                            QStringLiteral(
                                "Mirror is synced and visible; direct HTTPS "
                                "serving is still being configured."),
                            false);
                }
                // A public mirror's signed metadata is safe and useful even
                // before its optional direct endpoint is ready: the catalog
                // keeps the provisioning node visible but independently marks
                // it non-cloneable until endpoint health succeeds. Previously
                // this gate hid a fully registered/synced node from both Qt
                // and the World, making a successful one-click install look
                // lost whenever DNS/Tunnel setup lagged behind.
                publishRepository(index, false);
            }
            startRepoHosts();
            replicateReleaseArtifacts(index);
            // Propagate the freshly sealed state to the SSH-fed headless
            // mirrors too — they don't hear the relay's mirror-update frames.
            pushToSshMirrorRemotes(index);
        });
    worker->start();
}

// Push the served bare mirror's stable refs (heads + tags) to every ssh://
// push remote configured on the working copy — e.g. the ssh.<worker> Git
// gateway feeding the headless mirror fleet, whose post-receive hook re-seals
// and republishes each mirror. The relay's "mirror-update" websocket frame
// only reaches desktop peers in the live room; without this push the SSH-fed
// mirrors sat frozen at whatever the owner last pushed by hand (adhoc #272).
// Best-effort and fully async; runs after every successful mirror sync, so the
// Three-minute auto-sync doubles as the self-heal for a push a gateway missed.
void MainWindow::pushToSshMirrorRemotes(int index)
{
    if (index < 0 || index >= m_repositories.size())
        return;
    // By value: runGitCapture below pumps the event loop, and a reference into
    // m_repositories can dangle across it (git-pump UAF family, adhoc #106).
    const RepositoryRecord repo = m_repositories.at(index);
    // Only the source of truth propagates (a node holding the working copy);
    // mirrors and previews just pull, and private repos travel as sealed
    // replicas, never over a public mirror gateway.
    if (repo.previewOnly || repo.isPrivate ||
        repo.localPath.trimmed().isEmpty())
        return;
    // A source-of-truth repository has no on-disk served mirror: it publishes
    // through the sealed encrypted archive, and its record keeps mirrorPath
    // empty. Requiring one silently disabled every SSH-fed gateway for exactly
    // the repository that feeds them, so mirror2/mirror3 froze at whatever
    // commit the last manual push left while their catalog lease stayed fresh.
    // Fall back to the working copy, which holds the same heads and tags.
    const QString pushSource =
        (!repo.mirrorPath.trimmed().isEmpty() && QDir(repo.mirrorPath).exists())
            ? repo.mirrorPath
            : repo.localPath;
    if (!QDir(pushSource).exists())
        return;
    const QString repoKey = repo.owner + "/" + repo.name;
    if (m_sshMirrorPushing.contains(repoKey)) {
        m_sshMirrorPushPending.insert(repoKey);
        return;
    }

    QByteArray remotesOut;
    if (!runGitCapture(repo.localPath,
                       {QStringLiteral("remote"), QStringLiteral("-v")},
                       &remotesOut, nullptr))
        return;
    QStringList urls;
    for (const QString &line :
         QString::fromUtf8(remotesOut).split(QLatin1Char('\n'))) {
        const QString simplified = line.simplified();
        if (!simplified.endsWith(QLatin1String("(push)")))
            continue;
        const QStringList parts = simplified.split(QLatin1Char(' '));
        if (parts.size() < 2)
            continue;
        const QString url = parts.at(1);
        if (url.startsWith(QLatin1String("ssh://")) && !urls.contains(url))
            urls.append(url);
    }
    if (urls.isEmpty())
        return;

    m_sshMirrorPushing.insert(repoKey);
    auto remaining = std::make_shared<int>(urls.size());
    const auto finishPush = [this, repoKey, remaining]() {
        if (--*remaining > 0)
            return;
        m_sshMirrorPushing.remove(repoKey);
        if (!m_sshMirrorPushPending.remove(repoKey))
            return;
        // Repository rows may have moved while the asynchronous processes ran;
        // resolve by stable owner/name rather than retaining an index.
        QTimer::singleShot(0, this, [this, repoKey]() {
            for (int i = 0; i < m_repositories.size(); ++i) {
                const RepositoryRecord &candidate = m_repositories.at(i);
                if (candidate.owner + "/" + candidate.name == repoKey) {
                    pushToSshMirrorRemotes(i);
                    return;
                }
            }
        });
    };
    for (const QString &url : urls) {
        auto *process = new QProcess(this);
        // Never let an unreachable/unauthorized gateway hang the push on an
        // interactive credential or host-key prompt: this runs unattended.
        QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
        env.insert(QStringLiteral("GIT_TERMINAL_PROMPT"), QStringLiteral("0"));
        if (!env.contains(QStringLiteral("GIT_SSH_COMMAND")))
            env.insert(QStringLiteral("GIT_SSH_COMMAND"),
                       QStringLiteral("ssh -oBatchMode=yes"));
        process->setProcessEnvironment(env);
        const QString gatewayHost = QUrl(url).host();
        connect(process, &QProcess::finished, this,
                [this, process, repoKey, finishPush, gatewayHost](
                    int exitCode, QProcess::ExitStatus) {
                    const QString output =
                        QString::fromUtf8(process->readAllStandardOutput());
                    const QString errors =
                        QString::fromUtf8(process->readAllStandardError())
                            .trimmed();
                    process->deleteLater();
                    finishPush();
                    if (exitCode != 0) {
                        logSystem(QStringLiteral(
                                      "Mirror: SSH mirror push of %1 to %2 "
                                      "failed: %3")
                                      .arg(repoKey, gatewayHost,
                                           errors.right(300)));
                        return;
                    }
                    // --porcelain: one status line per ref; '=' means already
                    // up to date. Only speak up when something actually moved,
                    // so the quiet auto-sync cadence doesn't spam the log.
                    bool updated = false;
                    for (const QString &line :
                         output.split(QLatin1Char('\n'))) {
                        if (!line.isEmpty() && !line.startsWith('=') &&
                            !line.startsWith(QLatin1String("To ")) &&
                            !line.startsWith(QLatin1String("Done")))
                            updated = true;
                    }
                    if (updated)
                        logSystem(QStringLiteral(
                                      "Mirror: pushed %1 to SSH mirror %2.")
                                      .arg(repoKey, gatewayHost));
                });
        connect(process, &QProcess::errorOccurred, this,
                [this, process, repoKey, finishPush,
                 gatewayHost](QProcess::ProcessError error) {
                    // finished still fires for a crash after start; only a
                    // failed start ends the attempt here (avoids double
                    // decrement).
                    if (error != QProcess::FailedToStart)
                        return;
                    process->deleteLater();
                    finishPush();
                    logSystem(QStringLiteral("Mirror: could not run git to "
                                             "push %1 to SSH mirror %2.")
                                  .arg(repoKey, gatewayHost));
                });
        // Push from the served bare mirror (or the working copy when this is the
        // source of truth), but never let an unattended desktop rewind or delete
        // a branch that advanced on the gateway while this checkout was offline.
        // Automatic propagation therefore uses ordinary fast-forward refspecs
        // and explicitly disables force and prune, even when a machine carries
        // old push configuration. An intentional rewrite or branch deletion must
        // go through an explicit, reviewed Git operation; otherwise one stale
        // three-minute sync can undo a clean main merge on every headless
        // mirror.
        trackProcessActivity(process, QStringLiteral("push"),
                             QStringLiteral("Pushing %1/%2 to %3")
                                 .arg(repo.owner, repo.name, url));
        process->start(QStringLiteral("git"),
                       {QStringLiteral("-C"), pushSource,
                        QStringLiteral("push"), QStringLiteral("--porcelain"),
                        QStringLiteral("--atomic"),
                        QStringLiteral("--no-force"),
                        QStringLiteral("--no-prune"),
                        url, QStringLiteral("refs/heads/*:refs/heads/*"),
                        QStringLiteral("refs/tags/*:refs/tags/*")});
    }
}

void MainWindow::syncRepository(int index, bool quiet)
{
    if (index < 0 || index >= m_repositories.size() ||
        m_syncingRepos.contains(index))
        return;

    RepositoryRecord &repo = m_repositories[index];
    const bool preview = repo.previewOnly;
    if (!preview && repo.isPrivate) {
        syncPrivateRepository(index, quiet);
        return;
    }
    if (!preview) {
        syncPublicEncryptedRepository(index, quiet);
        return;
    }
    if (!QDir().mkpath(QFileInfo(repo.mirrorPath).absolutePath())) {
        if (!quiet)
            QMessageBox::warning(this, "Sync repository",
                                 "Could not create " +
                                     QFileInfo(repo.mirrorPath).absolutePath());
        return;
    }

    const bool hasMirror = QDir(repo.mirrorPath).exists();
    const QString source = repositorySource(repo);

    // A repository we publish ourselves, with no separate upstream working
    // copy, IS the source of truth. Re-fetching its own public route would be a
    // pointless loop through the HTTPS gateway, so there is nothing to sync.
    if (!preview && hasMirror && repo.publishToNetwork &&
        repo.localPath.trimmed().isEmpty() && repo.owner == accountOwner()) {
        if (!quiet)
            flashMessage(QStringLiteral("Nothing to sync for %1/%2 — this "
                                        "machine hosts it directly.")
                             .arg(repo.owner, repo.name));
        return;
    }

    // Mirror only the stable namespaces (branches + tags). Tool-managed refs
    // like refs/codex/* churn constantly on active repos: a client that wants a
    // ref which vanished between the advertisement and the pack negotiation gets
    // "not our ref" and the whole upload-pack fails (HTTP 502 -> "host
    // temporarily unavailable"). Issues and pull requests live in refs/heads, so
    // limiting to heads/tags keeps everything we serve while dropping the churn.
    static const QStringList kStableRefspecs = {
        QStringLiteral("+refs/heads/*:refs/heads/*"),
        QStringLiteral("+refs/tags/*:refs/tags/*")};
    // For our own private repo hosted through the mainnode, clone/fetch must carry
    // an owner-key-signed view token; viewAuthGitArgs returns the "-c
    // http.extraHeader=..." prefix (empty for public repos or non-mainnode sources)
    // generated fresh so the short-lived token never goes stale in stored config.
    const QStringList authArgs = viewAuthGitArgs(repo, source);
    const QStringList args =
        authArgs +
        (hasMirror ? QStringList{"-C", repo.mirrorPath, "fetch", "--prune",
                                "origin"} +
                        kStableRefspecs
                  : QStringList{"clone", "--bare", source, repo.mirrorPath});
    const QString mirrorPath = repo.mirrorPath;

    // Flag the repo "syncing" and reflect it in the UI right away — before any git
    // subprocess runs — so clicking "Sync" flips the button to "Syncing…"
    // instantly and never blocks the GUI thread. The insert also guards
    // re-entrancy so a concurrent auto-sync can't start a second fetch on this repo.
    m_syncingRepos.insert(index, quiet);
    refreshRepositoryList();
    if (!quiet) {
        const QString prefix =
            preview ? QStringLiteral("Preview cache: ")
                    : QStringLiteral("Mirror: ");
        logSystem(prefix +
                  (hasMirror ? QStringLiteral("fetching ") : QStringLiteral("cloning ")) +
                  repo.owner + "/" + repo.name + " from " + source + ".");
    }

    // Read the mirror's pre-fetch refs digest + HEAD and re-point origin at the
    // live source off the GUI thread. Each is a git subprocess that blocks for up
    // to 5s on a busy mirror (the for-each-ref digest over hundreds of issue/PR
    // refs is the slow one), and running them here froze the window every time a
    // sync *started* — the mirror image of the post-fetch housekeeping below,
    // which already runs on a worker for exactly this reason. The worker only
    // touches the mirror through path strings (never m_repositories or a widget);
    // the async fetch is kicked off back on the main thread once it finishes.
    auto beforeDigest = std::make_shared<QString>();
    auto beforeHeadCommit = std::make_shared<QString>();
    QThread *prep = QThread::create(
        [mirrorPath, source, hasMirror, beforeDigest, beforeHeadCommit] {
            *beforeDigest = mirrorRefsDigest(mirrorPath);
            *beforeHeadCommit =
                mirrorBranchCommit(mirrorPath, mirrorHeadBranch(mirrorPath));
            // Track the live source: an owned repo with a local working copy
            // should fetch from that copy, not from a stale relay URL baked into
            // origin at clone time (which can return HTTP 5xx through the host
            // tunnel).
            if (hasMirror && !source.isEmpty() &&
                !runGitCapture(mirrorPath,
                               {QStringLiteral("remote"), QStringLiteral("set-url"),
                                QStringLiteral("origin"), source},
                               nullptr, nullptr)) {
                runGitCapture(mirrorPath,
                              {QStringLiteral("remote"), QStringLiteral("add"),
                               QStringLiteral("origin"), source},
                              nullptr, nullptr);
            }
        });
    connect(prep, &QThread::finished, this,
            [this, prep, index, quiet, hasMirror, args, beforeDigest,
             beforeHeadCommit] {
                prep->deleteLater();
                if (index < 0 || index >= m_repositories.size()) {
                    m_syncingRepos.remove(index);
                    refreshRepositoryList();
                    return;
                }
                startSyncFetch(index, quiet, hasMirror, args, *beforeDigest,
                               *beforeHeadCommit);
            });
    prep->start();
}

void MainWindow::startSyncFetch(int index, bool quiet, bool hasMirror,
                                const QStringList &args,
                                const QString &beforeDigest,
                                const QString &beforeHeadCommit)
{
    auto *process = new QProcess(this);
    connect(process, &QProcess::finished, this,
            [this, process, index, quiet, beforeDigest, beforeHeadCommit,
             hasMirror](
                int exitCode, QProcess::ExitStatus) {
                const QString errors =
                    QString::fromUtf8(process->readAllStandardError()).trimmed();
                process->deleteLater();

                if (index < 0 || index >= m_repositories.size()) {
                    m_syncingRepos.remove(index);
                    refreshRepositoryList();
                    return;
                }

                RepositoryRecord &repo = m_repositories[index];
                if (exitCode == 0) {
                    // The post-fetch mirror housekeeping all shells out to git:
                    // repairing the bare repo's symbolic HEAD (so smart-HTTP clones
                    // check out real content), pruning churning tool refs left by
                    // older --mirror clones (so peers don't hit "not our ref"), and
                    // reading the refs back to tell whether the owner's repo
                    // actually changed. On a large or busy mirror that is hundreds
                    // of milliseconds of subprocess spawns, and running it here on
                    // the GUI thread froze the window every time a sync finished.
                    // Do that git work on a worker thread — it only touches the
                    // on-disk mirror through these path strings, never m_repositories
                    // or any widget — then apply the results back on the main thread,
                    // the same off-thread pattern scanRepoMentionsFor/deleteIssue
                    // use. The repo stays flagged "syncing" until the housekeeping
                    // finishes so a concurrent auto-sync can't race it on the same
                    // mirror.
                    const QString mirrorPath = repo.mirrorPath;
                    const QString localPath = repo.localPath;
                    auto afterDigest = std::make_shared<QString>();
                    auto headBranch = std::make_shared<QString>();
                    auto headCommit = std::make_shared<QString>();
                    QThread *worker = QThread::create(
                        [mirrorPath, localPath, afterDigest, headBranch,
                         headCommit] {
                            repairMirrorHead(mirrorPath, localPath);
                            pruneNonStableMirrorRefs(mirrorPath);
                            *afterDigest = mirrorRefsDigest(mirrorPath);
                            *headBranch = mirrorHeadBranch(mirrorPath);
                            *headCommit =
                                mirrorBranchCommit(mirrorPath, *headBranch);
                        });
                    connect(worker, &QThread::finished, this,
                            [this, worker, index, quiet, hasMirror, beforeDigest,
                             beforeHeadCommit, afterDigest, headBranch,
                             headCommit] {
                        worker->deleteLater();
                        m_syncingRepos.remove(index);
                        if (index < 0 || index >= m_repositories.size()) {
                            refreshRepositoryList();
                            return;
                        }
                        RepositoryRecord &repo = m_repositories[index];
                        const bool stillPreview = repo.previewOnly;
                        // Did the owner's repo actually change?
                        const bool changed =
                            !hasMirror || *afterDigest != beforeDigest;
                        repo.lastSyncMs = QDateTime::currentMSecsSinceEpoch();
                        if (!stillPreview)
                            saveRepositories();
                        refreshRepositoryList();
                        // Now that the bare mirror exists, (re)install the push
                        // hook so local pushes are detected (actions + refresh).
                        if (!stillPreview)
                            ensurePushHook(repo);
                        // A fresh install's first sync of the flagship repo (flagged
                        // by ensureFlagshipRepo, adhoc #113): open it now that the
                        // clone landed, instead of leaving the user on an empty list.
                        if (!stillPreview && !m_pendingAutoOpenRepoKey.isEmpty() &&
                            m_pendingAutoOpenRepoKey.compare(
                                repo.owner + "/" + repo.name, Qt::CaseInsensitive) == 0) {
                            m_pendingAutoOpenRepoKey.clear();
                            // On a fresh install, land on the welcome chat, not the
                            // Code view. Just select the repo internally (so the repo
                            // switcher shows "forkmesh") and refresh the UI; don't open
                            // the detail view which would load and show the Agents tab.
                            m_repoDetailIndex = index;
                            refreshRepositoryList();
                            QTimer::singleShot(0, this, [this] {
                                showChatView();
                                switchConversation(welcomeChannelForIdentity());
                            });
                        }
                        if (changed && hasMirror && !stillPreview &&
                            m_actionStore && !headBranch->isEmpty() &&
                            !headCommit->isEmpty() &&
                            *headCommit != beforeHeadCommit) {
                            enqueuePushEvent(repo.owner, repo.name, *headCommit,
                                             "refs/heads/" + *headBranch);
                        }
                        // If this repo's detail is open, reflect the new commits.
                        if (changed && index == m_repoDetailIndex)
                            refreshOpenRepoDetail();
                        // Newly-synced issues/PRs may @mention the local user.
                        if (changed && !stillPreview)
                            scanRepoMentionsFor(repo);
                        // Tell connected peers that also mirror this repo that it
                        // advanced from its source of truth. Only for real mirrors
                        // that already existed (an actual update, not a first clone).
                        // Carry the new HEAD so peers see exactly which commit is
                        // different and can confirm once they reach it.
                        const QString mirrorId =
                            catalogOwner(repo) + "/" +
                            repoSegment(repo.name, QStringLiteral("repository"));
                        if (changed && hasMirror && !stillPreview && m_backend)
                            m_backend->notifyMirrorUpdated(mirrorId, *headCommit);
                        // Round-trip acknowledgement: when we are a mirror pulling
                        // from an upstream source (not the working-copy owner that
                        // pushes it forward), report back that our mirror now holds
                        // the new commit, closing the signal -> update -> done loop
                        // in seconds instead of at the next advert tick.
                        if (changed && hasMirror && !stillPreview && m_backend &&
                            !headCommit->isEmpty() &&
                            !repositorySource(repo).isEmpty())
                            m_backend->notifyMirrorSynced(mirrorId, *headCommit);
                        // Quiet auto-syncs only speak up when something changed.
                        if (!quiet || changed) {
                            logSystem((stillPreview ? QStringLiteral("Preview cache: cached ")
                                                    : QStringLiteral("Mirror: synced ")) +
                                      repo.owner + "/" + repo.name + " into " +
                                      repo.mirrorPath + ".");
                        }
                        if (!quiet) {
                            flashMessage(stillPreview
                                             ? "Cached preview for " + repo.owner + "/" +
                                                   repo.name +
                                                   (changed ? QString()
                                                            : " (already up to date)")
                                             : "Synced " + repo.owner + "/" + repo.name +
                                                   (changed ? QString()
                                                            : " (already up to date)"));
                        }
                        if (!stillPreview && repo.publishToNetwork) {
                            publishRepository(index, false);
                            // Refresh the compatibility lifecycle hook after the
                            // direct-HTTPS mirror is published.
                            startRepoHosts();
                        }
                        // Also mirror the repo's release artifacts: pull any binary
                        // blobs we don't yet hold into our content-addressed store so
                        // this node can serve downloads too, not just clones (adhoc
                        // #77). Only on an actual change or a manual sync, so quiet
                        // auto-syncs don't re-scan an up-to-date store every tick.
                        if (!stillPreview && hasMirror && (changed || !quiet))
                            replicateReleaseArtifacts(index);
                    });
                    worker->start();
                } else {
                    m_syncingRepos.remove(index);
                    refreshRepositoryList();
                    // HTTPS gateway hiccups (HTTP 5xx, RPC failed, connection
                    // resets) and truncated responses are transient: the
                    // selected mirror endpoint is momentarily unavailable and
                    // the next sync will retry. Log
                    // them quietly rather than raising a persistent red error.
                    const bool transient = isTransientSyncError(errors);
                    logSystem((repo.previewOnly ? QStringLiteral("Preview cache: sync failed for ")
                                                : QStringLiteral("Mirror: sync failed for ")) +
                              repo.owner + "/" +
                              repo.name +
                              (transient ? QStringLiteral(" (host temporarily "
                                                          "unavailable): ")
                                         : QStringLiteral(": ")) +
                              errors.right(300));
                    if (!quiet && transient) {
                        flashMessage(QStringLiteral("Sync deferred for %1/%2 — host "
                                                    "temporarily unavailable.")
                                         .arg(repo.owner, repo.name),
                                     /*error=*/false);
                    } else if (!quiet) {
                        flashMessage(
                            (repo.previewOnly ? QStringLiteral("Preview failed for ")
                                              : QStringLiteral("Sync failed for ")) +
                                repo.owner + "/" + repo.name +
                                (errors.isEmpty() ? QString() :
                                                    ": " + errors.right(160)),
                            /*error=*/true);
                    }
                }
            });
    connect(process, &QProcess::errorOccurred, this,
            [this, process, index, quiet] {
                process->deleteLater();
                m_syncingRepos.remove(index);
                refreshRepositoryList();
                if (!quiet)
                    flashMessage(
                        "Could not run git. Install Git and try again.",
                        /*error=*/true);
            });
    trackProcessActivity(process, QStringLiteral("sync"),
                         QStringLiteral("git ") + args.join(QLatin1Char(' ')));
    process->start("git", args);
}

// ------------------------------------------------------------------ settings

void MainWindow::onProfileNameChanged(const QString &name)
{
    const QString trimmed = accountNameFromInput(name, QString());
    if (trimmed.isEmpty()) {
        if (m_settingsNameEdit)
            m_settingsNameEdit->setText(m_userName);
        return;
    }
    if (m_settingsNameEdit && m_settingsNameEdit->text() != trimmed)
        m_settingsNameEdit->setText(trimmed);
    if (m_nameEdit && m_nameEdit->text() != trimmed)
        m_nameEdit->setText(trimmed);
    if (trimmed == m_userName)
        return;
    const QString oldOwner =
        accountNameFromInput(m_accountName.isEmpty() ? m_userName : m_accountName,
                             QStringLiteral("owner"));
    setDesktopCapability(m_accountName, false);
    m_accountAuthenticated = false;
    m_accountTier = QStringLiteral("free");
    m_accountSolanaVerified = false;
    m_isAdmin = false;
    if (m_adminPollTimer)
        m_adminPollTimer->stop();
    QSettings().remove(kAuthedAccountSetting);
    m_userName = trimmed;
    m_accountName = trimmed;
    saveProfileName(trimmed);
    migrateReposForProfileName(oldOwner, trimmed);
    refreshSettingsEmailVerifiedBadge();
    updateUserSwitcher();
    updateChatIdentity();
    refreshRepositoryList();
    logSystem("Name changed to " + trimmed + ".");
}

void MainWindow::onAvatarChosen(const QByteArray &pngData)
{
    m_userAvatar = pngData;
    QSettings().setValue(kAvatarSetting, pngData);
    updateAvatarButton();
    updateUserAvatarButton();
    updateChatIdentity();
    // Persist to the account so the web dashboard shows the same avatar.
    pushAccountAvatar();
}

void MainWindow::logout()
{
    // Drop the signed-in account (admin/heartbeat state) so the user can log
    // back in, then tear the session down to the setup screen.
    const QString previousAccount = m_accountName;
    if (m_heartbeatTimer)
        m_heartbeatTimer->stop();
    if (m_adminPollTimer)
        m_adminPollTimer->stop();
    // Revoke the website session first — it is what the browser and the
    // account-scoped worker APIs see, so leaving it alive would keep this
    // machine "signed in" on the site after a desktop logout.
    revokeAccountSession();
    setDesktopCapability(m_accountName, false);
    m_accountAuthenticated = false;
    m_accountTier = QStringLiteral("free");
    m_accountSolanaVerified = false;
    m_isAdmin = false;
    m_seenPendingUsers.clear();
    m_accountName.clear();
    QSettings().remove(kAuthedAccountSetting);
    QSettings().remove(kAccountNameSetting);
    refreshSettingsEmailVerifiedBadge();
    leaveSession();
    // Come straight back with a password login. The setup screen's silent auth
    // only checks this node's key locally, so the website never learned about
    // the device; runLoginFlow() posts pubkey/deviceTs/deviceSig, which makes
    // the relay register this desktop key against the account and hand back a
    // real website session.
    promptRelogin(previousAccount);
}

void MainWindow::revokeAccountSession()
{
    const QString token = m_accountSessionToken.trimmed();
    m_accountSessionToken.clear();
    if (token.isEmpty() || !m_networkAccess)
        return;
    int status = 0;
    postAccountSync(QStringLiteral("logout"),
                    QJsonObject{{QStringLiteral("sessionToken"), token}},
                    &status);
}

bool MainWindow::promptRelogin(const QString &previousAccount)
{
    // A headless node has no one at the keyboard: it re-authenticates with its
    // key on the next start, so never block the service on a dialog.
    if (m_headless)
        return false;
    QString accountName = AccountCapability::normalizedAccount(previousAccount);
    if (!isValidNodeName(accountName)) {
        bool ok = false;
        accountName = QInputDialog::getText(this, "Log back in",
                                            "ForkMesh username:",
                                            QLineEdit::Normal, QString(), &ok)
                          .trimmed()
                          .toLower();
        if (!ok || !isValidNodeName(accountName))
            return false;
    }
    if (!runLoginFlow(accountName))
        return false;

    QSettings().setValue(kAccountNameSetting, m_accountName);
    if (m_settingsNameEdit)
        m_settingsNameEdit->setText(m_accountName);
    if (m_settingsMachineNodeEdit)
        m_settingsMachineNodeEdit->setText(machineNodeName());
    refreshSettingsEmailVerifiedBadge();
    // logout() left us on the setup screen; rejoin with the freshly signed-in
    // account so the user lands back in the app instead of clicking "Join".
    if (m_nameEdit)
        m_nameEdit->setText(m_accountName);
    startSession();
    return true;
}

void MainWindow::loginToUserAccount()
{
    // Ask which account to sign in to (defaulting to the current node name), then
    // run the shared email/password login flow. verifyTotpLogin() (via
    // runLoginFlow) sets m_accountName, the session token and auth state on
    // success, so we just sync the Settings fields afterwards.
    bool ok = false;
    const QString suggested = m_accountName.isEmpty()
                                  ? QSettings().value(kAccountNameSetting).toString()
                                  : m_accountName;
    const QString accountName =
        QInputDialog::getText(this, "Log in to a user account",
                              "ForkMesh username:", QLineEdit::Normal, suggested,
                              &ok)
            .trimmed()
            .toLower();
    if (!ok || accountName.isEmpty())
        return;
    if (!isValidNodeName(accountName)) {
        QMessageBox::warning(this, "Log in",
                             "Enter a valid username (lowercase letters, numbers "
                             "and hyphens; start with a letter).");
        return;
    }
    if (!runLoginFlow(accountName))
        return;

    // Persist and reflect the freshly signed-in account in the Settings UI.
    QSettings().setValue(kAccountNameSetting, m_accountName);
    if (m_settingsNameEdit)
        m_settingsNameEdit->setText(m_accountName);
    if (m_settingsMachineNodeEdit)
        m_settingsMachineNodeEdit->setText(machineNodeName());
    refreshSettingsEmailVerifiedBadge();
    QMessageBox::information(this, "Log in",
                             "Signed in as " + m_accountName + ".");
}

void MainWindow::uninstallForkMesh()
{
    const QString sourceDir = QStringLiteral(FORKMESH_SOURCE_DIR);
    const QString dataHome =
        QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);

    // Every directory ForkMesh owns: per-app data, local data, cache, the
    // QSettings config dir, and — for a from-checkout build — the source/build
    // tree the running binary lives in.
    QStringList dirs;
    auto addDir = [&dirs](const QString &d) {
        if (!d.isEmpty() && QDir(d).exists() && !dirs.contains(d))
            dirs << QDir(d).absolutePath();
    };
    addDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation));
    addDir(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation));
    addDir(QStandardPaths::writableLocation(QStandardPaths::CacheLocation));
    addDir(QFileInfo(QSettings().fileName()).absolutePath());
    addDir(sourceDir);
    // The IDE-extension handoff dir and the curl-installer's source checkout
    // (lowercase "forkmesh") aren't covered by the standard locations above.
    addDir(QDir::homePath() + QStringLiteral("/.forkmesh"));
    addDir(dataHome + QStringLiteral("/forkmesh"));

    // Loose files: the login-autostart entry, the installed desktop launcher,
    // the curl-installer binary, and every hicolor icon bucket install.sh wrote.
    QStringList files;
    auto addFile = [&files](const QString &f) {
        if (!f.isEmpty() && QFileInfo::exists(f) && !files.contains(f))
            files << f;
    };
    addFile(autostartDesktopPath());
    addFile(QDir::homePath() + QStringLiteral("/.local/bin/forkmesh"));
    addFile(dataHome + QStringLiteral("/applications/forkmesh.desktop"));
    addFile(dataHome + QStringLiteral("/icons/forkmesh.png"));
    QDirIterator iconIt(dataHome + QStringLiteral("/icons/hicolor"),
                        {QStringLiteral("forkmesh.png")}, QDir::Files,
                        QDirIterator::Subdirectories);
    while (iconIt.hasNext())
        addFile(iconIt.next());

    // ---- confirmation: a detailed warning, then a typed phrase -------------
    QString detail = QStringLiteral(
        "This permanently and irreversibly erases ForkMesh from this computer, "
        "including:\n\n"
        "  •  every mirrored repository\n"
        "  •  this machine's identity key (your account cannot be recovered)\n"
        "  •  all chat history, settings and caches\n"
        "  •  the desktop launcher and icons\n"
        "  •  the ForkMesh program files\n\nFolders removed:\n");
    for (const QString &d : std::as_const(dirs))
        detail += "    " + d + "/\n";
    detail += QStringLiteral("\nForkMesh will quit when it is done.");

    QMessageBox box(QMessageBox::Warning, QStringLiteral("Uninstall ForkMesh"),
                    detail, QMessageBox::Cancel, this);
    QPushButton *go =
        box.addButton(QStringLiteral("Uninstall…"), QMessageBox::DestructiveRole);
    box.setDefaultButton(QMessageBox::Cancel);
    box.exec();
    if (box.clickedButton() != go)
        return;

    bool ok = false;
    const QString typed = QInputDialog::getText(
        this, QStringLiteral("Confirm uninstall"),
        QStringLiteral("Type DELETE to permanently erase ForkMesh:"),
        QLineEdit::Normal, QString(), &ok);
    if (!ok || typed.trimmed().compare(QStringLiteral("DELETE"),
                                       Qt::CaseInsensitive) != 0)
        return;

    // ---- stop live services so nothing rewrites files during the wipe -----
    if (m_heartbeatTimer)
        m_heartbeatTimer->stop();
    if (m_adminPollTimer)
        m_adminPollTimer->stop();
    stopRepoHosts();
    if (m_backend) {
        m_backend->disconnect(this);
        m_backend->shutdown();
        m_backend->deleteLater();
        m_backend = nullptr;
    }

    const QStringList all = dirs + files;
#if defined(Q_OS_UNIX)
    // The source/build tree holds the binary we're running from, and the
    // settings file may still be open, so hand the whole removal to a detached
    // shell that waits for us to exit first. POSIX keeps a deleted-but-open
    // file alive until close, but a detached `rm` after we quit is the robust,
    // cross-shell way to be sure every byte is gone.
    auto shQuote = [](const QString &s) {
        return QLatin1Char('\'') + QString(s).replace(QStringLiteral("'"),
                                                      QStringLiteral("'\\''")) +
               QLatin1Char('\'');
    };
    QStringList quoted;
    for (const QString &p : all)
        quoted << shQuote(p);
    // Leave the directory tree we're about to delete before quitting.
    QDir::setCurrent(QDir::homePath());
    const bool spawned = QProcess::startDetached(
        QStringLiteral("/bin/sh"),
        {QStringLiteral("-c"),
         QStringLiteral("sleep 1; rm -rf ") + quoted.join(QLatin1Char(' '))});
    if (!spawned) {
        // No shell to hand off to: delete in-process as a best effort.
        for (const QString &f : std::as_const(files))
            QFile::remove(f);
        for (const QString &d : std::as_const(dirs))
            QDir(d).removeRecursively();
    }
#else
    for (const QString &f : std::as_const(files))
        QFile::remove(f);
    for (const QString &d : std::as_const(dirs))
        QDir(d).removeRecursively();
#endif

    QCoreApplication::exit(0);
}
