#include "MirrorActionsSummary.h"

#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>

#include <algorithm>
#include <cerrno>

#if defined(Q_OS_UNIX)
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace forkmesh::mirror_actions {
namespace {

constexpr qint64 kMaximumSafeJsonInteger = (qint64(1) << 53) - 1;
constexpr qsizetype kMaximumSummaryBytes = 256 * 1024;
constexpr qint64 kMaximumSummaryClockSkewMilliseconds = 60 * 1000;
constexpr auto kSummaryType = "forkmesh.mirror-actions-summary";
constexpr auto kSummaryFileName = "actions-summary.json";

const QRegularExpression kNode(
    QStringLiteral("^[a-z](?:[a-z0-9-]{0,61}[a-z0-9])?$"));
const QRegularExpression kSegment(
    QStringLiteral("^[A-Za-z0-9._-]{1,100}$"));
const QRegularExpression kCommit(
    QStringLiteral("^(?:[0-9a-f]{40}|[0-9a-f]{64})$"));
const QRegularExpression kRef(
    QStringLiteral(
        "^refs/(?:heads|tags)/(?!/)(?!.*(?:\\.\\.|//))"
        "[A-Za-z0-9._/-]{1,160}(?<!/)$"));

QString boundedSingleLine(const QString &value, qsizetype maximum)
{
    QString result;
    result.reserve(qMin(value.size(), maximum));
    for (QChar character : value) {
        const ushort code = character.unicode();
        if (character == QLatin1Char('\r') ||
            character == QLatin1Char('\n') ||
            character == QLatin1Char('\t')) {
            if (!result.isEmpty() &&
                !result.endsWith(QLatin1Char(' '))) {
                result += QLatin1Char(' ');
            }
        } else if (code >= 0x20 && !(code >= 0x7f && code <= 0x9f)) {
            result += character;
        }
        if (result.size() >= maximum)
            break;
    }
    return result.simplified();
}

QString boundedLogTail(
    const QString &value,
    qsizetype maximumBytes = kMaximumSummaryLogTailBytes)
{
    QString sanitized;
    sanitized.reserve(value.size());
    for (QChar character : value) {
        const ushort code = character.unicode();
        if (character == QLatin1Char('\r') ||
            character == QLatin1Char('\n')) {
            sanitized += QLatin1Char('\n');
        } else if (character == QLatin1Char('\t') ||
                   (code >= 0x20 && !(code >= 0x7f && code <= 0x9f))) {
            sanitized += character;
        }
    }

    QByteArray encoded = sanitized.toUtf8();
    if (encoded.size() > maximumBytes) {
        encoded = encoded.right(maximumBytes);
        while (!encoded.isEmpty() &&
               (static_cast<unsigned char>(encoded.front()) & 0xc0) == 0x80) {
            encoded.remove(0, 1);
        }
    }
    // Round-trip through UTF-8 so even a malformed surrogate in an in-memory
    // QString becomes a valid replacement character before JSON serialization.
    QString tail = QString::fromUtf8(encoded);
    encoded.fill('\0');
    encoded.clear();
    while (tail.toUtf8().size() > maximumBytes)
        tail.remove(0, 1);
    return tail;
}

bool validTimestamp(qint64 value, bool allowZero)
{
    return value >= (allowZero ? 0 : 1) &&
           value <= kMaximumSafeJsonInteger;
}

bool safeSummaryPath(const QString &path)
{
    if (path.size() < 2 || path.size() > 4096 ||
        path.contains(QChar::Null) ||
        path.contains(QLatin1Char('\r')) ||
        path.contains(QLatin1Char('\n'))) {
        return false;
    }
    const QFileInfo target(path);
    if (!target.isAbsolute() ||
        QDir::cleanPath(path) != target.absoluteFilePath() ||
        target.fileName() != QLatin1String(kSummaryFileName) ||
        target.isSymLink()) {
        return false;
    }
    const QFileInfo parent(target.absolutePath());
    if (!parent.isDir() || parent.isSymLink() ||
        parent.canonicalFilePath() !=
            QDir::cleanPath(parent.absoluteFilePath())) {
        return false;
    }

#if defined(Q_OS_UNIX)
    struct stat parentState {};
    const QByteArray parentPath =
        QFile::encodeName(parent.absoluteFilePath());
    if (::lstat(parentPath.constData(), &parentState) != 0 ||
        !S_ISDIR(parentState.st_mode) ||
        parentState.st_uid != ::geteuid() ||
        (parentState.st_mode & 0077) != 0 ||
        (parentState.st_mode & 0700) != 0700) {
        return false;
    }
    struct stat targetState {};
    const QByteArray targetPath = QFile::encodeName(path);
    if (::lstat(targetPath.constData(), &targetState) == 0) {
        if (!S_ISREG(targetState.st_mode) ||
            targetState.st_nlink != 1 ||
            targetState.st_uid != ::geteuid() ||
            (targetState.st_mode & 0077) != 0) {
            return false;
        }
    } else if (errno != ENOENT) {
        return false;
    }
#else
    const QFileDevice::Permissions unsafe =
        QFileDevice::ReadGroup | QFileDevice::WriteGroup |
        QFileDevice::ExeGroup | QFileDevice::ReadOther |
        QFileDevice::WriteOther | QFileDevice::ExeOther;
    if (target.exists() &&
        (!target.isFile() || (target.permissions() & unsafe))) {
        return false;
    }
#endif
    return true;
}

} // namespace

QJsonObject buildSummary(const QString &node,
                         const QList<ActionRun> &runs,
                         const ActionStore &store,
                         qint64 nowMs)
{
    if (!kNode.match(node).hasMatch() ||
        !validTimestamp(nowMs, false) ||
        nowMs > kMaximumSafeJsonInteger - kSummaryLeaseMilliseconds) {
        return {};
    }

    QList<ActionRun> ordered = runs;
    std::sort(ordered.begin(), ordered.end(),
              [](const ActionRun &left, const ActionRun &right) {
                  if (left.createdAtMs != right.createdAtMs)
                      return left.createdAtMs > right.createdAtMs;
                  return left.id > right.id;
              });
    const QSet<QString> statuses{
        ActionStatus::AwaitingApproval,
        ActionStatus::Queued,
        ActionStatus::Running,
        ActionStatus::Success,
        ActionStatus::Failed,
        ActionStatus::Rejected,
        ActionStatus::Cancelled,
        ActionStatus::Skipped,
    };
    QJsonArray serialized;
    for (const ActionRun &run : std::as_const(ordered)) {
        if (serialized.size() >= kMaximumSummaryRuns)
            break;
        const QString workflow =
            boundedSingleLine(run.workflowName, 160);
        const QString ref = run.ref.trimmed();
        const QString commit = run.commit.trimmed().toLower();
        if (run.id <= 0 ||
            !kSegment.match(run.owner).hasMatch() ||
            !kSegment.match(run.name).hasMatch() ||
            workflow.isEmpty() ||
            !kCommit.match(commit).hasMatch() ||
            !kRef.match(ref).hasMatch() ||
            !statuses.contains(run.status) ||
            !validTimestamp(run.createdAtMs, false) ||
            !validTimestamp(run.startedAtMs, true) ||
            !validTimestamp(run.finishedAtMs, true) ||
            run.createdAtMs >
                nowMs + kMaximumSummaryClockSkewMilliseconds ||
            run.startedAtMs >
                nowMs + kMaximumSummaryClockSkewMilliseconds ||
            run.finishedAtMs >
                nowMs + kMaximumSummaryClockSkewMilliseconds) {
            continue;
        }
        serialized.append(QJsonObject{
            {QStringLiteral("id"), run.id},
            {QStringLiteral("owner"), run.owner},
            {QStringLiteral("repository"), run.name},
            {QStringLiteral("workflow"), workflow},
            {QStringLiteral("commit"), commit},
            {QStringLiteral("ref"), ref},
            {QStringLiteral("status"), run.status},
            {QStringLiteral("createdAt"), run.createdAtMs},
            {QStringLiteral("startedAt"), run.startedAtMs},
            {QStringLiteral("finishedAt"), run.finishedAtMs},
            {QStringLiteral("logTail"),
             boundedLogTail(store.readLog(run))},
        });
    }

    QJsonObject result{
        {QStringLiteral("schemaVersion"), 1},
        {QStringLiteral("type"), QLatin1String(kSummaryType)},
        {QStringLiteral("node"), node},
        {QStringLiteral("updatedAt"), nowMs},
        {QStringLiteral("expiresAt"),
         nowMs + kSummaryLeaseMilliseconds},
        {QStringLiteral("runs"), serialized},
    };
    // The gateway's complete owner-only input is capped at 256 KiB. Preserve
    // the newest run tails at their full 16 KiB where possible, trimming older
    // tails only when the complete exact JSON document would exceed that
    // independent bound.
    QByteArray encoded =
        QJsonDocument(result).toJson(QJsonDocument::Compact);
    QJsonArray fittedRuns = serialized;
    for (int index = fittedRuns.size() - 1;
         encoded.size() + 1 > kMaximumSummaryBytes && index >= 0;
         --index) {
        QJsonObject item = fittedRuns.at(index).toObject();
        const QString current =
            item.value(QStringLiteral("logTail")).toString();
        const qsizetype currentBytes = current.toUtf8().size();
        const qsizetype excess =
            encoded.size() + 1 - kMaximumSummaryBytes;
        const qsizetype keep =
            qMax<qsizetype>(0, currentBytes - excess - 64);
        item.insert(QStringLiteral("logTail"),
                    boundedLogTail(current, keep));
        fittedRuns.replace(index, item);
        result.insert(QStringLiteral("runs"), fittedRuns);
        encoded = QJsonDocument(result).toJson(QJsonDocument::Compact);
    }
    encoded.fill('\0');
    if (QJsonDocument(result).toJson(QJsonDocument::Compact).size() + 1 >
        kMaximumSummaryBytes) {
        return {};
    }
    return result;
}

bool writeSummaryFile(const QString &path,
                      const QString &node,
                      const QList<ActionRun> &runs,
                      const ActionStore &store,
                      qint64 nowMs)
{
    if (!safeSummaryPath(path))
        return false;
    const QJsonObject summary = buildSummary(node, runs, store, nowMs);
    if (summary.isEmpty())
        return false;
    QByteArray encoded =
        QJsonDocument(summary).toJson(QJsonDocument::Compact);
    encoded.append('\n');
    if (encoded.size() > kMaximumSummaryBytes) {
        encoded.fill('\0');
        return false;
    }

    QSaveFile file(path);
    file.setDirectWriteFallback(false);
    if (!file.open(QIODevice::WriteOnly) ||
        !file.setPermissions(QFileDevice::ReadOwner |
                             QFileDevice::WriteOwner) ||
        file.write(encoded) != encoded.size() ||
        !file.commit()) {
        encoded.fill('\0');
        return false;
    }
    encoded.fill('\0');
    if (!QFile::setPermissions(path, QFileDevice::ReadOwner |
                                        QFileDevice::WriteOwner) ||
        !safeSummaryPath(path)) {
        return false;
    }
    return true;
}

} // namespace forkmesh::mirror_actions
