// MainWindowSearch: the Ctrl+K search overlay, split out of MainWindow.cpp.
// Issue #360 (Roadmap Phase 3), widened to the whole node by adhoc #28.
//
// One overlay searches everything this node holds. Node-wide, whatever repo is
// open:
//   - Repositories   — owner/name and description
//   - Agent sessions — task name, prompt, issue title, branch and worktree path
//   - Workflow runs  — workflow name/path, ref and commit
// and, for the repository currently open:
//   - Issues / Pull requests / Discussions — titles, bodies and comments
//   - Projects / Milestones — titles and descriptions
//   - Branches / Worktrees / Tags — names, tip subjects and checkout paths
//   - Commits — messages (`git log --grep`)
//   - Files   — path names (`git ls-tree`)
//   - Code    — file contents (`git grep`)
//
// Everything runs on detached worker threads (runOffThread) in two waves: the
// cheap metadata first, then the content scan (`git grep` / `git log`), so the
// list fills immediately and a big repo never freezes the window. A
// per-keystroke generation guard drops a wave the query has already moved past,
// and every category is capped hard — no per-record fan-out — so the overlay
// stays cheap whatever the query matches. Each row is clickable and jumps to
// the thing it matched.
//
// These are MainWindow member functions defined in their own translation unit;
// the class itself is declared in MainWindow.h. Shared helpers live in
// MainWindowInternal.h / MainWindowShared.cpp (namespace forkmesh::ui).

#include "MainWindow.h"
#include "MainWindowInternal.h"

#include "GlobalSearchMatch.h"

using namespace forkmesh::ui;
using namespace forkmesh::search;

namespace {

// Hard caps so the overlay can never balloon into a huge payload or a slow list
// rebuild, whatever the query matches.
constexpr int kMaxPerCategory = 40;
constexpr int kMaxCodeHits = 80; // file contents: the one deliberately longer list
constexpr int kMaxCommits = 200; // rows `git log` is allowed to walk back over
constexpr int kGrepTimeoutMs = 8000;
constexpr int kGitTimeoutMs = 5000;

// The repository fields the worker matches on. Snapshotted by value on the GUI
// thread so the worker never touches m_repositories.
struct RepoBrief {
    int index = 0;
    QString owner;
    QString name;
    QString description;
};

// Hits gathered by one wave, with a per-category cap so no single category can
// flood the list.
struct HitBag {
    QVector<GlobalSearchHit> hits;
    QHash<QString, int> counts;

    bool room(const char *kind, int cap = kMaxPerCategory) const
    {
        return counts.value(QLatin1String(kind)) < cap;
    }
    void add(const char *kind, GlobalSearchHit hit)
    {
        hit.kind = QLatin1String(kind);
        counts[hit.kind] += 1;
        hits.append(std::move(hit));
    }
};

// Run `git <args>` in `dir` on the worker thread and return its stdout split
// into non-empty lines. A hung git is killed at the timeout and reads as "no
// matches" rather than stalling the wave.
QStringList gitLines(const QString &dir, const QStringList &args, int timeoutMs)
{
    if (dir.isEmpty())
        return {};
    QProcess git;
    git.start(QStringLiteral("git"),
              QStringList{QStringLiteral("-C"), dir} + args);
    if (!git.waitForFinished(timeoutMs)) {
        git.kill();
        git.waitForFinished(1000);
        return {};
    }
    return QString::fromUtf8(git.readAllStandardOutput())
        .split(QLatin1Char('\n'), Qt::SkipEmptyParts);
}

} // namespace

void MainWindow::openGlobalSearch()
{
    QDialog dialog(this);
    m_searchDialog = &dialog;
    dialog.setWindowTitle("Search everything");
    dialog.setObjectName("searchDialog");
    dialog.resize(760, 560);
    auto *layout = new QVBoxLayout(&dialog);
    layout->setContentsMargins(14, 14, 14, 14);
    layout->setSpacing(8);

    m_searchInput = new QLineEdit;
    m_searchInput->setObjectName("searchInput");
    m_searchInput->setClearButtonEnabled(true);
    m_searchInput->setPlaceholderText(
        "Search repositories, issues, pull requests, agents, branches, "
        "worktrees, commits and code…");
    layout->addWidget(m_searchInput);

    m_searchStatus = new QLabel(
        "Type at least 2 characters to search everything on this node.");
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

    // Enter/click on a result closes the overlay and jumps to the match. Group
    // headers carry no kind and are inert.
    auto activate = [this, &dialog](QListWidgetItem *item) {
        if (!item)
            return;
        GlobalSearchHit hit;
        hit.kind = item->data(Qt::UserRole).toString();
        if (hit.kind.isEmpty())
            return;
        hit.number = item->data(Qt::UserRole + 1).toInt();
        hit.path = item->data(Qt::UserRole + 2).toString();
        hit.line = item->data(Qt::UserRole + 3).toInt();
        hit.repoIndex = item->data(Qt::UserRole + 4).toInt();
        dialog.accept();
        activateGlobalSearchHit(hit);
    };
    connect(m_searchList, &QListWidget::itemActivated, this, activate);
    connect(m_searchList, &QListWidget::itemClicked, this, activate);

    m_searchInput->setFocus();
    dialog.exec();

    // The dialog and its child widgets are torn down on return; clear the
    // dangling pointers so a late off-thread apply (see runGlobalSearch) no-ops.
    ++m_searchGen;
    m_searchWavesPending = 0;
    m_searchHits.clear();
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
    m_searchHits.clear();
    m_searchWavesPending = 0;
    m_searchList->clear();

    const QString query = rawQuery.trimmed();
    if (query.size() < 2) {
        m_searchStatus->setText(
            "Type at least 2 characters to search everything on this node.");
        return;
    }
    m_searchStatus->setText(QStringLiteral("Searching for “%1”…").arg(query));

    // Capture everything the workers need by value — nothing GUI-owned is
    // touched inside a work lambda. The stores are read-only here (no signing),
    // so the identity pointer is only ever read.
    QVector<RepoBrief> repos;
    repos.reserve(m_repositories.size());
    for (int i = 0; i < m_repositories.size(); ++i) {
        const RepositoryRecord &record = m_repositories.at(i);
        repos.append({i, record.owner, record.name, record.description});
    }
    const int openIndex = m_repoDetailIndex;
    const bool repoOpen = openIndex >= 0 && openIndex < m_repositories.size();
    QString localPath, mirrorPath;
    if (repoOpen) {
        const RepositoryRecord &repo =
            writableRecordFor(m_repositories.at(openIndex));
        localPath = repo.localPath;
        mirrorPath = repo.mirrorPath;
    }
    const ForkMeshIdentity *identity = &m_profileIdentity;
    const QString userName = m_userName;
    const QString gitDir = repoOpen ? repoGitDir() : QString();
    const QString ref = repoOpen ? currentRef() : QString();
    const QList<AgentSession> sessions = m_agentSessions;
    const QHash<int, AgentDiffStat> diffStats = m_agentDiffStats;
    const QList<ActionRun> runs = m_actionRuns;

    // Both waves land here: append what arrived and redraw the grouped list, so
    // the metadata is usable while the content scan is still running.
    auto deliver = [this, gen, query](const QVector<GlobalSearchHit> &hits) {
        // Drop a result the user has already typed past, or one that arrives
        // after the overlay closed.
        if (gen != m_searchGen || !m_searchList || !m_searchStatus)
            return;
        m_searchHits += hits;
        if (m_searchWavesPending > 0)
            --m_searchWavesPending;
        rebuildGlobalSearchList(query);
    };

    m_searchWavesPending = 2;

    // Wave 1 — metadata. Store parses and `git for-each-ref`/`ls-tree` reads:
    // everything that answers "what is this thing called" rather than "what is
    // inside it".
    runOffThread<QVector<GlobalSearchHit>>(
        [=]() -> QVector<GlobalSearchHit> {
            HitBag bag;

            // Repositories — every repo on this node, not just the open one, so
            // the overlay doubles as the way to jump between them.
            for (const RepoBrief &repo : repos) {
                if (!bag.room("repo"))
                    break;
                const QString slug =
                    QStringLiteral("%1/%2").arg(repo.owner, repo.name);
                QString where;
                if (containsFold(slug, query))
                    where = QStringLiteral("repository");
                else if (containsFold(repo.description, query))
                    where = snippetAround(repo.description, query);
                else
                    continue;
                GlobalSearchHit hit;
                hit.number = repo.index;
                hit.repoIndex = repo.index;
                hit.primary = slug;
                hit.detail = repo.index == openIndex
                                 ? where + QStringLiteral(" · open")
                                 : where;
                bag.add("repo", hit);
            }

            // Issues + PRs + discussions + projects + milestones: parse the open
            // repo's stores and match titles/bodies/comments.
            if (!localPath.isEmpty() || !mirrorPath.isEmpty()) {
                const IssueStore issues(localPath, mirrorPath, identity, userName);
                for (const Issue &issue : issues.loadAll()) {
                    if (!bag.room("issue"))
                        break;
                    if (issue.isDeleted())
                        continue;
                    QString where;
                    if (!matchIssue(issue, query, &where))
                        continue;
                    GlobalSearchHit hit;
                    hit.number = issue.number;
                    hit.primary = QStringLiteral("#%1  %2")
                                      .arg(issue.number)
                                      .arg(issue.title);
                    hit.detail = where;
                    bag.add("issue", hit);
                }

                for (const IssueMilestone &milestone : issues.loadMilestones()) {
                    if (!bag.room("milestone"))
                        break;
                    QString where;
                    if (!matchTitleBody(query, milestone.title,
                                        milestone.description, &where))
                        continue;
                    GlobalSearchHit hit;
                    hit.path = milestone.title;
                    hit.primary = milestone.title;
                    hit.detail = milestone.status + QStringLiteral(" · ") + where;
                    bag.add("milestone", hit);
                }

                const PullStore pulls(localPath, mirrorPath, identity, userName);
                for (const PullRequest &pr : pulls.loadAll()) {
                    if (!bag.room("pull"))
                        break;
                    QString where;
                    if (!matchPull(pr, query, &where))
                        continue;
                    GlobalSearchHit hit;
                    hit.number = pr.number;
                    hit.primary =
                        QStringLiteral("#%1  %2").arg(pr.number).arg(pr.title);
                    hit.detail = where;
                    bag.add("pull", hit);
                }

                const DiscussionStore discussions(localPath, mirrorPath, identity,
                                                  userName);
                for (const Discussion &d : discussions.loadAll()) {
                    if (!bag.room("discussion"))
                        break;
                    QString where;
                    if (!matchDiscussion(d, query, &where))
                        continue;
                    GlobalSearchHit hit;
                    hit.number = d.number;
                    hit.primary =
                        QStringLiteral("#%1  %2").arg(d.number).arg(d.title);
                    hit.detail = d.category.isEmpty()
                                     ? where
                                     : d.category + QStringLiteral(" · ") + where;
                    bag.add("discussion", hit);
                }

                const ProjectStore projects(localPath, mirrorPath, identity,
                                            userName);
                for (const Project &p : projects.loadAll()) {
                    if (!bag.room("project"))
                        break;
                    if (p.isDeleted())
                        continue;
                    QString where;
                    if (!matchProject(p, query, &where))
                        continue;
                    GlobalSearchHit hit;
                    hit.number = p.number;
                    hit.primary =
                        QStringLiteral("#%1  %2").arg(p.number).arg(p.title);
                    hit.detail = where;
                    bag.add("project", hit);
                }
            }

            // Agent sessions — every run this node has launched, whatever repo
            // (owner/name on a session is its repository), matched on the task
            // text, the branch it works on and its worktree path.
            for (const AgentSession &session : sessions) {
                if (!bag.room("agent"))
                    break;
                // A session's own checkout is only known once the agents list has
                // measured it, so this matches the paths we have and leaves the
                // rest to the Worktrees category above.
                const QString worktree = diffStats.value(session.id).worktree;
                QString where;
                if (containsFold(session.issueTitle, query))
                    where = QStringLiteral("task");
                else if (containsFold(session.prompt, query))
                    where = snippetAround(session.prompt, query);
                else if (containsFold(session.branchName, query))
                    where = QStringLiteral("branch ") + session.branchName;
                else if (containsFold(worktree, query))
                    where = QStringLiteral("worktree ") + worktree;
                else
                    continue;
                const QString task = session.issueTitle.isEmpty()
                                         ? session.prompt.simplified().left(90)
                                         : session.issueTitle;
                GlobalSearchHit hit;
                hit.number = session.id;
                hit.primary = task.isEmpty()
                                  ? QStringLiteral("Session %1").arg(session.id)
                                  : task;
                hit.detail = QStringLiteral("%1/%2 · %3 · %4")
                                 .arg(session.owner, session.name,
                                      session.status, where);
                for (const RepoBrief &repo : repos) {
                    if (repo.owner == session.owner && repo.name == session.name)
                        hit.repoIndex = repo.index;
                }
                bag.add("agent", hit);
            }

            // Workflow runs — local CI history, also node-wide.
            for (const ActionRun &run : runs) {
                if (!bag.room("action"))
                    break;
                QString where;
                if (containsFold(run.workflowName, query))
                    where = QStringLiteral("workflow");
                else if (containsFold(run.workflowPath, query))
                    where = run.workflowPath;
                else if (containsFold(run.ref, query))
                    where = run.ref;
                else if (containsFold(run.commit, query))
                    where = run.commit.left(12);
                else
                    continue;
                GlobalSearchHit hit;
                hit.number = run.id;
                hit.primary = QStringLiteral("%1  %2").arg(
                    run.workflowName.isEmpty() ? run.workflowPath
                                               : run.workflowName,
                    QStringLiteral("#%1").arg(run.id));
                hit.detail = QStringLiteral("%1/%2 · %3 · %4")
                                 .arg(run.owner, run.name, run.status, where);
                bag.add("action", hit);
            }

            // Branches — local heads and remote-tracking refs, matched on the
            // name or the subject of the commit they point at.
            for (const QString &row :
                 gitLines(gitDir,
                          {QStringLiteral("for-each-ref"),
                           QStringLiteral("--sort=-committerdate"),
                           QStringLiteral("--format=%(refname:short)%1f%(objectname:"
                                          "short)%1f%(contents:subject)"),
                           QStringLiteral("refs/heads"),
                           QStringLiteral("refs/remotes")},
                          kGitTimeoutMs)) {
                if (!bag.room("branch"))
                    break;
                const QStringList f = row.split(QChar(0x1f));
                if (f.isEmpty())
                    continue;
                const QString name = f.value(0);
                const QString subject = f.value(2);
                QString where;
                if (containsFold(name, query))
                    where = f.value(1);
                else if (containsFold(subject, query))
                    where = f.value(1) + QStringLiteral("  ") + subject;
                else
                    continue;
                GlobalSearchHit hit;
                hit.path = name;
                hit.primary = name;
                hit.detail = where;
                bag.add("branch", hit);
            }

            // Worktrees — every linked checkout of the open repo, matched on its
            // path or the branch it is on. `forkmesh/pulls` is PullStore's own
            // private worktree, not a workspace, so it stays hidden here too.
            for (const WorktreeRecord &wt : parseWorktreePorcelain(
                     gitLines(gitDir,
                              {QStringLiteral("worktree"), QStringLiteral("list"),
                               QStringLiteral("--porcelain")},
                              kGitTimeoutMs))) {
                if (!bag.room("worktree"))
                    break;
                if (wt.branch == QLatin1String("forkmesh/pulls"))
                    continue;
                if (!containsFold(wt.path, query) &&
                    !containsFold(wt.branch, query))
                    continue;
                GlobalSearchHit hit;
                hit.path = wt.branch; // switchToWorktree() navigates by branch
                hit.primary =
                    wt.detached
                        ? QStringLiteral("(detached %1)").arg(wt.head.left(8))
                        : (wt.branch.isEmpty() ? QStringLiteral("(no branch)")
                                               : wt.branch);
                hit.detail = wt.path;
                bag.add("worktree", hit);
            }

            // Tags — release refs, matched on name or tag/commit subject.
            for (const QString &row :
                 gitLines(gitDir,
                          {QStringLiteral("for-each-ref"),
                           QStringLiteral("--sort=-creatordate"),
                           QStringLiteral("--format=%(refname:short)%1f%(objectname:"
                                          "short)%1f%(contents:subject)"),
                           QStringLiteral("refs/tags")},
                          kGitTimeoutMs)) {
                if (!bag.room("tag"))
                    break;
                const QStringList f = row.split(QChar(0x1f));
                if (f.isEmpty())
                    continue;
                const QString name = f.value(0);
                if (!containsFold(name, query) && !containsFold(f.value(2), query))
                    continue;
                GlobalSearchHit hit;
                hit.path = name;
                hit.primary = name;
                hit.detail = f.value(2);
                bag.add("tag", hit);
            }

            // File names in the current tree — a path match is usually what
            // "find me that file" means, and `git grep` below never reports one.
            if (!gitDir.isEmpty() && !ref.isEmpty()) {
                for (const QString &path :
                     gitLines(gitDir,
                              {QStringLiteral("ls-tree"), QStringLiteral("-r"),
                               QStringLiteral("--name-only"), ref},
                              kGitTimeoutMs)) {
                    if (!bag.room("file"))
                        break;
                    if (!containsFold(path, query))
                        continue;
                    GlobalSearchHit hit;
                    hit.path = path;
                    hit.line = 1;
                    hit.primary = path;
                    bag.add("file", hit);
                }
            }

            return bag.hits;
        },
        deliver);

    // Wave 2 — content. The two scans that actually read history and file bytes,
    // kept apart so they never hold up the metadata above.
    runOffThread<QVector<GlobalSearchHit>>(
        [=]() -> QVector<GlobalSearchHit> {
            HitBag bag;
            if (gitDir.isEmpty())
                return bag.hits;

            // Commit messages at the current ref.
            for (const QString &row :
                 gitLines(gitDir,
                          {QStringLiteral("log"), QStringLiteral("-i"),
                           QStringLiteral("--fixed-strings"),
                           QStringLiteral("--grep=") + query,
                           QStringLiteral("--format=%H%x1f%h%x1f%ar%x1f%s"),
                           QStringLiteral("-n"),
                           QString::number(kMaxCommits), ref},
                          kGrepTimeoutMs)) {
                if (!bag.room("commit"))
                    break;
                const QStringList f = row.split(QChar(0x1f));
                if (f.size() < 4)
                    continue;
                GlobalSearchHit hit;
                hit.path = f.at(0); // full hash — what showCommit() wants
                hit.primary = QStringLiteral("%1  %2").arg(f.at(1), f.at(3));
                hit.detail = f.at(2);
                bag.add("commit", hit);
            }

            // File contents: one `git grep` over the tracked tree at the current
            // ref, which works for both a real checkout and a bare mirror.
            // Fixed-string, case-insensitive; issue metadata and pulls/ are
            // excluded so their records don't double up the matches already
            // surfaced above.
            QProcess grep;
            grep.start(QStringLiteral("git"),
                       {QStringLiteral("-C"), gitDir, QStringLiteral("grep"),
                        QStringLiteral("-n"), QStringLiteral("-I"),
                        QStringLiteral("-i"), QStringLiteral("-F"),
                        QStringLiteral("-e"), query, ref, QStringLiteral("--"),
                        QStringLiteral(":!.forkmesh/issues"),
                        QStringLiteral(":!pulls")});
            if (grep.waitForFinished(kGrepTimeoutMs)) {
                const QList<QByteArray> lines =
                    grep.readAllStandardOutput().split('\n');
                for (const QByteArray &raw : lines) {
                    if (!bag.room("code", kMaxCodeHits))
                        break;
                    const CodeRow row =
                        parseGrepRow(QString::fromUtf8(raw), ref);
                    if (!row.valid)
                        continue;
                    GlobalSearchHit hit;
                    hit.path = row.path;
                    hit.line = row.line;
                    hit.primary =
                        QStringLiteral("%1:%2").arg(hit.path).arg(hit.line);
                    hit.detail = row.text.left(160);
                    bag.add("code", hit);
                }
            } else {
                grep.kill();
                grep.waitForFinished(1000);
            }
            return bag.hits;
        },
        deliver);
}

void MainWindow::rebuildGlobalSearchList(const QString &query)
{
    if (!m_searchList || !m_searchStatus)
        return;

    // A wave landing mid-scroll shouldn't yank the row the user is on.
    const int keepRow = m_searchList->currentRow();
    m_searchList->clear();

    QStringList summary;
    for (int cat = 0; cat < categoryCount(); ++cat) {
        const Category &category = categoryAt(cat);
        const QColor color(QLatin1String(category.color));
        QVector<const GlobalSearchHit *> rows;
        for (const GlobalSearchHit &hit : m_searchHits)
            if (categoryIndexOf(hit.kind) == cat)
                rows.append(&hit);
        if (rows.isEmpty())
            continue;
        summary.append(QStringLiteral("%1 %2")
                           .arg(rows.size())
                           .arg(QString::fromLatin1(category.title).toLower()));

        // Group header: inert (no kind) so clicking it does nothing.
        auto *header = new QListWidgetItem(
            QStringLiteral("%1 (%2)")
                .arg(QString::fromLatin1(category.title))
                .arg(rows.size()));
        header->setFlags(Qt::NoItemFlags);
        QFont headerFont = header->font();
        headerFont.setBold(true);
        header->setFont(headerFont);
        header->setForeground(QColor("#8b949e"));
        m_searchList->addItem(header);

        for (const GlobalSearchHit *hit : rows) {
            auto *item = new QListWidgetItem(
                themedOcticon(QString::fromLatin1(category.icon), color, 16),
                hit->detail.isEmpty()
                    ? hit->primary
                    : hit->primary + QStringLiteral("\n") + hit->detail);
            item->setData(Qt::UserRole, hit->kind);
            item->setData(Qt::UserRole + 1, hit->number);
            item->setData(Qt::UserRole + 2, hit->path);
            item->setData(Qt::UserRole + 3, hit->line);
            item->setData(Qt::UserRole + 4, hit->repoIndex);
            m_searchList->addItem(item);
        }
    }

    if (keepRow >= 0 && keepRow < m_searchList->count())
        m_searchList->setCurrentRow(keepRow);

    const bool running = m_searchWavesPending > 0;
    if (m_searchHits.isEmpty()) {
        m_searchStatus->setText(
            running ? QStringLiteral("Searching for “%1”…").arg(query)
                    : QStringLiteral("No matches for “%1”.").arg(query));
        return;
    }
    QString text = summary.join(QStringLiteral(" · "));
    if (running)
        text += QStringLiteral("  ·  still searching…");
    m_searchStatus->setText(text);
}

void MainWindow::activateGlobalSearchHit(const GlobalSearchHit &hit)
{
    // A hit from another repository needs that repository open before the
    // per-repo views below can show it.
    if (hit.repoIndex >= 0 && hit.repoIndex != m_repoDetailIndex &&
        hit.repoIndex < m_repositories.size())
        openRepoDetail(hit.repoIndex);

    auto selectTab = [this](int tab) {
        if (tab < 0)
            return;
        if (m_repoDetailTabs && m_repoDetailTabs->button(tab))
            m_repoDetailTabs->button(tab)->setChecked(true);
        if (m_repoDetailStack)
            m_repoDetailStack->setCurrentIndex(tab);
    };

    if (hit.kind == QLatin1String("repo")) {
        if (hit.number != m_repoDetailIndex) // the step above may have opened it
            openRepoDetail(hit.number);
    } else if (hit.kind == QLatin1String("issue")) {
        openIssueReference(hit.number);
    } else if (hit.kind == QLatin1String("pull")) {
        openPullReference(hit.number);
    } else if (hit.kind == QLatin1String("discussion")) {
        selectTab(5);
        showDiscussion(hit.number);
    } else if (hit.kind == QLatin1String("project")) {
        selectTab(m_projectsTabIndex);
        showProject(hit.number);
    } else if (hit.kind == QLatin1String("milestone")) {
        selectTab(2); // Issues, then its Milestones sub-tab
        if (m_issueTabGroup && m_issueTabGroup->button(1))
            m_issueTabGroup->button(1)->click();
    } else if (hit.kind == QLatin1String("agent")) {
        selectTab(3);
        showAgentSession(hit.number);
    } else if (hit.kind == QLatin1String("action")) {
        openActionRunFromNotification(hit.number);
    } else if (hit.kind == QLatin1String("branch")) {
        switchToBranch(hit.path);
    } else if (hit.kind == QLatin1String("worktree")) {
        switchToWorktree(hit.path);
    } else if (hit.kind == QLatin1String("tag") ||
               hit.kind == QLatin1String("commit")) {
        openCommitHashReference(hit.path);
    } else if (hit.kind == QLatin1String("file") ||
               hit.kind == QLatin1String("code")) {
        openRepoFileAtLine(hit.path, hit.line);
    }
}
