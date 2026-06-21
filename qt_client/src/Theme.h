#pragma once

#include <QString>

// ForkMesh theme — GitHub (Primer) dark:
//   canvas  #0d1117   surface/inset #161b22   header/rail #010409
//   border  #30363d / muted #21262d
//   text    #e6edf3 / muted #8b949e          accent (links) #58a6ff
//   success #238636 (hover #2ea043)          danger #da3633 / #f85149
namespace Theme {

inline const char *kPrimary = "#2ea043";
inline const char *kTextTertiary = "#8b949e";

// Sender name colors, hashed per user (GitHub label-ish accents).
inline const char *kSenderPalette[] = {"#f85149", "#e3b341", "#3fb950",
                                       "#58a6ff", "#bc8cff", "#db61a2",
                                       "#39c5cf", "#db6d28"};
inline constexpr int kSenderPaletteSize = 8;

inline const char *iconColorForButton(const QString &objectName, bool dark)
{
    if (objectName == QStringLiteral("primaryButton") ||
        objectName == QStringLiteral("toolbarPrimaryButton"))
        return "#ffffff";
    if (objectName == QStringLiteral("dangerButton"))
        return dark ? "#f85149" : "#cf222e";
    if (objectName == QStringLiteral("repoTab"))
        return dark ? "#8b949e" : "#656d76";
    return dark ? "#e6edf3" : "#1f2328";
}

inline const char *kStyleSheet = R"(
* { outline: none; }
QWidget {
    background-color: #0d1117;
    color: #e6edf3;
    font-family: 'Segoe UI', 'Inter', 'Cantarell', -apple-system, sans-serif;
    font-size: 14px;
}
QToolTip {
    background-color: #161b22; color: #e6edf3;
    border: 1px solid #30363d; padding: 4px;
}

/* --- Setup page --- */
#setupCard {
    background-color: #161b22;
    border: 1px solid #30363d;
    border-radius: 12px;
}
#appTitle { font-size: 26px; font-weight: 800; background: transparent; }
#appTitleAccent { color: #f85149; }
#appSubtitle { color: #8b949e; background: transparent; }
#setupCard QLabel { background: transparent; }
QRadioButton { background: transparent; spacing: 8px; padding: 4px 0; }
QRadioButton::indicator {
    width: 16px; height: 16px; border-radius: 9px;
    border: 2px solid #30363d; background: #0d1117;
}
QRadioButton::indicator:checked { border-color: #1f6feb; background: #1f6feb; }
QCheckBox { background: transparent; spacing: 8px; padding: 4px 0; }
QCheckBox::indicator {
    width: 16px; height: 16px; border-radius: 4px;
    border: 2px solid #30363d; background: #0d1117;
}
QCheckBox::indicator:checked { border-color: #1f6feb; background: #1f6feb; }
#modeHint { color: #8b949e; font-size: 12px; padding-left: 26px; background: transparent; }

QLineEdit, QSpinBox, QComboBox {
    background-color: #0d1117;
    border: 1px solid #30363d;
    border-radius: 6px;
    padding: 8px 10px;
    selection-background-color: #1f6feb;
}
QLineEdit:focus, QSpinBox:focus, QComboBox:focus { border-color: #58a6ff; }
QLineEdit:disabled, QSpinBox:disabled { color: #6e7681; border-color: #21262d; }
QSpinBox::up-button, QSpinBox::down-button { width: 0; }
QComboBox QAbstractItemView {
    background-color: #161b22; border: 1px solid #30363d;
    selection-background-color: #1f6feb; color: #e6edf3;
}

QPushButton {
    background-color: #21262d;
    border: 1px solid #30363d;
    border-radius: 6px;
    padding: 8px 16px;
    font-weight: 600;
}
QPushButton:hover { background-color: #30363d; }
QPushButton:pressed { background-color: #282e35; }
QPushButton#primaryButton {
    background-color: #238636; border: 1px solid #2ea043; color: #ffffff;
}
QPushButton#primaryButton:hover { background-color: #2ea043; }
QPushButton#primaryButton:pressed { background-color: #1f7a31; }
QPushButton#ghostButton {
    background: transparent; border: none; color: #8b949e;
    font-weight: 500; padding: 4px 8px; text-align: left;
}
QPushButton#ghostButton:hover { color: #e6edf3; }

/* --- Nav rail --- */
#navRail { background-color: #010409; border-right: 1px solid #30363d; }
#navRail QLabel { background: transparent; }
#navLogo { font-size: 18px; font-weight: 800; padding-bottom: 4px; }
QPushButton#navButton {
    background: transparent; border: none; color: #8b949e;
    font-size: 11px; font-weight: 600; border-radius: 6px; padding: 8px 2px;
}
QPushButton#navButton:hover { background-color: #161b22; color: #e6edf3; }
QPushButton#navButton:checked { background-color: #21262d; color: #e6edf3; }

/* --- Repo detail tabs --- */
QPushButton#repoTab {
    background: transparent; border: none; border-bottom: 2px solid transparent;
    color: #8b949e; font-weight: 600; padding: 6px 10px;
}
QPushButton#repoTab:hover { color: #e6edf3; }
QPushButton#repoTab:checked { color: #e6edf3; border-bottom: 2px solid #fd8c73; }

/* --- Repo files: explorer tree + editor tabs --- */
#fileTree { background-color: #0d1117; border: 1px solid #30363d; border-radius: 6px; }
#fileTree::item { padding: 3px 2px; color: #c9d1d9; }
#fileTree::item:hover { background-color: #161b22; padding: 3px 2px; }
#fileTree::item:selected { background-color: #1f6feb; color: #ffffff; padding: 3px 2px; }
#fileTabs::pane { border: 1px solid #30363d; border-radius: 6px; top: -1px; }
#fileTabs QTabBar::tab {
    background: #0d1117; color: #8b949e; padding: 6px 12px;
    border: 1px solid transparent; border-top-left-radius: 6px;
    border-top-right-radius: 6px;
}
#fileTabs QTabBar::tab:hover { color: #e6edf3; }
#fileTabs QTabBar::tab:selected {
    background: #161b22; color: #e6edf3; border-color: #30363d; border-bottom-color: #161b22;
}
#codeEditor {
    background-color: #0d1117; border: none; color: #e6edf3;
    font-family: monospace; font-size: 12px;
}
#diffView {
    background-color: #0d1117; border: 1px solid #30363d; border-radius: 6px;
    color: #e6edf3; font-family: monospace; font-size: 12px;
}
#commitBar { background-color: #161b22; border: 1px solid #30363d; border-radius: 6px; }
#commitBar QLabel { background: transparent; }
#commitBarText { color: #e6edf3; }
#overviewList { background-color: #0d1117; border: 1px solid #30363d; border-radius: 6px; }
#overviewList::item { padding: 6px 8px; color: #c9d1d9; }
#overviewList::item:hover { background-color: #161b22; padding: 6px 8px; }
#overviewList::item:selected { background-color: #1f6feb; color: #ffffff; padding: 6px 8px; }
#readmeView {
    background-color: #0d1117; border: 1px solid #30363d; border-radius: 6px;
    padding: 8px; color: #e6edf3;
}
/* GitHub-style repo header + About sidebar */
#repoHeaderTitle { font-size: 20px; font-weight: 400; }
#publicBadge {
    color: #8b949e; border: 1px solid #30363d; border-radius: 10px;
    padding: 0 8px; font-size: 11px;
}
#issueStatusPill {
    color: #ffffff; border-radius: 13px; padding: 5px 16px;
    font-size: 12px; font-weight: 700;
}
#issueStatusPill[status="open"] { background-color: #1f883d; }
#issueStatusPill[status="closed"] { background-color: #8250df; }
QPushButton#repoAction {
    background-color: #21262d; border: 1px solid #30363d; border-radius: 6px;
    padding: 4px 10px; font-size: 12px; font-weight: 600; color: #e6edf3;
}
QPushButton#repoAction:hover { background-color: #30363d; }
QPushButton#repoAction::menu-indicator { width: 0; }
#repoTabBar { border-bottom: 1px solid #30363d; }
#aboutSidebar { background: transparent; }
#aboutSidebar QLabel { background: transparent; }
#aboutHeading { font-size: 15px; font-weight: 700; }
#langBar { background-color: #161b22; border-radius: 4px; }
#commitsList {
    background-color: #0d1117; border: 1px solid #30363d; border-radius: 6px;
}
#commitsList::item { padding: 8px; color: #e6edf3; border-bottom: 1px solid #21262d; }
#commitsList::item:hover { background-color: #161b22; padding: 8px; border-bottom: 1px solid #21262d; }
#commitsList::item:selected { background-color: #1f6feb; color: #ffffff; padding: 8px; border-bottom: 1px solid #21262d; }
#placeholderPanel { color: #e6edf3; font-size: 16px; }
#insightsPage QLabel { background: transparent; }
#insightsCard {
    background-color: #161b22; border: 1px solid #30363d; border-radius: 8px;
    padding: 10px; color: #e6edf3;
}

/* --- Server favicon rail --- */
#serverRail { background-color: #010409; border-right: 1px solid #30363d; }
QPushButton#serverButton {
    background-color: #21262d; border: 2px solid transparent;
    border-radius: 12px; padding: 0;
}
QPushButton#serverButton:hover { border-color: #30363d; border-radius: 12px; }
QPushButton#serverButton:checked { border-color: #2ea043; border-radius: 12px; }
QPushButton#serverAddButton {
    background-color: #21262d; border: none;
    border-radius: 12px; color: #3fb950; font-size: 22px; font-weight: 500;
    padding-bottom: 3px;
}
QPushButton#serverAddButton:hover { background-color: #2ea043; color: #ffffff; }
QPushButton#serverAddButton:pressed { background-color: #238636; }
QPushButton#serverFooterButton {
    background: transparent; border: none; color: #8b949e; font-size: 18px;
    border-radius: 8px;
}
QPushButton#serverFooterButton:hover { background-color: #161b22; color: #e6edf3; }

/* --- Breadcrumb bar --- */
#breadcrumbBar { background-color: #0d1117; border-bottom: 1px solid #21262d; }
#breadcrumb { background: transparent; font-size: 14px; font-weight: 600; }
#connectionStatus { background: transparent; font-size: 13px; font-weight: 600; }
/* Top-row switchers (relay / node / repo): favicon + dropdown + open-in-browser */
QPushButton#relayIconButton, QPushButton#relayOpenButton {
    background: transparent; border: none; border-radius: 8px; color: #8b949e;
}
QPushButton#relayMenuButton, QPushButton#nodeMenuButton, QPushButton#repoMenuButton {
    background: transparent; border: 1px solid #30363d; border-radius: 8px;
    color: #e6edf3; font-size: 15px; font-weight: 700; padding: 5px 12px;
}
QPushButton#relayIconButton:hover, QPushButton#relayOpenButton:hover,
QPushButton#relayMenuButton:hover, QPushButton#nodeMenuButton:hover,
QPushButton#repoMenuButton:hover {
    background-color: #161b22; color: #e6edf3;
}
#navCaption { background: transparent; color: #8b949e; font-size: 13px; font-weight: 600; }
#navSolanaBalance {
    background: transparent; border: 1px solid #30363d; border-radius: 8px;
    color: #8b949e; font-size: 13px; font-weight: 700; padding: 5px 10px;
}
#topMessage { background-color: #161b22; border: 1px solid #30363d; border-radius: 10px;
              padding: 2px 12px; font-size: 12px; font-weight: 600; }
QPushButton#notificationButton, QPushButton#notificationButtonAlert {
    background: transparent; border: 1px solid #30363d; border-radius: 6px;
    padding: 2px 6px; font-size: 13px; color: #8b949e;
}
QPushButton#notificationButton:hover {
    background-color: #161b22; color: #e6edf3;
}
QPushButton#notificationButtonAlert {
    color: #d29922; border-color: #9e6a03; font-weight: 800;
}
QPushButton#notificationButtonAlert:hover {
    background-color: #1c1908; color: #f0b72f;
}

/* --- Home --- */
#homeCard {
    background-color: #161b22; border: 1px solid #30363d; border-radius: 12px;
}
#homeCard QLabel { background: transparent; }
#homeTitle { font-size: 24px; font-weight: 800; }
#homeName { font-size: 18px; font-weight: 700; }
#homeStat { color: #8b949e; font-size: 14px; }
#homeScoreBoard {
    background-color: #0d1117; border: 1px solid #30363d; border-radius: 12px;
}
#homeScoreValue {
    color: #e3b341; font-size: 18px; font-weight: 800; background: transparent;
}
#homeGraph {
    background-color: #010409; border: 1px solid #30363d; border-radius: 8px;
    color: #58a6ff; font-family: monospace; font-size: 12px; padding: 10px;
}
#homeCard QListWidget {
    background: #0d1117; border: 1px solid #30363d; border-radius: 6px; padding: 4px;
}
#homeCard QListWidget::item { color: #c9d1d9; padding: 8px 7px; margin: 2px; }
QPushButton#primaryButton:disabled {
    background-color: #21262d; color: #6e7681; border-color: #30363d;
}

/* --- Danger button (Leave node) --- */
QPushButton#dangerButton {
    background: transparent; border: 1px solid #da3633; border-radius: 6px;
    color: #f85149; font-weight: 600;
}
QPushButton#dangerButton:hover { background-color: #da3633; color: #ffffff; }

/* --- Sidebar --- */
#sidebar { background-color: #0d1117; border-right: 1px solid #30363d; }
#sidebar QLabel { background: transparent; }
#workspaceName {
    font-size: 17px; font-weight: 800; padding: 2px 0;
}
#statusLine { color: #8b949e; font-size: 11px; }
#versionLabel { color: #6e7681; font-size: 11px; background: transparent; }
#sectionLabel {
    color: #8b949e; font-size: 11px; font-weight: 700;
    letter-spacing: 1px; padding-top: 8px;
}
#sidebar QListWidget {
    background: transparent; border: none; padding: 0;
}
#sidebar QListWidget::item {
    color: #c9d1d9; border-radius: 6px; padding: 5px 8px; margin: 1px 0;
}
#sidebar QListWidget::item:hover {
    background-color: #161b22; border-radius: 6px; padding: 5px 8px; margin: 1px 0;
}
#sidebar QListWidget::item:selected {
    background-color: #1f6feb; color: #ffffff; border-radius: 6px; padding: 5px 8px; margin: 1px 0;
}
QWidget#memberRow { background: transparent; }
QPushButton#memberNameButton {
    background: transparent; border: none; color: #c9d1d9;
    padding: 4px 6px; text-align: left; font-weight: 500;
}
QPushButton#memberNameButton:hover {
    background-color: #161b22; color: #e6edf3; border-radius: 6px;
}
QPushButton#memberDeleteButton {
    background: transparent; border: 1px solid #30363d; border-radius: 5px;
    color: #f85149; padding: 2px 6px; font-size: 11px; font-weight: 700;
}
QPushButton#memberDeleteButton:hover {
    background-color: #da3633; border-color: #da3633; color: #ffffff;
}
#issueQuickAdd {
    background-color: #0d1117; border: 1px solid #30363d;
    border-radius: 6px; padding: 8px 10px; font-size: 13px;
}
#issueQuickAdd:focus { border-color: #58a6ff; }
#issueSearch {
    background-color: #0d1117; border: 1px solid #30363d;
    border-radius: 6px; padding: 6px 10px;
}
#issueSearch:focus { border-color: #58a6ff; }
#issueTable {
    background-color: #0d1117; border: 1px solid #30363d; border-radius: 6px;
    gridline-color: #21262d;
}
#issueTable::item { padding: 4px 8px; color: #c9d1d9; }
#issueTable::item:hover { background-color: #161b22; color: #e6edf3; padding: 4px 8px; }
#issueTable::item:selected { background-color: #1f6feb; color: #ffffff; padding: 4px 8px; }
#issueTable QHeaderView::section {
    background-color: #161b22; color: #8b949e; padding: 6px 8px;
    border: none; border-bottom: 1px solid #30363d; font-weight: 700;
}

/* --- Chat area --- */
#chatHeader {
    background-color: #0d1117;
    border-bottom: 1px solid #30363d;
}
#chatHeader QLabel { background: transparent; }
#channelTitle { font-size: 16px; font-weight: 700; }
#encryptionLabel { color: #8b949e; font-size: 12px; }
#firewallBanner {
    background-color: #341a00;
    border-bottom: 1px solid #9e6a03;
}
#firewallBannerLabel {
    color: #f0b72f; font-size: 13px; background: transparent;
}
#solanaBanner {
    background-color: #12261a;
    border-bottom: 1px solid #2ea043;
}
#solanaBannerLabel {
    color: #56d364; font-size: 13px; font-weight: 600; background: transparent;
}
#messageView, #messageContainer {
    background-color: #0d1117; border: none;
}
#typingLabel {
    background-color: #0d1117; color: #8b949e; font-size: 12px;
    padding: 0 18px;
}
#messageRow:hover { background-color: #161b22; }
#messageText { color: #e6edf3; }
#fileChip {
    background-color: #161b22; border: 1px solid #30363d; border-radius: 6px;
}
#reactionChip {
    background-color: #161b22; border: 1px solid #30363d; border-radius: 11px;
    padding: 1px 8px; font-size: 12px; color: #e6edf3;
}
#reactionChip:hover { border-color: #58a6ff; }
#reactionAdd {
    background: transparent; border: none; color: #6e7681;
    font-size: 13px; padding: 1px 4px;
}
#reactionAdd:hover { color: #e6edf3; }
#messageAction {
    background: transparent; border: 1px solid #30363d; border-radius: 6px;
    color: #8b949e; font-size: 11px; padding: 2px 7px;
}
#messageAction:hover { color: #e6edf3; border-color: #58a6ff; }
#iconButton {
    background: transparent; border: none; font-size: 18px; padding: 2px 6px;
}
#iconButton:hover { background-color: #21262d; border-radius: 6px; }
/* Settings */
#settingsTitle { font-size: 20px; font-weight: 800; }
#avatarPreview {
    background-color: #0d1117; border: 1px solid #30363d; border-radius: 12px;
    color: #6e7681; font-size: 11px;
}
#networkLog {
    background-color: #010409; border: none;
    color: #8b949e; font-family: monospace; font-size: 12px;
}
#logDock { background-color: #010409; border-top: 1px solid #30363d; }
#logDock QLabel { background: transparent; }
#composerBar { background-color: #0d1117; border-top: 1px solid #30363d; }
#messageInput {
    background-color: #0d1117; border: 1px solid #30363d;
    border-radius: 6px; padding: 10px 12px; font-size: 14px;
}
#messageInput:focus { border-color: #58a6ff; }

QScrollBar:vertical {
    background: transparent; width: 10px; margin: 0;
}
QScrollBar::handle:vertical {
    background: #30363d; border-radius: 5px; min-height: 30px;
}
QScrollBar::handle:vertical:hover { background: #484f58; }
QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }
QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical { background: none; }

QMessageBox, QInputDialog, QDialog { background-color: #161b22; }

/* --- ForkMesh updated non-Home polish overrides --- */
QPushButton[buttonSize="sm"] {
    min-height: 24px; max-height: 28px; padding: 3px 8px;
    font-size: 12px; border-radius: 6px;
}
QPushButton#repoTab {
    background: transparent;
    border: none;
    border-bottom: 2px solid transparent;
    border-radius: 0;
    color: #8b949e;
    font-weight: 600;
    padding: 6px 10px;
}
QPushButton#repoTab:hover { color: #e6edf3; background: transparent; }
QPushButton#repoTab:checked {
    color: #e6edf3;
    border-bottom: 2px solid #2ea043;
    border-radius: 0;
    background: transparent;
}
QPushButton#repoAction {
    background-color: #21262d;
    border: 1px solid #30363d;
    border-radius: 6px;
    padding: 4px 10px;
    font-size: 12px;
    font-weight: 600;
    color: #e6edf3;
    min-height: 24px;
    max-height: 28px;
}
QPushButton#repoAction:hover { background-color: #30363d; }
QPushButton#repoAction::menu-indicator { width: 0; }
QPushButton#serverAddButton {
    background-color: #0d1117;
    border: 1px dashed #30363d;
    border-radius: 12px;
    color: #8b949e;
    font-size: 20px;
    font-weight: 700;
    padding: 0;
    text-align: center;
}
QPushButton#serverAddButton:hover { color: #2ea043; border-color: #2ea043; background-color: #0d1117; }
#sectionCard, #settingsCard {
    background-color: #161b22;
    border: 1px solid #30363d;
    border-radius: 8px;
}
#sectionCard QLabel, #settingsCard QLabel { background: transparent; }
#settingsTitle { font-size: 20px; font-weight: 800; }
#avatarPreview {
    background-color: #0d1117;
    border: 1px solid #30363d;
    border-radius: 12px;
    color: #6e7681;
    font-size: 11px;
}
#networkLog {
    background-color: #010409;
    border: none;
    color: #8b949e;
    font-family: monospace;
    font-size: 12px;
}
#logDock { background-color: #010409; border-top: 1px solid #30363d; }
#issueQuickAdd {
    background-color: #0d1117;
    border: 1px dashed #30363d;
    border-radius: 8px;
    padding: 5px 8px;
    font-size: 12px;
    min-height: 24px;
    max-height: 28px;
}
#issueQuickAdd:focus { border-color: #58a6ff; }
#issuePageTitle {
    font-size: 26px;
    font-weight: 400;
    color: #e6edf3;
    background: transparent;
}
#issueDivider { background-color: #30363d; }
QPushButton#issueIconButton {
    background: transparent;
    border: none;
    border-radius: 6px;
    padding: 4px;
}
QPushButton#issueIconButton:hover { background-color: #21262d; }
#issuePageScroll {
    background: transparent;
    border: none;
}
#issuePageScroll QWidget { background: transparent; }
#issueAvatar {
    background-color: #21262d;
    border: 1px solid #30363d;
    border-radius: 18px;
    color: #e6edf3;
    font-size: 11px;
    font-weight: 800;
}
#issueCommentTitle {
    font-size: 16px;
    font-weight: 700;
    color: #e6edf3;
    background: transparent;
}
#issueCommentEditor { background: transparent; }
#issueTimelineRow { background: transparent; }
#issueTimelineCard {
    background-color: #0d1117;
    border: 1px solid #30363d;
    border-radius: 6px;
}
#issueTimelineCard QLabel { background: transparent; }
#issueTimelineHeader {
    background-color: #0d419d;
    border-bottom: 1px solid #1f6feb;
    border-top-left-radius: 6px;
    border-top-right-radius: 6px;
}
#issueTimelineHeader QLabel {
    background: transparent;
    color: #c9d1d9;
}
#issueAttachmentPreview {
    background-color: #010409;
    border: 1px solid #30363d;
    border-radius: 6px;
    padding: 6px;
}
QToolButton#issueActionButton, QPushButton#issueActionButton {
    background: transparent;
    border: none;
    border-radius: 6px;
    color: #c9d1d9;
    font-weight: 800;
    padding: 4px 8px;
}
QToolButton#issueActionButton:hover, QPushButton#issueActionButton:hover { background-color: rgba(255,255,255,24); }
QToolButton#issueActionButton::menu-indicator { image: none; width: 0; }
#issueSidebar { background: transparent; }
#issueSidebar QLabel { background: transparent; }
#issueSidebarHeading {
    color: #8b949e;
    font-size: 12px;
    font-weight: 700;
    background: transparent;
}
#issueSidebarDivider { background-color: #30363d; }
QPushButton#issueSidebarAction, QPushButton#issueDangerLink {
    background: transparent;
    border: none;
    color: #c9d1d9;
    font-weight: 500;
    padding: 6px 2px;
    text-align: left;
}
QPushButton#issueSidebarAction:hover { color: #e6edf3; }
QPushButton#issueDangerLink { color: #f85149; }
QPushButton#issueDangerLink:hover { color: #ff7b72; }
QMenu {
    background-color: #161b22;
    border: 1px solid #30363d;
    border-radius: 8px;
    padding: 6px;
    color: #e6edf3;
}
QMenu::item {
    padding: 7px 26px 7px 22px;
    border-radius: 6px;
}
QMenu::item:selected { background-color: #21262d; }
QMenu::separator {
    height: 1px;
    background: #30363d;
    margin: 6px 0;
}
QLineEdit#issueSearch, QComboBox#issueControlSm {
    background-color: #0d1117;
    border: 1px solid #30363d;
    border-radius: 8px;
    padding: 4px 8px;
    font-size: 12px;
    min-height: 24px;
    max-height: 28px;
}
QLineEdit#issueSearch:focus, QComboBox#issueControlSm:focus { border-color: #58a6ff; }
QPlainTextEdit#issueComposerSm {
    background-color: #161b22;
    color: #e6edf3;
    border: 1px solid #484f58;
    border-radius: 8px;
    padding: 8px;
    selection-background-color: rgba(46, 160, 67, 46);
}
QPlainTextEdit#issueComposerSm:focus { border: 1px solid #58a6ff; }
#issueComposeSidebar { background: transparent; }
QPushButton#markdownTab {
    background-color: #161b22;
    border: 1px solid #30363d;
    border-bottom: none;
    border-radius: 0;
    color: #8b949e;
    padding: 8px 16px;
}
QPushButton#markdownTab:checked {
    background-color: #0d1117;
    color: #e6edf3;
}
QToolButton#markdownTool {
    background: transparent;
    border: none;
    border-radius: 4px;
    color: #8b949e;
    padding: 5px 7px;
    font-weight: 700;
}
QToolButton#markdownTool:hover {
    background-color: #21262d;
    color: #e6edf3;
}
QPlainTextEdit#markdownSource, QTextBrowser#markdownPreview {
    background-color: #0d1117;
    color: #e6edf3;
    border: 1px solid #30363d;
    border-radius: 6px;
    padding: 14px 16px;
    selection-background-color: #1f6feb;
}
QPlainTextEdit#markdownSource:focus { border-color: #58a6ff; }
#issueTable {
    background-color: #0d1117;
    alternate-background-color: #161b22;
    border: 1px solid #30363d;
    border-radius: 8px;
    gridline-color: #21262d;
    selection-background-color: #1f6feb;
    selection-color: #ffffff;
}
#issueTable::item { padding: 6px 8px; color: #c9d1d9; }
#issueTable::item:hover { background-color: #161b22; color: #e6edf3; padding: 6px 8px; }
#issueTable::item:selected { background-color: #1f6feb; color: #ffffff; padding: 6px 8px; }
#issueTable QHeaderView::section {
    background-color: #161b22;
    color: #8b949e;
    padding: 6px 8px;
    border: none;
    border-bottom: 1px solid #30363d;
    font-weight: 700;
}
#issueTable QTableCornerButton::section {
    background-color: #161b22;
    border: none;
    border-bottom: 1px solid #30363d;
}
#codeEditor {
    background-color: #0d1117;
    border: 1px solid #30363d;
    border-radius: 8px;
    color: #e6edf3;
    font-family: "SF Mono", "Menlo", "Consolas", monospace;
    font-size: 12px;
    selection-background-color: #264f78;
    selection-color: #ffffff;
}
#codeLineNumberArea {
    background-color: #0d1117;
    border-right: 1px solid #21262d;
}
#diffView {
    background-color: #0d1117;
    border: 1px solid #30363d;
    border-radius: 6px;
    color: #e6edf3;
    font-family: monospace;
    font-size: 12px;
}
#readmeView {
    background-color: #0d1117;
    border: 1px solid #30363d;
    border-radius: 8px;
    padding: 12px;
    color: #e6edf3;
}
/* Agent detail: log terminal + status pill + API-traffic panel */
QPlainTextEdit#actionLog {
    background-color: #010409;
    border: 1px solid #30363d;
    border-radius: 8px;
    color: #c9d1d9;
    padding: 10px 12px;
    selection-background-color: #1f6feb;
}
#agentStatusPill {
    background-color: #161b22;
    border: 1px solid #30363d;
    border-radius: 11px;
    padding: 2px 10px;
    font-size: 12px;
    font-weight: 600;
}
#agentNetPanel {
    background-color: #0d1117;
    border: 1px solid #30363d;
    border-radius: 8px;
    padding: 8px 12px;
}

)";

// GitHub (Primer) light:
//   canvas #ffffff   subtle #f6f8fa   border #d0d7de
//   text   #1f2328 / muted #656d76    accent (links) #0969da
//   success #1f883d (hover #1a7f37)    danger #cf222e
inline const char *kLightStyleSheet = R"(
* { outline: none; }
QWidget {
    background-color: #ffffff;
    color: #1f2328;
    font-family: 'Segoe UI', 'Inter', 'Cantarell', -apple-system, sans-serif;
    font-size: 14px;
}
QToolTip {
    background-color: #24292f; color: #ffffff;
    border: 1px solid #24292f; padding: 4px;
}

/* --- Setup page --- */
#setupCard {
    background-color: #f6f8fa;
    border: 1px solid #d0d7de;
    border-radius: 12px;
}
#appTitle { font-size: 26px; font-weight: 800; background: transparent; }
#appTitleAccent { color: #cf222e; }
#appSubtitle { color: #656d76; background: transparent; }
#setupCard QLabel { background: transparent; }
QRadioButton { background: transparent; spacing: 8px; padding: 4px 0; }
QRadioButton::indicator {
    width: 16px; height: 16px; border-radius: 9px;
    border: 2px solid #d0d7de; background: #ffffff;
}
QRadioButton::indicator:checked { border-color: #0969da; background: #0969da; }
QCheckBox { background: transparent; spacing: 8px; padding: 4px 0; }
QCheckBox::indicator {
    width: 16px; height: 16px; border-radius: 4px;
    border: 2px solid #d0d7de; background: #ffffff;
}
QCheckBox::indicator:checked { border-color: #0969da; background: #0969da; }
#modeHint { color: #656d76; font-size: 12px; padding-left: 26px; background: transparent; }

QLineEdit, QSpinBox, QComboBox {
    background-color: #ffffff;
    border: 1px solid #d0d7de;
    border-radius: 6px;
    padding: 8px 10px;
    selection-background-color: #0969da;
    selection-color: #ffffff;
}
QLineEdit:focus, QSpinBox:focus, QComboBox:focus { border-color: #0969da; }
QLineEdit:disabled, QSpinBox:disabled { color: #8c959f; border-color: #eaeef2; }
QSpinBox::up-button, QSpinBox::down-button { width: 0; }
QComboBox QAbstractItemView {
    background-color: #ffffff; border: 1px solid #d0d7de;
    selection-background-color: #0969da; selection-color: #ffffff; color: #1f2328;
}

QPushButton {
    background-color: #f6f8fa;
    border: 1px solid #d0d7de;
    border-radius: 6px;
    padding: 8px 16px;
    font-weight: 600;
}
QPushButton:hover { background-color: #f3f4f6; }
QPushButton:pressed { background-color: #ebecf0; }
QPushButton#primaryButton {
    background-color: #1f883d; border: 1px solid #1a7f37; color: #ffffff;
}
QPushButton#primaryButton:hover { background-color: #1a7f37; }
QPushButton#primaryButton:pressed { background-color: #187733; }
QPushButton#ghostButton {
    background: transparent; border: none; color: #656d76;
    font-weight: 500; padding: 4px 8px; text-align: left;
}
QPushButton#ghostButton:hover { color: #1f2328; }

/* --- Nav rail --- */
#navRail { background-color: #f6f8fa; border-right: 1px solid #d0d7de; }
#navRail QLabel { background: transparent; }
#navLogo { font-size: 18px; font-weight: 800; padding-bottom: 4px; }
QPushButton#navButton {
    background: transparent; border: none; color: #656d76;
    font-size: 11px; font-weight: 600; border-radius: 6px; padding: 8px 2px;
}
QPushButton#navButton:hover { background-color: #eaeef2; color: #1f2328; }
QPushButton#navButton:checked { background-color: #eaeef2; color: #1f2328; }

/* --- Repo detail tabs --- */
QPushButton#repoTab {
    background: transparent; border: none; border-bottom: 2px solid transparent;
    color: #656d76; font-weight: 600; padding: 6px 10px;
}
QPushButton#repoTab:hover { color: #1f2328; }
QPushButton#repoTab:checked { color: #1f2328; border-bottom: 2px solid #fd8c73; }

/* --- Repo files: explorer tree + editor tabs --- */
#fileTree { background-color: #ffffff; border: 1px solid #d0d7de; border-radius: 6px; }
#fileTree::item { padding: 3px 2px; color: #1f2328; }
#fileTree::item:hover { background-color: #f6f8fa; padding: 3px 2px; }
#fileTree::item:selected { background-color: #0969da; color: #ffffff; padding: 3px 2px; }
#fileTabs::pane { border: 1px solid #d0d7de; border-radius: 6px; top: -1px; }
#fileTabs QTabBar::tab {
    background: #ffffff; color: #656d76; padding: 6px 12px;
    border: 1px solid transparent; border-top-left-radius: 6px;
    border-top-right-radius: 6px;
}
#fileTabs QTabBar::tab:hover { color: #1f2328; }
#fileTabs QTabBar::tab:selected {
    background: #f6f8fa; color: #1f2328; border-color: #d0d7de; border-bottom-color: #f6f8fa;
}
#codeEditor {
    background-color: #ffffff; border: none; color: #1f2328;
    font-family: monospace; font-size: 12px;
}
#diffView {
    background-color: #ffffff; border: 1px solid #d0d7de; border-radius: 6px;
    color: #1f2328; font-family: monospace; font-size: 12px;
}
#commitBar { background-color: #f6f8fa; border: 1px solid #d0d7de; border-radius: 6px; }
#commitBar QLabel { background: transparent; }
#commitBarText { color: #1f2328; }
#overviewList { background-color: #ffffff; border: 1px solid #d0d7de; border-radius: 6px; }
#overviewList::item { padding: 6px 8px; color: #1f2328; }
#overviewList::item:hover { background-color: #f6f8fa; padding: 6px 8px; }
#overviewList::item:selected { background-color: #0969da; color: #ffffff; padding: 6px 8px; }
#readmeView {
    background-color: #ffffff; border: 1px solid #d0d7de; border-radius: 6px;
    padding: 8px; color: #1f2328;
}
/* GitHub-style repo header + About sidebar */
#repoHeaderTitle { font-size: 20px; font-weight: 400; }
#publicBadge {
    color: #656d76; border: 1px solid #d0d7de; border-radius: 10px;
    padding: 0 8px; font-size: 11px;
}
#issueStatusPill {
    color: #ffffff; border-radius: 13px; padding: 5px 16px;
    font-size: 12px; font-weight: 700;
}
#issueStatusPill[status="open"] { background-color: #1a7f37; }
#issueStatusPill[status="closed"] { background-color: #8250df; }
QPushButton#repoAction {
    background-color: #f6f8fa; border: 1px solid #d0d7de; border-radius: 6px;
    padding: 4px 10px; font-size: 12px; font-weight: 600; color: #1f2328;
}
QPushButton#repoAction:hover { background-color: #f3f4f6; }
QPushButton#repoAction::menu-indicator { width: 0; }
#repoTabBar { border-bottom: 1px solid #d0d7de; }
#aboutSidebar { background: transparent; }
#aboutSidebar QLabel { background: transparent; }
#aboutHeading { font-size: 15px; font-weight: 700; }
#langBar { background-color: #eaeef2; border-radius: 4px; }
#commitsList {
    background-color: #ffffff; border: 1px solid #d0d7de; border-radius: 6px;
}
#commitsList::item { padding: 8px; color: #1f2328; border-bottom: 1px solid #d8dee4; }
#commitsList::item:hover { background-color: #f6f8fa; padding: 8px; border-bottom: 1px solid #d8dee4; }
#commitsList::item:selected { background-color: #0969da; color: #ffffff; padding: 8px; border-bottom: 1px solid #d8dee4; }
#placeholderPanel { color: #1f2328; font-size: 16px; }
#insightsPage QLabel { background: transparent; }
#insightsCard {
    background-color: #f6f8fa; border: 1px solid #d0d7de; border-radius: 8px;
    padding: 10px; color: #1f2328;
}

/* --- Server favicon rail --- */
#serverRail { background-color: #f6f8fa; border-right: 1px solid #d0d7de; }
QPushButton#serverButton {
    background-color: #ffffff; border: 2px solid transparent;
    border-radius: 12px; padding: 0;
}
QPushButton#serverButton:hover { border-color: #d0d7de; border-radius: 12px; }
QPushButton#serverButton:checked { border-color: #1f883d; border-radius: 12px; }
QPushButton#serverAddButton {
    background-color: #eaeef2; border: none;
    border-radius: 12px; color: #1f883d; font-size: 22px; font-weight: 500;
    padding-bottom: 3px;
}
QPushButton#serverAddButton:hover { background-color: #1f883d; color: #ffffff; }
QPushButton#serverAddButton:pressed { background-color: #1a7f37; }
QPushButton#serverFooterButton {
    background: transparent; border: none; color: #656d76; font-size: 18px;
    border-radius: 8px;
}
QPushButton#serverFooterButton:hover { background-color: #eaeef2; color: #1f2328; }

/* --- Breadcrumb bar --- */
#breadcrumbBar { background-color: #ffffff; border-bottom: 1px solid #d8dee4; }
#breadcrumb { background: transparent; color: #656d76; font-size: 14px; font-weight: 600; }
#connectionStatus { background: transparent; color: #656d76; font-size: 13px; font-weight: 600; }
/* Top-row switchers (relay / node / repo): favicon + dropdown + open-in-browser */
QPushButton#relayIconButton, QPushButton#relayOpenButton {
    background: transparent; border: none; border-radius: 8px; color: #656d76;
}
QPushButton#relayMenuButton, QPushButton#nodeMenuButton, QPushButton#repoMenuButton {
    background: transparent; border: 1px solid #d0d7de; border-radius: 8px;
    color: #1f2328; font-size: 15px; font-weight: 700; padding: 5px 12px;
}
QPushButton#relayIconButton:hover, QPushButton#relayOpenButton:hover,
QPushButton#relayMenuButton:hover, QPushButton#nodeMenuButton:hover,
QPushButton#repoMenuButton:hover {
    background-color: #eaeef2; color: #1f2328;
}
#navCaption { background: transparent; color: #656d76; font-size: 13px; font-weight: 600; }
#navSolanaBalance {
    background: transparent; border: 1px solid #d0d7de; border-radius: 8px;
    color: #656d76; font-size: 13px; font-weight: 700; padding: 5px 10px;
}
#topMessage { background-color: #f6f8fa; border: 1px solid #d0d7de; border-radius: 10px;
              padding: 2px 12px; font-size: 12px; font-weight: 600; }
QPushButton#notificationButton, QPushButton#notificationButtonAlert {
    background: transparent; border: 1px solid #d0d7de; border-radius: 6px;
    padding: 2px 6px; font-size: 13px; color: #656d76;
}
QPushButton#notificationButton:hover {
    background-color: #f6f8fa; color: #1f2328;
}
QPushButton#notificationButtonAlert {
    color: #9a6700; border-color: #d4a72c; font-weight: 800;
}
QPushButton#notificationButtonAlert:hover {
    background-color: #fff8c5; color: #7d4e00;
}

/* --- Home --- */
#homeCard {
    background-color: #ffffff; border: 1px solid #d0d7de; border-radius: 12px;
}
#homeCard QLabel { background: transparent; }
#homeTitle { font-size: 24px; font-weight: 800; }
#homeName { font-size: 18px; font-weight: 700; }
#homeStat { color: #656d76; font-size: 14px; }
#homeScoreBoard {
    background-color: #f6f8fa; border: 1px solid #d0d7de; border-radius: 12px;
}
#homeScoreValue {
    color: #9a6700; font-size: 18px; font-weight: 800; background: transparent;
}
#homeGraph {
    background-color: #f6f8fa; border: 1px solid #d0d7de; border-radius: 8px;
    color: #0969da; font-family: monospace; font-size: 12px; padding: 10px;
}
#homeCard QListWidget {
    background: #f6f8fa; border: 1px solid #d0d7de; border-radius: 6px; padding: 4px;
}
#homeCard QListWidget::item { color: #1f2328; padding: 8px 7px; margin: 2px; }
QPushButton#primaryButton:disabled {
    background-color: #eaeef2; color: #8c959f; border-color: #d0d7de;
}

/* --- Danger button (Leave node) --- */
QPushButton#dangerButton {
    background: transparent; border: 1px solid #cf222e; border-radius: 6px;
    color: #cf222e; font-weight: 600;
}
QPushButton#dangerButton:hover { background-color: #cf222e; color: #ffffff; }

/* --- Sidebar --- */
#sidebar { background-color: #ffffff; border-right: 1px solid #d0d7de; }
#sidebar QLabel { background: transparent; }
#workspaceName {
    font-size: 17px; font-weight: 800; padding: 2px 0;
}
#statusLine { color: #656d76; font-size: 11px; }
#versionLabel { color: #8c959f; font-size: 11px; background: transparent; }
#sectionLabel {
    color: #656d76; font-size: 11px; font-weight: 700;
    letter-spacing: 1px; padding-top: 8px;
}
#sidebar QListWidget {
    background: transparent; border: none; padding: 0;
}
#sidebar QListWidget::item {
    color: #1f2328; border-radius: 6px; padding: 5px 8px; margin: 1px 0;
}
#sidebar QListWidget::item:hover {
    background-color: #f6f8fa; border-radius: 6px; padding: 5px 8px; margin: 1px 0;
}
#sidebar QListWidget::item:selected {
    background-color: #0969da; color: #ffffff; border-radius: 6px; padding: 5px 8px; margin: 1px 0;
}
QWidget#memberRow { background: transparent; }
QPushButton#memberNameButton {
    background: transparent; border: none; color: #1f2328;
    padding: 4px 6px; text-align: left; font-weight: 500;
}
QPushButton#memberNameButton:hover {
    background-color: #f6f8fa; color: #0969da; border-radius: 6px;
}
QPushButton#memberDeleteButton {
    background: transparent; border: 1px solid #d0d7de; border-radius: 5px;
    color: #cf222e; padding: 2px 6px; font-size: 11px; font-weight: 700;
}
QPushButton#memberDeleteButton:hover {
    background-color: #cf222e; border-color: #cf222e; color: #ffffff;
}
#issueQuickAdd {
    background-color: #ffffff; border: 1px solid #d0d7de;
    border-radius: 6px; padding: 8px 10px; font-size: 13px;
}
#issueQuickAdd:focus { border-color: #0969da; }
#issueSearch {
    background-color: #ffffff; border: 1px solid #d0d7de;
    border-radius: 6px; padding: 6px 10px;
}
#issueSearch:focus { border-color: #0969da; }
#issueTable {
    background-color: #ffffff; border: 1px solid #d0d7de; border-radius: 6px;
    gridline-color: #d8dee4;
}
#issueTable::item { padding: 4px 8px; color: #1f2328; }
#issueTable::item:hover { background-color: #f6f8fa; color: #1f2328; padding: 4px 8px; }
#issueTable::item:selected { background-color: #0969da; color: #ffffff; padding: 4px 8px; }
#issueTable QHeaderView::section {
    background-color: #f6f8fa; color: #656d76; padding: 6px 8px;
    border: none; border-bottom: 1px solid #d0d7de; font-weight: 700;
}

/* --- Chat area --- */
#chatHeader {
    background-color: #ffffff;
    border-bottom: 1px solid #d0d7de;
}
#chatHeader QLabel { background: transparent; }
#channelTitle { font-size: 16px; font-weight: 700; }
#encryptionLabel { color: #656d76; font-size: 12px; }
#firewallBanner {
    background-color: #fff8c5;
    border-bottom: 1px solid #d4a72c;
}
#firewallBannerLabel {
    color: #7d4e00; font-size: 13px; background: transparent;
}
#solanaBanner {
    background-color: #dafbe1;
    border-bottom: 1px solid #1f883d;
}
#solanaBannerLabel {
    color: #1a7f37; font-size: 13px; font-weight: 600; background: transparent;
}
#messageView, #messageContainer {
    background-color: #ffffff; border: none;
}
#typingLabel {
    background-color: #ffffff; color: #656d76; font-size: 12px;
    padding: 0 18px;
}
#messageRow:hover { background-color: #f6f8fa; }
#messageText { color: #1f2328; }
#fileChip {
    background-color: #f6f8fa; border: 1px solid #d0d7de; border-radius: 6px;
}
#reactionChip {
    background-color: #f6f8fa; border: 1px solid #d0d7de; border-radius: 11px;
    padding: 1px 8px; font-size: 12px; color: #1f2328;
}
#reactionChip:hover { border-color: #0969da; }
#reactionAdd {
    background: transparent; border: none; color: #8c959f;
    font-size: 13px; padding: 1px 4px;
}
#reactionAdd:hover { color: #1f2328; }
#messageAction {
    background: transparent; border: 1px solid #d0d7de; border-radius: 6px;
    color: #656d76; font-size: 11px; padding: 2px 7px;
}
#messageAction:hover { color: #1f2328; border-color: #0969da; }
#iconButton {
    background: transparent; border: none; font-size: 18px; padding: 2px 6px;
}
#iconButton:hover { background-color: #eaeef2; border-radius: 6px; }
/* Settings */
#settingsTitle { font-size: 20px; font-weight: 800; }
#avatarPreview {
    background-color: #f6f8fa; border: 1px solid #d0d7de; border-radius: 12px;
    color: #8c959f; font-size: 11px;
}
#networkLog {
    background-color: #f6f8fa; border: none;
    color: #656d76; font-family: monospace; font-size: 12px;
}
#logDock { background-color: #f6f8fa; border-top: 1px solid #d0d7de; }
#logDock QLabel { background: transparent; }
#composerBar { background-color: #ffffff; border-top: 1px solid #d0d7de; }
#messageInput {
    background-color: #ffffff; border: 1px solid #d0d7de;
    border-radius: 6px; padding: 10px 12px; font-size: 14px;
}
#messageInput:focus { border-color: #0969da; }

QScrollBar:vertical {
    background: transparent; width: 10px; margin: 0;
}
QScrollBar::handle:vertical {
    background: #d0d7de; border-radius: 5px; min-height: 30px;
}
QScrollBar::handle:vertical:hover { background: #afb8c1; }
QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }
QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical { background: none; }

QMessageBox, QInputDialog, QDialog { background-color: #ffffff; }

/* --- ForkMesh updated non-Home polish overrides --- */
QPushButton[buttonSize="sm"] {
    min-height: 24px; max-height: 28px; padding: 3px 8px;
    font-size: 12px; border-radius: 6px;
}
QPushButton#repoTab {
    background: transparent;
    border: none;
    border-bottom: 2px solid transparent;
    border-radius: 0;
    color: #656d76;
    font-weight: 600;
    padding: 6px 10px;
}
QPushButton#repoTab:hover { color: #1f2328; background: transparent; }
QPushButton#repoTab:checked {
    color: #1f2328;
    border-bottom: 2px solid #1f883d;
    border-radius: 0;
    background: transparent;
}
QPushButton#repoAction {
    background-color: #eaeef2;
    border: 1px solid #d0d7de;
    border-radius: 6px;
    padding: 4px 10px;
    font-size: 12px;
    font-weight: 600;
    color: #1f2328;
    min-height: 24px;
    max-height: 28px;
}
QPushButton#repoAction:hover { background-color: #d0d7de; }
QPushButton#repoAction::menu-indicator { width: 0; }
QPushButton#serverAddButton {
    background-color: #ffffff;
    border: 1px dashed #d0d7de;
    border-radius: 12px;
    color: #656d76;
    font-size: 20px;
    font-weight: 700;
    padding: 0;
    text-align: center;
}
QPushButton#serverAddButton:hover { color: #1f883d; border-color: #1f883d; background-color: #ffffff; }
#sectionCard, #settingsCard {
    background-color: #ffffff;
    border: 1px solid #d0d7de;
    border-radius: 8px;
}
#sectionCard QLabel, #settingsCard QLabel { background: transparent; }
#settingsTitle { font-size: 20px; font-weight: 800; }
#avatarPreview {
    background-color: #ffffff;
    border: 1px solid #d0d7de;
    border-radius: 12px;
    color: #6e7681;
    font-size: 11px;
}
#networkLog {
    background-color: #f6f8fa;
    border: none;
    color: #656d76;
    font-family: monospace;
    font-size: 12px;
}
#logDock { background-color: #f6f8fa; border-top: 1px solid #d0d7de; }
#issueQuickAdd {
    background-color: #ffffff;
    border: 1px dashed #d0d7de;
    border-radius: 8px;
    padding: 5px 8px;
    font-size: 12px;
    min-height: 24px;
    max-height: 28px;
}
#issueQuickAdd:focus { border-color: #0969da; }
#issuePageTitle {
    font-size: 26px;
    font-weight: 400;
    color: #1f2328;
    background: transparent;
}
#issueDivider { background-color: #d0d7de; }
QPushButton#issueIconButton {
    background: transparent;
    border: none;
    border-radius: 6px;
    padding: 4px;
}
QPushButton#issueIconButton:hover { background-color: #eaeef2; }
#issuePageScroll {
    background: transparent;
    border: none;
}
#issuePageScroll QWidget { background: transparent; }
#issueAvatar {
    background-color: #f6f8fa;
    border: 1px solid #d0d7de;
    border-radius: 18px;
    color: #1f2328;
    font-size: 11px;
    font-weight: 800;
}
#issueCommentTitle {
    font-size: 16px;
    font-weight: 700;
    color: #1f2328;
    background: transparent;
}
#issueCommentEditor { background: transparent; }
#issueTimelineRow { background: transparent; }
#issueTimelineCard {
    background-color: #ffffff;
    border: 1px solid #0969da;
    border-radius: 6px;
}
#issueTimelineCard QLabel { background: transparent; }
#issueTimelineHeader {
    background-color: #ddf4ff;
    border-bottom: 1px solid #80ccff;
    border-top-left-radius: 6px;
    border-top-right-radius: 6px;
}
#issueTimelineHeader QLabel {
    background: transparent;
    color: #1f2328;
}
#issueAttachmentPreview {
    background-color: #f6f8fa;
    border: 1px solid #d0d7de;
    border-radius: 6px;
    padding: 6px;
}
QToolButton#issueActionButton, QPushButton#issueActionButton {
    background: transparent;
    border: none;
    border-radius: 6px;
    color: #656d76;
    font-weight: 800;
    padding: 4px 8px;
}
QToolButton#issueActionButton:hover, QPushButton#issueActionButton:hover { background-color: #cceaff; }
QToolButton#issueActionButton::menu-indicator { image: none; width: 0; }
#issueSidebar { background: transparent; }
#issueSidebar QLabel { background: transparent; }
#issueSidebarHeading {
    color: #57606a;
    font-size: 12px;
    font-weight: 700;
    background: transparent;
}
#issueSidebarDivider { background-color: #d0d7de; }
QPushButton#issueSidebarAction, QPushButton#issueDangerLink {
    background: transparent;
    border: none;
    color: #1f2328;
    font-weight: 500;
    padding: 6px 2px;
    text-align: left;
}
QPushButton#issueSidebarAction:hover { color: #0969da; }
QPushButton#issueDangerLink { color: #cf222e; }
QPushButton#issueDangerLink:hover { color: #a40e26; }
QMenu {
    background-color: #ffffff;
    border: 1px solid #d0d7de;
    border-radius: 8px;
    padding: 6px;
    color: #1f2328;
}
QMenu::item {
    padding: 7px 26px 7px 22px;
    border-radius: 6px;
}
QMenu::item:selected { background-color: #f6f8fa; }
QMenu::separator {
    height: 1px;
    background: #d0d7de;
    margin: 6px 0;
}
QLineEdit#issueSearch, QComboBox#issueControlSm {
    background-color: #ffffff;
    border: 1px solid #d0d7de;
    border-radius: 8px;
    padding: 4px 8px;
    font-size: 12px;
    min-height: 24px;
    max-height: 28px;
}
QLineEdit#issueSearch:focus, QComboBox#issueControlSm:focus { border-color: #0969da; }
QPlainTextEdit#issueComposerSm {
    background-color: #ffffff;
    color: #1f2328;
    border: 1px solid #d0d7de;
    border-radius: 8px;
    padding: 8px;
    selection-background-color: rgba(46, 160, 67, 46);
}
QPlainTextEdit#issueComposerSm:focus { border: 1px solid #0969da; }
#issueComposeSidebar { background: transparent; }
QPushButton#markdownTab {
    background-color: #f6f8fa;
    border: 1px solid #d0d7de;
    border-bottom: none;
    border-radius: 0;
    color: #656d76;
    padding: 8px 16px;
}
QPushButton#markdownTab:checked {
    background-color: #ffffff;
    color: #1f2328;
}
QToolButton#markdownTool {
    background: transparent;
    border: none;
    border-radius: 4px;
    color: #656d76;
    padding: 5px 7px;
    font-weight: 700;
}
QToolButton#markdownTool:hover {
    background-color: #eaeef2;
    color: #1f2328;
}
QPlainTextEdit#markdownSource, QTextBrowser#markdownPreview {
    background-color: #ffffff;
    color: #1f2328;
    border: 1px solid #d0d7de;
    border-radius: 6px;
    padding: 14px 16px;
    selection-background-color: #0969da;
    selection-color: #ffffff;
}
QPlainTextEdit#markdownSource:focus { border-color: #0969da; }
#issueTable {
    background-color: #ffffff;
    alternate-background-color: #ffffff;
    border: 1px solid #d0d7de;
    border-radius: 8px;
    gridline-color: #eaeef2;
    selection-background-color: #0969da;
    selection-color: #ffffff;
}
#issueTable::item { padding: 6px 8px; color: #1f2328; }
#issueTable::item:hover { background-color: #f6f8fa; color: #1f2328; padding: 6px 8px; }
#issueTable::item:selected { background-color: #0969da; color: #ffffff; padding: 6px 8px; }
#issueTable QHeaderView::section {
    background-color: #ffffff;
    color: #656d76;
    padding: 6px 8px;
    border: none;
    border-bottom: 1px solid #d0d7de;
    font-weight: 700;
}
#issueTable QTableCornerButton::section {
    background-color: #ffffff;
    border: none;
    border-bottom: 1px solid #d0d7de;
}
#codeEditor {
    background-color: #ffffff;
    border: 1px solid #d0d7de;
    border-radius: 8px;
    color: #1f2328;
    font-family: "SF Mono", "Menlo", "Consolas", monospace;
    font-size: 12px;
    selection-background-color: #0969da;
    selection-color: #ffffff;
}
#codeLineNumberArea {
    background-color: #ffffff;
    border-right: 1px solid #eaeef2;
}
#diffView {
    background-color: #ffffff;
    border: 1px solid #d0d7de;
    border-radius: 6px;
    color: #1f2328;
    font-family: monospace;
    font-size: 12px;
}
#readmeView {
    background-color: #ffffff;
    border: 1px solid #d0d7de;
    border-radius: 8px;
    padding: 12px;
    color: #1f2328;
}
/* Agent detail: log terminal + status pill + API-traffic panel */
QPlainTextEdit#actionLog {
    background-color: #f6f8fa;
    border: 1px solid #d0d7de;
    border-radius: 8px;
    color: #1f2328;
    padding: 10px 12px;
    selection-background-color: #0969da;
    selection-color: #ffffff;
}
#agentStatusPill {
    background-color: #f6f8fa;
    border: 1px solid #d0d7de;
    border-radius: 11px;
    padding: 2px 10px;
    font-size: 12px;
    font-weight: 600;
}
#agentNetPanel {
    background-color: #f6f8fa;
    border: 1px solid #d0d7de;
    border-radius: 8px;
    padding: 8px 12px;
}

)";

// The stylesheet matching the OS color scheme (dark by default).
inline const char *styleSheetForDark(bool dark)
{
    return dark ? kStyleSheet : kLightStyleSheet;
}

} // namespace Theme
