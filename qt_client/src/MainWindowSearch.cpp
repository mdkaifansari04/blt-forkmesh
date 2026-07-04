// MainWindowSearch: repo-scoped global search (Ctrl+K), split out of
// MainWindow.cpp. Issue #360 (Roadmap Phase 3).
//
// One overlay searches three things at once, scoped to the currently-open repo:
//   - Issues     — titles, bodies and comments (from IssueStore events)
//   - Pull reqs  — titles, descriptions and conversation (from PullStore events)
//   - Code       — `git grep` over the repo's working tree/mirror
//
// The parse + grep run entirely on a detached worker thread (runOffThread), so a
// big repo never freezes the window; a per-keystroke generation guard drops a
// stale result when the query has already moved on. Result counts and the grep
// output are capped hard — repo-scoped only, no per-record fan-out — so the
// overlay stays cheap even on a large tree. Each result row is clickable and
// jumps to the matching issue, PR, or file+line.
//
// These are MainWindow member functions defined in their own translation unit;
// the class itself is declared in MainWindow.h. Shared helpers live in
// MainWindowInternal.h / MainWindowShared.cpp (namespace forkmesh::ui).

#include "MainWindow.h"
#include "MainWindowInternal.h"

using namespace forkmesh::ui;

namespace {

// Hard caps so the overlay can never balloon into a huge payload or a slow list
// rebuild, whatever the query matches.
constexpr int kMaxIssueHits = 40;
constexpr int kMaxPullHits = 40;
constexpr int kMaxCodeHits = 80;
constexpr int kGrepTimeoutMs = 8000;

// One search result, filled on the worker thread and rendered on the GUI thread.
struct SearchHit {
    QString kind;    // "issue" | "pull" | "code"
    int number = 0;  // issue/pull number
    QString path;    // code: repo-relative path
    int line = 0;    // code: 1-based line number
    QString primary; // main row text
    QString detail;  // dimmer context (matched-in note or the matched code line)
};

// A short single-line snippet of `haystack` around the first match of `needle`,
// so a body/comment hit shows where it landed instead of just "matched".
QString snippetAround(const QString &haystack, const QString &needle)
{
    QString flat = haystack;
    flat.replace(QLatin1Char('\n'), QLatin1Char(' '));
    flat = flat.simplified();
    const int at = flat.indexOf(needle, 0, Qt::CaseInsensitive);
    if (at < 0)
        return flat.left(120);
    const int start = qMax(0, at - 30);
    QString out = flat.mid(start, 120);
    if (start > 0)
        out.prepend(QStringLiteral("…"));
    return out;
}

// Search a signed issue/PR body log for the query. Returns true and fills *where
// with a short context note the first time the query is found in title/body/
// comments.
bool matchIssue(const Issue &issue, const QString &q, QString *where)
{
    if (issue.title.contains(q, Qt::CaseInsensitive)) {
        *where = QStringLiteral("title");
        return true;
    }
    for (const IssueEvent &ev : issue.events) {
        if (ev.body.contains(q, Qt::CaseInsensitive)) {
            *where = ev.type == QLatin1String("open")
                         ? snippetAround(ev.body, q)
                         : QStringLiteral("comment: ") + snippetAround(ev.body, q);
            return true;
        }
    }
    return false;
}

bool matchPull(const PullRequest &pr, const QString &q, QString *where)
{
    if (pr.title.contains(q, Qt::CaseInsensitive)) {
        *where = QStringLiteral("title");
        return true;
    }
    if (pr.description.contains(q, Qt::CaseInsensitive)) {
        *where = snippetAround(pr.description, q);
        return true;
    }
    for (const PullEvent &ev : pr.events) {
        if (ev.body.contains(q, Qt::CaseInsensitive)) {
            *where = QStringLiteral("comment: ") + snippetAround(ev.body, q);
            return true;
        }
    }
    return false;
}

} // namespace

void MainWindow::openGlobalSearch()
{
    if (m_repoDetailIndex < 0 || m_repoDetailIndex >= m_repositories.size()) {
        flashMessage("Open a repository first to search it.", true);
        return;
    }

    QDialog dialog(this);
    m_searchDialog = &dialog;
    dialog.setWindowTitle("Search this repository");
    dialog.setObjectName("searchDialog");
    dialog.resize(680, 480);
    auto *layout = new QVBoxLayout(&dialog);
    layout->setContentsMargins(14, 14, 14, 14);
    layout->setSpacing(8);

    m_searchInput = new QLineEdit;
    m_searchInput->setObjectName("searchInput");
    m_searchInput->setClearButtonEnabled(true);
    m_searchInput->setPlaceholderText(
        "Search issues, pull requests and code…");
    layout->addWidget(m_searchInput);

    m_searchStatus = new QLabel(
        "Type at least 2 characters to search titles, comments and code.");
    m_searchStatus->setObjectName("statusLine");
    layout->addWidget(m_searchStatus);

    m_searchList = new QListWidget;
    m_searchList->setObjectName("searchResults");
    m_searchList->setUniformItemSizes(false);
    layout->addWidget(m_searchList, 1);

    // Debounce keystrokes so we don't re-parse the stores and spawn a `git grep`
    // on every letter — only after the user pauses briefly.
    auto *debounce = new QTimer(&dialog);
    debounce->setSingleShot(true);
    debounce->setInterval(180);
    connect(m_searchInput, &QLineEdit::textChanged, debounce,
            qOverload<>(&QTimer::start));
    connect(debounce, &QTimer::timeout, this,
            [this] { runGlobalSearch(m_searchInput->text()); });

    // Enter/click on a result closes the overlay and jumps to the match.
    auto activate = [this, &dialog](QListWidgetItem *item) {
        if (!item)
            return;
        const QString kind = item->data(Qt::UserRole).toString();
        const int number = item->data(Qt::UserRole + 1).toInt();
        const QString path = item->data(Qt::UserRole + 2).toString();
        const int line = item->data(Qt::UserRole + 3).toInt();
        dialog.accept();
        if (kind == QLatin1String("issue"))
            openIssueReference(number);
        else if (kind == QLatin1String("pull"))
            openPullReference(number);
        else if (kind == QLatin1String("code"))
            openRepoFileAtLine(path, line);
    };
    connect(m_searchList, &QListWidget::itemActivated, this, activate);
    connect(m_searchList, &QListWidget::itemClicked, this, activate);

    m_searchInput->setFocus();
    dialog.exec();

    // The dialog and its child widgets are torn down on return; clear the
    // dangling pointers so a late off-thread apply (see runGlobalSearch) no-ops.
    ++m_searchGen;
    m_searchDialog = nullptr;
    m_searchInput = nullptr;
    m_searchList = nullptr;
    m_searchStatus = nullptr;
}

void MainWindow::runGlobalSearch(const QString &rawQuery)
{
    if (!m_searchList || !m_searchStatus)
        return;
    const int gen = ++m_searchGen;
    m_searchList->clear();

    const QString query = rawQuery.trimmed();
    if (query.size() < 2) {
        m_searchStatus->setText(
            "Type at least 2 characters to search titles, comments and code.");
        return;
    }
    m_searchStatus->setText(QStringLiteral("Searching for “%1”…").arg(query));

    // Capture everything the worker needs by value — nothing GUI-owned is touched
    // inside the work lambda. The stores are read-only here (no signing), so the
    // identity pointer is only ever read.
    const RepositoryRecord &repo =
        writableRecordFor(m_repositories.at(m_repoDetailIndex));
    const QString localPath = repo.localPath;
    const QString mirrorPath = repo.mirrorPath;
    const ForkMeshIdentity *identity = &m_profileIdentity;
    const QString userName = m_userName;
    const QString gitDir = repoGitDir();
    const QString ref = currentRef();

    runOffThread<QVector<SearchHit>>(
        [=]() -> QVector<SearchHit> {
            QVector<SearchHit> hits;

            // Issues + PRs: parse the stores and match titles/bodies/comments.
            const IssueStore issues(localPath, mirrorPath, identity, userName);
            int issueCount = 0;
            for (const Issue &issue : issues.loadAll()) {
                if (issueCount >= kMaxIssueHits)
                    break;
                if (issue.isDeleted())
                    continue;
                QString where;
                if (!matchIssue(issue, query, &where))
                    continue;
                SearchHit hit;
                hit.kind = QStringLiteral("issue");
                hit.number = issue.number;
                hit.primary = QStringLiteral("#%1  %2")
                                  .arg(issue.number)
                                  .arg(issue.title);
                hit.detail = where;
                hits.append(hit);
                ++issueCount;
            }

            const PullStore pulls(localPath, mirrorPath, identity, userName);
            int pullCount = 0;
            for (const PullRequest &pr : pulls.loadAll()) {
                if (pullCount >= kMaxPullHits)
                    break;
                QString where;
                if (!matchPull(pr, query, &where))
                    continue;
                SearchHit hit;
                hit.kind = QStringLiteral("pull");
                hit.number = pr.number;
                hit.primary =
                    QStringLiteral("#%1  %2").arg(pr.number).arg(pr.title);
                hit.detail = where;
                hits.append(hit);
                ++pullCount;
            }

            // Code: one `git grep` over the tracked tree at the current ref, which
            // works for both a real checkout and a bare mirror. Fixed-string,
            // case-insensitive; issues/ and pulls/ are excluded so their markdown
            // doesn't double up the matches already surfaced above.
            if (!gitDir.isEmpty()) {
                QProcess grep;
                grep.start(QStringLiteral("git"),
                           {QStringLiteral("-C"), gitDir, QStringLiteral("grep"),
                            QStringLiteral("-n"), QStringLiteral("-I"),
                            QStringLiteral("-i"), QStringLiteral("-F"),
                            QStringLiteral("-e"), query, ref, QStringLiteral("--"),
                            QStringLiteral(":!issues"), QStringLiteral(":!pulls")});
                if (grep.waitForFinished(kGrepTimeoutMs)) {
                    // `git grep <rev>` prefixes each line with "<rev>:"; strip it,
                    // then split the remaining "path:line:text".
                    static const QRegularExpression rowRe(
                        QStringLiteral("^(.+?):(\\d+):(.*)$"));
                    const QString prefix = ref + QLatin1Char(':');
                    const QList<QByteArray> lines =
                        grep.readAllStandardOutput().split('\n');
                    int codeCount = 0;
                    for (const QByteArray &raw : lines) {
                        if (codeCount >= kMaxCodeHits)
                            break;
                        QString line = QString::fromUtf8(raw);
                        if (line.startsWith(prefix))
                            line = line.mid(prefix.size());
                        const QRegularExpressionMatch m = rowRe.match(line);
                        if (!m.hasMatch())
                            continue;
                        SearchHit hit;
                        hit.kind = QStringLiteral("code");
                        hit.path = m.captured(1);
                        hit.line = m.captured(2).toInt();
                        hit.primary = QStringLiteral("%1:%2").arg(hit.path).arg(
                            hit.line);
                        hit.detail = m.captured(3).trimmed().left(160);
                        hits.append(hit);
                        ++codeCount;
                    }
                } else {
                    grep.kill();
                    grep.waitForFinished(1000);
                }
            }
            return hits;
        },
        [this, gen, query](const QVector<SearchHit> &hits) {
            // Drop a result the user has already typed past, or one that arrives
            // after the overlay closed.
            if (gen != m_searchGen || !m_searchList || !m_searchStatus)
                return;

            int issues = 0, pulls = 0, code = 0;
            for (const SearchHit &hit : hits) {
                QString iconName;
                QColor color;
                if (hit.kind == QLatin1String("issue")) {
                    iconName = QStringLiteral("issue-opened");
                    color = QColor("#3fb950");
                    ++issues;
                } else if (hit.kind == QLatin1String("pull")) {
                    iconName = QStringLiteral("git-pull-request");
                    color = QColor("#a371f7");
                    ++pulls;
                } else {
                    iconName = QStringLiteral("code");
                    color = QColor("#58a6ff");
                    ++code;
                }
                auto *item = new QListWidgetItem(
                    themedOcticon(iconName, color, 16),
                    hit.detail.isEmpty()
                        ? hit.primary
                        : hit.primary + QStringLiteral("\n") + hit.detail);
                item->setData(Qt::UserRole, hit.kind);
                item->setData(Qt::UserRole + 1, hit.number);
                item->setData(Qt::UserRole + 2, hit.path);
                item->setData(Qt::UserRole + 3, hit.line);
                m_searchList->addItem(item);
            }

            if (hits.isEmpty()) {
                m_searchStatus->setText(
                    QStringLiteral("No matches for “%1”.").arg(query));
            } else {
                m_searchStatus->setText(
                    QStringLiteral("%1 issue%2 · %3 pull request%4 · %5 code match%6")
                        .arg(issues)
                        .arg(issues == 1 ? "" : "s")
                        .arg(pulls)
                        .arg(pulls == 1 ? "" : "s")
                        .arg(code)
                        .arg(code == 1 ? "" : "es"));
            }
        });
}
