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
