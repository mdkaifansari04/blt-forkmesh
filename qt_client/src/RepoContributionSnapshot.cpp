#include "RepoContributionSnapshot.h"
#include "RepoContributionSnapshotInternal.h"

#include "ForkMeshIdentity.h"
#include "IssueStore.h"
#include "PullStore.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMap>
#include <QProcess>
#include <QRegularExpression>
#include <QSet>
#include <QTime>
#include <QTimeZone>

#include <cmath>
#include <limits>
#include <utility>

namespace RepoContributionSnapshotInternal {

namespace {

std::function<void()> &beforePullMetadataRecheckHook()
{
    thread_local std::function<void()> hook;
    return hook;
}

thread_local std::optional<LanguageInspectionLimits>
    languageInspectionLimitsForTests;

LanguageInspectionLimits normalizedLanguageInspectionLimits(
    LanguageInspectionLimits limits)
{
    limits.maxBlobs = qMax<qsizetype>(0, limits.maxBlobs);
    limits.maxBlobBytes = qMax<qint64>(0, limits.maxBlobBytes);
    return limits;
}

LanguageInspectionLimits currentLanguageInspectionLimits()
{
    return normalizedLanguageInspectionLimits(
        languageInspectionLimitsForTests.value_or(
            LanguageInspectionLimits{}));
}

}

void setBeforePullMetadataRecheckHookForTests(std::function<void()> hook)
{
    beforePullMetadataRecheckHook() = std::move(hook);
}

ScopedLanguageInspectionLimitsForTests::
    ScopedLanguageInspectionLimitsForTests(
        const LanguageInspectionLimits &limits)
    : m_previous(languageInspectionLimitsForTests)
{
    languageInspectionLimitsForTests =
        normalizedLanguageInspectionLimits(limits);
}

ScopedLanguageInspectionLimitsForTests::
    ~ScopedLanguageInspectionLimitsForTests()
{
    languageInspectionLimitsForTests = m_previous;
}

void invokeBeforePullMetadataRecheckHook()
{
    std::function<void()> hook =
        std::move(beforePullMetadataRecheckHook());
    beforePullMetadataRecheckHook() = {};
    if (hook)
        hook();
}

QString dayActorKey(const QString &date, const QString &actor)
{
    return date + QChar::Null + actor;
}

void splitDayActorKey(const QString &key, QString *date, QString *actor)
{
    const qsizetype separator = key.indexOf(QChar::Null);
    if (date)
        *date = separator < 0 ? QString() : key.left(separator);
    if (actor)
        *actor = separator < 0 ? QString() : key.mid(separator + 1);
}

bool incrementBounded(int *value)
{
    if (!value || *value < 0 || *value >= kMaxCount)
        return false;
    ++*value;
    return true;
}

RemovedCoverage removeOldestDate(DayMap *days)
{
    RemovedCoverage removed;
    if (!days || days->isEmpty())
        return removed;

    QString oldestDate;
    splitDayActorKey(days->constBegin().key(), &oldestDate, nullptr);
    while (!days->isEmpty()) {
        QString currentDate;
        splitDayActorKey(days->constBegin().key(), &currentDate, nullptr);
        if (currentDate != oldestDate)
            break;
        const DayCounts counts = days->constBegin().value();
        removed.commits = removed.commits || counts.commits > 0;
        removed.collaboration = removed.collaboration || counts.issues > 0 ||
                                counts.pulls > 0 || counts.reviews > 0;
        days->erase(days->begin());
    }
    return removed;
}

RemovedCoverage trimDayLimit(DayMap *days)
{
    RemovedCoverage removed;
    if (!days)
        return removed;
    while (days->size() > kMaxDayRows) {
        const RemovedCoverage oldest = removeOldestDate(days);
        removed.commits = removed.commits || oldest.commits;
        removed.collaboration =
            removed.collaboration || oldest.collaboration;
    }
    return removed;
}

bool removeSmallestExtension(ExtensionMap *extensions)
{
    if (!extensions || extensions->isEmpty())
        return false;
    auto smallest = extensions->begin();
    for (auto it = extensions->begin(); it != extensions->end(); ++it) {
        if (it->bytes < smallest->bytes ||
            (it->bytes == smallest->bytes && it.key() > smallest.key())) {
            smallest = it;
        }
    }
    extensions->erase(smallest);
    return true;
}

bool trimExtensionLimit(ExtensionMap *extensions)
{
    bool trimmed = false;
    while (extensions && extensions->size() > kMaxExtensions)
        trimmed = removeSmallestExtension(extensions) || trimmed;
    return trimmed;
}

int encodedSize(const QByteArray &compact)
{
    return compact.toBase64(QByteArray::Base64UrlEncoding |
                            QByteArray::OmitTrailingEquals)
        .size();
}

bool encodedPayloadFits(const QByteArray &compact)
{
    return encodedSize(compact) <= kMaxPayloadEncoded;
}

bool appendBounded(QByteArray *destination, const QByteArray &chunk,
                   qsizetype maxBytes)
{
    if (!destination || maxBytes < 0 || destination->size() > maxBytes ||
        chunk.size() > maxBytes - destination->size()) {
        return false;
    }
    destination->append(chunk);
    return true;
}

}

namespace {

using namespace RepoContributionSnapshotInternal;

constexpr int kGitTimeoutMs = 30000;
constexpr int kMaxErrorLength = 240;
constexpr int kMaxRetentionYears = 5;
constexpr int kMaxGitCommits = kMaxCount + 1;
constexpr qsizetype kMaxGitOutputBytes = 64 * 1024 * 1024;
constexpr qsizetype kMaxGitErrorBytes = 16 * 1024;
constexpr qint64 kGitReadChunkBytes = 64 * 1024;

struct GitResult {
    bool ok = false;
    QByteArray output;
    QString error;
};

const QTimeZone &utcTimeZone()
{
    static const QTimeZone zone(QTimeZone::UTC);
    return zone;
}

QString boundedError(QString error)
{
    error = error.simplified();
    if (error.isEmpty())
        error = QStringLiteral("Could not build the repository contribution snapshot.");
    return error.left(kMaxErrorLength);
}

RepoContributionSnapshot failedSnapshot(const QString &error)
{
    RepoContributionSnapshot result;
    result.error = boundedError(error);
    return result;
}

GitResult runGit(const QString &dir, const QStringList &args,
                 const QByteArray *input = nullptr,
                 qsizetype maxOutputBytes = kMaxGitOutputBytes,
                 int timeoutMs = kGitTimeoutMs)
{
    GitResult result;
    if (dir.trimmed().isEmpty()) {
        result.error = QStringLiteral("Git source path is empty.");
        return result;
    }

    QProcess process;
    process.start(QStringLiteral("git"),
                  QStringList{QStringLiteral("-C"), dir} + args);
    if (input) {
        if (process.write(*input) != input->size()) {
            process.kill();
            result.error =
                QStringLiteral("Could not write Git command input.");
            return result;
        }
        process.closeWriteChannel();
    }

    QByteArray standardError;
    auto drainChannel = [&](QProcess::ProcessChannel channel,
                            QByteArray *destination, qsizetype maxBytes) {
        process.setReadChannel(channel);
        while (process.bytesAvailable() > 0) {
            const qsizetype remaining = maxBytes - destination->size();
            const qint64 requestSize = qMin<qint64>(
                kGitReadChunkBytes, qint64(remaining) + 1);
            const QByteArray chunk = process.read(requestSize);
            if (chunk.isEmpty())
                break;
            if (!appendBounded(destination, chunk, maxBytes))
                return false;
        }
        return true;
    };
    auto drainOutput = [&] {
        return drainChannel(QProcess::StandardOutput, &result.output,
                            maxOutputBytes) &&
               drainChannel(QProcess::StandardError, &standardError,
                            kMaxGitErrorBytes);
    };

    QElapsedTimer timer;
    timer.start();
    for (;;) {
        const bool finished = process.waitForFinished(40);
        if (!drainOutput()) {
            process.kill();
            process.waitForFinished(1000);
            result.error = QStringLiteral(
                "Git command output exceeded the snapshot limit.");
            return result;
        }
        if (finished || process.state() == QProcess::NotRunning)
            break;
        if (timer.hasExpired(timeoutMs)) {
            process.kill();
            process.waitForFinished(1000);
            result.error = QStringLiteral("Git command timed out.");
            return result;
        }
    }
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
        result.error = QString::fromUtf8(standardError)
                           .trimmed()
                           .left(kMaxErrorLength);
        if (result.error.isEmpty())
            result.error = QStringLiteral("Git command failed.");
        return result;
    }
    result.ok = true;
    return result;
}

bool unsafeSnapshotCategory(QChar::Category category)
{
    switch (category) {
    case QChar::Other_Control:
    case QChar::Other_Format:
    case QChar::Other_Surrogate:
    case QChar::Other_PrivateUse:
    case QChar::Other_NotAssigned:
    case QChar::Separator_Space:
    case QChar::Separator_Line:
    case QChar::Separator_Paragraph:
        return true;
    default:
        return false;
    }
}

bool safeSnapshotString(const QString &value, qsizetype maxLength)
{
    if (value.isEmpty())
        return false;

    qsizetype codePointCount = 0;
    for (qsizetype i = 0; i < value.size(); ++i) {
        const QChar current = value.at(i);
        char32_t codePoint = current.unicode();
        if (current.isHighSurrogate()) {
            if (i + 1 >= value.size() || !value.at(i + 1).isLowSurrogate())
                return false;
            codePoint = QChar::surrogateToUcs4(current, value.at(++i));
        } else if (current.isLowSurrogate()) {
            return false;
        }
        if (++codePointCount > maxLength ||
            unsafeSnapshotCategory(QChar::category(codePoint))) {
            return false;
        }
    }
    return true;
}

QString resolvedCommit(const QString &dir, const QString &ref)
{
    if (!safeSnapshotString(ref, std::numeric_limits<qsizetype>::max()))
        return {};
    const GitResult result =
        runGit(dir, {QStringLiteral("rev-parse"), QStringLiteral("--verify"),
                     QStringLiteral("-q"), ref + QStringLiteral("^{commit}")});
    return result.ok ? QString::fromUtf8(result.output).trimmed().toLower()
                     : QString();
}

QStringList branchCandidates(const QString &dir, const QString &branch)
{
    if (!safeSnapshotString(branch, kMaxBranchLength))
        return {};
    const QString &value = branch;
    if (value.startsWith(QLatin1String("refs/")))
        return {value};

    QStringList candidates{value, QStringLiteral("refs/heads/") + value,
                           QStringLiteral("refs/remotes/") + value};
    const GitResult refs = runGit(
        dir, {QStringLiteral("for-each-ref"), QStringLiteral("--format=%(refname)"),
              QStringLiteral("refs/heads/"), QStringLiteral("refs/remotes/")});
    if (refs.ok) {
        for (const QByteArray &line : refs.output.split('\n')) {
            const QString ref = QString::fromUtf8(line).trimmed();
            if (ref.isEmpty() || ref.endsWith(QLatin1String("/HEAD")))
                continue;
            if ((ref.startsWith(QLatin1String("refs/heads/")) &&
                 ref.mid(QStringLiteral("refs/heads/").size()) == value) ||
                (ref.startsWith(QLatin1String("refs/remotes/")) &&
                 ref.section('/', 3) == value) ||
                ref.endsWith(QLatin1Char('/') + value)) {
                if (!candidates.contains(ref))
                    candidates.append(ref);
            }
        }
    }
    return candidates;
}

QString resolvedBranchRef(const QString &dir, const QString &branch,
                          const QString &head)
{
    for (const QString &candidate : branchCandidates(dir, branch)) {
        if (resolvedCommit(dir, candidate) == head)
            return candidate;
    }
    return {};
}

bool sourceMatchesInput(const QString &dir, const RepoContributionSnapshotInput &input,
                        QString *headOut = nullptr,
                        QString *branchRefOut = nullptr)
{
    const QString requested = resolvedCommit(dir, input.head);
    if (requested.isEmpty())
        return false;
    const QString branchRef =
        resolvedBranchRef(dir, input.branch, requested);
    if (branchRef.isEmpty())
        return false;
    if (headOut)
        *headOut = requested;
    if (branchRefOut)
        *branchRefOut = branchRef;
    return true;
}

QString chooseSource(const RepoContributionSnapshotInput &input,
                     QString *resolvedHead, QString *resolvedBranch)
{
    const QStringList candidates{input.mirrorPath.trimmed(),
                                 input.workTreePath.trimmed()};
    QSet<QString> seen;
    for (const QString &candidate : candidates) {
        if (candidate.isEmpty() || seen.contains(candidate) ||
            !QFileInfo::exists(candidate))
            continue;
        seen.insert(candidate);
        QString head;
        QString branchRef;
        if (sourceMatchesInput(candidate, input, &head, &branchRef)) {
            if (resolvedHead)
                *resolvedHead = head;
            if (resolvedBranch)
                *resolvedBranch = branchRef;
            return candidate;
        }
    }
    return {};
}

QString configValue(const QString &dir, const QString &key, bool localOnly)
{
    if (dir.trimmed().isEmpty() || !QFileInfo::exists(dir))
        return {};
    QStringList args{QStringLiteral("config")};
    if (localOnly)
        args << QStringLiteral("--local");
    args << QStringLiteral("--get") << key;
    const GitResult result = runGit(dir, args);
    return result.ok ? QString::fromUtf8(result.output).trimmed() : QString();
}

QString configuredIdentityValue(const RepoContributionSnapshotInput &input,
                                const QString &source, const QString &key)
{
    const QString workTree = input.workTreePath.trimmed();
    QString value = configValue(workTree, key, true);
    if (!value.isEmpty())
        return value;
    return configValue(source, key, false);
}

QString normalizedEmail(const QString &value)
{
    return value.trimmed().toLower();
}

QString normalizedName(const QString &value)
{
    return value.simplified().toCaseFolded();
}

bool validActorKey(const QString &key)
{
    if (key.size() != 43)
        return false;
    for (const QChar ch : key) {
        const ushort code = ch.unicode();
        const bool asciiLetter =
            (code >= 'A' && code <= 'Z') || (code >= 'a' && code <= 'z');
        const bool asciiDigit = code >= '0' && code <= '9';
        if (!(asciiLetter || asciiDigit || ch == QLatin1Char('-') ||
              ch == QLatin1Char('_'))) {
            return false;
        }
    }
    const QByteArray decoded =
        QByteArray::fromBase64(key.toLatin1(), QByteArray::Base64UrlEncoding);
    return decoded.size() == 32 &&
           QString::fromLatin1(decoded.toBase64(
               QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals)) == key;
}

bool validSignature(const QString &signature)
{
    if (signature.size() != 86)
        return false;
    for (const QChar ch : signature) {
        const ushort code = ch.unicode();
        const bool asciiLetter =
            (code >= 'A' && code <= 'Z') || (code >= 'a' && code <= 'z');
        const bool asciiDigit = code >= '0' && code <= '9';
        if (!(asciiLetter || asciiDigit || ch == QLatin1Char('-') ||
              ch == QLatin1Char('_'))) {
            return false;
        }
    }
    const QByteArray decoded = QByteArray::fromBase64(
        signature.toLatin1(), QByteArray::Base64UrlEncoding);
    return decoded.size() == 64 &&
           QString::fromLatin1(decoded.toBase64(
               QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals)) ==
               signature;
}

QString utcDateForMs(qint64 timestampMs)
{
    return QDateTime::fromMSecsSinceEpoch(timestampMs, utcTimeZone())
        .date()
        .toString(Qt::ISODate);
}

bool timestampInRange(qint64 timestampMs, qint64 capturedAtMs,
                      const QDate &from, const QDate &through)
{
    if (timestampMs < 0 || timestampMs > capturedAtMs)
        return false;
    const QDate date =
        QDateTime::fromMSecsSinceEpoch(timestampMs, utcTimeZone()).date();
    return date.isValid() && date >= from && date <= through;
}

QString eventIdentity(const QByteArray &canonical, const QString &author,
                      const QString &signature, const QByteArray &kind)
{
    return QString::fromLatin1(kind) + QLatin1Char('\n') + author + QLatin1Char('\n') +
           signature + QLatin1Char('\n') +
           QString::fromLatin1(QCryptographicHash::hash(
                                  canonical, QCryptographicHash::Sha256)
                                  .toHex());
}

bool verifiedIssueOpen(int issueNumber, const IssueEvent &event,
                       QByteArray *canonicalOut)
{
    if (!validActorKey(event.author) || !validSignature(event.sig))
        return false;
    QByteArray canonical = IssueStore::canonicalString(issueNumber, event);
    if (ForkMeshIdentity::verifySignature(event.author, event.sig, canonical)) {
        if (canonicalOut)
            *canonicalOut = canonical;
        return true;
    }

    static const QRegularExpression openId(QStringLiteral("^open-([1-9][0-9]*)$"));
    const QRegularExpressionMatch match = openId.match(event.id);
    if (!match.hasMatch())
        return false;
    bool ok = false;
    const int proposed = match.captured(1).toInt(&ok);
    if (!ok || proposed == issueNumber)
        return false;
    canonical = IssueStore::canonicalString(proposed, event);
    if (!ForkMeshIdentity::verifySignature(event.author, event.sig, canonical))
        return false;
    if (canonicalOut)
        *canonicalOut = canonical;
    return true;
}

const QSet<QString> &supportedExtensions()
{
    static const QSet<QString> extensions{
        QStringLiteral("c"),       QStringLiteral("h"),      QStringLiteral("cc"),
        QStringLiteral("cpp"),     QStringLiteral("cxx"),    QStringLiteral("hh"),
        QStringLiteral("hpp"),     QStringLiteral("hxx"),    QStringLiteral("cs"),
        QStringLiteral("go"),      QStringLiteral("html"),   QStringLiteral("htm"),
        QStringLiteral("css"),     QStringLiteral("java"),   QStringLiteral("js"),
        QStringLiteral("mjs"),     QStringLiteral("cjs"),    QStringLiteral("jsx"),
        QStringLiteral("json"),    QStringLiteral("kt"),     QStringLiteral("kts"),
        QStringLiteral("md"),      QStringLiteral("markdown"),
        QStringLiteral("mdown"),   QStringLiteral("mkdn"),   QStringLiteral("php"),
        QStringLiteral("py"),      QStringLiteral("pyw"),    QStringLiteral("rb"),
        QStringLiteral("rs"),      QStringLiteral("sh"),     QStringLiteral("bash"),
        QStringLiteral("zsh"),     QStringLiteral("fish"),   QStringLiteral("sql"),
        QStringLiteral("swift"),   QStringLiteral("ts"),     QStringLiteral("tsx"),
        QStringLiteral("mts"),     QStringLiteral("cts"),    QStringLiteral("yaml"),
        QStringLiteral("yml")};
    return extensions;
}

bool internalPath(const QString &path)
{
    return path == QLatin1String(".forkmesh") ||
           path.startsWith(QLatin1String(".forkmesh/"));
}

bool generatedSource(const QString &path, const QByteArray &content)
{



    const QString lowerPath = path.toLower();
    const QStringList segments = lowerPath.split(QLatin1Char('/'));
    if (segments.contains(QStringLiteral("generated")))
        return true;
    const QString name = segments.isEmpty() ? lowerPath : segments.constLast();
    if (name.contains(QLatin1String(".generated.")) ||
        name.endsWith(QLatin1String(".generated"))) {
        return true;
    }

    const QByteArray prefix = content.left(8 * 1024).toLower();
    return prefix.contains("@generated") ||
           (prefix.contains("code generated") &&
            prefix.contains("do not edit")) ||
           prefix.contains("this file is auto-generated") ||
           prefix.contains("this file is autogenerated");
}

bool parseBlobBatch(const QByteArray &batch, const QSet<QString> &expected,
                    QHash<QString, QByteArray> *contentByOid)
{
    if (!contentByOid)
        return false;
    contentByOid->clear();
    contentByOid->reserve(expected.size());
    for (qsizetype pos = 0; pos < batch.size();) {
        const qsizetype newline = batch.indexOf('\n', pos);
        if (newline < 0)
            return false;
        const QList<QByteArray> header =
            batch.mid(pos, newline - pos).split(' ');
        pos = newline + 1;
        if (header.size() != 3 ||
            header.at(1) != QByteArrayLiteral("blob")) {
            return false;
        }
        bool sizeOk = false;
        const qlonglong size = header.at(2).toLongLong(&sizeOk);
        if (!sizeOk || size < 0 || size > batch.size() - pos ||
            pos + size >= batch.size() || batch.at(pos + size) != '\n') {
            return false;
        }
        const QString oid = QString::fromUtf8(header.at(0));
        if (!expected.contains(oid) || contentByOid->contains(oid))
            return false;
        contentByOid->insert(oid, batch.mid(pos, size));
        pos += size + 1;
    }
    return contentByOid->size() == expected.size();
}

QJsonArray dayRows(const QMap<QString, DayCounts> &days)
{
    QJsonArray rows;
    for (auto it = days.constBegin(); it != days.constEnd(); ++it) {
        QString date;
        QString actor;
        splitDayActorKey(it.key(), &date, &actor);
        rows.append(QJsonArray{date, actor, it->commits, it->issues, it->pulls,
                               it->reviews});
    }
    return rows;
}

QJsonArray extensionRows(const QMap<QString, ExtensionCounts> &extensions)
{
    QJsonArray rows;
    for (auto it = extensions.constBegin(); it != extensions.constEnd(); ++it)
        rows.append(QJsonArray{it.key(), it->bytes, it->files});
    return rows;
}

QJsonObject makePayload(const RepoContributionSnapshotInput &input,
                        const QString &resolvedHead, const QDate &from,
                        const QDate &through, const QString &commitCoverage,
                        const QString &collaborationCoverage,
                        const QString &languageCoverage,
                        const QMap<QString, DayCounts> &days,
                        const QMap<QString, ExtensionCounts> &extensions,
                        int fileCount)
{
    QJsonObject coverage;
    coverage.insert(QStringLiteral("commits"), commitCoverage);
    coverage.insert(QStringLiteral("collaboration"), collaborationCoverage);
    coverage.insert(QStringLiteral("languages"), languageCoverage);

    QJsonObject payload;
    payload.insert(QStringLiteral("version"), 1);
    payload.insert(QStringLiteral("capturedAt"), double(input.capturedAtMs));
    payload.insert(QStringLiteral("head"), resolvedHead);
    payload.insert(QStringLiteral("branch"), input.branch);
    payload.insert(QStringLiteral("from"), from.toString(Qt::ISODate));
    payload.insert(QStringLiteral("through"), through.toString(Qt::ISODate));
    payload.insert(QStringLiteral("coverage"), coverage);
    payload.insert(QStringLiteral("days"), dayRows(days));
    payload.insert(QStringLiteral("extensions"), extensionRows(extensions));
    payload.insert(QStringLiteral("fileCount"), fileCount);
    return payload;
}

QByteArray compactPayload(const QJsonObject &payload)
{
    return QJsonDocument(payload).toJson(QJsonDocument::Compact);
}

QString logoLanguageForExtension(const QString &extension)
{
    static const QHash<QString, QString> names{
        {QStringLiteral("c"), QStringLiteral("C")},
        {QStringLiteral("h"), QStringLiteral("C/C++")},
        {QStringLiteral("cc"), QStringLiteral("C++")},
        {QStringLiteral("cpp"), QStringLiteral("C++")},
        {QStringLiteral("cxx"), QStringLiteral("C++")},
        {QStringLiteral("hh"), QStringLiteral("C++")},
        {QStringLiteral("hpp"), QStringLiteral("C++")},
        {QStringLiteral("hxx"), QStringLiteral("C++")},
        {QStringLiteral("cs"), QStringLiteral("C#")},
        {QStringLiteral("go"), QStringLiteral("Go")},
        {QStringLiteral("html"), QStringLiteral("HTML")},
        {QStringLiteral("htm"), QStringLiteral("HTML")},
        {QStringLiteral("css"), QStringLiteral("CSS")},
        {QStringLiteral("java"), QStringLiteral("Java")},
        {QStringLiteral("js"), QStringLiteral("JavaScript")},
        {QStringLiteral("mjs"), QStringLiteral("JavaScript")},
        {QStringLiteral("cjs"), QStringLiteral("JavaScript")},
        {QStringLiteral("jsx"), QStringLiteral("JavaScript")},
        {QStringLiteral("json"), QStringLiteral("JSON")},
        {QStringLiteral("kt"), QStringLiteral("Kotlin")},
        {QStringLiteral("kts"), QStringLiteral("Kotlin")},
        {QStringLiteral("md"), QStringLiteral("Markdown")},
        {QStringLiteral("markdown"), QStringLiteral("Markdown")},
        {QStringLiteral("mdown"), QStringLiteral("Markdown")},
        {QStringLiteral("mkdn"), QStringLiteral("Markdown")},
        {QStringLiteral("php"), QStringLiteral("PHP")},
        {QStringLiteral("py"), QStringLiteral("Python")},
        {QStringLiteral("pyw"), QStringLiteral("Python")},
        {QStringLiteral("rb"), QStringLiteral("Ruby")},
        {QStringLiteral("rs"), QStringLiteral("Rust")},
        {QStringLiteral("sh"), QStringLiteral("Shell")},
        {QStringLiteral("bash"), QStringLiteral("Shell")},
        {QStringLiteral("zsh"), QStringLiteral("Shell")},
        {QStringLiteral("fish"), QStringLiteral("Shell")},
        {QStringLiteral("sql"), QStringLiteral("SQL")},
        {QStringLiteral("swift"), QStringLiteral("Swift")},
        {QStringLiteral("ts"), QStringLiteral("TypeScript")},
        {QStringLiteral("tsx"), QStringLiteral("TypeScript")},
        {QStringLiteral("mts"), QStringLiteral("TypeScript")},
        {QStringLiteral("cts"), QStringLiteral("TypeScript")},
        {QStringLiteral("yaml"), QStringLiteral("YAML")},
        {QStringLiteral("yml"), QStringLiteral("YAML")},
        {QStringLiteral("dart"), QStringLiteral("Dart")},
        {QStringLiteral("vue"), QStringLiteral("Vue")},
        {QStringLiteral("svelte"), QStringLiteral("Svelte")},
    };
    return names.value(extension.toLower());
}

void addLogoLanguage(QMap<QString, qint64> *languages,
                     const QString &extension, qint64 bytes)
{
    if (!languages || bytes <= 0)
        return;
    const QString language = logoLanguageForExtension(extension);
    if (language.isEmpty())
        return;
    constexpr qint64 kMaxLanguageBytes = qint64(1) << 50;
    const qint64 prior = languages->value(language);
    languages->insert(
        language, qMin(kMaxLanguageBytes, prior + qMin(bytes, kMaxLanguageBytes)));
}

QString logoProjectCategory(const QString &description,
                            const QStringList &topics,
                            const QSet<QString> &frameworks,
                            const QSet<QString> &topLevel)
{
    const QString clues =
        (description + QLatin1Char(' ') + topics.join(QLatin1Char(' ')))
            .toLower();
    auto mentions = [&clues](std::initializer_list<const char *> needles) {
        for (const char *needle : needles) {
            if (clues.contains(QString::fromLatin1(needle)))
                return true;
        }
        return false;
    };
    if (mentions({"developer platform", "dev platform", "git forge"}))
        return QStringLiteral("developer platform");
    if (mentions({"game", "three.js", "threejs"}))
        return QStringLiteral("game or interactive experience");
    if (mentions({"command line", "cli", "terminal tool"}))
        return QStringLiteral("command-line tool");
    if (mentions({"library", "sdk", "framework"}))
        return QStringLiteral("library or framework");
    if (mentions({"documentation", "docs", "handbook"}))
        return QStringLiteral("documentation");
    if (mentions({"mobile", "android", "ios"}) ||
        frameworks.contains(QStringLiteral("Flutter"))) {
        return QStringLiteral("mobile application");
    }
    if (frameworks.contains(QStringLiteral("Cloudflare Workers")) ||
        topLevel.contains(QStringLiteral("terraform")) ||
        topLevel.contains(QStringLiteral("infrastructure"))) {
        return QStringLiteral("cloud or infrastructure");
    }
    if (frameworks.contains(QStringLiteral("Next.js")) ||
        frameworks.contains(QStringLiteral("Vite")) ||
        frameworks.contains(QStringLiteral("Node.js"))) {
        return QStringLiteral("web application");
    }
    if (frameworks.contains(QStringLiteral("CMake")) ||
        frameworks.contains(QStringLiteral("Qt"))) {
        return QStringLiteral("native application or library");
    }
    if (mentions({"api", "service", "server"}))
        return QStringLiteral("service or API");
    return QStringLiteral("software project");
}

}

RepoContributionSnapshot buildRepoContributionSnapshot(
    const RepoContributionSnapshotInput &input)
{
    if (input.capturedAtMs <= 0)
        return failedSnapshot(QStringLiteral("Snapshot capture time is invalid."));
    if (!validActorKey(input.publishingKey))
        return failedSnapshot(QStringLiteral("Publishing key is not a valid Ed25519 key."));
    if (!safeSnapshotString(input.head, kMaxHeadLength) ||
        !safeSnapshotString(input.branch, kMaxBranchLength)) {
        return failedSnapshot(QStringLiteral("Snapshot head or branch is invalid."));
    }

    const QDateTime capturedAt =
        QDateTime::fromMSecsSinceEpoch(input.capturedAtMs, utcTimeZone());
    if (!capturedAt.isValid())
        return failedSnapshot(QStringLiteral("Snapshot capture time is out of range."));
    const QDate through = capturedAt.date();
    const int retentionYears = qBound(1, input.retentionYears, kMaxRetentionYears);
    QDate from = through.addYears(-retentionYears);
    if (from.addYears(retentionYears) < through)
        from = from.addDays(1);
    if (!from.isValid() || !through.isValid())
        return failedSnapshot(QStringLiteral("Snapshot date range is invalid."));

    QString resolvedHead;
    QString resolvedBranch;
    const QString source =
        chooseSource(input, &resolvedHead, &resolvedBranch);
    if (source.isEmpty()) {
        return failedSnapshot(
            QStringLiteral("Repository branch does not match the requested snapshot."));
    }

    QStringList changedFiles;
    const GitResult changed = runGit(
        source,
        {QStringLiteral("diff-tree"), QStringLiteral("--root"),
         QStringLiteral("--no-commit-id"), QStringLiteral("--name-only"),
         QStringLiteral("-r"), QStringLiteral("-z"), resolvedHead});
    if (changed.ok) {
        for (const QByteArray &rawPath : changed.output.split('\0')) {
            QString path =
                QDir::fromNativeSeparators(QString::fromUtf8(rawPath)).trimmed();
            if (path.isEmpty() || path.startsWith(QLatin1Char('/')) ||
                path == QLatin1String("..") ||
                path.startsWith(QLatin1String("../")) ||
                !safeSnapshotString(path, 160) ||
                changedFiles.contains(path)) {
                continue;
            }
            changedFiles.append(path);
            if (changedFiles.size() >= 8)
                break;
        }
    }

    QString commitCoverage = QStringLiteral("complete");
    QString collaborationCoverage = QStringLiteral("complete");
    QString languageCoverage = QStringLiteral("complete");
    QMap<QString, DayCounts> days;

    const QString configuredEmail = normalizedEmail(configuredIdentityValue(
        input, source, QStringLiteral("user.email")));
    const QString configuredName = normalizedName(configuredIdentityValue(
        input, source, QStringLiteral("user.name")));
    if (configuredEmail.isEmpty() && configuredName.isEmpty()) {
        commitCoverage = QStringLiteral("partial");
    } else {
        const GitResult log = runGit(
            source,
            {QStringLiteral("log"),
             QStringLiteral("--max-count=") + QString::number(kMaxGitCommits),
             QStringLiteral("--format=%at%x00%an%x00%ae"), resolvedHead});
        if (!log.ok) {
            commitCoverage = QStringLiteral("partial");
        } else {
            int seenCommits = 0;
            for (const QByteArray &line : log.output.split('\n')) {
                if (line.isEmpty())
                    continue;
                const QList<QByteArray> fields = line.split('\0');
                if (fields.size() != 3)
                    continue;
                bool timestampOk = false;
                const qint64 seconds = fields.at(0).toLongLong(&timestampOk);
                if (!timestampOk || seconds < 0 ||
                    seconds > std::numeric_limits<qint64>::max() / 1000)
                    continue;
                ++seenCommits;
                const QString authorName = normalizedName(
                    QString::fromUtf8(fields.at(1)));
                const QString authorEmail = normalizedEmail(
                    QString::fromUtf8(fields.at(2)));
                const bool matches = !configuredEmail.isEmpty()
                                         ? authorEmail == configuredEmail
                                         : authorName == configuredName;
                const qint64 timestampMs = seconds * 1000;
                if (!matches || !timestampInRange(timestampMs, input.capturedAtMs,
                                                  from, through))
                    continue;
                DayCounts &counts =
                    days[dayActorKey(utcDateForMs(timestampMs), input.publishingKey)];
                if (!incrementBounded(&counts.commits))
                    commitCoverage = QStringLiteral("partial");
            }
            if (seenCommits >= kMaxGitCommits)
                commitCoverage = QStringLiteral("partial");
        }
    }

    const QString pullRefName = QStringLiteral("refs/heads/forkmesh/pulls");
    const QString repositoryHeadBefore =
        resolvedCommit(source, QStringLiteral("HEAD"));
    const QString pullRefBefore = resolvedCommit(source, pullRefName);
    QString issueError;
    QString pullError;
    IssueStore issueStore(QString(), source, nullptr);
    PullStore pullStore(QString(), source, nullptr);
    QList<Issue> issues;
    QList<PullRequest> pulls;
    bool issuesReadFromHead = false;
    bool pullsReadFromDedicatedRef = false;
    bool pullsReadFromHead = false;

    if (repositoryHeadBefore == resolvedHead) {
        issuesReadFromHead = true;
        issues = issueStore.loadAllStrictAtRef(resolvedHead, &issueError);
    } else {
        issueError = QStringLiteral(
            "Issue metadata HEAD does not match the requested snapshot.");
    }

    if (!pullRefBefore.isEmpty()) {
        pullsReadFromDedicatedRef = true;
        pulls = pullStore.loadAllStrictAtRef(pullRefBefore, &pullError);
    } else if (repositoryHeadBefore == resolvedHead) {
        pullsReadFromHead = true;
        pulls = pullStore.loadAllStrictAtRef(resolvedHead, &pullError);
    } else {
        pullError = QStringLiteral(
            "Pull metadata has no ref matching the requested snapshot.");
    }

    invokeBeforePullMetadataRecheckHook();
    const QString repositoryHeadAfter =
        resolvedCommit(source, QStringLiteral("HEAD"));
    const QString pullRefAfter = resolvedCommit(source, pullRefName);

    if (!issuesReadFromHead || repositoryHeadAfter != resolvedHead) {
        issues.clear();
        if (issueError.isEmpty()) {
            issueError = QStringLiteral(
                "Issue metadata changed while the snapshot was being read.");
        }
    }

    const bool pullMetadataStable =
        (pullsReadFromDedicatedRef && pullRefAfter == pullRefBefore) ||
        (pullsReadFromHead && pullRefAfter.isEmpty() &&
         repositoryHeadAfter == resolvedHead);
    if (!pullMetadataStable) {
        pulls.clear();
        if (pullError.isEmpty()) {
            pullError = QStringLiteral(
                "Pull metadata changed while the snapshot was being read.");
        }
    }

    if (!issueError.isEmpty() || !pullError.isEmpty())
        collaborationCoverage = QStringLiteral("partial");

    QSet<QString> seenEvents;
    for (const Issue &issue : issues) {
        for (const IssueEvent &event : issue.events) {
            if (event.type != QLatin1String("open") ||
                !timestampInRange(event.ts, input.capturedAtMs, from, through))
                continue;
            QByteArray canonical;
            if (!verifiedIssueOpen(issue.number, event, &canonical))
                continue;
            const QString identity = eventIdentity(
                canonical, event.author, event.sig, QByteArrayLiteral("issue"));
            if (seenEvents.contains(identity))
                continue;
            seenEvents.insert(identity);
            DayCounts &counts = days[dayActorKey(utcDateForMs(event.ts), event.author)];
            if (!incrementBounded(&counts.issues))
                collaborationCoverage = QStringLiteral("partial");
        }
    }

    for (const PullRequest &pull : pulls) {
        if (timestampInRange(pull.ts, input.capturedAtMs, from, through)) {
            QByteArray canonical = PullStore::canonicalString(pull);
            bool verified =
                validActorKey(pull.author) && validSignature(pull.sig) &&
                ForkMeshIdentity::verifySignature(pull.author, pull.sig,
                                                  canonical);
            if (!verified && validActorKey(pull.author) &&
                validSignature(pull.sig)) {
                canonical = PullStore::legacyCanonicalString(pull);
                verified = ForkMeshIdentity::verifySignature(
                    pull.author, pull.sig, canonical);
            }
            if (verified) {
                const QString identity = eventIdentity(
                    canonical, pull.author, pull.sig, QByteArrayLiteral("pull"));
                if (!seenEvents.contains(identity)) {
                    seenEvents.insert(identity);
                    DayCounts &counts =
                        days[dayActorKey(utcDateForMs(pull.ts), pull.author)];
                    if (!incrementBounded(&counts.pulls))
                        collaborationCoverage = QStringLiteral("partial");
                }
            }
        }
        for (const PullEvent &event : pull.events) {
            if (event.type != QLatin1String("review") ||
                !timestampInRange(event.ts, input.capturedAtMs, from, through))
                continue;
            const QByteArray canonical =
                PullStore::canonicalString(pull.number, event);
            if (!validActorKey(event.author) || !validSignature(event.sig) ||
                !ForkMeshIdentity::verifySignature(event.author, event.sig,
                                                   canonical)) {
                continue;
            }
            const QString identity = eventIdentity(
                canonical, event.author, event.sig, QByteArrayLiteral("review"));
            if (seenEvents.contains(identity))
                continue;
            seenEvents.insert(identity);
            DayCounts &counts = days[dayActorKey(utcDateForMs(event.ts), event.author)];
            if (!incrementBounded(&counts.reviews))
                collaborationCoverage = QStringLiteral("partial");
        }
    }

    QMap<QString, ExtensionCounts> extensions;
    int fileCount = 0;
    struct LanguageCandidate {
        QString path;
        QString extension;
        QString oid;
        qint64 size = 0;
    };
    QList<LanguageCandidate> languageCandidates;
    QSet<QString> languageOids;
    QStringList languageOidOrder;
    qint64 languageBlobBytes = 0;
    const LanguageInspectionLimits languageLimits =
        currentLanguageInspectionLimits();
    const GitResult tree = runGit(
        source,
        {QStringLiteral("ls-tree"), QStringLiteral("-r"), QStringLiteral("-l"),
         QStringLiteral("-z"), resolvedHead});
    if (!tree.ok) {
        languageCoverage = QStringLiteral("partial");
    } else {
        for (const QByteArray &record : tree.output.split('\0')) {
            if (record.isEmpty())
                continue;
            const int tab = record.indexOf('\t');
            if (tab < 0)
                continue;
            const QList<QByteArray> meta = record.left(tab).simplified().split(' ');
            if (meta.size() < 4 || meta.at(1) != QByteArrayLiteral("blob"))
                continue;
            const QString path = QString::fromUtf8(record.mid(tab + 1));
            if (internalPath(path))
                continue;
            if (fileCount < kMaxCount)
                ++fileCount;
            else
                languageCoverage = QStringLiteral("partial");

            bool sizeOk = false;
            const qint64 size = meta.at(3).toLongLong(&sizeOk);
            const QString name = path.section('/', -1);
            const int dot = name.lastIndexOf('.');
            if (dot <= 0)
                continue;
            const QString extension = name.mid(dot + 1).toLower();
            if (!supportedExtensions().contains(extension))
                continue;
            if (!sizeOk || size < 0 || meta.at(2).isEmpty()) {
                languageCoverage = QStringLiteral("partial");
                continue;
            }
            if (generatedSource(path, QByteArray()))
                continue;
            if (languageCandidates.size() >= languageLimits.maxBlobs) {
                languageCoverage = QStringLiteral("partial");
                continue;
            }

            const QString oid = QString::fromUtf8(meta.at(2));
            if (!languageOids.contains(oid)) {
                if (size > languageLimits.maxBlobBytes -
                               languageBlobBytes) {
                    languageCoverage = QStringLiteral("partial");
                    continue;
                }
                languageOids.insert(oid);
                languageOidOrder.append(oid);
                languageBlobBytes += size;
            }
            languageCandidates.append({path, extension, oid, size});
        }

        if (!languageOids.isEmpty()) {
            QByteArray batchInput;
            for (const QString &oid : std::as_const(languageOidOrder))
                batchInput += oid.toUtf8() + '\n';
            const GitResult blobs =
                runGit(source,
                       {QStringLiteral("cat-file"),
                        QStringLiteral("--batch")},
                       &batchInput);
            QHash<QString, QByteArray> contentByOid;
            const bool inspectionComplete =
                blobs.ok &&
                parseBlobBatch(blobs.output, languageOids, &contentByOid);
            if (!inspectionComplete) {
                languageCoverage = QStringLiteral("partial");
            } else {
                for (const LanguageCandidate &candidate :
                     std::as_const(languageCandidates)) {
                    const QByteArray content =
                        contentByOid.value(candidate.oid);
                    if (content.size() != candidate.size) {
                        languageCoverage = QStringLiteral("partial");
                        continue;
                    }
                    if (content.contains('\0') ||
                        generatedSource(candidate.path, content)) {
                        continue;
                    }
                    ExtensionCounts &counts =
                        extensions[candidate.extension];
                    if (candidate.size >
                        qint64(kMaxCount - counts.bytes)) {
                        counts.bytes = kMaxCount;
                        languageCoverage = QStringLiteral("partial");
                    } else {
                        counts.bytes += int(candidate.size);
                    }
                    if (!incrementBounded(&counts.files))
                        languageCoverage = QStringLiteral("partial");
                }
            }
        }
    }
    if (trimExtensionLimit(&extensions))
        languageCoverage = QStringLiteral("partial");

    if (resolvedCommit(source, resolvedBranch) != resolvedHead) {
        return failedSnapshot(QStringLiteral(
            "Repository branch changed while the snapshot was being read."));
    }

    const RemovedCoverage boundedRows = trimDayLimit(&days);
    if (boundedRows.commits)
        commitCoverage = QStringLiteral("partial");
    if (boundedRows.collaboration)
        collaborationCoverage = QStringLiteral("partial");

    QJsonObject payload = makePayload(
        input, resolvedHead, from, through, commitCoverage, collaborationCoverage,
        languageCoverage, days, extensions, fileCount);
    QByteArray compact = compactPayload(payload);
    while (!encodedPayloadFits(compact) && !days.isEmpty()) {
        const RemovedCoverage removed = removeOldestDate(&days);
        if (removed.commits)
            commitCoverage = QStringLiteral("partial");
        if (removed.collaboration)
            collaborationCoverage = QStringLiteral("partial");
        payload = makePayload(input, resolvedHead, from, through, commitCoverage,
                              collaborationCoverage, languageCoverage, days,
                              extensions, fileCount);
        compact = compactPayload(payload);
    }
    while (!encodedPayloadFits(compact) && !extensions.isEmpty()) {
        removeSmallestExtension(&extensions);
        languageCoverage = QStringLiteral("partial");
        payload = makePayload(input, resolvedHead, from, through, commitCoverage,
                              collaborationCoverage, languageCoverage, days,
                              extensions, fileCount);
        compact = compactPayload(payload);
    }
    if (!encodedPayloadFits(compact)) {
        return failedSnapshot(
            QStringLiteral("Repository contribution snapshot exceeds the transport limit."));
    }

    RepoContributionSnapshot result;
    result.payload = payload;
    result.compactPayload = compact;
    result.changedFiles = changedFiles;
    result.complete = true;
    return result;
}

QString repoContributionDependencyFingerprint(
    const RepoContributionSnapshotInput &input)
{
    QString resolvedHead;
    QString resolvedBranch;
    const QString source =
        chooseSource(input, &resolvedHead, &resolvedBranch);
    if (source.isEmpty())
        return {};

    const QString pullMetadataHead = resolvedCommit(
        source, QStringLiteral("refs/heads/forkmesh/pulls"));
    const QString configuredEmail = normalizedEmail(configuredIdentityValue(
        input, source, QStringLiteral("user.email")));
    const QString configuredName = normalizedName(configuredIdentityValue(
        input, source, QStringLiteral("user.name")));

    QByteArray canonical =
        QByteArrayLiteral("forkmesh-contribution-dependencies-v1\n");
    auto appendField = [&canonical](const QString &value) {
        const QByteArray bytes = value.toUtf8();
        canonical += QByteArray::number(bytes.size()) + ':' + bytes + '\n';
    };
    appendField(pullMetadataHead);
    appendField(configuredEmail);
    appendField(configuredName);
    return QString::fromLatin1(
        QCryptographicHash::hash(canonical, QCryptographicHash::Sha256)
            .toHex());
}

RepoContributionPreparation prepareRepoContributionSnapshot(
    const RepoContributionSnapshotInput &input,
    const QString &expectedDependencyFingerprint,
    bool cachedSnapshotAvailable)
{
    RepoContributionPreparation preparation;
    preparation.dependencyFingerprint =
        repoContributionDependencyFingerprint(input);
    if (!preparation.dependencyFingerprint.isEmpty() &&
        cachedSnapshotAvailable &&
        preparation.dependencyFingerprint == expectedDependencyFingerprint) {
        return preparation;
    }

    RepoContributionSnapshot snapshot =
        buildRepoContributionSnapshot(input);
    const QString dependencyAfter =
        repoContributionDependencyFingerprint(input);
    if (dependencyAfter != preparation.dependencyFingerprint) {
        preparation.dependencyFingerprint = dependencyAfter;
        snapshot = failedSnapshot(QStringLiteral(
            "Repository contribution dependencies changed while the snapshot was being read."));
    }
    preparation.rebuiltSnapshot = std::move(snapshot);
    return preparation;
}

QJsonObject buildRepoLogoMetadata(const RepoLogoMetadataInput &input)
{


    constexpr qsizetype kMaxLogoTreeBytes = 2 * 1024 * 1024;
    constexpr int kMaxLogoTreeEntries = 4096;
    constexpr int kMaxTopLevelCandidates = 128;
    constexpr int kMaxTopLevelPublished = 24;
    constexpr int kMaxFrameworks = 12;
    constexpr int kMaxTopics = 12;

    QMap<QString, qint64> languages;
    const QJsonArray extensionRows =
        input.contributionPayload.value(QStringLiteral("extensions")).toArray();
    for (const QJsonValue &value : extensionRows) {
        const QJsonArray row = value.toArray();
        if (row.size() < 2)
            continue;
        addLogoLanguage(&languages, row.at(0).toString(),
                        qint64(row.at(1).toDouble()));
    }
    const bool reusedContributionLanguages = !languages.isEmpty();

    QSet<QString> topLevel;
    QSet<QString> frameworks;
    auto inspectPath = [&](const QString &rawPath, qint64 bytes) {
        const QString path = rawPath.left(240);
        if (path.isEmpty() || path == QLatin1String(".forkmesh") ||
            path.startsWith(QLatin1String(".forkmesh/"))) {
            return;
        }
        const QString first = path.section(QLatin1Char('/'), 0, 0);
        if (!first.isEmpty() && topLevel.size() < kMaxTopLevelCandidates)
            topLevel.insert(path.contains(QLatin1Char('/')) ? first + QLatin1Char('/')
                                                           : first);

        const QString lower = path.toLower();
        const QString base = lower.section(QLatin1Char('/'), -1);
        if (base == QLatin1String("package.json"))
            frameworks.insert(QStringLiteral("Node.js"));
        if (base.startsWith(QLatin1String("next.config.")))
            frameworks.insert(QStringLiteral("Next.js"));
        if (base.startsWith(QLatin1String("vite.config.")))
            frameworks.insert(QStringLiteral("Vite"));
        if (base == QLatin1String("wrangler.toml") ||
            base == QLatin1String("wrangler.jsonc"))
            frameworks.insert(QStringLiteral("Cloudflare Workers"));
        if (base == QLatin1String("cmakelists.txt"))
            frameworks.insert(QStringLiteral("CMake"));
        if (base.endsWith(QLatin1String(".pro")))
            frameworks.insert(QStringLiteral("Qt"));
        if (base == QLatin1String("pyproject.toml") ||
            base == QLatin1String("setup.py"))
            frameworks.insert(QStringLiteral("Python packaging"));
        if (base == QLatin1String("cargo.toml"))
            frameworks.insert(QStringLiteral("Cargo"));
        if (base == QLatin1String("go.mod"))
            frameworks.insert(QStringLiteral("Go modules"));
        if (base == QLatin1String("pubspec.yaml"))
            frameworks.insert(QStringLiteral("Flutter"));
        if (base == QLatin1String("composer.json"))
            frameworks.insert(QStringLiteral("Composer"));
        if (base == QLatin1String("gemfile"))
            frameworks.insert(QStringLiteral("Ruby Bundler"));
        if (base == QLatin1String("pom.xml") ||
            base.startsWith(QLatin1String("build.gradle")))
            frameworks.insert(QStringLiteral("JVM build"));
        if (base == QLatin1String("dockerfile") ||
            base.startsWith(QLatin1String("docker-compose.")))
            frameworks.insert(QStringLiteral("Docker"));

        if (!reusedContributionLanguages) {
            const QString name = path.section(QLatin1Char('/'), -1);
            const int dot = name.lastIndexOf(QLatin1Char('.'));
            if (dot > 0)
                addLogoLanguage(&languages, name.mid(dot + 1), bytes);
        }
    };

    const QStringList sources{input.workTreePath, input.mirrorPath};
    for (const QString &source : sources) {
        if (source.trimmed().isEmpty() || input.head.trimmed().isEmpty())
            continue;
        const GitResult tree =
            runGit(source,
                   {QStringLiteral("ls-tree"), QStringLiteral("-r"),
                    QStringLiteral("-l"), QStringLiteral("-z"), input.head},
                   nullptr, kMaxLogoTreeBytes, 10000);
        if (!tree.ok)
            continue;
        int entries = 0;
        for (const QByteArray &record : tree.output.split('\0')) {
            if (record.isEmpty() || entries++ >= kMaxLogoTreeEntries)
                break;
            const int tab = record.indexOf('\t');
            if (tab < 0)
                continue;
            const QList<QByteArray> fields =
                record.left(tab).simplified().split(' ');
            if (fields.size() < 4 || fields.at(1) != QByteArrayLiteral("blob"))
                continue;
            bool sizeOk = false;
            const qint64 bytes = fields.at(3).toLongLong(&sizeOk);
            inspectPath(QString::fromUtf8(record.mid(tab + 1)),
                        sizeOk ? qMax<qint64>(1, bytes) : 1);
        }
        break;
    }

    const QString primaryLanguage = input.primaryLanguage.trimmed().left(80);
    if (!primaryLanguage.isEmpty() && !languages.contains(primaryLanguage))
        languages.insert(primaryLanguage, 1);

    QList<QPair<QString, qint64>> rankedLanguages;
    for (auto it = languages.constBegin(); it != languages.constEnd(); ++it)
        rankedLanguages.append({it.key().left(80), it.value()});
    std::sort(rankedLanguages.begin(), rankedLanguages.end(),
              [](const auto &left, const auto &right) {
                  if (left.second != right.second)
                      return left.second > right.second;
                  return left.first < right.first;
              });
    QJsonObject languageObject;
    for (int i = 0; i < qMin(12, rankedLanguages.size()); ++i)
        languageObject.insert(rankedLanguages.at(i).first,
                              double(rankedLanguages.at(i).second));

    QStringList structure = topLevel.values();
    std::sort(structure.begin(), structure.end(),
              [](const QString &left, const QString &right) {
                  return left.compare(right, Qt::CaseInsensitive) < 0;
              });
    structure = structure.mid(0, kMaxTopLevelPublished);

    QStringList frameworkList = frameworks.values();
    std::sort(frameworkList.begin(), frameworkList.end(),
              [](const QString &left, const QString &right) {
                  return left.compare(right, Qt::CaseInsensitive) < 0;
              });
    frameworkList = frameworkList.mid(0, kMaxFrameworks);

    QStringList topics;
    for (const QString &topic : input.topics) {
        const QString bounded = topic.trimmed().left(80);
        if (!bounded.isEmpty() && !topics.contains(bounded))
            topics.append(bounded);
        if (topics.size() >= kMaxTopics)
            break;
    }

    QJsonArray structureJson;
    for (const QString &item : std::as_const(structure))
        structureJson.append(item);
    QJsonArray frameworkJson;
    for (const QString &item : std::as_const(frameworkList))
        frameworkJson.append(item);
    QJsonArray topicsJson;
    for (const QString &item : std::as_const(topics))
        topicsJson.append(item);

    const QString category = logoProjectCategory(
        input.description.left(500), topics, frameworks, topLevel);
    return {
        {QStringLiteral("description"), input.description.trimmed().left(500)},
        {QStringLiteral("languages"), languageObject},
        {QStringLiteral("topics"), topicsJson},
        {QStringLiteral("fileStructure"), structureJson},
        {QStringLiteral("frameworks"), frameworkJson},
        {QStringLiteral("projectCategory"), category.left(80)},
    };
}

bool repoContributionResponseNeedsRefresh(const QJsonObject &response,
                                          bool contributionSubmitted)
{
    return contributionSubmitted &&
           response.contains(QStringLiteral("contributionsAccepted")) &&
           !response.value(QStringLiteral("contributionsAccepted")).toBool();
}

QByteArray profileContributionCanonical(
    const QString &owner, const QString &repo, const QString &updatedAt,
    const QByteArray &compactPayload)
{
    const QByteArray digest =
        QCryptographicHash::hash(compactPayload, QCryptographicHash::Sha256).toHex();
    return QByteArrayLiteral("forkmesh-profile-contribution-v1\n") + owner.toUtf8() +
           QByteArrayLiteral("\n") + repo.toUtf8() + QByteArrayLiteral("\n") +
           updatedAt.toUtf8() + QByteArrayLiteral("\n") + digest;
}

QJsonObject signedContributionFields(
    const RepoContributionSnapshot &snapshot, const QString &owner,
    const QString &repo, const QString &updatedAt,
    const ForkMeshIdentity &identity)
{
    constexpr qint64 kMaxJsonSafeInteger = 9007199254740991LL;
    static const QRegularExpression segmentPattern(
        QStringLiteral("^[A-Za-z0-9._:-]{1,80}$"));
    static const QRegularExpression updatedAtPattern(
        QStringLiteral("^(?:0|[1-9][0-9]{0,15})$"));

    if (!snapshot.complete || !snapshot.error.isEmpty() ||
        snapshot.compactPayload.isEmpty() || !identity.isValid() ||
        !segmentPattern.match(owner).hasMatch() ||
        !segmentPattern.match(repo).hasMatch() ||
        !updatedAtPattern.match(updatedAt).hasMatch()) {
        return {};
    }

    bool updatedAtOk = false;
    const qint64 updatedAtMs = updatedAt.toLongLong(&updatedAtOk);
    const QJsonValue capturedAtValue =
        snapshot.payload.value(QStringLiteral("capturedAt"));
    if (!updatedAtOk || updatedAtMs < 0 ||
        updatedAtMs > kMaxJsonSafeInteger || !capturedAtValue.isDouble()) {
        return {};
    }
    const double capturedAt = capturedAtValue.toDouble();
    if (!std::isfinite(capturedAt) || std::floor(capturedAt) != capturedAt ||
        capturedAt < 0 || capturedAt > double(kMaxJsonSafeInteger) ||
        qint64(capturedAt) != updatedAtMs) {
        return {};
    }

    const QByteArray compact =
        QJsonDocument(snapshot.payload).toJson(QJsonDocument::Compact);
    if (compact != snapshot.compactPayload)
        return {};
    const QByteArray encoded = snapshot.compactPayload.toBase64(
        QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals);
    if (encoded.isEmpty() || encoded.size() > kMaxPayloadEncoded)
        return {};

    const QByteArray canonical = profileContributionCanonical(
        owner, repo, updatedAt, snapshot.compactPayload);
    const QString signature = identity.signData(canonical);
    if (signature.isEmpty() ||
        !ForkMeshIdentity::verifySignature(identity.publicKey(), signature,
                                           canonical)) {
        return {};
    }
    return QJsonObject{
        {QStringLiteral("contributionPayload"), QString::fromLatin1(encoded)},
        {QStringLiteral("contributionSig"), signature}};
}

RepoContributionPublicationCache::RepoContributionPublicationCache(
    int maxEntries, int maxInFlight)
    : m_maxEntries(qMax(1, maxEntries)),
      m_maxInFlight(qMax(1, maxInFlight))
{
}

QString RepoContributionPublicationCache::key(
    const QString &owner, const QString &repo, const QString &head,
    const QString &branch, const QString &accountPublicKey,
    const QString &dependencyFingerprint)
{
    QByteArray canonical =
        QByteArrayLiteral("forkmesh-contribution-cache-v1\n");
    auto appendField = [&canonical](const QString &value) {
        const QByteArray bytes = value.toUtf8();
        canonical += QByteArray::number(bytes.size()) + ':' + bytes + '\n';
    };
    appendField(owner);
    appendField(repo);
    appendField(head);
    appendField(branch);
    appendField(accountPublicKey);
    appendField(dependencyFingerprint);
    return QString::fromLatin1(
        QCryptographicHash::hash(canonical, QCryptographicHash::Sha256)
            .toHex());
}

std::optional<RepoContributionSnapshot>
RepoContributionPublicationCache::lookup(const QString &key,
                                         qint64 nowMs) const
{
    if (!m_results.contains(key) || !m_storedAtMs.contains(key))
        return std::nullopt;
    const RepoContributionSnapshot snapshot = m_results.value(key);
    const bool success = snapshot.complete && snapshot.error.isEmpty();
    const qint64 ttlMs = success ? 6LL * 60 * 60 * 1000
                                 : 5LL * 60 * 1000;
    const qint64 ageMs = qMax<qint64>(0, nowMs - m_storedAtMs.value(key));
    if (ageMs >= ttlMs)
        return std::nullopt;
    return snapshot;
}

RepoContributionPublicationCache::BeginResult
RepoContributionPublicationCache::begin(const QString &key,
                                        bool requestDialog)
{
    if (key.isEmpty())
        return BeginResult::CapacityExceeded;
    if (m_inFlight.contains(key)) {
        if (requestDialog)
            m_dialogRequested.insert(key);
        return BeginResult::Coalesced;
    }
    if (m_inFlight.size() >= m_maxInFlight)
        return BeginResult::CapacityExceeded;
    if (requestDialog)
        m_dialogRequested.insert(key);
    m_inFlight.insert(key);
    return BeginResult::Started;
}

bool RepoContributionPublicationCache::finish(
    const QString &key, RepoContributionSnapshot snapshot, qint64 nowMs)
{
    const Completion completion = complete(key);
    if (completion.discarded)
        return completion.requestDialog;
    store(key, std::move(snapshot), nowMs);
    return completion.requestDialog;
}

RepoContributionPublicationCache::Completion
RepoContributionPublicationCache::complete(const QString &key)
{
    Completion completion;
    m_inFlight.remove(key);
    completion.requestDialog = m_dialogRequested.remove(key) > 0;
    completion.discarded = m_discardOnFinish.remove(key) > 0;
    return completion;
}

void RepoContributionPublicationCache::store(
    const QString &key, RepoContributionSnapshot snapshot, qint64 nowMs)
{
    if (key.isEmpty())
        return;
    if (!snapshot.complete || !snapshot.error.isEmpty()) {
        snapshot.complete = false;
        snapshot.payload = {};
        snapshot.compactPayload.clear();
        snapshot.error = snapshot.error.simplified().left(240);
        if (snapshot.error.isEmpty()) {
            snapshot.error = QStringLiteral(
                "Could not build the repository contribution snapshot.");
        }
    }
    m_results.insert(key, std::move(snapshot));
    m_storedAtMs.insert(key, nowMs);
    evictOldest();
}

bool RepoContributionPublicationCache::inFlight(const QString &key) const
{
    return m_inFlight.contains(key);
}

void RepoContributionPublicationCache::invalidate(const QString &key)
{
    if (key.isEmpty())
        return;
    m_results.remove(key);
    m_storedAtMs.remove(key);
    if (m_inFlight.contains(key))
        m_discardOnFinish.insert(key);
}

bool RepoContributionPublicationCache::claimStaleRetry(
    const QString &publishKey, const QString &snapshotKey)
{
    if (publishKey.isEmpty() || snapshotKey.isEmpty() ||
        m_staleRetrySnapshotKey.value(publishKey) == snapshotKey) {
        return false;
    }
    m_staleRetrySnapshotKey.insert(publishKey, snapshotKey);
    return true;
}

void RepoContributionPublicationCache::clearStaleRetry(
    const QString &publishKey)
{
    m_staleRetrySnapshotKey.remove(publishKey);
}

void RepoContributionPublicationCache::evictOldest()
{
    while (m_results.size() > m_maxEntries) {
        QString oldestKey;
        qint64 oldestTime = std::numeric_limits<qint64>::max();
        for (auto it = m_storedAtMs.constBegin();
             it != m_storedAtMs.constEnd(); ++it) {
            if (it.value() < oldestTime ||
                (it.value() == oldestTime &&
                 (oldestKey.isEmpty() || it.key() < oldestKey))) {
                oldestKey = it.key();
                oldestTime = it.value();
            }
        }
        if (oldestKey.isEmpty())
            break;
        m_results.remove(oldestKey);
        m_storedAtMs.remove(oldestKey);
    }
}
