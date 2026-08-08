
#include "MainWindow.h"
#include "MainWindowInternal.h"

#include "GlobalSearchMatch.h"

using namespace forkmesh::ui;
using namespace forkmesh::search;

namespace {

constexpr int kMaxPerCategory = 40;
constexpr int kMaxCodeHits = 80; // file contents: the one deliberately longer list
constexpr int kMaxCommits = 200; // rows `git log` is allowed to walk back over
constexpr int kGrepTimeoutMs = 8000;
constexpr int kGitTimeoutMs = 5000;

struct RepoBrief {
    int index = 0;
    QString owner;
    QString name;
    QString description;
};

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

    auto *debounce = new QTimer(&dialog);
    debounce->setSingleShot(true);
    debounce->setInterval(180);
    connect(m_searchInput, &QLineEdit::textChanged, debounce,
            qOverload<>(&QTimer::start));
    connect(debounce, &QTimer::timeout, this,
            [this] { runGlobalSearch(m_searchInput->text()); });

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

    auto deliver = [this, gen, query](const QVector<GlobalSearchHit> &hits) {
        if (gen != m_searchGen || !m_searchList || !m_searchStatus)
            return;
        m_searchHits += hits;
        if (m_searchWavesPending > 0)
            --m_searchWavesPending;
        rebuildGlobalSearchList(query);
    };

    m_searchWavesPending = 2;

    runOffThread<QVector<GlobalSearchHit>>(
        [=]() -> QVector<GlobalSearchHit> {
            HitBag bag;

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

            for (const AgentSession &session : sessions) {
                if (!bag.room("agent"))
                    break;
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

    runOffThread<QVector<GlobalSearchHit>>(
        [=]() -> QVector<GlobalSearchHit> {
            HitBag bag;
            if (gitDir.isEmpty())
                return bag.hits;

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
