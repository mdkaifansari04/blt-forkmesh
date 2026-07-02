// Cross-region free helpers shared by several MainWindow feature translation
// units. These were forward-declared at the top of MainWindow.cpp and defined
// deep in its feature code; lifted here (with external linkage, in namespace
// forkmesh::ui) so the per-feature MainWindow*.cpp files can all call them.
// Declarations live in MainWindowInternal.h.

// MainWindowInternal.h pulls in the full Qt/include block plus the forkmesh::ui
// helper layer (themedOcticon, runGitCapture, DiffFileEntry, …) that the diff
// renderers and other shared helpers below depend on.
#include "MainWindowInternal.h"

#include "ActionStore.h"
#include "AgentStore.h"

namespace forkmesh::ui {

QString openAiAuthHeader(const QString &apiKey)
{
    return QStringLiteral("Bearer ") + apiKey.trimmed();
}

QNetworkRequest openAiRequest(const QUrl &url, const QString &apiKey)
{
    QNetworkRequest request(url);
    request.setRawHeader("Authorization", openAiAuthHeader(apiKey).toUtf8());
    request.setRawHeader("Accept", "application/json");
    return request;
}

QString apiErrorSummary(QNetworkReply *reply, const QByteArray &body)
{
    const int status =
        reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    QString message = reply->errorString();
    const QJsonDocument doc = QJsonDocument::fromJson(body);
    const QString apiMessage =
        doc.object().value("error").toObject().value("message").toString();
    if (!apiMessage.isEmpty())
        message = apiMessage;
    if (status > 0)
        return QStringLiteral("HTTP %1: %2").arg(status).arg(message);
    return message;
}

QString openAiResponseText(const QJsonObject &obj)
{
    const QString direct = obj.value(QStringLiteral("output_text")).toString().trimmed();
    if (!direct.isEmpty())
        return direct;

    QStringList parts;
    const QJsonArray output = obj.value(QStringLiteral("output")).toArray();
    for (const QJsonValue &outputValue : output) {
        const QJsonArray content =
            outputValue.toObject().value(QStringLiteral("content")).toArray();
        for (const QJsonValue &contentValue : content) {
            const QJsonObject contentObj = contentValue.toObject();
            const QString text = contentObj.value(QStringLiteral("text")).toString();
            if (!text.trimmed().isEmpty())
                parts << text.trimmed();
        }
    }
    return parts.join(QStringLiteral("\n\n")).trimmed();
}

// USD cost of an "Ask AI" Responses call from its usage tokens. Prices are the
// published OpenAI rates for gpt-4.1-nano (input $0.10 / output $0.40 per 1M
// tokens); bump these if the model or its pricing changes.
double openAiAskCostUsd(const QJsonObject &response, qint64 *inTokens,
                        qint64 *outTokens)
{
    const QJsonObject usage = response.value(QStringLiteral("usage")).toObject();
    const qint64 input = static_cast<qint64>(
        usage.value(QStringLiteral("input_tokens")).toDouble());
    const qint64 output = static_cast<qint64>(
        usage.value(QStringLiteral("output_tokens")).toDouble());
    if (inTokens)
        *inTokens = input;
    if (outTokens)
        *outTokens = output;
    constexpr double kInputPerMillion = 0.10;
    constexpr double kOutputPerMillion = 0.40;
    return (input * kInputPerMillion + output * kOutputPerMillion) / 1000000.0;
}

QString mirrorHeadBranch(const QString &mirrorPath)
{
    if (!QDir(mirrorPath).exists())
        return QString();
    QProcess p;
    p.start("git", {"-C", mirrorPath, "symbolic-ref", "--short", "HEAD"});
    if (p.waitForFinished(5000) && p.exitCode() == 0) {
        const QString head = QString::fromUtf8(p.readAllStandardOutput()).trimmed();
        if (!head.isEmpty())
            return head;
    }
    // A bare mirror cloned from the relay can carry an unset/dangling HEAD (the
    // relay serves git-upload-pack without advertising a symref HEAD), so the
    // symbolic-ref above yields nothing. Every downstream figure the Mirror nodes
    // view shows for a node — its latest commit and the issue/commit/pull/
    // discussion counts — is read relative to this branch, so an empty result
    // blanks nearly the whole row for such a node (issue #243). Fall back to an
    // actual served branch: prefer main/master, else the first refs/heads/* held.
    QProcess refs;
    refs.start("git", {"-C", mirrorPath, "for-each-ref",
                       "--format=%(refname:short)", "refs/heads/"});
    if (!refs.waitForFinished(5000) || refs.exitCode() != 0)
        return QString();
    const QStringList branches = QString::fromUtf8(refs.readAllStandardOutput())
                                     .split('\n', Qt::SkipEmptyParts);
    for (const QString &preferred : {QStringLiteral("main"), QStringLiteral("master")})
        if (branches.contains(preferred))
            return preferred;
    return branches.isEmpty() ? QString() : branches.first().trimmed();
}

QString mirrorBranchCommit(const QString &mirrorPath, const QString &branch)
{
    if (!QDir(mirrorPath).exists() || branch.isEmpty())
        return QString();
    QProcess p;
    p.start("git", {"-C", mirrorPath, "rev-parse", "--verify",
                    "refs/heads/" + branch});
    if (!p.waitForFinished(5000) || p.exitCode() != 0)
        return QString();
    return QString::fromUtf8(p.readAllStandardOutput()).trimmed();
}

QString actionStatusText(const QString &status)
{
    if (status == ActionStatus::AwaitingApproval) return QStringLiteral("Awaiting approval");
    if (status == ActionStatus::Queued) return QStringLiteral("Queued");
    if (status == ActionStatus::Running) return QStringLiteral("Running");
    if (status == ActionStatus::Success) return QStringLiteral("Success");
    if (status == ActionStatus::Failed) return QStringLiteral("Failed");
    if (status == ActionStatus::Rejected) return QStringLiteral("Rejected");
    if (status == ActionStatus::Cancelled) return QStringLiteral("Cancelled");
    return status;
}

QColor actionStatusColor(const QString &status)
{
    if (status == ActionStatus::Success) return QColor("#3fb950");
    if (status == ActionStatus::Failed) return QColor("#f85149");
    if (status == ActionStatus::Running) return QColor("#58a6ff");
    if (status == ActionStatus::AwaitingApproval) return QColor("#d29922");
    if (status == ActionStatus::Rejected) return QColor("#8b949e");
    if (status == ActionStatus::Cancelled) return QColor("#8b949e");
    return QColor("#8b949e");
}

// Detailed, timestamped startup logging so a slow launch can be diagnosed from
// the terminal: each phase prints "[startup +<ms>ms] <phase>". Always on (cheap).
QElapsedTimer &startupClock()
{
    static QElapsedTimer t;
    if (!t.isValid())
        t.start();
    return t;
}
void logStartup(const QString &phase)
{
    qInfo().noquote() << QStringLiteral("[startup +%1ms] %2")
                             .arg(startupClock().elapsed(), 5)
                             .arg(phase);
}

// Same idea for the rebuild/restart path, which can be slow (git pull, cmake
// configure, full rebuild, relaunch): each phase prints "[restart +<ms>ms]" so
// the time sink is obvious from the terminal. The clock is reset by
// beginRestartLog() at the start of each restart sequence.
QElapsedTimer &restartClock()
{
    static QElapsedTimer t;
    return t;
}
void beginRestartLog()
{
    restartClock().start();
}
void logRestart(const QString &phase)
{
    if (!restartClock().isValid())
        restartClock().start();
    qInfo().noquote() << QStringLiteral("[restart +%1ms] %2")
                             .arg(restartClock().elapsed(), 5)
                             .arg(phase);
}


// ---- Shared display helpers (diffs, agent status, reference links) ----
// Declarations (and the DiffFileEntry/DiffFileNavigator types) live in
// MainWindowInternal.h; these were lifted out of MainWindowIssues/Ide/Agents.

// Format an estimated agent task cost as a short USD string, e.g. "$0.01".
QString agentCostText(double usd)
{
    return QStringLiteral("$%1").arg(usd, 0, 'f', 2);
}

// Turn references in an already-HTML-escaped commit message into links the detail
// view resolves: "#123" → the matching issue / pull request, and a bare commit SHA
// (7-40 hex with at least one a-f letter, so plain decimal numbers are left alone)
// → that commit via showCommit. Both kinds are matched in one pass so a SHA can't
// match inside a generated href; operating on escaped text keeps the digits/'#'/hex
// intact while leaving the rest untouched.
QString linkifyIssueRefs(const QString &escaped)
{
    static const QRegularExpression re(QStringLiteral(
        "#(\\d+)|\\b(?=[0-9a-f]*[a-f])([0-9a-f]{7,40})\\b"));
    QString out;
    int last = 0;
    auto it = re.globalMatch(escaped);
    while (it.hasNext()) {
        const QRegularExpressionMatch m = it.next();
        out += escaped.mid(last, m.capturedStart() - last);
        if (!m.captured(1).isEmpty()) {
            const QString num = m.captured(1);
            out += QStringLiteral(
                       "<a href=\"ref:%1\" style=\"color:#58a6ff;text-decoration:none\">"
                       "#%1</a>")
                       .arg(num);
        } else {
            const QString sha = m.captured(2);
            out += QStringLiteral(
                       "<a href=\"commit:%1\" style=\"color:#58a6ff;text-decoration:none\">"
                       "%1</a>")
                       .arg(sha);
        }
        last = m.capturedEnd();
    }
    out += escaped.mid(last);
    return out;
}

// Reference patterns recognised inside a markdown comment body. Tried in order at
// each position: a forkmesh:// permalink, a "#123" issue/PR reference, or a bare
// commit SHA (7-40 hex with at least one a-f letter, so plain numbers are left
// alone). See autolinkReferences() / openBodyReference().
const QRegularExpression &bodyReferenceRegex()
{
    static const QRegularExpression re(QStringLiteral(
        // Permalink: stop before trailing sentence punctuation so "...#3." links #3.
        "(forkmesh://(?:issue|pull|commit)/[^\\s<>()\\[\\]]*[^\\s<>()\\[\\].,;:!?'\"])"
        "|(?<![\\w/#])#(\\d+)\\b"
        "|\\b(?=[0-9a-f]*[a-f])([0-9a-f]{7,40})\\b"));
    return re;
}

// Linkify the plain-text portion of a line (no code/links): wrap each reference in
// a markdown link with a private scheme openBodyReference() resolves.
QString linkifyReferenceText(const QString &text)
{
    QString out;
    int last = 0;
    auto it = bodyReferenceRegex().globalMatch(text);
    while (it.hasNext()) {
        const QRegularExpressionMatch m = it.next();
        out += text.mid(last, m.capturedStart() - last);
        if (!m.captured(1).isEmpty()) {
            // Keep the permalink as an explicit <autolink> so the exact href reaches
            // the router untouched by markdown.
            out += QStringLiteral("<%1>").arg(m.captured(1));
        } else if (!m.captured(2).isEmpty()) {
            const QString num = m.captured(2);
            out += QStringLiteral("[#%1](forkmesh-ref:%1)").arg(num);
        } else {
            const QString sha = m.captured(3);
            out += QStringLiteral("[%1](forkmesh-commit:%1)").arg(sha);
        }
        last = m.capturedEnd();
    }
    out += text.mid(last);
    return out;
}

// If a "[label](target)" link starts at `start`, return the index past its closing
// ')'; otherwise -1. First-match bracket/paren scan — enough to leave existing
// markdown links verbatim so we never nest one inside another.
int markdownLinkSpanEnd(const QString &line, int start)
{
    const int close = line.indexOf(QLatin1Char(']'), start + 1);
    if (close < 0)
        return -1;
    const int paren = close + 1;
    if (paren >= line.size() || line.at(paren) != QLatin1Char('('))
        return -1;
    const int end = line.indexOf(QLatin1Char(')'), paren + 1);
    return end < 0 ? -1 : end + 1;
}

// If a "<scheme://...>" autolink starts at `start`, return the index past its '>';
// otherwise -1 (a bare '<' is left as literal text).
int autolinkSpanEnd(const QString &line, int start)
{
    const int close = line.indexOf(QLatin1Char('>'), start + 1);
    if (close < 0)
        return -1;
    if (!line.mid(start + 1, close - start - 1).contains(QStringLiteral("://")))
        return -1;
    return close + 1;
}

// Linkify one non-fenced line: copy inline `code` spans, existing markdown links and
// autolinks verbatim, and linkify references in the remaining text.
QString linkifyReferenceLine(const QString &line)
{
    QString out;
    const int n = line.size();
    int i = 0;
    while (i < n) {
        const QChar c = line.at(i);
        if (c == QLatin1Char('`')) {
            int run = 1;
            while (i + run < n && line.at(i + run) == QLatin1Char('`'))
                ++run;
            const QString ticks = line.mid(i, run);
            int close = i + run;
            int found = -1;
            while (close < n) {
                const int idx = line.indexOf(ticks, close);
                if (idx < 0)
                    break;
                const int after = idx + run;
                if (after < n && line.at(after) == QLatin1Char('`')) {
                    close = after; // part of a longer run; keep looking
                    continue;
                }
                found = idx;
                break;
            }
            if (found >= 0) {
                out += line.mid(i, found + run - i);
                i = found + run;
            } else {
                out += ticks; // unterminated: emit literally
                i += run;
            }
            continue;
        }
        if (c == QLatin1Char('[')) {
            const int end = markdownLinkSpanEnd(line, i);
            if (end > i) {
                out += line.mid(i, end - i);
                i = end;
                continue;
            }
            out += c;
            ++i;
            continue;
        }
        if (c == QLatin1Char('<')) {
            const int end = autolinkSpanEnd(line, i);
            if (end > i) {
                out += line.mid(i, end - i);
                i = end;
                continue;
            }
            out += c;
            ++i;
            continue;
        }
        int j = i;
        while (j < n) {
            const QChar d = line.at(j);
            if (d == QLatin1Char('`') || d == QLatin1Char('[') || d == QLatin1Char('<'))
                break;
            ++j;
        }
        out += linkifyReferenceText(line.mid(i, j - i));
        i = j;
    }
    return out;
}


QString diffStickyStyleSheet(int fontPt)
{
    const bool dark = qApp->palette().color(QPalette::Base).lightness() < 128;
    return QStringLiteral("background:%1; border:1px solid %2; padding:6px 10px;"
                          " font-family:monospace; font-size:%3px;")
        .arg(dark ? QStringLiteral("#161b22") : QStringLiteral("#f6f8fa"),
             dark ? QStringLiteral("#30363d") : QStringLiteral("#d0d7de"))
        .arg(qBound(8, fontPt, 28));
}

// Render a path with the directory dimmed and the basename bold, matching the
// diff's .fdir / .fname spans.
QString diffStickyPathHtml(const QString &path)
{
    const bool dark = qApp->palette().color(QPalette::Base).lightness() < 128;
    const QString dirFg = dark ? QStringLiteral("#8b949e") : QStringLiteral("#6e7781");
    const QString nameFg = dark ? QStringLiteral("#e6edf3") : QStringLiteral("#1f2328");
    const int slash = path.lastIndexOf(QLatin1Char('/'));
    if (slash >= 0)
        return QStringLiteral("<span style='color:%1'>%2</span>"
                              "<span style='color:%3; font-weight:600'>%4</span>")
            .arg(dirFg, path.left(slash + 1).toHtmlEscaped(), nameFg,
                 path.mid(slash + 1).toHtmlEscaped());
    return QStringLiteral("<span style='color:%1; font-weight:600'>%2</span>")
        .arg(nameFg, path.toHtmlEscaped());
}

QString agentStatusText(const QString &status)
{
    if (status == AgentStatus::Queued) return QStringLiteral("Queued");
    if (status == AgentStatus::Running) return QStringLiteral("Running");
    if (status == AgentStatus::Waiting) return QStringLiteral("Waiting");
    if (status == AgentStatus::Success) return QStringLiteral("Success");
    if (status == AgentStatus::Failed) return QStringLiteral("Failed");
    if (status == AgentStatus::Stopped) return QStringLiteral("Stopped");
    if (status == AgentStatus::Cleared) return QStringLiteral("Cleared");
    return status;
}

// An agent is "working" on the issue while queued or running.
bool agentSessionActive(const AgentSession *s)
{
    return s && (s->status == AgentStatus::Running ||
                 s->status == AgentStatus::Queued);
}

QString solanaDisplayCurrency()
{
    QSettings s;
    QString cur = s.value(kSolanaDisplayCurrencySetting).toString().toLower();
    if (cur.isEmpty())
        cur = s.value(kSolanaDisplayUsdSetting, false).toBool()
                  ? QStringLiteral("usd")
                  : QStringLiteral("sol");
    if (cur != QLatin1String("usd") && cur != QLatin1String("inr"))
        cur = QStringLiteral("sol");
    return cur;
}

QColor agentStatusColor(const QString &status)
{
    if (status == AgentStatus::Success) return QColor("#3fb950");
    if (status == AgentStatus::Failed) return QColor("#f85149");
    if (status == AgentStatus::Running) return QColor("#58a6ff");
    if (status == AgentStatus::Queued) return QColor("#d29922");
    if (status == AgentStatus::Waiting) return QColor("#d29922");
    if (status == AgentStatus::Stopped) return QColor("#8b949e");
    if (status == AgentStatus::Cleared) return QColor("#8b949e");
    return QColor("#8b949e");
}

QIcon agentStatusOcticon(const AgentSession &s, int px)
{
    if (s.merged)
        return themedOcticon("git-merge", QColor("#a371f7"), px);
    if (s.status == AgentStatus::Running)
        return themedOcticon("sync", QColor("#3fb950"), px);
    if (s.status == AgentStatus::Success)
        return themedOcticon("check-circle", QColor("#3fb950"), px);
    if (s.status == AgentStatus::Failed)
        return themedOcticon("x", QColor("#f85149"), px);
    if (s.status == AgentStatus::Stopped)
        return themedOcticon("stop", QColor("#f85149"), px);
    if (s.status == AgentStatus::Waiting)
        return themedOcticon("hand", QColor("#e3742f"), px);
    if (s.status == AgentStatus::Queued)
        return themedOcticon("history", QColor("#d29922"), px);
    // Cleared / unknown.
    return themedOcticon("circle-slash", QColor("#8b949e"), px);
}


QString diffImageMimeForPath(const QString &path)
{
    const QString lower = path.toLower();
    if (lower.endsWith(QStringLiteral(".png")))
        return QStringLiteral("image/png");
    if (lower.endsWith(QStringLiteral(".jpg")) || lower.endsWith(QStringLiteral(".jpeg")))
        return QStringLiteral("image/jpeg");
    if (lower.endsWith(QStringLiteral(".gif")))
        return QStringLiteral("image/gif");
    if (lower.endsWith(QStringLiteral(".webp")))
        return QStringLiteral("image/webp");
    if (lower.endsWith(QStringLiteral(".bmp")))
        return QStringLiteral("image/bmp");
    if (lower.endsWith(QStringLiteral(".ico")))
        return QStringLiteral("image/x-icon");
    return QString();
}

// A diff can carry an attacker-influenced image straight into an auto-rendered
// view (an untrusted PR's asset, or a file an issue agent fetched), so it isn't
// enough to trust the ".png" extension and hand the bytes to QTextDocument's
// image loader. QImageReader::canRead() sniffs the actual content, and size()
// reads just the format header for PNG/JPEG/GIF/BMP — no pixel decode yet — so
// this rejects both non-images and "decompression bomb" images (a small file
// whose declared dimensions would blow up to a huge pixel buffer once decoded)
// without paying the decode cost ourselves.
bool diffImageSafeToDecode(const QByteArray &bytes)
{
    QBuffer buf;
    buf.setData(bytes);
    buf.open(QIODevice::ReadOnly);
    QImageReader reader(&buf);
    if (!reader.canRead())
        return false;
    const QSize size = reader.size();
    constexpr qint64 kMaxPixels = 40'000'000; // ~40MP, comfortably above any diff asset
    return !size.isValid() ||
           qint64(size.width()) * qint64(size.height()) <= kMaxPixels;
}

QString diffImageCellHtml(const QString &label, const QString &path,
                          const QString &mime, const QByteArray &bytes)
{
    const QString caption =
        QStringLiteral("<div class='imgcaption'>%1</div>").arg(label);
    if (bytes.isEmpty())
        return QStringLiteral("<td class='imgcell'><div class='imgempty'>Not "
                              "present</div>%1</td>")
            .arg(caption);
    if (!diffImageSafeToDecode(bytes))
        return QStringLiteral("<td class='imgcell'><div class='imgempty'>Preview "
                              "skipped (unrecognized or oversized image)</div>%1"
                              "</td>")
            .arg(caption);
    return QStringLiteral("<td class='imgcell'><img alt=\"%1: %2\" src=\"data:%3;"
                          "base64,%4\">%5</td>")
        .arg(label.toHtmlEscaped(), path.toHtmlEscaped(), mime,
             QString::fromLatin1(bytes.toBase64()), caption);
}

QString diffImagePreviewHtml(const QString &dir, const QString &base,
                             const QString &head, const DiffFileEntry &f)
{
    constexpr int kMaxInlineImageBytes = 512 * 1024;
    const QString &path = f.path;
    const QString mime = diffImageMimeForPath(path);
    if (mime.isEmpty())
        return QString();

    // An empty base/head isn't "read the index" (what `git show :path` would do)
    // — it means the caller has no commit for that side at all: the Source
    // Control and live agent-diff views pass "" for whichever side is the
    // uncommitted working tree, and `:path` is simply missing for a brand-new
    // untracked file. Read the intended content directly instead so a freshly
    // added/edited image actually previews (adhoc #61).
    QByteArray oldBytes;
    if (f.status != QLatin1String("added") && !dir.isEmpty()) {
        const QString oldRef = base.isEmpty() ? QStringLiteral("HEAD") : base;
        if (!runGitCapture(dir, {"show", oldRef + ":" + path}, &oldBytes, nullptr) ||
            oldBytes.size() > kMaxInlineImageBytes)
            oldBytes.clear();
    }
    QByteArray newBytes;
    if (f.status != QLatin1String("deleted")) {
        if (head.isEmpty()) {
            if (!dir.isEmpty()) {
                QFile file(QDir(dir).filePath(path));
                if (file.open(QIODevice::ReadOnly))
                    newBytes = file.readAll();
            }
        } else {
            runGitCapture(dir, {"show", head + ":" + path}, &newBytes, nullptr);
        }
        if (newBytes.size() > kMaxInlineImageBytes)
            newBytes.clear();
    }
    if (oldBytes.isEmpty() && newBytes.isEmpty())
        return QString();

    return QStringLiteral("<table class='imagetable' width='100%' cellspacing='0' "
                          "cellpadding='0'><tr>%1%2</tr></table>")
        .arg(diffImageCellHtml(QStringLiteral("Before"), path, mime, oldBytes),
             diffImageCellHtml(QStringLiteral("After"), path, mime, newBytes));
}

// Render a unified diff into an HTML table with an old/new line-number gutter
// and +/- coloring (classes styled by the document stylesheet), one block per
// file with a named anchor so the file list can scroll to it.
// Build a line-number gutter cell. When anchors is true the number links to
// "cmt:<path>?s=<side>&l=<line>" so the PR view can attach a comment to that
// line. The file path is carried in the anchor (not inferred from the selected
// list row) so a continuous all-files diff targets the right file (issue #250).
QString gutterCellHtml(const QString &cls, const QString &num, const QString &side,
                       bool anchors, const QString &path)
{
    if (num.isEmpty())
        return QStringLiteral("<td class='ln %1'></td>").arg(cls);
    if (!anchors)
        return QStringLiteral("<td class='ln %1'>%2</td>").arg(cls, num);
    const QString enc = QString::fromLatin1(QUrl::toPercentEncoding(path));
    return QStringLiteral("<td class='ln lnlink %1'>"
                          "<a href='cmt:%2?s=%3&l=%4' title='Comment on this line'>"
                          "%4</a></td>")
        .arg(cls, enc, side, num);
}

// Insert any inline-comment note rows that target the just-emitted line numbers
// (keys "<path>\x1fold:<n>" / "<path>\x1fnew:<n>"), spanning all `columns`
// columns of the table. The path prefix keeps notes from one file off another
// file's same-numbered line when every file is rendered into one view (#250).
QString lineNoteRows(const QHash<QString, QString> &lineNotes, const QString &path,
                     const QString &oldNum, const QString &newNum, int columns)
{
    QString out;
    const auto addNote = [&](const QString &key) {
        const QString note = lineNotes.value(key);
        if (!note.isEmpty())
            out += QStringLiteral("<tr><td class='notecell' colspan='%1'>%2</td></tr>")
                       .arg(columns)
                       .arg(note);
    };
    const QString prefix = path + QLatin1Char('\x1f');
    if (!oldNum.isEmpty())
        addNote(prefix + QStringLiteral("old:") + oldNum);
    if (!newNum.isEmpty())
        addNote(prefix + QStringLiteral("new:") + newNum);
    return out;
}

// A single placeholder row standing in for a binary file's diff body, which is a
// base85 literal/delta blob rather than reviewable text. `columns` matches the
// table the row is inserted into (2 for a one-sided add/delete, 3 otherwise).
QString diffBinaryRowHtml(const DiffFileEntry &f, int columns)
{
    QString verb = QStringLiteral("changed");
    if (f.status == QLatin1String("added"))
        verb = QStringLiteral("added");
    else if (f.status == QLatin1String("deleted"))
        verb = QStringLiteral("removed");
    else if (f.status == QLatin1String("renamed"))
        verb = QStringLiteral("renamed");
    return QStringLiteral("<tr><td class='code ctx' colspan='%1'>"
                          "<i>Binary file %2 \xE2\x80\x94 content not shown</i></td></tr>")
        .arg(columns)
        .arg(verb);
}

// Per-file header block shared by the unified and split renderers. The old text
// badge ("ADDED"/"MODIFIED"/…) is replaced by a status octicon (the word lives
// on as a tooltip); the path shows its directory dimmed and the basename bold;
// a five-segment green/red proportion bar mirrors GitHub's diff-stat squares.
// The right side carries the per-file comment icon (PR view only) and the
// "Viewed" toggle. Returns the header div only — the caller appends the image
// preview and opens the diff table itself (and skips both for viewed files).
QString diffFileHeaderHtml(const DiffFileEntry &f, bool viewed, bool anchors)
{
    QString icon = QStringLiteral("file-diff");
    QColor tint(QStringLiteral("#d29922"));
    QString word = QStringLiteral("Modified");
    if (f.status == QLatin1String("added")) {
        icon = QStringLiteral("diff");
        tint = QColor(QStringLiteral("#3fb950"));
        word = QStringLiteral("Added");
    } else if (f.status == QLatin1String("deleted")) {
        icon = QStringLiteral("trash");
        tint = QColor(QStringLiteral("#f85149"));
        word = QStringLiteral("Removed");
    } else if (f.status == QLatin1String("renamed")) {
        icon = QStringLiteral("file-diff");
        tint = QColor(QStringLiteral("#58a6ff"));
        word = QStringLiteral("Renamed");
    }
    const QString badge =
        QStringLiteral("<span class='stbadge' title='%1'>%2</span>")
            .arg(word, octiconMarkup(icon, 14, tint));

    const QString encPath = QString::fromLatin1(QUrl::toPercentEncoding(f.path));
    // "Viewed" checkbox toggle (the diff is collapsed when checked).
    const QString viewedLink =
        QStringLiteral("<a class='viewedtoggle%1' href='viewed:%2'>"
                       "<span style='font-size:19px'>%3</span> Viewed</a>")
            .arg(viewed ? QStringLiteral(" on") : QString(), encPath,
                 viewed ? QString::fromUtf8("\xE2\x98\x91")
                        : QString::fromUtf8("\xE2\x98\x90"));
    // Per-file comment icon, only in the PR view (anchors enabled).
    const QString commentIcon =
        anchors ? QStringLiteral("<a class='filecomment' href='filecomment:%1' "
                                 "title='Comment on this file'>%2</a>")
                      .arg(encPath, QString::fromUtf8("\xF0\x9F\x92\xAC"))
                : QString();

    // Split the path so the directory reads as muted context and the file name
    // stands out.
    QString pathHtml;
    const int slash = f.path.lastIndexOf(QLatin1Char('/'));
    if (slash >= 0)
        pathHtml = QStringLiteral("<span class='fdir'>%1</span>"
                                  "<span class='fname'>%2</span>")
                       .arg(f.path.left(slash + 1).toHtmlEscaped(),
                            f.path.mid(slash + 1).toHtmlEscaped());
    else
        pathHtml = QStringLiteral("<span class='fname'>%1</span>")
                       .arg(f.path.toHtmlEscaped());

    // Five filled-block glyphs split green/red by the additions' share, clamped
    // so any change of a kind shows at least one block.
    const int total = f.adds + f.dels;
    QString bar;
    if (total > 0) {
        int green = qRound(5.0 * f.adds / total);
        if (f.adds > 0 && green == 0)
            green = 1;
        if (f.dels > 0 && green == 5)
            green = 4;
        const int red = 5 - green;
        if (green > 0)
            bar += QStringLiteral("<span class='barblk add'>%1</span>")
                       .arg(QString(green, QChar(0x2588)));
        if (red > 0)
            bar += QStringLiteral("<span class='barblk del'>%1</span>")
                       .arg(QString(red, QChar(0x2588)));
    }

    // Binary files have no line counts; show a "BIN" marker in place of +/-.
    const QString statHtml =
        f.binary
            ? QStringLiteral("<span class='fstat'> BIN</span>")
            : QStringLiteral("<span class='fstat'> <span class='sadd'>+%1</span> "
                             "<span class='sdel'>\xE2\x88\x92%2</span> %3</span>")
                  .arg(QString::number(f.adds), QString::number(f.dels), bar);

    return QString::fromUtf8(
               "<a name=\"%1\"></a><div class='fileblock%6'>"
               "<div class='fileheader'>"
               "<table width='100%' cellspacing='0' cellpadding='0'><tr>"
               "<td>%2<span class='fpath'>%3</span>%4</td>"
               "<td align='right'>%5%7</td></tr></table></div>")
        .arg(f.anchor, badge, pathHtml, statHtml, commentIcon,
             viewed ? QStringLiteral(" viewed") : QString(), viewedLink);
}

QString renderUnifiedDiffHtml(const QString &patch, QList<DiffFileEntry> &files,
                              const QString &dir, const QString &base,
                              const QString &head,
                              const QString &anchorFile = QString(),
                              const QHash<QString, QString> &lineNotes = {},
                              const QSet<QString> &viewedFiles = {})
{
    static const QRegularExpression hunkRe(
        QStringLiteral("@@ -(\\d+)(?:,\\d+)? \\+(\\d+)(?:,\\d+)? @@"));
    const bool anchors = !anchorFile.isEmpty();
    QString html;
    // Avoid repeated reallocation on big diffs; rendered HTML runs a few times
    // the size of the raw patch (row markup per line).
    html.reserve(patch.size() * 3);
    QString fileBody;
    const QStringList lines = patch.split(QLatin1Char('\n'));
    int oldNo = 0, newNo = 0, fileIdx = -1;
    bool inFile = false;
    // Set once a file's "GIT binary patch" / "Binary files … differ" marker is
    // seen, so the base85 literal/delta payload that follows is skipped instead of
    // rendered as garbage context rows. Reset at the next "diff --git".
    bool inBinary = false;
    // Path of the file whose rows are currently being emitted (for comment
    // anchors / note keys when all files share one rendered view — issue #250).
    const auto curPath = [&] {
        return fileIdx >= 0 ? files[fileIdx].path : QString();
    };

    // Per-file header is emitted lazily: we buffer the rows so the header can
    // report final +/- counts (read from the hunks), then prepend the styled
    // header block before the table.
    auto emitFileHeader = [&](int idx) {
        const DiffFileEntry &f = files[idx];
        const bool viewed = viewedFiles.contains(f.path);
        html += diffFileHeaderHtml(f, viewed, anchors);
        if (!viewed) {
            html += diffImagePreviewHtml(dir, base, head, f);
            html += QStringLiteral(
                "<table class='difftable' cellspacing='0' cellpadding='0'>");
        }
    };
    auto closeFile = [&] {
        if (inFile) {
            const DiffFileEntry &f = files[fileIdx];
            const bool viewed = viewedFiles.contains(f.path);
            emitFileHeader(fileIdx);
            if (viewed) {
                html += QStringLiteral("</div>");
            } else {
                html += fileBody;
                html += QStringLiteral("</table></div>");
            }
            fileBody.clear();
            inFile = false;
        }
    };

    for (const QString &line : lines) {
        if (line.startsWith(QLatin1String("diff --git "))) {
            closeFile();
            QString path = line;
            const int bpos = line.indexOf(QLatin1String(" b/"));
            if (bpos >= 0)
                path = line.mid(bpos + 3);
            DiffFileEntry f;
            f.path = path;
            f.anchor = QStringLiteral("file-%1").arg(files.size());
            files.append(f);
            fileIdx = files.size() - 1;
            inFile = true;
            inBinary = false;
            continue;
        }
        if (!inFile)
            continue;
        // Status detection (header lines come before the first hunk).
        if (fileIdx >= 0) {
            if (line.startsWith(QLatin1String("new file")))
                files[fileIdx].status = QStringLiteral("added");
            else if (line.startsWith(QLatin1String("deleted file")))
                files[fileIdx].status = QStringLiteral("deleted");
            else if (line.startsWith(QLatin1String("rename ")) ||
                     line.startsWith(QLatin1String("similarity ")))
                files[fileIdx].status = QStringLiteral("renamed");
        }
        // Binary section: mark the file, emit one placeholder row, and skip the
        // base85 payload (and any "Binary files … differ" line) that follows.
        if (line.startsWith(QLatin1String("GIT binary patch")) ||
            line.startsWith(QLatin1String("Binary files "))) {
            if (fileIdx >= 0 && !files[fileIdx].binary) {
                files[fileIdx].binary = true;
                const bool oneSided =
                    files[fileIdx].status == QLatin1String("added") ||
                    files[fileIdx].status == QLatin1String("deleted");
                fileBody += diffBinaryRowHtml(files[fileIdx], oneSided ? 2 : 3);
            }
            inBinary = true;
            continue;
        }
        if (inBinary)
            continue;
        if (line.startsWith(QLatin1String("index ")) ||
            line.startsWith(QLatin1String("--- ")) ||
            line.startsWith(QLatin1String("+++ ")) ||
            line.startsWith(QLatin1String("new file")) ||
            line.startsWith(QLatin1String("deleted file")) ||
            line.startsWith(QLatin1String("similarity ")) ||
            line.startsWith(QLatin1String("rename ")) ||
            line.startsWith(QLatin1String("old mode")) ||
            line.startsWith(QLatin1String("new mode")))
            continue;
        // A fully-added or fully-deleted file leaves one side's gutter blank on
        // every row. Drop that always-empty column so the content sits flush
        // left instead of behind a dead gutter.
        const bool addOnly =
            fileIdx >= 0 && files[fileIdx].status == QLatin1String("added");
        const bool delOnly =
            fileIdx >= 0 && files[fileIdx].status == QLatin1String("deleted");
        const bool oneSided = addOnly || delOnly;
        if (line.startsWith(QLatin1String("@@"))) {
            const QRegularExpressionMatch m = hunkRe.match(line);
            if (m.hasMatch()) {
                oldNo = m.captured(1).toInt();
                newNo = m.captured(2).toInt();
            }
            fileBody += QStringLiteral("<tr>%1<td class='code hunk'>%2</td></tr>")
                            .arg(oneSided
                                     ? QStringLiteral("<td class='ln hunk'></td>")
                                     : QStringLiteral("<td class='ln hunk'></td>"
                                                      "<td class='ln hunk'></td>"),
                                 line.toHtmlEscaped());
            continue;
        }

        const QChar c0 = line.isEmpty() ? QLatin1Char(' ') : line.at(0);
        QString text = line.isEmpty() ? QString() : line.mid(1);
        QString cls, oldCell, newCell;
        if (c0 == QLatin1Char('+')) {
            cls = QStringLiteral("add");
            newCell = QString::number(newNo++);
            if (fileIdx >= 0)
                ++files[fileIdx].adds;
        } else if (c0 == QLatin1Char('-')) {
            cls = QStringLiteral("del");
            oldCell = QString::number(oldNo++);
            if (fileIdx >= 0)
                ++files[fileIdx].dels;
        } else if (c0 == QLatin1Char('\\')) { // "\ No newline at end of file"
            cls = QStringLiteral("ctx");
            text = line;
        } else {
            cls = QStringLiteral("ctx");
            oldCell = QString::number(oldNo++);
            newCell = QString::number(newNo++);
        }
        QString gutters;
        if (addOnly)
            gutters = gutterCellHtml(cls, newCell, QStringLiteral("new"), anchors,
                                     curPath());
        else if (delOnly)
            gutters = gutterCellHtml(cls, oldCell, QStringLiteral("old"), anchors,
                                     curPath());
        else
            gutters =
                gutterCellHtml(cls, oldCell, QStringLiteral("old"), anchors,
                               curPath()) +
                gutterCellHtml(cls, newCell, QStringLiteral("new"), anchors,
                               curPath());
        fileBody += QStringLiteral("<tr>%1<td class='code %2'>%3</td></tr>")
                        .arg(gutters, cls,
                             text.isEmpty() ? QStringLiteral("&nbsp;")
                                            : text.toHtmlEscaped());
        if (anchors)
            fileBody += lineNoteRows(lineNotes, curPath(), oldCell, newCell,
                                     oneSided ? 2 : 3);
    }
    closeFile();
    return html;
}

// Render a unified diff into a side-by-side (split) HTML table: per file, four
// columns — old line-number, old code, new line-number, new code. Deletions sit
// on the left, additions on the right, context spans both. Within a hunk a run
// of removed lines is paired row-for-row with the following run of added lines;
// any surplus on one side leaves the opposite cell blank. Stats and per-file
// anchors/headers match renderUnifiedDiffHtml so the file list and toggle line up.
QString renderSplitDiffHtml(const QString &patch, QList<DiffFileEntry> &files,
                            const QString &dir, const QString &base,
                            const QString &head,
                            const QString &anchorFile = QString(),
                            const QHash<QString, QString> &lineNotes = {},
                            const QSet<QString> &viewedFiles = {})
{
    static const QRegularExpression hunkRe(
        QStringLiteral("@@ -(\\d+)(?:,\\d+)? \\+(\\d+)(?:,\\d+)? @@"));
    const bool anchors = !anchorFile.isEmpty();
    QString html;
    html.reserve(patch.size() * 3);
    QString fileBody;
    const QStringList lines = patch.split(QLatin1Char('\n'));
    int oldNo = 0, newNo = 0, fileIdx = -1;
    bool inFile = false;
    bool inBinary = false; // see renderUnifiedDiffHtml

    // Path of the file currently being emitted (for comment anchors / note keys
    // when all files share one rendered view — issue #250).
    const auto curPath = [&] {
        return fileIdx >= 0 ? files[fileIdx].path : QString();
    };
    // A side gutter cell; clickable (comment anchor) when anchors is on.
    const auto gut = [&](const QString &extraCls, const QString &num,
                         const QString &side) {
        if (num.isEmpty())
            return QStringLiteral("<td class='ln %1'></td>").arg(extraCls);
        if (!anchors)
            return QStringLiteral("<td class='ln %1'>%2</td>").arg(extraCls, num);
        const QString enc = QString::fromLatin1(QUrl::toPercentEncoding(curPath()));
        return QStringLiteral("<td class='ln lnlink %1'>"
                              "<a href='cmt:%2?s=%3&l=%4' title='Comment on this line'>"
                              "%4</a></td>")
            .arg(extraCls, enc, side, num);
    };

    // Buffered runs of removed/added lines awaiting pairing.
    QStringList pendingDel, pendingAdd;
    const auto emitText = [](const QString &t) {
        return t.isEmpty() ? QStringLiteral("&nbsp;") : t.toHtmlEscaped();
    };
    // A fully-added or fully-deleted file has content on one side only. When the
    // current file is one-sided, fall back to a 2-column unified layout so the
    // diff sits flush-left instead of behind a dead, always-blank column.
    const auto oneSidedKind = [&]() -> int {
        if (fileIdx < 0)
            return 0;
        if (files[fileIdx].status == QLatin1String("added"))
            return 1; // add-only
        if (files[fileIdx].status == QLatin1String("deleted"))
            return -1; // del-only
        return 0;
    };
    const auto flushPairs = [&] {
        if (const int kind = oneSidedKind()) {
            const bool addOnly = kind > 0;
            const QStringList &buf = addOnly ? pendingAdd : pendingDel;
            const QString cls =
                addOnly ? QStringLiteral("add") : QStringLiteral("del");
            const QString side =
                addOnly ? QStringLiteral("new") : QStringLiteral("old");
            for (const QString &t : buf) {
                const QString ln = QString::number(addOnly ? newNo++ : oldNo++);
                fileBody +=
                    QStringLiteral("<tr>%1<td class='code %2'>%3</td></tr>")
                        .arg(gut(cls, ln, side), cls, emitText(t));
                if (anchors)
                    fileBody += lineNoteRows(lineNotes, curPath(),
                                             addOnly ? QString() : ln,
                                             addOnly ? ln : QString(), 2);
            }
            pendingDel.clear();
            pendingAdd.clear();
            return;
        }
        const int n = qMax(pendingDel.size(), pendingAdd.size());
        for (int i = 0; i < n; ++i) {
            const bool hasDel = i < pendingDel.size();
            const bool hasAdd = i < pendingAdd.size();
            const QString oldLn = hasDel ? QString::number(oldNo++) : QString();
            const QString newLn = hasAdd ? QString::number(newNo++) : QString();
            const QString delCls = hasDel ? QStringLiteral("del") : QString();
            const QString addCls = hasAdd ? QStringLiteral("add") : QString();
            fileBody += QStringLiteral("<tr>%1<td class='code ocode %2'>%3</td>"
                                       "%4<td class='code ncode %5'>%6</td></tr>")
                            .arg(gut(delCls, oldLn, QStringLiteral("old")), delCls,
                                 hasDel ? emitText(pendingDel.at(i)) : QStringLiteral("&nbsp;"),
                                 gut(QStringLiteral("nln ") + addCls, newLn,
                                     QStringLiteral("new")),
                                 addCls,
                                 hasAdd ? emitText(pendingAdd.at(i)) : QStringLiteral("&nbsp;"));
            if (anchors)
                fileBody += lineNoteRows(lineNotes, curPath(), oldLn, newLn, 4);
        }
        pendingDel.clear();
        pendingAdd.clear();
    };

    auto emitFileHeader = [&](int idx) {
        const DiffFileEntry &f = files[idx];
        const bool viewed = viewedFiles.contains(f.path);
        html += diffFileHeaderHtml(f, viewed, anchors);
        if (!viewed) {
            html += diffImagePreviewHtml(dir, base, head, f);
            html += QStringLiteral(
                "<table class='difftable' cellspacing='0' cellpadding='0'>");
        }
    };
    auto closeFile = [&] {
        if (inFile) {
            const bool viewed = viewedFiles.contains(files[fileIdx].path);
            flushPairs();
            emitFileHeader(fileIdx);
            if (viewed) {
                html += QStringLiteral("</div>");
            } else {
                html += fileBody;
                html += QStringLiteral("</table></div>");
            }
            fileBody.clear();
            inFile = false;
        }
    };

    for (const QString &line : lines) {
        if (line.startsWith(QLatin1String("diff --git "))) {
            closeFile();
            QString path = line;
            const int bpos = line.indexOf(QLatin1String(" b/"));
            if (bpos >= 0)
                path = line.mid(bpos + 3);
            DiffFileEntry f;
            f.path = path;
            f.anchor = QStringLiteral("file-%1").arg(files.size());
            files.append(f);
            fileIdx = files.size() - 1;
            inFile = true;
            inBinary = false;
            continue;
        }
        if (!inFile)
            continue;
        if (fileIdx >= 0) {
            if (line.startsWith(QLatin1String("new file")))
                files[fileIdx].status = QStringLiteral("added");
            else if (line.startsWith(QLatin1String("deleted file")))
                files[fileIdx].status = QStringLiteral("deleted");
            else if (line.startsWith(QLatin1String("rename ")) ||
                     line.startsWith(QLatin1String("similarity ")))
                files[fileIdx].status = QStringLiteral("renamed");
        }
        // Binary section: one placeholder row, then skip the base85 payload.
        if (line.startsWith(QLatin1String("GIT binary patch")) ||
            line.startsWith(QLatin1String("Binary files "))) {
            if (fileIdx >= 0 && !files[fileIdx].binary) {
                files[fileIdx].binary = true;
                fileBody += diffBinaryRowHtml(files[fileIdx],
                                              oneSidedKind() ? 2 : 4);
            }
            inBinary = true;
            continue;
        }
        if (inBinary)
            continue;
        if (line.startsWith(QLatin1String("index ")) ||
            line.startsWith(QLatin1String("--- ")) ||
            line.startsWith(QLatin1String("+++ ")) ||
            line.startsWith(QLatin1String("new file")) ||
            line.startsWith(QLatin1String("deleted file")) ||
            line.startsWith(QLatin1String("similarity ")) ||
            line.startsWith(QLatin1String("rename ")) ||
            line.startsWith(QLatin1String("old mode")) ||
            line.startsWith(QLatin1String("new mode")))
            continue;
        if (line.startsWith(QLatin1String("@@"))) {
            flushPairs();
            const QRegularExpressionMatch m = hunkRe.match(line);
            if (m.hasMatch()) {
                oldNo = m.captured(1).toInt();
                newNo = m.captured(2).toInt();
            }
            // One-sided files use the 2-column layout (see flushPairs).
            fileBody += oneSidedKind()
                            ? QStringLiteral("<tr><td class='ln hunk'></td>"
                                             "<td class='code hunk'>%1</td></tr>")
                                  .arg(line.toHtmlEscaped())
                            : QStringLiteral(
                                  "<tr><td class='ln hunk'></td>"
                                  "<td class='code hunk'>%1</td>"
                                  "<td class='ln nln hunk'></td>"
                                  "<td class='code hunk'>&nbsp;</td></tr>")
                                  .arg(line.toHtmlEscaped());
            continue;
        }

        const QChar c0 = line.isEmpty() ? QLatin1Char(' ') : line.at(0);
        const QString text = line.isEmpty() ? QString() : line.mid(1);
        if (c0 == QLatin1Char('+')) {
            pendingAdd << text;
            if (fileIdx >= 0)
                ++files[fileIdx].adds;
        } else if (c0 == QLatin1Char('-')) {
            pendingDel << text;
            if (fileIdx >= 0)
                ++files[fileIdx].dels;
        } else if (c0 == QLatin1Char('\\')) { // "\ No newline at end of file"
            flushPairs();
            // One-sided files keep the 2-column layout; otherwise render on both
            // sides as context so neither column drifts.
            fileBody += oneSidedKind()
                            ? QStringLiteral("<tr><td class='ln'></td>"
                                             "<td class='code'>%1</td></tr>")
                                  .arg(line.toHtmlEscaped())
                            : QStringLiteral(
                                  "<tr><td class='ln'></td><td class='code'>%1</td>"
                                  "<td class='ln nln'></td><td class='code'>%1</td></tr>")
                                  .arg(line.toHtmlEscaped());
        } else if (const int kind = oneSidedKind()) {
            // Context line in a one-sided file (rare): single gutter + code.
            flushPairs();
            const bool addOnly = kind > 0;
            const QString ln = QString::number(addOnly ? newNo++ : oldNo++);
            if (addOnly)
                ++oldNo;
            else
                ++newNo;
            fileBody += QStringLiteral("<tr>%1<td class='code'>%2</td></tr>")
                            .arg(gut(QString(), ln,
                                     addOnly ? QStringLiteral("new")
                                             : QStringLiteral("old")),
                                 emitText(text));
        } else {
            flushPairs();
            const QString ln1 = QString::number(oldNo++);
            const QString ln2 = QString::number(newNo++);
            fileBody += QStringLiteral(
                            "<tr>%1<td class='code ocode'>%3</td>"
                            "%2<td class='code ncode'>%3</td></tr>")
                            .arg(gut(QString(), ln1, QStringLiteral("old")),
                                 gut(QStringLiteral("nln"), ln2, QStringLiteral("new")),
                                 emitText(text));
            if (anchors)
                fileBody += lineNoteRows(lineNotes, curPath(), ln1, ln2, 4);
        }
    }
    closeFile();
    return html;
}

// User preference (persisted): render diffs side-by-side (split) vs unified.
// Defaults to side-by-side. Shared by the commit and pull-request diff views.
bool diffSplitPref()
{
    return QSettings().value(QStringLiteral("view/diffSplit"), true).toBool();
}
void setDiffSplitPref(bool split)
{
    QSettings().setValue(QStringLiteral("view/diffSplit"), split);
}

// User preference (persisted): automatically mark a pull request's files as
// "Viewed" as the reviewer scrolls past them in the Files-changed diff.
// Defaults off, matching GitHub's same-named setting.
bool autoMarkViewedOnScrollPref()
{
    return QSettings().value(QStringLiteral("view/autoMarkViewedOnScroll"), false).toBool();
}
void setAutoMarkViewedOnScrollPref(bool on)
{
    QSettings().setValue(QStringLiteral("view/autoMarkViewedOnScroll"), on);
}

// Dispatch to the split or unified renderer based on the current preference.
QString renderDiffHtml(const QString &patch, QList<DiffFileEntry> &files,
                       const QString &dir, const QString &base, const QString &head,
                       const QString &anchorFile,
                       const QHash<QString, QString> &lineNotes,
                       const QSet<QString> &viewedFiles)
{
    return diffSplitPref()
               ? renderSplitDiffHtml(patch, files, dir, base, head, anchorFile,
                                     lineNotes, viewedFiles)
               : renderUnifiedDiffHtml(patch, files, dir, base, head, anchorFile,
                                       lineNotes, viewedFiles);
}

// Theme-aware stylesheet for the diff HTML produced by the renderers above,
// shared by the commit and pull-request diff QTextBrowsers. Includes the
// split-view central divider (td.nln) on top of the unified-view rules.
QString diffStyleSheet(int fontPt)
{
    const bool dark = qApp->palette().color(QPalette::Base).lightness() < 128;
    const QString addBg = dark ? "#12261c" : "#e6ffec";
    const QString delBg = dark ? "#2d1416" : "#ffebe9";
    const QString hunkBg = dark ? "#0d1d33" : "#ddf4ff";
    const QString hunkFg = dark ? "#58a6ff" : "#0969da";
    const QString lnFg = "#8b949e";
    const QString headBg = dark ? "#161b22" : "#f6f8fa";
    const QString border = dark ? "#30363d" : "#d0d7de";
    const QString gutterBg = dark ? "#0d1117" : "#f6f8fa";
    const QString fg = dark ? "#e6edf3" : "#1f2328";
    return QStringLiteral(
               ".fileblock { margin:0; }"
               ".fileheader { background:%1; padding:6px 10px; font-family:"
               "monospace; font-size:12px; border:1px solid %7; }"
               // Status octicon badge (image) sat next to the path.
               ".stbadge { margin-right:8px; vertical-align:middle; }"
               ".fpath { vertical-align:middle; }"
               ".fdir { color:%2; }"
               ".fname { font-weight:600; color:%8; }"
               ".fstat { color:%2; font-size:11px; }"
               // GitHub-style green/red proportion bar (filled block glyphs).
               ".barblk { font-family:monospace; letter-spacing:-1px; }"
               ".barblk.add { color:#3fb950; } .barblk.del { color:#f85149; }"
               ".viewedtoggle { color:%2; text-decoration:none; font-size:11px; }"
               ".viewedtoggle.on { color:#3fb950; }"
               ".filecomment { color:%2; text-decoration:none; font-size:15px; "
               "margin-right:14px; }"
               ".sadd { color:#3fb950; font-weight:700; }"
               ".sdel { color:#f85149; font-weight:700; }"
               ".difftable { font-family:monospace; font-size:%10px; width:100%; "
               "border-left:1px solid %7; border-right:1px solid %7; "
               "border-bottom:1px solid %7; }"
               ".imagetable { border-left:1px solid %7; border-right:1px solid %7; }"
               ".imgcell { width:50%; padding:10px; text-align:center; }"
               ".imgcell img { max-width:100%; max-height:360px; }"
               ".imgempty { color:%2; padding:60px 0; border:1px solid %7; }"
               ".imgcaption { color:%2; font-size:12px; margin-top:6px; }"
               "td.ln { color:%2; text-align:right; padding:0 10px; width:1%; "
               "font-size:%10px; white-space:nowrap; background:%9; "
               "border-right:1px solid %7; }"
               "td.code { white-space:pre; padding:0 10px; color:%8; "
               "font-size:%10px; }"
               ".add { background:%3; } .del { background:%4; }"
               ".hunk { color:%5; background:%6; }"
               "td.ln.hunk { background:%6; border-right:1px solid %7; }"
               // Split view: the new-side line-number gutter doubles as the
               // central divider between the old and new columns.
               "td.nln { border-left:1px solid %7; }"
               // Clickable line-number gutters (PR view) + inline comment rows.
               "td.lnlink a { color:%2; text-decoration:none; }"
               "td.notecell { padding:8px 12px; background:%1; "
               "border:1px solid %7; color:%8; white-space:normal; }"
               ".reviewthread { font-family:sans-serif; }"
               ".threadhead { color:%8; font-size:12px; margin-bottom:8px; }"
               ".threadstate { font-size:10px; font-weight:700; padding:1px 6px; "
               "border:1px solid %7; }"
               ".threadstate.resolved { color:#3fb950; }"
               ".threadstate.unresolved { color:#d29922; }"
               ".threadevent { margin-top:8px; padding-top:8px; "
               "border-top:1px solid %7; }"
               ".threadbody { color:%8; }"
               ".threadsystem { color:%2; font-size:11px; margin-top:6px; }"
               ".threadactions { margin-top:8px; }"
               ".threadactions a { color:#58a6ff; text-decoration:none; }"
               ".suggestion { background:%9; border:1px solid %7; color:%8; "
               "padding:8px; margin-top:6px; white-space:pre; }"
               ".notehdr { color:%2; font-size:11px; margin-bottom:4px; }")
        .arg(headBg, lnFg, addBg, delBg, hunkFg, hunkBg, border, fg, gutterBg)
        .arg(qBound(8, fontPt, 28));
}


} // namespace forkmesh::ui
