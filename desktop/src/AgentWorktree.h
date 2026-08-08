#pragma once

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSet>
#include <QStandardPaths>
#include <QString>
#include <QStringList>

namespace forkmesh::agentwt {

inline QString titleSlug(const QString &title, const QString &fallback)
{
    static const QSet<QString> kFiller = {
        QStringLiteral("a"),     QStringLiteral("an"),    QStringLiteral("and"),
        QStringLiteral("are"),   QStringLiteral("as"),    QStringLiteral("at"),
        QStringLiteral("be"),    QStringLiteral("but"),   QStringLiteral("can"),
        QStringLiteral("could"), QStringLiteral("do"),    QStringLiteral("dont"),
        QStringLiteral("each"),  QStringLiteral("for"),   QStringLiteral("from"),
        QStringLiteral("get"),   QStringLiteral("has"),   QStringLiteral("have"),
        QStringLiteral("i"),     QStringLiteral("id"),    QStringLiteral("if"),
        QStringLiteral("in"),    QStringLiteral("into"),  QStringLiteral("is"),
        QStringLiteral("it"),    QStringLiteral("its"),   QStringLiteral("just"),
        QStringLiteral("let"),   QStringLiteral("lets"),  QStringLiteral("like"),
        QStringLiteral("make"),  QStringLiteral("my"),    QStringLiteral("of"),
        QStringLiteral("on"),    QStringLiteral("or"),    QStringLiteral("other"),
        QStringLiteral("our"),   QStringLiteral("out"),   QStringLiteral("please"),
        QStringLiteral("should"),QStringLiteral("so"),    QStringLiteral("some"),
        QStringLiteral("that"),  QStringLiteral("the"),   QStringLiteral("their"),
        QStringLiteral("them"),  QStringLiteral("then"),  QStringLiteral("there"),
        QStringLiteral("these"), QStringLiteral("they"),  QStringLiteral("this"),
        QStringLiteral("to"),    QStringLiteral("too"),   QStringLiteral("up"),
        QStringLiteral("use"),   QStringLiteral("we"),    QStringLiteral("when"),
        QStringLiteral("with"),  QStringLiteral("would"), QStringLiteral("you"),
        QStringLiteral("your")};
    QStringList words{QString()};
    for (QChar ch : title.toLower()) {
        const char a = ch.toLatin1();
        if ((a >= 'a' && a <= 'z') || (a >= '0' && a <= '9'))
            words.last().append(ch);
        else if (!words.last().isEmpty())
            words.append(QString());
    }
    QString slug;
    int kept = 0;
    for (const QString &word : words) {
        if (word.isEmpty() || kFiller.contains(word))
            continue;
        if (!slug.isEmpty() &&
            (kept >= 4 || slug.size() + 1 + word.size() > 30))
            break;
        if (!slug.isEmpty())
            slug.append(QLatin1Char('-'));
        slug.append(word);
        ++kept;
    }
    if (slug.isEmpty()) {
        for (const QString &word : words) {
            if (word.isEmpty())
                continue;
            if (!slug.isEmpty() &&
                (kept >= 4 || slug.size() + 1 + word.size() > 30))
                break;
            if (!slug.isEmpty())
                slug.append(QLatin1Char('-'));
            slug.append(word);
            ++kept;
        }
    }
    return slug.isEmpty() ? fallback : slug.left(30);
}

inline QString dirName(int sessionId, const QString &title)
{
    const QString slug = titleSlug(title, QString());
    return slug.isEmpty()
               ? QStringLiteral("agent-%1").arg(sessionId)
               : QStringLiteral("agent-%1-%2").arg(QString::number(sessionId), slug);
}

inline QString root(const QString &repoPath)
{
    const QString repo = repoPath.trimmed();
    if (!repo.isEmpty() &&
        QFileInfo::exists(QDir(repo).filePath(QStringLiteral(".git"))))
        return QDir(repo).filePath(QStringLiteral(".worktrees"));
    return QStandardPaths::writableLocation(QStandardPaths::TempLocation) +
           QStringLiteral("/forkmesh-worktrees");
}

inline void ensureRoot(const QString &root)
{
    QDir().mkpath(root);
    const QString marker = QDir(root).filePath(QStringLiteral(".gitignore"));
    if (QFileInfo::exists(marker))
        return;
    QFile file(marker);
    if (file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        file.write("# ForkMesh agent worktrees. Never part of the project.\n*\n");
}

inline QString shellQuote(const QString &path)
{
    QString escaped = path;
    escaped.replace(QLatin1Char('\''), QStringLiteral("'\\''"));
    return QLatin1Char('\'') + escaped + QLatin1Char('\'');
}

} // namespace forkmesh::agentwt
