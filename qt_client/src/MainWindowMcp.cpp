// Settings -> MCP: the connector that lets an outside agent work this node's
// mesh (adhoc #16).
//
// tools/forkmesh_mcp_server.py has always been able to serve issues, projects
// and PRs to any MCP-capable agent, but connecting it meant knowing where the
// script lives, which environment variables it reads, and that its write tools
// sign as your node. This page turns that into: press Generate, copy one JSON
// block into the agent, done — and the token it mints is the thing that lets
// that agent act with this node's identity, the way an API token would.
//
// These are MainWindow member functions in their own translation unit; the
// class itself is declared in MainWindow.h.

#include "ControlNode.h"
#include "MainWindow.h"
#include "MainWindowInternal.h"
#include "McpConnector.h"

#include <QApplication>
#include <QClipboard>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QSettings>
#include <QStandardPaths>
#include <QTimer>

using namespace forkmesh::ui;

namespace {

// The interpreter the generated config will name. An absolute path keeps the
// agent working even when it is launched with a different PATH than this app.
QString pythonCommand()
{
    const QString found = QStandardPaths::findExecutable(QStringLiteral("python3"));
    return found.isEmpty() ? QStringLiteral("python3") : found;
}

QString mcpAppDataDir()
{
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
}

// The marker a genie run prints, on a line of its own, the moment it picks a
// task off the shared list: "FORKMESH_TASK <id>: <title>". Kept deliberately
// loose about the separator (colon or dash) and tolerant of the surrounding
// markdown the CLI likes to add, since it is the agent — not us — writing it.
const QRegularExpression &genieTaskLineRe()
{
    static const QRegularExpression re(
        QStringLiteral("FORKMESH_TASK[ \\t]+([A-Za-z0-9_-]{1,32})[ \\t]*"
                       "[:\\-\\x{2013}\\x{2014}][ \\t]*([^\\n]{1,200})"));
    return re;
}

} // namespace

QString MainWindow::mcpServerScriptPath() const
{
    return forkmesh::control::findMcpServerScript(
        QStringLiteral(FORKMESH_SOURCE_DIR),
        QCoreApplication::applicationDirPath());
}

QWidget *MainWindow::buildMcpConnectorTab()
{
    auto *body = new QWidget;
    auto *col = new QVBoxLayout(body);
    col->setContentsMargins(2, 14, 2, 14);
    col->setSpacing(10);

    auto *headLabel = new QLabel("MCP CONNECTOR");
    headLabel->setObjectName("sectionLabel");

    auto *hint = new QLabel(
        "Connect any MCP-capable agent \xe2\x80\x94 Claude Code, Codex, an "
        "editor extension, your own script \xe2\x80\x94 to this node. The agent "
        "gets tools for the mesh: it can search and read repositories, file and "
        "comment on issues, plan with milestones and projects, and open pull "
        "requests. Generate a connector token below and paste the config into "
        "the agent; from then on it works your tasks with this node's identity, "
        "the way a personal access token would let it act as you.");
    hint->setObjectName("statusLine");
    hint->setWordWrap(true);

    m_mcpStatusLabel = new QLabel;
    m_mcpStatusLabel->setObjectName("modeHint");
    m_mcpStatusLabel->setWordWrap(true);
    m_mcpStatusLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);

    // --- token -------------------------------------------------------------
    auto *tokenLabel = new QLabel("CONNECTOR TOKEN");
    tokenLabel->setObjectName("sectionLabel");

    m_mcpTokenEdit = new QLineEdit;
    m_mcpTokenEdit->setReadOnly(true);
    m_mcpTokenEdit->setPlaceholderText(
        "No connector token \xe2\x80\x94 agents get read-only tools");
    m_mcpTokenEdit->setToolTip(
        "The secret an agent presents as FORKMESH_MCP_TOKEN. Anything holding "
        "it can write signed issues, comments and pull requests as this node.");

    m_mcpGenerateButton = new QPushButton("Generate token");
    m_mcpGenerateButton->setObjectName("primaryButton");
    m_mcpGenerateButton->setCursor(Qt::PointingHandCursor);
    connect(m_mcpGenerateButton, &QPushButton::clicked, this,
            &MainWindow::generateMcpConnector);

    auto *copyTokenButton = new QPushButton("Copy token");
    copyTokenButton->setCursor(Qt::PointingHandCursor);
    connect(copyTokenButton, &QPushButton::clicked, this, [this] {
        const QString token =
            forkmesh::mcp::loadConnector(mcpAppDataDir()).token;
        if (token.isEmpty()) {
            flashMessage("Generate a connector token first.", true);
            return;
        }
        QApplication::clipboard()->setText(token);
        flashMessage("Connector token copied.", false);
    });

    m_mcpRevokeButton = new QPushButton("Revoke");
    m_mcpRevokeButton->setCursor(Qt::PointingHandCursor);
    m_mcpRevokeButton->setToolTip(
        "Delete the token. Every agent still configured with it drops to "
        "read-only immediately.");
    connect(m_mcpRevokeButton, &QPushButton::clicked, this,
            &MainWindow::revokeMcpConnector);

    auto *tokenRow = new QHBoxLayout;
    tokenRow->setContentsMargins(0, 0, 0, 0);
    tokenRow->addWidget(m_mcpTokenEdit, 1);
    tokenRow->addWidget(m_mcpGenerateButton);
    tokenRow->addWidget(copyTokenButton);
    tokenRow->addWidget(m_mcpRevokeButton);

    // --- config ------------------------------------------------------------
    auto *configLabel = new QLabel("AGENT CONFIGURATION");
    configLabel->setObjectName("sectionLabel");

    auto *configHint = new QLabel(
        "Paste this into the agent's MCP configuration \xe2\x80\x94 "
        "<code>.mcp.json</code> at the root of a project for Claude Code, or "
        "<code>claude_desktop_config.json</code> for Claude Desktop. Restart "
        "the agent afterwards so it picks the server up.");
    configHint->setObjectName("statusLine");
    configHint->setTextFormat(Qt::RichText);
    configHint->setWordWrap(true);

    m_mcpConfigEdit = new QPlainTextEdit;
    m_mcpConfigEdit->setReadOnly(true);
    m_mcpConfigEdit->setLineWrapMode(QPlainTextEdit::NoWrap);
    m_mcpConfigEdit->setMinimumHeight(170);
    QFont mono(QStringLiteral("monospace"));
    mono.setStyleHint(QFont::Monospace);
    m_mcpConfigEdit->setFont(mono);

    auto *copyConfigButton = new QPushButton("Copy config");
    copyConfigButton->setObjectName("primaryButton");
    copyConfigButton->setCursor(Qt::PointingHandCursor);
    connect(copyConfigButton, &QPushButton::clicked, this, [this] {
        QApplication::clipboard()->setText(m_mcpConfigEdit->toPlainText());
        flashMessage("MCP configuration copied.", false);
    });

    auto *copyCommandButton = new QPushButton("Copy CLI command");
    copyCommandButton->setCursor(Qt::PointingHandCursor);
    copyCommandButton->setToolTip(
        "One-liner that registers the connector for every Claude Code project "
        "on this machine.");
    connect(copyCommandButton, &QPushButton::clicked, this, [this] {
        const QString script = mcpServerScriptPath();
        if (script.isEmpty()) {
            flashMessage("Could not find forkmesh_mcp_server.py.", true);
            return;
        }
        QApplication::clipboard()->setText(forkmesh::mcp::cliCommand(
            pythonCommand(), script, repositoryMirrorRoot(),
            forkmesh::mcp::loadConnector(mcpAppDataDir()).token));
        flashMessage("Claude Code command copied.", false);
    });

    m_mcpTestButton = new QPushButton("Test connection");
    m_mcpTestButton->setCursor(Qt::PointingHandCursor);
    m_mcpTestButton->setToolTip(
        "Launch the server exactly as the agent would and list the tools it "
        "answers with.");
    connect(m_mcpTestButton, &QPushButton::clicked, this,
            &MainWindow::testMcpConnector);

    m_mcpTestLabel = new QLabel;
    m_mcpTestLabel->setObjectName("modeHint");
    m_mcpTestLabel->setWordWrap(true);
    m_mcpTestLabel->hide();

    auto *configButtonRow = new QHBoxLayout;
    configButtonRow->setContentsMargins(0, 0, 0, 0);
    configButtonRow->addWidget(copyConfigButton);
    configButtonRow->addWidget(copyCommandButton);
    configButtonRow->addWidget(m_mcpTestButton);
    configButtonRow->addStretch();

    // --- how to use --------------------------------------------------------
    auto *usageLabel = new QLabel("HOW AGENTS USE IT");
    usageLabel->setObjectName("sectionLabel");

    auto *usage = new QLabel(QStringLiteral(
        "<b>1. Connect.</b> Generate a token, copy the config into the agent, "
        "restart it. Ask the agent to call <code>whoami</code> \xe2\x80\x94 it "
        "answers with this node's public key and whether it may write.<br>"
        "<b>2. Find work.</b> <code>list_repos</code> shows every repository "
        "this node exposes; <code>search_issues</code> finds open tasks; "
        "<code>read_file</code> reads any file at any git ref.<br>"
        "<b>3. Do the work.</b> The agent edits a checkout and commits on a "
        "branch the way it always does \xe2\x80\x94 the connector is for mesh "
        "operations, not for editing files.<br>"
        "<b>4. Report back.</b> <code>comment_on_issue</code> posts progress, "
        "<code>create_issue</code> files follow-ups, "
        "<code>create_milestone</code> / <code>create_project</code> / "
        "<code>update_project</code> keep the plan current, and "
        "<code>open_pr_from_branch</code> opens the pull request. "
        "<code>get_pr_diff</code> reads one back for review.<br><br>"
        "Every write is a normal signed ForkMesh entry \xe2\x80\x94 the same "
        "bytes this app writes, signed by the same identity key. There is no "
        "privileged side door. Reads work without a token, so handing out the "
        "config with the <code>FORKMESH_MCP_TOKEN</code> line removed gives an "
        "agent browse-only access; revoking demotes every agent still holding "
        "the old token to exactly that."));
    usage->setObjectName("statusLine");
    usage->setTextFormat(Qt::RichText);
    usage->setWordWrap(true);

    // --- genie ---------------------------------------------------------------
    // The other direction (adhoc #42): instead of handing an outside agent this
    // node's mesh, this hands a local agent the *website's* task list. The
    // credential is minted on the site (Organization Admin -> "Remote ForkMesh
    // MCP"), pasted here once, and the prompt bar's "genie" button then starts
    // runs that pick their own work off that list.
    auto *genieLabel = new QLabel("GENIE (WEBSITE TASK LIST)");
    genieLabel->setObjectName("sectionLabel");

    auto *genieHint = new QLabel(QStringLiteral(
        "Paste the revocable task-only credential your organization generated "
        "on the website (Organization Admin \xe2\x86\x92 <b>Remote ForkMesh "
        "MCP</b>). The <b>genie</b> button in the prompt bar then starts an "
        "agent against <code>%1</code>: it calls <code>list_org_tasks</code>, "
        "takes the highest-priority unassigned task, works it in an isolated "
        "worktree, and reports back with <code>complete_org_task</code>. The "
        "task it picks becomes this session's title while it runs. Revoke the "
        "credential on the website to cut every agent still holding it.")
            .arg(genieMcpUrl().toString()));
    genieHint->setObjectName("statusLine");
    genieHint->setTextFormat(Qt::RichText);
    genieHint->setWordWrap(true);

    m_genieTokenEdit = new QLineEdit;
    m_genieTokenEdit->setEchoMode(QLineEdit::Password);
    m_genieTokenEdit->setPlaceholderText(
        "Bearer credential from the website \xe2\x80\x94 genie is off without it");
    m_genieTokenEdit->setToolTip(
        "The organization bot token the website minted. Anything holding it can "
        "read and complete that organization's tasks.");

    m_genieOrgEdit = new QLineEdit;
    m_genieOrgEdit->setPlaceholderText("organization");
    m_genieOrgEdit->setToolTip(
        "Named in the prompt so the agent knows whose task list it is working. "
        "The credential itself is what scopes access.");
    m_genieOrgEdit->setMaximumWidth(220);

    m_genieWorkflowCombo = new QComboBox;
    m_genieWorkflowCombo->addItem(QStringLiteral("Finish at a pull request"),
                                  QStringLiteral("pr"));
    m_genieWorkflowCombo->addItem(QStringLiteral("Deploy and verify"),
                                  QStringLiteral("deploy"));
    m_genieWorkflowCombo->setToolTip(
        "Which of the website's two workflows the run follows once the work is "
        "committed.");

    auto *genieSaveButton = new QPushButton("Save genie setup");
    genieSaveButton->setObjectName("primaryButton");
    genieSaveButton->setCursor(Qt::PointingHandCursor);
    connect(genieSaveButton, &QPushButton::clicked, this,
            &MainWindow::saveGenieSettings);

    m_genieStatusLabel = new QLabel;
    m_genieStatusLabel->setObjectName("modeHint");
    m_genieStatusLabel->setWordWrap(true);

    auto *genieRow = new QHBoxLayout;
    genieRow->setContentsMargins(0, 0, 0, 0);
    genieRow->addWidget(m_genieTokenEdit, 1);
    genieRow->addWidget(m_genieOrgEdit);
    genieRow->addWidget(m_genieWorkflowCombo);
    genieRow->addWidget(genieSaveButton);

    col->addWidget(headLabel);
    col->addWidget(hint);
    col->addWidget(m_mcpStatusLabel);
    col->addSpacing(6);
    col->addWidget(tokenLabel);
    col->addLayout(tokenRow);
    col->addSpacing(6);
    col->addWidget(configLabel);
    col->addWidget(configHint);
    col->addWidget(m_mcpConfigEdit);
    col->addLayout(configButtonRow);
    col->addWidget(m_mcpTestLabel);
    col->addSpacing(6);
    col->addWidget(genieLabel);
    col->addWidget(genieHint);
    col->addLayout(genieRow);
    col->addWidget(m_genieStatusLabel);
    col->addSpacing(6);
    col->addWidget(usageLabel);
    col->addWidget(usage);
    col->addStretch();

    refreshMcpConnectorTab();
    return body;
}

void MainWindow::refreshMcpConnectorTab()
{
    if (!m_mcpStatusLabel || !m_mcpTokenEdit || !m_mcpConfigEdit)
        return;

    const QString script = mcpServerScriptPath();
    const forkmesh::mcp::Connector connector =
        forkmesh::mcp::loadConnector(mcpAppDataDir());

    QStringList status;
    status << (script.isEmpty()
                   ? QStringLiteral(
                         "\xe2\x9a\xa0 forkmesh_mcp_server.py was not found in "
                         "this installation \xe2\x80\x94 the configuration "
                         "below cannot be used until it is.")
                   : QStringLiteral("Server: %1").arg(script));
    status << QStringLiteral("Identity: %1")
                  .arg(m_profileIdentity.publicKey().isEmpty()
                           ? QStringLiteral("not loaded")
                           : m_profileIdentity.publicKey());
    status << (connector.isValid()
                   ? QStringLiteral("Access: read + write, granted %1")
                         .arg(QDateTime::fromMSecsSinceEpoch(connector.createdMs)
                                  .toString(QStringLiteral("yyyy-MM-dd HH:mm")))
                   : QStringLiteral(
                         "Access: read-only \xe2\x80\x94 no token has been "
                         "generated yet"));
    m_mcpStatusLabel->setText(status.join(QLatin1Char('\n')));

    m_mcpTokenEdit->setText(forkmesh::mcp::maskToken(connector.token));
    if (m_mcpGenerateButton)
        m_mcpGenerateButton->setText(connector.isValid() ? "Regenerate token"
                                                         : "Generate token");
    if (m_mcpRevokeButton)
        m_mcpRevokeButton->setEnabled(connector.isValid());

    m_mcpConfigEdit->setPlainText(forkmesh::mcp::configJson(
        pythonCommand(),
        script.isEmpty() ? QStringLiteral("/path/to/forkmesh_mcp_server.py")
                         : script,
        repositoryMirrorRoot(), connector.token));

    // Genie (adhoc #42): show the saved website credential and workflow.
    const QSettings settings;
    const QString genieToken =
        settings.value(kGenieTokenSetting).toString().trimmed();
    if (m_genieTokenEdit && m_genieTokenEdit->text().trimmed() != genieToken)
        m_genieTokenEdit->setText(genieToken);
    if (m_genieOrgEdit)
        m_genieOrgEdit->setText(settings.value(kGenieOrgSetting).toString());
    if (m_genieWorkflowCombo) {
        const int idx = m_genieWorkflowCombo->findData(
            settings.value(kGenieWorkflowSetting, QStringLiteral("pr")).toString());
        if (idx >= 0)
            m_genieWorkflowCombo->setCurrentIndex(idx);
    }
    if (m_genieStatusLabel)
        m_genieStatusLabel->setText(
            genieToken.isEmpty()
                ? QStringLiteral("Genie: not configured \xe2\x80\x94 the prompt "
                                 "bar's genie button will send you back here.")
                : QStringLiteral("Genie: ready against %1.")
                      .arg(genieMcpUrl().toString()));
}

void MainWindow::generateMcpConnector()
{
    forkmesh::mcp::Connector connector;
    connector.token = forkmesh::mcp::generateToken();
    connector.node = m_profileIdentity.publicKey();
    connector.label = machineNodeName();
    connector.createdMs = QDateTime::currentMSecsSinceEpoch();

    QString error;
    if (!forkmesh::mcp::saveConnector(mcpAppDataDir(), connector, &error)) {
        flashMessage(error, true);
        return;
    }
    refreshMcpConnectorTab();
    // The field itself only ever shows a masked token, so put the real string
    // on the clipboard once, right when it is minted.
    QApplication::clipboard()->setText(connector.token);
    flashMessage("Connector token generated and copied to the clipboard. "
                 "Paste the configuration below into your agent.",
                 false);
}

void MainWindow::revokeMcpConnector()
{
    QString error;
    if (!forkmesh::mcp::revokeConnector(mcpAppDataDir(), &error)) {
        flashMessage(error, true);
        return;
    }
    refreshMcpConnectorTab();
    flashMessage("Connector revoked. Agents holding the old token are now "
                 "read-only.",
                 false);
}

void MainWindow::testMcpConnector()
{
    if (!m_mcpTestLabel || !m_mcpTestButton)
        return;
    if (m_mcpTestProcess)
        return; // a probe is already running

    const QString script = mcpServerScriptPath();
    if (script.isEmpty()) {
        m_mcpTestLabel->setText(
            "Could not find forkmesh_mcp_server.py in this installation.");
        m_mcpTestLabel->show();
        return;
    }

    m_mcpTestButton->setEnabled(false);
    m_mcpTestLabel->setText(QString::fromUtf8("Starting the server\xe2\x80\xa6"));
    m_mcpTestLabel->show();

    auto *proc = new QProcess(this);
    m_mcpTestProcess = proc;
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    const QString reposDir = repositoryMirrorRoot();
    if (!reposDir.isEmpty())
        env.insert(QStringLiteral("FORKMESH_REPOS_DIR"), reposDir);
    const QString token = forkmesh::mcp::loadConnector(mcpAppDataDir()).token;
    if (!token.isEmpty())
        env.insert(QStringLiteral("FORKMESH_MCP_TOKEN"), token);
    proc->setProcessEnvironment(env);

    // Drive the same stdio handshake an agent performs: initialize, then ask
    // for the tool list. Anything less would be testing a different code path
    // than the one that actually has to work.
    connect(proc, &QProcess::finished, this,
            [this, proc](int, QProcess::ExitStatus) {
                proc->deleteLater();
                m_mcpTestProcess = nullptr;
                if (m_mcpTestButton)
                    m_mcpTestButton->setEnabled(true);
                if (!m_mcpTestLabel)
                    return;
                QStringList tools;
                bool writable = false;
                const QList<QByteArray> lines =
                    proc->readAllStandardOutput().split('\n');
                for (const QByteArray &line : lines) {
                    const QJsonObject result =
                        QJsonDocument::fromJson(line).object()
                            .value(QStringLiteral("result")).toObject();
                    const QJsonArray listed =
                        result.value(QStringLiteral("tools")).toArray();
                    for (const QJsonValue &tool : listed)
                        tools << tool.toObject()
                                     .value(QStringLiteral("name")).toString();
                    writable = writable ||
                               result.value(QStringLiteral("instructions"))
                                   .toString()
                                   .contains(QStringLiteral("may write"));
                }
                if (tools.isEmpty()) {
                    const QString err =
                        QString::fromUtf8(proc->readAllStandardError()).trimmed();
                    m_mcpTestLabel->setText(
                        QStringLiteral("The server did not answer. %1")
                            .arg(err.isEmpty()
                                     ? QStringLiteral(
                                           "Check that python3 and the "
                                           "'cryptography' package are installed.")
                                     : err.section(QLatin1Char('\n'), -1)));
                    return;
                }
                m_mcpTestLabel->setText(
                    QStringLiteral("Connected. %1 tools available (%2); this "
                                   "connector may %3.")
                        .arg(tools.size())
                        .arg(tools.join(QStringLiteral(", ")),
                             writable ? QStringLiteral("read and write")
                                      : QStringLiteral("read only")));
            });
    connect(proc, &QProcess::errorOccurred, this, [this, proc] {
        if (m_mcpTestProcess != proc)
            return;
        proc->deleteLater();
        m_mcpTestProcess = nullptr;
        if (m_mcpTestButton)
            m_mcpTestButton->setEnabled(true);
        if (m_mcpTestLabel)
            m_mcpTestLabel->setText(
                "Could not launch python3. Install Python 3 and the "
                "'cryptography' package, then try again.");
    });

    proc->start(pythonCommand(), {script});
    const QByteArray handshake =
        QByteArrayLiteral("{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":"
                          "\"initialize\",\"params\":{\"protocolVersion\":"
                          "\"2025-06-18\"}}\n"
                          "{\"jsonrpc\":\"2.0\",\"id\":2,\"method\":"
                          "\"tools/list\"}\n");
    proc->write(handshake);
    proc->closeWriteChannel(); // the server exits when stdin closes

    // Closing stdin is what ends the server, but a wedged interpreter would
    // otherwise leave the button disabled forever.
    QTimer::singleShot(15000, proc, [proc] {
        if (proc->state() != QProcess::NotRunning)
            proc->kill();
    });
}

// --- Genie (adhoc #42) -----------------------------------------------------
//
// The website's Organization Admin page can mint a revocable, task-only bearer
// credential for the relay's own Streamable HTTP MCP server (<origin>/mcp),
// which exposes list_org_tasks / get_org_task / complete_org_task. Pasting that
// setup into an agent by hand is exactly the friction the "genie" button
// removes: press it and this app starts a normal tracked agent session whose
// opening prompt is that same setup block, so the agent picks its own work off
// the shared task list instead of running something typed here.

QUrl MainWindow::genieMcpUrl() const
{
    const QString configured =
        QSettings().value(kGenieUrlSetting).toString().trimmed();
    if (!configured.isEmpty()) {
        const QUrl url(configured);
        if (url.isValid() && !url.host().isEmpty())
            return url;
    }
    // Default to the active relay: the credential the website mints is scoped to
    // that same origin, so pointing anywhere else could only fail to authorize.
    QUrl url = catalogApiUrl();
    url.setPath(QStringLiteral("/mcp"));
    url.setQuery(QString());
    url.setFragment(QString());
    return url;
}

// The opening prompt for a genie run: the MCP server block the website
// generates, then the workflow the agent follows. Deliberately close to
// orgRemoteMcpPrompt() in cloudflare_worker/public/dashboard/js/04-account.js —
// the difference is that ForkMesh has already prepared an isolated worktree and
// branch for this run, so the agent is told to work in it rather than to build
// its own, and it is asked to announce the task it picked so the desktop can
// retitle the session live.
QString MainWindow::genieSetupPrompt(const QString &extraInstruction) const
{
    const QSettings settings;
    const QString token = settings.value(kGenieTokenSetting).toString().trimmed();
    const QString org = settings.value(kGenieOrgSetting).toString().trimmed();
    const bool deploy =
        settings.value(kGenieWorkflowSetting, QStringLiteral("pr")).toString() ==
        QLatin1String("deploy");

    const QJsonObject config{
        {QStringLiteral("mcpServers"),
         QJsonObject{
             {QStringLiteral("forkmesh"),
              QJsonObject{
                  {QStringLiteral("type"), QStringLiteral("http")},
                  {QStringLiteral("url"), genieMcpUrl().toString()},
                  {QStringLiteral("headers"),
                   QJsonObject{{QStringLiteral("Authorization"),
                                QStringLiteral("Bearer ") + token}}},
              }},
         }},
    };

    QStringList lines;
    lines << QStringLiteral(
        "Configure the following remote Streamable HTTP MCP server named "
        "forkmesh, then immediately use it for this assignment:")
          << QString()
          << QString::fromUtf8(QJsonDocument(config).toJson(QJsonDocument::Indented))
          << QStringLiteral("Work the shared task list for the %1 organization.")
                 .arg(org.isEmpty() ? QStringLiteral("connected") : org)
          << QStringLiteral(
                 "1. Call list_org_tasks. Select the highest-priority unfinished "
                 "task routed to an agent or explicitly unassigned. Never take a "
                 "task assigned to a person.")
          << QStringLiteral(
                 "2. The moment you have chosen, print one line of its own in "
                 "exactly this form, before doing anything else:\n"
                 "   FORKMESH_TASK <task id>: <task title>\n"
                 "   ForkMesh reads that line and shows the title live beside "
                 "this run, so print it again if you ever switch tasks.")
          << QStringLiteral(
                 "3. Call get_org_task for that id and read its repository "
                 "instructions before planning.")
          << QStringLiteral(
                 "4. ForkMesh has already prepared the isolated worktree and "
                 "branch you are running in. Work only here: never stash, reset, "
                 "clean, or commit in any other checkout, and preserve unrelated "
                 "changes.")
          << QStringLiteral(
                 "5. Implement the smallest complete fix, then run focused tests "
                 "plus the repository's required checks.")
          << QStringLiteral(
                 "6. Commit only the task's files with a clear message. Never "
                 "force-push or bypass failing checks or conflicts.");
    if (deploy) {
        lines << QStringLiteral(
            "7. Integrate the latest main into this branch without "
            "force-pushing, merge only when the merge and required checks are "
            "clean, then deploy with the repository's documented deploy command "
            "and verify the live revision.");
        lines << QStringLiteral(
            "8. Call complete_org_task with the commit, deployed revision, the "
            "tests you ran, and the exact QA steps.");
    } else {
        lines << QStringLiteral(
            "7. ForkMesh opens the pull request from this branch when the run "
            "finishes. Do not merge and do not deploy.");
        lines << QStringLiteral(
            "8. Call complete_org_task with the branch, the tests you ran, and "
            "the exact QA steps.");
    }
    lines << QStringLiteral(
        "If credentials, authorization, infrastructure, tests, or conflict "
        "resolution block safe completion, report the blocker and do not mark "
        "the task complete. Treat the bearer credential above as a secret: never "
        "print it in logs, commits, pull requests, task notes, or chat.");
    const QString extra = extraInstruction.trimmed();
    if (!extra.isEmpty())
        lines << QString()
              << QStringLiteral("Additional instruction from the operator:")
              << extra;
    return lines.join(QLatin1Char('\n'));
}

void MainWindow::startGenieAgent()
{
    const QString token =
        QSettings().value(kGenieTokenSetting).toString().trimmed();
    if (token.isEmpty()) {
        flashMessage(
            QString::fromUtf8(
                "Genie needs the remote MCP credential from the website "
                "\xE2\x80\x94 generate it in Organization Admin, then paste it "
                "into Settings \xE2\x86\x92 MCP."),
            true);
        showSection(1); // Settings
        return;
    }
    const int repoIndex = issuesRepoIndex();
    if (repoIndex < 0) {
        flashMessage("Open a repository first \xE2\x80\x94 genie runs in its "
                     "worktree.",
                     true);
        return;
    }
    // Anything typed in the quick-add box is guidance for the genie run, not the
    // task itself: the task comes from the shared list. Keep it in the history
    // like every other send, then clear the box so the next prompt starts fresh.
    QString typed;
    if (m_issueQuickAdd) {
        typed = m_issueQuickAdd->toPlainText().trimmed();
        if (!typed.isEmpty())
            recordQuickAddHistory(typed);
    }
    // Genie is a Claude Code run by construction: the remote server is
    // configured from the prompt, which needs an agent that can add an MCP
    // server to itself. The API-key providers have no such tool.
    //
    // The prompt embeds the bearer credential, so it is persisted with the
    // session (a resume has to replay it) exactly like the other agent
    // credentials this app stores locally — but it is registered in
    // localProviderCredentialValues() so it is redacted out of the transcript,
    // the run log, and the owner-sealed snapshot pushed to the relay.
    const int sessionId = startAdHocAgentForRepo(
        repoIndex, genieSetupPrompt(typed), QStringLiteral("claude-code"),
        /*createPr=*/true,
        QSettings().value(kClaudeCodeModelSetting).toString(), /*genie=*/true);
    if (sessionId <= 0)
        return;
    if (m_issueQuickAdd)
        m_issueQuickAdd->clear();
    clearQuickAddImages();
    setIssueInlineNotice(
        QString::fromUtf8("Genie is picking its own task off the shared list "
                          "\xE2\x80\x94 the title updates here once it has "
                          "chosen."));
}

// Watch a running agent's assistant text for the "FORKMESH_TASK <id>: <title>"
// line a genie run prints when it takes a task, and retitle the live session
// with it so the agents list, its dot and the transcript header all say what
// the agent is actually working on rather than the opening prompt's first line.
void MainWindow::applyGenieTaskTitle(int sessionId, const QString &assistantText)
{
    if (sessionId <= 0 || !m_agentStore ||
        !assistantText.contains(QLatin1String("FORKMESH_TASK")))
        return;
    const QRegularExpressionMatch match = genieTaskLineRe().match(assistantText);
    if (!match.hasMatch())
        return;
    QString title = match.captured(2).trimmed();
    // The CLI often wraps the line in markdown; strip the decoration so the row
    // shows the plain title.
    while (!title.isEmpty() &&
           (title.endsWith(QLatin1Char('*')) || title.endsWith(QLatin1Char('`')) ||
            title.endsWith(QLatin1Char('_'))))
        title.chop(1);
    title = title.trimmed();
    if (title.isEmpty())
        return;
    if (title.size() > 80)
        title = title.left(77) + QString::fromUtf8("\xE2\x80\xA6");

    {
        // Scoped: reloadAgents() below rebuilds m_agentSessions, so the pointer
        // must not outlive this block (the git-pump UAF family, adhoc #106).
        AgentSession *session = findAgentSession(sessionId);
        if (!session || session->issueTitle == title)
            return;
        session->issueTitle = title;
        m_agentStore->saveSession(*session);
    }
    if (m_streamSessionInfo.contains(sessionId)) {
        AgentSession info = m_streamSessionInfo.value(sessionId);
        info.issueTitle = title;
        m_streamSessionInfo[sessionId] = info;
    }
    logSystem(QStringLiteral("Genie: session #%1 is working \"%2\" (task %3).")
                  .arg(sessionId)
                  .arg(title, match.captured(1)));
    reloadAgents();
    scheduleAgentSessionsPush(); // the website's session list shows it too
}

void MainWindow::saveGenieSettings()
{
    if (!m_genieTokenEdit || !m_genieOrgEdit || !m_genieWorkflowCombo)
        return;
    QSettings settings;
    const QString token = m_genieTokenEdit->text().trimmed();
    settings.setValue(kGenieTokenSetting, token);
    settings.setValue(kGenieOrgSetting, m_genieOrgEdit->text().trimmed());
    settings.setValue(kGenieWorkflowSetting,
                      m_genieWorkflowCombo->currentData().toString());
    refreshMcpConnectorTab();
    flashMessage(token.isEmpty()
                     ? QStringLiteral("Genie credential cleared.")
                     : QStringLiteral("Genie is set up. The \"genie\" button in "
                                      "the prompt bar now starts a run."),
                 false);
}
