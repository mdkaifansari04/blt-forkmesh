// Merge queue: land pull requests one after the next without babysitting them.
//
// A repository with the queue switched on (Settings -> Automation) grows a
// "Queue" tile on every open pull request and a queue panel under the
// pull-request list. Queued pull requests are worked in order: for each one the
// runner brings its head branch up to date with the base branch — which the
// previous merge just moved — dry-runs the merge, and merges it. Anything it
// cannot fix on its own (a conflict a human has to resolve, a missing peer
// approval) leaves that entry in the queue with the reason on its row and moves
// on to the next one, so one stuck pull request never wedges the queue.
//
// The queue is the repository's, not the session's: it is persisted with the
// record (RepositoryRecord::mergeQueue) and picks up where it left off. See
// MergeQueue.h for the data model — everything here is the UI and the git work
// around it.

#include "MainWindow.h"
#include "MainWindowInternal.h"

#include "MergeQueue.h"
#include "PullStore.h"

#include <QDateTime>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>

using namespace forkmesh::ui;

namespace {

// Come back to a queue that still has work in it. Long enough that a queue
// which is only waiting on a human (a conflict, an approval) costs nothing to
// keep, short enough that the fix is picked up without another click.
constexpr int kMergeQueueRetryMs = 20000;
// After a merge the base branch has moved, so every entry behind it needs
// re-checking (and probably updating) — come straight back.
constexpr int kMergeQueueNextMs = 1500;

QString mergeQueueStateColor(const QString &state)
{
    if (state == MergeQueueState::Blocked)
        return QStringLiteral("#d29922");
    if (state == MergeQueueState::Failed)
        return QStringLiteral("#f85149");
    if (state == MergeQueueState::Merging || state == MergeQueueState::Updating)
        return QStringLiteral("#58a6ff");
    return QStringLiteral("#8b949e");
}

QString mergeQueueStateCaption(const MergeQueueEntry &entry)
{
    if (!entry.detail.isEmpty())
        return entry.detail;
    if (entry.state == MergeQueueState::Updating)
        return QStringLiteral("Updating from the base branch");
    if (entry.state == MergeQueueState::Merging)
        return QStringLiteral("Merging");
    if (entry.state == MergeQueueState::Blocked)
        return QStringLiteral("Waiting");
    if (entry.state == MergeQueueState::Failed)
        return QStringLiteral("Last attempt failed");
    return QStringLiteral("Queued");
}

} // namespace

// ---- Panel -----------------------------------------------------------------

QWidget *MainWindow::buildMergeQueuePanel()
{
    m_mergeQueuePanel = new QWidget;
    m_mergeQueuePanel->setObjectName(QStringLiteral("mergeQueuePanel"));
    auto *col = new QVBoxLayout(m_mergeQueuePanel);
    col->setContentsMargins(8, 6, 8, 8);
    col->setSpacing(4);

    auto *headerRow = new QHBoxLayout;
    headerRow->setContentsMargins(0, 0, 0, 0);
    auto *heading = new QLabel(QStringLiteral("MERGE QUEUE"));
    heading->setObjectName(QStringLiteral("sectionLabel"));
    headerRow->addWidget(heading);
    m_mergeQueueSummary = new QLabel;
    m_mergeQueueSummary->setObjectName(QStringLiteral("statusLine"));
    headerRow->addWidget(m_mergeQueueSummary);
    headerRow->addStretch();

    auto smallButton = [](const QString &text, const char *icon,
                          const QString &tooltip) {
        auto *b = new QPushButton(text);
        b->setObjectName(QStringLiteral("ghostButton"));
        b->setProperty("buttonSize", "sm");
        b->setCursor(Qt::PointingHandCursor);
        setOcticon(b, QString::fromLatin1(icon), 14);
        b->setToolTip(tooltip);
        return b;
    };

    m_mergeQueuePauseButton = smallButton(
        QStringLiteral("Pause"), "hand",
        QStringLiteral("Stop working through the queue without losing its order"));
    connect(m_mergeQueuePauseButton, &QPushButton::clicked, this, [this] {
        const int index = m_repoDetailIndex;
        if (index < 0 || index >= m_repositories.size())
            return;
        setMergeQueuePaused(!m_repositories.at(index).mergeQueuePaused);
    });
    headerRow->addWidget(m_mergeQueuePauseButton);
    col->addLayout(headerRow);

    m_mergeQueueList = new QListWidget;
    m_mergeQueueList->setObjectName(QStringLiteral("overviewList"));
    enableHoverRowHighlight(m_mergeQueueList);
    m_mergeQueueList->setSelectionMode(QAbstractItemView::SingleSelection);
    m_mergeQueueList->setMaximumHeight(150);
    m_mergeQueueList->setToolTip(
        QStringLiteral("Pull requests waiting to merge, in the order they will "
                       "merge. Double-click one to open it."));
    connect(m_mergeQueueList, &QListWidget::itemDoubleClicked, this,
            [this](QListWidgetItem *item) {
                if (item)
                    switchToPullTab(item->data(Qt::UserRole).toInt());
            });
    connect(m_mergeQueueList, &QListWidget::currentRowChanged, this,
            [this](int) { refreshMergeQueuePanel(); });
    col->addWidget(m_mergeQueueList);

    m_mergeQueueUpButton = smallButton(
        QString(), "chevron-up",
        QStringLiteral("Merge the selected pull request earlier"));
    connect(m_mergeQueueUpButton, &QPushButton::clicked, this,
            [this] { moveMergeQueueSelection(-1); });
    m_mergeQueueDownButton = smallButton(
        QString(), "chevron-down",
        QStringLiteral("Merge the selected pull request later"));
    connect(m_mergeQueueDownButton, &QPushButton::clicked, this,
            [this] { moveMergeQueueSelection(1); });
    m_mergeQueueRemoveButton = smallButton(
        QStringLiteral("Remove"), "x",
        QStringLiteral("Take the selected pull request out of the queue (it stays "
                       "open)"));
    connect(m_mergeQueueRemoveButton, &QPushButton::clicked, this,
            &MainWindow::removeMergeQueueSelection);
    m_mergeQueueClearButton = smallButton(
        QStringLiteral("Clear"), "trash",
        QStringLiteral("Empty the queue — every pull request in it stays open"));
    connect(m_mergeQueueClearButton, &QPushButton::clicked, this,
            &MainWindow::clearMergeQueue);

    auto *buttonRow = new QHBoxLayout;
    buttonRow->setContentsMargins(0, 0, 0, 0);
    buttonRow->setSpacing(4);
    buttonRow->addWidget(m_mergeQueueUpButton);
    buttonRow->addWidget(m_mergeQueueDownButton);
    buttonRow->addWidget(m_mergeQueueRemoveButton);
    buttonRow->addStretch();
    buttonRow->addWidget(m_mergeQueueClearButton);
    col->addLayout(buttonRow);

    m_mergeQueuePanel->hide(); // shown by refreshMergeQueuePanel() when enabled
    return m_mergeQueuePanel;
}

void MainWindow::refreshMergeQueuePanel()
{
    if (!m_mergeQueuePanel)
        return;
    const bool enabled = mergeQueueEnabledForOpenRepo();
    m_mergeQueuePanel->setVisible(enabled);
    if (!enabled) {
        if (m_mergeQueueList)
            m_mergeQueueList->clear();
        return;
    }
    const RepositoryRecord &repo = m_repositories.at(m_repoDetailIndex);
    const MergeQueue queue = MergeQueue::fromRows(repo.mergeQueue);

    if (m_mergeQueueList) {
        // Keep the selection on the same pull request across a rebuild — the
        // panel repaints on every pull reload, and the reorder buttons act on
        // whatever is selected. Block the list's signals meanwhile: clear() and
        // setCurrentItem() both fire currentRowChanged, whose handler calls
        // back into this function.
        QSignalBlocker block(m_mergeQueueList);
        const int selected = m_mergeQueueList->currentItem()
                                 ? m_mergeQueueList->currentItem()
                                       ->data(Qt::UserRole)
                                       .toInt()
                                 : -1;
        m_mergeQueueList->clear();
        int position = 0;
        for (const MergeQueueEntry &entry : queue.entries()) {
            ++position;
            QString title;
            for (const PullRequest &pr : std::as_const(m_currentPulls)) {
                if (pr.number == entry.number) {
                    title = pr.title;
                    break;
                }
            }
            auto *item = new QListWidgetItem(
                QStringLiteral("%1. #%2  %3")
                    .arg(position)
                    .arg(entry.number)
                    .arg(title.isEmpty() ? QStringLiteral("Pull request") : title));
            item->setData(Qt::UserRole, entry.number);
            item->setToolTip(QStringLiteral("#%1 — %2")
                                 .arg(entry.number)
                                 .arg(mergeQueueStateCaption(entry)));
            item->setForeground(QColor(mergeQueueStateColor(entry.state)));
            m_mergeQueueList->addItem(item);
            if (entry.number == selected)
                m_mergeQueueList->setCurrentItem(item);
        }
    }

    if (m_mergeQueueSummary) {
        QString summary = queue.summary();
        if (repo.mergeQueuePaused)
            summary += QString::fromUtf8(" \xC2\xB7 paused");
        else if (!queue.isEmpty())
            summary += QString::fromUtf8(" \xC2\xB7 %1")
                           .arg(mergeQueueStateCaption(queue.front()));
        m_mergeQueueSummary->setText(summary);
    }
    if (m_mergeQueuePauseButton) {
        const bool paused = repo.mergeQueuePaused;
        m_mergeQueuePauseButton->setText(paused ? QStringLiteral("Resume")
                                                : QStringLiteral("Pause"));
        setOcticon(m_mergeQueuePauseButton,
                   paused ? QStringLiteral("play") : QStringLiteral("hand"), 14);
        m_mergeQueuePauseButton->setToolTip(
            paused ? QStringLiteral("Start working through the queue again")
                   : QStringLiteral("Stop working through the queue without "
                                    "losing its order"));
    }
    const bool haveSelection =
        m_mergeQueueList && m_mergeQueueList->currentItem() != nullptr;
    const int row = haveSelection ? m_mergeQueueList->currentRow() : -1;
    if (m_mergeQueueUpButton)
        m_mergeQueueUpButton->setEnabled(row > 0);
    if (m_mergeQueueDownButton)
        m_mergeQueueDownButton->setEnabled(row >= 0 && row < queue.size() - 1);
    if (m_mergeQueueRemoveButton)
        m_mergeQueueRemoveButton->setEnabled(haveSelection);
    if (m_mergeQueueClearButton)
        m_mergeQueueClearButton->setEnabled(!queue.isEmpty());
}

// ---- Repository state ------------------------------------------------------

bool MainWindow::mergeQueueEnabledForOpenRepo() const
{
    return m_repoDetailIndex >= 0 && m_repoDetailIndex < m_repositories.size() &&
           m_repositories.at(m_repoDetailIndex).mergeQueueEnabled;
}

MergeQueue MainWindow::openRepoMergeQueue() const
{
    if (m_repoDetailIndex < 0 || m_repoDetailIndex >= m_repositories.size())
        return MergeQueue();
    return MergeQueue::fromRows(m_repositories.at(m_repoDetailIndex).mergeQueue);
}

MergeQueue MainWindow::mergeQueueFor(const QString &owner,
                                     const QString &name) const
{
    const int index = repoIndexFor(owner, name);
    if (index < 0 || index >= m_repositories.size())
        return MergeQueue();
    return MergeQueue::fromRows(m_repositories.at(index).mergeQueue);
}

void MainWindow::saveMergeQueue(const QString &owner, const QString &name,
                                const MergeQueue &queue)
{
    const int index = repoIndexFor(owner, name);
    if (index < 0 || index >= m_repositories.size())
        return;
    const QStringList rows = queue.toRows();
    if (rows != m_repositories.at(index).mergeQueue) {
        m_repositories[index].mergeQueue = rows;
        saveRepositories();
    }
    refreshMergeQueuePanel();
    updatePullActionState();
}

void MainWindow::setMergeQueueEntryState(const QString &owner, const QString &name,
                                         int number, const QString &state,
                                         const QString &detail)
{
    MergeQueue queue = mergeQueueFor(owner, name);
    if (!queue.setState(number, state, detail,
                        QDateTime::currentMSecsSinceEpoch()))
        return; // the entry is gone: it merged, or the user removed it
    saveMergeQueue(owner, name, queue);
}

void MainWindow::dropMergeQueueEntry(const QString &owner, const QString &name,
                                     int number)
{
    MergeQueue queue = mergeQueueFor(owner, name);
    if (!queue.remove(number))
        return;
    saveMergeQueue(owner, name, queue);
}

void MainWindow::setRepoMergeQueueEnabled(bool on)
{
    if (m_repoDetailIndex < 0 || m_repoDetailIndex >= m_repositories.size())
        return;
    if (m_repositories[m_repoDetailIndex].mergeQueueEnabled == on)
        return;
    m_repositories[m_repoDetailIndex].mergeQueueEnabled = on;
    saveRepositories();
    const RepositoryRecord &repo = m_repositories.at(m_repoDetailIndex);
    logSystem(QStringLiteral("Merge queue %1 for %2/%3.")
                  .arg(on ? QStringLiteral("enabled") : QStringLiteral("disabled"),
                       repo.owner, repo.name));
    if (m_settingsMergeQueueCheck) {
        QSignalBlocker block(m_settingsMergeQueueCheck);
        m_settingsMergeQueueCheck->setChecked(on);
    }
    refreshMergeQueuePanel();
    updatePullActionState();
    if (on)
        scheduleMergeQueueRun(kMergeQueueNextMs);
}

void MainWindow::setMergeQueuePaused(bool paused)
{
    if (m_repoDetailIndex < 0 || m_repoDetailIndex >= m_repositories.size())
        return;
    if (m_repositories[m_repoDetailIndex].mergeQueuePaused == paused)
        return;
    m_repositories[m_repoDetailIndex].mergeQueuePaused = paused;
    saveRepositories();
    const RepositoryRecord &repo = m_repositories.at(m_repoDetailIndex);
    logSystem(QStringLiteral("Merge queue %1 for %2/%3.")
                  .arg(paused ? QStringLiteral("paused") : QStringLiteral("resumed"),
                       repo.owner, repo.name));
    refreshMergeQueuePanel();
    if (!paused)
        scheduleMergeQueueRun(kMergeQueueNextMs);
}

// ---- Membership ------------------------------------------------------------

void MainWindow::addPullToMergeQueue(int number)
{
    if (number <= 0 || m_repoDetailIndex < 0 ||
        m_repoDetailIndex >= m_repositories.size())
        return;
    const RepositoryRecord repo = m_repositories.at(m_repoDetailIndex);
    if (!repo.mergeQueueEnabled) {
        setRepoDetailNotice(
            QStringLiteral("The merge queue is off for this repository — turn it "
                           "on in Settings \xE2\x80\xBA Automation."),
            true);
        return;
    }
    if (!pullStoreForCurrentRepo().canWrite()) {
        setRepoDetailNotice(
            QStringLiteral("Read-only mirror \xE2\x80\x94 pull requests are merged "
                           "on the node that owns this repository."),
            true);
        return;
    }
    for (const PullRequest &pr : std::as_const(m_currentPulls)) {
        if (pr.number == number && pr.status != QLatin1String("open")) {
            setRepoDetailNotice(
                QStringLiteral("Pull request #%1 is %2 \xE2\x80\x94 only open pull "
                               "requests can be queued.")
                    .arg(number)
                    .arg(pr.status),
                true);
            return;
        }
    }
    MergeQueue queue = mergeQueueFor(repo.owner, repo.name);
    if (!queue.enqueue(number, QDateTime::currentMSecsSinceEpoch())) {
        setRepoDetailNotice(
            QStringLiteral("Pull request #%1 is already in the merge queue (place "
                           "%2 of %3).")
                .arg(number)
                .arg(queue.indexOf(number) + 1)
                .arg(queue.size()));
        return;
    }
    saveMergeQueue(repo.owner, repo.name, queue);
    logSystem(QStringLiteral("Merge queue: pull request #%1 queued at place %2.")
                  .arg(number)
                  .arg(queue.size()));
    setRepoDetailNotice(
        QStringLiteral("Pull request #%1 joined the merge queue at place %2.")
            .arg(number)
            .arg(queue.size()));
    scheduleMergeQueueRun(kMergeQueueNextMs);
}

void MainWindow::removePullFromMergeQueue(int number)
{
    if (m_repoDetailIndex < 0 || m_repoDetailIndex >= m_repositories.size())
        return;
    const RepositoryRecord repo = m_repositories.at(m_repoDetailIndex);
    MergeQueue queue = mergeQueueFor(repo.owner, repo.name);
    if (!queue.remove(number))
        return;
    saveMergeQueue(repo.owner, repo.name, queue);
    logSystem(
        QStringLiteral("Merge queue: pull request #%1 removed.").arg(number));
    setRepoDetailNotice(
        QStringLiteral("Pull request #%1 left the merge queue.").arg(number));
}

void MainWindow::toggleCurrentPullInMergeQueue()
{
    if (m_currentPullNumber <= 0)
        return;
    if (openRepoMergeQueue().contains(m_currentPullNumber))
        removePullFromMergeQueue(m_currentPullNumber);
    else
        addPullToMergeQueue(m_currentPullNumber);
}

void MainWindow::moveMergeQueueSelection(int delta)
{
    if (!m_mergeQueueList || !m_mergeQueueList->currentItem() ||
        m_repoDetailIndex < 0 || m_repoDetailIndex >= m_repositories.size())
        return;
    const int number = m_mergeQueueList->currentItem()->data(Qt::UserRole).toInt();
    const RepositoryRecord repo = m_repositories.at(m_repoDetailIndex);
    MergeQueue queue = mergeQueueFor(repo.owner, repo.name);
    if (!queue.move(number, delta))
        return;
    saveMergeQueue(repo.owner, repo.name, queue);
    logSystem(QStringLiteral("Merge queue: pull request #%1 moved to place %2.")
                  .arg(number)
                  .arg(queue.indexOf(number) + 1));
    // saveMergeQueue rebuilt the rows; put the cursor back on the entry that
    // just moved so the buttons keep acting on it.
    for (int row = 0; row < m_mergeQueueList->count(); ++row) {
        if (m_mergeQueueList->item(row)->data(Qt::UserRole).toInt() == number) {
            m_mergeQueueList->setCurrentRow(row);
            break;
        }
    }
}

void MainWindow::removeMergeQueueSelection()
{
    if (!m_mergeQueueList || !m_mergeQueueList->currentItem())
        return;
    removePullFromMergeQueue(
        m_mergeQueueList->currentItem()->data(Qt::UserRole).toInt());
}

void MainWindow::clearMergeQueue()
{
    if (m_repoDetailIndex < 0 || m_repoDetailIndex >= m_repositories.size())
        return;
    const RepositoryRecord repo = m_repositories.at(m_repoDetailIndex);
    MergeQueue queue = mergeQueueFor(repo.owner, repo.name);
    if (queue.isEmpty())
        return;
    const int removed = queue.size();
    queue.clear();
    saveMergeQueue(repo.owner, repo.name, queue);
    logSystem(QStringLiteral("Merge queue: cleared %1 pull request%2.")
                  .arg(removed)
                  .arg(removed == 1 ? QString() : QStringLiteral("s")));
    setRepoDetailNotice(
        QStringLiteral("Merge queue cleared; %1 pull request%2 stayed open.")
            .arg(removed)
            .arg(removed == 1 ? QString() : QStringLiteral("s")));
}

void MainWindow::queueBranchForMerge(const QString &branch)
{
    if (branch.trimmed().isEmpty() || m_repoDetailIndex < 0 ||
        m_repoDetailIndex >= m_repositories.size())
        return;
    if (!mergeQueueEnabledForOpenRepo()) {
        setRepoDetailNotice(
            QStringLiteral("The merge queue is off for this repository — turn it "
                           "on in Settings \xE2\x80\xBA Automation."),
            true);
        return;
    }
    // A branch reaches the queue through its pull request: that is what carries
    // the review, the checks and the merge itself. Queue the open one when it
    // exists, and open one for the branch when it does not.
    for (const PullRequest &pr : std::as_const(m_currentPulls)) {
        if (pr.status == QLatin1String("open") && pr.head == branch) {
            addPullToMergeQueue(pr.number);
            return;
        }
    }
    const int number = createPullFromBranch(branch);
    if (number > 0)
        addPullToMergeQueue(number);
}

// ---- Runner ----------------------------------------------------------------

void MainWindow::scheduleMergeQueueRun(int delayMs)
{
    if (!m_mergeQueueTimer) {
        m_mergeQueueTimer = new QTimer(this);
        m_mergeQueueTimer->setSingleShot(true);
        connect(m_mergeQueueTimer, &QTimer::timeout, this,
                &MainWindow::processMergeQueue);
    }
    const int delay = qMax(0, delayMs);
    // Coalesce: a later request must never push an already-pending, earlier
    // wake-up back (the retry heartbeat would otherwise keep deferring the
    // prompt "the base moved, come back now" run).
    if (m_mergeQueueTimer->isActive() &&
        m_mergeQueueTimer->remainingTime() >= 0 &&
        m_mergeQueueTimer->remainingTime() <= delay)
        return;
    m_mergeQueueTimer->start(delay);
}

void MainWindow::processMergeQueue()
{
    // Every git step below pumps the GUI event loop, so this can be re-entered
    // from a timer tick or a pull reload mid-merge. Come back later instead.
    if (m_mergeQueueBusy) {
        scheduleMergeQueueRun(kMergeQueueNextMs);
        return;
    }
    if (m_repoDetailIndex < 0 || m_repoDetailIndex >= m_repositories.size())
        return;
    // By value: the merge steps pump the event loop, and a reload can reassign
    // m_repositories out from under a reference into it (adhoc #119).
    const RepositoryRecord repo = m_repositories.at(m_repoDetailIndex);
    if (!repo.mergeQueueEnabled || repo.mergeQueuePaused)
        return;
    MergeQueue queue = MergeQueue::fromRows(repo.mergeQueue);
    if (queue.isEmpty())
        return;
    PullStore store = pullStoreForCurrentRepo();
    if (!store.canWrite())
        return; // read-only mirror: the owner's node drains this queue
    // Nothing to decide on until the pull requests are loaded; the reload itself
    // wakes the runner back up (applyLoadedPulls).
    if (m_currentPulls.isEmpty()) {
        scheduleMergeQueueRun(kMergeQueueRetryMs);
        return;
    }
    // Never take the working tree away from something already holding it: an AI
    // conflict fix, a delete worker, or an interrupted `git am` session.
    if (m_aiFix || m_pullDeleteInProgress || store.conflictMergeInProgress()) {
        scheduleMergeQueueRun(kMergeQueueRetryMs);
        return;
    }

    m_mergeQueueBusy = true;
    bool pending = false;   // something is still waiting on a human or a retry
    bool mergedOne = false;
    PullRequest mergedPull;
    for (const MergeQueueEntry &entry : queue.entries()) {
        const int number = entry.number;
        // Every step below pumps the GUI event loop, so the user can edit the
        // queue (or pause it, or switch repositories) while this pass walks a
        // copy of it. Re-read the stored state before acting on each entry: a
        // removed entry must not merge, and a pause stops the walk.
        {
            const int liveIndex = repoIndexFor(repo.owner, repo.name);
            if (liveIndex < 0 ||
                !m_repositories.at(liveIndex).mergeQueueEnabled ||
                m_repositories.at(liveIndex).mergeQueuePaused)
                break;
            // m_currentPulls below belongs to whatever repository the detail
            // view shows; if the user moved away, stop rather than judging this
            // queue against another repo's pull requests. The pass resumes when
            // they come back (applyLoadedPulls reschedules it).
            if (m_repoDetailIndex != liveIndex)
                break;
            if (!MergeQueue::fromRows(m_repositories.at(liveIndex).mergeQueue)
                     .contains(number))
                continue;
        }
        PullRequest pr;
        bool found = false;
        for (const PullRequest &candidate : std::as_const(m_currentPulls)) {
            if (candidate.number == number) {
                pr = candidate;
                found = true;
                break;
            }
        }
        if (!found) {
            dropMergeQueueEntry(repo.owner, repo.name, number);
            logSystem(QStringLiteral("Merge queue: pull request #%1 no longer "
                                     "exists; dropped from the queue.")
                          .arg(number));
            continue;
        }
        if (pr.status != QLatin1String("open")) {
            dropMergeQueueEntry(repo.owner, repo.name, number);
            logSystem(QStringLiteral("Merge queue: pull request #%1 left the queue "
                                     "(already %2).")
                          .arg(number)
                          .arg(pr.status));
            continue;
        }
        // The queue keeps the repository's own merge rules; it does not vote.
        if (repo.requirePeerApproval && !pr.independentReviewGateSatisfied()) {
            setMergeQueueEntryState(
                repo.owner, repo.name, number, MergeQueueState::Blocked,
                pr.hasIndependentChangesRequested()
                    ? QStringLiteral("Changes requested — resolve the review")
                    : QStringLiteral("Waiting for an independent peer approval"));
            pending = true;
            continue;
        }
        // Auto-update: the merge in front of this one moved the base, so bring
        // the head branch up to date before testing or merging it.
        bool behind = false;
        int behindCount = 0;
        QString error;
        if (store.isBranchBehindBase(number, &behind, &error, &behindCount) &&
            behind) {
            setMergeQueueEntryState(
                repo.owner, repo.name, number, MergeQueueState::Updating,
                QStringLiteral("Updating from %1 (%2 commit%3 behind)")
                    .arg(pr.base.isEmpty() ? QStringLiteral("the base branch")
                                           : pr.base)
                    .arg(behindCount)
                    .arg(behindCount == 1 ? QString() : QStringLiteral("s")));
            QString updateError;
            if (!store.updateBranchFromBase(number, &updateError)) {
                setMergeQueueEntryState(
                    repo.owner, repo.name, number, MergeQueueState::Failed,
                    QStringLiteral("Could not update from %1: %2")
                        .arg(pr.base.isEmpty() ? QStringLiteral("the base branch")
                                               : pr.base,
                             updateError.trimmed()));
                logSystem(QStringLiteral("Merge queue: pull request #%1 could not "
                                         "be updated from its base: %2")
                              .arg(number)
                              .arg(updateError.trimmed().left(200)));
                pending = true;
                continue;
            }
            logSystem(QStringLiteral("Merge queue: updated pull request #%1 from "
                                     "its base branch.")
                          .arg(number));
        }
        bool clean = false;
        QStringList conflicts;
        QString checkError;
        if (!store.checkMergeable(number, &clean, &conflicts, &checkError,
                                  /*keepGuiAlive=*/true)) {
            setMergeQueueEntryState(
                repo.owner, repo.name, number, MergeQueueState::Failed,
                checkError.trimmed().isEmpty()
                    ? QStringLiteral("Could not test whether it merges")
                    : checkError.trimmed());
            pending = true;
            continue;
        }
        if (!clean) {
            setMergeQueueEntryState(
                repo.owner, repo.name, number, MergeQueueState::Blocked,
                conflicts.isEmpty()
                    ? QStringLiteral("Conflicts with the base branch")
                    : QStringLiteral("Conflicts in %1").arg(
                          conflicts.join(QStringLiteral(", "))));
            pending = true;
            continue;
        }
        setMergeQueueEntryState(repo.owner, repo.name, number,
                                MergeQueueState::Merging,
                                QStringLiteral("Merging"));
        QString mergeError;
        if (!store.mergePull(number, &mergeError, repo.requirePeerApproval)) {
            setMergeQueueEntryState(
                repo.owner, repo.name, number, MergeQueueState::Failed,
                QStringLiteral("Merge failed: %1").arg(mergeError.trimmed()));
            logSystem(QStringLiteral("Merge queue: merging pull request #%1 "
                                     "failed: %2")
                          .arg(number)
                          .arg(mergeError.trimmed().left(200)));
            pending = true;
            continue;
        }
        dropMergeQueueEntry(repo.owner, repo.name, number);
        mergedOne = true;
        mergedPull = pr;
        // One merge per pass: the base has moved, so everything behind this
        // entry has to be re-evaluated (and probably updated) from scratch.
        break;
    }

    if (mergedOne) {
        setRepoDetailNotice(
            QStringLiteral("Merge queue merged pull request #%1.")
                .arg(mergedPull.number));
        // Closes linked issues, refreshes the views the new commit invalidates
        // and publishes the merge when auto-sync-on-merge is on — the same tail
        // the Merge button runs. It reloads the pull requests, which repaints
        // the queue panel and schedules the next pass.
        afterPullMerged(mergedPull);
    } else {
        refreshMergeQueuePanel();
    }
    m_mergeQueueBusy = false;
    if (mergedOne)
        scheduleMergeQueueRun(kMergeQueueNextMs);
    else if (pending)
        scheduleMergeQueueRun(kMergeQueueRetryMs);
}
