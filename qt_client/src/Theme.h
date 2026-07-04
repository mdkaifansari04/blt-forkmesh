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
    if (objectName == QStringLiteral("repoTab") ||
        objectName == QStringLiteral("socialIconButton"))
        return dark ? "#8b949e" : "#656d76";
    if (objectName == QStringLiteral("quickAddSendIcon"))
        return dark ? "#3fb950" : "#1a7f37";
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
/* Relay host: pre-filled default, de-emphasized ("greyed out") but editable. */
#relayHostEdit { color: #8b949e; }
#relayHostEdit:focus { color: #e6edf3; }
/* Join wizard header */
#wizardStep { color: #3fb950; font-size: 11px; font-weight: 700; letter-spacing: 1px; background: transparent; }
#wizardTitle { font-size: 20px; font-weight: 800; background: transparent; }

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
QComboBox#quickAddAgentSelector, QComboBox#quickAddModelSelector, QComboBox#quickAddModeSelector {
    border: none;
    background-color: transparent;
    padding: 0px 4px 0px 8px;
}
QComboBox#quickAddAgentSelector:focus, QComboBox#quickAddModelSelector:focus, QComboBox#quickAddModeSelector:focus {
    border: none;
    background-color: rgba(88, 166, 255, 0.08);
}
QComboBox#quickAddAgentSelector::drop-down, QComboBox#quickAddModelSelector::drop-down, QComboBox#quickAddModeSelector::drop-down {
    border: none;
    width: 20px;
}
QComboBox#quickAddAgentSelector::down-arrow, QComboBox#quickAddModelSelector::down-arrow, QComboBox#quickAddModeSelector::down-arrow {
    image: url(:/icons/octicons/chevron-down.svg);
    width: 16px;
    height: 16px;
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
/* The commit strip's "N Commits" toggle: ghost-button look, plus a lit
   checked state while the commits panel is showing under the commit bar. */
QPushButton#commitsToggle {
    background: transparent; border: 1px solid transparent; color: #8b949e;
    font-weight: 500; padding: 4px 8px; text-align: left; border-radius: 6px;
}
QPushButton#commitsToggle:hover { color: #e6edf3; }
QPushButton#commitsToggle:checked {
    color: #e6edf3; background: rgba(46,160,67,0.18);
    border: 1px solid #2ea043;
}

/* --- Network-log quick-filter chips --- */
#logFilterScroll, #logFilterScroll > QWidget,
#logFilterScroll > QWidget > QWidget { background: transparent; border: none; }
QPushButton#logFilterChip {
    background: transparent; border: 1px solid #30363d; color: #8b949e;
    font-weight: 600; font-size: 11px; padding: 2px 10px; border-radius: 11px;
    min-height: 20px; max-height: 24px;
}
QPushButton#logFilterChip:hover { color: #e6edf3; border-color: #6e7681; }
QPushButton#logFilterChip:checked {
    background-color: #21262d; color: #e6edf3; border-color: #2ea043;
}

/* --- Quick-add issue bar: grey-bordered, centered card with social + donate --- */
#quickAddCard {
    background-color: #0d1117;
    border: 1px solid #6e7681;
    border-radius: 10px;
}
QPushButton#donateButton {
    background-color: #9945ff; border: 1px solid #b07bff; color: #ffffff;
    font-weight: 700;
}
QPushButton#donateButton:hover { background-color: #a85cff; }
QPushButton#donateButton:pressed { background-color: #7d34d6; }
QPushButton#socialButton {
    background: transparent; border: 1px solid #30363d; color: #8b949e;
    font-weight: 600; padding: 8px 12px;
}
QPushButton#socialButton:hover { color: #e6edf3; border-color: #6e7681; }
/* Compact icon-only social buttons, stacked beside the donate button (adhoc #117). */
QPushButton#socialIconButton {
    background: transparent; border: 1px solid #30363d; color: #8b949e;
    border-radius: 6px; padding: 0;
}
QPushButton#socialIconButton:hover { border-color: #6e7681; }
#footerGitIdentity { color: #8b949e; font-size: 12px; }

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
/* Hover fill is painted by HoverRowDelegate so the row never shifts. */
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
#settingsTabs::pane { border: 1px solid #30363d; border-radius: 6px; top: -1px; }
#settingsTabs QTabBar::tab {
    background: #0d1117; color: #8b949e; padding: 7px 16px;
    border: 1px solid transparent; border-top-left-radius: 6px;
    border-top-right-radius: 6px;
}
#settingsTabs QTabBar::tab:hover { color: #e6edf3; }
#settingsTabs QTabBar::tab:selected {
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
#pullReviewSummary {
    background-color: #161b22; border: 1px solid #30363d; border-radius: 6px;
    color: #e6edf3;
}
#commitBar { background-color: #161b22; border: 1px solid #30363d; border-radius: 6px; }
#commitBar QLabel { background: transparent; }
#commitBarText { color: #e6edf3; }
#overviewList { background-color: #0d1117; border: 1px solid #30363d; border-radius: 6px; }
#overviewList::item { padding: 6px 8px; color: #c9d1d9; }
/* Hover fill is painted by HoverRowDelegate so the row never shifts. */
#overviewList::item:selected { background-color: #1f6feb; color: #ffffff; padding: 6px 8px; }
#overviewList QHeaderView::section {
    background-color: #0d1117; color: #8b949e; padding: 4px 8px;
    border: none; border-bottom: 1px solid #21262d;
    border-right: 2px solid #010409; font-weight: 600;
}
/* Per-row size bar in the Code overview. */
#sizeBarTrack { background-color: #21262d; border-radius: 3px; }
#sizeBarFill { background-color: #3fb950; border-radius: 3px; }
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
#aboutText { color: #8b949e; font-size: 11px; }
QPushButton#aboutEditButton {
    background-color: transparent; border: 1px solid transparent;
    border-radius: 6px; padding: 4px;
}
QPushButton#aboutEditButton:hover {
    background-color: #21262d; border-color: #30363d;
}
QPushButton#aboutEditButton:disabled { background-color: transparent; }
#langBar { background-color: #161b22; border-radius: 5px; }
#aboutRule { background-color: #21262d; border: none; }
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
/* Presence dot overlaid on the avatar: ring matches the bar so it reads as a cut-out. */
#connectionDot { border: 2px solid #0d1117; }
/* Red unread-count badge on the chat button. */
#chatUnreadBadge {
    background-color: #da3633; color: #ffffff; border: 1px solid #0d1117;
    border-radius: 7px; font-size: 9px; font-weight: 700;
}
/* Count label on the agents button (no red styling). */
#agentsNavBadge {
    color: #656d76; font-size: 13px; font-weight: 600;
}
/* Primary section nav (Code / Chat / Notifications / Settings) — uniform,
   always visible, with a clear selected state. */
#topNavBar { background: transparent; }
#navDivider { background-color: #21262d; border: none; }
QPushButton#topNavButton {
    background: transparent; border: 1px solid transparent; border-radius: 6px;
    color: #8b949e; font-size: 13px; font-weight: 600; padding: 5px 12px;
}
QPushButton#topNavButton:hover { background-color: #161b22; color: #e6edf3; }
QPushButton#topNavButton:checked {
    background-color: #21262d; color: #e6edf3; border-color: #30363d;
}
QPushButton#topNavButton[alert="true"] { color: #d29922; border-color: #9e6a03; }
QPushButton#topNavButton[alert="true"]:checked {
    background-color: #1c1908; color: #f0b72f; border-color: #9e6a03;
}
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
#nodeSwitchProgress { background: transparent; border: none; }
#nodeSwitchProgress::chunk { background-color: #58a6ff; border-radius: 1px; }
#navCaption { background: transparent; color: #8b949e; font-size: 13px; font-weight: 600; }
#navNodeName { background: transparent; color: #8b949e; font-size: 11px; font-weight: 600; }
#navSolanaBalance {
    background: transparent; border: none; border-radius: 8px;
    color: #8b949e; font-size: 13px; font-weight: 700; padding: 5px 10px;
    min-width: 126px; max-width: 126px;
}
#topMessage { background-color: #161b22; border: 1px solid #30363d; border-radius: 10px;
              padding: 2px 12px; font-size: 12px; font-weight: 600; }
#topMessageOverlay { background-color: #161b22; border: 1px solid #30363d;
                     border-radius: 10px; }
#topMessageOverlayText { font-size: 12px; font-weight: 600; color: #c9d1d9; }
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
#promptWrapper {
    background-color: #0d1117; border: 1px solid rgba(57,211,83,0.55); border-radius: 6px;
}
#promptWrapper:focus-within { border-color: #39d353; }
#promptWrapper #issueQuickAdd {
    background: transparent; border: none; border-radius: 0;
}
#promptWrapper #issueQuickAdd:focus { border: none; }
QPushButton#quickAddSendIcon {
    background: transparent; border: none; color: #3fb950;
    padding: 4px; border-radius: 4px;
}
QPushButton#quickAddSendIcon:hover { color: #56d364; background: rgba(63,185,80,0.15); }
#issueSearch {
    background-color: #0d1117; border: 1px solid #30363d;
    border-radius: 6px; padding: 6px 10px;
}
#issueSearch:focus { border-color: #58a6ff; }
#globalSearch {
    background-color: #010409; border: 1px solid #30363d;
    border-radius: 6px; padding: 5px 8px; color: #e6edf3;
}
#globalSearch:focus { border-color: #58a6ff; }
#globalSearchPopup {
    background-color: #161b22; border: 1px solid #30363d;
    border-radius: 8px; padding: 4px; outline: none;
}
#globalSearchPopup::item { color: #c9d1d9; padding: 6px 8px; border-radius: 6px; }
#globalSearchPopup::item:selected { background-color: #1f6feb; color: #ffffff; }
#searchResultsTitle { font-size: 18px; color: #e6edf3; }
#searchResultsTree {
    background-color: #0d1117; border: 1px solid #30363d; border-radius: 8px;
    padding: 4px; outline: none;
}
#searchResultsTree::item { padding: 4px 6px; color: #c9d1d9; }
#searchResultsTree::item:selected { background-color: #1f6feb; color: #ffffff; }
#issueTable {
    background-color: #0d1117; border: 1px solid #30363d; border-radius: 6px;
    gridline-color: #21262d;
}
#issueTable::item { padding: 4px 8px; color: #c9d1d9; }
/* Row hover is a full-row light-green fill drawn by HoverRowDelegate; keep the
   per-cell hover geometry identical to the base item so text never shifts. */
#issueTable::item:hover { padding: 4px 8px; }
#issueTable::item:selected { background-color: #1f6feb; color: #ffffff; padding: 4px 8px; }
#issueTable QHeaderView::section {
    background-color: #161b22; color: #8b949e; padding: 6px 8px;
    border: none; border-bottom: 1px solid #30363d;
    border-right: 2px solid #010409; font-weight: 700;
}

/* --- Kanban issue board --- */
#issueBoardScroll { border: none; background: transparent; }
#issueBoardColumn {
    background-color: #161b22; border: 1px solid #30363d; border-radius: 8px;
}
#issueBoardHeader {
    color: #c9d1d9; font-weight: 700; padding: 2px 2px 4px 2px;
}
#issueBoardList {
    background-color: transparent; border: none;
}
#issueBoardList::item {
    background-color: #0d1117; border: 1px solid #30363d; border-radius: 6px;
    color: #c9d1d9; padding: 8px 8px; margin: 3px 1px;
}
#issueBoardList::item:selected { border-color: #1f6feb; color: #c9d1d9; }

/* --- Chat area --- */
#chatHeader {
    background-color: #0d1117;
    border-bottom: 1px solid #30363d;
}
#chatHeader QLabel { background: transparent; }
#channelTitle { font-size: 16px; font-weight: 700; }
#encryptionLabel { color: #8b949e; font-size: 12px; }

/* --- Node profile control panel --- */
#nodeProfilePanel { background: transparent; }
#nodeProfileContent { background-color: #0d1117; }
#profileBanner { border-radius: 16px; }
#profileName { font-size: 17px; font-weight: 800; }
QPushButton#profileActionButton {
    background-color: #161b22; border: 1px solid #30363d; border-radius: 10px;
    color: #c9d1d9;
}
QPushButton#profileActionButton:hover {
    background-color: #1f2937; border-color: #58a6ff; color: #e6edf3;
}
QPushButton#profileActionButton:pressed { background-color: #0d1117; }
#statTile {
    background-color: #161b22; border: 1px solid #30363d; border-radius: 9px;
    padding: 6px 4px; color: #e6edf3;
}
#profileCard {
    background-color: #161b22; border: 1px solid #30363d; border-radius: 9px;
    padding: 7px 10px; color: #c9d1d9;
}
#profileMono {
    background-color: #161b22; border: 1px solid #30363d; border-radius: 8px;
    padding: 6px 9px; color: #c9d1d9; font-family: monospace;
}
#profileQr {
    background-color: #ffffff; border-radius: 10px; padding: 8px;
}

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
#walletVerifyBanner {
    background-color: #2b210a;
    border-bottom: 1px solid #bb8009;
}
#walletVerifyBanner QLabel { background: transparent; }
#walletVerifyTitle { color: #e3b341; font-size: 15px; font-weight: 800; }
#walletVerifyBody { color: #d8c08a; font-size: 13px; }
#messageView, #messageContainer {
    background-color: #0d1117; border: none;
}
#typingLabel {
    background-color: #0d1117; color: #8b949e; font-size: 12px;
    padding: 0 18px;
}
#messageText { color: #e6edf3; }
#fileChip {
    background-color: #161b22; border: 1px solid #30363d; border-radius: 6px;
}
#reactionChip {
    background-color: #161b22; border: 1px solid #21262d; border-radius: 18px;
    padding: 2px 9px; font-size: 12px; color: #e6edf3;
}
#reactionChip:hover { border-color: #58a6ff; background-color: #1c2433; }
#reactionAdd {
    background: transparent; border: 1px solid transparent; border-radius: 12px;
    padding: 2px 7px;
}
#reactionAdd:hover { border-color: #30363d; background-color: #161b22; }
#reactionPicker {
    background-color: #161b22; border: 1px solid #30363d; border-radius: 10px;
}
#reactionPickerButton { background: transparent; border: none; border-radius: 8px; }
#reactionPickerButton:hover { background-color: #21262d; }
#messageAction {
    background: transparent; border: none; border-radius: 6px;
    color: #8b949e; font-size: 11px; padding: 2px 7px;
}
#messageAction:hover { color: #e6edf3; }
#messageAction::menu-indicator { image: none; width: 0; }
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
#emailVerifiedBadge {
    background-color: #0f2a1a; border: 1px solid #238636; border-radius: 10px;
    color: #3fb950; padding: 2px 8px; font-size: 11px; font-weight: 700;
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
QScrollBar:horizontal {
    background: transparent; height: 10px; margin: 0;
}
QScrollBar::handle:horizontal {
    background: #30363d; border-radius: 5px; min-width: 30px;
}
QScrollBar::handle:horizontal:hover { background: #484f58; }
QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal { width: 0; }
QScrollBar::add-page:horizontal, QScrollBar::sub-page:horizontal { background: none; }

/* Draggable splitter handles — a clear, grabbable grip ridge that lights up
   on hover/drag. Edges are transparent so it blends with any panel color. */
QSplitter::handle:horizontal {
    width: 9px;
    background: qlineargradient(x1:0, y1:0, x2:1, y2:0,
        stop:0 transparent, stop:0.30 transparent,
        stop:0.34 #30363d, stop:0.5 #6e7681, stop:0.66 #30363d,
        stop:0.70 transparent, stop:1 transparent);
}
QSplitter::handle:vertical {
    height: 9px;
    background: qlineargradient(x1:0, y1:0, x2:0, y2:1,
        stop:0 transparent, stop:0.30 transparent,
        stop:0.34 #30363d, stop:0.5 #6e7681, stop:0.66 #30363d,
        stop:0.70 transparent, stop:1 transparent);
}
QSplitter::handle:horizontal:hover, QSplitter::handle:vertical:hover { background: #1f6feb; }
QSplitter::handle:pressed { background: #58a6ff; }

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
/* Shortcuts tab: each shortcut file is a clickable card (a QPushButton hosting
   its own labels). */
QPushButton#shortcutCard {
    background-color: #161b22;
    border: 1px solid #30363d;
    border-radius: 8px;
    text-align: left;
}
QPushButton#shortcutCard:hover { border-color: #2ea043; background-color: #1b2129; }
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
    min-height: 46px;
    max-height: 46px;
}
#issueQuickAdd:focus { border-color: #58a6ff; }
#promptWrapper {
    background-color: #0d1117; border: 1px solid rgba(57,211,83,0.55); border-radius: 8px;
}
#promptWrapper:focus-within { border-color: #39d353; }
#promptWrapper #issueQuickAdd {
    background: transparent; border: none; border-radius: 0;
    min-height: 46px; max-height: 46px;
}
#promptWrapper #issueQuickAdd:focus { border: none; }
QPushButton#quickAddSendIcon {
    background: transparent; border: none; color: #3fb950;
    padding: 4px; border-radius: 4px;
}
QPushButton#quickAddSendIcon:hover { color: #56d364; background: rgba(63,185,80,0.15); }
/* Footer prompt bottom bar (adhoc #99): the Auto/Create-issue/Agent toggles get
   a green filled checkmark instead of the generic blue-filled indicator, and the
   Agent controls sit in a thin bordered box centred in the bar. */
QCheckBox#quickAddAutoCheck::indicator,
QCheckBox#quickAddCreateIssueCheck::indicator,
QCheckBox#quickAddAgentCheck::indicator {
    width: 16px; height: 16px; border-radius: 4px;
    border: 2px solid #30363d; background: #0d1117;
}
QCheckBox#quickAddAutoCheck::indicator:checked,
QCheckBox#quickAddCreateIssueCheck::indicator:checked,
QCheckBox#quickAddAgentCheck::indicator:checked {
    border-color: #2ea043; background: #2ea043;
    image: url(:/icons/octicons/check-white.svg);
}
#quickAddAgentBox {
    border: none; background: transparent;
    font-size: 12px;
}
/* Slash-actions "/" box (adhoc #116), left of the Agent checkbox — a small
   bordered square like the Claude Code extension's own actions button. */
QPushButton#quickAddSlashButton {
    background: #0d1117; border: 1px solid #30363d; border-radius: 5px;
    color: #8b949e; font-weight: 600; font-size: 12px; padding: 0;
}
QPushButton#quickAddSlashButton:hover { border-color: #58a6ff; color: #e6edf3; }
/* The popup itself: filter box on top, scrollable action list below. */
#slashActionsPopup {
    background: #161b22; border: 1px solid #30363d; border-radius: 8px;
}
QLineEdit#slashActionsFilter {
    background: transparent; border: none; border-bottom: 1px solid #30363d;
    border-radius: 0; padding: 10px 12px; color: #e6edf3; font-size: 13px;
}
#slashActionsScroll { background: transparent; border: none; }
#slashActionsScroll > QWidget > QWidget { background: transparent; }
QLabel#slashActionsHeader {
    color: #8b949e; font-size: 11px; font-weight: 600;
    padding: 8px 12px 4px 12px;
}
QFrame#slashActionRow { background: transparent; border-radius: 6px; }
QFrame#slashActionRow[slashSelected="true"] { background: #1f2937; }
QLabel#slashActionRowLabel { color: #e6edf3; font-size: 13px; background: transparent; }
QLabel#slashActionRowValue { color: #8b949e; font-size: 12px; background: transparent; }
QToolButton#slashEffortDot {
    background: transparent; border: 2px solid #6e7681; border-radius: 5px;
}
QToolButton#slashEffortDot:checked { background: #e6edf3; border-color: #e6edf3; }
QCheckBox#slashToggle::indicator {
    width: 28px; height: 16px; border-radius: 8px; border: none; background: #30363d;
}
QCheckBox#slashToggle::indicator:checked { background: #2ea043; }
/* "Agents:" status strip above the footer prompt (adhoc #111): a plain
   ghost-button label plus small borderless dot buttons, one per session. */
QPushButton#agentStatusLabel {
    background: transparent; border: none; color: #8b949e;
    font-weight: 600; font-size: 12px; padding: 2px 0;
}
QPushButton#agentStatusLabel:hover { color: #e6edf3; }
QPushButton#agentStatusDot {
    background: transparent; border: none; padding: 0; border-radius: 3px;
}
QPushButton#agentStatusDot:hover { background: rgba(139,148,158,0.2); }
#agentStatusScroll { background: transparent; border: none; }
#agentStatusScroll > QWidget > QWidget { background: transparent; }
/* Small "fix conflicts with agent" button (adhoc #139) at the end of the
   footer's "Agents:" strip; only shown while the selected session conflicts. */
QPushButton#agentStatusFixButton {
    background: transparent; border: 1px solid #3fb950; border-radius: 5px; padding: 0;
}
QPushButton#agentStatusFixButton:hover { background: rgba(63,185,80,0.15); }
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
#mentionPopup {
    background-color: #161b22;
    color: #e6edf3;
    border: 1px solid #30363d;
    border-radius: 6px;
    outline: 0;
}
#mentionPopup::item { padding: 4px 10px; }
#mentionPopup::item:selected { background-color: #1f6feb; color: #ffffff; }
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
#issueTable::item:hover { padding: 6px 8px; }
#issueTable::item:selected { background-color: #1f6feb; color: #ffffff; padding: 6px 8px; }
#issueTable QHeaderView::section {
    background-color: #161b22;
    color: #8b949e;
    padding: 6px 8px;
    border: none;
    border-bottom: 1px solid #30363d;
    border-right: 2px solid #010409;
    font-weight: 700;
}
#issueTable QTableCornerButton::section {
    background-color: #161b22;
    border: none;
    border-bottom: 1px solid #30363d;
}
#actionWorkflowList {
    background-color: #0d1117;
    alternate-background-color: #161b22;
    border: 1px solid #30363d;
    border-radius: 8px;
    outline: 0;
    padding: 4px;
}
#actionWorkflowList::item {
    border-radius: 6px;
    color: #c9d1d9;
    padding: 7px 10px;
}
#actionWorkflowList::item:hover {
    background-color: #161b22;
    color: #e6edf3;
}
#actionWorkflowList::item:selected {
    background-color: #238636;
    color: #ffffff;
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
#pullReviewSummary {
    background-color: #161b22;
    border: 1px solid #30363d;
    border-radius: 6px;
    color: #e6edf3;
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

/* --- Item-view selection, applied last so it wins over the rules above ---
   1. border:0 gives every ::item a stylesheet box, so QStyleSheetStyle applies
      the same horizontal padding whether or not the row is selected. Without
      it Qt only pads the selected item and the text jumps right on select.
   2. Selection highlight is the brand green (#238636, the primary-button green)
      instead of blue. */
QAbstractItemView::item { border: 0px; }
#issueTable { selection-background-color: #238636; selection-color: #ffffff; }
#issueTable::item:selected,
#overviewList::item:selected,
#fileTree::item:selected,
#commitsList::item:selected,
#sidebar QListWidget::item:selected { background-color: #238636; color: #ffffff; }
QComboBox QAbstractItemView { selection-background-color: #238636; }

/* Floating ▲/▼ jump-to-top/bottom buttons (ScrollJumpButtons), e.g. over the
   agent raw-output log. The rich transcript styles its own copy per scheme. */
QPushButton#scrollJump {
    background-color: #161b22; color: #e6edf3;
    border: 1px solid #30363d; border-radius: 15px;
    font-size: 12px; font-weight: 700;
}
QPushButton#scrollJump:hover { background-color: #1f2630; }

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
/* Relay host: pre-filled default, de-emphasized ("greyed out") but editable. */
#relayHostEdit { color: #8c959f; }
#relayHostEdit:focus { color: #1f2328; }
/* Join wizard header */
#wizardStep { color: #1a7f37; font-size: 11px; font-weight: 700; letter-spacing: 1px; background: transparent; }
#wizardTitle { font-size: 20px; font-weight: 800; background: transparent; }

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
QComboBox#quickAddAgentSelector, QComboBox#quickAddModelSelector, QComboBox#quickAddModeSelector {
    border: none;
    background-color: transparent;
    padding: 0px 4px 0px 8px;
}
QComboBox#quickAddAgentSelector:focus, QComboBox#quickAddModelSelector:focus, QComboBox#quickAddModeSelector:focus {
    border: none;
    background-color: rgba(9, 105, 218, 0.08);
}
QComboBox#quickAddAgentSelector::drop-down, QComboBox#quickAddModelSelector::drop-down, QComboBox#quickAddModeSelector::drop-down {
    border: none;
    width: 20px;
}
QComboBox#quickAddAgentSelector::down-arrow, QComboBox#quickAddModelSelector::down-arrow, QComboBox#quickAddModeSelector::down-arrow {
    image: url(:/icons/octicons/chevron-down.svg);
    width: 16px;
    height: 16px;
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
/* The commit strip's "N Commits" toggle: ghost-button look, plus a lit
   checked state while the commits panel is showing under the commit bar. */
QPushButton#commitsToggle {
    background: transparent; border: 1px solid transparent; color: #656d76;
    font-weight: 500; padding: 4px 8px; text-align: left; border-radius: 6px;
}
QPushButton#commitsToggle:hover { color: #1f2328; }
QPushButton#commitsToggle:checked {
    color: #1f2328; background: rgba(31,136,61,0.14);
    border: 1px solid #1f883d;
}

/* --- Network-log quick-filter chips --- */
#logFilterScroll, #logFilterScroll > QWidget,
#logFilterScroll > QWidget > QWidget { background: transparent; border: none; }
QPushButton#logFilterChip {
    background: transparent; border: 1px solid #d0d7de; color: #656d76;
    font-weight: 600; font-size: 11px; padding: 2px 10px; border-radius: 11px;
    min-height: 20px; max-height: 24px;
}
QPushButton#logFilterChip:hover { color: #1f2328; border-color: #afb8c1; }
QPushButton#logFilterChip:checked {
    background-color: #eaeef2; color: #1f2328; border-color: #1f883d;
}

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
/* Hover fill is painted by HoverRowDelegate so the row never shifts. */
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
#settingsTabs::pane { border: 1px solid #d0d7de; border-radius: 6px; top: -1px; }
#settingsTabs QTabBar::tab {
    background: #ffffff; color: #656d76; padding: 7px 16px;
    border: 1px solid transparent; border-top-left-radius: 6px;
    border-top-right-radius: 6px;
}
#settingsTabs QTabBar::tab:hover { color: #1f2328; }
#settingsTabs QTabBar::tab:selected {
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
#pullReviewSummary {
    background-color: #f6f8fa; border: 1px solid #d0d7de; border-radius: 6px;
    color: #1f2328;
}
#commitBar { background-color: #f6f8fa; border: 1px solid #d0d7de; border-radius: 6px; }
#commitBar QLabel { background: transparent; }
#commitBarText { color: #1f2328; }
#overviewList { background-color: #ffffff; border: 1px solid #d0d7de; border-radius: 6px; }
#overviewList::item { padding: 6px 8px; color: #1f2328; }
/* Hover fill is painted by HoverRowDelegate so the row never shifts. */
#overviewList::item:selected { background-color: #0969da; color: #ffffff; padding: 6px 8px; }
#overviewList QHeaderView::section {
    background-color: #ffffff; color: #656d76; padding: 4px 8px;
    border: none; border-bottom: 1px solid #d8dee4;
    border-right: 2px solid #afb8c1; font-weight: 600;
}
/* Per-row size bar in the Code overview. */
#sizeBarTrack { background-color: #eaeef2; border-radius: 3px; }
#sizeBarFill { background-color: #2da44e; border-radius: 3px; }
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
#aboutText { color: #656d76; font-size: 11px; }
QPushButton#aboutEditButton {
    background-color: transparent; border: 1px solid transparent;
    border-radius: 6px; padding: 4px;
}
QPushButton#aboutEditButton:hover {
    background-color: #f6f8fa; border-color: #d0d7de;
}
QPushButton#aboutEditButton:disabled { background-color: transparent; }
#langBar { background-color: #eaeef2; border-radius: 5px; }
#aboutRule { background-color: #d0d7de; border: none; }
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
#globalSearch {
    background-color: #ffffff; border: 1px solid #d0d7de;
    border-radius: 6px; padding: 5px 8px; color: #1f2328;
}
#globalSearch:focus { border-color: #0969da; }
#globalSearchPopup {
    background-color: #ffffff; border: 1px solid #d0d7de;
    border-radius: 8px; padding: 4px; outline: none;
}
#globalSearchPopup::item { color: #1f2328; padding: 6px 8px; border-radius: 6px; }
#globalSearchPopup::item:selected { background-color: #0969da; color: #ffffff; }
#searchResultsTitle { font-size: 18px; color: #1f2328; }
#searchResultsTree {
    background-color: #ffffff; border: 1px solid #d0d7de; border-radius: 8px;
    padding: 4px; outline: none;
}
#searchResultsTree::item { padding: 4px 6px; color: #1f2328; }
#searchResultsTree::item:selected { background-color: #0969da; color: #ffffff; }
#breadcrumb { background: transparent; color: #656d76; font-size: 14px; font-weight: 600; }
#connectionStatus { background: transparent; color: #656d76; font-size: 13px; font-weight: 600; }
/* Presence dot overlaid on the avatar: ring matches the bar so it reads as a cut-out. */
#connectionDot { border: 2px solid #ffffff; }
/* Red unread-count badge on the chat button. */
#chatUnreadBadge {
    background-color: #cf222e; color: #ffffff; border: 1px solid #ffffff;
    border-radius: 7px; font-size: 9px; font-weight: 700;
}
/* Count label on the agents button (no red styling). */
#agentsNavBadge {
    color: #8b949e; font-size: 13px; font-weight: 600;
}
/* Primary section nav (Code / Chat / Notifications / Settings) — uniform,
   always visible, with a clear selected state. */
#topNavBar { background: transparent; }
#navDivider { background-color: #d0d7de; border: none; }
QPushButton#topNavButton {
    background: transparent; border: 1px solid transparent; border-radius: 6px;
    color: #656d76; font-size: 13px; font-weight: 600; padding: 5px 12px;
}
QPushButton#topNavButton:hover { background-color: #eaeef2; color: #1f2328; }
QPushButton#topNavButton:checked {
    background-color: #eaeef2; color: #1f2328; border-color: #d0d7de;
}
QPushButton#topNavButton[alert="true"] { color: #9a6700; border-color: #d4a72c; }
QPushButton#topNavButton[alert="true"]:checked {
    background-color: #fff8c5; color: #7d4e00; border-color: #d4a72c;
}
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
#nodeSwitchProgress { background: transparent; border: none; }
#nodeSwitchProgress::chunk { background-color: #0969da; border-radius: 1px; }
#navCaption { background: transparent; color: #656d76; font-size: 13px; font-weight: 600; }
#navNodeName { background: transparent; color: #656d76; font-size: 11px; font-weight: 600; }
#navSolanaBalance {
    background: transparent; border: none; border-radius: 8px;
    color: #656d76; font-size: 13px; font-weight: 700; padding: 5px 10px;
    min-width: 126px; max-width: 126px;
}
#topMessage { background-color: #f6f8fa; border: 1px solid #d0d7de; border-radius: 10px;
              padding: 2px 12px; font-size: 12px; font-weight: 600; }
#topMessageOverlay { background-color: #ffffff; border: 1px solid #d0d7de;
                     border-radius: 10px; }
#topMessageOverlayText { font-size: 12px; font-weight: 600; color: #1f2328; }
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
#promptWrapper {
    background-color: #ffffff; border: 1px solid rgba(26,127,55,0.5); border-radius: 6px;
}
#promptWrapper:focus-within { border-color: #1a7f37; }
#promptWrapper #issueQuickAdd {
    background: transparent; border: none; border-radius: 0;
}
#promptWrapper #issueQuickAdd:focus { border: none; }
QPushButton#quickAddSendIcon {
    background: transparent; border: none; color: #1a7f37;
    padding: 4px; border-radius: 4px;
}
QPushButton#quickAddSendIcon:hover { color: #1a7f37; background: rgba(26,127,55,0.12); }
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
#issueTable::item:hover { padding: 4px 8px; }
#issueTable::item:selected { background-color: #0969da; color: #ffffff; padding: 4px 8px; }
#issueTable QHeaderView::section {
    background-color: #f6f8fa; color: #656d76; padding: 6px 8px;
    border: none; border-bottom: 1px solid #d0d7de;
    border-right: 2px solid #afb8c1; font-weight: 700;
}

/* --- Kanban issue board --- */
#issueBoardScroll { border: none; background: transparent; }
#issueBoardColumn {
    background-color: #f6f8fa; border: 1px solid #d0d7de; border-radius: 8px;
}
#issueBoardHeader {
    color: #1f2328; font-weight: 700; padding: 2px 2px 4px 2px;
}
#issueBoardList {
    background-color: transparent; border: none;
}
#issueBoardList::item {
    background-color: #ffffff; border: 1px solid #d0d7de; border-radius: 6px;
    color: #1f2328; padding: 8px 8px; margin: 3px 1px;
}
#issueBoardList::item:selected { border-color: #0969da; color: #1f2328; }

/* --- Chat area --- */
#chatHeader {
    background-color: #ffffff;
    border-bottom: 1px solid #d0d7de;
}
#chatHeader QLabel { background: transparent; }
#channelTitle { font-size: 16px; font-weight: 700; }
#encryptionLabel { color: #656d76; font-size: 12px; }

/* --- Node profile control panel --- */
#nodeProfilePanel { background: transparent; }
#nodeProfileContent { background-color: #ffffff; }
#profileBanner { border-radius: 16px; }
#profileName { font-size: 17px; font-weight: 800; }
QPushButton#profileActionButton {
    background-color: #f6f8fa; border: 1px solid #d0d7de; border-radius: 10px;
    color: #1f2328;
}
QPushButton#profileActionButton:hover {
    background-color: #eaeef2; border-color: #0969da; color: #1f2328;
}
QPushButton#profileActionButton:pressed { background-color: #e1e6eb; }
#statTile {
    background-color: #f6f8fa; border: 1px solid #d0d7de; border-radius: 9px;
    padding: 6px 4px; color: #1f2328;
}
#profileCard {
    background-color: #f6f8fa; border: 1px solid #d0d7de; border-radius: 9px;
    padding: 7px 10px; color: #1f2328;
}
#profileMono {
    background-color: #f6f8fa; border: 1px solid #d0d7de; border-radius: 8px;
    padding: 6px 9px; color: #1f2328; font-family: monospace;
}
#profileQr {
    background-color: #ffffff; border: 1px solid #d0d7de; border-radius: 10px;
    padding: 8px;
}

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
#walletVerifyBanner {
    background-color: #fff8c5;
    border-bottom: 1px solid #d4a72c;
}
#walletVerifyBanner QLabel { background: transparent; }
#walletVerifyTitle { color: #7d4e00; font-size: 15px; font-weight: 800; }
#walletVerifyBody { color: #6a4b16; font-size: 13px; }
#messageView, #messageContainer {
    background-color: #ffffff; border: none;
}
#typingLabel {
    background-color: #ffffff; color: #656d76; font-size: 12px;
    padding: 0 18px;
}
#messageText { color: #1f2328; }
#fileChip {
    background-color: #f6f8fa; border: 1px solid #d0d7de; border-radius: 6px;
}
#reactionChip {
    background-color: #f6f8fa; border: 1px solid #e5e7eb; border-radius: 18px;
    padding: 2px 9px; font-size: 12px; color: #1f2328;
}
#reactionChip:hover { border-color: #0969da; background-color: #eef4fb; }
#reactionAdd {
    background: transparent; border: 1px solid transparent; border-radius: 12px;
    padding: 2px 7px;
}
#reactionAdd:hover { border-color: #d0d7de; background-color: #f6f8fa; }
#reactionPicker {
    background-color: #ffffff; border: 1px solid #d0d7de; border-radius: 10px;
}
#reactionPickerButton { background: transparent; border: none; border-radius: 8px; }
#reactionPickerButton:hover { background-color: #eaeef2; }
#messageAction {
    background: transparent; border: none; border-radius: 6px;
    color: #656d76; font-size: 11px; padding: 2px 7px;
}
#messageAction:hover { color: #1f2328; }
#messageAction::menu-indicator { image: none; width: 0; }
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
#emailVerifiedBadge {
    background-color: #dafbe1; border: 1px solid #2da44e; border-radius: 10px;
    color: #1a7f37; padding: 2px 8px; font-size: 11px; font-weight: 700;
}
#networkLog {
    background-color: #ffffff; border: none;
    color: #1f2328; font-family: monospace; font-size: 12px;
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
QScrollBar:horizontal {
    background: transparent; height: 10px; margin: 0;
}
QScrollBar::handle:horizontal {
    background: #d0d7de; border-radius: 5px; min-width: 30px;
}
QScrollBar::handle:horizontal:hover { background: #afb8c1; }
QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal { width: 0; }
QScrollBar::add-page:horizontal, QScrollBar::sub-page:horizontal { background: none; }

/* Draggable splitter handles — a clear, grabbable grip ridge that lights up
   on hover/drag. Edges are transparent so it blends with any panel color. */
QSplitter::handle:horizontal {
    width: 9px;
    background: qlineargradient(x1:0, y1:0, x2:1, y2:0,
        stop:0 transparent, stop:0.30 transparent,
        stop:0.34 #d0d7de, stop:0.5 #8c959f, stop:0.66 #d0d7de,
        stop:0.70 transparent, stop:1 transparent);
}
QSplitter::handle:vertical {
    height: 9px;
    background: qlineargradient(x1:0, y1:0, x2:0, y2:1,
        stop:0 transparent, stop:0.30 transparent,
        stop:0.34 #d0d7de, stop:0.5 #8c959f, stop:0.66 #d0d7de,
        stop:0.70 transparent, stop:1 transparent);
}
QSplitter::handle:horizontal:hover, QSplitter::handle:vertical:hover { background: #0969da; }
QSplitter::handle:pressed { background: #0969da; }

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
/* Shortcuts tab: each shortcut file is a clickable card (a QPushButton hosting
   its own labels). */
QPushButton#shortcutCard {
    background-color: #ffffff;
    border: 1px solid #d0d7de;
    border-radius: 8px;
    text-align: left;
}
QPushButton#shortcutCard:hover { border-color: #2ea043; background-color: #f6f8fa; }
#settingsTitle { font-size: 20px; font-weight: 800; }
#avatarPreview {
    background-color: #ffffff;
    border: 1px solid #d0d7de;
    border-radius: 12px;
    color: #6e7681;
    font-size: 11px;
}
#networkLog {
    background-color: #ffffff;
    border: none;
    color: #1f2328;
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
    min-height: 46px;
    max-height: 46px;
}
#issueQuickAdd:focus { border-color: #0969da; }
#promptWrapper {
    background-color: #ffffff; border: 1px solid rgba(26,127,55,0.5); border-radius: 8px;
}
#promptWrapper:focus-within { border-color: #1a7f37; }
#promptWrapper #issueQuickAdd {
    background: transparent; border: none; border-radius: 0;
    min-height: 46px; max-height: 46px;
}
#promptWrapper #issueQuickAdd:focus { border: none; }
QPushButton#quickAddSendIcon {
    background: transparent; border: none; color: #1a7f37;
    padding: 4px; border-radius: 4px;
}
QPushButton#quickAddSendIcon:hover { color: #1a7f37; background: rgba(26,127,55,0.12); }
/* Footer prompt bottom bar (adhoc #99): see the dark-theme block above for the
   rationale — green filled checkmark indicators plus a thin bordered Agent box. */
QCheckBox#quickAddAutoCheck::indicator,
QCheckBox#quickAddCreateIssueCheck::indicator,
QCheckBox#quickAddAgentCheck::indicator {
    width: 16px; height: 16px; border-radius: 4px;
    border: 2px solid #d0d7de; background: #ffffff;
}
QCheckBox#quickAddAutoCheck::indicator:checked,
QCheckBox#quickAddCreateIssueCheck::indicator:checked,
QCheckBox#quickAddAgentCheck::indicator:checked {
    border-color: #1a7f37; background: #1a7f37;
    image: url(:/icons/octicons/check-white.svg);
}
#quickAddAgentBox {
    border: none; background: transparent;
    font-size: 12px;
}
/* Slash-actions "/" box (adhoc #116): see the dark-theme block above for the
   rationale — a small bordered square left of the Agent checkbox. */
QPushButton#quickAddSlashButton {
    background: #ffffff; border: 1px solid #d0d7de; border-radius: 5px;
    color: #57606a; font-weight: 600; font-size: 12px; padding: 0;
}
QPushButton#quickAddSlashButton:hover { border-color: #0969da; color: #1f2328; }
#slashActionsPopup {
    background: #ffffff; border: 1px solid #d0d7de; border-radius: 8px;
}
QLineEdit#slashActionsFilter {
    background: transparent; border: none; border-bottom: 1px solid #d0d7de;
    border-radius: 0; padding: 10px 12px; color: #1f2328; font-size: 13px;
}
#slashActionsScroll { background: transparent; border: none; }
#slashActionsScroll > QWidget > QWidget { background: transparent; }
QLabel#slashActionsHeader {
    color: #6e7781; font-size: 11px; font-weight: 600;
    padding: 8px 12px 4px 12px;
}
QFrame#slashActionRow { background: transparent; border-radius: 6px; }
QFrame#slashActionRow[slashSelected="true"] { background: #eaeef2; }
QLabel#slashActionRowLabel { color: #1f2328; font-size: 13px; background: transparent; }
QLabel#slashActionRowValue { color: #6e7781; font-size: 12px; background: transparent; }
QToolButton#slashEffortDot {
    background: transparent; border: 2px solid #8c959f; border-radius: 5px;
}
QToolButton#slashEffortDot:checked { background: #1f2328; border-color: #1f2328; }
QCheckBox#slashToggle::indicator {
    width: 28px; height: 16px; border-radius: 8px; border: none; background: #d0d7de;
}
QCheckBox#slashToggle::indicator:checked { background: #1a7f37; }
/* "Agents:" status strip above the footer prompt (adhoc #111): a plain
   ghost-button label plus small borderless dot buttons, one per session. */
QPushButton#agentStatusLabel {
    background: transparent; border: none; color: #6e7781;
    font-weight: 600; font-size: 12px; padding: 2px 0;
}
QPushButton#agentStatusLabel:hover { color: #1f2328; }
QPushButton#agentStatusDot {
    background: transparent; border: none; padding: 0; border-radius: 3px;
}
QPushButton#agentStatusDot:hover { background: rgba(110,119,129,0.2); }
#agentStatusScroll { background: transparent; border: none; }
#agentStatusScroll > QWidget > QWidget { background: transparent; }
QPushButton#agentStatusFixButton {
    background: transparent; border: 1px solid #1a7f37; border-radius: 5px; padding: 0;
}
QPushButton#agentStatusFixButton:hover { background: rgba(26,127,55,0.15); }
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
#mentionPopup {
    background-color: #ffffff;
    color: #1f2328;
    border: 1px solid #d0d7de;
    border-radius: 6px;
    outline: 0;
}
#mentionPopup::item { padding: 4px 10px; }
#mentionPopup::item:selected { background-color: #0969da; color: #ffffff; }
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
#issueTable::item:hover { padding: 6px 8px; }
#issueTable::item:selected { background-color: #0969da; color: #ffffff; padding: 6px 8px; }
#issueTable QHeaderView::section {
    background-color: #ffffff;
    color: #656d76;
    padding: 6px 8px;
    border: none;
    border-bottom: 1px solid #d0d7de;
    border-right: 2px solid #afb8c1;
    font-weight: 700;
}
#issueTable QTableCornerButton::section {
    background-color: #ffffff;
    border: none;
    border-bottom: 1px solid #d0d7de;
}
#actionWorkflowList {
    background-color: #ffffff;
    alternate-background-color: #f6f8fa;
    border: 1px solid #d0d7de;
    border-radius: 8px;
    outline: 0;
    padding: 4px;
}
#actionWorkflowList::item {
    border-radius: 6px;
    color: #1f2328;
    padding: 7px 10px;
}
#actionWorkflowList::item:hover {
    background-color: #f6f8fa;
    color: #1f2328;
}
#actionWorkflowList::item:selected {
    background-color: #1f883d;
    color: #ffffff;
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
#pullReviewSummary {
    background-color: #f6f8fa;
    border: 1px solid #d0d7de;
    border-radius: 6px;
    color: #1f2328;
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

/* --- Item-view selection (see the dark theme for the rationale) ---
   border:0 stops the selected row's text from shifting; the highlight is the
   light-theme brand green (#1f883d, the primary-button green) instead of blue. */
QAbstractItemView::item { border: 0px; }
#issueTable { selection-background-color: #1f883d; selection-color: #ffffff; }
#issueTable::item:selected,
#overviewList::item:selected,
#fileTree::item:selected,
#commitsList::item:selected,
#sidebar QListWidget::item:selected { background-color: #1f883d; color: #ffffff; }
QComboBox QAbstractItemView { selection-background-color: #1f883d; }

/* Floating ▲/▼ jump-to-top/bottom buttons (ScrollJumpButtons), e.g. over the
   agent raw-output log. The rich transcript styles its own copy per scheme. */
QPushButton#scrollJump {
    background-color: #f6f8fa; color: #1f2328;
    border: 1px solid #d0d7de; border-radius: 15px;
    font-size: 12px; font-weight: 700;
}
QPushButton#scrollJump:hover { background-color: #eef1f5; }

)";

// The stylesheet matching the OS color scheme (dark by default).
inline const char *styleSheetForDark(bool dark)
{
    return dark ? kStyleSheet : kLightStyleSheet;
}

} // namespace Theme
