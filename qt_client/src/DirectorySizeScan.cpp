#include "DirectorySizeScan.h"

#include <QByteArray>
#include <QDataStream>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <algorithm>

#ifdef Q_OS_UNIX
#include <unistd.h>
#endif

namespace forkmesh {
namespace {

constexpr int kUnreadableSampleLimit = 6;
constexpr quint32 kScanResultMagic = 0x464d535a; // "FMSZ"
constexpr qint32 kScanResultVersion = 1;

// A directory needs both read (to list it) and execute (to stat what is in
// it); missing either is what turns a scan of "/" into a handful of slices for
// an unprivileged user.
bool canDescend(const QFileInfo &info)
{
    return info.isReadable() && info.isExecutable();
}

void scanInto(const QString &path, int depth,
              const DirectorySizeScanOptions &options,
              DirectorySizeScanResult &result, SunburstNode &node)
{
    const QFileInfoList entries = QDir(path).entryInfoList(
        QDir::Dirs | QDir::Files | QDir::NoDotAndDotDot | QDir::Hidden |
        QDir::System | QDir::NoSymLinks);
    for (const QFileInfo &info : entries) {
        const QString absolute = info.absoluteFilePath();
        if (!options.pruned.isEmpty() && options.pruned.contains(absolute))
            continue;
        if (info.isDir()) {
            if (info.fileName() == QLatin1String(".git"))
                continue;
            SunburstNode child;
            child.name = info.fileName();
            if (canDescend(info)) {
                scanInto(absolute, depth + 1, options, result, child);
            } else {
                // Counted, not descended: the rescan-as-administrator offer is
                // built from exactly these.
                ++result.unreadableDirs;
                if (result.unreadableSample.size() < kUnreadableSampleLimit)
                    result.unreadableSample.append(absolute);
            }
            node.size += child.size;
            node.fileCount += child.fileCount;
            if (depth < options.maxDepth && child.size > 0)
                node.children.append(std::move(child));
        } else {
            node.size += info.size();
            node.fileCount += 1;
            if (depth < options.maxDepth && info.size() > 0) {
                SunburstNode leaf;
                leaf.name = info.fileName();
                leaf.size = info.size();
                leaf.fileCount = 1;
                node.children.append(std::move(leaf));
            }
        }
    }
    std::sort(node.children.begin(), node.children.end(),
              [](const SunburstNode &a, const SunburstNode &b) {
                  return a.size > b.size;
              });
}

void writeNode(QDataStream &stream, const SunburstNode &node)
{
    stream << node.name << node.size << qint32(node.fileCount)
           << qint32(node.children.size());
    for (const SunburstNode &child : node.children)
        writeNode(stream, child);
}

bool readNode(QDataStream &stream, SunburstNode &node, int depth)
{
    if (depth > kSizeMapMaxDepth + 1)
        return false; // a malformed stream can't be walked into the stack
    qint32 fileCount = 0;
    qint32 childCount = 0;
    stream >> node.name >> node.size >> fileCount >> childCount;
    if (stream.status() != QDataStream::Ok || childCount < 0)
        return false;
    node.fileCount = fileCount;
    node.children.reserve(childCount);
    for (qint32 i = 0; i < childCount; ++i) {
        SunburstNode child;
        if (!readNode(stream, child, depth + 1))
            return false;
        node.children.append(std::move(child));
    }
    return true;
}

// mountinfo fields are escaped octally for the four characters that would
// otherwise break the space-separated layout.
QString unescapeMountField(const QString &field)
{
    QString out;
    out.reserve(field.size());
    for (int i = 0; i < field.size(); ++i) {
        if (field.at(i) == QLatin1Char('\\') && i + 3 < field.size()) {
            bool ok = false;
            const int code = QStringView(field).mid(i + 1, 3).toInt(&ok, 8);
            if (ok) {
                out.append(QChar(code));
                i += 3;
                continue;
            }
        }
        out.append(field.at(i));
    }
    return out;
}

} // namespace

DirectorySizeScanResult scanDirectorySizes(const QString &path,
                                           const DirectorySizeScanOptions &options)
{
    DirectorySizeScanResult result;
    result.root.name = QFileInfo(path).fileName();
    if (!canDescend(QFileInfo(path))) {
        result.unreadableDirs = 1;
        result.unreadableSample.append(QDir::cleanPath(path));
        return result;
    }
    scanInto(path, 0, options, result, result.root);
    return result;
}

QSet<QString> mountPointsFromMountTable(const QByteArray &table)
{
    QSet<QString> mounts;
    for (const QByteArray &rawLine : table.split('\n')) {
        const QString line = QString::fromUtf8(rawLine).trimmed();
        if (line.isEmpty())
            continue;
        const QStringList fields = line.split(QLatin1Char(' '), Qt::SkipEmptyParts);
        // mountinfo: "36 25 0:29 / /proc rw,… - proc proc rw" — the mount point
        // is field 5. mtab/proc-mounts: "proc /proc proc rw,… 0 0" — field 2.
        // The separating "-" only exists in mountinfo, so it tells them apart.
        const bool mountInfo = fields.contains(QStringLiteral("-"));
        const int index = mountInfo ? 4 : 1;
        if (fields.size() <= index)
            continue;
        const QString mount = unescapeMountField(fields.at(index));
        if (!mount.startsWith(QLatin1Char('/')))
            continue;
        mounts.insert(QDir::cleanPath(mount));
    }
    return mounts;
}

QSet<QString> systemMountPoints()
{
    for (const QString &source : {QStringLiteral("/proc/self/mountinfo"),
                                  QStringLiteral("/proc/mounts")}) {
        QFile file(source);
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
            continue;
        const QSet<QString> mounts = mountPointsFromMountTable(file.readAll());
        if (!mounts.isEmpty())
            return mounts;
    }
    return {};
}

QByteArray encodeScanRequest(const QString &path,
                             const DirectorySizeScanOptions &options)
{
    QJsonArray pruned;
    for (const QString &entry : options.pruned)
        pruned.append(entry);
    const QJsonObject request{{QStringLiteral("path"), path},
                              {QStringLiteral("maxDepth"), options.maxDepth},
                              {QStringLiteral("pruned"), pruned}};
    return QJsonDocument(request).toJson(QJsonDocument::Compact);
}

bool decodeScanRequest(const QByteArray &payload, QString *path,
                       DirectorySizeScanOptions *options)
{
    const QJsonObject request = QJsonDocument::fromJson(payload).object();
    const QString requested = request.value(QStringLiteral("path")).toString();
    if (requested.isEmpty())
        return false;
    if (path)
        *path = requested;
    if (options) {
        const int depth =
            request.value(QStringLiteral("maxDepth")).toInt(kSizeMapMaxDepth);
        options->maxDepth = qBound(0, depth, kSizeMapMaxDepth);
        options->pruned.clear();
        const QJsonArray pruned = request.value(QStringLiteral("pruned")).toArray();
        for (const QJsonValue &value : pruned) {
            const QString entry = value.toString();
            if (!entry.isEmpty())
                options->pruned.insert(entry);
        }
    }
    return true;
}

QByteArray encodeScanResult(const DirectorySizeScanResult &result)
{
    QByteArray payload;
    QDataStream stream(&payload, QIODevice::WriteOnly);
    stream.setVersion(QDataStream::Qt_6_0);
    stream << kScanResultMagic << kScanResultVersion
           << qint32(result.unreadableDirs) << result.unreadableSample;
    writeNode(stream, result.root);
    return payload;
}

bool decodeScanResult(const QByteArray &payload, DirectorySizeScanResult *result)
{
    QDataStream stream(payload);
    stream.setVersion(QDataStream::Qt_6_0);
    quint32 magic = 0;
    qint32 version = 0;
    qint32 unreadable = 0;
    QStringList sample;
    stream >> magic >> version >> unreadable >> sample;
    if (stream.status() != QDataStream::Ok || magic != kScanResultMagic ||
        version != kScanResultVersion)
        return false;
    DirectorySizeScanResult decoded;
    decoded.unreadableDirs = unreadable;
    decoded.unreadableSample = sample;
    if (!readNode(stream, decoded.root, 0))
        return false;
    if (result)
        *result = std::move(decoded);
    return true;
}

bool runningAsRoot()
{
#ifdef Q_OS_UNIX
    return ::geteuid() == 0;
#else
    return false;
#endif
}

} // namespace forkmesh
