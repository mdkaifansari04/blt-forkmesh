#!/usr/bin/env python3
"""A chat message can be turned into a repository issue everywhere chat runs.

A bug reported in chat used to have to be retyped on the issues page. Every
chat surface now carries a per-message control that files it instead:

* the full chat page (public/chat.js);
* the dashboard chat and the World's in-world chat, which share
  public/dashboard-chat.js;
* the desktop client's message "..." menu (MessageRow -> MainWindow).

The three web surfaces share public/chat-issue-filing.js, because the signed
"open" event they post has to stay in lockstep with the dashboard's own
new-issue form (submitWebIssue in dashboard/js/03-issues-profile-io.js) and with
verify_issue_event in the worker: same author key, same content preimage, same
canonical string, or the relay rejects the submission.
"""

from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
PUBLIC = ROOT / "cloudflare_worker" / "public"
FILING = (PUBLIC / "chat-issue-filing.js").read_text(encoding="utf-8")
CHAT = (PUBLIC / "chat.js").read_text(encoding="utf-8")
DASHBOARD_CHAT = (PUBLIC / "dashboard-chat.js").read_text(encoding="utf-8")
DASHBOARD_ISSUES = (
    PUBLIC / "dashboard" / "js" / "03-issues-profile-io.js"
).read_text(encoding="utf-8")
WORLD_CSS = (PUBLIC / "world" / "world.css").read_text(encoding="utf-8")
CHAT_CSS = (PUBLIC / "chat.css").read_text(encoding="utf-8")

QT_SRC = ROOT / "qt_client" / "src"
MESSAGE_ROW_HEADER = (QT_SRC / "MessageRow.h").read_text(encoding="utf-8")
MESSAGE_ROW = (QT_SRC / "MessageRow.cpp").read_text(encoding="utf-8")
QT_MESSAGES = (QT_SRC / "MainWindowMessages.cpp").read_text(encoding="utf-8")
QT_ISSUES = (QT_SRC / "MainWindowIssues.cpp").read_text(encoding="utf-8")
QT_HEADER = (QT_SRC / "MainWindow.h").read_text(encoding="utf-8")


def _region(source, start, end):
    i = source.index(start)
    return source[i:source.index(end, i)]


def test_filing_module_signs_the_same_open_event_as_the_dashboard_form():



    for contract in (
        'export const WEB_ISSUE_KEY_STORAGE = "forkmesh.webIssueKey";',
        '`forkmesh-issue-event-v1\\nopen\\n0\\n${pub}\\n${ts}\\n${contentHash}`',
        'sha256HexLower(`${title}${NUL}${cleanBody}${NUL}`)',
        '{ name: "Ed25519" }, privateKey, encoder.encode(canonical)',
        '"open-web-" + ts',
        "number: 0,",
        "titleIfNew: title,",
    ):
        assert contract in FILING

    signer = _region(
        DASHBOARD_ISSUES, "async function submitWebIssue", "window.ForkMeshDashboardActions")
    assert 'forkmesh-issue-event-v1\\nopen\\n0\\n${pub}\\n${ts}\\n${contentHash}' in signer
    assert 'const content = title + NUL + cleanBody + NUL;' in signer
    assert 'WEB_ISSUE_KEY_STORAGE' in DASHBOARD_ISSUES


def test_the_message_seeds_the_title_and_the_body_keeps_the_attribution():
    assert "export function issueTitleFromMessage(text)" in FILING
    title = _region(
        FILING, "export function issueTitleFromMessage", "// Attribution matters")

    assert 'String(text || "").split(/\\r?\\n/)[0]' in title
    assert "first.length > 200" in title
    body = _region(
        FILING, "export function issueBodyFromMessage", "let repositoriesPromise")

    assert "chat message by ${String(who || \"someone\")} at ${when}." in body


def test_the_full_chat_page_files_from_any_message():
    assert 'from "./chat-issue-filing.js"' in CHAT
    actions = _region(CHAT, "function buildRow(record, prev)", "function renderActiveChannel")
    assert '"Create an issue from this message",' in actions
    assert "beginIssueFromMessage(record, button)" in actions

    assert "if (!record.deleted && record.text) {" in actions
    form = _region(CHAT, "function beginIssueFromMessage(", "function buildDateDivider")
    assert "issueTitleFromMessage(record.text)" in form
    assert "loadIssueRepositories()" in form
    assert "issueBodyFromMessage({" in form
    assert "await fileWebIssue(" in form
    for marker in (".chat-issue-form", ".chat-issue-repo", ".chat-issue-status"):
        assert marker in CHAT_CSS


def test_the_dashboard_and_world_chats_file_from_any_message():
    actions = _region(
        DASHBOARD_CHAT, "function buildMessageActions(", "let activeReactionPicker")
    assert 'messageActionButton("Issue", () => void beginIssueFromMessage(record)' in actions
    assert "if (record.text) {" in actions
    form = _region(
        DASHBOARD_CHAT,
        "async function beginIssueFromMessage(",
        "// The rail's mini chat")
    assert "filing.issueTitleFromMessage(record.text)" in form
    assert "filing.issueBodyFromMessage({" in form
    assert "await fileWebIssue(" in form


    assert "for (const option of fullRepository?.options || []) {" in form


def test_the_world_bundle_loads_the_signer_on_demand():


    loader = _region(
        DASHBOARD_CHAT, "let issueFilingPromise = null;", "async function ed25519Verify")
    assert 'import("/chat-issue-filing.js")' in loader


    assert "window.ForkMeshDashboardActions?.submitWebIssue" in loader
    assert ".world-native-chat .chat-issue-form {" in WORLD_CSS


def test_the_desktop_message_menu_files_an_issue_too():
    assert "void createIssueRequested(const QString &text, const QString &senderName," \
        in MESSAGE_ROW_HEADER
    menu = _region(MESSAGE_ROW, "const bool showCopy =", "menuButton->setMenu(menu);")
    assert "const bool showCreateIssue = !message.text.isEmpty();" in menu
    assert 'menu->addAction("Create issue\\xE2\\x80\\xA6")' in menu
    assert "emit createIssueRequested(m_message.text," in menu
    assert "connect(row, &MessageRow::createIssueRequested, this," in QT_MESSAGES
    assert "&MainWindow::promptIssueFromChatMessage);" in QT_MESSAGES


def test_the_desktop_prefills_the_normal_compose_page():

    assert "void composeNewIssue(const QString &prefillTitle, const QString &prefillBody);" \
        in QT_HEADER
    assert "void promptIssueFromChatMessage(const QString &text, const QString &senderName," \
        in QT_HEADER
    assert "composeNewIssue(QString(), QString());" in QT_ISSUES
    assert "titleEdit->setText(prefillTitle);" in QT_ISSUES
    assert "bodyEdit->setMarkdown(prefillBody);" in QT_ISSUES
    handler = _region(
        QT_ISSUES,
        "void MainWindow::promptIssueFromChatMessage(",
        "void MainWindow::composeNewIssue(")

    assert "QInputDialog::getItem(" in handler
    assert "if (m_repositories.size() > 1) {" in handler
    assert "openRepoDetail(repoIndex);" in handler
    assert "composeNewIssue(title, body);" in handler

    assert '"chat message by "' in handler
