// Organization-private task catalog and management surface.

#include "MainWindow.h"
#include "MainWindowInternal.h"

#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QJsonDocument>
#include <QMessageBox>
#include <QNetworkReply>
#include <QPlainTextEdit>
#include <QRegularExpression>
#include <QSpinBox>
#include <QSplitter>
#include <QTextBrowser>

using namespace forkmesh::ui;

namespace {

// Rows painted per page. The relay returns the whole organization catalog
// (MAX_TASKS in world_office_tasks.py), so the table pages through it instead
// of stopping at the first hundred rows.
constexpr int kOrganizationTaskPageSize = 100;

QString taskText(const QJsonObject &task, const QString &field)
{
    return task.value(field).toString().trimmed();
}

QString taskErrorText(const QJsonObject &payload, const QString &fallback)
{
    QString error = payload.value(QStringLiteral("error")).toString().trimmed();
    if (error.isEmpty())
        return fallback;
    error.replace(QLatin1Char('_'), QLatin1Char(' '));
    return error;
}

// The canonical prefix this desktop signs with its account key for one task
// request when it holds no account session token. Empty when the relay accepts
// no key-signed form of the request: editing, deleting, timers, and QA verdicts
// deliberately still require a real session. `resource` receives the task id
// for a proof that names one. Must stay in lockstep with
// _org_task_signed_session in the worker's entry.py.
QString organizationTaskProof(const QByteArray &method, const QString &path,
                              QString *resource)
{
    static const QRegularExpression completeRe(
        QStringLiteral("^/api/tasks/([a-f0-9]{32})/complete/?$"));
    const bool collection = path == QLatin1String("/api/tasks") ||
                            path == QLatin1String("/api/tasks/");
    if (method == QByteArrayLiteral("GET"))
        return collection ? kOrgTaskListProof : QString();
    if (method != QByteArrayLiteral("POST"))
        return QString();
    if (collection)
        return kOrgTaskOpenProof;
    const QRegularExpressionMatch complete = completeRe.match(path);
    if (!complete.hasMatch())
        return QString();
    *resource = complete.captured(1);
    return kOrgTaskCompleteProof;
}

// Tasks that are not finished — the number the rail badge shows. Counted
// exactly the way applyOrganizationTasks counts it while filling the table, so
// a badge refreshed without the page built agrees with one refreshed with it.
int openOrganizationTaskCount(const QJsonArray &tasks)
{
    int open = 0;
    for (const QJsonValue &value : tasks) {
        const QJsonObject task = value.toObject();
        const bool done =
            task.value(QStringLiteral("completedAt")).toDouble() > 0 ||
            taskText(task, QStringLiteral("status")) == QLatin1String("done");
        if (!done)
            ++open;
    }
    return open;
}

QString taskTimestamp(qint64 milliseconds)
{
    if (milliseconds <= 0)
        return QString::fromUtf8("\xE2\x80\x94");
    return QDateTime::fromMSecsSinceEpoch(milliseconds)
        .toLocalTime()
        .toString(QStringLiteral("yyyy-MM-dd HH:mm"));
}

QString taskAssigneeLabel(const QJsonObject &task)
{
    const QString kind = taskText(task, QStringLiteral("assigneeKind"));
    if (kind == QLatin1String("agent"))
        return QStringLiteral("Bot");
    if (kind == QLatin1String("unassigned"))
        return QStringLiteral("Unassigned");
    const QString assignee = taskText(task, QStringLiteral("assignee"));
    return assignee.isEmpty() ? QStringLiteral("Unassigned") : assignee;
}

QJsonObject selectedOrganizationTask(
    QTableWidget *table, const QJsonArray &tasks)
{
    if (!table || table->currentRow() < 0)
        return {};
    QTableWidgetItem *item = table->item(table->currentRow(), 0);
    if (!item)
        return {};
    const int index = item->data(Qt::UserRole).toInt();
    return index >= 0 && index < tasks.size() ? tasks.at(index).toObject()
                                              : QJsonObject();
}

QString taskDetailHtml(const QJsonObject &task)
{
    if (task.isEmpty()) {
        return QStringLiteral(
            "<p style='color:#8b949e'>Select a task to inspect its private "
            "details and available actions.</p>");
    }
    const QJsonObject qa = task.value(QStringLiteral("qa")).toObject();
    auto row = [](const QString &label, const QString &value) {
        const QString shown =
            value.trimmed().isEmpty() ? QString::fromUtf8("\xE2\x80\x94")
                                      : value.toHtmlEscaped();
        return QStringLiteral(
                   "<tr><th align='left' style='padding:3px 12px 3px 0;"
                   "color:#8b949e'>%1</th><td style='padding:3px 0'>%2</td></tr>")
            .arg(label.toHtmlEscaped(), shown);
    };
    QString html =
        QStringLiteral("<h2 style='margin:0 0 8px 0'>%1</h2>")
            .arg(taskText(task, QStringLiteral("title")).toHtmlEscaped());
    html += QStringLiteral("<table cellspacing='0'>");
    html += row(QStringLiteral("Status"),
                taskText(task, QStringLiteral("status")));
    html += row(QStringLiteral("Priority"),
                QString::number(task.value(QStringLiteral("priority")).toInt()));
    html += row(QStringLiteral("Department"),
                taskText(task, QStringLiteral("department")));
    html += row(QStringLiteral("Team"),
                taskText(task, QStringLiteral("team")));
    html += row(QStringLiteral("Destination"),
                taskText(task, QStringLiteral("destination")));
    html += row(QStringLiteral("Repository"),
                taskText(task, QStringLiteral("repository")));
    html += row(QStringLiteral("Assignee"), taskAssigneeLabel(task));
    html += row(QStringLiteral("Created by"),
                taskText(task, QStringLiteral("createdBy")));
    html += row(QStringLiteral("Follows task"),
                taskText(task, QStringLiteral("parentTaskId")));
    html += row(QStringLiteral("Agent session"),
                taskText(task, QStringLiteral("agentSessionId")));
    html += row(QStringLiteral("QA status"),
                taskText(qa, QStringLiteral("status")));
    html += row(QStringLiteral("QA reviewer"),
                taskText(qa, QStringLiteral("reviewer")));
    html += row(QStringLiteral("QA requested"),
                taskTimestamp(
                    qint64(qa.value(QStringLiteral("requestedAt")).toDouble())));
    html += row(QStringLiteral("Updated"),
                taskTimestamp(qint64(
                    task.value(QStringLiteral("updatedAt")).toDouble())));
    html += row(QStringLiteral("Task ID"),
                taskText(task, QStringLiteral("id")));
    html += QStringLiteral("</table>");
    auto block = [&html](const QString &heading, const QString &value) {
        html += QStringLiteral(
                    "<h3 style='margin:14px 0 4px 0'>%1</h3>"
                    "<p style='white-space:pre-wrap;margin:0'>%2</p>")
                    .arg(heading.toHtmlEscaped(),
                         (value.isEmpty()
                              ? QString::fromUtf8("\xE2\x80\x94")
                              : value.toHtmlEscaped()));
    };
    block(QStringLiteral("Details"),
          taskText(task, QStringLiteral("details")));
    block(QStringLiteral("How to test"),
          taskText(qa, QStringLiteral("howToTest")));
    block(QStringLiteral("Completion note"),
          taskText(task, QStringLiteral("completionNote")));
    return html;
}

} // namespace

QWidget *MainWindow::buildOrganizationTasksSection()
{
    auto *page = new QWidget;
    page->setObjectName(QStringLiteral("organizationTasksSection"));
    auto *outer = new QVBoxLayout(page);
    outer->setContentsMargins(20, 18, 20, 20);
    outer->setSpacing(10);

    auto *headingRow = new QHBoxLayout;
    auto *heading = new QLabel(QStringLiteral("Organization tasks"));
    heading->setObjectName(QStringLiteral("pageTitle"));
    headingRow->addWidget(heading);
    m_organizationTasksSummary = new QLabel;
    m_organizationTasksSummary->setObjectName(QStringLiteral("mutedLabel"));
    headingRow->addWidget(m_organizationTasksSummary);
    headingRow->addStretch();

    m_organizationTaskNewButton = new QPushButton(QStringLiteral("New task"));
    m_organizationTaskNewButton->setObjectName(QStringLiteral("primaryButton"));
    setOcticon(m_organizationTaskNewButton, "plus", 14);
    connect(m_organizationTaskNewButton, &QPushButton::clicked, this,
            &MainWindow::createOrganizationTask);
    headingRow->addWidget(m_organizationTaskNewButton);

    auto *refresh = new QPushButton(QStringLiteral("Refresh"));
    refresh->setObjectName(QStringLiteral("ghostButton"));
    setOcticon(refresh, "sync", 14);
    connect(refresh, &QPushButton::clicked, this,
            &MainWindow::refreshOrganizationTasks);
    headingRow->addWidget(refresh);
    outer->addLayout(headingRow);

    m_organizationTasksStatus = new QLabel(
        QStringLiteral("Open Tasks to load the private organization catalog."));
    m_organizationTasksStatus->setObjectName(
        QStringLiteral("organizationTasksStatus"));
    m_organizationTasksStatus->setWordWrap(true);
    outer->addWidget(m_organizationTasksStatus);

    m_organizationTasksSearch = new QLineEdit;
    m_organizationTasksSearch->setObjectName(
        QStringLiteral("organizationTasksSearch"));
    m_organizationTasksSearch->setPlaceholderText(
        QStringLiteral("Search title, details, assignee, repository, status…"));
    m_organizationTasksSearch->setClearButtonEnabled(true);
    outer->addWidget(m_organizationTasksSearch);

    auto *splitter = new QSplitter(Qt::Horizontal);
    splitter->setChildrenCollapsible(false);
    m_organizationTasksTable = new QTableWidget(0, 7);
    m_organizationTasksTable->setObjectName(
        QStringLiteral("organizationTasksTable"));
    m_organizationTasksTable->setHorizontalHeaderLabels(
        {QStringLiteral("Priority"), QStringLiteral("Title"),
         QStringLiteral("Status"), QStringLiteral("Department"),
         QStringLiteral("Repository"), QStringLiteral("Assignee"),
         QStringLiteral("QA")});
    m_organizationTasksTable->verticalHeader()->hide();
    m_organizationTasksTable->setSelectionBehavior(
        QAbstractItemView::SelectRows);
    m_organizationTasksTable->setSelectionMode(
        QAbstractItemView::SingleSelection);
    m_organizationTasksTable->setEditTriggers(
        QAbstractItemView::NoEditTriggers);
    m_organizationTasksTable->setAlternatingRowColors(true);
    m_organizationTasksTable->setSortingEnabled(false);
    auto *header = m_organizationTasksTable->horizontalHeader();
    header->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    header->setSectionResizeMode(1, QHeaderView::Stretch);
    for (int column = 2; column < 7; ++column)
        header->setSectionResizeMode(column, QHeaderView::ResizeToContents);
    connect(m_organizationTasksTable, &QTableWidget::itemSelectionChanged,
            this, [this] {
                renderOrganizationTaskDetail();
                updateOrganizationTaskActions();
            });
    // The search runs over the whole catalog, not just the painted page, so a
    // match on page 12 still surfaces — it re-pages the filtered set from the
    // first page.
    connect(m_organizationTasksSearch, &QLineEdit::textChanged, this,
            [this](const QString &) {
                m_organizationTasksPage = 0;
                renderOrganizationTaskRows();
            });

    auto *tableHost = new QWidget;
    auto *tableLayout = new QVBoxLayout(tableHost);
    tableLayout->setContentsMargins(0, 0, 0, 0);
    tableLayout->setSpacing(6);
    tableLayout->addWidget(m_organizationTasksTable, 1);

    auto *pager = new QHBoxLayout;
    pager->setContentsMargins(0, 0, 0, 0);
    pager->setSpacing(8);
    m_organizationTaskPrevPageButton =
        new QPushButton(QString::fromUtf8("\xE2\x80\xB9 Previous"));
    m_organizationTaskPrevPageButton->setObjectName(
        QStringLiteral("ghostButton"));
    m_organizationTaskNextPageButton =
        new QPushButton(QString::fromUtf8("Next \xE2\x80\xBA"));
    m_organizationTaskNextPageButton->setObjectName(
        QStringLiteral("ghostButton"));
    m_organizationTaskPageLabel = new QLabel;
    m_organizationTaskPageLabel->setObjectName(QStringLiteral("mutedLabel"));
    connect(m_organizationTaskPrevPageButton, &QPushButton::clicked, this,
            [this] {
                m_organizationTasksPage =
                    qMax(0, m_organizationTasksPage - 1);
                renderOrganizationTaskRows();
            });
    connect(m_organizationTaskNextPageButton, &QPushButton::clicked, this,
            [this] {
                ++m_organizationTasksPage;
                renderOrganizationTaskRows();
            });
    pager->addWidget(m_organizationTaskPrevPageButton);
    pager->addWidget(m_organizationTaskPageLabel);
    pager->addStretch();
    pager->addWidget(m_organizationTaskNextPageButton);
    tableLayout->addLayout(pager);
    splitter->addWidget(tableHost);

    auto *detailHost = new QWidget;
    auto *detailLayout = new QVBoxLayout(detailHost);
    detailLayout->setContentsMargins(12, 0, 0, 0);
    detailLayout->setSpacing(8);
    m_organizationTaskDetail = new QTextBrowser;
    m_organizationTaskDetail->setObjectName(
        QStringLiteral("organizationTaskDetail"));
    m_organizationTaskDetail->setOpenExternalLinks(false);
    detailLayout->addWidget(m_organizationTaskDetail, 1);

    auto *actions = new QGridLayout;
    actions->setContentsMargins(0, 0, 0, 0);
    actions->setSpacing(6);
    auto addAction = [this, actions](QPushButton **slot, const QString &text,
                                     const QString &icon, int row, int column) {
        *slot = new QPushButton(text);
        (*slot)->setObjectName(QStringLiteral("ghostButton"));
        setOcticon(*slot, icon, 14);
        actions->addWidget(*slot, row, column);
    };
    addAction(&m_organizationTaskEditButton, QStringLiteral("Edit"),
              QStringLiteral("pencil"), 0, 0);
    addAction(&m_organizationTaskAgentButton, QStringLiteral("Assign Bot"),
              QStringLiteral("hubot"), 0, 1);
    addAction(&m_organizationTaskStartButton, QStringLiteral("Start"),
              QStringLiteral("play"), 0, 2);
    addAction(&m_organizationTaskCompleteButton, QStringLiteral("Complete"),
              QStringLiteral("check"), 1, 0);
    addAction(&m_organizationTaskQaButton, QStringLiteral("Send to QA"),
              QStringLiteral("beaker"), 1, 1);
    addAction(&m_organizationTaskReturnButton,
              QStringLiteral("Return to list"),
              QStringLiteral("reply"), 1, 2);
    addAction(&m_organizationTaskDeleteButton, QStringLiteral("Delete"),
              QStringLiteral("trash"), 2, 0);
    addAction(&m_organizationTaskFollowUpButton, QStringLiteral("Follow up"),
              QStringLiteral("git-branch"), 2, 1);
    m_organizationTaskDeleteButton->setObjectName(
        QStringLiteral("dangerButton"));
    connect(m_organizationTaskEditButton, &QPushButton::clicked, this,
            &MainWindow::editOrganizationTask);
    connect(m_organizationTaskAgentButton, &QPushButton::clicked, this,
            &MainWindow::assignOrganizationTaskToAgent);
    connect(m_organizationTaskStartButton, &QPushButton::clicked, this,
            [this] {
                const QJsonObject task = selectedOrganizationTask(
                    m_organizationTasksTable, m_organizationTasks);
                runOrganizationTaskAction(
                    taskText(task, QStringLiteral("status")) ==
                            QLatin1String("active")
                        ? QStringLiteral("stop")
                        : QStringLiteral("start"));
            });
    connect(m_organizationTaskCompleteButton, &QPushButton::clicked, this,
            [this] { runOrganizationTaskAction(QStringLiteral("complete")); });
    connect(m_organizationTaskQaButton, &QPushButton::clicked, this,
            [this] { runOrganizationTaskAction(QStringLiteral("qa")); });
    connect(m_organizationTaskReturnButton, &QPushButton::clicked, this,
            [this] { runOrganizationTaskAction(QStringLiteral("return")); });
    connect(m_organizationTaskDeleteButton, &QPushButton::clicked, this,
            &MainWindow::deleteOrganizationTask);
    connect(m_organizationTaskFollowUpButton, &QPushButton::clicked, this,
            &MainWindow::createOrganizationTaskFollowUp);
    detailLayout->addLayout(actions);
    splitter->addWidget(detailHost);
    splitter->setStretchFactor(0, 3);
    splitter->setStretchFactor(1, 2);
    outer->addWidget(splitter, 1);

    auto *queueHeading = new QHBoxLayout;
    auto *queueTitle = new QLabel(QStringLiteral("Claude / Codex queue"));
    queueTitle->setObjectName(QStringLiteral("sectionTitle"));
    queueHeading->addWidget(queueTitle);
    auto *queueHelp = new QLabel(
        QStringLiteral("Local queued items not yet linked to Tasks"));
    queueHelp->setObjectName(QStringLiteral("mutedLabel"));
    queueHeading->addWidget(queueHelp);
    queueHeading->addStretch();
    m_organizationTaskQueueMoveButton =
        new QPushButton(QStringLiteral("Move into Tasks"));
    m_organizationTaskQueueMoveButton->setObjectName(
        QStringLiteral("primaryButton"));
    setOcticon(m_organizationTaskQueueMoveButton, "plus", 14);
    m_organizationTaskQueueMoveButton->setEnabled(false);
    connect(m_organizationTaskQueueMoveButton, &QPushButton::clicked, this,
            &MainWindow::moveQueuedAgentItemToTasks);
    queueHeading->addWidget(m_organizationTaskQueueMoveButton);
    outer->addLayout(queueHeading);

    m_organizationTaskQueueTable = new QTableWidget(0, 5);
    m_organizationTaskQueueTable->setObjectName(
        QStringLiteral("organizationTaskQueueTable"));
    m_organizationTaskQueueTable->setHorizontalHeaderLabels(
        {QStringLiteral("Provider"), QStringLiteral("Item"),
         QStringLiteral("Status"), QStringLiteral("Repository"),
         QStringLiteral("Queued")});
    m_organizationTaskQueueTable->verticalHeader()->hide();
    m_organizationTaskQueueTable->setSelectionBehavior(
        QAbstractItemView::SelectRows);
    m_organizationTaskQueueTable->setSelectionMode(
        QAbstractItemView::SingleSelection);
    m_organizationTaskQueueTable->setEditTriggers(
        QAbstractItemView::NoEditTriggers);
    m_organizationTaskQueueTable->setMaximumHeight(190);
    auto *queueHeader = m_organizationTaskQueueTable->horizontalHeader();
    queueHeader->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    queueHeader->setSectionResizeMode(1, QHeaderView::Stretch);
    for (int column = 2; column < 5; ++column)
        queueHeader->setSectionResizeMode(column,
                                          QHeaderView::ResizeToContents);
    connect(m_organizationTaskQueueTable,
            &QTableWidget::itemSelectionChanged, this, [this] {
                m_organizationTaskQueueMoveButton->setEnabled(
                    m_organizationTaskQueueTable->currentRow() >= 0 &&
                    m_organizationTasksCanManage);
            });
    outer->addWidget(m_organizationTaskQueueTable);

    renderOrganizationTaskDetail();
    updateOrganizationTaskActions();
    refreshOrganizationTaskQueue();
    return page;
}

void MainWindow::requestOrganizationTasks(
    const QByteArray &method, const QString &path, const QJsonObject &body,
    OrganizationTaskReplyHandler handler)
{
    if (!m_networkAccess) {
        handler(false, {}, QStringLiteral(
            "Sign in to an organization account to use private tasks."));
        return;
    }
    QUrl url = catalogApiUrl();
    url.setPath(path);
    url.setQuery(QString());
    QNetworkRequest request(url);
    request.setAttribute(QNetworkRequest::CacheLoadControlAttribute,
                         QNetworkRequest::AlwaysNetwork);
    request.setTransferTimeout(15000);
    request.setRawHeader(QByteArrayLiteral("Accept"),
                         QByteArrayLiteral("application/json"));
    bool keySigned = false;
    if (!m_accountSessionToken.trimmed().isEmpty()) {
        request.setRawHeader(
            QByteArrayLiteral("Authorization"),
            QByteArrayLiteral("Bearer ") + m_accountSessionToken.toUtf8());
    } else {
        // Only a desktop that signed in with a password holds a session token.
        // The ordinary launch is authenticateSilently(), which proves this
        // install owns the account's key and mints no token at all — so
        // without the signed fallback the whole tab reported "Sign in to an
        // organization account" to an operator who was already signed in
        // (adhoc #52). The relay accepts the signature for reading the board
        // and for the two writes a desktop makes for its own run; anything
        // else still needs a real session.
        QString resource;
        const QString proof = organizationTaskProof(method, path, &resource);
        if (proof.isEmpty() ||
            !authenticateOrgTaskRequest(url, request, proof, resource)) {
            // Distinguish "not signed in at all" from "signed in with this
            // device's key, but this change (delete, edit, QA, another
            // member's timer, ...) deliberately needs a real session." The
            // first message told an operator who was plainly using their own
            // account to "sign in" to it, which reads as a no-op bug when the
            // actual, actionable step is a password login (adhoc #108).
            handler(false, {},
                    m_accountAuthenticated
                        ? QString::fromUtf8(
                              "This change needs a password sign-in on this "
                              "device, not just its saved account key \xE2"
                              "\x80\x94 use Settings \xE2\x86\x92 \"Log in to "
                              "a user account\", then try again.")
                        : QStringLiteral(
                              "Sign in to an organization account to use "
                              "private tasks."));
            return;
        }
        keySigned = true;
    }
    const QByteArray payload =
        body.isEmpty() ? QByteArray() : QJsonDocument(body).toJson(
                                             QJsonDocument::Compact);
    if (!payload.isEmpty())
        request.setHeader(QNetworkRequest::ContentTypeHeader,
                          QStringLiteral("application/json"));

    logSystem(QStringLiteral("Organization tasks: %1 %2 requested.")
                  .arg(QString::fromLatin1(method), path));
    QNetworkReply *reply = nullptr;
    if (method == QByteArrayLiteral("GET"))
        reply = m_networkAccess->get(request);
    else if (method == QByteArrayLiteral("POST"))
        reply = m_networkAccess->post(request, payload);
    else
        reply = m_networkAccess->sendCustomRequest(request, method, payload);
    connect(reply, &QNetworkReply::finished, this,
            [this, reply, method, path, keySigned,
             handler = std::move(handler)]() mutable {
                const QByteArray raw = reply->readAll();
                const QJsonDocument document = QJsonDocument::fromJson(raw);
                const QJsonObject data = document.object();
                const int status = reply->attribute(
                    QNetworkRequest::HttpStatusCodeAttribute).toInt();
                const bool ok = reply->error() == QNetworkReply::NoError &&
                                status >= 200 && status < 300 &&
                                data.value(QStringLiteral("ok")).toBool(true);
                QString error = ok
                    ? QString()
                    : taskErrorText(
                          data,
                          status > 0
                              ? QStringLiteral("HTTP %1").arg(status)
                              : reply->errorString());
                // "invalid session" reads as a bug to an operator who is
                // plainly signed in: what the relay actually rejected is this
                // machine's key. Say so, and name the one action that fixes it.
                if (keySigned && status == 401)
                    error += QString::fromUtf8(
                        " \xE2\x80\x94 this machine's key is not authorized "
                        "for the account. Use Settings \xE2\x86\x92 \"Log in "
                        "to a user account\" to register it.");
                logSystem(
                    QStringLiteral("Organization tasks: %1 %2 %3.")
                        .arg(QString::fromLatin1(method), path,
                             ok ? QStringLiteral("completed")
                                : QStringLiteral("failed: ") + error));
                reply->deleteLater();
                handler(ok, data, error);
            });
}

void MainWindow::refreshOrganizationTasks()
{
    if (m_organizationTasksLoading || !m_organizationTasksStatus)
        return;
    m_organizationTasksLoading = true;
    m_organizationTasksStatus->setText(
        QString::fromUtf8("Loading organization tasks\xE2\x80\xA6"));
    requestOrganizationTasks(
        QByteArrayLiteral("GET"), QStringLiteral("/api/tasks"), {},
        [this](bool ok, const QJsonObject &payload, const QString &error) {
            m_organizationTasksLoading = false;
            if (!ok) {
                m_organizationTasksStatus->setText(
                    QStringLiteral("Tasks unavailable: %1").arg(error));
                return;
            }
            applyOrganizationTasks(payload);
            m_organizationTasksStatus->setText(
                QStringLiteral("Private catalog refreshed at %1.")
                    .arg(QTime::currentTime().toString(
                        QStringLiteral("HH:mm:ss"))));
        });
}

// Paint the open-task count onto the Tasks rail button and remember it, so the
// next launch has a number to show before the relay has answered anything.
void MainWindow::setOrganizationTaskBadge(int openCount)
{
    QSettings().setValue(kOrganizationTaskOpenCountSetting, openCount);
    if (auto *button = dynamic_cast<ActivityRailButton *>(m_tasksNavButton))
        button->setBadgeCount(openCount);
}

// Launch-time counterpart: show the last count we knew about immediately. The
// background refresh below replaces it as soon as the relay answers.
void MainWindow::restoreOrganizationTaskBadge()
{
    if (auto *button = dynamic_cast<ActivityRailButton *>(m_tasksNavButton)) {
        button->setBadgeCount(
            QSettings().value(kOrganizationTaskOpenCountSetting, 0).toInt());
    }
}

// Refresh the count without requiring a visit to the Tasks page. The page is
// built lazily, so on a fresh launch there is no status label and no table to
// fill — count the payload directly then. A failed read (offline, or an account
// with no private catalog) deliberately leaves the restored badge alone.
void MainWindow::refreshOrganizationTaskBadge()
{
    if (m_organizationTasksLoading)
        return;
    if (m_organizationTasksStatus && m_organizationTasksTable) {
        refreshOrganizationTasks(); // page exists: keep table and badge in step
        return;
    }
    m_organizationTasksLoading = true;
    requestOrganizationTasks(
        QByteArrayLiteral("GET"), QStringLiteral("/api/tasks"), {},
        [this](bool ok, const QJsonObject &payload, const QString &) {
            m_organizationTasksLoading = false;
            if (!ok)
                return;
            setOrganizationTaskBadge(openOrganizationTaskCount(
                payload.value(QStringLiteral("tasks")).toArray()));
        });
}

void MainWindow::applyOrganizationTasks(const QJsonObject &payload)
{
    if (!m_organizationTasksTable)
        return;
    const QJsonObject prior = selectedOrganizationTask(
        m_organizationTasksTable, m_organizationTasks);
    const QString selectedId = taskText(prior, QStringLiteral("id"));
    m_organizationTasks =
        payload.value(QStringLiteral("tasks")).toArray();
    m_organizationTaskActor =
        payload.value(QStringLiteral("actor")).toString().toLower();
    m_organizationTasksCanManage =
        payload.value(QStringLiteral("canManage")).toBool();
    m_organizationTaskMembers.clear();
    for (const QJsonValue &value :
         payload.value(QStringLiteral("members")).toArray()) {
        const QString member = value.toString().trimmed().toLower();
        if (!member.isEmpty())
            m_organizationTaskMembers.append(member);
    }
    m_organizationTaskDepartments.clear();
    for (const QJsonValue &value :
         payload.value(QStringLiteral("departments")).toArray()) {
        const QString department = value.toString().trimmed().toLower();
        if (!department.isEmpty())
            m_organizationTaskDepartments.append(department);
    }

    renderOrganizationTaskRows(selectedId);
    const int openCount = openOrganizationTaskCount(m_organizationTasks);
    if (m_organizationTasksSummary) {
        m_organizationTasksSummary->setText(
            QStringLiteral("%1 open \xC2\xB7 %2 total%3")
                .arg(openCount)
                .arg(m_organizationTasks.size())
                .arg(m_organizationTasksCanManage
                         ? QStringLiteral(" \xC2\xB7 manager")
                         : QString()));
    }
    setOrganizationTaskBadge(openCount);
    refreshOrganizationTaskQueue();
}

// Paint one page of the catalog. `m_organizationTasks` always holds every task
// the relay returned; the search narrows that whole list and the page then cuts
// a window out of the match set, so nothing past row 100 is unreachable.
// Column 0 keeps the task's absolute index in Qt::UserRole, which is what
// selectedOrganizationTask() resolves against.
void MainWindow::renderOrganizationTaskRows(const QString &selectTaskId)
{
    if (!m_organizationTasksTable)
        return;
    const QString selectedId =
        selectTaskId.isEmpty()
            ? taskText(selectedOrganizationTask(m_organizationTasksTable,
                                                m_organizationTasks),
                       QStringLiteral("id"))
            : selectTaskId;
    const QString query = m_organizationTasksSearch
                              ? m_organizationTasksSearch->text().trimmed()
                              : QString();
    QList<int> matches;
    matches.reserve(m_organizationTasks.size());
    for (int index = 0; index < m_organizationTasks.size(); ++index) {
        const QJsonObject task = m_organizationTasks.at(index).toObject();
        if (query.isEmpty()) {
            matches.append(index);
            continue;
        }
        const QJsonObject qa = task.value(QStringLiteral("qa")).toObject();
        const QString haystack =
            QStringList{
                taskText(task, QStringLiteral("title")),
                taskText(task, QStringLiteral("details")),
                taskAssigneeLabel(task),
                taskText(task, QStringLiteral("repository")),
                taskText(task, QStringLiteral("department")),
                taskText(task, QStringLiteral("status")),
                taskText(qa, QStringLiteral("howToTest")),
                taskText(task, QStringLiteral("id")),
            }.join(QLatin1Char('\n'));
        if (haystack.contains(query, Qt::CaseInsensitive))
            matches.append(index);
    }

    const int pageCount =
        qMax(1, (matches.size() + kOrganizationTaskPageSize - 1) /
                    kOrganizationTaskPageSize);
    m_organizationTasksPage =
        qBound(0, m_organizationTasksPage, pageCount - 1);
    const int first = m_organizationTasksPage * kOrganizationTaskPageSize;
    const int last =
        qMin(matches.size(), first + kOrganizationTaskPageSize);

    // Drop every row first: clearContents() would leave the previous page's
    // priority spin boxes and assignee combos behind on rows this page renders
    // as plain cells.
    m_organizationTasksTable->setRowCount(0);
    m_organizationTasksTable->setRowCount(qMax(0, last - first));
    int selectedRow = -1;
    for (int row = 0; row + first < last; ++row) {
        const int index = matches.at(first + row);
        const QJsonObject task = m_organizationTasks.at(index).toObject();
        const QJsonObject qa = task.value(QStringLiteral("qa")).toObject();
        const bool done =
            task.value(QStringLiteral("completedAt")).toDouble() > 0 ||
            taskText(task, QStringLiteral("status")) == QLatin1String("done");
        const QStringList values = {
            QString::number(task.value(QStringLiteral("priority")).toInt()),
            taskText(task, QStringLiteral("title")),
            taskText(task, QStringLiteral("status")) == QLatin1String("done")
                ? QStringLiteral("\u2713 Done")
                : taskText(task, QStringLiteral("status")) ==
                          QLatin1String("active")
                    ? QStringLiteral("\u25CC In progress")
                    : taskText(task, QStringLiteral("assigneeKind")) ==
                                  QLatin1String("agent") &&
                              !taskText(task, QStringLiteral("agentSessionId"))
                                   .isEmpty()
                        ? QStringLiteral("\u2261 Queued")
                        : QStringLiteral("\u25C6 Ready"),
            taskText(task, QStringLiteral("department")),
            taskText(task, QStringLiteral("repository")),
            taskAssigneeLabel(task),
            taskText(qa, QStringLiteral("status")),
        };
        for (int column = 0; column < values.size(); ++column) {
            auto *item = new QTableWidgetItem(values.at(column));
            item->setToolTip(values.at(column));
            if (column == 0) {
                item->setData(Qt::UserRole, index);
                item->setTextAlignment(Qt::AlignCenter);
            }
            m_organizationTasksTable->setItem(row, column, item);
        }
        if (m_organizationTasksCanManage && !done &&
            taskText(task, QStringLiteral("status")) !=
                QLatin1String("active")) {
            auto *priority = new QSpinBox;
            priority->setRange(1, 99);
            priority->setValue(
                task.value(QStringLiteral("priority")).toInt(50));
            priority->setFrame(false);
            const QString id = taskText(task, QStringLiteral("id"));
            connect(priority, &QSpinBox::editingFinished, this,
                    [this, priority, id] {
                        requestOrganizationTasks(
                            QByteArrayLiteral("PATCH"),
                            QStringLiteral("/api/tasks/") + id,
                            {{QStringLiteral("priority"), priority->value()}},
                            [this](bool ok, const QJsonObject &,
                                   const QString &error) {
                                m_organizationTasksStatus->setText(
                                    ok ? QStringLiteral("Priority updated.")
                                       : QStringLiteral(
                                             "Priority update failed: %1")
                                             .arg(error));
                                if (ok)
                                    refreshOrganizationTasks();
                            });
                    });
            m_organizationTasksTable->setCellWidget(row, 0, priority);

            auto *assignee = new QComboBox;
            assignee->addItem(QStringLiteral("Bot"), QStringLiteral("agent"));
            assignee->addItem(QStringLiteral("Unassigned"),
                              QStringLiteral("unassigned"));
            for (const QString &member :
                 std::as_const(m_organizationTaskMembers))
                assignee->addItem(QStringLiteral("@") + member,
                                  QStringLiteral("user:") + member);
            QString selected =
                taskText(task, QStringLiteral("assigneeKind"));
            if (selected == QLatin1String("user"))
                selected = QStringLiteral("user:") +
                           taskText(task, QStringLiteral("assignee"));
            assignee->setCurrentIndex(
                qMax(0, assignee->findData(selected)));
            connect(
                assignee, &QComboBox::activated, this,
                [this, assignee, id](int) {
                    const QString value = assignee->currentData().toString();
                    QJsonObject body{
                        {QStringLiteral("assigneeKind"),
                         value.startsWith(QLatin1String("user:"))
                             ? QStringLiteral("user")
                             : value},
                    };
                    if (value.startsWith(QLatin1String("user:")))
                        body.insert(QStringLiteral("assignee"), value.mid(5));
                    requestOrganizationTasks(
                        QByteArrayLiteral("PATCH"),
                        QStringLiteral("/api/tasks/") + id, body,
                        [this](bool ok, const QJsonObject &,
                               const QString &error) {
                            m_organizationTasksStatus->setText(
                                ok ? QStringLiteral("Assignee updated.")
                                   : QStringLiteral(
                                         "Assignee update failed: %1")
                                         .arg(error));
                            if (ok)
                                refreshOrganizationTasks();
                        });
                });
            m_organizationTasksTable->setCellWidget(row, 5, assignee);
        }
        if (taskText(task, QStringLiteral("id")) == selectedId)
            selectedRow = row;
    }
    if (m_organizationTaskPageLabel) {
        m_organizationTaskPageLabel->setText(
            matches.isEmpty()
                ? (m_organizationTasks.isEmpty()
                       ? QStringLiteral("No tasks")
                       : QStringLiteral("No tasks match this search"))
                : QStringLiteral("%1\xE2\x80\x93%2 of %3 \xC2\xB7 page %4 of %5")
                      .arg(first + 1)
                      .arg(last)
                      .arg(matches.size())
                      .arg(m_organizationTasksPage + 1)
                      .arg(pageCount));
    }
    if (m_organizationTaskPrevPageButton)
        m_organizationTaskPrevPageButton->setEnabled(
            m_organizationTasksPage > 0);
    if (m_organizationTaskNextPageButton)
        m_organizationTaskNextPageButton->setEnabled(
            m_organizationTasksPage + 1 < pageCount);
    if (selectedRow >= 0)
        m_organizationTasksTable->selectRow(selectedRow);
    else if (m_organizationTasksTable->rowCount() > 0)
        m_organizationTasksTable->selectRow(0);
    renderOrganizationTaskDetail();
    updateOrganizationTaskActions();
}

void MainWindow::refreshOrganizationTaskQueue()
{
    if (!m_organizationTaskQueueTable)
        return;
    const int selectedId =
        m_organizationTaskQueueTable->currentRow() >= 0 &&
                m_organizationTaskQueueTable->item(
                    m_organizationTaskQueueTable->currentRow(), 0)
            ? m_organizationTaskQueueTable
                  ->item(m_organizationTaskQueueTable->currentRow(), 0)
                  ->data(Qt::UserRole)
                  .toInt()
            : 0;
    m_organizationTaskQueueTable->setRowCount(0);
    int selectedRow = -1;
    for (const AgentSession &session : std::as_const(m_agentSessions)) {
        const bool supported =
            session.provider == QLatin1String("codex") ||
            session.provider.startsWith(QLatin1String("claude"));
        if (!supported || !session.orgTaskId.trimmed().isEmpty())
            continue;
        const int row = m_organizationTaskQueueTable->rowCount();
        m_organizationTaskQueueTable->insertRow(row);
        QString title = session.issueTitle.trimmed();
        if (title.isEmpty())
            title = session.prompt.simplified().left(160);
        if (title.isEmpty())
            title = QStringLiteral("Queued agent item #%1").arg(session.id);
        const QString provider =
            session.provider == QLatin1String("codex")
                ? QStringLiteral("Codex")
                : QStringLiteral("Claude");
        const QStringList values{
            provider,
            title,
            session.status,
            session.owner + QLatin1Char('/') + session.name,
            taskTimestamp(session.createdAtMs),
        };
        for (int column = 0; column < values.size(); ++column) {
            auto *item = new QTableWidgetItem(values.at(column));
            item->setToolTip(values.at(column));
            if (column == 0)
                item->setData(Qt::UserRole, session.id);
            m_organizationTaskQueueTable->setItem(row, column, item);
        }
        if (session.id == selectedId)
            selectedRow = row;
    }
    if (selectedRow >= 0)
        m_organizationTaskQueueTable->selectRow(selectedRow);
    m_organizationTaskQueueMoveButton->setEnabled(
        selectedRow >= 0 && m_organizationTasksCanManage);
}

void MainWindow::moveQueuedAgentItemToTasks()
{
    const int row = m_organizationTaskQueueTable
                        ? m_organizationTaskQueueTable->currentRow()
                        : -1;
    QTableWidgetItem *item =
        row >= 0 ? m_organizationTaskQueueTable->item(row, 0) : nullptr;
    const int sessionId = item ? item->data(Qt::UserRole).toInt() : 0;
    auto it = std::find_if(
        m_agentSessions.begin(), m_agentSessions.end(),
        [sessionId](const AgentSession &session) {
            return session.id == sessionId;
        });
    if (it == m_agentSessions.end() || !m_organizationTasksCanManage)
        return;
    QString title = it->issueTitle.trimmed();
    if (title.isEmpty())
        title = it->prompt.simplified().left(160);
    if (title.isEmpty())
        title = QStringLiteral("Queued agent item #%1").arg(it->id);
    const QString details = it->prompt.trimmed().left(4000);
    const QJsonObject body{
        {QStringLiteral("title"), title},
        {QStringLiteral("details"), details},
        {QStringLiteral("department"), QStringLiteral("engineering")},
        {QStringLiteral("destination"), QStringLiteral("agent")},
        {QStringLiteral("assigneeKind"), QStringLiteral("agent")},
        {QStringLiteral("repository"),
         it->owner + QLatin1Char('/') + it->name},
        {QStringLiteral("priority"), 50},
        {QStringLiteral("agent"),
         QJsonObject{
             {QStringLiteral("provider"), it->provider},
             {QStringLiteral("model"), it->model},
             {QStringLiteral("mode"), it->mode},
             {QStringLiteral("strength"), it->strength},
             {QStringLiteral("sessionId"), QString::number(it->id)},
         }},
    };
    requestOrganizationTasks(
        QByteArrayLiteral("POST"), QStringLiteral("/api/tasks"), body,
        [this, sessionId](bool ok, const QJsonObject &payload,
                          const QString &error) {
            if (!ok) {
                m_organizationTasksStatus->setText(
                    QStringLiteral("Queue import failed: %1").arg(error));
                return;
            }
            const QString taskId =
                payload.value(QStringLiteral("task"))
                    .toObject()
                    .value(QStringLiteral("id"))
                    .toString();
            auto session = std::find_if(
                m_agentSessions.begin(), m_agentSessions.end(),
                [sessionId](const AgentSession &candidate) {
                    return candidate.id == sessionId;
                });
            if (session != m_agentSessions.end()) {
                session->orgTask = true;
                session->orgTaskId = taskId;
                if (m_agentStore)
                    m_agentStore->saveSession(*session);
            }
            m_organizationTasksStatus->setText(
                QStringLiteral("Queued item moved into Tasks."));
            refreshOrganizationTaskQueue();
            refreshOrganizationTasks();
        });
}

void MainWindow::renderOrganizationTaskDetail()
{
    if (!m_organizationTaskDetail)
        return;
    m_organizationTaskDetail->setHtml(taskDetailHtml(
        selectedOrganizationTask(
            m_organizationTasksTable, m_organizationTasks)));
}

void MainWindow::updateOrganizationTaskActions()
{
    const QJsonObject task = selectedOrganizationTask(
        m_organizationTasksTable, m_organizationTasks);
    const bool selected = !task.isEmpty();
    const bool done =
        task.value(QStringLiteral("completedAt")).toDouble() > 0 ||
        taskText(task, QStringLiteral("status")) == QLatin1String("done");
    const bool active =
        taskText(task, QStringLiteral("status")) == QLatin1String("active");
    const bool agent =
        taskText(task, QStringLiteral("assigneeKind")) ==
        QLatin1String("agent");
    const bool mine =
        taskText(task, QStringLiteral("assignee")).compare(
            m_organizationTaskActor, Qt::CaseInsensitive) == 0;
    const bool hasRepository =
        !taskText(task, QStringLiteral("repository")).isEmpty();
    if (m_organizationTaskNewButton)
        m_organizationTaskNewButton->setVisible(
            m_organizationTasksCanManage);
    if (m_organizationTaskEditButton)
        m_organizationTaskEditButton->setEnabled(
            selected && m_organizationTasksCanManage && !active && !done);
    if (m_organizationTaskAgentButton)
        m_organizationTaskAgentButton->setEnabled(
            selected && m_organizationTasksCanManage && !active && !done &&
            hasRepository);
    if (m_organizationTaskStartButton) {
        m_organizationTaskStartButton->setText(
            active ? QStringLiteral("Stop") : QStringLiteral("Start"));
        m_organizationTaskStartButton->setEnabled(
            selected && mine && !done);
    }
    if (m_organizationTaskCompleteButton)
        m_organizationTaskCompleteButton->setEnabled(
            selected && !done && (mine || m_organizationTasksCanManage));
    if (m_organizationTaskQaButton)
        m_organizationTaskQaButton->setEnabled(
            selected && !done && m_organizationTasksCanManage);
    if (m_organizationTaskReturnButton)
        m_organizationTaskReturnButton->setEnabled(
            selected && !done && agent && m_organizationTasksCanManage);
    if (m_organizationTaskDeleteButton)
        m_organizationTaskDeleteButton->setEnabled(
            selected && m_organizationTasksCanManage);
    if (m_organizationTaskFollowUpButton)
        m_organizationTaskFollowUpButton->setEnabled(
            selected &&
            (m_organizationTasksCanManage || mine ||
             taskText(task, QStringLiteral("createdBy")) ==
                 m_organizationTaskActor));
}

void MainWindow::createOrganizationTask()
{
    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("New organization task"));
    auto *layout = new QFormLayout(&dialog);
    auto *title = new QLineEdit;
    title->setMaxLength(160);
    auto *details = new QPlainTextEdit;
    details->setMaximumHeight(120);
    auto *department = new QComboBox;
    department->addItems(m_organizationTaskDepartments.isEmpty()
                             ? QStringList{QStringLiteral("general"),
                                           QStringLiteral("engineering")}
                             : m_organizationTaskDepartments);
    auto *repository = new QLineEdit(QStringLiteral("forkmesh/forkmesh"));
    auto *assignee = new QComboBox;
    assignee->addItem(QStringLiteral("Bot"), QStringLiteral("agent"));
    assignee->addItem(QStringLiteral("Unassigned"),
                      QStringLiteral("unassigned"));
    for (const QString &member : std::as_const(m_organizationTaskMembers))
        assignee->addItem(member, QStringLiteral("user:") + member);
    auto *priority = new QSpinBox;
    priority->setRange(1, 99);
    priority->setValue(50);
    layout->addRow(QStringLiteral("Title"), title);
    layout->addRow(QStringLiteral("Details"), details);
    layout->addRow(QStringLiteral("Department"), department);
    layout->addRow(QStringLiteral("Repository"), repository);
    layout->addRow(QStringLiteral("Assignee"), assignee);
    layout->addRow(QStringLiteral("Priority"), priority);
    auto *buttons = new QDialogButtonBox(
        QDialogButtonBox::Save | QDialogButtonBox::Cancel);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addRow(buttons);
    title->setFocus();
    if (dialog.exec() != QDialog::Accepted || title->text().trimmed().isEmpty())
        return;

    const QString assignment = assignee->currentData().toString();
    const bool agent = assignment == QLatin1String("agent");
    QJsonObject body{
        {QStringLiteral("title"), title->text().trimmed()},
        {QStringLiteral("details"), details->toPlainText().trimmed()},
        {QStringLiteral("department"), department->currentText()},
        {QStringLiteral("destination"),
         agent ? QStringLiteral("agent") : QStringLiteral("department")},
        {QStringLiteral("assigneeKind"),
         agent ? QStringLiteral("agent")
               : assignment == QLatin1String("unassigned")
                   ? QStringLiteral("unassigned")
                   : QStringLiteral("user")},
        {QStringLiteral("repository"), repository->text().trimmed()},
        {QStringLiteral("priority"), priority->value()},
    };
    if (assignment.startsWith(QLatin1String("user:")))
        body.insert(QStringLiteral("assignee"), assignment.mid(5));
    requestOrganizationTasks(
        QByteArrayLiteral("POST"), QStringLiteral("/api/tasks"), body,
        [this, agent](bool ok, const QJsonObject &payload,
                      const QString &error) {
            if (!ok) {
                m_organizationTasksStatus->setText(
                    QStringLiteral("Task creation failed: %1").arg(error));
                return;
            }
            const QJsonObject task =
                payload.value(QStringLiteral("task")).toObject();
            m_organizationTasksStatus->setText(
                QStringLiteral("Task created."));
            if (agent)
                queueOrganizationTaskAgent(task);
            else
                refreshOrganizationTasks();
        });
}

// File the prompt-bar text as an ordinary shared task. It is deliberately
// unassigned and routed to General: pressing "task" must not launch a genie (or
// any other agent) as a side effect. An operator can assign/start it from the
// Tasks page when it is ready.
void MainWindow::createQuickAddOrganizationTask()
{
    if (!m_issueQuickAdd)
        return;
    const QString prompt = m_issueQuickAdd->toPlainText().trimmed();
    if (prompt.isEmpty()) {
        flashMessage(QStringLiteral("Type a task first."), true);
        m_issueQuickAdd->setFocus();
        return;
    }

    QString title = prompt.section(QLatin1Char('\n'), 0, 0).simplified();
    if (title.size() > 160)
        title = title.left(159).trimmed() + QString::fromUtf8("\xE2\x80\xA6");
    QString repository;
    const int repoIndex = issuesRepoIndex();
    if (repoIndex >= 0 && repoIndex < m_repositories.size()) {
        const RepositoryRecord &repo = m_repositories.at(repoIndex);
        repository = repo.owner + QLatin1Char('/') + repo.name;
    }
    const QJsonObject body{
        {QStringLiteral("title"), title},
        {QStringLiteral("details"), prompt},
        {QStringLiteral("department"), QStringLiteral("general")},
        {QStringLiteral("destination"), QStringLiteral("department")},
        {QStringLiteral("assigneeKind"), QStringLiteral("unassigned")},
        {QStringLiteral("repository"), repository},
        {QStringLiteral("priority"), 50},
    };
    requestOrganizationTasks(
        QByteArrayLiteral("POST"), QStringLiteral("/api/tasks"), body,
        [this, prompt](bool ok, const QJsonObject &, const QString &error) {
            if (!ok) {
                flashMessage(QStringLiteral("Task creation failed: %1").arg(error),
                             true);
                return;
            }
            recordQuickAddHistory(prompt);
            m_issueQuickAdd->clear();
            clearQuickAddImages();
            showSection(kOrganizationTasksSectionIndex);
            refreshOrganizationTasks();
            flashMessage(QStringLiteral("Task added to General."));
        });
}

void MainWindow::createOrganizationTaskFollowUp()
{
    const QJsonObject parent = selectedOrganizationTask(
        m_organizationTasksTable, m_organizationTasks);
    const QString parentId = taskText(parent, QStringLiteral("id"));
    if (parentId.isEmpty())
        return;
    bool accepted = false;
    const QString title = QInputDialog::getText(
        this, QStringLiteral("Follow-up task"),
        QStringLiteral("Task title (assignee is inherited)"),
        QLineEdit::Normal, QString(), &accepted).trimmed();
    if (!accepted || title.isEmpty())
        return;
    requestOrganizationTasks(
        QByteArrayLiteral("POST"), QStringLiteral("/api/tasks"),
        {{QStringLiteral("title"), title},
         {QStringLiteral("parentTaskId"), parentId}},
        [this](bool ok, const QJsonObject &, const QString &error) {
            m_organizationTasksStatus->setText(
                ok ? QStringLiteral(
                         "Follow-up created with the same assignee.")
                   : QStringLiteral("Follow-up creation failed: %1")
                         .arg(error));
            if (ok)
                refreshOrganizationTasks();
        });
}

void MainWindow::editOrganizationTask()
{
    const QJsonObject task = selectedOrganizationTask(
        m_organizationTasksTable, m_organizationTasks);
    if (task.isEmpty())
        return;
    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("Edit organization task"));
    auto *layout = new QFormLayout(&dialog);
    auto *title = new QLineEdit(taskText(task, QStringLiteral("title")));
    title->setMaxLength(160);
    auto *details = new QPlainTextEdit(
        taskText(task, QStringLiteral("details")));
    details->setMaximumHeight(120);
    auto *repository = new QLineEdit(
        taskText(task, QStringLiteral("repository")));
    auto *assignee = new QComboBox;
    assignee->addItem(QStringLiteral("Bot"), QStringLiteral("agent"));
    assignee->addItem(QStringLiteral("Unassigned"),
                      QStringLiteral("unassigned"));
    for (const QString &member : std::as_const(m_organizationTaskMembers))
        assignee->addItem(member, QStringLiteral("user:") + member);
    QString current = taskText(task, QStringLiteral("assigneeKind"));
    if (current == QLatin1String("user"))
        current = QStringLiteral("user:") +
                  taskText(task, QStringLiteral("assignee"));
    const int assignmentIndex = assignee->findData(current);
    if (assignmentIndex >= 0)
        assignee->setCurrentIndex(assignmentIndex);
    auto *priority = new QSpinBox;
    priority->setRange(1, 99);
    priority->setValue(task.value(QStringLiteral("priority")).toInt(50));
    layout->addRow(QStringLiteral("Title"), title);
    layout->addRow(QStringLiteral("Details"), details);
    layout->addRow(QStringLiteral("Repository"), repository);
    layout->addRow(QStringLiteral("Assignee"), assignee);
    layout->addRow(QStringLiteral("Priority"), priority);
    auto *buttons = new QDialogButtonBox(
        QDialogButtonBox::Save | QDialogButtonBox::Cancel);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addRow(buttons);
    if (dialog.exec() != QDialog::Accepted || title->text().trimmed().isEmpty())
        return;
    const QString assignment = assignee->currentData().toString();
    const bool assignAgent = assignment == QLatin1String("agent");
    QJsonObject body{
        {QStringLiteral("title"), title->text().trimmed()},
        {QStringLiteral("details"), details->toPlainText().trimmed()},
        {QStringLiteral("repository"), repository->text().trimmed()},
        {QStringLiteral("priority"), priority->value()},
        {QStringLiteral("assigneeKind"),
         assignAgent ? QStringLiteral("agent")
                     : assignment == QLatin1String("unassigned")
                         ? QStringLiteral("unassigned")
                         : QStringLiteral("user")},
    };
    if (assignment.startsWith(QLatin1String("user:")))
        body.insert(QStringLiteral("assignee"), assignment.mid(5));
    const QString path = QStringLiteral("/api/tasks/") +
                         taskText(task, QStringLiteral("id"));
    requestOrganizationTasks(
        QByteArrayLiteral("PATCH"), path, body,
        [this, assignAgent](bool ok, const QJsonObject &payload,
                            const QString &error) {
            if (!ok) {
                m_organizationTasksStatus->setText(
                    QStringLiteral("Task update failed: %1").arg(error));
                return;
            }
            const QJsonObject changed =
                payload.value(QStringLiteral("task")).toObject();
            m_organizationTasksStatus->setText(
                QStringLiteral("Task updated."));
            if (assignAgent &&
                taskText(changed, QStringLiteral("agentSessionId")).isEmpty())
                queueOrganizationTaskAgent(changed);
            else
                refreshOrganizationTasks();
        });
}

void MainWindow::assignOrganizationTaskToAgent()
{
    const QJsonObject task = selectedOrganizationTask(
        m_organizationTasksTable, m_organizationTasks);
    if (task.isEmpty())
        return;
    if (taskText(task, QStringLiteral("assigneeKind")) ==
        QLatin1String("agent")) {
        queueOrganizationTaskAgent(task);
        return;
    }
    const QString path = QStringLiteral("/api/tasks/") +
                         taskText(task, QStringLiteral("id"));
    requestOrganizationTasks(
        QByteArrayLiteral("PATCH"), path,
        {{QStringLiteral("assigneeKind"), QStringLiteral("agent")},
         {QStringLiteral("repository"),
          taskText(task, QStringLiteral("repository"))}},
        [this](bool ok, const QJsonObject &payload, const QString &error) {
            if (!ok) {
                m_organizationTasksStatus->setText(
                    QStringLiteral("Agent assignment failed: %1").arg(error));
                return;
            }
            queueOrganizationTaskAgent(
                payload.value(QStringLiteral("task")).toObject());
        });
}

void MainWindow::queueOrganizationTaskAgent(const QJsonObject &task)
{
    const QStringList repository =
        taskText(task, QStringLiteral("repository"))
            .split(QLatin1Char('/'), Qt::SkipEmptyParts);
    if (repository.size() != 2) {
        m_organizationTasksStatus->setText(
            QStringLiteral("Agent assignment needs an owner/repository."));
        return;
    }
    const QString id = taskText(task, QStringLiteral("id"));
    const QString title = taskText(task, QStringLiteral("title"));
    const QString details = taskText(task, QStringLiteral("details"));
    const QString prompt =
        QStringLiteral("[task:%1] %2%3")
            .arg(id, title,
                 details.isEmpty() ? QString()
                                   : QStringLiteral("\n\n") + details);
    const QString endpoint =
        QStringLiteral("/api/orgs/%1/repos/%2/agent-bots")
            .arg(repository.at(0).toLower(), repository.at(1));
    m_organizationTasksStatus->setText(
        QString::fromUtf8("Assigning task to an eligible agent node\xE2\x80\xA6"));
    requestOrganizationTasks(
        QByteArrayLiteral("POST"), endpoint,
        {{QStringLiteral("provider"), QStringLiteral("agent")},
         {QStringLiteral("prompt"), prompt.left(8000)},
         {QStringLiteral("title"), title},
         {QStringLiteral("taskKey"), QStringLiteral("task:") + id}},
        [this, id](bool ok, const QJsonObject &payload,
                   const QString &error) {
            if (!ok) {
                m_organizationTasksStatus->setText(
                    QStringLiteral(
                        "Task is assigned to the agent queue, but dispatch "
                        "failed: %1")
                        .arg(error));
                refreshOrganizationTasks();
                return;
            }
            const QString sessionId =
                payload.value(QStringLiteral("session"))
                    .toObject()
                    .value(QStringLiteral("id"))
                    .toString();
            if (sessionId.isEmpty()) {
                m_organizationTasksStatus->setText(
                    QStringLiteral("Agent dispatch returned no session id."));
                refreshOrganizationTasks();
                return;
            }
            requestOrganizationTasks(
                QByteArrayLiteral("PATCH"),
                QStringLiteral("/api/tasks/") + id,
                {{QStringLiteral("agentSessionId"), sessionId}},
                [this](bool linked, const QJsonObject &,
                       const QString &linkError) {
                    m_organizationTasksStatus->setText(
                        linked
                            ? QStringLiteral(
                                  "Agent session queued and linked to the task.")
                            : QStringLiteral(
                                  "Agent queued, but linking its session failed: "
                                  "%1")
                                  .arg(linkError));
                    refreshOrganizationTasks();
                });
        });
}

void MainWindow::runOrganizationTaskAction(const QString &action)
{
    const QJsonObject task = selectedOrganizationTask(
        m_organizationTasksTable, m_organizationTasks);
    const QString id = taskText(task, QStringLiteral("id"));
    if (id.isEmpty())
        return;
    QJsonObject body;
    if (action == QLatin1String("complete")) {
        bool accepted = false;
        const QString note = QInputDialog::getMultiLineText(
            this, QStringLiteral("Complete organization task"),
            QStringLiteral("Completion note and evidence"),
            taskText(task, QStringLiteral("completionNote")), &accepted);
        if (!accepted)
            return;
        body.insert(QStringLiteral("completionNote"), note.trimmed());
    } else if (action == QLatin1String("qa")) {
        const QJsonObject qa = task.value(QStringLiteral("qa")).toObject();
        bool accepted = false;
        const QString howToTest = QInputDialog::getMultiLineText(
            this, QStringLiteral("Send organization task to QA"),
            QStringLiteral("Exact QA steps"),
            taskText(qa, QStringLiteral("howToTest")), &accepted);
        if (!accepted)
            return;
        body.insert(QStringLiteral("howToTest"), howToTest.trimmed());
    }
    requestOrganizationTasks(
        QByteArrayLiteral("POST"),
        QStringLiteral("/api/tasks/%1/%2").arg(id, action), body,
        [this, action](bool ok, const QJsonObject &, const QString &error) {
            m_organizationTasksStatus->setText(
                ok ? QStringLiteral("Task action completed: %1.").arg(action)
                   : QStringLiteral("Task action failed: %1").arg(error));
            if (ok)
                refreshOrganizationTasks();
        });
}

void MainWindow::deleteOrganizationTask()
{
    const QJsonObject task = selectedOrganizationTask(
        m_organizationTasksTable, m_organizationTasks);
    const QString id = taskText(task, QStringLiteral("id"));
    if (id.isEmpty())
        return;
    requestOrganizationTasks(
        QByteArrayLiteral("DELETE"), QStringLiteral("/api/tasks/") + id, {},
        [this](bool ok, const QJsonObject &, const QString &error) {
            m_organizationTasksStatus->setText(
                ok ? QStringLiteral("Task deleted.")
                   : QStringLiteral("Task deletion failed: %1").arg(error));
            if (ok) {
                refreshOrganizationTasks();
                return;
            }
            // A failed delete left the task sitting right where it was, with
            // only a status label above the table to explain why — easy to
            // miss, which read as "delete did nothing" (adhoc #108). Put the
            // reason somewhere the operator cannot scroll past.
            QMessageBox::warning(this, QStringLiteral("Delete task"), error);
        });
}
