#pragma once

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
#fileTree::item:hover { background-color: #161b22; }
#fileTree::item:selected { background-color: #1f6feb; color: #ffffff; }
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
#commitBar { background-color: #161b22; border: 1px solid #30363d; border-radius: 6px; }
#commitBar QLabel { background: transparent; }
#commitBarText { color: #e6edf3; }
#overviewList { background-color: #0d1117; border: 1px solid #30363d; border-radius: 6px; }
#overviewList::item { padding: 6px 8px; color: #c9d1d9; }
#overviewList::item:hover { background-color: #161b22; }
#overviewList::item:selected { background-color: #1f6feb; color: #ffffff; }
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
#commitsList::item:hover { background-color: #161b22; }
#placeholderPanel { color: #e6edf3; font-size: 16px; }

/* --- Server favicon rail --- */
#serverRail { background-color: #010409; border-right: 1px solid #30363d; }
QPushButton#serverButton {
    background-color: #21262d; border: 2px solid transparent;
    border-radius: 12px; padding: 0;
}
QPushButton#serverButton:hover { border-color: #30363d; border-radius: 12px; }
QPushButton#serverButton:checked { border-color: #2ea043; border-radius: 12px; }
QPushButton#serverAddButton {
    background-color: #0d1117; border: 1px dashed #30363d;
    border-radius: 12px; color: #8b949e; font-size: 20px; font-weight: 700;
}
QPushButton#serverAddButton:hover { color: #2ea043; border-color: #2ea043; }
QPushButton#serverFooterButton {
    background: transparent; border: none; color: #8b949e; font-size: 18px;
    border-radius: 8px;
}
QPushButton#serverFooterButton:hover { background-color: #161b22; color: #e6edf3; }

/* --- Breadcrumb bar --- */
#breadcrumbBar { background-color: #0d1117; border-bottom: 1px solid #21262d; }
#breadcrumb { background: transparent; font-size: 12px; font-weight: 600; }

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
#sidebar QListWidget::item:hover { background-color: #161b22; }
#sidebar QListWidget::item:selected {
    background-color: #1f6feb; color: #ffffff;
}
#issueQuickAdd {
    background-color: #0d1117; border: 1px solid #30363d;
    border-radius: 6px; padding: 8px 10px; font-size: 13px;
}
#issueQuickAdd:focus { border-color: #58a6ff; }

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
#bchBanner {
    background-color: #12261a;
    border-bottom: 1px solid #2ea043;
}
#bchBannerLabel {
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
#fileTree::item:hover { background-color: #f6f8fa; }
#fileTree::item:selected { background-color: #0969da; color: #ffffff; }
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
#commitBar { background-color: #f6f8fa; border: 1px solid #d0d7de; border-radius: 6px; }
#commitBar QLabel { background: transparent; }
#commitBarText { color: #1f2328; }
#overviewList { background-color: #ffffff; border: 1px solid #d0d7de; border-radius: 6px; }
#overviewList::item { padding: 6px 8px; color: #1f2328; }
#overviewList::item:hover { background-color: #f6f8fa; }
#overviewList::item:selected { background-color: #0969da; color: #ffffff; }
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
#commitsList::item:hover { background-color: #f6f8fa; }
#placeholderPanel { color: #1f2328; font-size: 16px; }

/* --- Server favicon rail --- */
#serverRail { background-color: #f6f8fa; border-right: 1px solid #d0d7de; }
QPushButton#serverButton {
    background-color: #ffffff; border: 2px solid transparent;
    border-radius: 12px; padding: 0;
}
QPushButton#serverButton:hover { border-color: #d0d7de; border-radius: 12px; }
QPushButton#serverButton:checked { border-color: #1f883d; border-radius: 12px; }
QPushButton#serverAddButton {
    background-color: #ffffff; border: 1px dashed #d0d7de;
    border-radius: 12px; color: #656d76; font-size: 20px; font-weight: 700;
}
QPushButton#serverAddButton:hover { color: #1f883d; border-color: #1f883d; }
QPushButton#serverFooterButton {
    background: transparent; border: none; color: #656d76; font-size: 18px;
    border-radius: 8px;
}
QPushButton#serverFooterButton:hover { background-color: #eaeef2; color: #1f2328; }

/* --- Breadcrumb bar --- */
#breadcrumbBar { background-color: #ffffff; border-bottom: 1px solid #d8dee4; }
#breadcrumb { background: transparent; color: #656d76; font-size: 12px; font-weight: 600; }

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
#sidebar QListWidget::item:hover { background-color: #f6f8fa; }
#sidebar QListWidget::item:selected {
    background-color: #0969da; color: #ffffff;
}
#issueQuickAdd {
    background-color: #ffffff; border: 1px solid #d0d7de;
    border-radius: 6px; padding: 8px 10px; font-size: 13px;
}
#issueQuickAdd:focus { border-color: #0969da; }

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
#bchBanner {
    background-color: #dafbe1;
    border-bottom: 1px solid #1f883d;
}
#bchBannerLabel {
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
)";

// The stylesheet matching the OS color scheme (dark by default).
inline const char *styleSheetForDark(bool dark)
{
    return dark ? kStyleSheet : kLightStyleSheet;
}

} // namespace Theme
