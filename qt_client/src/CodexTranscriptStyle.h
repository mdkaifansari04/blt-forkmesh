#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QSet>
#include <QString>
#include <QStringList>

// The text side of rendering a Codex run the way the Codex CLI narrates it
// (adhoc #34) — everything that is a decision about *what a command did*, kept
// apart from the widgets that draw it so it can be tested on its own.
//
// Codex has no Read or Grep tool: it explores by running `sed -n`, `rg`, `ls`,
// and its CLI classifies those command lines into the "Read foo.cpp" /
// "Search pat in foo.cpp" lines it folds under a single "Explored" heading,
// spelling out only the commands that actually change something as "Ran".
// ClaudeTranscriptView::setCodexStyle() draws what this namespace decides.
namespace CodexTranscriptStyle {

// The command a Bash/commandExecution call ran. Codex reports it either as a
// command line or as an argv array (["bash", "-lc", "…"]); the argv form reads
// back as the line it stands for, with the shell wrapper dropped.
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
    // "bash -lc <script>" / "sh -c <script>": the script is the command.
    if (argv.size() >= 3 && argv.at(1).startsWith(QLatin1Char('-'))
        && argv.at(1).contains(QLatin1Char('c'))) {
        const QString shell = argv.first().section(QLatin1Char('/'), -1);
        if (shell == QLatin1String("bash") || shell == QLatin1String("sh")
            || shell == QLatin1String("zsh"))
            return argv.at(2).trimmed();
    }
    return argv.join(QLatin1Char(' ')).trimmed();
}

// Split a command line into shell words: quoted runs stay whole and lose their
// quotes. Enough to tell what a command acted on, not a real shell parser —
// nothing here is executed, only labelled.
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

// The shell operators that separate one command from the next; the word after
// one of these starts a fresh command.
inline bool isShellOperator(const QString &token)
{
    static const QSet<QString> ops{
        QStringLiteral("&&"), QStringLiteral("||"), QStringLiteral("|"),
        QStringLiteral(";"),  QStringLiteral("&"),  QStringLiteral(">"),
        QStringLiteral(">>"), QStringLiteral("<"),  QStringLiteral("2>&1")};
    return ops.contains(token);
}

// Classify a command line as one step of read-only exploration: on a match,
// verb is "Read"/"Search"/"List" and target is what it acted on, and the call
// becomes a line under "Explored" instead of a card of its own. Anything else
// is a real command and renders as "Ran <command>".
inline bool exploreLine(const QString &command, QString &verb, QString &target)
{
    const QString line = command.trimmed();
    if (line.isEmpty() || line.contains(QLatin1Char('\n')))
        return false;
    QStringList words = shellWords(line);
    if (words.isEmpty())
        return false;
    // Only a lone simple command reads as one step of exploration; a pipeline or
    // a chain is doing something the user should see spelled out. The test is
    // per word, after quotes are consumed — a regex like 'a|b' is an argument,
    // not a pipe.
    for (const QString &w : words)
        if (isShellOperator(w) || w.startsWith(QLatin1Char('>'))
            || w.startsWith(QLatin1Char('<')))
            return false;
    const QString cmd = words.takeFirst().section(QLatin1Char('/'), -1);

    // Everything the command wasn't told *how* to do — flags drop out, what's
    // left is what it acted on.
    QStringList args;
    for (const QString &w : words)
        if (!w.startsWith(QLatin1Char('-')))
            args << w;

    if (cmd == QLatin1String("cat") || cmd == QLatin1String("sed")
        || cmd == QLatin1String("head") || cmd == QLatin1String("tail")
        || cmd == QLatin1String("nl") || cmd == QLatin1String("wc")) {
        // The file is the last thing named: sed's script ("1,50p") and head's
        // line count come first, the path last.
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

// Codex's action verb for a tool whose card is otherwise unchanged — it titles
// a row by what happened, not by the tool that did it. Empty when there isn't
// one, leaving the Claude Code "Name(args)" heading in place.
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
