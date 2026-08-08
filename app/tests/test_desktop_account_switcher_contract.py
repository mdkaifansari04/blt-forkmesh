from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
HEADER = (ROOT / "desktop/src/MainWindow.h").read_text(encoding="utf-8")
CHAT = (ROOT / "desktop/src/MainWindowChat.cpp").read_text(encoding="utf-8")
REPOS = (ROOT / "desktop/src/MainWindowRepos.cpp").read_text(encoding="utf-8")
SETUP = (ROOT / "desktop/src/MainWindowSetup.cpp").read_text(encoding="utf-8")


def test_avatar_opens_account_and_instance_management():
    assert 'QStringLiteral("accountSwitcherButton")' in CHAT
    assert "&MainWindow::showUserAccountMenu" in CHAT
    assert 'QStringLiteral("Manage account")' in REPOS
    assert 'QStringLiteral("Manage relay instances")' in REPOS
    assert 'QStringLiteral("Manage mirror servers")' in REPOS
    assert "showNetworkTab(kNetworkRelaysTab)" in REPOS
    assert "showNetworkTab(kNetworkHostsTab)" in REPOS


def test_recent_users_are_bounded_and_switch_through_real_login():
    assert "QStringList MainWindow::recentUserAccounts() const" in REPOS
    assert "while (accounts.size() > 8)" in REPOS
    assert 'QStringLiteral("Switch to @%1 on %2…")' in REPOS
    assert "restoreCachedAccountSession(account, targetServer)" in REPOS
    assert "if (!restored && !runLoginFlow(account))" in REPOS
    assert "cacheActiveAccountSession(previousServer)" in REPOS
    assert "rememberUserAccount(m_accountName);" in SETUP


def test_recent_users_remember_and_restore_their_instance():
    assert "kRecentUserInstancesSetting" in REPOS
    assert "saveRecentUserInstance(account, currentAccountInstanceUrl())" in REPOS
    assert "saveRecentUserInstance(m_accountName, targetServer)" in REPOS
    assert "targetServer = recentUserInstance(account)" in REPOS
    assert "switchToServer(targetServerIndex)" in REPOS
    assert 'QStringLiteral("Instance or relay")' in REPOS
    assert 'QStringLiteral("Sign in on another instance…")' in REPOS


def test_remote_instance_login_keeps_owner_signing_fail_closed():
    assert "AccountCapability::allowsPasswordOnlyFallback(err)" in SETUP
    assert "activateAccountSession(webResp, accountName, false," in SETUP
    assert "owner-signed actions remain disabled" in REPOS
    assert 'payload.value(QStringLiteral("isAdmin")).toBool(false)' in SETUP
    assert "m_profileLinkedNodes.clear()" in SETUP
    assert "void showUserAccountMenu();" in HEADER
