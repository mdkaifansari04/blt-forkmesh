#include "LogSource.h"

#include <QDir>
#include <QFileInfo>
#include <QLatin1String>
#include <QRegularExpression>

#include <utility>

namespace forkmesh {
namespace {

// This translation unit's own compile-time path. Everything else about a
// location is derived from it: the checkout it names is the prefix every other
// __builtin_FILE() in the build shares, which is exactly the part worth
// stripping — and getting it this way costs no CMake definitions and cannot
// drift out of step with where the sources actually are.
const char *const kThisFile = __FILE__;
const QLatin1String kThisFileSuffix("qt_client/src/LogSource.cpp");

// Windows compilers report backslashes; the stored form is '/' everywhere so a
// log written on one platform still reads (and resolves) on another.
QString withForwardSlashes(const QString &raw)
{
    QString path = raw;
    path.replace(QLatin1Char('\\'), QLatin1Char('/'));
    return path;
}

} // namespace

QString logSourceBuildRoot()
{
    static const QString root = [] {
        const QString self = withForwardSlashes(QString::fromUtf8(kThisFile));
        const int at = self.lastIndexOf(kThisFileSuffix);
        // <= 0 means the compiler reported a relative path: there is no
        // checkout prefix to strip, and paths are already short.
        return at <= 0 ? QString() : self.left(at - 1);
    }();
    return root;
}

QString logSourceRelativePath(const char *absolutePath)
{
    if (!absolutePath || !*absolutePath)
        return QString();
    // Cleaned first: a header included as "../src/MainWindow.h" reaches the
    // compiler builtin with that hop still in it ("…/qt_client/tests/../src/
    // MainWindow.h"), which is neither readable in a log entry nor comparable
    // against the checkout prefix.
    const QString path = QDir::cleanPath(
        withForwardSlashes(QString::fromUtf8(absolutePath)));
    const QString root = logSourceBuildRoot();
    if (!root.isEmpty() && path.size() > root.size() + 1 &&
        path.at(root.size()) == QLatin1Char('/') && path.startsWith(root))
        return path.mid(root.size() + 1);
    // Outside the checkout (a generated file, a header from a sibling tree):
    // the last two components still say enough to identify it.
    const int lastSlash = path.lastIndexOf(QLatin1Char('/'));
    if (lastSlash <= 0)
        return path;
    const int prevSlash = path.lastIndexOf(QLatin1Char('/'), lastSlash - 1);
    return prevSlash < 0 ? path : path.mid(prevSlash + 1);
}

QString logSourceSuffix(const QString &relativePath, int line)
{
    if (relativePath.isEmpty() || line <= 0)
        return QString();
    // Anything outside this set either would not survive the round trip through
    // splitLogSource() or would need escaping in the anchor the Log view builds
    // from it. No source file in this tree is named that way, so such a path is
    // dropped rather than stored wrong.
    static const QRegularExpression safeRe(
        QStringLiteral("\\A[A-Za-z0-9._+/-]+\\z"));
    if (!safeRe.match(relativePath).hasMatch())
        return QString();
    return QStringLiteral("  [%1:%2]").arg(relativePath).arg(line);
}

// Reads "  [<path>:<line>]" off the end of `message`, backwards and by hand.
// Every retained line is classified through here — 20,000 of them each time the
// log timeline is rebuilt — so it stops at the first character that rules the
// tail out, which for an entry that carries none is the last one.
bool splitLogSource(const QString &message, QString *body, QString *path,
                    int *line)
{
    const auto fail = [&] {
        if (body)
            *body = message;
        return false;
    };
    int at = message.size() - 1;
    if (at < 0 || message.at(at) != QLatin1Char(']'))
        return fail();
    const int lineEnd = at;
    while (at > 0 && message.at(at - 1).isDigit())
        --at;
    if (at == lineEnd) // "[…:]" — no line number
        return fail();
    const int lineStart = at;
    if (at == 0 || message.at(at - 1) != QLatin1Char(':'))
        return fail();
    --at;
    const int pathEnd = at;
    while (at > 0) {
        const QChar c = message.at(at - 1);
        if (c == QLatin1Char('[') || c == QLatin1Char(']') || c.isSpace())
            break;
        --at;
    }
    if (at == pathEnd || at == 0 || message.at(at - 1) != QLatin1Char('['))
        return fail();
    const int pathStart = at;
    --at; // the '['
    if (at == 0 || !message.at(at - 1).isSpace())
        return fail(); // a bracketed phrase glued to the message is not ours

    int bodyEnd = at;
    while (bodyEnd > 0 && message.at(bodyEnd - 1).isSpace())
        --bodyEnd;
    if (body)
        *body = message.left(bodyEnd);
    if (path)
        *path = message.mid(pathStart, pathEnd - pathStart);
    if (line)
        *line = message.mid(lineStart, lineEnd - lineStart).toInt();
    return true;
}

QString logMessageBody(const QString &message)
{
    QString body;
    splitLogSource(message, &body, nullptr, nullptr);
    return body;
}

QString logSourceLabel(const QString &relativePath, int line)
{
    if (relativePath.isEmpty())
        return QString();
    const int slash = relativePath.lastIndexOf(QLatin1Char('/'));
    const QString name =
        slash < 0 ? relativePath : relativePath.mid(slash + 1);
    return line > 0 ? QStringLiteral("%1:%2").arg(name).arg(line) : name;
}

QString resolveLogSourcePath(const QString &relativePath,
                             const QStringList &extraRoots)
{
    if (relativePath.isEmpty())
        return QString();
    if (QFileInfo(relativePath).isAbsolute()) {
        return QFileInfo(relativePath).isFile() ? relativePath : QString();
    }

    QStringList roots;
    roots << logSourceBuildRoot() << extraRoots << QDir::currentPath();
    // "qt_client/src/Foo.cpp" also resolves against a root that is itself the
    // qt_client directory — the common case when the open repository is the
    // checkout but the explorer was pointed a level in.
    const int slash = relativePath.indexOf(QLatin1Char('/'));
    const QString firstComponent =
        slash > 0 ? relativePath.left(slash) : QString();

    for (const QString &root : std::as_const(roots)) {
        if (root.isEmpty())
            continue;
        const QString direct = QDir(root).filePath(relativePath);
        if (QFileInfo(direct).isFile())
            return QDir::cleanPath(direct);
        if (!firstComponent.isEmpty() &&
            QFileInfo(root).fileName() == firstComponent) {
            const QString inner =
                QDir(root).filePath(relativePath.mid(slash + 1));
            if (QFileInfo(inner).isFile())
                return QDir::cleanPath(inner);
        }
    }
    return QString();
}

} // namespace forkmesh
