#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QSet>
#include <QString>
#include <QStringList>

namespace CodexTranscriptStyle {

inline QString commandText(const QJsonObject &input)
{
    const QJsonValue raw = input.value(QLatin1String("command"));
    if (raw.isString())
        return raw.toString().trimmed();
    if (!raw.isArray())
        return QString();
    QStringList argv;
    for (const QJsonValue &v : raw.toArray())
        argv << v.toString();
    if (argv.size() >= 3 && argv.at(1).startsWith(QLatin1Char('-'))
        && argv.at(1).contains(QLatin1Char('c'))) {
        const QString shell = argv.first().section(QLatin1Char('/'), -1);
        if (shell == QLatin1String("bash") || shell == QLatin1String("sh")
            || shell == QLatin1String("zsh"))
            return argv.at(2).trimmed();
    }
    return argv.join(QLatin1Char(' ')).trimmed();
}

inline QStringList shellWords(const QString &line)
{
    QStringList words;
    QString word;
    QChar quote;
    bool started = false;
    for (const QChar &c : line) {
        if (!quote.isNull()) {
            if (c == quote)
                quote = QChar();
            else
                word += c;
            continue;
        }
        if (c == QLatin1Char('\'') || c == QLatin1Char('"')) {
            quote = c;
            started = true;
            continue;
        }
        if (c.isSpace()) {
            if (started)
                words << word;
            word.clear();
            started = false;
            continue;
        }
        word += c;
        started = true;
    }
    if (started)
        words << word;
    return words;
}

inline bool isShellOperator(const QString &token)
{
    static const QSet<QString> ops{
        QStringLiteral("&&"), QStringLiteral("||"), QStringLiteral("|"),
        QStringLiteral(";"),  QStringLiteral("&"),  QStringLiteral(">"),
        QStringLiteral(">>"), QStringLiteral("<"),  QStringLiteral("2>&1")};
    return ops.contains(token);
}

inline bool exploreLine(const QString &command, QString &verb, QString &target)
{
    const QString line = command.trimmed();
    if (line.isEmpty() || line.contains(QLatin1Char('\n')))
        return false;
    QStringList words = shellWords(line);
    if (words.isEmpty())
        return false;
    for (const QString &w : words)
        if (isShellOperator(w) || w.startsWith(QLatin1Char('>'))
            || w.startsWith(QLatin1Char('<')))
            return false;
    const QString cmd = words.takeFirst().section(QLatin1Char('/'), -1);

    QStringList args;
    for (const QString &w : words)
        if (!w.startsWith(QLatin1Char('-')))
            args << w;

    if (cmd == QLatin1String("cat") || cmd == QLatin1String("sed")
        || cmd == QLatin1String("head") || cmd == QLatin1String("tail")
        || cmd == QLatin1String("nl") || cmd == QLatin1String("wc")) {
        if (args.isEmpty())
            return false;
        verb = QStringLiteral("Read");
        target = args.last().section(QLatin1Char('/'), -1);
        return !target.isEmpty();
    }
    if (cmd == QLatin1String("rg") || cmd == QLatin1String("grep")
        || cmd == QLatin1String("ag") || cmd == QLatin1String("ack")) {
        if (args.isEmpty())
            return false; // a bare "rg --files" isn't a search for anything
        verb = QStringLiteral("Search");
        target = args.first();
        if (args.size() > 1)
            target += QStringLiteral(" in ")
                      + args.at(1).section(QLatin1Char('/'), -1);
        return true;
    }
    if (cmd == QLatin1String("ls") || cmd == QLatin1String("find")) {
        verb = QStringLiteral("List");
        target = args.isEmpty() ? QStringLiteral(".") : args.first();
        return true;
    }
    return false;
}

inline QString toolVerb(const QString &name)
{
    if (name == QLatin1String("FileChange") || name == QLatin1String("Edit")
        || name == QLatin1String("Write") || name == QLatin1String("MultiEdit")
        || name == QLatin1String("NotebookEdit"))
        return QStringLiteral("Edited");
    if (name == QLatin1String("WebSearch"))
        return QStringLiteral("Searched");
    if (name == QLatin1String("TodoWrite"))
        return QStringLiteral("Updated plan");
    return QString();
}

} // namespace CodexTranscriptStyle
