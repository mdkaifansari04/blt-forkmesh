#pragma once

// Internal UI helper layer for MainWindow: custom delegates, mini-chart and
// spinner widgets, syntax highlighters, the code-preview editor, and the many
// free helper functions/constants the window's feature code shares. Lifted out
// of MainWindow.cpp (which was ~54k lines) so each piece is navigable and so
// the per-feature MainWindow*.cpp translation units can share it. Everything
// lives in namespace forkmesh::ui; MainWindow.cpp does `using namespace`.

#include "MainWindow.h"
#include "TerminalWidget.h"

#include "ActionFile.h"
#include "ActionRunner.h"
#include "BackgroundActivity.h"
#include "BackoffNetworkAccessManager.h"
#include "ClaudeAgentScript.h"
#include "ClaudeIdeBridge.h"
#include "ClaudeStreamSession.h"
#include "ClaudeTranscriptView.h"
#include "ScrollJumpButtons.h"
#include "DirectorySizeScan.h"
#include "StallWatchdog.h"
#include "IssueBurnup.h"
#include "QrCode.h"
#include "ReferenceLinks.h"

#include "MarkdownEditor.h"
#include "MessageRow.h"
#include "MainnodeRoom.h"
#include "PullReviewModel.h"
#include "RepoHost.h"
#include "RepoSecurity.h"
#include "ServerNode.h"
#include "SingleInstance.h"
#include "SystemStats.h"
#include "AgentStore.h"
#include "Theme.h"

#include <QAbstractButton>
#include <QAction>
#include <QApplication>
#include <QBuffer>
#include <QButtonGroup>
#include <QCheckBox>
#include <QClipboard>
#include <QCursor>
#include <QCloseEvent>
#include <QCryptographicHash>
#include <QComboBox>
#include <QCompleter>
#include <QCoreApplication>
#include <QDate>
#include <QDateTime>
#include <QLocale>
#include <QDesktopServices>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QFile>
#include <QFileDialog>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileInfo>
#include <QMimeData>
#include <QMutex>
#include <QMutexLocker>
#include <QFontDatabase>
#include <QFormLayout>
#include <QFrame>
#include <QGraphicsOpacityEffect>
#include <QGridLayout>
#include <QGroupBox>
#include <QGuiApplication>
#include <QStandardPaths>
#include <QStorageInfo>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QWidgetAction>
#include <QEnterEvent>
#include <QMessageBox>
#include <QContextMenuEvent>
#include <QMimeDatabase>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSslSocket>
#include <QSslError>
#include <QImage>
#include <QImageReader>
#include <QKeyEvent>
#include <QHelpEvent>
#include <QLineF>
#include <QToolTip>
#include <QConicalGradient>
#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
#include <QPolygonF>
#include <QRadialGradient>
#include <QRadioButton>
#include <QShortcut>
#include <QTreeWidgetItem>
#include <QtMath>
#include <QPlainTextEdit>
#include <QPointer>
#include <QPropertyAnimation>
#include <QRandomGenerator>
#include <QEventLoop>
#include <QFileSystemWatcher>
#include <QProcess>
#include <QProcessEnvironment>
#include <QProgressBar>
#include <QTextCursor>
#include <QTimeZone>
#include <QPushButton>
#include <QRegularExpression>
#include <QRegularExpressionValidator>
#include <QSaveFile>
#include <QScreen>
#include <QScopedValueRollback>
#include <QScrollArea>
#include <QSet>
#include <QScrollBar>
#include <QSettings>
#include <QSignalBlocker>
#include <QSize>
#include <QSysInfo>
#include <QSplitter>
#include <QSpinBox>
#include <QStackedWidget>
#include <QStringList>
#include <QStringListModel>
#include <QStyle>
#include <QStyledItemDelegate>
#include <QStyleHints>
#include <QStyleOptionComboBox>
#include <QSyntaxHighlighter>
#include <QAbstractItemView>
#include <QHeaderView>
#include <QTableWidget>
#include <QTabWidget>
#include <QTextBrowser>
#include <QTextBlock>
#include <QTextDocument>
#include <QTextEdit>
#include <QTemporaryDir>
#include <QTemporaryFile>
#include <QToolButton>
#include <QTreeWidget>
#include <QSystemTrayIcon>
#include <QSvgRenderer>
#include <QScopeGuard>
#include <QThread>
#include <QTimer>
#include <QUrl>
#include <QUrlQuery>
#include <QUuid>
#include <QVector>
#include <QWindow>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <memory>
#include <optional>

#ifndef Q_OS_WIN
#include <csignal>
#include <pwd.h>
#include <sys/types.h>
#include <unistd.h>
#endif

#ifndef FORKMESH_VERSION
#define FORKMESH_VERSION "dev"
#endif
#ifndef FORKMESH_SOURCE_DIR
#define FORKMESH_SOURCE_DIR ""
#endif

namespace forkmesh::ui {

class WindowChromeBar : public QWidget
{
public:
    explicit WindowChromeBar(QWidget *parent = nullptr) : QWidget(parent)
    {
        setObjectName(QStringLiteral("windowChromeBar"));
        setFixedHeight(42);
    }

protected:
    void mousePressEvent(QMouseEvent *event) override
    {
        if (event->button() == Qt::LeftButton) {
            if (QWindow *handle = window()->windowHandle())
                handle->startSystemMove();
            event->accept();
            return;
        }
        QWidget::mousePressEvent(event);
    }

    void mouseDoubleClickEvent(QMouseEvent *event) override
    {
        if (event->button() == Qt::LeftButton) {
            QWidget *top = window();
            if (top->isMaximized())
                top->showNormal();
            else
                top->showMaximized();
            event->accept();
            return;
        }
        QWidget::mouseDoubleClickEvent(event);
    }
};

// Cross-region free helpers shared by several MainWindow feature .cpp files.
// Defined in MainWindowShared.cpp.
QString openAiAuthHeader(const QString &apiKey);
QNetworkRequest openAiRequest(const QUrl &url, const QString &apiKey);
QString apiErrorSummary(QNetworkReply *reply, const QByteArray &body);
QString openAiResponseText(const QJsonObject &obj);
double openAiAskCostUsd(const QJsonObject &response, qint64 *inTokens = nullptr,
                        qint64 *outTokens = nullptr);
QString mirrorHeadBranch(const QString &mirrorPath);
QString mirrorBranchCommit(const QString &mirrorPath, const QString &branch);
struct MirrorBranchTip {
    QString branch;
    QString commit;
};
MirrorBranchTip mirrorPrimaryBranchTip(const QString &mirrorPath,
                                       const QString &workTree);
QString actionStatusText(const QString &status);
QColor actionStatusColor(const QString &status);
void logStartup(const QString &phase);
void beginRestartLog();
void logRestart(const QString &phase);


// --- Shared display helpers: diff rendering, agent status, reference links,
// and SCM AI models. Defined in MainWindowShared.cpp; used by several panels.
struct DiffFileEntry {
    QString path;
    QString anchor;
    int adds = 0;
    int dels = 0;
    QString status = QStringLiteral("modified"); // added/deleted/modified/renamed
    // A binary file (`git diff --binary` emits a "GIT binary patch" literal/delta
    // block, or a plain "Binary files … differ" line): its payload is not a text
    // diff, so the renderers show a placeholder row and the header shows "BIN"
    // instead of +/- counts.
    bool binary = false;
};
QString diffStyleSheet(int fontPt = 12);
QString renderDiffHtml(const QString &patch, QList<DiffFileEntry> &files,
                       const QString &dir, const QString &base, const QString &head,
                       const QString &anchorFile = QString(),
                       const QHash<QString, QString> &lineNotes = {},
                       const QSet<QString> &viewedFiles = {});
bool diffSplitPref();
void setDiffSplitPref(bool split);
// Compact rich-text label (status octicon + muted dir / bold name + coloured
// +adds/-dels) for a changed file, used by the PR review page's sticky header
// overlay (adhoc #56). Unlike diffFileHeaderHtml this carries no Viewed toggle
// or table layout — it renders inline in a QLabel.
QString diffStickyLabelHtml(const DiffFileEntry &f);
// Progressive diff rendering (adhoc #421). QTextEdit::setHtml() parses, styles
// and lays out the whole document synchronously on the GUI thread, so handing it
// a multi-megabyte diff froze the window — which is why large diffs used to be
// replaced by a "hidden for speed" notice. These split rendered HTML into
// per-file blocks, bound pathological rich-text tables (the full patch remains
// in the PR/Git data), and lay out only the first screenful up front. Remaining
// bounded blocks stream one event-loop turn at a time.
void renderDiffStreamed(QTextEdit *view, const QString &html,
                        const QString &styleSheet);
// Force everything still queued for `view` into its document now. Call before an
// operation that needs the whole document (an anchor jump, a document-wide
// search, a file-position scan) rather than only what is on screen.
void flushDiffStream(QTextEdit *view);
// Register a callback run every time `view`'s diff finishes streaming (and
// immediately at the end of a render that needed no streaming), for state that
// is derived from the complete document. Hooks are additive: register once per
// owner, at construction.
void addDiffStreamFinishedHook(QTextEdit *view, std::function<void()> hook);
bool autoMarkViewedOnScrollPref();
void setAutoMarkViewedOnScrollPref(bool on);
// Paint find-in-diff matches as extra selections (active match brighter) and
// update the "n/m" count label. Shared by the PR and branch/PR-range find bars.
void applyDiffSearchHighlights(QTextBrowser *diff,
                               const QList<QTextCursor> &matches, int activeIndex,
                               QLabel *countLabel, bool termEmpty);
QString diffStickyStyleSheet(int fontPt);
QString diffStickyPathHtml(const QString &path);
QString agentCostText(double usd);
QString agentStatusText(const QString &status);
QColor agentStatusColor(const QString &status);
bool agentSessionActive(const AgentSession *s);
QString solanaDisplayCurrency();
QIcon agentStatusOcticon(const AgentSession &s, int px = 13);
QColor agentStatusIconColor(const AgentSession &s);
QString linkifyIssueRefs(const QString &escaped);
QString linkifyReferenceLine(const QString &line);
class DiffFileNavigator : public QObject
{
public:
    DiffFileNavigator(QTextBrowser *diff, QListWidget *list, int anchorRole,
                      QObject *parent)
        : QObject(parent), m_diff(diff), m_list(list), m_anchorRole(anchorRole)
    {
        m_sticky = new QLabel(m_diff->viewport());
        m_sticky->setObjectName(QStringLiteral("diffStickyHeader"));
        m_sticky->setTextFormat(Qt::RichText);
        m_sticky->hide();
        QObject::connect(m_diff->verticalScrollBar(), &QScrollBar::valueChanged,
                         this, [this] {
                             if (!m_ignoreScroll)
                                 refresh(/*syncSelection=*/true);
                         });
        QObject::connect(m_list, &QListWidget::currentItemChanged, this,
                         [this](QListWidgetItem *it, QListWidgetItem *) {
                             if (!it)
                                 return;
                             const QString anchor = it->data(m_anchorRole).toString();
                             if (anchor.isEmpty())
                                 return;
                             // Jump the diff to the file's header, aligned to the top.
                             // Suppress the scroll that fires so it can't re-select.
                             // The target file may still be queued behind the
                             // visible window, so land the whole diff first.
                             m_ignoreScroll = true;
                             flushDiffStream(m_diff);
                             m_diff->scrollToAnchor(anchor);
                             m_ignoreScroll = false;
                             refresh(/*syncSelection=*/false);
                         });
        // A streamed diff only holds the visible window's files right after a
        // render; recompute the spans once the rest has landed (adhoc #421).
        addDiffStreamFinishedHook(m_diff, [this] { rebuildSpans(); });
    }

    // Recompute the file-header positions after the diff HTML was (re)rendered.
    // `files` is the same in-order list used to fill the file list, so anchors map
    // a span back to its row.
    void rebuild(const QList<DiffFileEntry> &files, int fontPt)
    {
        m_files = files;
        m_sticky->setStyleSheet(diffStickyStyleSheet(fontPt));
        rebuildSpans();
    }

private:
    struct Span {
        int pos;
        QString path;
        QString anchor;
    };

    // Walk the document's blocks (cheap and layout-free) for each file's header
    // position. Covers whatever is currently in the document: re-run from the
    // stream-finished hook once a streamed diff is complete.
    void rebuildSpans()
    {
        m_spans.clear();
        QTextDocument *doc = m_diff->document();
        int idx = 0;
        for (QTextBlock b = doc->begin(); b.isValid() && idx < m_files.size();
             b = b.next()) {
            const int at = b.text().indexOf(m_files.at(idx).path);
            if (at >= 0) {
                m_spans.append({b.position() + at, m_files.at(idx).path,
                                m_files.at(idx).anchor});
                ++idx;
            }
        }
        refresh(/*syncSelection=*/false);
    }

    void refresh(bool syncSelection)
    {
        if (m_spans.isEmpty() || m_diff->verticalScrollBar()->value() <= 0) {
            m_sticky->hide();
            return;
        }
        const int top = m_diff->cursorForPosition(QPoint(2, 2)).position();
        const Span *cur = nullptr;
        for (const Span &s : m_spans) {
            if (s.pos <= top)
                cur = &s;
            else
                break;
        }
        if (!cur) {
            m_sticky->hide();
            return;
        }
        if (syncSelection)
            selectByAnchor(cur->anchor);
        m_sticky->setText(diffStickyPathHtml(cur->path));
        m_sticky->setGeometry(0, 0, m_diff->viewport()->width(),
                              m_sticky->sizeHint().height());
        m_sticky->show();
        m_sticky->raise();
    }

    void selectByAnchor(const QString &anchor)
    {
        for (int i = 0; i < m_list->count(); ++i) {
            if (m_list->item(i)->data(m_anchorRole).toString() != anchor)
                continue;
            if (m_list->currentRow() != i) {
                QSignalBlocker block(m_list);
                m_list->setCurrentRow(i);
            }
            return;
        }
    }

    QTextBrowser *m_diff = nullptr;
    QListWidget *m_list = nullptr;
    QLabel *m_sticky = nullptr;
    int m_anchorRole = Qt::UserRole;
    bool m_ignoreScroll = false;
    QList<DiffFileEntry> m_files;
    QList<Span> m_spans;
};
// Models offered for inline commit-message / X-post generation, with per-million
// token pricing so the realised cost can be shown after each call.
struct ScmAiModel {
    const char *provider; // "claude" | "openai"
    const char *id;
    const char *label;
    double inPerM;
    double outPerM;
    bool estimated; // pricing is approximate (OpenAI)
};
const ScmAiModel kScmAiModels[] = {
    {"claude", "claude-opus-4-8", "Claude Opus 4.8", 5.0, 25.0, false},
    {"claude", "claude-sonnet-4-6", "Claude Sonnet 4.6", 3.0, 15.0, false},
    {"claude", "claude-haiku-4-5", "Claude Haiku 4.5", 1.0, 5.0, false},
    {"openai", "gpt-4.1", "OpenAI GPT-4.1", 2.0, 8.0, true},
    {"openai", "gpt-4.1-mini", "OpenAI GPT-4.1 mini", 0.40, 1.60, true},
    {"openai", "gpt-4.1-nano", "OpenAI GPT-4.1 nano", 0.10, 0.40, true},
};
const int kScmAiModelCount = int(sizeof(kScmAiModels) / sizeof(kScmAiModels[0]));

constexpr int kTableSortRole = Qt::UserRole + 10;
// Per-cell percentage (0..100) read by ProgressBarDelegate to draw a mini bar.
constexpr int kProgressBarRole = Qt::UserRole + 11;
// Last-sync timestamp (qint64 ms) for a behind-but-online mirror node, read by
// MirrorSyncDelegate to draw a pac-man countdown to its next heartbeat/re-sync.
constexpr int kPacmanAnchorRole = Qt::UserRole + 12;
// Status light on a Mirror-nodes row's Node cell (adhoc #230): 0 = steady lamp
// (all green, or grey offline), 1 = caution (spinning orange), 2 = error
// (spinning red). MainWindow::animateMirrorNodeLights re-renders non-zero rows.
constexpr int kNodeLightRole = Qt::UserRole + 13;
// Cadence on which a node re-fetches its mirrors from source (mirrors
// m_mirrorSyncTimer, which adds ±15% jitter — the pie is an approximation);
// a behind node is expected to catch up within roughly one minute. This is
// only the dropped-event safety net: push events still notify mirror peers the
// moment the source moves.
constexpr qint64 kMirrorSyncIntervalMs = 60LL * 1000;
constexpr int kMirrorSyncJitterPercent = 15;

// Extra labels this machine answers to when a workflow declares `runs-on:`
// (free-form, comma/space separated — e.g. "ios, xcode, gpu"). The machine's
// node name, its mirror-executor node name and the platform are always labels;
// this setting only adds capability tags on top of them.
constexpr auto kActionNodeLabelsSetting = "actions/nodeLabels";
// Defined further down; used early by MirrorSyncDelegate to pick chart colors.
bool currentThemeIsDark();
// Defined with the rest of the icon helpers further down; commit ref badges use
// it before that definition while painting the graph rows.
inline QIcon themedOcticon(const QString &name, const QColor &color, int size);

// Column in the commits list that carries the Summary text + the commit hash
// (Qt::UserRole). The metadata columns sit to its left.
constexpr int kCommitSummaryCol = 6;
// Column showing the short commit hash (also flags unsynced commits).
constexpr int kCommitHashCol = 2;
// Trailing column carrying the per-row "delete from history" button. Only the
// node holding the working copy (the source of truth) can act on it.
constexpr int kCommitActionCol = 7;
// Leftmost gutter that paints the commit graph (lanes + node dot). It is the
// last logical column but is moved to visual position 0 so the existing column
// indices above stay unchanged.
constexpr int kCommitGraphCol = 8;
// Per-row graph data read by CommitGraphDelegate. Kept above kTableSortRole's
// neighbours (UserRole+10) to avoid clashing with the sort key.
constexpr int kGraphLanesRole = Qt::UserRole + 20;    // QVariantList<int> lanes at the row's top edge
constexpr int kGraphNodeLaneRole = Qt::UserRole + 21; // int lane of this commit's dot
constexpr int kGraphBottomLanesRole =
    Qt::UserRole + 22; // QVariantList<int> lanes at the row's bottom edge
// VS-Code-style commit rows: a commit row expands in place to show the files it
// touched. These roles live on the Summary item and drive CommitSummaryDelegate.
constexpr int kCommitRowKindRole = Qt::UserRole + 23; // 0 = commit, 1 = file child row
constexpr int kCommitExpandedRole = Qt::UserRole + 24; // bool: commit row is expanded
constexpr int kCommitAuthorRole = Qt::UserRole + 25;   // author drawn right of the summary
constexpr int kCommitUnsyncedRole = Qt::UserRole + 26; // bool: not yet on the mirror
constexpr int kCommitFileAddsRole = Qt::UserRole + 27; // file row: added lines
constexpr int kCommitFileDelsRole = Qt::UserRole + 28; // file row: deleted lines
constexpr int kCommitFilePathRole = Qt::UserRole + 29; // file row: repo-relative path
constexpr int kCommitRefsRole = Qt::UserRole + 30;     // branch/tag badges (QStringList)
constexpr int kCommitBodyRole = Qt::UserRole + 31;     // full message body (fed to the hover box)
constexpr int kGraphIsMergeRole = Qt::UserRole + 32;   // graph cell: commit has >1 parent
constexpr int kCommitFilesRole =
    Qt::UserRole + 33; // QStringList "path\tadds\tdels" of the files the commit
                       // touched, previewed in the summary's hover box
constexpr int kCommitRefKindsRole =
    Qt::UserRole + 34; // QStringList aligned with kCommitRefsRole: local/remote/tag

// URL scheme for a clickable branch-name link; the percent-encoded branch name
// follows. Clicking it opens that branch's row in the Branches tab (adhoc #123).
// Shared by the link builder and its handler.
const QLatin1String kBranchLinkScheme("forkmesh-branch:");

// "forkmesh-pull:<number>" link in the agent-detail meta line: when a session
// has a pull request, its "PR #N" reference links to that PR's tab. Shared by
// the link builder and its linkActivated handler.
const QLatin1String kPullLinkScheme("forkmesh-pull:");

// "forkmesh-issue:<number>" link in the agent-detail meta line: when a session
// was started from an issue, its "#N" reference links to that issue's tab in the
// session's repo (adhoc #138). Shared by the link builder and its handler.
const QLatin1String kIssueLinkScheme("forkmesh-issue:");

// HTML for a branch name that, when clicked, opens that branch's row in the
// Branches tab (handlers route kBranchLinkScheme -> MainWindow::switchToBranch).
// Shared across the agent header, the pull-request header and anywhere else a
// branch name is shown, so "click a branch anywhere → open it in Branches" works
// uniformly (issue #204). Plain (un-escaped) when there's no branch.
inline QString branchLinkHtml(const QString &branch)
{
    if (branch.isEmpty())
        return QString();
    const QString href = kBranchLinkScheme +
                         QString::fromUtf8(QUrl::toPercentEncoding(branch));
    return QStringLiteral(
               "<a href=\"%1\" style=\"color:#58a6ff;text-decoration:none\">%2</a>")
        .arg(href, branch.toHtmlEscaped());
}

// Per-repository about/catalog metadata lives under ForkMesh's own metadata dir
// instead of the project root.
const QLatin1String kRepoInfoPath(".forkmesh/info.json");

// "forkmesh-agent:<sessionId>" link in the PR-detail meta line: when an agent
// session produced a pull request, the header links back to that session on the
// Agents tab (adhoc #78). Shared by the link builder and its linkActivated handler.
const QLatin1String kAgentLinkScheme("forkmesh-agent:");

// Lane geometry, shared between the column-width calc and the delegate so the
// dots line up with the section width.
constexpr int kGraphLaneWidth = 12;
constexpr int kGraphMargin = 8;
// Cap on how far a row's text can be pushed right by a very wide graph, so a
// deep merge history can't shove the messages off-screen.
constexpr int kGraphMaxTextIndent = 160;
// Commit node is drawn as a "bullseye": a hollow ring with a filled centre,
// matching the VS Code git-graph look. Slightly larger than before so the
// nodes read as clear anchors; lane lines stop at the ring's edge on merge
// rows so the background shows through the ring/centre-dot gap.
constexpr qreal kGraphNodeOuter = 4.5; // outer ring radius
constexpr qreal kGraphNodeInner = 2.0; // centre-dot radius

// Stable per-lane colour so a branch keeps its hue down the whole graph.
// Blue leads so the trunk lane (main) draws blue, like the VS Code graph.
inline QColor commitGraphLaneColor(int lane)
{
    static const QColor palette[] = {
        QColor("#58a6ff"), QColor("#d29922"), QColor("#db61a2"),
        QColor("#bc8cff"), QColor("#39c5cf"), QColor("#3fb950"),
    };
    constexpr int n = int(sizeof(palette) / sizeof(palette[0]));
    return palette[((lane % n) + n) % n];
}

// Defined below: outlines a selected row in green instead of filling it solid.
inline void paintRowSelectionBorder(QPainter *painter,
                                    const QStyleOptionViewItem &option,
                                    const QModelIndex &index);

// Paints the git-graph gutter the way the VS Code git-graph view does: lanes
// that pass straight through a row are drawn as vertical lines, while a lane
// that merges into the commit (or branches out of it) loops through a rounded
// quarter-circle corner — a horizontal run along the node's centreline joined
// to a vertical run in its own lane. Merge commits draw as a bullseye (hollow
// ring with a filled centre), regular commits as a solid dot. Each row carries
// the lanes present at its top and bottom edges; comparing the two boundaries
// tells us which lanes pass through, merge in, or branch out. Topology is
// meaningful only while the list is in git-log order, which is why that
// ordering is pinned when the list loads. Called by CommitSummaryDelegate
// inside the summary cell (the standalone gutter column is hidden) so each
// row's text can start right beside its own rightmost lane.
inline void paintCommitGraphGutter(QPainter *painter, const QRect &r,
                                   const QVariantList &topLanes,
                                   const QVariantList &botLanes, int nodeLane,
                                   bool isMerge)
{
    if (topLanes.isEmpty() && botLanes.isEmpty() && nodeLane < 0)
        return;
    const qreal yTop = r.top();
    const qreal yBot = r.top() + r.height(); // meets the next row's top edge
    const qreal yMid = r.center().y() + 0.5;
    auto laneX = [&](int lane) -> qreal {
        return r.left() + kGraphMargin + lane * kGraphLaneWidth;
    };
    // Which lane columns are occupied at each edge of the row.
    QSet<int> topSet;
    QSet<int> botSet;
    int maxLane = nodeLane;
    for (const QVariant &v : topLanes) {
        const int l = v.toInt();
        topSet.insert(l);
        maxLane = std::max(maxLane, l);
    }
    for (const QVariant &v : botLanes) {
        const int l = v.toInt();
        botSet.insert(l);
        maxLane = std::max(maxLane, l);
    }

    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);

    // On merge rows the lines stop short of the node by the ring radius, so
    // the hollow ring keeps a clean background gap around its centre dot
    // instead of lane strokes cutting through it.
    const qreal trim = (isMerge && nodeLane >= 0) ? kGraphNodeOuter : 0.0;

    // Round caps/joins keep the lanes and their loops smooth where they meet
    // nodes and each other.
    auto strokePath = [&](const QPainterPath &path, const QColor &c) {
        QPen pen(c, 2.0);
        pen.setCapStyle(Qt::RoundCap);
        pen.setJoinStyle(Qt::RoundJoin);
        painter->setPen(pen);
        painter->setBrush(Qt::NoBrush);
        painter->drawPath(path);
    };
    auto straight = [&](qreal x, qreal y0, qreal y1, const QColor &c) {
        QPainterPath path(QPointF(x, y0));
        path.lineTo(QPointF(x, y1));
        strokePath(path, c);
    };
    // A lane looping into the node from the row's top edge: vertical in its
    // own lane, then a rounded quarter-circle corner onto the node's
    // centreline — the smooth "loop" the VS Code graph draws for merges.
    auto loopIn = [&](int lane, const QColor &c) {
        const qreal x0 = laneX(lane);
        const qreal x1 = laneX(nodeLane);
        const qreal rad = qMax(0.0, qMin(qAbs(x1 - x0) - trim, yMid - yTop));
        const qreal sx = (x1 > x0) ? 1.0 : -1.0;
        QPainterPath path(QPointF(x0, yTop));
        path.lineTo(QPointF(x0, yMid - rad));
        path.quadTo(QPointF(x0, yMid), QPointF(x0 + sx * rad, yMid));
        path.lineTo(QPointF(x1 - sx * trim, yMid));
        strokePath(path, c);
    };
    // A lane looping out of the node towards the row's bottom edge: horizontal
    // along the centreline, then the rounded corner down into its own lane.
    auto loopOut = [&](int lane, const QColor &c) {
        const qreal x0 = laneX(nodeLane);
        const qreal x1 = laneX(lane);
        const qreal rad = qMax(0.0, qMin(qAbs(x1 - x0) - trim, yBot - yMid));
        const qreal sx = (x1 > x0) ? 1.0 : -1.0;
        QPainterPath path(QPointF(x0 + sx * trim, yMid));
        path.lineTo(QPointF(x1 - sx * rad, yMid));
        path.quadTo(QPointF(x1, yMid), QPointF(x1, yMid + rad));
        path.lineTo(QPointF(x1, yBot));
        strokePath(path, c);
    };

    // Every lane other than the node's: straight through if present at both
    // edges, a merge loop if it only enters from the top, a branch loop if it
    // only leaves at the bottom. Rows without a node (expanded file rows) only
    // carry pass-through lanes; anything else degrades to a straight stub.
    for (int lane = 0; lane <= maxLane; ++lane) {
        if (lane == nodeLane)
            continue;
        const bool inTop = topSet.contains(lane);
        const bool inBot = botSet.contains(lane);
        const QColor c = commitGraphLaneColor(lane);
        if (inTop && inBot)
            straight(laneX(lane), yTop, yBot, c);
        else if (inTop)
            nodeLane >= 0 ? loopIn(lane, c) : straight(laneX(lane), yTop, yMid, c);
        else if (inBot)
            nodeLane >= 0 ? loopOut(lane, c) : straight(laneX(lane), yMid, yBot, c);
    }

    if (nodeLane >= 0) {
        const QColor c = commitGraphLaneColor(nodeLane);
        const qreal nx = laneX(nodeLane);
        // The node's own lane: a straight stub above (it was reached from a
        // child) and below (its first parent continues here), trimmed at the
        // ring's edge on merge rows so the ring interior stays clear.
        if (topSet.contains(nodeLane))
            straight(nx, yTop, yMid - trim, c);
        if (botSet.contains(nodeLane))
            straight(nx, yMid + trim, yBot, c);
        if (isMerge) {
            // Merge node: hollow ring + filled centre.
            painter->setBrush(Qt::NoBrush);
            painter->setPen(QPen(c, 2.0));
            painter->drawEllipse(QPointF(nx, yMid), kGraphNodeOuter, kGraphNodeOuter);
            painter->setPen(Qt::NoPen);
            painter->setBrush(c);
            painter->drawEllipse(QPointF(nx, yMid), kGraphNodeInner, kGraphNodeInner);
        } else {
            // Regular commit: a solid dot.
            painter->setPen(Qt::NoPen);
            painter->setBrush(c);
            painter->drawEllipse(QPointF(nx, yMid), kGraphNodeOuter - 0.7,
                                 kGraphNodeOuter - 0.7);
        }
    }
    painter->restore();
}

// Paints the commits list's Summary column the way VS Code's source-control
// graph does: the text starts right beside the commit's own lane (so it shifts
// with the graph), a chevron flags that the row expands into its files, the
// author sits dimmed at the right edge, and expanded file rows show their
// per-file +/− counts. All the metadata that used to live in table columns
// (author / date / hash / files / adds / dels) now rides the item's tooltip.
class CommitSummaryDelegate : public QStyledItemDelegate
{
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    void paint(QPainter *painter, const QStyleOptionViewItem &option,
               const QModelIndex &index) const override
    {
        // Background only (no default text/icon): strip the selection band the
        // same way HoverRowDelegate does, then draw the green outline on top.
        QStyleOptionViewItem opt(option);
        initStyleOption(&opt, index);
        opt.text.clear();
        opt.icon = QIcon();
        opt.features &= ~QStyleOptionViewItem::HasDecoration;
        opt.state &= ~(QStyle::State_MouseOver | QStyle::State_Selected);
        const QWidget *w = option.widget;
        QStyle *style = w ? w->style() : QApplication::style();
        style->drawControl(QStyle::CE_ItemViewItem, &opt, painter, w);
        paintRowSelectionBorder(painter, option, index);

        const bool fileRow = index.data(kCommitRowKindRole).toInt() == 1;

        // The graph is painted here, inside the summary cell (its standalone
        // gutter column is hidden): that lets each row's text start right
        // beside its own rightmost lane — the VS Code graph look — instead of
        // after a shared fixed-width gutter (adhoc #74).
        const QModelIndex graphIdx =
            index.model()->index(index.row(), kCommitGraphCol);
        const QVariantList topLanes = graphIdx.data(kGraphLanesRole).toList();
        const QVariantList botLanes =
            graphIdx.data(kGraphBottomLanesRole).toList();
        const QVariant nodeLaneVar = graphIdx.data(kGraphNodeLaneRole);
        const int nodeLane = nodeLaneVar.isValid() ? nodeLaneVar.toInt() : -1;
        paintCommitGraphGutter(painter, option.rect, topLanes, botLanes,
                               nodeLane,
                               graphIdx.data(kGraphIsMergeRole).toBool());
        int rowMaxLane = std::max(nodeLane, 0);
        for (const QVariant &v : topLanes)
            rowMaxLane = std::max(rowMaxLane, v.toInt());
        for (const QVariant &v : botLanes)
            rowMaxLane = std::max(rowMaxLane, v.toInt());
        if (fileRow) // nested one step under its commit's lane
            rowMaxLane =
                std::max(rowMaxLane, index.data(kGraphNodeLaneRole).toInt());
        const int indent =
            std::min(kGraphMargin + (rowMaxLane + 1) * kGraphLaneWidth,
                     kGraphMaxTextIndent);

        const QFontMetrics fm(option.font);
        QRect r = option.rect.adjusted(indent, 0, -8, 0);
        const QColor dim("#8b949e");

        painter->save();
        if (fileRow) {
            r.adjust(18, 0, 0, 0); // nest files under their commit
            const int adds = index.data(kCommitFileAddsRole).toInt();
            const int dels = index.data(kCommitFileDelsRole).toInt();
            const QString addsTxt = QStringLiteral("+%1").arg(adds);
            const QString delsTxt =
                QString::fromUtf8("\xE2\x88\x92%1").arg(dels);
            const int delsW = fm.horizontalAdvance(delsTxt);
            const int addsW = fm.horizontalAdvance(addsTxt);
            painter->setPen(QColor("#f85149"));
            painter->drawText(QRect(r.right() - delsW, r.top(), delsW, r.height()),
                              Qt::AlignVCenter | Qt::AlignRight, delsTxt);
            painter->setPen(QColor("#2ea043"));
            painter->drawText(
                QRect(r.right() - delsW - 6 - addsW, r.top(), addsW, r.height()),
                Qt::AlignVCenter | Qt::AlignRight, addsTxt);
            // File-type icon leading the name, like the VS Code graph's rows.
            int fx = r.left();
            const QIcon fic = qvariant_cast<QIcon>(index.data(Qt::DecorationRole));
            if (!fic.isNull()) {
                fic.paint(painter, QRect(fx, r.center().y() - 7, 14, 14));
                fx += 18;
            }
            const int textW = r.right() - fx - delsW - addsW - 20;
            painter->setPen(dim);
            painter->drawText(
                QRect(fx, r.top(), std::max(0, textW), r.height()),
                Qt::AlignVCenter | Qt::AlignLeft,
                fm.elidedText(index.data(Qt::DisplayRole).toString(),
                              Qt::ElideMiddle, std::max(0, textW)));
            painter->restore();
            return;
        }

        // Commit row: checks icon + summary, author (and the amber unsynced
        // marker) right-aligned. No disclosure chevron — the VS Code graph
        // keeps rows plain; clicking a row still expands it into its files,
        // so the text starts right beside the commit's own node.
        int x = r.left();
        const QIcon icon = qvariant_cast<QIcon>(index.data(Qt::DecorationRole));
        if (!icon.isNull()) {
            const QRect ir(x, r.center().y() - 7, 14, 14);
            icon.paint(painter, ir);
            x += 18;
        }
        int rightEdge = r.right();
        // Branch / tag badges (the VS Code graph's ref pills) lead the summary.
        const QStringList refs = index.data(kCommitRefsRole).toStringList();
        const QStringList refKinds =
            index.data(kCommitRefKindsRole).toStringList();
        if (!refs.isEmpty()) {
            painter->setRenderHint(QPainter::Antialiasing, true);
            for (int refIndex = 0; refIndex < refs.size(); ++refIndex) {
                const QString ref = refs.at(refIndex);
                const QString kind = refKinds.value(refIndex);
                const bool remote = kind == QLatin1String("remote");
                const bool tag = kind == QLatin1String("tag");
                const QString iconName = remote ? QStringLiteral("cloud")
                                                : tag ? QStringLiteral("tag")
                                                      : QStringLiteral("git-commit");
                const QColor badgeColor =
                    remote ? QColor("#8957e5")
                           : tag ? QColor("#2da44e") : QColor("#1f6feb");
                constexpr int iconSize = 12;
                constexpr int iconGap = 4;
                const int rw = fm.horizontalAdvance(ref) + 12 + iconSize + iconGap;
                if (x + rw > rightEdge - 80)
                    break; // keep room for the summary itself
                const QRect br(x, r.center().y() - fm.height() / 2 - 1, rw,
                               fm.height() + 2);
                // Local heads carry the target/commit glyph, remote-tracking
                // heads carry the cloud glyph, and tags retain their own mark.
                // Blue/purple/green match the graph/ref palette in the adjacent
                // VS Code view and make local vs published state readable before
                // the ref text itself is parsed.
                painter->setPen(Qt::NoPen);
                painter->setBrush(badgeColor);
                painter->drawRoundedRect(br, br.height() / 2.0,
                                         br.height() / 2.0);
                const QRect iconRect(br.left() + 6,
                                     br.center().y() - iconSize / 2,
                                     iconSize, iconSize);
                themedOcticon(iconName, Qt::white, iconSize)
                    .paint(painter, iconRect);
                painter->setPen(Qt::white);
                painter->drawText(
                    br.adjusted(6 + iconSize + iconGap, 0, -6, 0),
                    Qt::AlignVCenter | Qt::AlignLeft, ref);
                x += rw + 5;
            }
            painter->setBrush(Qt::NoBrush);
        }
        if (index.data(kCommitUnsyncedRole).toBool()) {
            const QString mark = QString::fromUtf8("\xE2\x96\xB2");
            const int mw = fm.horizontalAdvance(mark);
            painter->setPen(QColor("#d29922"));
            painter->drawText(QRect(rightEdge - mw, r.top(), mw, r.height()),
                              Qt::AlignVCenter | Qt::AlignRight, mark);
            rightEdge -= mw + 8;
        }
        // Draw the subject flush-left, then append the author dimmed at its tail
        // so the row reads "<subject> · <author>" instead of a separate
        // right-aligned author column. The full commit message gets the width
        // first; the username only takes whatever room is left after it, so a
        // long subject is never truncated just to reserve space for the author
        // (issue #52).
        const QVariant fgVar = index.data(Qt::ForegroundRole);
        const QColor fg = fgVar.isValid()
                              ? qvariant_cast<QBrush>(fgVar).color()
                              : option.palette.color(QPalette::Text);
        const int textW = std::max(0, rightEdge - x);
        const QString subject = index.data(Qt::DisplayRole).toString();
        const QString author = index.data(kCommitAuthorRole).toString();
        const QString suffix =
            author.isEmpty() ? QString()
                             : QString::fromUtf8("  \xC2\xB7  ") + author;
        const QString elidedSubject =
            fm.elidedText(subject, Qt::ElideRight, textW);
        const int subjectW = fm.horizontalAdvance(elidedSubject);
        painter->setPen(fg);
        painter->drawText(QRect(x, r.top(), subjectW, r.height()),
                          Qt::AlignVCenter | Qt::AlignLeft, elidedSubject);
        if (!suffix.isEmpty()) {
            const int rem = std::max(0, textW - subjectW);
            painter->setPen(dim);
            painter->drawText(
                QRect(x + subjectW, r.top(), rem, r.height()),
                Qt::AlignVCenter | Qt::AlignLeft,
                fm.elidedText(suffix, Qt::ElideRight, rem));
        }
        painter->restore();
    }
};

class SortTableWidgetItem : public QTableWidgetItem
{
public:
    using QTableWidgetItem::QTableWidgetItem;

    bool operator<(const QTableWidgetItem &other) const override
    {
        const QVariant left = data(kTableSortRole);
        const QVariant right = other.data(kTableSortRole);
        if (left.isValid() && right.isValid()) {
            bool leftOk = false;
            bool rightOk = false;
            const double leftNumber = left.toDouble(&leftOk);
            const double rightNumber = right.toDouble(&rightOk);
            if (leftOk && rightOk)
                return leftNumber < rightNumber;
            return left.toString().compare(right.toString(), Qt::CaseInsensitive) < 0;
        }
        return QTableWidgetItem::operator<(other);
    }
};

// Deterministic, pleasant bar colour for a contributor name. Hashed over the
// UTF-8 bytes (not qHash, which is per-process randomised for QString) so a
// contributor keeps the same hue across runs.
static QColor insightContributorColor(const QString &name)
{
    quint32 h = 2166136261u; // FNV-1a
    for (const char c : name.toUtf8())
        h = (h ^ static_cast<quint8>(c)) * 16777619u;
    return QColor::fromHsv(int(h % 360u), 150, 205);
}

// A compact vertical-bar chart of one contributor's commits over time: one bar
// per time bucket, scaled to a maximum shared across the Insights "Commit
// activity" list so volume stays comparable between contributors.
class CommitBarChart : public QWidget
{
public:
    CommitBarChart(QVector<int> counts, int sharedMax, const QColor &color,
                   QWidget *parent = nullptr)
        : QWidget(parent), m_counts(std::move(counts)),
          m_max(qMax(1, sharedMax)), m_color(color)
    {
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        setMinimumHeight(30);
    }

protected:
    QSize sizeHint() const override { return QSize(240, 30); }

    void paintEvent(QPaintEvent *) override
    {
        if (m_counts.isEmpty())
            return;
        QPainter p(this);
        const int n = m_counts.size();
        const qreal slot = qreal(width()) / n;
        const int barW = qMax(1, int(slot) - 2);
        const int h = height();
        // Faint baseline so quiet stretches still read as a timeline.
        p.fillRect(0, h - 1, width(), 1, QColor(255, 255, 255, 28));
        for (int i = 0; i < n; ++i) {
            const int c = m_counts.at(i);
            if (c <= 0)
                continue;
            const int bh = qMax(2, qRound(qreal(c) / m_max * (h - 2)));
            p.fillRect(QRectF(i * slot, h - bh, barW, bh), m_color);
        }
    }

private:
    QVector<int> m_counts;
    int m_max;
    QColor m_color;
};

// A super-tiny usage meter for the top bar, sized to tuck in next to the
// node's public-wallet balance/avatar (issue #266). One thin vertical bar per
// rolling window — 5-hour, weekly, and (Claude only, adhoc #96) the premium
// per-model weekly window we label "Fable" — each an empty track that fills
// 0..100% of that window's utilisation and is tinted green/amber/red as it nears
// the cap. Values are fed from Claude Code rate-limit events (see usageChanged);
// a value of -1 means "unknown" and leaves an empty track. Stored as a plain
// QWidget* on MainWindow and poked via static_cast, like ProgressSlider.
class TokenUsageMiniChart : public QWidget
{
public:
    // Which rolling window a figure belongs to. Fable is the per-model weekly
    // allowance the provider reports alongside the plan-wide one; charts built
    // with `windows == 2` (Codex) simply never show it.
    enum Window { FiveHour = 0, Weekly = 1, Fable = 2, WindowCount = 3 };

    explicit TokenUsageMiniChart(const QString &title =
                                     QStringLiteral("Claude Code usage"),
                                 bool remainingMode = false,
                                 int windows = 2,
                                 QWidget *parent = nullptr)
        : QWidget(parent), m_title(title), m_remainingMode(remainingMode),
          m_windows(qBound(1, windows, int(WindowCount)))
    {
        setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
        // Thin vertical bars that ride in the prompt toolbar (adhoc #47). No
        // inline text — the label/figures live in the hover tooltip only, so the
        // strip stays tiny next to the send buttons. The 3px padding around the
        // bars is what the hover refresh box is drawn in (adhoc #96).
        setFixedSize(qRound(m_windows * kBarW + (m_windows - 1) * kGap) + 6, 24);
        refreshTooltip();
    }

    // Update one window's utilisation (0..100); pass -1 to mark it unknown.
    void setUsage(Window window, int percent)
    {
        int &slot = m_pct[window];
        const int clamped = percent < 0 ? -1 : qBound(0, percent, 100);
        if (slot == clamped)
            return;
        slot = clamped;
        refreshTooltip();
        update();
    }
    void setUsage(bool weekly, int percent)
    {
        setUsage(weekly ? Weekly : FiveHour, percent);
    }

    // Update one window's "resets in ..." text (e.g. "2h 13m"), shown next to its
    // utilisation in the tooltip so the user can see how long until the limit
    // clears (issue #50). Pass an empty string to mark it unknown.
    void setReset(Window window, const QString &remaining)
    {
        setWindowNote(window,
                      remaining.isEmpty()
                          ? QString()
                          : QStringLiteral("resets in %1").arg(remaining));
    }
    void setReset(bool weekly, const QString &remaining)
    {
        setReset(weekly ? Weekly : FiveHour, remaining);
    }

    void setWindowNote(Window window, const QString &note)
    {
        QString &slot = m_note[window];
        if (slot == note)
            return;
        slot = note;
        refreshTooltip();
    }
    void setWindowNote(bool weekly, const QString &note)
    {
        setWindowNote(weekly ? Weekly : FiveHour, note);
    }

    // For Codex we do not get a live utilization percentage from the CLI today,
    // so the top bar shows the rolling-window time remaining that ForkMesh
    // already tracks when Codex sessions start.
    void setRemaining(bool weekly, int percent, const QString &note)
    {
        setUsage(weekly, percent);
        setWindowNote(weekly, note);
    }

    // Hover feedback (adhoc #96): flash a box around the bars — green when the
    // hover pulled a fresh reading, red when the refresh failed — so a hover
    // that leaves the figures unchanged still says whether it worked.
    void flashRefresh(bool ok)
    {
        m_flash = ok ? 1 : -1;
        update();
        const int token = ++m_flashToken;
        QTimer::singleShot(kFlashMs, this, [this, token] {
            if (token != m_flashToken) // a newer flash owns the box now
                return;
            m_flash = 0;
            update();
        });
    }

    // The per-session token/cost detail that used to live on the agent detail
    // page (issue #84): shown in the hover tooltip below the 5h/weekly figures.
    // Pass an empty string to drop it (e.g. when no session is selected).
    void setStats(const QString &stats)
    {
        if (m_stats == stats)
            return;
        m_stats = stats;
        refreshTooltip();
    }

    // There's no background timer pulling fresh usage anymore (it only refreshes
    // right after a prompt is sent), so the figures under the mouse can be stale
    // by the time the user actually looks at them. Fire this on hover to pull a
    // fresh reading on demand instead.
    std::function<void()> onHover;

protected:
    void enterEvent(QEnterEvent *) override
    {
        if (onHover)
            onHover();
    }
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);

        // Vertical gauges side by side, in Window order: 5-hour, weekly, Fable.
        // Each is an empty track filling from the bottom to its utilisation and
        // tinted by barColor(); -1 (unknown) leaves it empty.
        const qreal totalW = m_windows * kBarW + (m_windows - 1) * kGap;
        qreal x = (width() - totalW) / 2.0;
        const qreal top = 3.0;
        const qreal trackH = height() - 6.0;
        for (int i = 0; i < m_windows; ++i) {
            const QRectF track(x, top, kBarW, trackH);
            p.setPen(Qt::NoPen);
            p.setBrush(textColor(38));
            p.drawRoundedRect(track, kBarW / 2.0, kBarW / 2.0);
            if (m_pct[i] > 0) {
                const qreal fillH = trackH * qBound(0, m_pct[i], 100) / 100.0;
                const QRectF fill(x, top + trackH - fillH, kBarW, fillH);
                p.setBrush(barColor(m_pct[i]));
                p.drawRoundedRect(fill, kBarW / 2.0, kBarW / 2.0);
            }
            x += kBarW + kGap;
        }

        // The refresh-result box (adhoc #96) rides in the padding around the
        // bars, so it never overdraws a gauge.
        if (m_flash != 0) {
            QPen pen(m_flash > 0 ? QColor("#3fb950") : QColor("#f85149"));
            pen.setWidthF(1.5);
            p.setPen(pen);
            p.setBrush(Qt::NoBrush);
            p.drawRoundedRect(QRectF(rect()).adjusted(0.75, 0.75, -0.75, -0.75),
                              4.0, 4.0);
        }
    }

private:
    QColor textColor(int alpha) const
    {
        QColor c = palette().color(QPalette::WindowText);
        c.setAlpha(alpha);
        return c;
    }
    QColor barColor(int pct) const
    {
        if (m_remainingMode) {
            if (pct <= 10)
                return QColor("#f85149"); // red: nearly out of window
            if (pct <= 30)
                return QColor("#d29922"); // amber: low remaining time
            return QColor("#3fb950");     // green: plenty remaining
        }
        if (pct >= 90)
            return QColor("#f85149"); // red: near the cap
        if (pct >= 70)
            return QColor("#d29922"); // amber: getting close
        return QColor("#3fb950");     // green: plenty left
    }
    void refreshTooltip()
    {
        auto line = [this](const QString &label, int v, const QString &note) {
            const QString value =
                v < 0 ? QString::fromUtf8("\xE2\x80\x94") // em dash
                      : QStringLiteral("%1%").arg(v);
            QString s = m_remainingMode
                            ? QStringLiteral("%1 remaining: %2").arg(label, value)
                            : QStringLiteral("%1: %2").arg(label, value);
            if (!note.isEmpty())
                s += QString::fromUtf8(" \xC2\xB7 ") + note; // ·
            return s;
        };
        static const char *labels[WindowCount] = {"5-hour", "Weekly", "Fable"};
        QString tip = m_title;
        for (int i = 0; i < m_windows; ++i)
            tip += QLatin1Char('\n')
                   + line(QString::fromLatin1(labels[i]), m_pct[i], m_note[i]);
        if (!m_stats.isEmpty())
            tip += QStringLiteral("\n\n") + m_stats;
        setToolTip(tip);
    }

    static constexpr qreal kBarW = 4.0;
    static constexpr qreal kGap = 3.0;
    static constexpr int kFlashMs = 900; // how long the hover box stays up

    QString m_title;
    bool m_remainingMode = false;
    int m_windows = 2;              // how many of the Window slots are drawn
    int m_pct[WindowCount] = {-1, -1, -1};
    QString m_note[WindowCount];    // extra tooltip text per window
    QString m_stats; // per-session token/cost line, shown under the gauges
    int m_flash = 0;     // 0 = none, 1 = refreshed (green), -1 = failed (red)
    int m_flashToken = 0; // guards against an older flash clearing a newer one
};

// A tiny moving line chart for one system resource (CPU, memory or disk). New
// per-second samples push in from the right and scroll the history left, so the
// recent load is visible at a glance; the current figure prints on its own
// line under the label. Replaces the static "CPU x% MEM y MB" footer text
// (adhoc #17). Kept header-only (no Q_OBJECT) like the other Internal.h mini-
// charts; the click hook is a std::function so a left-click can still open
// the stall dialog.
class ResourceSparkline : public QWidget
{
public:
    explicit ResourceSparkline(const QString &label, QWidget *parent = nullptr,
                               int side = 34, int maxPoints = 60)
        : QWidget(parent), m_label(label), m_maxPoints(qMax(2, maxPoints))
    {
        setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
        setFixedSize(side, side); // a little button-sized square
        setCursor(Qt::PointingHandCursor);
    }

    // Append one reading. `value` is plotted on a fixed 0..`maxValue` scale so
    // the curve's height is comparable across samples (auto-scaling would turn a
    // near-flat disk trace into noise); `valueText` is the figure shown beside
    // the label.
    void addSample(double value, double maxValue, const QString &valueText)
    {
        m_max = maxValue > 0 ? maxValue : 100.0;
        m_value = valueText;
        m_history.append(value);
        while (m_history.size() > m_maxPoints)
            m_history.removeFirst();
        update();
    }

    void setSamples(const QVector<double> &values, double maxValue,
                    const QString &valueText)
    {
        m_max = maxValue > 0 ? maxValue : 1.0;
        m_value = valueText;
        m_history = values.mid(qMax(0, values.size() - m_maxPoints));
        update();
    }

    std::function<void()> onClicked; // invoked on a left-click

protected:
    void mousePressEvent(QMouseEvent *e) override
    {
        if (e->button() == Qt::LeftButton && onClicked)
            onClicked();
        QWidget::mousePressEvent(e);
    }

    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);

        // Rounded card that doubles as the sparkline's full-height track, so the
        // curve reads as a background layer and the label/value sit over it.
        const QRectF box = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
        QPainterPath cardPath;
        cardPath.addRoundedRect(box, 4, 4);
        QColor card = palette().color(QPalette::WindowText);
        card.setAlpha(28);
        p.setPen(Qt::NoPen);
        p.setBrush(card);
        p.drawPath(cardPath);

        // The sparkline fills the whole card (adhoc #46), with the most recent
        // sample at its right edge so the curve scrolls left over time. Clipped
        // to the rounded card so the fill/line never spill past the corners.
        const QRectF area = box.adjusted(1.5, 1.5, -1.5, -1.5);
        if (area.height() >= 2 && m_history.size() >= 2) {
            p.save();
            p.setClipPath(cardPath);
            const QColor line = gaugeColor(m_history.last() / m_max * 100.0);
            const double step = area.width() / double(m_maxPoints - 1);
            const int n = m_history.size();
            QPolygonF curve;
            for (int i = 0; i < n; ++i) {
                const double x = area.right() - (n - 1 - i) * step;
                const double norm = qBound(0.0, m_history.at(i) / m_max, 1.0);
                curve << QPointF(x, area.bottom() - norm * area.height());
            }
            QPolygonF fill = curve;
            fill << QPointF(curve.last().x(), area.bottom())
                 << QPointF(curve.first().x(), area.bottom());
            QColor under = line;
            under.setAlpha(70);
            p.setBrush(under);
            p.setPen(Qt::NoPen);
            p.drawPolygon(fill);
            QPen pen(line);
            pen.setWidthF(1.2);
            p.setPen(pen);
            p.setBrush(Qt::NoBrush);
            p.drawPolyline(curve);
            p.restore();
        }

        // A header font that shrinks until the wider of the label/value lines
        // fits, so neither is clipped however the app's base font is sized.
        QFont f = font();
        double pt = f.pointSizeF() > 0 ? qMin(8.0, f.pointSizeF()) : 7.0;
        const double avail = width() - 6;
        for (; pt > 5.5; pt -= 0.5) {
            f.setPointSizeF(pt);
            const QFontMetrics fm(f);
            if (fm.horizontalAdvance(m_label) <= avail &&
                fm.horizontalAdvance(m_value) <= avail)
                break;
        }
        f.setPointSizeF(pt);
        p.setFont(f);
        const QFontMetrics fm(f);
        const int lineH = fm.height();

        // Label + value overlaid on the chart: the resource label and its value
        // stack on centered lines (e.g. "CPU" then "9%"), vertically centered
        // and given a mild opacity so the curve stays visible behind them.
        const double topY = (height() - lineH * 2) / 2.0;
        QColor lab = palette().color(QPalette::WindowText);
        lab.setAlpha(170);
        p.setPen(lab);
        p.drawText(QRectF(3, topY, width() - 6, lineH),
                   Qt::AlignVCenter | Qt::AlignHCenter, m_label);
        const double lastPct =
            m_history.isEmpty() ? 0.0 : m_history.last() / m_max * 100.0;
        QColor val = gaugeColor(lastPct);
        val.setAlpha(210);
        p.setPen(val);
        p.drawText(QRectF(3, topY + lineH, width() - 6, lineH),
                   Qt::AlignVCenter | Qt::AlignHCenter, m_value);
    }

private:
    static QColor gaugeColor(double pct)
    {
        if (pct >= 90)
            return QColor("#f85149"); // red: pegged
        if (pct >= 70)
            return QColor("#d29922"); // amber: getting busy
        return QColor("#3fb950");     // green: light load
    }

    QString m_label;
    QString m_value;
    double m_max = 100.0;
    QVector<double> m_history;
    int m_maxPoints = 60;
};

// A row-sized memory trend square for one process in the "High memory usage"
// panel (adhoc #98). Each refresh of that panel pushes the process's resident
// size in, so a row shows at a glance whether that PID is still growing or has
// levelled off. Unlike ResourceSparkline it plots on a caller-supplied scale
// shared by every row (the largest resident size on the list), so the squares
// are comparable down the column, and it carries no label — the numbers are
// already in the neighbouring cells.
class ProcessMemorySparkline : public QWidget
{
public:
    static constexpr int kMaxPoints = 24; // ~2 minutes at the panel's 5s refresh

    explicit ProcessMemorySparkline(QWidget *parent = nullptr) : QWidget(parent)
    {
        setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
        setFixedSize(kSide, kSide);
        setAttribute(Qt::WA_TransparentForMouseEvents);
    }

    // `history` is oldest-to-newest resident sizes in KB; `maxValue` is the
    // shared full-scale value for the column.
    void setHistory(const QVector<double> &history, double maxValue)
    {
        m_history = history;
        m_max = maxValue > 0 ? maxValue : 1.0;
        update();
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        const QRectF box = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
        QPainterPath cardPath;
        cardPath.addRoundedRect(box, 3, 3);
        QColor card = palette().color(QPalette::WindowText);
        card.setAlpha(28);
        p.setPen(Qt::NoPen);
        p.setBrush(card);
        p.drawPath(cardPath);
        const QRectF area = box.adjusted(1.5, 1.5, -1.5, -1.5);
        if (m_history.isEmpty() || area.height() < 2 || area.width() < 2)
            return;

        // A single sample is still worth drawing — a flat line at that level
        // says the process was only just seen.
        p.save();
        p.setClipPath(cardPath);
        const double norm = qBound(0.0, m_history.last() / m_max, 1.0);
        const QColor line = norm >= 0.66   ? QColor("#f85149")
                            : norm >= 0.33 ? QColor("#d29922")
                                           : QColor("#3fb950");
        const int n = m_history.size();
        const double step = area.width() / double(kMaxPoints - 1);
        QPolygonF curve;
        for (int i = 0; i < n; ++i) {
            const double x =
                n > 1 ? area.right() - (n - 1 - i) * step : area.left();
            const double v = qBound(0.0, m_history.at(i) / m_max, 1.0);
            curve << QPointF(x, area.bottom() - v * area.height());
        }
        if (n == 1)
            curve << QPointF(area.right(), curve.first().y());
        QPolygonF fill = curve;
        fill << QPointF(curve.last().x(), area.bottom())
             << QPointF(curve.first().x(), area.bottom());
        QColor under = line;
        under.setAlpha(70);
        p.setBrush(under);
        p.setPen(Qt::NoPen);
        p.drawPolygon(fill);
        QPen pen(line);
        pen.setWidthF(1.2);
        p.setPen(pen);
        p.setBrush(Qt::NoBrush);
        p.drawPolyline(curve);
        p.restore();
    }

private:
    static constexpr int kSide = 34; // fits a table row without growing it
    QVector<double> m_history;
    double m_max = 1.0;
};

// The relay link's speed as a single coloured dot pinned above the instance
// logo on the window-chrome line (adhoc #124). It is what is left of the
// spinning radar dish that used to sit beside the CPU/MEM/DISK sparklines
// (adhoc #87): the dish's node blips became the node dots next to the agent
// fleet, and its latency grading survives here as the dot's colour — green when
// the link is snappy, amber when it is sluggish, red when it is very slow or
// the relay stopped answering. The measured round-trip itself rides the
// instance button's tooltip and the relay dropdown, so the chrome line stays
// free of another number.
class RelaySpeedDot : public QWidget
{
public:
    explicit RelaySpeedDot(QWidget *parent = nullptr) : QWidget(parent)
    {
        setFixedSize(kSide, kSide);
        // Clicks belong to the logo underneath: the dot is pure indicator, so
        // pressing it still opens the relay switcher.
        setAttribute(Qt::WA_TransparentForMouseEvents);
        if (parent)
            parent->installEventFilter(this);
        reposition();
    }

    // Record a successful probe (round-trip milliseconds).
    void setLatency(int ms)
    {
        ms = qMax(0, ms);
        if (!m_unreachable && m_latencyMs == ms)
            return;
        m_latencyMs = ms;
        m_unreachable = false;
        update();
    }

    // The relay failed to answer the last probe: show the red alert.
    void setUnreachable()
    {
        if (m_unreachable)
            return;
        m_unreachable = true;
        update();
    }

    int latencyMs() const { return m_latencyMs; }
    bool unreachable() const { return m_unreachable; }

    // Green when snappy, amber when sluggish, red when very slow — the same
    // grading the dish used, so the colours mean exactly what they used to.
    // A latency of -1 (nothing measured yet) grades as unknown.
    static QColor speedColor(int ms, bool unreachable)
    {
        if (unreachable)
            return QColor("#f85149"); // red: not answering
        if (ms < 0)
            return QColor("#8b949e"); // grey: still measuring
        if (ms >= 1000)
            return QColor("#f85149"); // red
        if (ms >= 300)
            return QColor("#d29922"); // amber
        return QColor("#3fb950");     // green
    }

protected:
    bool eventFilter(QObject *watched, QEvent *event) override
    {
        if (watched == parentWidget() && (event->type() == QEvent::Resize ||
                                          event->type() == QEvent::Show))
            reposition();
        return QWidget::eventFilter(watched, event);
    }

    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        const QColor colour = speedColor(m_latencyMs, m_unreachable);

        // Soft halo so the dot reads against the favicon it sits over, then the
        // dot itself ringed in the chrome background (same treatment as the
        // avatar's connection dot).
        QColor halo = colour;
        halo.setAlpha(60);
        p.setPen(Qt::NoPen);
        p.setBrush(halo);
        p.drawEllipse(QRectF(rect()));

        p.setPen(QPen(palette().color(QPalette::Window), 1.0));
        p.setBrush(colour);
        p.drawEllipse(QRectF(rect()).adjusted(1.5, 1.5, -1.5, -1.5));
    }

private:
    // Centred on the logo's top edge, so it reads as a status light above the
    // instance rather than a badge on one of its corners (the pending-join dot
    // already owns the top-right corner).
    void reposition()
    {
        if (QWidget *owner = parentWidget())
            move(qMax(0, (owner->width() - width()) / 2), 0);
    }

    static constexpr int kSide = 10;
    int m_latencyMs = -1;       // last measured round-trip; -1 = unknown/probing
    bool m_unreachable = false; // relay failed to answer the last probe
};

// A matrix of tiny squares on the window-chrome line, one per agent session,
// sitting immediately right of the "Agents (N)" button. Each square is painted
// in the same colour as that session's status icon in the agents list, so the
// whole fleet reads at a glance: green running/done, red failed, amber queued,
// purple merged, grey cleared.
//
// Running sessions get the night-rider treatment the agents list used to give its
// (now dropped) Activity column: a Larson highlight travels along the matrix and
// each running square pulses at a speed and brightness driven by how hard that
// session is working, so a busy agent visibly races while a quiet one just
// breathes. The animation timer only runs while something is actually running.
class AgentDotMatrix : public QWidget
{
public:
    struct Dot {
        int sessionId = 0;
        QColor color;
        bool running = false;
        double intensity = 0.0; // 0..1 live-output (bytes) meter
        // 0..1 token-throughput meter: the session's tok/s scaled against a
        // flat-out run (adhoc #35). Volume of raw output alone made a session
        // chewing through a big file look as busy as one actually generating, so
        // the blink now takes the token rate into account as well.
        double throughput = 0.0;
    };

    explicit AgentDotMatrix(QWidget *parent = nullptr) : QWidget(parent)
    {
        setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
        setFixedHeight(kRows * kPitch);
        setFixedWidth(0); // nothing to show until the first setDots()
        setCursor(Qt::PointingHandCursor);
        hide();
        m_sweep = new QTimer(this);
        m_sweep->setInterval(60);
        connect(m_sweep, &QTimer::timeout, this, [this] {
            m_phase += 0.045;
            if (m_phase >= 1.0)
                m_phase -= 1.0;
            update();
        });
    }

    // Replace the fleet. Anything past the visible grid is dropped from the
    // paint (the caller folds the remainder into the tooltip), so the matrix
    // can never grow the chrome line without bound.
    void setDots(const QVector<Dot> &dots)
    {
        m_dots = dots.mid(0, kRows * kMaxColumns);
        const int columns = (m_dots.size() + kRows - 1) / kRows;
        setFixedWidth(columns * kPitch);
        bool anyRunning = false;
        for (const Dot &d : std::as_const(m_dots))
            anyRunning = anyRunning || d.running;
        if (anyRunning && !m_sweep->isActive())
            m_sweep->start();
        else if (!anyRunning && m_sweep->isActive())
            m_sweep->stop();
        update();
    }

    // How many of the dots handed to setDots() actually fit in the grid, so the
    // caller can say "showing the first N" instead of silently truncating.
    int shownCount() const { return m_dots.size(); }

    // Clicking a square opens that session; clicking the empty space around
    // them falls back to session id 0 (the agents overview).
    std::function<void(int)> onDotClicked;

protected:
    void mousePressEvent(QMouseEvent *e) override
    {
        if (e->button() == Qt::LeftButton && onDotClicked) {
            const int index = dotAt(e->position().toPoint());
            onDotClicked(index >= 0 ? m_dots.at(index).sessionId : 0);
            // Accept it: the window-chrome bar under this widget turns an
            // unhandled press into a system window-move, so letting the click
            // fall through would drag the window every time a square is opened.
            e->accept();
            return;
        }
        QWidget::mousePressEvent(e);
    }

    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        p.setPen(Qt::NoPen);
        for (int i = 0; i < m_dots.size(); ++i) {
            const Dot &dot = m_dots.at(i);
            QColor color = dot.color;
            double scale = 1.0;
            if (dot.running) {
                // The highlight travels along the matrix (each square is offset
                // a little further through the cycle), and how hard the session is
                // working both speeds up its cycle and deepens the pulse — that's
                // the "live activity" part: idle running agents breathe slowly and
                // dimly, streaming ones strobe. "Working" is the stronger of the
                // raw-output meter and the token throughput (adhoc #35), so an
                // agent producing fast still races while it thinks between chunks.
                const double meter = qMax(qBound(0.0, dot.intensity, 1.0),
                                          qBound(0.0, dot.throughput, 1.0));
                const double speed = 0.6 + 1.9 * meter;
                double t = m_phase * speed + i * kSweepStep;
                t -= std::floor(t);
                const double tri = 1.0 - std::abs(2.0 * t - 1.0);
                const double depth = 0.45 + 0.35 * meter;
                const double glow = (1.0 - depth) + depth * tri;
                color.setAlphaF(qBound(0.18, glow, 1.0));
                scale = 0.82 + 0.18 * tri; // the crest swells a touch
            } else {
                color.setAlpha(205);
            }
            const QPointF center = cellCenter(i);
            const double side = kSide * scale;
            p.setBrush(color);
            p.drawRoundedRect(
                QRectF(center.x() - side / 2.0, center.y() - side / 2.0, side,
                       side),
                1.2, 1.2);
        }
    }

private:
    // Column-major fill, so the fleet grows to the right in tidy columns of
    // kRows rather than reflowing every square when one agent is added.
    QPointF cellCenter(int index) const
    {
        const int column = index / kRows;
        const int row = index % kRows;
        return QPointF(column * kPitch + kPitch / 2.0,
                       row * kPitch + kPitch / 2.0);
    }

    int dotAt(const QPoint &pos) const
    {
        const int column = pos.x() / kPitch;
        const int row = pos.y() / kPitch;
        if (column < 0 || row < 0 || row >= kRows)
            return -1;
        const int index = column * kRows + row;
        return index < m_dots.size() ? index : -1;
    }

    static constexpr int kRows = 3;        // squares stacked per column
    static constexpr int kPitch = 7;       // cell size, including its gap
    static constexpr double kSide = 4.5;   // painted square
    static constexpr int kMaxColumns = 22; // ~66 agents before the tooltip takes over
    static constexpr double kSweepStep = 0.06; // per-square offset of the sweep

    QVector<Dot> m_dots;
    double m_phase = 0.0;      // 0..1 Larson sweep parameter
    QTimer *m_sweep = nullptr; // only ticks while something is running
};

// The mesh companion to the fleet matrix (adhoc #124): one dot per node on the
// network, stacked three deep in the same 3x7 grid the agent squares use and
// sitting immediately right of them behind a faint divider, so one glance at
// the chrome line covers both the agents and the machines. This is where the
// relay radar's blips went when the dish was retired — same roster, same
// colours (green serving, amber out of sync or failing an integrity gate, grey
// offline) — except the dots are always the whole mesh rather than only the
// repo whose Mirror-nodes tab happens to be open. Nodes are drawn as circles
// where agents are rounded squares, so the two groups stay tellable apart.
class NodeDotMatrix : public QWidget
{
public:
    struct Dot {
        QString name;
        QColor color;
        bool self = false; // this machine, ringed so it's findable
    };

    explicit NodeDotMatrix(QWidget *parent = nullptr) : QWidget(parent)
    {
        setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
        setFixedHeight(kRows * kPitch);
        setFixedWidth(0); // nothing to show until the first setDots()
        setCursor(Qt::PointingHandCursor);
        hide();
    }

    // Replace the mesh. Anything past the visible grid is dropped from the
    // paint (the caller folds the remainder into the tooltip and puts the
    // online nodes first), so the matrix can never grow the chrome line
    // without bound.
    void setDots(const QVector<Dot> &dots)
    {
        m_dots = dots.mid(0, kRows * kMaxColumns);
        const int columns = (m_dots.size() + kRows - 1) / kRows;
        setFixedWidth(columns * kPitch);
        update();
    }

    // How many of the dots handed to setDots() actually fit in the grid.
    int shownCount() const { return m_dots.size(); }

    // Clicking a dot opens that node in the Network > Nodes list; clicking the
    // empty space around them falls back to the list itself (empty name).
    std::function<void(const QString &)> onDotClicked;

protected:
    void mousePressEvent(QMouseEvent *e) override
    {
        if (e->button() == Qt::LeftButton && onDotClicked) {
            const int index = dotAt(e->position().toPoint());
            onDotClicked(index >= 0 ? m_dots.at(index).name : QString());
            // Same reason as AgentDotMatrix: an unhandled press on the
            // window-chrome bar below turns into a system window-move.
            e->accept();
            return;
        }
        QWidget::mousePressEvent(e);
    }

    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        for (int i = 0; i < m_dots.size(); ++i) {
            const Dot &dot = m_dots.at(i);
            QColor color = dot.color;
            color.setAlpha(205); // same weight as an idle agent square
            const QPointF center = cellCenter(i);
            p.setBrush(color);
            p.setPen(Qt::NoPen);
            p.drawEllipse(center, kRadius, kRadius);
            if (dot.self) {
                // A thin ring marks this machine among its peers.
                QColor ring = palette().color(QPalette::WindowText);
                ring.setAlpha(190);
                p.setBrush(Qt::NoBrush);
                p.setPen(QPen(ring, 0.9));
                p.drawEllipse(center, kRadius + 1.0, kRadius + 1.0);
            }
        }
    }

private:
    // Column-major fill, matching the agent matrix so the two grids line up
    // row for row across the divider.
    QPointF cellCenter(int index) const
    {
        const int column = index / kRows;
        const int row = index % kRows;
        return QPointF(column * kPitch + kPitch / 2.0,
                       row * kPitch + kPitch / 2.0);
    }

    int dotAt(const QPoint &pos) const
    {
        const int column = pos.x() / kPitch;
        const int row = pos.y() / kPitch;
        if (column < 0 || row < 0 || row >= kRows)
            return -1;
        const int index = column * kRows + row;
        return index < m_dots.size() ? index : -1;
    }

    static constexpr int kRows = 3;        // dots stacked per column
    static constexpr int kPitch = 7;       // cell size, including its gap
    static constexpr double kRadius = 2.3; // painted dot
    static constexpr int kMaxColumns = 12; // ~36 nodes before the tooltip takes over

    QVector<Dot> m_dots;
};

// The CI companion to the fleet matrix (adhoc #70): the most recent action runs
// as a row of tiny status squares sitting immediately right of the agent dots,
// so one glance at the chrome line covers both what the agents and what the
// workflows are doing. Newest run on the left, each square tinted with the same
// actionStatusColor() the Actions tab uses; a running square breathes so an
// in-flight workflow is distinguishable from a finished blue one.
class ActionRunStrip : public QWidget
{
public:
    struct Cell {
        int runId = 0;
        QColor color;
        bool running = false;
    };

    // How many runs the strip shows before the tooltip takes over.
    static constexpr int kMaxCells = 9;

    explicit ActionRunStrip(QWidget *parent = nullptr) : QWidget(parent)
    {
        setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
        setFixedHeight(kHeight);
        setFixedWidth(0); // nothing to show until the first setCells()
        setCursor(Qt::PointingHandCursor);
        hide();
        m_pulse = new QTimer(this);
        m_pulse->setInterval(90);
        connect(m_pulse, &QTimer::timeout, this, [this] {
            m_phase += 0.05;
            if (m_phase >= 1.0)
                m_phase -= 1.0;
            update();
        });
    }

    // Replace the strip. Anything past kMaxCells is dropped from the paint (the
    // caller folds the remainder into the tooltip).
    void setCells(const QVector<Cell> &cells)
    {
        m_cells = cells.mid(0, kMaxCells);
        setFixedWidth(m_cells.isEmpty() ? 0 : m_cells.size() * kPitch);
        bool anyRunning = false;
        for (const Cell &c : std::as_const(m_cells))
            anyRunning = anyRunning || c.running;
        if (anyRunning && !m_pulse->isActive())
            m_pulse->start();
        else if (!anyRunning && m_pulse->isActive())
            m_pulse->stop();
        update();
    }

    int shownCount() const { return m_cells.size(); }

    // Clicking a square opens that run; clicking past them falls back to run id
    // 0 (the repository's Actions tab).
    std::function<void(int)> onCellClicked;

protected:
    void mousePressEvent(QMouseEvent *e) override
    {
        if (e->button() == Qt::LeftButton && onCellClicked) {
            const int index = cellAt(e->position().toPoint());
            onCellClicked(index >= 0 ? m_cells.at(index).runId : 0);
            // Same reason as AgentDotMatrix: an unhandled press on the
            // window-chrome bar below turns into a system window-move.
            e->accept();
            return;
        }
        QWidget::mousePressEvent(e);
    }

    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        p.setPen(Qt::NoPen);
        for (int i = 0; i < m_cells.size(); ++i) {
            const Cell &cell = m_cells.at(i);
            QColor color = cell.color;
            if (cell.running) {
                double t = m_phase + i * kPulseStep;
                t -= std::floor(t);
                const double tri = 1.0 - std::abs(2.0 * t - 1.0);
                color.setAlphaF(qBound(0.35, 0.55 + 0.45 * tri, 1.0));
            } else {
                color.setAlpha(215);
            }
            p.setBrush(color);
            p.drawRoundedRect(
                QRectF(i * kPitch + (kPitch - kSide) / 2.0,
                       (kHeight - kSide) / 2.0, kSide, kSide),
                2.0, 2.0);
        }
    }

private:
    int cellAt(const QPoint &pos) const
    {
        const int index = pos.x() / kPitch;
        return (index >= 0 && index < m_cells.size()) ? index : -1;
    }

    static constexpr int kPitch = 11;   // cell size, including its gap
    static constexpr double kSide = 8.0; // painted square
    static constexpr int kHeight = 21;  // matches AgentDotMatrix's 3x7 grid
    static constexpr double kPulseStep = 0.09; // per-square offset of the pulse

    QVector<Cell> m_cells;
    double m_phase = 0.0;
    QTimer *m_pulse = nullptr; // only ticks while a run is in flight
};

// A plain track-and-knob on/off switch, used for controls where the state is a
// real power switch (e.g. "is this node online") rather than a momentary
// action, so it reads unambiguously as on/off instead of just another button.
class ToggleSwitch : public QAbstractButton
{
public:
    explicit ToggleSwitch(QWidget *parent = nullptr) : QAbstractButton(parent)
    {
        setCheckable(true);
        setCursor(Qt::PointingHandCursor);
        setFixedSize(46, 24);
    }

    QSize sizeHint() const override { return QSize(46, 24); }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);

        const QColor track = isChecked() ? QColor("#2ea043") : QColor("#30363d");
        const QRectF trackRect(0.5, 0.5, width() - 1.0, height() - 1.0);
        const qreal r = trackRect.height() / 2.0;
        p.setPen(QPen(track.darker(130), 1));
        p.setBrush(track);
        p.drawRoundedRect(trackRect, r, r);

        const qreal knobD = trackRect.height() - 4.0;
        const qreal x = isChecked() ? trackRect.right() - knobD - 2.0
                                    : trackRect.left() + 2.0;
        const QRectF knobRect(x, trackRect.top() + 2.0, knobD, knobD);
        p.setPen(Qt::NoPen);
        p.setBrush(Qt::white);
        p.drawEllipse(knobRect);
    }
};

// One mirror node's live state for the repo the Mirror-nodes panel is showing
// (adhoc #122). This was the dot model for the activity strip that floated
// above the Mirror nodes tab (adhoc #197); the strip is gone (adhoc #420) and
// so is the radar dish that inherited its blips (adhoc #124), but
// loadMirrorNodesPanel still builds these so the chrome line's node dots can be
// tinted with this repo's sync/integrity state.
// A node the relay's integrity gate is rejecting is kept in the list even while
// offline, so the warning stays visible instead of the node just disappearing
// (adhoc #196).
struct MirrorNodeDot
{
    QString id;
    QString name;
    bool online = false;
    bool self = false;
    // Online node serving a commit behind the source of truth: drawn amber
    // instead of green until it catches up at its next heartbeat, so an
    // out-of-sync mirror is visible at a glance.
    bool behind = false;
    // Relay's integrity gate is rejecting this node's clones (adhoc #196).
    bool integrityFailing = false;
};

// Paints a light-green highlight across the FULL row under the mouse. Qt's
// `::item:hover` stylesheet only covers the single hovered cell, so we track the
// hovered row ourselves and fill every cell in it. The per-cell grey hover is
// suppressed by clearing State_MouseOver before the base paint, and nothing
// about the geometry changes on hover (no padding/border tweaks), so text never
// shifts. Works for any item view (tables, lists and trees) — for trees we match
// on the hovered index's row *and* parent so sibling rows elsewhere don't light
// up too.
class HoverRowDelegate : public QStyledItemDelegate
{
public:
    explicit HoverRowDelegate(QAbstractItemView *view)
        : QStyledItemDelegate(view), m_view(view)
    {
        m_view->setMouseTracking(true);
        m_view->viewport()->setMouseTracking(true);
        m_view->viewport()->installEventFilter(this);
        connect(m_view, &QAbstractItemView::entered, this,
                [this](const QModelIndex &index) { setHovered(index); });
        connect(m_view, &QAbstractItemView::viewportEntered, this,
                [this] { setHovered(QModelIndex()); });
    }

    void paint(QPainter *painter, const QStyleOptionViewItem &option,
               const QModelIndex &index) const override
    {
        QStyleOptionViewItem opt(option);
        // Never let the base/style draw the per-cell grey hover.
        opt.state &= ~QStyle::State_MouseOver;
        const bool sameRow = m_hovered.isValid() &&
                             index.row() == m_hovered.row() &&
                             index.parent() == m_hovered.parent();
        if (m_hoverFill && sameRow && !(option.state & QStyle::State_Selected))
            painter->fillRect(option.rect, QColor(46, 160, 67, 55)); // light green
        // Mark the selected row with a green outline rather than a solid green
        // band (issue #252, matching the agents list): strip the selection flag so
        // neither the style nor the stylesheet fills the row, then draw the outline
        // on top. enableHoverRowHighlight()'s blankSelectionBand() clears the band
        // the view would otherwise still paint from selection-background-color.
        opt.state &= ~QStyle::State_Selected;
        QStyledItemDelegate::paint(painter, opt, index);
        paintRowSelectionBorder(painter, option, index);
    }

    // Item views shape (and, for elided columns, fully lay out) the ENTIRE
    // display string on every paint, even though only the first few dozen
    // characters are ever visible in a list cell. An adhoc agent session stores
    // its whole prompt as the row "title", so a single cell could carry a
    // multi-thousand-character backtrace (issue #216) and block the GUI thread
    // for >1.5 s HarfBuzz-shaping text nobody can see. Capping the handed-off
    // string to a length far beyond any column's visible width keeps the drawn
    // result pixel-identical while bounding the per-paint shaping cost.
    QString displayText(const QVariant &value, const QLocale &locale) const override
    {
        QString text = QStyledItemDelegate::displayText(value, locale);
        if (text.size() > kMaxCellDisplayChars) {
            text.truncate(kMaxCellDisplayChars);
            text += QChar(0x2026); // horizontal ellipsis
        }
        return text;
    }

protected:
    static constexpr int kMaxCellDisplayChars = 512;

    bool eventFilter(QObject *obj, QEvent *event) override
    {
        if (event->type() == QEvent::Leave)
            setHovered(QModelIndex());
        return QStyledItemDelegate::eventFilter(obj, event);
    }

    // Subclasses can opt out of the light-green mouse-hover row fill while still
    // tracking the hovered row (e.g. the agents list, which wants no hover tint).
    bool m_hoverFill = false;

private:
    void setHovered(const QModelIndex &index)
    {
        if (QModelIndex(m_hovered) == index)
            return;
        m_hovered = index;
        if (m_view)
            m_view->viewport()->update();
    }

    QAbstractItemView *m_view = nullptr;
    QPersistentModelIndex m_hovered;
};

// The delegates now outline a selected row in green rather than filling it solid,
// but the view still paints a solid selection band from the app-wide stylesheet
// (selection-background-color plus the ::item:selected background rule, keyed on
// the view's object name). Blank both on this view, keyed on that same object name
// so the per-widget rule overrides the app rule, leaving only the outline showing
// (issue #252, mirroring the agents list).
inline void blankSelectionBand(QAbstractItemView *view)
{
    const QString name = view->objectName();
    if (name.isEmpty())
        return; // no object-name rule to override
    const QString sel = QStringLiteral("#") + name;
    view->setStyleSheet(
        view->styleSheet() + sel +
        QStringLiteral(" { selection-background-color: transparent; }") + sel +
        QStringLiteral("::item:selected { background: transparent; }"));
}

// Give a list-style view (table, list or tree) a full-row light-green hover
// highlight that never shifts the row's contents, plus the green selected-row
// outline (issue #252) in place of the solid selection band.
inline void enableHoverRowHighlight(QAbstractItemView *view)
{
    if (!view)
        return;
    view->setItemDelegate(new HoverRowDelegate(view));
    blankSelectionBand(view);
}

// Slices a 1px row outline in `color` across this single cell: top + bottom
// edges always, plus the left/right end caps on the first/last *visible* column
// (visual order, so it follows reordered headers). One free helper so every
// delegate that outlines a row -- selected (green), failed (red), the per-column
// scanner -- draws an identical outline and the seam between cells is invisible.
inline void paintRowBorder(QPainter *painter,
                           const QStyleOptionViewItem &option,
                           const QModelIndex &index, const QColor &color)
{
    bool drawLeft = index.column() == 0;
    bool drawRight = true;
    if (const auto *table = qobject_cast<const QTableView *>(option.widget)) {
        QHeaderView *header = table->horizontalHeader();
        int first = -1, last = -1;
        for (int v = 0; v < header->count(); ++v) {
            if (header->isSectionHidden(header->logicalIndex(v)))
                continue;
            if (first < 0)
                first = v;
            last = v;
        }
        const int visual = header->visualIndex(index.column());
        drawLeft = (visual == first);
        drawRight = (visual == last);
    }
    const QRect r = option.rect;
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, false);
    painter->setPen(QPen(color, 1));
    painter->drawLine(r.topLeft(), r.topRight());
    painter->drawLine(QPoint(r.left(), r.bottom()), QPoint(r.right(), r.bottom()));
    if (drawLeft)
        painter->drawLine(r.topLeft(), QPoint(r.left(), r.bottom()));
    if (drawRight)
        painter->drawLine(QPoint(r.right(), r.top()), QPoint(r.right(), r.bottom()));
    painter->restore();
}

// Outlines the SELECTED row in green with a transparent fill, instead of the
// solid green selection band.
inline void paintRowSelectionBorder(QPainter *painter,
                                    const QStyleOptionViewItem &option,
                                    const QModelIndex &index)
{
    if (!(option.state & QStyle::State_Selected))
        return;
    paintRowBorder(painter, option, index, QColor(46, 160, 67)); // #2ea043 green
}

// HoverRowDelegate variant that drops the light-green mouse-hover row tint while
// keeping the base's green selected-row outline (issue #184: the agents list wants
// no hover fill). The outline itself is drawn by HoverRowDelegate::paint.
class SelectionBorderRowDelegate : public HoverRowDelegate
{
public:
    explicit SelectionBorderRowDelegate(QAbstractItemView *view)
        : HoverRowDelegate(view)
    {
        m_hoverFill = false; // agents list: no mouse-hover row tint (issue #184)
    }
};

// Outlines a failed Action run in red right in the runs list, so a failure (e.g.
// a Cloudflare deploy that errored out) stands out on the run itself instead of
// in a banner pinned across the top of the tab (adhoc #62). A row is flagged via
// ActionFailedRole on its first-column item; the red outline is sliced per cell
// like the green selection border and survives selection, since it draws on top.
class ActionFailureBorderDelegate : public QStyledItemDelegate
{
public:
    using QStyledItemDelegate::QStyledItemDelegate;
    static constexpr int ActionFailedRole = Qt::UserRole + 1;

    void paint(QPainter *painter, const QStyleOptionViewItem &option,
               const QModelIndex &index) const override
    {
        QStyledItemDelegate::paint(painter, option, index);
        if (index.sibling(index.row(), 0).data(ActionFailedRole).toBool())
            paintRowBorder(painter, option, index, QColor(0xf8, 0x51, 0x49)); // red
    }
};

// RAII guard that suspends a widget's repaints for a bulk table rebuild, so
// clearing the rows and inserting/populating them fires a single repaint when
// the guard goes out of scope instead of one per row. Without it, inserting
// rows one at a time (often with the event loop pumped between them, as the
// commit/branch loaders do) makes the list visibly fill "one row after another"
// and feel slow. Restores the previous state even on an early return, and
// nesting is safe because it remembers and restores whatever it found.
class TableRepaintGuard
{
public:
    explicit TableRepaintGuard(QWidget *w) : m_w(w)
    {
        if (m_w) {
            m_was = m_w->updatesEnabled();
            m_w->setUpdatesEnabled(false);
        }
    }
    ~TableRepaintGuard()
    {
        if (m_w)
            m_w->setUpdatesEnabled(m_was);
    }
    TableRepaintGuard(const TableRepaintGuard &) = delete;
    TableRepaintGuard &operator=(const TableRepaintGuard &) = delete;

private:
    QWidget *m_w = nullptr;
    bool m_was = true;
};

// Gives a table's columns standard-spreadsheet drag behaviour (issue #263): every
// divider drags independently, resizing only its own column while the columns to
// its right simply shift over (a horizontal scrollbar appears if they overflow),
// just like Excel/Sheets. Qt's auto-sizing header modes fight this -- a Stretch or
// stretch-last column silently absorbs a neighbour's drag (so the divider snaps
// back and distant columns jump, which feels broken), and ResizeToContents locks
// the divider entirely. So once real rows have populated, this fits each auto-sized
// column to the width of its widest data and switches it to Interactive:
// ResizeToContents and Stretch/stretch-last columns alike are sized to their content
// (a Stretch column would otherwise keep only the width it was stretched to fill,
// which can be narrower than its content and elide the text), and stretch-last is
// turned off. Fixed button columns are left exactly as the caller set them, and
// so is `keepFlexibleColumn` when the caller has one column that must go on
// absorbing the spare width (the agents list's Issue title, adhoc #35). Call
// once after the header has been configured.
inline void makeColumnsResizable(QTableWidget *table, int keepFlexibleColumn = -1)
{
    if (!table || !table->model())
        return;
    QHeaderView *header = table->horizontalHeader();
    auto done = std::make_shared<bool>(false);
    QObject::connect(
        table->model(), &QAbstractItemModel::rowsInserted, table,
        [table, header, done, keepFlexibleColumn]() {
            if (*done)
                return;
            *done = true;
            // Defer to the next event-loop turn so the fit reflects the
            // freshly-set cell contents rather than the just-inserted empty rows.
            QTimer::singleShot(0, table, [table, header, keepFlexibleColumn]() {
                // The last column may auto-fill via stretchLastSection rather than
                // a per-section Stretch mode; capture that before turning it off.
                const bool stretchLast = header->stretchLastSection();
                const int last = header->count() - 1;
                header->setStretchLastSection(false);
                for (int i = 0; i < header->count(); ++i) {
                    if (i == keepFlexibleColumn)
                        continue; // stays Stretch, absorbing the spare width
                    const QHeaderView::ResizeMode mode = header->sectionResizeMode(i);
                    const bool autosized =
                        mode == QHeaderView::ResizeToContents ||
                        mode == QHeaderView::Stretch || (stretchLast && i == last);
                    if (!autosized)
                        continue; // leave Fixed button columns untouched
                    // Switch to draggable Interactive, then expand the column to
                    // the width of its widest cell (or header label) so nothing is
                    // elided. A Stretch column otherwise reports only the width it
                    // was stretched to fill, which can be narrower than its data.
                    header->setSectionResizeMode(i, QHeaderView::Interactive);
                    table->resizeColumnToContents(i);
                }
            });
        });
}

// Shared geometry + painting for the issue progress bars, so the list-column
// delegate and the draggable detail-panel slider stay pixel-identical. The bar
// fills the cell minus an "NN%" label drawn at the right edge.

// Maps an x coordinate within `cellRect` to a 0..100 percentage along the track.
inline int progressPctForX(const QRect &cellRect, int x, const QFontMetrics &fm)
{
    const QRect cell = cellRect.adjusted(8, 0, -8, 0);
    const int textW = fm.horizontalAdvance(QStringLiteral("100%")) + 4;
    const int barW = qMax(1, cell.width() - textW);
    return qBound(0, qRound((x - cell.left()) * 100.0 / barW), 100);
}

// Draws the track, fill and right-aligned percent for `pct` into `cellRect`.
inline void paintProgressBar(QPainter *painter, const QRect &cellRect, int pct,
                             const QFontMetrics &fm)
{
    pct = qBound(0, pct, 100);
    const QRect cell = cellRect.adjusted(8, 0, -8, 0);
    const QString label = QStringLiteral("%1%").arg(pct);
    const int textW = fm.horizontalAdvance(QStringLiteral("100%")) + 4;
    QRect barRect(cell.left(), cell.center().y() - 4,
                  qMax(0, cell.width() - textW), 8);
    QRect textRect(barRect.right() + 4, cell.top(), textW, cell.height());

    // Theme-aware so the track reads as a soft groove rather than a black box on
    // a light row. Matches the milestone progress bars.
    const bool dark = currentThemeIsDark();
    const QColor track(dark ? "#30363d" : "#d0d7de");
    const QColor fillColor(pct >= 100 ? "#3fb950" : "#388bfd"); // green / blue
    const QColor textColor(dark ? "#8b949e" : "#57606a");

    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);
    painter->setPen(Qt::NoPen);
    painter->setBrush(track);
    painter->drawRoundedRect(barRect, 4, 4);
    if (pct > 0) {
        QRect fill(barRect.left(), barRect.top(),
                   qMax(barRect.height(), barRect.width() * pct / 100),
                   barRect.height());
        painter->setBrush(fillColor);
        painter->drawRoundedRect(fill, 4, 4);
    }
    painter->setPen(textColor);
    painter->drawText(textRect, Qt::AlignVCenter | Qt::AlignRight, label);
    painter->restore();
}

// Paints a compact progress bar (track + fill + "NN%") in place of plain text.
// Subclasses HoverRowDelegate so the column keeps the full-row hover highlight.
// The percentage is read from kProgressBarRole; the cell's display text is left
// empty so only the bar shows. Sorting still works via kTableSortRole on the
// underlying item.
class ProgressBarDelegate : public HoverRowDelegate
{
public:
    using HoverRowDelegate::HoverRowDelegate;

    QSize sizeHint(const QStyleOptionViewItem &option,
                   const QModelIndex &index) const override
    {
        QSize base = HoverRowDelegate::sizeHint(option, index);
        return QSize(qMax(base.width(), 96), qMax(base.height(), 18));
    }

    void paint(QPainter *painter, const QStyleOptionViewItem &option,
               const QModelIndex &index) const override
    {
        // Base draws the hover/selection background (and the empty cell text).
        HoverRowDelegate::paint(painter, option, index);
        const QVariant value = index.data(kProgressBarRole);
        if (!value.isValid())
            return;
        paintProgressBar(painter, option.rect, value.toInt(), option.fontMetrics);
    }
};

// Draws a compact "resource usage" bar (track + fill + "NN%") for the Mirror
// nodes view's CPU / RAM / disk columns. Unlike paintProgressBar (geared to task
// progress, where 100% is good and green) this colours by load: green when there
// is headroom, amber as it tightens, red when nearly exhausted. The percentage is
// read from kProgressBarRole; a value < 0 (or no value) renders a muted em-dash
// for nodes that don't advertise telemetry. Details live in the cell's tooltip.
inline void paintResourceBar(QPainter *painter, const QRect &cellRect, int pct,
                             const QFontMetrics &fm)
{
    const bool dark = currentThemeIsDark();
    const QColor track(dark ? "#30363d" : "#d0d7de");
    const QColor textColor(dark ? "#8b949e" : "#57606a");
    const QRect cell = cellRect.adjusted(8, 0, -8, 0);

    if (pct < 0) {
        painter->save();
        painter->setPen(textColor);
        painter->drawText(cell, Qt::AlignVCenter | Qt::AlignLeft,
                          QString::fromUtf8("\xE2\x80\x94"));
        painter->restore();
        return;
    }
    pct = qBound(0, pct, 100);
    const QString label = QStringLiteral("%1%").arg(pct);
    const int textW = fm.horizontalAdvance(QStringLiteral("100%")) + 4;
    QRect barRect(cell.left(), cell.center().y() - 4,
                  qMax(0, cell.width() - textW), 8);
    QRect textRect(barRect.right() + 4, cell.top(), textW, cell.height());

    // green < 70 <= amber < 90 <= red.
    const QColor fillColor(pct >= 90 ? "#f85149" : (pct >= 70 ? "#d29922" : "#3fb950"));

    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);
    painter->setPen(Qt::NoPen);
    painter->setBrush(track);
    painter->drawRoundedRect(barRect, 4, 4);
    if (pct > 0) {
        QRect fill(barRect.left(), barRect.top(),
                   qMax(barRect.height(), barRect.width() * pct / 100),
                   barRect.height());
        painter->setBrush(fillColor);
        painter->drawRoundedRect(fill, 4, 4);
    }
    painter->setPen(textColor);
    painter->drawText(textRect, Qt::AlignVCenter | Qt::AlignRight, label);
    painter->restore();
}

// Cell delegate wrapper around paintResourceBar; keeps the full-row hover via
// HoverRowDelegate, like ProgressBarDelegate.
class ResourceBarDelegate : public HoverRowDelegate
{
public:
    using HoverRowDelegate::HoverRowDelegate;

    QSize sizeHint(const QStyleOptionViewItem &option,
                   const QModelIndex &index) const override
    {
        QSize base = HoverRowDelegate::sizeHint(option, index);
        return QSize(qMax(base.width(), 84), qMax(base.height(), 18));
    }

    void paint(QPainter *painter, const QStyleOptionViewItem &option,
               const QModelIndex &index) const override
    {
        HoverRowDelegate::paint(painter, option, index);
        const QVariant value = index.data(kProgressBarRole);
        paintResourceBar(painter, option.rect,
                         value.isValid() ? value.toInt() : -1,
                         option.fontMetrics);
    }
};

// A draggable version of the progress bar for the issue detail panel: click or
// drag anywhere along the track to set the percentage. Pure QWidget (no moc) —
// the owner wires the result through the onCommitted callback, fired once the
// drag/click finishes so the store is written only on release.
class ProgressSlider : public QWidget
{
public:
    explicit ProgressSlider(QWidget *parent = nullptr) : QWidget(parent)
    {
        setCursor(Qt::PointingHandCursor);
        setMinimumHeight(18);
        setToolTip(QStringLiteral("Drag to set progress"));
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    }

    int value() const { return m_value; }
    void setValue(int pct)
    {
        pct = qBound(0, pct, 100);
        if (pct == m_value)
            return;
        m_value = pct;
        update();
    }

    // Invoked with the final percentage when a click/drag finishes.
    std::function<void(int)> onCommitted;

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        paintProgressBar(&painter, rect(), m_value, fontMetrics());
    }
    void mousePressEvent(QMouseEvent *e) override
    {
        if (e->button() != Qt::LeftButton)
            return;
        m_dragging = true;
        setValue(progressPctForX(rect(), int(e->position().x()), fontMetrics()));
    }
    void mouseMoveEvent(QMouseEvent *e) override
    {
        if (m_dragging)
            setValue(progressPctForX(rect(), int(e->position().x()), fontMetrics()));
    }
    void mouseReleaseEvent(QMouseEvent *e) override
    {
        if (!m_dragging || e->button() != Qt::LeftButton)
            return;
        m_dragging = false;
        if (onCommitted)
            onCommitted(m_value);
    }

private:
    int m_value = 0;
    bool m_dragging = false;
};

// One column of the Kanban issue board: a QListWidget that accepts cards dragged
// from any sibling column. On a cross-column drop it doesn't move the item itself
// (the board is rebuilt from the store after the issue's status label changes);
// instead it reads the dragged card's issue number and invokes onDrop with this
// column's name. A plain callback avoids needing Q_OBJECT/moc in this .cpp.
class BoardColumnList : public QListWidget
{
public:
    explicit BoardColumnList(QString column, QWidget *parent = nullptr)
        : QListWidget(parent), m_column(std::move(column))
    {
        setObjectName("issueBoardList");
        setDragEnabled(true);
        setAcceptDrops(true);
        setDragDropMode(QAbstractItemView::DragDrop);
        setDefaultDropAction(Qt::MoveAction);
        setSelectionMode(QAbstractItemView::SingleSelection);
        setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        setWordWrap(true);
        setUniformItemSizes(false);
    }

    // Invoked with (issueNumber, targetColumn) when a card is dropped here from
    // another column.
    std::function<void(int number, const QString &column)> onDrop;

protected:
    void dropEvent(QDropEvent *event) override
    {
        auto *src = qobject_cast<QListWidget *>(event->source());
        QListWidgetItem *item = src ? src->currentItem() : nullptr;
        if (src && src != this && item && onDrop) {
            const int number = item->data(Qt::UserRole).toInt();
            // We rebuild the board from the store rather than letting the view
            // physically move the row, so don't apply the drag's move action.
            event->setDropAction(Qt::IgnoreAction);
            event->accept();
            onDrop(number, m_column);
            return;
        }
        event->ignore();
    }

private:
    QString m_column;
};

// Draws the cell's relative-time text (via the base) and, when the cell carries
// kPacmanAnchorRole (a behind-but-online node), a small pac-man pie at the right
// edge that fills toward a closed mouth as the node nears its next heartbeat and
// re-sync. Painting via a delegate (rather than a cell widget) keeps the chart
// aligned with its row through sorting. The view animates it by repainting the
// viewport on a timer.
class MirrorSyncDelegate : public HoverRowDelegate
{
public:
    using HoverRowDelegate::HoverRowDelegate;

    QSize sizeHint(const QStyleOptionViewItem &option,
                   const QModelIndex &index) const override
    {
        QSize base = HoverRowDelegate::sizeHint(option, index);
        if (index.data(kPacmanAnchorRole).isValid())
            base.setWidth(base.width() + kDiameter + 12); // room for the pie
        return base;
    }

    void paint(QPainter *painter, const QStyleOptionViewItem &option,
               const QModelIndex &index) const override
    {
        // Base draws hover/selection background and the left-aligned time text.
        HoverRowDelegate::paint(painter, option, index);
        const QVariant anchor = index.data(kPacmanAnchorRole);
        if (!anchor.isValid())
            return;
        qint64 elapsed =
            (QDateTime::currentMSecsSinceEpoch() - anchor.toLongLong()) %
            kMirrorSyncIntervalMs;
        if (elapsed < 0)
            elapsed += kMirrorSyncIntervalMs;
        const double frac =
            qBound(0.0, double(elapsed) / double(kMirrorSyncIntervalMs), 1.0);

        const bool dark = currentThemeIsDark();
        QRectF box(option.rect.right() - kDiameter - 6,
                   option.rect.center().y() - kDiameter / 2.0,
                   kDiameter, kDiameter);
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing, true);
        painter->setPen(QPen(QColor(dark ? "#30363d" : "#d0d7de"), 1.2));
        painter->setBrush(Qt::NoBrush);
        painter->drawEllipse(box);
        if (frac > 0.004) {
            painter->setPen(Qt::NoPen);
            painter->setBrush(QColor(dark ? "#d29922" : "#9a6700"));
            // Sweep clockwise from 12 o'clock; Qt pie angles are 1/16°, CCW+.
            painter->drawPie(box, 90 * 16, -int(frac * 360.0 * 16));
        }
        painter->restore();
    }

private:
    static constexpr int kDiameter = 12;
};

inline QString formatByteSize(qint64 bytes)
{
    static const char *units[] = {"B", "KB", "MB", "GB", "TB"};
    double size = bytes;
    int unit = 0;
    while (size >= 1024.0 && unit < 4) {
        size /= 1024.0;
        ++unit;
    }
    return unit == 0 ? QStringLiteral("%1 B").arg(bytes)
                     : QStringLiteral("%1 %2").arg(size, 0, 'f', 1).arg(units[unit]);
}

// Builds a Mirror nodes resource cell: an empty SortTableWidgetItem carrying the
// usage percentage (kProgressBarRole, drawn as a little bar by ResourceBarDelegate)
// and a hover tooltip with the underlying figures. pct < 0 renders as "unknown".
inline SortTableWidgetItem *makeResourceBarCell(int pct, const QString &tooltip)
{
    auto *item = new SortTableWidgetItem(QString());
    item->setData(kProgressBarRole, pct);
    item->setData(kTableSortRole, double(pct)); // unknown (-1) sorts below 0%
    if (!tooltip.isEmpty())
        item->setToolTip(tooltip);
    return item;
}

// A RAM/disk usage bar cell from used/total byte counts (total <= 0 == unknown),
// with a "<used> used of <total> (NN%) \xC2\xB7 <free> free" tooltip.
inline SortTableWidgetItem *makeByteUsageCell(const QString &label, qint64 used, qint64 total)
{
    if (total <= 0)
        return makeResourceBarCell(-1, QString());
    used = qBound<qint64>(0, used, total);
    const int pct = int(qRound(100.0 * double(used) / double(total)));
    const QString tip =
        QStringLiteral("%1: %2 used of %3 (%4%) \xC2\xB7 %5 free")
            .arg(label, formatByteSize(used), formatByteSize(total))
            .arg(pct)
            .arg(formatByteSize(total - used));
    return makeResourceBarCell(pct, tip);
}

// A CPU usage bar cell from a 0..100 host-CPU percentage (< 0 == unknown).
inline SortTableWidgetItem *makeCpuUsageCell(double cpuPercent)
{
    if (cpuPercent < 0.0)
        return makeResourceBarCell(-1, QString());
    const int pct = int(qRound(qBound(0.0, cpuPercent, 100.0)));
    return makeResourceBarCell(
        pct, QStringLiteral("CPU: %1% busy across all cores").arg(pct));
}

// Format an integer with thousands separators, e.g. 1234567 -> "1,234,567". Uses
// a fixed US-English locale so the separator is always a comma regardless of the
// host's system locale; numbers below 1000 are returned unchanged.
inline QString formatCount(qint64 n)
{
    static const QLocale locale(QLocale::English, QLocale::UnitedStates);
    return locale.toString(n);
}

class ClickableIssueBody : public QWidget
{
public:
    explicit ClickableIssueBody(QWidget *parent = nullptr) : QWidget(parent) {}

    std::function<void()> onClicked;

protected:
    void mousePressEvent(QMouseEvent *event) override
    {
        if (event->button() == Qt::LeftButton && onClicked) {
            // The callback may reassign or clear onClicked (e.g. swapping the
            // body for an inline editor). Copy it to a local first so the
            // closure—and everything it captured—stays alive for the duration
            // of the call instead of being freed mid-execution.
            auto callback = onClicked;
            callback();
            event->accept();
            return;
        }
        QWidget::mousePressEvent(event);
    }
};

const QString kRepoUrl = QStringLiteral("https://github.com/forkmesh/forkmesh.git");
const QString kDisplayNameSetting = QStringLiteral("profile/displayName");
const QString kHandleSetting = QStringLiteral("profile/handle");
const QString kAccountNameSetting = QStringLiteral("account/nodeName");
// The fun node name a true first run handed out (see randomFunNodeName). While
// the account name is still exactly this — nobody typed a username, signed up,
// or logged in — the person at the desktop is a guest, not a user, and chat
// speaks as "Guest ####" like the website does for anonymous visitors (adhoc
// #113). Renaming (setup screen or Settings) or claiming a user account makes
// account/nodeName diverge from this and thereby exits guest mode.
const QString kGeneratedNodeNameSetting =
    QStringLiteral("account/generatedNodeName");
// This machine's own node name on the mesh, distinct from the username: a user
// account owns many nodes, and the machine you're sitting at is just one of
// them. Unset means "derive a default" (hostname for user-account installs,
// the account name for bare node accounts) — see MainWindow::machineNodeName().
const QString kMachineNodeNameSetting = QStringLiteral("node/machineName");
// Persisted Hosts list (adhoc #263): non-sensitive JSON metadata only
// ({name, ip, user, status}). Legacy password fields are removed on load.
const QString kHostsSetting = QStringLiteral("hosts/list");
const QString kSolanaSetting = QStringLiteral("profile/solana");
const QString kAvatarSetting = QStringLiteral("profile/avatarPng");
const QString kServerUrlSetting = QStringLiteral("server/url");
// The mainnode is ForkMesh's canonical coordination point: a well-known
// owner/repo/room triple that every node's shared rooms and inbox routes
// converge on. The *host* is fully configurable (the first-run Relay server
    // field; self-hosting one is a first-class target — see /docs#self-hosting), but
// this path shape is a network-wide protocol constant, so it lives in one place
// instead of being spelled out at each call site.
const QString kMainnodeDefaultHost = forkmesh::mainnode::kDefaultHost;
const QString kMainnodeRoomPath = forkmesh::mainnode::kRoomPath;
const QString kLocalServerUrl = forkmesh::mainnode::kLocalServerUrl;
const QString kDefaultServerUrl = forkmesh::mainnode::kDefaultServerUrl;
const QString kRoomNameSetting = QStringLiteral("server/room");
// Local World dev server (cloudflare_worker/tools/world_dev_server.py). The
// World button probes this before falling back to the relay portal; set it to
// "off" to skip the probe entirely.
const QString kWorldDevUrlSetting = QStringLiteral("world/localDevUrl");
// Last account this node key authenticated as; lets the app start offline once a
// registered account has been confirmed at least once on this machine.
const QString kAuthedAccountSetting = QStringLiteral("account/authedName");
const QString kDesktopCapableAccountSetting =
    QStringLiteral("account/desktopCapableName");
const QString kDesktopCapablePublicKeySetting =
    QStringLiteral("account/desktopCapablePublicKey");
const QString kEmailVerifiedSettingPrefix =
    QStringLiteral("account/emailVerified/");
const QString kServersArray = QStringLiteral("servers/items");
const QString kActiveServerSetting = QStringLiteral("servers/active");
const QString kDefaultRoomName = forkmesh::mainnode::kDefaultRoomName;
const QString kRepositoriesArray = QStringLiteral("repositories/items");
const QString kMirrorRootSetting = QStringLiteral("repositories/mirrorRoot");
const QString kLastRepositorySetting = QStringLiteral("repositories/lastOpen");
// Last repo-detail tab actually viewed (updated by recordNavLocation()); a
// restart restores this rather than landing wherever a fresh open would
// (adhoc #101) — the app comes back on the page it was left on.
const QString kLastRepoDetailTabSetting =
    QStringLiteral("repositories/lastOpenDetailTab");
// Issue looper (adhoc #125): persist the running state so a restart resumes the
// loop on the same repo with the same provider instead of silently dropping it.
const QString kLooperActiveSetting = QStringLiteral("looper/active");
const QString kLooperProviderSetting = QStringLiteral("looper/provider");
const QString kLooperRepoSetting = QStringLiteral("looper/repo"); // "owner/name"

inline QString savedSolanaAddress()
{
    return QSettings().value(kSolanaSetting).toString().trimmed();
}

inline void saveSolanaAddress(const QString &address)
{
    QSettings().setValue(kSolanaSetting, address.trimmed());
}

inline QString emailVerifiedSettingKey(const QString &accountName)
{
    return kEmailVerifiedSettingPrefix + accountName.trimmed().toLower();
}
const QString kPreviewCacheRootSetting = QStringLiteral("repositories/previewCacheRoot");
const QString kConnectionTotalSetting = QStringLiteral("stats/connectionTotalMs");
// Persisted "node taken offline by the user" flag (reward heartbeat + repo
// serving paused). Persisted so a deliberately-offline node stays offline across
// restarts rather than silently resuming reward collection.
const QString kNodeOfflineSetting = QStringLiteral("stats/nodeOffline");
const QString kThemeSetting = QStringLiteral("app/theme"); // system | dark | light
// Show a desktop alert when a push lands on one of this node's mirrors.
const QString kPushAlertSetting = QStringLiteral("actions/pushAlert");
// Show a desktop alert when an action run starts and finishes. Retained for
// migration: older builds stored a plain bool here; new builds read/write
// kActionAlertModeSetting ("all" / "failed" / "none") instead.
const QString kActionAlertSetting = QStringLiteral("actions/runAlert");
// Which action runs raise a desktop alert: "all" (start + every finish),
// "failed" (only failures), or "none" (never). Mirrors GitHub's per-account
// Actions notification choice.
const QString kActionAlertModeSetting = QStringLiteral("actions/runAlertMode");

// Resolve the effective action-alert mode, migrating the legacy bool: an
// explicit mode wins; otherwise the old on/off toggle maps to all/none.
inline QString actionAlertMode()
{
    QSettings settings;
    const QString mode = settings.value(kActionAlertModeSetting).toString();
    if (mode == QLatin1String("all") || mode == QLatin1String("failed") ||
        mode == QLatin1String("none"))
        return mode;
    // Default off: action alerts are opt-in like every other notification.
    return settings.value(kActionAlertSetting, false).toBool()
               ? QStringLiteral("all")
               : QStringLiteral("none");
}
const QString kNodeConnectAlertSetting = QStringLiteral("notifications/nodeConnect");
const QString kDisbursementAlertSetting = QStringLiteral("notifications/disbursement");
// Every remaining desktop-notification category. All alerts are opt-in: off on a
// fresh install (the defaults are all false) and individually re-enabled from
// Settings → Notifications. The in-app Notifications page still logs events
// regardless; only the OS popups are gated.
const QString kChatMessageAlertSetting = QStringLiteral("notifications/chatMessages");
const QString kMentionAlertSetting = QStringLiteral("notifications/mentions");
const QString kIssueAlertSetting = QStringLiteral("notifications/issues");
const QString kPullAlertSetting = QStringLiteral("notifications/pullRequests");
const QString kCommentAlertSetting = QStringLiteral("notifications/comments");
const QString kMirrorUpdateAlertSetting = QStringLiteral("notifications/mirrorUpdated");
const QString kCoveOpenAlertSetting = QStringLiteral("notifications/coveOpened");
const QString kNewUserAlertSetting = QStringLiteral("notifications/newUser");
const QString kEmailNotifyMentionSetting = QStringLiteral("notifications/email/mention");
const QString kEmailNotifySubscribedSetting = QStringLiteral("notifications/email/subscribed");
const QString kEmailNotifyPullSubmittedSetting = QStringLiteral("notifications/email/pullSubmitted");
const QString kEmailNotifyIssueAssignedSetting = QStringLiteral("notifications/email/issueAssigned");
const QString kEmailNotifyRepoSharedSetting = QStringLiteral("notifications/email/repoShared");
const QString kEmailNotifyBountyFundedSetting = QStringLiteral("notifications/email/bountyFunded");
const QString kEmailNotifyBountyPaidSetting = QStringLiteral("notifications/email/bountyPaid");
const QString kEmailNotifyReleasePublishedSetting = QStringLiteral("notifications/email/releasePublished");
const QString kEmailNotifyPendingInboxSetting = QStringLiteral("notifications/email/pendingInbox");
const QString kEmailNotifyCreditsRefilledSetting = QStringLiteral("notifications/email/creditsRefilled");
const QString kEmailNotifyGeneralChatSetting = QStringLiteral("notifications/email/generalChat");
const QString kEmailNotifyHostOnlineSetting = QStringLiteral("notifications/email/hostOnline");
const QString kEmailNotifyHostOfflineSetting = QStringLiteral("notifications/email/hostOffline");
// A single #welcome channel. The old split #welcome-nodes / #welcome-users
// rooms filled with node churn and unverified-account noise, so the one-time
// "just joined" greeting now goes to one room and ONLY for new user accounts
// whose email is verified — plain nodes and unverified users stay silent, so
// #welcome reads as a genuine roll-call of real people.
const QString kWelcomeChannel = QStringLiteral("#welcome");
// A #welcome greeting only raises a "new user joined" ping while it is this
// fresh — peers replay their in-session history on every reconnect, and a
// replayed greeting that fell out of the id-dedupe set must not re-ping for a
// join the user already saw.
const qint64 kWelcomePingFreshMs = 5 * 60 * 1000;
// Legacy QSettings migration prefix retained for installs that already posted to
// an older welcome room.
const QString kLegacyWelcomeAnnouncedSettingPrefix =
    QStringLiteral("chat/welcomeAnnounced/");

// True when a notification category is enabled. Default false: notifications are
// off until the user turns them on, so a fresh install is silent.
inline bool notifyEnabled(const QString &key)
{
    return QSettings().value(key, false).toBool();
}
// Historical per-PR bounty settings (issue #347). The Worker-held wallet and
// escrow path is frozen; startup forces enabled=false and mode="perPr" so an
// older preferences file cannot reactivate custody. Keys remain only to support
// that one-way local migration.
const QString kAutoPrBountyEnabledSetting =
    QStringLiteral("bounty/autoPrEnabled");
const QString kAutoPrBountyAmountSetting =
    QStringLiteral("bounty/autoPrAmountUsd");
const QString kAutoPrBountyModeSetting = QStringLiteral("bounty/autoPrMode");
const QString kSolanaDisplayUsdSetting = QStringLiteral("profile/solanaDisplayUsd");
// Top-bar balance display currency: "sol" | "usd" | "inr". Supersedes the
// older boolean above (migrated on first read).
const QString kSolanaDisplayCurrencySetting =
    QStringLiteral("profile/solanaDisplayCurrency");
const QString kSolanaLastBalanceSettingPrefix =
    QStringLiteral("profile/solanaLastBalance/");
const QString kWindowGeometrySetting = QStringLiteral("ui/windowGeometry");
// Opt-in: show a small rebuild+restart button in the top nav (off by default).
const QString kShowRebuildButtonSetting = QStringLiteral("ui/showRebuildButton");
// Opt-in: log every HTTP request that flows through the shared network manager
// to the network log (method + status + URL). Off by default; a diagnostic aid
// for spotting chatty background traffic (adhoc #74).
const QString kVerboseNetworkLogSetting =
    QStringLiteral("diagnostics/verboseNetworkLog");
// App-level outbound firewall. Enabled by default: destinations must be on the
// whitelist before requests from ForkMesh are allowed to reach the network.
const QString kRequestFirewallEnabledSetting =
    QStringLiteral("firewall/whitelistOnly");
const QString kRequestFirewallWhitelistSetting =
    QStringLiteral("firewall/whitelist");
const QString kRequestFirewallDefaultSeededSetting =
    QStringLiteral("firewall/defaultWhitelistSeeded");
constexpr int kRequestFirewallHistoryLimit = 100;

inline QStringList defaultRequestFirewallWhitelist()
{
    return {QStringLiteral("hostwild:forkmesh.com"),
            QStringLiteral("hostwild:anthropic.com"),
            QStringLiteral("hostwild:solana.com")};
}

inline QStringList requestFirewallWhitelistWithDefaults()
{
    QSettings settings;
    QStringList rules = settings.value(kRequestFirewallWhitelistSetting).toStringList();
    if (!settings.value(kRequestFirewallDefaultSeededSetting, false).toBool()) {
        for (const QString &rule : defaultRequestFirewallWhitelist()) {
            if (!rules.contains(rule))
                rules.append(rule);
        }
        rules.sort(Qt::CaseInsensitive);
        settings.setValue(kRequestFirewallWhitelistSetting, rules);
        settings.setValue(kRequestFirewallDefaultSeededSetting, true);
    }
    return rules;
}
// On by default: when the periodic inbox poll finds new issues, merge and
// commit them automatically — but only while the owner's working tree has no
// uncommitted tracked changes, so issue commits never interleave with work in
// progress (issue #193). Off → incoming issues wait in the inbox for a manual
// "Sync inbox" click. The manual button is never gated by this.
const QString kAutoSyncIssuesSetting = QStringLiteral("repos/autoSyncIssues");
// When on, merging a pull request immediately syncs the new merge commit to the
// served mirror (and notifies peers) the moment you hit "Merge". Off (the
// default) → the merge lands locally only; the floating "Sync" button surfaces
// the pending commit and nothing reaches main until you click it. Adhoc #110:
// several owners were surprised that Merge published to main with no confirming
// click, so this stays opt-in.
const QString kAutoSyncOnMergeSetting = QStringLiteral("repos/autoSyncOnMerge");
// When on, MainWindow::maybeAutoUpdate() periodically checks the update remote
// and, on finding a new tagged release (not just any commit on main), runs the
// same update/rebuild/relaunch flow as the manual "Update, rebuild & restart"
// button — quietly, and never while an agent is running. Off by default on
// desktop; seeded on for headless installs in main.cpp (an operator-run VM has
// no one around to click "update").
const QString kAutoUpdateSetting = QStringLiteral("update/autoUpdate");
// Hourly local snapshots of the live database (Settings -> Data -> Automatic
// backups). OFF by default everywhere except control nodes — the installs that
// hold a Cloudflare API token (see forkmesh::autoBackupDefault) — because a
// rolling day of ~1GB tarballs filled several small VPS disks. An explicit
// true turns backups on for any node.
const QString kAutoBackupEnabledSetting = QStringLiteral("backup/hourlyEnabled");
// How many hourly snapshots are kept before the oldest is pruned.
const QString kAutoBackupKeepSetting = QStringLiteral("backup/keepCount");
// When a new UI stall is detected, hand its backtrace to a coding agent so the
// freeze gets fixed automatically. On by default (adhoc #205).
const QString kAutoAgentOnStallSetting =
    QStringLiteral("diagnostics/autoAgentOnStall");
// How many bytes of crashes.log had already been seen as of the last startup,
// so a crash that ended the previous session (which never gets a chance to log
// itself — the process is gone) shows up as a line in *this* session's own log
// instead of only ever living in crashes.log/stderr/journalctl (adhoc #200).
// Purely local: the notice is shown in-app and nothing leaves the machine.
const QString kCrashLogSeenOffsetSetting =
    QStringLiteral("diagnostics/crashLogSeenOffset");
const QString kVotesSpentSetting = QStringLiteral("votes/spent");
const QString kVotedSetting = QStringLiteral("votes/voted");
// Personal access tokens used only to authenticate clones when importing a repo
// from GitHub/GitLab, which lifts the unauthenticated clone rate limits. Stored
// locally; never published or sent anywhere but the provider's git endpoint.
const QString kGithubTokenSetting = QStringLiteral("import/githubToken");
const QString kGitlabTokenSetting = QStringLiteral("import/gitlabToken");
const QString kCodexApiKeySetting = QStringLiteral("agents/codexApiKey");
const QString kOpenAiAdminKeySetting = QStringLiteral("agents/openAiAdminKey");
// IDE integration: when on, the issue view gains "run in IDE" buttons that hand
// the issue to the ForkMesh VS Code / Codeium extension via ~/.forkmesh/ide/.
const QString kIdeIntegrationSetting = QStringLiteral("ide/integrationEnabled");
// Voice input: when whisper.cpp is downloaded and built (from Settings), a mic
// button next to the prompt box lets the user dictate the prompt locally. The
// install dir holds the cloned/built repo; the model name picks which ggml model
// was fetched (tiny.en/base.en/small.en).
const QString kWhisperDirSetting = QStringLiteral("voice/whisperDir");
const QString kWhisperModelSetting = QStringLiteral("voice/whisperModel");
// Public World origin allowed to control local speech-to-text. This is not a
// capability; one-use/session secrets are memory-only inside WorldSpeechBridge.
const QString kWorldSpeechOriginSetting =
    QStringLiteral("voice/worldExactOrigin");
// Which speech-to-text engine the mic uses: "whisper" (whisper.cpp, the default)
// or "parakeet" (NVIDIA Parakeet via a local Python env). The Parakeet model name
// picks which checkpoint the runner pulls (parakeet-mlx on Apple Silicon, NeMo
// elsewhere).
const QString kVoiceEngineSetting = QStringLiteral("voice/engine");
const QString kParakeetModelSetting = QStringLiteral("voice/parakeetModel");
// Which microphone the recorder captures from (adhoc #10). Empty == the system
// default; otherwise a recorder-specific device id from voiceInputDevices().
const QString kVoiceInputDeviceSetting = QStringLiteral("voice/inputDevice");
// When on, a successful "Merge to main" automatically runs "Pull <base> into
// all" so every other branch catches up with the just-merged work (adhoc #250).
const QString kBranchAutoPullAllSetting =
    QStringLiteral("branches/autoPullAllOnMerge");
const QString kCodexModelSetting = QStringLiteral("agents/codexModel");
const QString kIssueAskAiModel = QStringLiteral("gpt-4.1-nano");
// Persisted footer quick-add prompt history (adhoc #200) so Up still recalls
// prompts sent in earlier sessions, not just the current one.
const QString kQuickAddHistorySetting = QStringLiteral("issues/quickAddHistory");
// Whether the footer quick-add's "Create issue" toggle is on, remembered across
// launches (off by default: the common path starts an agent straight from the
// typed prompt without filing an issue first).
const QString kQuickAddCreateIssueSetting =
    QStringLiteral("issues/quickAddCreateIssue");
// Genie (adhoc #42): the website's remote-MCP setup, as generated by
// Organization Admin → "Remote ForkMesh MCP". The token is the revocable
// task-only bearer credential; the org names whose shared task list the agent
// works; the workflow picks the finishing sequence ("pr" or "deploy"). The URL
// defaults to the active relay's /mcp endpoint when left blank.
const QString kGenieTokenSetting = QStringLiteral("genie/token");
const QString kGenieOrgSetting = QStringLiteral("genie/org");
const QString kGenieWorkflowSetting = QStringLiteral("genie/workflow");
const QString kGenieUrlSetting = QStringLiteral("genie/mcpUrl");
const QString kClaudeApiKeySetting = QStringLiteral("agents/claudeApiKey");
// Anthropic Admin API key (sk-ant-admin01-...) — required for the cost report;
// a regular API key cannot read organization spend.
const QString kClaudeAdminKeySetting = QStringLiteral("agents/claudeAdminKey");
const QString kCodexCommandSetting = QStringLiteral("agents/codexCommand");
const QString kClaudeCommandSetting = QStringLiteral("agents/claudeCommand");
const QString kAgentContextSetting = QStringLiteral("agents/contextWindow");
const QString kAgentMaxOutputSetting = QStringLiteral("agents/maxOutputTokens");
// User-editable instruction preamble prepended to every agent prompt (the
// prompt that drives Claude and the other providers). Blank => built-in default.
const QString kAgentPromptPreambleSetting =
    QStringLiteral("agents/promptPreamble");
// User-editable instruction for the "Prioritize from README" issues button
// (issue #286): the default agent reads the README and reorders the open issue
// backlog. Blank => built-in default below.
const QString kPrioritizePromptSetting =
    QStringLiteral("agents/prioritizePrompt");
// Cached month-to-date spend labels (issue #115) so the figures persist and are
// shown immediately on restart instead of "not yet refreshed".
const QString kOpenAiSpendTextSetting = QStringLiteral("agents/openAiSpendText");
const QString kOpenAiSpendTsSetting = QStringLiteral("agents/openAiSpendTs");
const QString kClaudeSpendTextSetting = QStringLiteral("agents/claudeSpendText");
const QString kClaudeSpendTsSetting = QStringLiteral("agents/claudeSpendTs");
const QString kOpenAiCreditTextSetting = QStringLiteral("agents/openAiCreditText");
const QString kOpenAiCreditTsSetting = QStringLiteral("agents/openAiCreditTs");
const QString kClaudeCreditTextSetting = QStringLiteral("agents/claudeCreditText");
const QString kClaudeCreditTsSetting = QStringLiteral("agents/claudeCreditTs");
// Anchors (epoch ms) for the rolling 5-hour and weekly usage windows. They are
// reset to "now" whenever an agent runs after the previous window has elapsed,
// so the agent sessions screen can count down the time left in each window.
const QString kCodexLimit5hStartSetting = QStringLiteral("agents/codexLimit5hStart");
const QString kCodexLimitWeekStartSetting = QStringLiteral("agents/codexLimitWeekStart");
const QString kCodexUsage5hPctSetting = QStringLiteral("agents/codexUsage5hPct");
const QString kCodexUsageWeekPctSetting = QStringLiteral("agents/codexUsageWeekPct");
const QString kCodexUsage5hResetSetting = QStringLiteral("agents/codexUsage5hReset");
const QString kCodexUsageWeekResetSetting = QStringLiteral("agents/codexUsageWeekReset");
const QString kClaudeLimit5hStartSetting = QStringLiteral("agents/claudeLimit5hStart");
const QString kClaudeLimitWeekStartSetting = QStringLiteral("agents/claudeLimitWeekStart");
// Last-seen utilisation (0..100) of each Claude Code rolling window, cached so
// the top-bar mini usage chart (issue #266) can render the last known figures on
// the very first frame, before any agent has streamed a fresh rate-limit event.
const QString kClaudeUsage5hPctSetting = QStringLiteral("agents/claudeUsage5hPct");
const QString kClaudeUsageWeekPctSetting = QStringLiteral("agents/claudeUsageWeekPct");
// Wall-clock reset instant (epoch ms) of each Claude Code rolling window, taken
// from the OAuth usage endpoint's resets_at, cached alongside the utilisation so
// the mini chart's tooltip can show "resets in 2h" / "resets in 4d" straight
// away on the first frame after a restart (issue #50).
const QString kClaudeUsage5hResetSetting = QStringLiteral("agents/claudeUsage5hReset");
const QString kClaudeUsageWeekResetSetting = QStringLiteral("agents/claudeUsageWeekReset");
// Same pair for the premium per-model weekly window the OAuth usage endpoint
// reports next to the plan-wide one — the third bar on the chart (adhoc #96).
const QString kClaudeUsageFablePctSetting = QStringLiteral("agents/claudeUsageFablePct");
const QString kClaudeUsageFableResetSetting = QStringLiteral("agents/claudeUsageFableReset");
constexpr qint64 kAgentLimit5hMs = 5LL * 60 * 60 * 1000;
constexpr qint64 kAgentLimitWeekMs = 7LL * 24 * 60 * 60 * 1000;
// Issue #346: whether either window has been seen maxed out (>=99%) since it
// last refilled, so the drop back down can be told apart from "just polled
// while still low". Cleared the moment the refill notification fires.
const QString kClaudeUsage5hExhaustedSetting = QStringLiteral("agents/claudeUsage5hExhausted");
const QString kClaudeUsageWeekExhaustedSetting = QStringLiteral("agents/claudeUsageWeekExhausted");
const QString kClaudeUsageFableExhaustedSetting = QStringLiteral("agents/claudeUsageFableExhausted");
const QString kCodexUsage5hExhaustedSetting = QStringLiteral("agents/codexUsage5hExhausted");
const QString kCodexUsageWeekExhaustedSetting = QStringLiteral("agents/codexUsageWeekExhausted");
// Opt-in: email the node's account when a previously-maxed-out usage window
// refills. Off by default — most nodes are watched interactively.
const QString kEmailOnCreditsRefillSetting = QStringLiteral("agents/emailOnCreditsRefill");
// Opt-in: open a standard calendar reminder when an agent provider's usage
// window is exhausted, and ping locally when it reaches its known reset time.
const QString kUsageLimitCalendarReminderSetting =
    QStringLiteral("agents/usageLimitCalendarReminder");
const QString kUsageLimitReminderScheduledPrefix =
    QStringLiteral("agents/usageLimitReminderScheduled/");
const QString kUsageLimitReminderNotifiedPrefix =
    QStringLiteral("agents/usageLimitReminderNotified/");

// Compact "3h 12m" / "4d 6h" / "5m" rendering of a remaining duration, rounded
// up to the minute. Shared by the agent-limits label and the top-bar usage
// chart's reset-time tooltip (issue #50).
static QString humanizeRemaining(qint64 ms)
{
    const qint64 totalMin = (ms + 59999) / 60000; // round up to the minute
    const qint64 days = totalMin / (24 * 60);
    const qint64 hours = (totalMin % (24 * 60)) / 60;
    const qint64 mins = totalMin % 60;
    if (days > 0)
        return QStringLiteral("%1d %2h").arg(days).arg(hours);
    if (hours > 0)
        return QStringLiteral("%1h %2m").arg(hours).arg(mins);
    return QStringLiteral("%1m").arg(mins);
}

const QString kDefaultCodexCommand =
    QStringLiteral("codex -a never {modelArg} exec --sandbox workspace-write - < {promptFile}");
const QString kPreviousCodexCommand =
    QStringLiteral("codex -a never exec --sandbox workspace-write - < {promptFile}");
const QString kOlderCodexCommand =
    QStringLiteral("codex exec --sandbox workspace-write - < {promptFile}");
const QString kLegacyCodexCommand =
    QStringLiteral("codex exec --sandbox workspace-write --ask-for-approval never \"$(cat {promptFile})\"");
// Legacy default that required the `claude` CLI to be installed. Kept only so
// stored settings using it can be migrated to the API-key based runner below.
const QString kLegacyClaudeCommand =
    QStringLiteral("claude -p \"$(cat {promptFile})\" --dangerously-skip-permissions");
// "Claude Code" provider: drive the real `claude` CLI headlessly (no prompts, no
// input) so it edits the worktree until done; ForkMesh then turns the diff into a
// PR. Distinct from "Claude API" (the bundled python script) above.
const QString kClaudeCodeCommandSetting = QStringLiteral("agents/claudeCodeCommand");
// Which Claude model the `claude` CLI runs as (passed through as `--model`):
// empty = the CLI's own default, otherwise an alias like "opus"/"sonnet"/"haiku"
// or the "auto" sentinel (adhoc #91) that routes each task to a model.
// Surfaced as a chooser in the footer quick-add bar (adhoc #261).
const QString kClaudeCodeModelSetting = QStringLiteral("agents/claudeCodeModel");
// Last model picked in the Releases tab's "Generate release notes with agent"
// row, split by provider family since Claude and GPT model ids don't overlap.
// Remembered so drafting the next release starts on whatever model generated
// the previous one instead of resetting to the first item in the list.
const QString kReleaseNotesClaudeModelSetting =
    QStringLiteral("agents/releaseNotesClaudeModel");
const QString kReleaseNotesGptModelSetting =
    QStringLiteral("agents/releaseNotesGptModel");
// Disk cache of the last successful /v1/models fetch (see
// MainWindow::refreshClaudeModelCombo), loaded back into m_liveClaudeModels at
// startup so a model combo built before this session's first live fetch
// completes still lists the real models instead of just "Auto".
const QString kClaudeModelsCacheSetting = QStringLiteral("agents/claudeModelsCache");
// App-server model/list cache. The Codex catalog includes display names,
// supported reasoning efforts, modalities, and the current default; keep the
// raw model objects so pickers can update without shipping a stale hard-coded
// list or starting a CLI process merely to open a menu.
const QString kCodexModelsCacheSetting = QStringLiteral("agents/codexModelsCache");
// Composer "Auto mode" toggle: true => run Claude Code unattended (skip the
// permission prompts). Read when a transcript session launches.
const QString kClaudeAutoModeSetting = QStringLiteral("agents/claudeAutoMode");
// Exact composer mode shared by CLI-backed agents. The older bool above remains
// for settings migration and code paths that only distinguish unattended runs.
const QString kAgentModeSetting = QStringLiteral("agents/cliPermissionMode");
// The composer mode-selector labels. One word each (adhoc #38) so the whole
// composer row stays compact — "Auto mode" was the only one carrying the word
// "mode" and the dropdown itself already says what it is. Only "Auto" skips the
// CLI's permission prompts today; "Ask" / "Edit" / "Plan" all mean "don't skip"
// until the app can drive per-tool approval headlessly (see MainWindowChat's
// selector). Codex maps each label to an approval policy/sandbox by substring
// ("ask", "edit", "plan", "auto"), so these names are what it keys off too.
const QString kClaudeAutoModeLabel = QStringLiteral("Auto");
const QString kAgentAskModeLabel = QStringLiteral("Ask");

// Does a session's stored permission-mode label (AgentSession::mode) run the
// agent unattended? An empty label means the session predates per-session mode
// capture, so callers fall back to the global kClaudeAutoModeSetting. Sessions
// (and the saved kAgentModeSetting) written before the labels were shortened
// still say "Auto mode", and must keep running unattended.
inline bool agentModeSkipsPermissions(const QString &modeLabel)
{
    const QString label = modeLabel.trimmed().toLower();
    return label == QLatin1String("auto") || label == QLatin1String("auto mode");
}
// Slash-actions menu (adhoc #116), mirroring the Claude Code extension's "/"
// actions popup. Effort level for Claude Code runs ("low"/"medium"/"high"/
// "xhigh"/"max"), passed to the CLI as `--effort`.
const QString kClaudeEffortSetting = QStringLiteral("agents/claudeEffort");
// Effort levels the installed `claude` CLI actually accepts, probed from its own
// `--help` output (adhoc #38) and cached so the composer's speed picker offers
// the real list rather than a hard-coded guess that drifts with the CLI. Empty /
// unset falls back to defaultAgentEffortLevels() below.
const QString kClaudeEffortLevelsCacheSetting =
    QStringLiteral("agents/claudeEffortLevels");
// The fallback ladder for the composer's speed picker: what the CLI has shipped
// for a while, used until a probe (Claude Code) or the app-server model catalog
// (Codex) says otherwise.
inline QStringList defaultAgentEffortLevels()
{
    return {QStringLiteral("low"), QStringLiteral("medium"),
            QStringLiteral("high"), QStringLiteral("xhigh"),
            QStringLiteral("max")};
}
// One short label per effort id, for the composer's speed picker. Unknown ids (a
// CLI probe can surface levels this app has never heard of) just get their first
// letter capitalised, so a new level still reads as a real choice.
inline QString agentEffortLabel(const QString &level)
{
    const QString id = level.trimmed().toLower();
    if (id.isEmpty())
        return QString();
    if (id == QLatin1String("xhigh"))
        return QStringLiteral("Ultra");
    QString label = id;
    label[0] = label[0].toUpper();
    return label;
}
// "Thinking" toggle: false => launch the CLI with MAX_THINKING_TOKENS=0 so the
// model skips extended thinking. Default on (the CLI's own behavior).
const QString kClaudeThinkingSetting = QStringLiteral("agents/claudeThinking");
// "Switch models when a message is flagged" toggle: true => pass
// `--fallback-model` so the CLI retries on another Claude model when the chosen
// one is unavailable or a message is refused. Default off.
const QString kClaudeFallbackModelSetting =
    QStringLiteral("agents/claudeFallbackModel");
// When an agent is created from a non-Agents tab, automatically switch to the
// Agents tab and select the new session so the user can watch it run.
// Default on; can be disabled in Settings.
const QString kAutoSwitchToAgentSetting = QStringLiteral("agents/autoSwitchToAgent");
// When on, agent sessions started here are owner-encrypted before their opaque
// snapshots reach the relay. The browser deliberately has no recipient private
// key; inspection and steering remain on the owner desktop. Off by default so
// no agent metadata leaves this machine unless the user opts in.
const QString kPublishAgentsToWebSetting =
    QStringLiteral("agents/publishToWeb");
// When a repo's tests or build fail (the same kind of failure this very task
// was dispatched to fix), automatically send the failure back to whichever
// agent session last worked on that branch instead of waiting for a manual
// dispatch (adhoc #306). Default on; can be disabled in Settings.
const QString kAutoFixFailuresSetting =
    QStringLiteral("agents/autoFixFailures");
// Whether to hide external `claude` CLI sessions (ones ForkMesh didn't start
// itself, detected by scanning the repo's Claude Code project files) from the
// Agents tab. Default on: external sessions are excluded unless the user
// opts in, since they surface another process's transcripts unprompted.
const QString kExcludeExternalClaudeSetting =
    QStringLiteral("agents/excludeExternalClaude");
// Jail agents at launch (adhoc #236): run each agent in its own scratch
// environment (private per-session TMPDIR/cache, see AgentJail) with a memory
// cap applied before the CLI starts. Off by default.
const QString kAgentJailSetting = QStringLiteral("agents/jailEnabled");
// Memory cap (MB) applied to jailed agents; clamped to a sane floor so a typo
// can't make every agent die instantly at launch.
const QString kAgentJailMemoryMbSetting = QStringLiteral("agents/jailMemoryMb");
constexpr int kDefaultAgentJailMemoryMb = 4096;
constexpr int kMinAgentJailMemoryMb = 256;

// The configured jail memory cap, clamped to the floor above.
inline int agentJailMemoryMb()
{
    return qMax(kMinAgentJailMemoryMb,
                QSettings()
                    .value(kAgentJailMemoryMbSetting, kDefaultAgentJailMemoryMb)
                    .toInt());
}
// How many agent sessions may run at the same time (adhoc #433). Anything
// started beyond the cap stays Queued and launches as a slot frees up, so a
// batch of assignments can't spawn a dozen CLIs at once. Adjustable in
// Settings -> Agents & IDE.
const QString kMaxRunningAgentsSetting = QStringLiteral("agents/maxRunning");
constexpr int kDefaultMaxRunningAgents = 5;
constexpr int kMinMaxRunningAgents = 1;

// The configured concurrency cap, clamped to at least one slot so a zero or a
// typo can't wedge the queue with nothing ever starting.
inline int maxRunningAgents()
{
    return qMax(kMinMaxRunningAgents,
                QSettings()
                    .value(kMaxRunningAgentsSetting, kDefaultMaxRunningAgents)
                    .toInt());
}
// Footer quick-add "Auto-send" toggle (adhoc #45): true => submit the prompt as
// soon as a voice dictation finishes transcribing, without pressing Enter/Send.
const QString kVoiceAutoSubmitSetting = QStringLiteral("agents/voiceAutoSubmit");
// The footer quick-add "YOLO" (adhoc #12) and "Task" (adhoc #18) toggles were
// dropped from the composer in adhoc #120, so agents/quickAddYolo and
// agents/quickAddTask are no longer read or written: a prompted run never
// auto-merges and always opens an organization task.
// Last known number of open organization tasks, mirrored into settings so the
// Tasks rail badge is on screen from the first frame after a restart instead of
// staying blank until someone opens the Tasks page (adhoc #79).
const QString kOrganizationTaskOpenCountSetting =
    QStringLiteral("tasks/openCount");
// Canonical prefixes this desktop signs with its account key to open and close
// an organization task when it has no account session token to present (the
// authenticateSilently path holds keys, not sessions). Must stay byte-identical
// to ORG_TASK_OPEN_PROOF / ORG_TASK_COMPLETE_PROOF in the worker's entry.py.
const QString kOrgTaskOpenProof = QStringLiteral("forkmesh-org-task-open-v1");
const QString kOrgTaskCompleteProof =
    QStringLiteral("forkmesh-org-task-complete-v1");
// Same key, reading the board. Without it the Tasks tab was empty for every
// operator who launched normally instead of typing a password (adhoc #52).
// Must stay byte-identical to ORG_TASK_LIST_PROOF in entry.py.
const QString kOrgTaskListProof = QStringLiteral("forkmesh-org-task-list-v1");
// Same signing key, for the one credential the "genie" button needs (adhoc
// #49): the relay mints this desktop's task-only remote-MCP bearer instead of
// its operator copying one out of the website. Must stay byte-identical to
// GENIE_CREDENTIAL_PROOF in entry.py.
const QString kGenieCredentialProof =
    QStringLiteral("forkmesh-genie-credential-v1");
// Same signing key, for this account's own website alert inbox: the Alerts page
// shows what the site's bell shows, and clears it from here (adhoc #59). Must
// stay byte-identical to ACCOUNT_ALERT_LIST_PROOF / ACCOUNT_ALERT_READ_PROOF in
// entry.py.
const QString kAccountAlertListProof =
    QStringLiteral("forkmesh-account-alert-list-v1");
const QString kAccountAlertReadProof =
    QStringLiteral("forkmesh-account-alert-read-v1");
// Deleting one ping from that inbox signs the row's id as well, so a captured
// delete cannot be replayed against a different notification (adhoc #77). Must
// stay byte-identical to ACCOUNT_ALERT_DELETE_PROOF in entry.py.
const QString kAccountAlertDeleteProof =
    QStringLiteral("forkmesh-account-alert-delete-v1");
// Transcript diff style: true => side-by-side (split), false => unified.
const QString kClaudeDiffSplitSetting = QStringLiteral("agents/claudeDiffSplit");
// Diff viewer text size (points), adjustable with the +/- zoom control.
const QString kDiffFontPtSetting = QStringLiteral("ui/diffFontPt");
const QString kDefaultClaudeCodeCommand =
    QStringLiteral("claude -p \"$(cat {promptFile})\" --dangerously-skip-permissions");
// Claude Code in the embedded terminal runs interactively (not -p headless) so
// the user can watch and steer it. Configurable; {promptFile}/{issueNumber} are
// substituted.
const QString kClaudeCodeTerminalCommandSetting =
    QStringLiteral("agents/claudeCodeTerminalCommand");
const QString kDefaultClaudeCodeTerminalCommand =
    QStringLiteral("claude \"$(cat {promptFile})\" --dangerously-skip-permissions");
// Prior interactive default that prompted for every permission. Migrated to the
// full-accept default above so existing sessions stop stalling on prompts.
const QString kLegacyClaudeCodeTerminalCommand =
    QStringLiteral("claude \"$(cat {promptFile})\"");
// Raised from 2000: the view now renders in segments (see kNetworkLogSegmentSize)
// instead of the whole buffer at once, so a much larger in-memory/on-disk history
// no longer costs render time up front — it only matters once the user actually
// scrolls back far enough to load it.
constexpr int kNetworkLogLimit = 20000;
// How many matching lines to render per "page" of the network log: the initial
// view, and each older batch loaded when the user scrolls to the top.
constexpr int kNetworkLogSegmentSize = 300;
// How many lines the always-on footer strip seeds with on startup, and the cap
// on its live buffer (setFooterUpdateLine drops the oldest block past it).
constexpr int kFooterLogSeedLines = 300;

const QString kCodexProvider = QStringLiteral("codex");

// Provider family helpers. The Anthropic-backed "Claude API" script (plus the
// legacy "claude"/"claude-code" values) shares usage windows, spend tracking and
// iconography. Codex uses the logged-in Codex CLI account; OpenAI API uses the
// saved OpenAI key.
inline bool agentIsClaudeProvider(const QString &provider)
{
    return provider.startsWith(QLatin1String("claude"));
}

inline bool agentIsCodexProvider(const QString &provider)
{
    return provider == kCodexProvider;
}

inline bool agentUsesOpenAiKey(const QString &provider)
{
    return provider == QLatin1String("openai");
}

// User's preferred default agent (Settings → Agents). One of the canonical
// provider ids "codex", "openai", "claude-api" or "claude-code"; the quick-add
// and issue-detail provider pickers start on this value. Falls back to OpenAI API
// for an unset/unknown stored value.
const QString kDefaultAgentProviderSetting =
    QStringLiteral("agents/defaultProvider");
const QString kQuickAddAgentProviderSetting =
    QStringLiteral("agents/quickAddProvider");
const QString kFallbackAgentProvider = QStringLiteral("openai");

inline bool agentProviderIsKnown(const QString &provider)
{
    return agentIsCodexProvider(provider) ||
           provider == QLatin1String("openai") ||
           provider == QLatin1String("claude-api") ||
           provider == QLatin1String("claude-code");
}

inline QString defaultAgentProvider()
{
    const QString value = QSettings()
                              .value(kDefaultAgentProviderSetting,
                                     kFallbackAgentProvider)
                              .toString()
                              .trimmed();
    return agentProviderIsKnown(value) ? value : kFallbackAgentProvider;
}

inline QString quickAddAgentProvider()
{
    const QString value =
        QSettings().value(kQuickAddAgentProviderSetting).toString().trimmed();
    // "Manual (create issue)" is a quick-add-only pseudo-provider (adhoc #29): it
    // files an issue instead of running an agent, so it's not in the known-agent
    // set but must still be restorable across launches.
    if (value == QLatin1String("manual"))
        return value;
    return agentProviderIsKnown(value) ? value : defaultAgentProvider();
}

// Point a provider QComboBox (built with the codex/openai/claude-api/claude-code
// item data) at the user's saved default agent, falling back to the first item
// when the stored value isn't present.
inline void selectDefaultAgentProvider(QComboBox *combo)
{
    if (!combo)
        return;
    const int index = combo->findData(defaultAgentProvider());
    combo->setCurrentIndex(index >= 0 ? index : 0);
}

inline void selectQuickAddAgentProvider(QComboBox *combo)
{
    if (!combo)
        return;
    const int index = combo->findData(quickAddAgentProvider());
    combo->setCurrentIndex(index >= 0 ? index : 0);
}

// A QComboBox whose popup always opens tall enough to show every item, with no
// up/down scroll-arrow buttons (issue #348). Once a Qt Style Sheet is applied
// app-wide (Theme::kStyleSheet, set in MainWindow's ctor), Qt's CSS engine
// renders combo popups as a short scrollable list with those scroller buttons
// even when the whole list would fit — and the usual fix, forcing
// QStyle::SH_ComboBox_Popup through a QProxyStyle, is silently ignored while a
// stylesheet is active. Resizing the popup by hand right after it opens is the
// reliable workaround: given room for every row plus the container's scroller
// chrome, nothing needs scrolling so Qt hides the arrows. When the list is
// genuinely taller than the screen the arrows correctly stay (we cap there).
// It also sizes itself to the item that is actually showing rather than to the
// widest item in its list (adhoc #72). Qt's own hint measures every entry, so a
// single long label — "GPT-5.5 Codex" in the model picker, "Claude API" in the
// provider one — padded all four composer dropdowns with dead space even while
// short labels like "CC" or "Auto" were selected.
class FullPopupComboBox : public QComboBox {
public:
    explicit FullPopupComboBox(QWidget *parent = nullptr) : QComboBox(parent)
    {
        // Never wider than the selected label needs; the row's stretches take
        // the leftover space instead of the dropdowns.
        setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Fixed);
    }

    QSize sizeHint() const override { return currentTextSizeHint(); }
    QSize minimumSizeHint() const override { return currentTextSizeHint(); }

protected:
    void paintEvent(QPaintEvent *event) override
    {
        // The hint follows the selection, and the pickers are repopulated with
        // signals blocked (refreshQuickAddSpeedSelector, the model refresh,
        // applyLiveClaudeModelsToCombos), so currentIndexChanged is not a
        // reliable place to re-ask for space. A repaint always follows a
        // selection change: if the text about to be painted is not the one the
        // last hint was measured from, relayout first. This settles after one
        // extra layout pass — the next paint sees a matching label.
        if (m_hintedText != currentText()) {
            m_hintedText = currentText();
            updateGeometry();
        }
        QComboBox::paintEvent(event);
    }

    void showPopup() override
    {
        setMaxVisibleItems(qMax(maxVisibleItems(), count()));
        QComboBox::showPopup();
        QAbstractItemView *v = view();
        if (!v || count() == 0)
            return;
        QWidget *popup = v;
        for (QWidget *w = v; w; w = w->parentWidget()) {
            if (w->windowFlags().testFlag(Qt::Popup)) {
                popup = w;
                break;
            }
        }
        if (!popup->windowFlags().testFlag(Qt::Popup)) {
            QWidget *top = v->window();
            if (top && top->windowFlags().testFlag(Qt::Popup))
                popup = top;
        }
        // Height for every row plus the view frame. sizeHintForRow under-reports
        // the styled row height (the rows aren't laid out with their stylesheet
        // metrics yet when the base showPopup returns) and the view's own
        // sizeHint is just QListView's fixed default, so sum the row hints and add
        // a small cushion per row to cover the styling. If this still fits the
        // screen, force the popup and the view to that height so no internal scroll
        // buttons appear.
        int rowsH = 0;
        for (int row = 0; row < count(); ++row) {
            int rowH = v->sizeHintForRow(row);
            if (rowH <= 0)
                rowH = fontMetrics().height() + 8;
            rowsH += rowH + 8;
        }
        const int fullHeight = 2 * v->frameWidth() + rowsH;
        QRect geo = popup->geometry();
        QScreen *screen = popup->screen();
        if (!screen && windowHandle())
            screen = windowHandle()->screen();
        if (!screen)
            screen = QGuiApplication::primaryScreen();
        const QRect avail =
            screen ? screen->availableGeometry()
                   : QRect(QPoint(0, 0), QSize(10000, 10000));
        const int height = qMin(fullHeight, avail.height());
        v->setVerticalScrollBarPolicy(fullHeight <= avail.height()
                                          ? Qt::ScrollBarAlwaysOff
                                          : Qt::ScrollBarAsNeeded);
        v->setMinimumHeight(height);
        popup->setMinimumHeight(height);
        if (height <= geo.height() && geo.height() >= fullHeight)
            return; // already tall enough
        geo.setHeight(height);
        // Keep the now-taller popup fully on screen: if growing it pushed the
        // bottom (or top, when it opened upward) past the screen edge, slide it
        // back in, otherwise Qt clamps the height again and the arrows return.
        if (geo.bottom() > avail.bottom())
            geo.moveBottom(avail.bottom());
        if (geo.top() < avail.top())
            geo.moveTop(avail.top());
        popup->setGeometry(geo);
    }

private:
    // Same shape as QComboBox's own hint (font metrics for the contents, then
    // the style adds frame and drop-down arrow) but measured from the current
    // text alone instead of the widest item in the model.
    QSize currentTextSizeHint() const
    {
        const QFontMetrics fm = fontMetrics();
        const QString text = currentText();
        QSize contents(fm.horizontalAdvance(text.isEmpty() ? QStringLiteral("XX") : text),
                       qMax(fm.height(), 14) + 2);
        const QIcon icon = currentIndex() >= 0 ? itemIcon(currentIndex()) : QIcon();
        if (!icon.isNull()) {
            contents.setWidth(contents.width() + iconSize().width() + 4);
            contents.setHeight(qMax(contents.height(), iconSize().height()));
        }
        QStyleOptionComboBox opt;
        initStyleOption(&opt);
        return style()->sizeFromContents(QStyle::CT_ComboBox, &opt, contents, this);
    }

    QString m_hintedText;
};

// "Auto" model sentinel (adhoc #91). Instead of a fixed model, the transcript
// launcher routes each task: a free local heuristic pass first, then a triage
// ladder that asks Haiku whether it can handle the task and escalates through
// progressively stronger models until one is confident (Haiku can also name
// the right model directly). Every decision — and why — is written into the
// transcript as a "_local_notice" event.
const QString kClaudeAutoModelId = QStringLiteral("auto");

// The escalation ladder auto mode climbs, weakest first. `alias` is the short
// name the triage JSON uses ("haiku"/"sonnet"/"opus"/"fable"); `id` is what the
// CLI receives as --model; `label` is what transcript notices show.
struct ClaudeAutoRung {
    QString alias;
    QString id;
    QString label;
};

inline const QList<ClaudeAutoRung> &claudeAutoLadder()
{
    static const QList<ClaudeAutoRung> kLadder = {
        {QStringLiteral("haiku"), QStringLiteral("claude-haiku-4-5"),
         QStringLiteral("Haiku 4.5")},
        {QStringLiteral("sonnet"), QStringLiteral("claude-sonnet-4-6"),
         QStringLiteral("Sonnet 4.6")},
        {QStringLiteral("opus"), QStringLiteral("claude-opus-4-8"),
         QStringLiteral("Opus 4.8")},
        {QStringLiteral("fable"), QStringLiteral("claude-fable-5"),
         QStringLiteral("Fable 5")},
    };
    return kLadder;
}

// Whether a model id/alias belongs to the Claude family (Claude Code or Claude
// API): a "claude-*" id, one of the short CLI aliases (opus/sonnet/haiku/fable),
// or the "auto" router sentinel. Used to keep a model captured under one
// provider from being handed to another when a session is continued by a
// different agent (adhoc #76) — passing a Claude model to Codex, or a Codex
// model to Claude, makes the CLI reject the turn.
inline bool agentModelIsClaudeStyle(const QString &model)
{
    const QString m = model.trimmed().toLower();
    if (m.isEmpty())
        return false;
    if (m == kClaudeAutoModelId || m.startsWith(QLatin1String("claude")))
        return true;
    for (const ClaudeAutoRung &r : claudeAutoLadder())
        if (m == r.alias)
            return true;
    return false;
}

// Whether `model` is compatible with `provider`. An empty model always matches
// (the provider falls back to its own default). Claude providers need a
// Claude-style model; the Codex/OpenAI CLIs need a non-Claude one. This lets a
// session be continued by a different provider without the leftover model from
// the previous provider breaking the run (adhoc #76).
inline bool agentModelMatchesProvider(const QString &provider, const QString &model)
{
    if (model.trimmed().isEmpty())
        return true;
    if (provider == QLatin1String("claude-code") ||
        provider.startsWith(QLatin1String("claude")))
        return agentModelIsClaudeStyle(model);
    return !agentModelIsClaudeStyle(model);
}

// Pre-model router for auto mode: a self-hosted, zero-cost heuristic pass over
// the task text. Only the obvious cases are decided here — an explicit "use
// opus"-style request, clearly trivial edits, or clearly heavyweight work.
// Everything in between returns an empty model so the LLM triage ladder makes
// the call.
struct ClaudeAutoRoute {
    QString model;  // empty = not confident, fall through to LLM triage
    QString reason; // human-readable, shown in the transcript
};

inline ClaudeAutoRoute claudeAutoHeuristicRoute(const QString &task)
{
    const QString t = task.toLower();
    // An explicit model request in the task wins outright.
    for (const ClaudeAutoRung &r : claudeAutoLadder())
        if (t.contains(QStringLiteral("use %1").arg(r.alias)) ||
            t.contains(QStringLiteral("with %1").arg(r.alias)))
            return {r.id,
                    QStringLiteral("the task explicitly asks for %1").arg(r.label)};
    static const QStringList kTrivial = {
        QStringLiteral("typo"),        QStringLiteral("spelling"),
        QStringLiteral("rename"),      QStringLiteral("tooltip"),
        QStringLiteral("whitespace"),  QStringLiteral("padding"),
        QStringLiteral("margin"),      QStringLiteral("wording"),
        QStringLiteral("bump version"),
    };
    static const QStringList kHeavy = {
        QStringLiteral("refactor"),   QStringLiteral("architect"),
        QStringLiteral("redesign"),   QStringLiteral("rewrite"),
        QStringLiteral("migrat"),     QStringLiteral("concurren"),
        QStringLiteral("race condition"), QStringLiteral("deadlock"),
        QStringLiteral("security"),   QStringLiteral("protocol"),
        QStringLiteral("performance"), QStringLiteral("optimiz"),
        QStringLiteral("across the codebase"),
    };
    for (const QString &k : kHeavy)
        if (t.contains(k))
            return {claudeAutoLadder().at(2).id, // Opus
                    QStringLiteral("the task mentions \"%1\"").arg(k)};
    if (task.size() > 2500)
        return {claudeAutoLadder().at(2).id, // Opus
                QStringLiteral("the task description is long and detailed")};
    if (task.size() <= 220)
        for (const QString &k : kTrivial)
            if (t.contains(k))
                return {claudeAutoLadder().at(0).id, // Haiku
                        QStringLiteral("a short task mentioning \"%1\" looks routine")
                            .arg(k)};
    return {};
}

// Prepare a Claude model combo: the "Auto" router entry (adhoc #91) followed by
// the live provider models once mergeLiveClaudeModels fills them in. The
// property marks combos whose launch path understands the "auto" sentinel
// (composer + quick-add, which start transcript sessions) so the live-merge
// re-inserts the entry after replacing the list. The branch/action fix combos
// stay on concrete models for now — their claude-code runs would route fine
// (they start transcript sessions too), but the same widgets also serve the
// claude-api/openai providers where "auto" means nothing.
inline void populateClaudeModelCombo(QComboBox *combo)
{
    if (!combo)
        return;
    combo->clear();
    combo->setEditable(false);
    combo->setProperty("allowAutoModel", true);
    combo->addItem(QStringLiteral("Auto"), kClaudeAutoModelId);
}

inline QString codexChatGptModelId(const QString &model)
{
    const QString trimmed = model.trimmed();
    if (trimmed.isEmpty())
        return QStringLiteral("gpt-5.5");
    if (trimmed == QLatin1String("gpt-5.5-codex"))
        return QStringLiteral("gpt-5.5");
    if (trimmed == QLatin1String("gpt-5.4"))
        return QStringLiteral("gpt-5.4");
    if (trimmed == QLatin1String("gpt-5.4-mini") ||
        trimmed == QLatin1String("gpt-5.4-Mini"))
        return QStringLiteral("gpt-5.4-mini");
    // model/list is authoritative and evolves independently of ForkMesh.
    // Preserve catalog model ids introduced after this binary was released.
    return trimmed;
}

inline void populateCodexModelCombo(QComboBox *combo)
{
    if (!combo)
        return;
    combo->clear();
    combo->setEditable(false);
    combo->setInsertPolicy(QComboBox::NoInsert);
    combo->setProperty("allowAutoModel", false);
    const QJsonArray live = QJsonDocument::fromJson(
                                QSettings().value(kCodexModelsCacheSetting).toByteArray())
                                .array();
    for (const QJsonValue &value : live) {
        const QJsonObject model = value.toObject();
        if (model.value(QStringLiteral("hidden")).toBool())
            continue;
        QString id = model.value(QStringLiteral("model")).toString().trimmed();
        if (id.isEmpty())
            id = model.value(QStringLiteral("id")).toString().trimmed();
        if (!id.isEmpty())
            combo->addItem(model.value(QStringLiteral("displayName")).toString(id), id);
    }
    if (combo->count() == 0) {
        combo->addItem(QStringLiteral("GPT-5.5"), QStringLiteral("gpt-5.5"));
        combo->addItem(QStringLiteral("GPT-5.4"), QStringLiteral("gpt-5.4"));
        combo->addItem(QStringLiteral("GPT-5.4-Mini"),
                       QStringLiteral("gpt-5.4-mini"));
    }
}

inline void mergeLiveCodexModels(QComboBox *combo, const QJsonArray &models)
{
    if (!combo || models.isEmpty())
        return;
    QSignalBlocker blocker(combo);
    const QVariant selected = combo->currentData();
    combo->clear();
    QString defaultId;
    for (const QJsonValue &value : models) {
        const QJsonObject model = value.toObject();
        if (model.value(QStringLiteral("hidden")).toBool())
            continue;
        QString id = model.value(QStringLiteral("model")).toString().trimmed();
        if (id.isEmpty())
            id = model.value(QStringLiteral("id")).toString().trimmed();
        if (!id.isEmpty()) {
            combo->addItem(model.value(QStringLiteral("displayName")).toString(id), id);
            if (model.value(QStringLiteral("isDefault")).toBool())
                defaultId = id;
        }
    }
    const int restored = combo->findData(selected);
    const int fallback = combo->findData(defaultId);
    combo->setCurrentIndex(restored >= 0 ? restored : qMax(0, fallback));
}

inline QString selectedModelComboValue(QComboBox *combo)
{
    if (!combo)
        return QString();
    const QString text = combo->currentText().trimmed();
    const int idx = combo->currentIndex();
    if (idx >= 0 && combo->itemText(idx) == text)
        return combo->itemData(idx).toString().trimmed();
    return text;
}

inline void selectModelComboValue(QComboBox *combo, const QString &model)
{
    if (!combo)
        return;
    const QString trimmed = model.trimmed();
    const int idx = combo->findData(trimmed);
    if (idx >= 0) {
        combo->setCurrentIndex(idx);
    } else if (combo->isEditable()) {
        combo->setEditText(trimmed);
    } else if (combo->count() > 0) {
        combo->setCurrentIndex(0);
    }
}

// Friendly label for a session's `model` field, so the agent header can show
// which LLM actually did the work alongside its worktree location. Known short
// aliases and full IDs are mapped to display names; anything else is shown as-is.
// Model names in the pickers drop the vendor prefix (adhoc #38): the provider
// dropdown sitting right beside them already says which CLI is running, so the
// live "Claude Opus 5" reads as "Opus 5" and the composer row stays compact.
inline QString compactModelName(const QString &name)
{
    QString label = name.trimmed();
    static const QLatin1String prefixes[] = {QLatin1String("Claude "),
                                             QLatin1String("Anthropic ")};
    for (const QLatin1String &prefix : prefixes) {
        if (label.startsWith(prefix, Qt::CaseInsensitive)) {
            label = label.mid(prefix.size()).trimmed();
            break;
        }
    }
    return label.isEmpty() ? name.trimmed() : label;
}

inline QString agentModelLabel(const QString &model)
{
    if (model.trimmed().isEmpty())
        return QString();
    static const QHash<QString, QString> kLabels = {
        {QStringLiteral("auto"), QStringLiteral("Auto")},
        {QStringLiteral("opus"), QStringLiteral("Opus")},
        {QStringLiteral("sonnet"), QStringLiteral("Sonnet")},
        {QStringLiteral("haiku"), QStringLiteral("Haiku")},
        {QStringLiteral("fable"), QStringLiteral("Fable")},
        {QStringLiteral("claude-haiku-4-5"), QStringLiteral("Haiku 4.5")},
        {QStringLiteral("claude-haiku-4-5-20251001"), QStringLiteral("Haiku 4.5")},
        {QStringLiteral("claude-sonnet-4-6"), QStringLiteral("Sonnet 4.6")},
        {QStringLiteral("claude-sonnet-5"), QStringLiteral("Sonnet 5")},
        {QStringLiteral("claude-opus-4-8"), QStringLiteral("Opus 4.8")},
        {QStringLiteral("claude-fable-5"), QStringLiteral("Fable 5")},
        {QStringLiteral("gpt-4.1-nano"), QStringLiteral("GPT-4.1 nano")},
        {QStringLiteral("gpt-4.1-mini"), QStringLiteral("GPT-4.1 mini")},
        {QStringLiteral("gpt-4.1"), QStringLiteral("GPT-4.1")},
        {QStringLiteral("gpt-5"), QStringLiteral("GPT-5")},
        {QStringLiteral("gpt-5.1"), QStringLiteral("GPT-5.1")},
        {QStringLiteral("gpt-5.1-codex"), QStringLiteral("GPT-5.1 Codex")},
        {QStringLiteral("gpt-5.4"), QStringLiteral("GPT-5.4")},
        {QStringLiteral("gpt-5.4-mini"), QStringLiteral("GPT-5.4-Mini")},
        {QStringLiteral("gpt-5.5"), QStringLiteral("GPT-5.5")},
        {QStringLiteral("gpt-5.5-codex"), QStringLiteral("GPT-5.5 Codex")},
    };
    return kLabels.value(model.trimmed(), model.trimmed());
}

// Fill an agent-provider model combo for one of the three agent providers
// (adhoc #56; shared by the branch "Fix with agent" bar and the Actions "Fix
// with agent" bar). Item data is the model id passed straight to the caller's
// start function. claude-code combos start with this static fallback so they
// are never blank, then refreshClaudeModelCombo / mergeLiveClaudeModels swaps
// in the live line-up once a claude.ai OAuth token is available (users signed
// in with an API key, or offline, keep the fallback); openai/claude-api combos
// keep static lists too (no live fetch for those).
inline void fillAgentFixModelCombo(QComboBox *combo, const QString &provider)
{
    if (!combo)
        return;
    combo->clear();
    if (provider == QLatin1String("claude-code")) {
        combo->addItem(QStringLiteral("Sonnet 5"), QStringLiteral("claude-sonnet-5"));
        combo->addItem(QStringLiteral("Opus 4.8"), QStringLiteral("claude-opus-4-8"));
        combo->addItem(QStringLiteral("Haiku 4.5"), QStringLiteral("claude-haiku-4-5"));
        combo->addItem(QStringLiteral("Fable 5"), QStringLiteral("claude-fable-5"));
    } else if (agentIsCodexProvider(provider)) {
        populateCodexModelCombo(combo);
    } else if (agentUsesOpenAiKey(provider)) {
        combo->addItem(QStringLiteral("GPT-5.5"), QStringLiteral("gpt-5.5"));
        combo->addItem(QStringLiteral("GPT-5.5 Codex"),
                       QStringLiteral("gpt-5.5-codex"));
        combo->addItem(QStringLiteral("GPT-5.1 Codex"),
                       QStringLiteral("gpt-5.1-codex"));
        combo->addItem(QStringLiteral("GPT-5.1"), QStringLiteral("gpt-5.1"));
        combo->addItem(QStringLiteral("GPT-5"), QStringLiteral("gpt-5"));
        combo->addItem(QStringLiteral("GPT-4.1 nano"), QStringLiteral("gpt-4.1-nano"));
        combo->addItem(QStringLiteral("GPT-4.1 mini"), QStringLiteral("gpt-4.1-mini"));
        combo->addItem(QStringLiteral("GPT-4.1"), QStringLiteral("gpt-4.1"));
    } else { // claude API
        combo->addItem(QStringLiteral("Haiku 4.5"), QStringLiteral("claude-haiku-4-5"));
        combo->addItem(QStringLiteral("Sonnet 4.6"), QStringLiteral("claude-sonnet-4-6"));
        combo->addItem(QStringLiteral("Opus 4.8"), QStringLiteral("claude-opus-4-8"));
    }
}

// Replace a Claude model combo's contents with the live provider line-up (the
// `data` array from /v1/models). Signals are blocked and the current pick is
// restored by its data value so replacing never disturbs the selection or fires
// the change handler. Skips combos that belong to a non-Claude provider (their
// static lists are managed by fillAgentFixModelCombo instead).
inline void mergeLiveClaudeModels(QComboBox *combo, const QJsonArray &models)
{
    if (!combo || models.isEmpty())
        return;
    const QVariant claudeModelCombo = combo->property("claudeModelCombo");
    if (claudeModelCombo.isValid() && !claudeModelCombo.toBool())
        return;
    QSignalBlocker block(combo);
    const QVariant picked = combo->currentData();
    combo->clear();
    // Keep the "Auto" router entry on combos that support it (adhoc #91) —
    // clearing for the live list would otherwise drop it.
    if (combo->property("allowAutoModel").toBool())
        combo->addItem(QStringLiteral("Auto"), kClaudeAutoModelId);
    for (const QJsonValue &v : models) {
        const QJsonObject m = v.toObject();
        const QString id = m.value(QStringLiteral("id")).toString();
        if (id.isEmpty())
            continue;
        combo->addItem(
            compactModelName(m.value(QStringLiteral("display_name")).toString(id)),
            id);
    }
    const int idx = combo->findData(picked);
    // No restorable pick: land on the first live model, not the synthetic
    // "Auto" entry — an untouched chooser keeps showing the concrete default
    // that actually runs, and auto routing stays strictly opt-in.
    const int fallback =
        combo->property("allowAutoModel").toBool() && combo->count() > 1 ? 1 : 0;
    combo->setCurrentIndex(idx >= 0 ? idx : fallback);
}

// The tab a repository opens on: Code(0), the repo's own front page. Was a
// Settings → General preference defaulting to Agents, which meant every launch
// and every repo switch detoured through the Agents tab; adhoc #119 dropped both
// the setting and the detour, so a repo just opens where the app opens — its
// overview — and a relaunch restores the tab last viewed
// (kLastRepoDetailTabSetting).
constexpr int kRepoLandingTab = 0; // Code

// Live claude.ai OAuth access token the Claude Code CLI stores in
// ~/.claude/.credentials.json. Empty when the user logged in with an API key
// (or isn't signed in). Read fresh each call so a token the CLI has rotated is
// picked up automatically.
inline QString claudeCodeOAuthToken()
{
    QFile credFile(QDir::homePath() +
                   QStringLiteral("/.claude/.credentials.json"));
    if (!credFile.open(QIODevice::ReadOnly))
        return QString();
    return QJsonDocument::fromJson(credFile.readAll())
        .object()
        .value(QStringLiteral("claudeAiOauth"))
        .toObject()
        .value(QStringLiteral("accessToken"))
        .toString();
}

// System identity Anthropic requires on /v1/messages when authenticating with a
// claude.ai subscription OAuth token (as the Claude Code CLI does) instead of an
// API key.
const QString kClaudeCodeOAuthSystem =
    QStringLiteral("You are Claude Code, Anthropic's official CLI for Claude.");

// Materialize the bundled Claude agent script into the app data dir and return
// its path. The script talks to the Anthropic API directly using
// ANTHROPIC_API_KEY, so no `claude` binary is required.
inline QString claudeAgentScriptPath()
{
    const QString dir =
        QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) +
        QStringLiteral("/agents");
    QDir().mkpath(dir);
    const QString path = dir + QStringLiteral("/forkmesh_claude_agent.py");
    const QByteArray wanted = forkmeshClaudeAgentScript().toUtf8();
    QFile file(path);
    bool needsWrite = true;
    if (file.open(QIODevice::ReadOnly)) {
        needsWrite = file.readAll() != wanted;
        file.close();
    }
    if (needsWrite && file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        file.write(wanted);
        file.close();
    }
    return path;
}

// Default Claude command: run the bundled script with python3, feeding it the
// prompt file. {promptFile} is expanded by AgentRunner before execution.
inline QString defaultClaudeCommand()
{
    QString quoted = claudeAgentScriptPath();
    quoted.replace(QLatin1Char('\''), QStringLiteral("'\\''"));
    return QStringLiteral("python3 '%1' {promptFile}").arg(quoted);
}

// Read the Claude command, migrating any legacy `claude` CLI command (or a stale
// script path) to the current python-based default.
inline QString claudeCommandSetting()
{
    QSettings settings;
    const QString current = defaultClaudeCommand();
    QString command =
        settings.value(kClaudeCommandSetting, current).toString();
    const bool isLegacy = command.trimmed().isEmpty() ||
                          command == kLegacyClaudeCommand ||
                          command.contains(QStringLiteral("claude -p")) ||
                          command.contains(QStringLiteral("forkmesh_claude_agent.py"));
    if (isLegacy && command != current) {
        command = current;
        settings.setValue(kClaudeCommandSetting, command);
    }
    return command;
}

// The "Claude Code" command runs the real `claude` CLI (unlike claudeCommandSetting,
// which is migrated to the bundled python script). Configurable so users can match
// their own install / flags; defaults to a non-interactive headless invocation.
inline QString claudeCodeCommandSetting()
{
    QSettings settings;
    QString command =
        settings.value(kClaudeCodeCommandSetting, kDefaultClaudeCodeCommand)
            .toString()
            .trimmed();
    if (command.isEmpty())
        command = kDefaultClaudeCodeCommand;
    return command;
}

// The instruction preamble prepended to every agent prompt. Editable in
// Settings → Agents; an empty/whitespace value falls back to the built-in
// default so clearing the field restores the original behaviour.
inline QString agentPromptPreamble()
{
    const QString stored =
        QSettings().value(kAgentPromptPreambleSetting).toString().trimmed();
    return stored.isEmpty() ? AgentRunner::defaultPromptPreamble() : stored;
}

// Built-in instruction for the "Prioritize from README" button. The README and
// the open-issue list are appended after this text before the request is sent,
// so the editable prompt only governs how the model is told to rank them.
inline QString defaultPrioritizePrompt()
{
    return QStringLiteral(
        "You are triaging a software project's open issue backlog. Use the "
        "project's README as the guide to its goals, scope and priorities, then "
        "order the open issues from most to least important to the project's "
        "success. Favour issues that unblock core functionality or match the "
        "README's stated direction. Respond with ONLY a JSON array of the issue "
        "numbers in priority order, highest priority first, for example "
        "[12, 5, 8]. Do not include any other text.");
}

// The editable prioritization instruction (Settings -> Agents). Blank restores
// the built-in default so clearing the box is always safe.
inline QString prioritizePromptSetting()
{
    const QString stored =
        QSettings().value(kPrioritizePromptSetting).toString().trimmed();
    return stored.isEmpty() ? defaultPrioritizePrompt() : stored;
}

// Instruction for the "Analyze completeness" button (adhoc #139, adhoc #200). The
// README, the repository file listing and the open-issue list are appended after
// this text before the request is sent. The verdict judges how much of each
// issue's work is already implemented in the code, not how well the issue is
// written.
inline QString completenessPrompt()
{
    // \xE2\x80\x94 is an em-dash; keep this QString::fromUtf8 (not QStringLiteral)
    // so the multi-byte UTF-8 escape decodes to one code point, not mojibake.
    return QString::fromUtf8(
        "You are reviewing a software project's open issues to judge how much of "
        "each issue's requested work is ALREADY implemented in the codebase. Use "
        "the project's README and the repository file listing below for context, "
        "and reason carefully about whether the described feature or fix already "
        "exists in the code. For EACH open issue decide whether it is Complete (the "
        "work appears fully done and the issue could be closed), Partial (some of it "
        "exists but more is needed) or Incomplete (not started), and estimate a "
        "completeness percentage from 0 to 100 that reflects how much of the work is "
        "actually done. Give one short sentence (the \"comment\") on what is "
        "implemented and what is still missing. Be accurate and conservative: do not "
        "call something Complete unless the code really supports it. Respond with "
        "ONLY a JSON array, one object per issue, exactly like: "
        "[{\"number\":12,\"rating\":\"Partial\",\"completeness\":40,\"comment\":"
        "\"history exists but is not persisted across restarts\"}]. Do not add code "
        "fences or any other commentary.");
}

inline QString codexCommandSetting()
{
    QSettings settings;
    QString command = settings.value(kCodexCommandSetting, kDefaultCodexCommand).toString();
    if (command == kLegacyCodexCommand || command == kPreviousCodexCommand ||
        command == kOlderCodexCommand) {
        command = kDefaultCodexCommand;
        settings.setValue(kCodexCommandSetting, command);
    } else if (command.contains(QStringLiteral("--ask-for-approval"))) {
        command.replace(QStringLiteral(" --ask-for-approval never"), QString());
        command.replace(QStringLiteral(" --ask-for-approval=never"), QString());
        command.replace(QStringLiteral("--ask-for-approval never "), QString());
        command.replace(QStringLiteral("--ask-for-approval=never "), QString());
        command = command.trimmed();
        if (command.isEmpty())
            command = kDefaultCodexCommand;
        settings.setValue(kCodexCommandSetting, command);
    }
    return command;
}

// Directory holding client/CMakeLists.txt to update from: the build-time
// checkout when it still exists, otherwise a persistent clone managed by the
// app in its data directory (used when the binary was installed without a
// checkout, e.g. via install.sh).
inline QString updateClientDir()
{
    const QString baked = QStringLiteral(FORKMESH_SOURCE_DIR);
    if (!baked.isEmpty() && QDir(baked).exists("CMakeLists.txt"))
        return baked;
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) +
           "/src/qt_client";
}

inline bool gitOutput(const QString &clientDir, const QStringList &arguments, QString *out)
{
    QProcess process;
    process.setWorkingDirectory(clientDir);
    process.start(QStringLiteral("git"), arguments);
    if (!process.waitForFinished(3000)) {
        process.kill();
        process.waitForFinished(1000);
        return false;
    }
    if (process.exitCode() != 0)
        return false;
    if (out)
        *out = QString::fromUtf8(process.readAllStandardOutput()).trimmed();
    return true;
}

inline QStringList quickUpdatePullArguments(const QString &clientDir)
{
    QString upstream;
    if (gitOutput(clientDir,
                  {"rev-parse", "--abbrev-ref", "--symbolic-full-name", "@{u}"},
                  &upstream) &&
        !upstream.isEmpty()) {
        return {"pull", "--ff-only"};
    }

    QString branch;
    if (gitOutput(clientDir, {"branch", "--show-current"}, &branch) &&
        !branch.isEmpty()) {
        return {"pull", "--ff-only", "origin", branch};
    }

    return {"pull", "--ff-only", "origin", "HEAD"};
}

// When ForkMesh runs as root (e.g. launched via `sudo`), updates must never be
// written under /root. Returns the invoking non-root user's name when we are
// root and SUDO_USER points at a real user, otherwise an empty string (meaning
// "run the update in-process as the current user").
inline QString invokingNonRootUser()
{
#ifndef Q_OS_WIN
    if (geteuid() == 0) {
        const QByteArray sudoUser = qgetenv("SUDO_USER");
        if (!sudoUser.isEmpty() && sudoUser != "root")
            return QString::fromUtf8(sudoUser);
    }
#endif
    return QString();
}

// Home directory for a named user (falls back to /home/<user>).
inline QString homeForUser(const QString &user)
{
#ifndef Q_OS_WIN
    if (!user.isEmpty()) {
        if (struct passwd *pw = getpwnam(user.toLocal8Bit().constData()))
            return QString::fromLocal8Bit(pw->pw_dir);
        return QStringLiteral("/home/") + user;
    }
#endif
    return QDir::homePath();
}

// The managed source checkout directory (holding qt_client/CMakeLists.txt) under
// a specific home directory.
inline QString clientDirUnderHome(const QString &home)
{
    return home + QStringLiteral("/.local/share/forkmesh/src/qt_client");
}

// Single-quote a string for safe use inside an `sh -c` command line.
inline QString shellSingleQuote(const QString &value)
{
    QString escaped = value;
    escaped.replace(QLatin1Char('\''), QStringLiteral("'\\''"));
    return QLatin1Char('\'') + escaped + QLatin1Char('\'');
}

inline QString builtExecutablePath(const QString &buildDir)
{
#ifdef Q_OS_MACOS
    const QString appExecutable = buildDir + "/ForkMesh.app/Contents/MacOS/ForkMesh";
    if (QFileInfo::exists(appExecutable))
        return appExecutable;
#endif
    return buildDir + "/forkmesh";
}

#ifdef Q_OS_MACOS
inline QString brewPrefix(const QString &formula)
{
    const QString brew = QStandardPaths::findExecutable("brew");
    if (brew.isEmpty())
        return {};

    QProcess process;
    process.start(brew, {"--prefix", formula});
    if (!process.waitForFinished(3000) || process.exitCode() != 0)
        return {};
    return QString::fromUtf8(process.readAllStandardOutput()).trimmed();
}
#endif

// How many parallel jobs a local qt_client build may use. cc1plus peaks
// between 0.6 GB and 1.6 GB on the big MainWindow*.cpp translation units, so a
// plain -j<cores> on a many-core box swamps physical RAM and shoves the whole
// machine into swap — and an OOM kill during an in-place update can take out
// the RUNNING node, which nothing restarts (the fleet daemons run under nohup,
// no supervisor). Budget ~3 GiB of RAM per job, never exceeding the core
// count. The Ninja-generator builds additionally gate the heavy targets'
// compiles behind the forkmesh_heavy job pool (see qt_client/CMakeLists.txt);
// this cap is what protects Makefile-generator builds, which ignore pools.
inline int ramCappedBuildJobs()
{
    int jobs = QThread::idealThreadCount();
    const qint64 totalRam = SystemStats::totalMemoryBytes();
    if (totalRam > 0)
        jobs = qBound(1, int(totalRam / (3LL * 1024 * 1024 * 1024)), jobs);
    return jobs;
}

inline QStringList cmakeConfigureArgs(const QString &clientDir, const QString &buildDir,
                               const QString &buildType)
{
    QStringList args{"-S", clientDir, "-B", buildDir,
                     "-DCMAKE_BUILD_TYPE=" + buildType, "-DFORKMESH_BUILD_TESTS=OFF"};
#ifdef Q_OS_MACOS
    const QString qtPrefix = brewPrefix("qt");
    if (!qtPrefix.isEmpty())
        args << "-DCMAKE_PREFIX_PATH=" + qtPrefix;
    const QString opensslPrefix = brewPrefix("openssl@3");
    if (!opensslPrefix.isEmpty())
        args << "-DOPENSSL_ROOT_DIR=" + opensslPrefix;
#endif
    return args;
}

// One command in the "Build & preview" pipeline (issue #214): a program + args
// run in `dir`, with the status line to show while it runs.
struct PullPreviewStep {
    QString program;
    QStringList args;
    QString dir;
    QString status;
};

// Build the ordered command pipeline that checks the PR head (`commit`) out into
// `previewDir` — reusing an existing worktree when `haveWorktree`, otherwise
// registering a fresh one against `gitDir` — then configures and compiles the
// qt_client in `clientDir`/`buildDir` with `jobs` parallel jobs. Pure (no
// filesystem side effects) so the sequence can be unit-tested.
inline QList<PullPreviewStep> pullPreviewSteps(const QString &gitDir,
                                        const QString &previewDir,
                                        const QString &clientDir,
                                        const QString &buildDir,
                                        const QString &commit, bool haveWorktree,
                                        int jobs)
{
    QList<PullPreviewStep> steps;
    // Drop any stale worktree registration so the checkout/add below is clean.
    steps << PullPreviewStep{QStringLiteral("git"),
                             {QStringLiteral("-C"), gitDir,
                              QStringLiteral("worktree"), QStringLiteral("prune")},
                             gitDir, QString::fromUtf8("Preparing worktree\xE2\x80\xA6")};
    if (haveWorktree) {
        // Reuse the existing worktree: just move it to the PR's head commit.
        steps << PullPreviewStep{QStringLiteral("git"),
                                 {QStringLiteral("-C"), previewDir,
                                  QStringLiteral("checkout"), QStringLiteral("--detach"),
                                  QStringLiteral("-f"), commit},
                                 previewDir,
                                 QString::fromUtf8("Checking out the pull request\xE2\x80\xA6")};
    } else {
        steps << PullPreviewStep{QStringLiteral("git"),
                                 {QStringLiteral("-C"), gitDir,
                                  QStringLiteral("worktree"), QStringLiteral("add"),
                                  QStringLiteral("--detach"), previewDir, commit},
                                 gitDir,
                                 QString::fromUtf8("Checking out the pull request\xE2\x80\xA6")};
    }
    steps << PullPreviewStep{QStringLiteral("cmake"),
                             cmakeConfigureArgs(clientDir, buildDir,
                                                QStringLiteral("Release")),
                             clientDir, QString::fromUtf8("Configuring\xE2\x80\xA6")};
    steps << PullPreviewStep{QStringLiteral("cmake"),
                             {QStringLiteral("--build"), buildDir, QStringLiteral("-j"),
                              QString::number(jobs)},
                             buildDir, QString::fromUtf8("Building\xE2\x80\xA6")};
    return steps;
}

const QString kDmPrefix = QStringLiteral("@");

inline bool isDirectConversation(const QString &conversation)
{
    return conversation.startsWith(kDmPrefix);
}

inline QString dmKey(const QString &peerId)
{
    return kDmPrefix + peerId;
}

inline QString dmPeerId(const QString &conversation)
{
    return conversation.mid(1);
}

inline QString repoSegment(QString value, const QString &fallback)
{
    value = value.trimmed().toLower();
    QString out;
    bool lastWasDash = false;
    for (const QChar ch : value) {
        const bool ok = ch.isLetterOrNumber() || ch == '_' || ch == '-';
        if (ok) {
            out.append(ch);
            lastWasDash = false;
        } else if (!lastWasDash) {
            out.append('-');
            lastWasDash = true;
        }
    }
    while (out.startsWith('-'))
        out.remove(0, 1);
    while (out.endsWith('-'))
        out.chop(1);
    if (out.isEmpty())
        out = fallback;
    return out.left(48);
}

inline QString repoNameFromUrl(QString url)
{
    url = url.trimmed();
    url.replace('\\', '/');
    QString name = url.section('/', -1);
    if (name.endsWith(".git"))
        name.chop(4);
    return repoSegment(name, QStringLiteral("repository"));
}

// Username (and legacy node name): a single DNS-like label — lowercase letters,
// digits and hyphens, starting with a letter and ending with a letter or digit,
// max 63 chars. Users and nodes are being split apart — a user signs up with a
// username and can attach many nodes — but both identifiers share this shape,
// and the wire protocol still calls the account field "nodeName". Mirrors
// valid_node_name in the worker and NAME_RE on the website.
inline bool isValidNodeName(const QString &value)
{
    static const QRegularExpression re(
        QStringLiteral("^[a-z](?:[a-z0-9-]{0,61}[a-z0-9])?$"));
    return re.match(value).hasMatch();
}

// A fresh install has no node name yet. Rather than block the welcome screen
// until the user thinks one up, hand them a friendly generated one (Docker
// container name style: "adjective-noun-1234") so the node has a valid name
// and can register/start mirroring immediately; they can still rename
// themselves later from Settings. The vocabulary leans on fork/mesh/git/
// networking words so a generated name reads as a ForkMesh node rather than
// a generic container name. Always satisfies isValidNodeName.
inline QString randomFunNodeName()
{
    static const char *const adjectives[] = {
        "cobalt", "vector", "matrix", "quantum", "neon", "radar",
        "servo", "optic", "circuit", "static", "binary", "ion",
        "modular", "atomic", "thermal", "signal", "carbon", "titanium",
        "magnetic", "packet", "kernel", "proxy", "cache", "relay",
        "armored", "synced", "sharded", "routed", "mirrored", "hashed",
    };
    static const char *const nouns[] = {
        "terminal", "daemon", "router", "gateway", "switch", "beacon",
        "socket", "server", "node", "relay", "mirror", "archive",
        "cluster", "module", "engine", "kernel", "probe", "sensor",
        "uplink", "rack", "core", "bus", "cache", "vault",
        "forge", "drone", "bot", "array", "host", "mesh",
    };
    const int a = QRandomGenerator::global()->bounded(
        int(sizeof(adjectives) / sizeof(adjectives[0])));
    const int n = QRandomGenerator::global()->bounded(
        int(sizeof(nouns) / sizeof(nouns[0])));
    const int suffix = QRandomGenerator::global()->bounded(1000, 10000);
    return QStringLiteral("%1-%2-%3")
        .arg(QLatin1String(adjectives[a]), QLatin1String(nouns[n]))
        .arg(suffix);
}

inline QString accountNameFromInput(QString value, const QString &fallback = QStringLiteral("node"))
{
    value = value.trimmed().toLower();
    QString out;
    for (const QChar &c : value) {
        if (c.unicode() >= 128)
            continue;
        if ((c >= QChar('a') && c <= QChar('z')) ||
            (c >= QChar('0') && c <= QChar('9')) || c == QChar('-'))
            out.append(c);
    }
    // Must start with a letter and not end with a hyphen; cap at 63 chars.
    while (!out.isEmpty() && !(out.at(0) >= QChar('a') && out.at(0) <= QChar('z')))
        out.remove(0, 1);
    out = out.left(63);
    while (!out.isEmpty() && out.endsWith(QChar('-')))
        out.chop(1);
    if (out.isEmpty())
        out = fallback;
    return out;
}

inline bool textMentionsNodeName(const QString &text, const QString &nodeName)
{
    const QString mentionName = accountNameFromInput(nodeName, QString());
    if (!isValidNodeName(mentionName))
        return false;

    static const QRegularExpression mentionRe(
        QStringLiteral("(?:^|[^A-Za-z0-9_-])@([A-Za-z][A-Za-z0-9-]{0,62})(?![A-Za-z0-9-])"));
    auto matches = mentionRe.globalMatch(text);
    while (matches.hasNext()) {
        const QRegularExpressionMatch match = matches.next();
        if (match.captured(1).compare(mentionName, Qt::CaseInsensitive) == 0)
            return true;
    }
    return false;
}

inline QString savedProfileName()
{
    // A node has exactly one identity string. It used to be stored three times
    // (profile/handle, profile/displayName, account/nodeName); it now lives only
    // under account/nodeName. Fall back to the legacy keys so existing installs
    // keep their name on first read after upgrading.
    QSettings settings;
    QString name = settings.value(kAccountNameSetting).toString().trimmed();
    if (name.isEmpty())
        name = settings.value(kHandleSetting).toString().trimmed();
    if (name.isEmpty())
        name = settings.value(kDisplayNameSetting).toString().trimmed();
    return name;
}

inline QString formatRepoDate(qint64 timestampMs)
{
    if (timestampMs <= 0)
        return QStringLiteral("never");
    return QDateTime::fromMSecsSinceEpoch(timestampMs).toString("yyyy-MM-dd hh:mm");
}

inline QString formatIssueRelativeTime(qint64 timestampMs)
{
    if (timestampMs <= 0)
        return QStringLiteral("just now");
    const qint64 secs =
        QDateTime::fromMSecsSinceEpoch(timestampMs).secsTo(QDateTime::currentDateTime());
    if (secs < 60)
        return QStringLiteral("just now");
    const qint64 mins = secs / 60;
    if (mins < 60)
        return mins == 1 ? QStringLiteral("1 minute ago")
                         : QStringLiteral("%1 minutes ago").arg(mins);
    const qint64 hours = mins / 60;
    if (hours < 24)
        return hours == 1 ? QStringLiteral("1 hour ago")
                          : QStringLiteral("%1 hours ago").arg(hours);
    const qint64 days = hours / 24;
    if (days < 30)
        return days == 1 ? QStringLiteral("yesterday")
                         : QStringLiteral("%1 days ago").arg(days);
    const qint64 months = days / 30;
    if (months < 12)
        return months == 1 ? QStringLiteral("last month")
                           : QStringLiteral("%1 months ago").arg(months);
    const qint64 years = days / 365;
    return years <= 1 ? QStringLiteral("last year")
                      : QStringLiteral("%1 years ago").arg(years);
}

// Compact "time ago" for table cells: 29s, 7m, 5h, 3d, 2w, 4mo, 1y.
inline QString formatShortRelativeTime(qint64 timestampSecs)
{
    if (timestampSecs <= 0)
        return QString();
    const qint64 secs = QDateTime::fromSecsSinceEpoch(timestampSecs)
                            .secsTo(QDateTime::currentDateTime());
    if (secs < 0)
        return QStringLiteral("now");
    if (secs < 60)
        return QStringLiteral("%1s").arg(secs);
    const qint64 mins = secs / 60;
    if (mins < 60)
        return QStringLiteral("%1m").arg(mins);
    const qint64 hours = mins / 60;
    if (hours < 24)
        return QStringLiteral("%1h").arg(hours);
    const qint64 days = hours / 24;
    if (days < 7)
        return QStringLiteral("%1d").arg(days);
    const qint64 weeks = days / 7;
    if (days < 30)
        return QStringLiteral("%1w").arg(weeks);
    const qint64 months = days / 30;
    if (months < 12)
        return QStringLiteral("%1mo").arg(months);
    return QStringLiteral("%1y").arg(days / 365);
}

inline QString formatInsightBytes(qint64 bytes)
{
    if (bytes < 1024)
        return QStringLiteral("%1 B").arg(bytes);
    const QStringList units{"KB", "MB", "GB", "TB"};
    double value = double(bytes);
    int unit = -1;
    do {
        value /= 1024.0;
        ++unit;
    } while (value >= 1024.0 && unit + 1 < units.size());
    const int precision = value >= 10.0 ? 0 : 1;
    return QStringLiteral("%1 %2").arg(value, 0, 'f', precision).arg(units.at(unit));
}

inline QString insightMetricCell(const QString &label, const QString &value,
                          const QString &detail = QString())
{
    const QString detailHtml =
        detail.isEmpty()
            ? QString()
            : QStringLiteral("<br><span style='color:#8b949e; font-size:12px'>%1</span>")
                  .arg(detail.toHtmlEscaped());
    return QStringLiteral(
               "<td width='16.6%' style='border:1px solid #30363d; "
               "border-radius:8px; padding:10px 12px;'>"
               "<div style='font-size:21px; font-weight:800'>%1</div>"
               "<div style='color:#8b949e; font-size:12px; font-weight:600'>%2</div>%3"
               "</td>")
        .arg(value.toHtmlEscaped(), label.toHtmlEscaped(), detailHtml);
}

inline QString insightMetricsTable(const QStringList &cells)
{
    QString html =
        QStringLiteral("<table width='100%' cellspacing='8' cellpadding='0'><tr>");
    for (const QString &cell : cells)
        html += cell;
    html += QStringLiteral("</tr></table>");
    return html;
}

inline bool currentThemeIsDark()
{
    const QString pref = QSettings().value(kThemeSetting, "system").toString();
    if (pref == "light")
        return false;
    if (pref == "dark")
        return true;
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
    return QGuiApplication::styleHints()->colorScheme() != Qt::ColorScheme::Light;
#else
    return true;
#endif
}

class IssueBurnupChart final : public QWidget
{
public:
    explicit IssueBurnupChart(QWidget *parent = nullptr) : QWidget(parent)
    {
        setMinimumHeight(340);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    }

    void setSeries(QList<IssueBurnupPoint> series)
    {
        m_series = std::move(series);
        update();
    }

    QSize sizeHint() const override { return QSize(760, 420); }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        const bool dark = currentThemeIsDark();
        const QColor text(dark ? "#e6edf3" : "#1f2328");
        const QColor muted(dark ? "#8b949e" : "#656d76");
        const QColor grid(dark ? "#30363d" : "#d8dee4");
        const QColor openColor(dark ? "#58a6ff" : "#0969da");
        const QColor closedColor(dark ? "#3fb950" : "#1a7f37");
        const QRectF plot = QRectF(rect()).adjusted(54, 18, -20, -46);

        if (plot.width() <= 0 || plot.height() <= 0)
            return;
        if (m_series.isEmpty()) {
            painter.setPen(muted);
            painter.drawText(plot, Qt::AlignCenter,
                             QStringLiteral("No issue history in this range"));
            return;
        }

        int maximum = 1;
        for (const IssueBurnupPoint &point : m_series)
            maximum = qMax(maximum, qMax(point.openCount, point.closedCount));
        const int roundedMaximum = qMax(4, ((maximum + 3) / 4) * 4);

        painter.setFont(font());
        for (int i = 0; i <= 4; ++i) {
            const qreal y = plot.bottom() - plot.height() * i / 4.0;
            painter.setPen(QPen(grid, 1));
            painter.drawLine(QPointF(plot.left(), y), QPointF(plot.right(), y));
            painter.setPen(muted);
            painter.drawText(QRectF(0, y - 10, plot.left() - 8, 20),
                             Qt::AlignRight | Qt::AlignVCenter,
                             QString::number(roundedMaximum * i / 4));
        }

        const qint64 firstTs = m_series.first().timestampMs;
        const qint64 lastTs = m_series.last().timestampMs;
        const qint64 duration = qMax<qint64>(1, lastTs - firstTs);
        auto position = [&](int index, int count) {
            const IssueBurnupPoint &point = m_series.at(index);
            const qreal x = plot.left() +
                            plot.width() * (point.timestampMs - firstTs) / duration;
            const qreal y = plot.bottom() -
                            plot.height() * count / roundedMaximum;
            return QPointF(x, y);
        };

        const QString dateFormat =
            duration <= 2 * 24 * 60 * 60 * 1000LL
                ? QStringLiteral("h AP")
                : (duration <= 14 * 24 * 60 * 60 * 1000LL
                       ? QStringLiteral("ddd")
                       : QStringLiteral("MMM d"));
        for (int tick = 0; tick <= 4; ++tick) {
            const int index = (m_series.size() - 1) * tick / 4;
            const qreal x = position(index, 0).x();
            painter.setPen(muted);
            painter.drawText(
                QRectF(x - 46, plot.bottom() + 10, 92, 24),
                Qt::AlignHCenter | Qt::AlignTop,
                QDateTime::fromMSecsSinceEpoch(m_series.at(index).timestampMs)
                    .toString(dateFormat));
        }

        auto drawSeries = [&](const QColor &color, auto countFor) {
            QPainterPath path;
            for (int i = 0; i < m_series.size(); ++i) {
                const QPointF point = position(i, countFor(m_series.at(i)));
                if (i == 0)
                    path.moveTo(point);
                else
                    path.lineTo(point);
            }
            painter.setPen(QPen(color, 3, Qt::SolidLine, Qt::RoundCap,
                                Qt::RoundJoin));
            painter.setBrush(Qt::NoBrush);
            painter.drawPath(path);
            const QPointF last =
                position(m_series.size() - 1, countFor(m_series.last()));
            painter.setBrush(color);
            painter.setPen(QPen(dark ? QColor("#0d1117") : QColor("#ffffff"), 2));
            painter.drawEllipse(last, 5, 5);
        };

        drawSeries(openColor,
                   [](const IssueBurnupPoint &point) { return point.openCount; });
        drawSeries(closedColor,
                   [](const IssueBurnupPoint &point) { return point.closedCount; });

        painter.setPen(text);
        painter.setBrush(Qt::NoBrush);
        painter.drawRect(plot);
    }

private:
    QList<IssueBurnupPoint> m_series;
};

// One entry in the Size map tab's tree. It lives in DirectorySizeScan.h so
// the scan can also run from the elevated helper process, which links none of
// the widget code (adhoc #76).
using forkmesh::SunburstNode;

// The Size map tab's multi-level pie (adhoc #189): ring 1 is the working
// tree's top-level directories and files, each deeper ring subdivides its
// parent down to individual files.
// Hover shows the exact path/size/share, clicking a directory re-centres the
// chart on it and clicking the hub goes back up one level. Top-level
// directories take fixed categorical hues in size order (never cycled —
// everything past eight goes muted gray) and descendants inherit the parent
// hue stepped toward the surface, so a directory keeps its colour at every
// zoom level. No Q_OBJECT: interaction is self-contained, so it stays a
// header-only widget like the other inline charts.
class RepoSunburstChart final : public QWidget
{
public:
    explicit RepoSunburstChart(QWidget *parent = nullptr) : QWidget(parent)
    {
        setMinimumSize(360, 400);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        setMouseTracking(true);
    }

    void setRoot(SunburstNode root)
    {
        m_root = std::move(root);
        m_trail.clear();
        m_hover = -1;
        update();
    }

    void clear()
    {
        m_basePath.clear();
        setRoot(SunburstNode());
    }

    // Absolute path of the working tree the chart is showing, so a right-click
    // can reveal the hovered directory in the desktop file manager (adhoc #200).
    void setBasePath(const QString &path) { m_basePath = path; }

    QSize sizeHint() const override { return QSize(640, 640); }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        const bool dark = currentThemeIsDark();
        const QColor surface(dark ? "#0d1117" : "#ffffff");
        const QColor text(dark ? "#e6edf3" : "#1f2328");
        const QColor muted(dark ? "#8b949e" : "#656d76");
        m_segments.clear();

        const SunburstNode *focus = focusNode();
        if (!focus || focus->size <= 0) {
            painter.setPen(muted);
            painter.drawText(rect(), Qt::AlignCenter,
                             QStringLiteral("No files to chart yet"));
            return;
        }

        const Geometry g = geometry_();
        layoutRing(*focus, 1, 90.0, -360.0, m_trail);

        for (int i = 0; i < m_segments.size(); ++i) {
            const Segment &seg = m_segments.at(i);
            const qreal r0 = g.hole + (seg.depth - 1) * g.ringWidth;
            const qreal r1 = r0 + g.ringWidth;
            QPainterPath path;
            const QRectF outer(g.center.x() - r1, g.center.y() - r1, 2 * r1, 2 * r1);
            const QRectF inner(g.center.x() - r0, g.center.y() - r0, 2 * r0, 2 * r0);
            path.arcMoveTo(outer, seg.startDeg);
            path.arcTo(outer, seg.startDeg, seg.spanDeg);
            path.arcTo(inner, seg.startDeg + seg.spanDeg, -seg.spanDeg);
            path.closeSubpath();
            QColor fill = seg.color;
            if (i == m_hover)
                fill = dark ? fill.lighter(125) : fill.darker(112);
            painter.setPen(QPen(surface, 2));
            painter.setBrush(fill);
            painter.drawPath(path);
        }

        // Direct labels only where they comfortably fit (over ~18 degrees);
        // the hover tooltip carries every other value.
        painter.setFont(font());
        for (const Segment &seg : m_segments) {
            if (qAbs(seg.spanDeg) < 18.0)
                continue;
            const qreal midDeg = seg.startDeg + seg.spanDeg / 2;
            const qreal midRad = midDeg * M_PI / 180.0;
            const qreal r = g.hole + (seg.depth - 1) * g.ringWidth + g.ringWidth / 2;
            const QPointF at(g.center.x() + r * std::cos(midRad),
                             g.center.y() - r * std::sin(midRad));
            const QString name = painter.fontMetrics().elidedText(
                seg.name, Qt::ElideRight, int(g.ringWidth * 1.8));
            const QRectF box(at.x() - 70, at.y() - 15, 140, 30);
            painter.setPen(QColor(0, 0, 0, 150));
            painter.drawText(box.translated(1, 1), Qt::AlignCenter,
                             name + "\n" + QLocale().formattedDataSize(seg.size));
            painter.setPen(Qt::white);
            painter.drawText(box, Qt::AlignCenter,
                             name + "\n" + QLocale().formattedDataSize(seg.size));
        }

        // Hub: the focused directory's name and total; when zoomed, hint that
        // clicking it goes back up.
        painter.setPen(text);
        QFont hubFont = font();
        hubFont.setBold(true);
        painter.setFont(hubFont);
        const QRectF hub(g.center.x() - g.hole, g.center.y() - g.hole,
                         2 * g.hole, 2 * g.hole);
        const QString hubName = m_trail.isEmpty()
                                    ? (m_root.name.isEmpty()
                                           ? QStringLiteral("repository")
                                           : m_root.name)
                                    : focus->name;
        painter.drawText(hub.adjusted(6, 0, -6, -14), Qt::AlignCenter,
                         painter.fontMetrics().elidedText(
                             hubName, Qt::ElideMiddle, int(g.hole * 1.7)));
        painter.setFont(font());
        painter.setPen(muted);
        painter.drawText(hub.adjusted(6, 22, -6, 0), Qt::AlignCenter,
                         QLocale().formattedDataSize(focus->size) +
                             (m_trail.isEmpty() ? QString()
                                                : QStringLiteral("\n⌃ up")));
    }

    void mouseMoveEvent(QMouseEvent *event) override
    {
        const int hit = segmentAt(event->pos());
        if (hit != m_hover) {
            m_hover = hit;
            update();
        }
        if (hit >= 0) {
            const Segment &seg = m_segments.at(hit);
            setCursor(seg.hasChildren ? Qt::PointingHandCursor : Qt::ArrowCursor);
            const qreal pct = m_root.size > 0
                                  ? 100.0 * double(seg.size) / double(m_root.size)
                                  : 0.0;
            QToolTip::showText(
                event->globalPosition().toPoint(),
                QStringLiteral("%1 — %2 · %3% of repo")
                    .arg(seg.path, QLocale().formattedDataSize(seg.size),
                         QString::number(pct, 'f', 1)),
                this);
        } else {
            QToolTip::hideText();
            setCursor(!m_trail.isEmpty() && inHub(event->pos())
                          ? Qt::PointingHandCursor
                          : Qt::ArrowCursor);
        }
        QWidget::mouseMoveEvent(event);
    }

    void mousePressEvent(QMouseEvent *event) override
    {
        if (event->button() == Qt::LeftButton) {
            if (inHub(event->pos())) {
                if (!m_trail.isEmpty()) {
                    m_trail.removeLast();
                    m_hover = -1;
                    update();
                }
                return;
            }
            const int hit = segmentAt(event->pos());
            if (hit >= 0 && m_segments.at(hit).hasChildren) {
                m_trail = m_segments.at(hit).trail;
                m_hover = -1;
                update();
                return;
            }
        }
        QWidget::mousePressEvent(event);
    }

    void leaveEvent(QEvent *event) override
    {
        m_hover = -1;
        update();
        QWidget::leaveEvent(event);
    }

    // Right-click a ring segment (or the hub) to open that directory in the
    // desktop file manager (adhoc #200). Paths come straight from the segment
    // trail, so they line up with whatever the working-tree scan produced.
    void contextMenuEvent(QContextMenuEvent *event) override
    {
        if (m_basePath.isEmpty()) {
            QWidget::contextMenuEvent(event);
            return;
        }
        const int hit = segmentAt(event->pos());
        QString rel;
        QString label;
        if (hit >= 0) {
            rel = m_segments.at(hit).path;
            label = m_segments.at(hit).name;
        } else if (inHub(event->pos())) {
            rel = focusRelativePath();
            const SunburstNode *focus = focusNode();
            label = focus && !focus->name.isEmpty() ? focus->name
                                                    : QStringLiteral("repository");
        } else {
            QWidget::contextMenuEvent(event);
            return;
        }
        const QString target =
            rel.isEmpty() ? m_basePath : QDir(m_basePath).filePath(rel);
        // File slices reveal their containing directory (a file itself can't
        // be opened as a folder).
        const QFileInfo targetInfo(target);
        const QString dir = targetInfo.isDir()
                                ? target
                                : (targetInfo.isFile() ? targetInfo.absolutePath()
                                                       : QString());
        if (dir.isEmpty() || !QDir(dir).exists()) {
            QWidget::contextMenuEvent(event);
            return;
        }
        QMenu menu(this);
        QAction *open = menu.addAction(
            QStringLiteral("Open \"%1\" in file explorer").arg(label));
        connect(open, &QAction::triggered, this,
                [dir] { QDesktopServices::openUrl(QUrl::fromLocalFile(dir)); });
        menu.exec(event->globalPos());
    }

private:
    static constexpr int kRings = 4;

    struct Segment {
        qreal startDeg = 0; // Qt convention: 0° at 3 o'clock, CCW positive
        qreal spanDeg = 0;  // negative = clockwise on screen
        int depth = 1;      // 1-based ring index from the focus
        qint64 size = 0;
        QString name;
        QString path; // repo-relative, for the tooltip
        QColor color;
        bool hasChildren = false;
        QList<int> trail; // child-index chain from the root to this node
    };

    struct Geometry {
        QPointF center;
        qreal hole = 0;
        qreal ringWidth = 0;
    };

    Geometry geometry_() const
    {
        Geometry g;
        g.center = QPointF(width() / 2.0, height() / 2.0);
        const qreal radius = qMax<qreal>(60.0, qMin(width(), height()) / 2.0 - 8);
        g.hole = qMax<qreal>(34.0, radius * 0.24);
        g.ringWidth = (radius - g.hole) / kRings;
        return g;
    }

    const SunburstNode *focusNode() const
    {
        const SunburstNode *node = &m_root;
        for (int index : m_trail) {
            if (index < 0 || index >= node->children.size())
                return &m_root;
            node = &node->children.at(index);
        }
        return node;
    }

    // Repo-relative path of the currently focused directory (empty at the root),
    // matching the naming Segment::path uses.
    QString focusRelativePath() const
    {
        QStringList names;
        const SunburstNode *node = &m_root;
        for (int index : m_trail) {
            if (index < 0 || index >= node->children.size())
                return QString();
            node = &node->children.at(index);
            names.append(node->name);
        }
        return names.join(QLatin1Char('/'));
    }

    // Fixed categorical slots for ring 1 (stepped for dark/light surfaces);
    // deeper rings shade the inherited hue toward the surface so depth reads
    // as lightness, and odd siblings get a nudge so same-hue neighbours stay
    // separable next to the 2px gaps.
    QColor slotColor(int index, bool dark) const
    {
        static const char *kDark[] = {"#3987e5", "#199e70", "#c98500",
                                      "#008300", "#9085e9", "#e66767",
                                      "#d55181", "#d95926"};
        static const char *kLight[] = {"#2a78d6", "#1baf7a", "#eda100",
                                       "#008300", "#4a3aa7", "#e34948",
                                       "#e87ba4", "#eb6834"};
        if (index < 0 || index >= 8)
            return QColor("#8a8a8a");
        return QColor(dark ? kDark[index] : kLight[index]);
    }

    static QColor shaded(const QColor &base, int depth, bool dark, int index)
    {
        const qreal f = qMin(0.55, (depth - 1) * 0.16 +
                                       (depth > 1 ? (index % 2) * 0.06 : 0.0));
        const int toward = dark ? 255 : 0;
        return QColor(int(base.red() + (toward - base.red()) * f),
                      int(base.green() + (toward - base.green()) * f),
                      int(base.blue() + (toward - base.blue()) * f));
    }

    // The colour of the node `trail` points at has to survive zooming, so it
    // is always derived from the FULL tree: slot by ring-1 child index, then
    // shaded by absolute depth.
    QColor colorForTrail(const QList<int> &trail, bool dark) const
    {
        if (trail.isEmpty())
            return QColor("#8a8a8a");
        return shaded(slotColor(trail.first(), dark), trail.size(), dark,
                      trail.last());
    }

    void layoutRing(const SunburstNode &node, int depth, qreal startDeg,
                    qreal spanDeg, const QList<int> &trail)
    {
        if (depth > kRings || node.size <= 0)
            return;
        const bool dark = currentThemeIsDark();
        qreal at = startDeg;
        for (int i = 0; i < node.children.size(); ++i) {
            const SunburstNode &child = node.children.at(i);
            if (child.size <= 0)
                continue;
            const qreal childSpan =
                spanDeg * qMin<qreal>(1.0, double(child.size) / double(node.size));
            if (qAbs(childSpan) >= 0.4) {
                QList<int> childTrail = trail;
                childTrail.append(i);
                QStringList names;
                const SunburstNode *walk = &m_root;
                for (int index : childTrail) {
                    walk = &walk->children.at(index);
                    names.append(walk->name);
                }
                Segment seg;
                seg.startDeg = at;
                seg.spanDeg = childSpan;
                seg.depth = depth;
                seg.size = child.size;
                seg.name = child.name;
                seg.path = names.join(QLatin1Char('/'));
                seg.color = colorForTrail(childTrail, dark);
                seg.hasChildren = !child.children.isEmpty();
                seg.trail = childTrail;
                m_segments.append(seg);
                layoutRing(child, depth + 1, at, childSpan, childTrail);
            }
            at += childSpan;
        }
    }

    bool inHub(const QPoint &pos) const
    {
        const Geometry g = geometry_();
        return QLineF(g.center, pos).length() <= g.hole;
    }

    int segmentAt(const QPoint &pos) const
    {
        const Geometry g = geometry_();
        const qreal r = QLineF(g.center, pos).length();
        if (r <= g.hole)
            return -1;
        // Same convention as the segments: degrees CCW from 3 o'clock.
        qreal angle = std::atan2(g.center.y() - pos.y(), pos.x() - g.center.x())
                      * 180.0 / M_PI;
        for (int i = 0; i < m_segments.size(); ++i) {
            const Segment &seg = m_segments.at(i);
            const qreal r0 = g.hole + (seg.depth - 1) * g.ringWidth;
            const qreal r1 = r0 + g.ringWidth;
            if (r < r0 || r > r1)
                continue;
            // Segments run clockwise (negative span) from startDeg; normalise
            // the cursor angle into [start+span, start].
            qreal delta = std::fmod(seg.startDeg - angle, 360.0);
            if (delta < 0)
                delta += 360.0;
            if (delta <= qAbs(seg.spanDeg))
                return i;
        }
        return -1;
    }

    SunburstNode m_root;
    QString m_basePath; // absolute working-tree path, for "open in file explorer"
    QList<int> m_trail; // child-index chain from the root to the focus
    int m_hover = -1;
    mutable QVector<Segment> m_segments; // rebuilt each paint (geometry-dependent)
};

// One mounted filesystem beside the big size map (adhoc #21): a small
// used/free donut plus the mount point, already drawn so the whole set reads
// as a column of tiny maps. Clicking one re-roots the full scan on that mount,
// which is how "/" (or any other filesystem) gets expanded without the folder
// picker. Header-only with a plain callback, like the other Internal.h mini
// widgets — no Q_OBJECT, so no moc entry is needed.
class StorageMiniMap final : public QWidget
{
public:
    explicit StorageMiniMap(const QStorageInfo &volume, QWidget *parent = nullptr)
        : QWidget(parent), m_mountPoint(volume.rootPath()),
          m_device(QString::fromUtf8(volume.device())),
          m_type(QString::fromUtf8(volume.fileSystemType())),
          m_total(qMax<qint64>(0, volume.bytesTotal())),
          m_used(qMax<qint64>(0, volume.bytesTotal() - volume.bytesAvailable()))
    {
        setCursor(Qt::PointingHandCursor);
        setMouseTracking(true);
        setMinimumHeight(56);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        setToolTip(QStringLiteral("%1\n%2 · %3\n%4 used of %5")
                       .arg(QDir::toNativeSeparators(m_mountPoint), m_device,
                            m_type, QLocale().formattedDataSize(m_used),
                            QLocale().formattedDataSize(m_total)));
    }

    QString mountPoint() const { return m_mountPoint; }

    void setOnClicked(std::function<void()> callback)
    {
        m_onClicked = std::move(callback);
    }

    // Marks the filesystem the big map is currently showing.
    void setSelected(bool selected)
    {
        if (m_selected == selected)
            return;
        m_selected = selected;
        update();
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        const bool dark = currentThemeIsDark();
        const QColor text(dark ? "#e6edf3" : "#1f2328");
        const QColor muted(dark ? "#8b949e" : "#656d76");
        const QColor track(dark ? "#30363d" : "#d0d7de");
        const QColor used(dark ? "#3987e5" : "#2a78d6");
        const QColor accent(dark ? "#58a6ff" : "#0969da");

        if (m_selected || m_hover) {
            painter.setPen(m_selected ? QPen(accent, 1) : Qt::NoPen);
            painter.setBrush(QColor(dark ? "#161b22" : "#f6f8fa"));
            painter.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5),
                                    6, 6);
        }

        const qreal ring = 6.0;
        const qreal diameter = qMin<qreal>(38.0, height() - 12);
        const QRectF donut(8 + ring / 2, (height() - diameter) / 2.0 + ring / 2,
                           diameter - ring, diameter - ring);
        painter.setBrush(Qt::NoBrush);
        painter.setPen(QPen(track, ring, Qt::SolidLine, Qt::FlatCap));
        painter.drawEllipse(donut);
        const qreal fraction =
            m_total > 0 ? qBound<qreal>(0.0, double(m_used) / double(m_total), 1.0)
                        : 0.0;
        if (fraction > 0) {
            painter.setPen(QPen(used, ring, Qt::SolidLine, Qt::FlatCap));
            painter.drawArc(donut, 90 * 16, int(-fraction * 360 * 16));
        }

        QFont pct = font();
        pct.setPointSizeF(qMax<qreal>(7.0, font().pointSizeF() - 2.5));
        painter.setFont(pct);
        painter.setPen(muted);
        painter.drawText(donut.adjusted(-ring, -ring, ring, ring), Qt::AlignCenter,
                         QStringLiteral("%1%").arg(qRound(fraction * 100)));

        const int textLeft = int(8 + diameter + 10);
        const QRect textArea(textLeft, 6, width() - textLeft - 8, height() - 12);
        QFont title = font();
        title.setBold(true);
        painter.setFont(title);
        painter.setPen(m_selected ? accent : text);
        const QRect titleRect(textArea.left(), textArea.top(), textArea.width(),
                              textArea.height() / 2);
        painter.drawText(titleRect, Qt::AlignLeft | Qt::AlignVCenter,
                         painter.fontMetrics().elidedText(
                             QDir::toNativeSeparators(m_mountPoint),
                             Qt::ElideMiddle, titleRect.width()));
        painter.setFont(font());
        painter.setPen(muted);
        const QRect subRect(textArea.left(), textArea.center().y(),
                            textArea.width(), textArea.height() / 2);
        painter.drawText(subRect, Qt::AlignLeft | Qt::AlignVCenter,
                         painter.fontMetrics().elidedText(
                             QStringLiteral("%1 of %2 · %3")
                                 .arg(QLocale().formattedDataSize(m_used),
                                      QLocale().formattedDataSize(m_total), m_type),
                             Qt::ElideRight, subRect.width()));
    }

    void enterEvent(QEnterEvent *event) override
    {
        m_hover = true;
        update();
        QWidget::enterEvent(event);
    }

    void leaveEvent(QEvent *event) override
    {
        m_hover = false;
        update();
        QWidget::leaveEvent(event);
    }

    void mouseReleaseEvent(QMouseEvent *event) override
    {
        if (event->button() != Qt::LeftButton || !rect().contains(event->pos()) ||
            !m_onClicked) {
            QWidget::mouseReleaseEvent(event);
            return;
        }
        // The callback re-roots the map, which rebuilds this whole column and
        // deleteLater()s this very card — and the rescan behind it pumps the
        // event loop (runGitCapture), so `this` can already be gone when it
        // returns. Copy the callback out, accept the event first, and touch
        // nothing afterwards.
        const std::function<void()> callback = m_onClicked;
        event->accept();
        callback();
    }

private:
    QString m_mountPoint;
    QString m_device;
    QString m_type;
    qint64 m_total = 0;
    qint64 m_used = 0;
    bool m_hover = false;
    bool m_selected = false;
    std::function<void()> m_onClicked;
};

inline QString formatDuration(qint64 ms)
{
    const qint64 totalSeconds = std::max<qint64>(0, ms / 1000);
    const qint64 hours = totalSeconds / 3600;
    const qint64 minutes = (totalSeconds % 3600) / 60;
    const qint64 seconds = totalSeconds % 60;
    if (hours > 0)
        return QStringLiteral("%1h %2m").arg(hours).arg(minutes, 2, 10, QChar('0'));
    if (minutes > 0)
        return QStringLiteral("%1m %2s").arg(minutes).arg(seconds, 2, 10, QChar('0'));
    return QStringLiteral("%1s").arg(seconds);
}

inline QString compactAddress(QString address)
{
    address = address.trimmed();
    if (address.size() <= 30)
        return address;
    return address.left(18) + QStringLiteral("...") + address.right(8);
}

inline QStringList splitIssueFieldList(const QString &text)
{
    QStringList values;
    QSet<QString> seen;
    for (const QString &part : text.split(',', Qt::SkipEmptyParts)) {
        const QString value = part.trimmed();
        if (value.isEmpty() || seen.contains(value))
            continue;
        values << value;
        seen.insert(value);
    }
    return values;
}

// Every raster icon below is drawn at the app's device-pixel-ratio and tagged
// with it, so HiDPI screens composite the pixmap 1:1 instead of upscaling a
// logical-size raster into a pixelated blur (adhoc #139).
inline qreal iconDevicePixelRatio()
{
    const qreal dpr = qGuiApp ? qGuiApp->devicePixelRatio() : 1.0;
    return dpr > 0.0 ? dpr : 1.0;
}

// A w×h-logical-pixel transparent pixmap backed by a DPR-scaled raster;
// QPainter coordinates on it stay logical.
inline QPixmap crispIconPixmap(int w, int h, qreal dpr)
{
    QPixmap pm(qMax(1, qRound(w * dpr)), qMax(1, qRound(h * dpr)));
    pm.setDevicePixelRatio(dpr);
    pm.fill(Qt::transparent);
    return pm;
}

inline QPixmap crispIconPixmap(int size, qreal dpr)
{
    return crispIconPixmap(size, size, dpr);
}

// A small platform emoji for a node's operating system.
// Crisp vector icons for the server-rail footer (glyph fonts render these
// inconsistently across platforms, so we draw them).
inline QPixmap refreshPixmap(const QColor &color, double angleDeg, int size)
{
    QPixmap pm = crispIconPixmap(size, iconDevicePixelRatio());
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    p.translate(size / 2.0, size / 2.0);
    p.rotate(angleDeg);
    const double r = size * 0.28;
    QPen pen(color, std::max(1.6, size * 0.10));
    pen.setCapStyle(Qt::RoundCap);
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);
    p.drawArc(QRectF(-r, -r, 2 * r, 2 * r), 95 * 16, 250 * 16);
    // Arrowhead at the arc's open end.
    p.setPen(Qt::NoPen);
    p.setBrush(color);
    const double a = size * 0.14;
    QPainterPath tri;
    tri.moveTo(a * 0.2, -r - a * 0.7);
    tri.lineTo(a * 0.2, -r + a * 0.7);
    tri.lineTo(a * 1.2, -r);
    tri.closeSubpath();
    p.drawPath(tri);
    return pm;
}

// An hourglass, used in place of the spinning-arrows icon when a button's
// action is queued behind other work rather than actively running.
inline QPixmap hourglassPixmap(const QColor &color, double angleDeg, int size)
{
    QPixmap pm = crispIconPixmap(size, iconDevicePixelRatio());
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    p.translate(size / 2.0, size / 2.0);
    p.rotate(angleDeg);
    const double w = size * 0.34;
    const double h = size * 0.34;
    QPen pen(color, std::max(1.4, size * 0.09));
    pen.setJoinStyle(Qt::RoundJoin);
    pen.setCapStyle(Qt::RoundCap);
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);
    QPainterPath glass;
    glass.moveTo(-w, -h);
    glass.lineTo(w, -h);
    glass.lineTo(-w, h);
    glass.lineTo(w, h);
    glass.closeSubpath();
    p.drawPath(glass);
    return pm;
}

// A deterministic procedural *face* avatar. Each seed maps, via SHA-256 + a
// splitmix64 PRNG, to a unique cartoon face — backdrop, skin tone, hairstyle &
// colour, brows, eyes, nose, mouth and the odd extra (glasses, beard, blush,
// freckles). The same seed always yields the same face, so a node's identity
// reads consistently everywhere it appears, while the huge feature space keeps
// every node clearly distinguishable at a glance.
inline QByteArray forkMeshAvatarPng(const QString &seed)
{
    const QByteArray h =
        QCryptographicHash::hash(seed.toUtf8(), QCryptographicHash::Sha256);
    // Fold the whole digest into a 64-bit seed (FNV-1a), then stream unlimited
    // entropy out of it with splitmix64 so every feature draws independently.
    quint64 state = 0xCBF29CE484222325ULL;
    for (char c : h)
        state = (state ^ static_cast<quint8>(c)) * 0x100000001B3ULL;
    auto nextU64 = [&state]() {
        state += 0x9E3779B97F4A7C15ULL;
        quint64 z = state;
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
        return z ^ (z >> 31);
    };
    auto rnd = [&](int n) { return n > 0 ? int(nextU64() % quint64(n)) : 0; };
    auto chance = [&](int pct) { return int(nextU64() % 100) < pct; };

    // Curated palettes — vivid backdrops, natural skin tones, natural + a few
    // playful dyed hair colours.
    static const char *kBackdrops[][2] = {
        {"#1f6feb", "#0d419d"}, {"#2ea043", "#176f2c"}, {"#bc8cff", "#8957e5"},
        {"#db61a2", "#bf3989"}, {"#f0883e", "#bd561d"}, {"#39c5cf", "#1b7c83"},
        {"#e3b341", "#b08800"}, {"#fb7185", "#be123c"}, {"#6e7bf2", "#414bb2"},
        {"#34d399", "#059669"}, {"#22d3ee", "#0e7490"}, {"#f471b5", "#a3367f"}};
    static const char *kSkins[] = {"#ffe0bd", "#ffcd94", "#f1c27d", "#e0ac69",
                                   "#c68642", "#a8703e", "#8d5524", "#613a1f"};
    static const char *kHairs[] = {
        "#2c1b18", "#3b2417", "#5a3825", "#7a4a2b", "#a55728", "#c89f6d",
        "#e6cea0", "#d7d7d7", "#f2f2f2", "#e25563", "#5b6ee1", "#34a853",
        "#9b59b6", "#ff8fab"};
    const int nBack = int(sizeof(kBackdrops) / sizeof(kBackdrops[0]));
    const int nSkin = int(sizeof(kSkins) / sizeof(kSkins[0]));
    const int nHair = int(sizeof(kHairs) / sizeof(kHairs[0]));

    const int S = 128;
    QImage img(S, S, QImage::Format_ARGB32_Premultiplied);
    img.fill(Qt::transparent);
    QPainter p(&img);
    p.setRenderHint(QPainter::Antialiasing);

    // --- Backdrop: diagonal gradient + a soft spotlight behind the head. ---
    const int bg = rnd(nBack);
    QLinearGradient grad(0, 0, S, S);
    grad.setColorAt(0.0, QColor(kBackdrops[bg][0]));
    grad.setColorAt(1.0, QColor(kBackdrops[bg][1]));
    p.fillRect(QRectF(0, 0, S, S), QBrush(grad));
    QColor glowC = QColor(kBackdrops[bg][0]).lighter(140);
    glowC.setAlpha(115);
    QRadialGradient halo(QPointF(S / 2.0, S * 0.44), S * 0.62);
    halo.setColorAt(0.0, glowC);
    glowC.setAlpha(0);
    halo.setColorAt(1.0, glowC);
    p.fillRect(QRectF(0, 0, S, S), QBrush(halo));

    // --- Geometry & colours. ---
    const qreal cx = S / 2.0;
    const qreal faceCy = 73, faceHW = 35, faceHH = 40;
    const QRectF faceRect(cx - faceHW, faceCy - faceHH, faceHW * 2, faceHH * 2);
    const qreal headTop = faceRect.top();
    const qreal eyeY = 72, eyeDX = 14, browY = 60, mouthY = 94;
    const qreal lx = cx - eyeDX, rxe = cx + eyeDX;

    const QColor skin(kSkins[rnd(nSkin)]);
    const QColor skinShadow = skin.darker(120);
    const QColor hair(kHairs[rnd(nHair)]);
    QColor brow = hair.darker(135);
    if (brow.lightnessF() > 0.65)
        brow = QColor("#6b4f3a");
    const int hairStyle = rnd(9); // 0 bald · 1 buzz · 2 short · 3 side-part ·
                                  // 4 flat-top · 5 afro · 6 long · 7 bun · 8 mohawk

    // Lay down hair that sits *behind* the head (long hair frames the face).
    if (hairStyle == 6) {
        p.setPen(Qt::NoPen);
        p.setBrush(hair);
        QPainterPath bk;
        bk.addRoundedRect(QRectF(cx - faceHW - 7, headTop - 2,
                                 (faceHW + 7) * 2, faceHH * 2 + 20),
                          28, 28);
        p.drawPath(bk);
    }

    // Ears, then the face on top.
    p.setPen(Qt::NoPen);
    p.setBrush(skin);
    p.drawEllipse(QPointF(faceRect.left() + 3, faceCy + 3), 7, 9);
    p.drawEllipse(QPointF(faceRect.right() - 3, faceCy + 3), 7, 9);
    p.drawEllipse(faceRect);
    p.setBrush(Qt::NoBrush);
    p.setPen(QPen(skin.darker(123), 1.6));
    p.drawEllipse(faceRect);
    p.setPen(Qt::NoPen);

    // Fill the scalp above a hairline, following the round crown.
    auto fillScalp = [&](qreal hairlineY, qreal grow) {
        p.save();
        QPainterPath clip;
        clip.addEllipse(faceRect.adjusted(-grow, -grow, grow, 0));
        p.setClipPath(clip);
        p.setPen(Qt::NoPen);
        p.setBrush(hair);
        p.drawRect(QRectF(0, 0, S, hairlineY));
        p.restore();
    };

    // --- Hair on top of the head. ---
    if (hairStyle == 0) { // bald — just a faint scalp highlight
        QColor shine = skin.lighter(115);
        shine.setAlpha(120);
        p.setPen(Qt::NoPen);
        p.setBrush(shine);
        p.drawEllipse(QPointF(cx - 9, headTop + 16), 10, 6);
    } else if (hairStyle == 1) { // buzz cut
        fillScalp(56, 1.5);
    } else if (hairStyle == 2) { // short
        fillScalp(53, 5);
    } else if (hairStyle == 3) { // side part + swooped bang
        fillScalp(51, 6);
        p.setPen(QPen(skin, 2.6, Qt::SolidLine, Qt::RoundCap));
        p.setBrush(Qt::NoBrush);
        p.drawLine(QPointF(cx - 4, headTop + 3), QPointF(cx - 13, 50));
        p.setPen(Qt::NoPen);
        p.setBrush(hair);
        QPainterPath sw;
        sw.moveTo(cx - 8, 48);
        sw.cubicTo(cx + 16, 42, cx + 22, 54, cx + 16, 60);
        sw.cubicTo(cx + 8, 54, cx - 2, 54, cx - 8, 53);
        p.drawPath(sw);
    } else if (hairStyle == 4) { // flat top
        fillScalp(52, 3);
        p.setPen(Qt::NoPen);
        p.setBrush(hair);
        QPainterPath cap;
        cap.addRoundedRect(
            QRectF(cx - faceHW * 0.9, headTop - 9, faceHW * 1.8, 22), 6, 6);
        p.drawPath(cap);
    } else if (hairStyle == 5) { // afro / curly
        p.setPen(Qt::NoPen);
        p.setBrush(hair);
        const qreal cyr = headTop + 4;
        for (int i = 0; i < 9; ++i) {
            const qreal ang = M_PI * (0.06 + 0.88 * i / 8.0);
            const qreal px = cx - (faceHW + 5) * qCos(ang);
            const qreal py = cyr - (faceHH * 0.66) * qSin(ang);
            p.drawEllipse(QPointF(px, py), 12.5, 12.5);
        }
        fillScalp(55, 9);
    } else if (hairStyle == 6) { // long (back panel already drawn)
        fillScalp(51, 6);
    } else if (hairStyle == 7) { // top knot / bun
        fillScalp(51, 5);
        p.setPen(Qt::NoPen);
        p.setBrush(hair);
        p.drawEllipse(QPointF(cx, headTop - 5), 10, 10);
        p.setPen(QPen(hair.darker(130), 3));
        p.setBrush(Qt::NoBrush);
        p.drawArc(QRectF(cx - 9, headTop + 1, 18, 10), 200 * 16, 140 * 16);
        p.setPen(Qt::NoPen);
    } else { // mohawk
        p.setPen(Qt::NoPen);
        p.setBrush(hair);
        QPainterPath mo;
        mo.moveTo(cx - 8, 54);
        mo.lineTo(cx - 9, headTop - 14);
        mo.quadTo(cx, headTop - 22, cx + 9, headTop - 14);
        mo.lineTo(cx + 8, 54);
        mo.quadTo(cx, 50, cx - 8, 54);
        p.drawPath(mo);
    }

    // --- Eyebrows. ---
    const int browStyle = rnd(4); // 0 flat · 1 raised · 2 angry · 3 worried
    if (chance(85)) {
        auto drawBrow = [&](qreal ex, bool right) {
            qreal inY = browY, outY = browY;
            if (browStyle == 1) { inY -= 2; outY -= 4; }
            else if (browStyle == 2) { inY += 2.5; outY -= 1.5; }
            else if (browStyle == 3) { inY -= 2.5; outY += 1.5; }
            const qreal innerX = right ? ex - 6.5 : ex + 6.5;
            const qreal outerX = right ? ex + 6.5 : ex - 6.5;
            p.setPen(QPen(brow, 3.0, Qt::SolidLine, Qt::RoundCap));
            p.setBrush(Qt::NoBrush);
            p.drawLine(QPointF(innerX, inY), QPointF(outerX, outY));
            p.setPen(Qt::NoPen);
        };
        drawBrow(lx, false);
        drawBrow(rxe, true);
    }

    // --- Eyes (one may wink). ---
    auto drawEye = [&](qreal ex, qreal ey, int style, bool right) {
        const QColor dark("#20232a");
        p.setPen(Qt::NoPen);
        if (style == 0) { // bold dot with a catch-light
            p.setBrush(dark);
            p.drawEllipse(QPointF(ex, ey), 5.3, 6.0);
            p.setBrush(QColor(255, 255, 255, 235));
            p.drawEllipse(QPointF(ex - 1.6, ey - 2.0), 1.5, 1.5);
        } else if (style == 1) { // white + steerable pupil
            p.setBrush(Qt::white);
            p.drawEllipse(QPointF(ex, ey), 6.6, 7.3);
            const qreal gaze = right ? 1.4 : -1.4;
            p.setBrush(dark);
            p.drawEllipse(QPointF(ex + gaze, ey + 0.5), 3.4, 3.8);
            p.setBrush(QColor(255, 255, 255, 235));
            p.drawEllipse(QPointF(ex + gaze - 1.2, ey - 1.1), 1.2, 1.2);
        } else if (style == 2) { // sleepy line
            p.setPen(QPen(dark, 3.0, Qt::SolidLine, Qt::RoundCap));
            p.setBrush(Qt::NoBrush);
            p.drawLine(QPointF(ex - 5, ey + 1), QPointF(ex + 5, ey + 1));
            p.setPen(Qt::NoPen);
        } else { // happy arch
            p.setPen(QPen(dark, 3.0, Qt::SolidLine, Qt::RoundCap));
            p.setBrush(Qt::NoBrush);
            p.drawArc(QRectF(ex - 6, ey - 4, 12, 11), 20 * 16, 140 * 16);
            p.setPen(Qt::NoPen);
        }
    };
    const int eyeStyle = rnd(4);
    const bool wink = chance(12);
    drawEye(lx, eyeY, eyeStyle, false);
    drawEye(rxe, eyeY, wink ? 3 : eyeStyle, true);

    // --- Glasses (a strong identifier). ---
    if (chance(30)) {
        const bool roundLens = chance(60);
        QColor frame = chance(22)
                           ? QColor(kBackdrops[(bg + 4) % nBack][0]).darker(115)
                           : QColor("#23262e");
        p.setPen(QPen(frame, 2.6, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p.setBrush(QColor(255, 255, 255, 38));
        const qreal r = 9.5;
        if (roundLens) {
            p.drawEllipse(QPointF(lx, eyeY), r, r);
            p.drawEllipse(QPointF(rxe, eyeY), r, r);
        } else {
            p.drawRoundedRect(QRectF(lx - r, eyeY - r * 0.85, 2 * r, 1.7 * r), 3, 3);
            p.drawRoundedRect(QRectF(rxe - r, eyeY - r * 0.85, 2 * r, 1.7 * r), 3, 3);
        }
        p.setBrush(Qt::NoBrush);
        p.drawLine(QPointF(lx + r, eyeY - 1), QPointF(rxe - r, eyeY - 1));
        p.drawLine(QPointF(lx - r, eyeY - 1), QPointF(faceRect.left() + 1, eyeY - 3));
        p.drawLine(QPointF(rxe + r, eyeY - 1), QPointF(faceRect.right() - 1, eyeY - 3));
        p.setPen(Qt::NoPen);
    }

    // --- Nose. ---
    const int noseStyle = rnd(3);
    p.setPen(Qt::NoPen);
    p.setBrush(skinShadow);
    if (noseStyle == 1) {
        p.drawEllipse(QPointF(cx, 84), 2.6, 2.2);
    } else if (noseStyle == 2) {
        p.setPen(QPen(skinShadow, 2.4, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p.setBrush(Qt::NoBrush);
        QPainterPath nz;
        nz.moveTo(cx, 78);
        nz.lineTo(cx, 85);
        nz.lineTo(cx + 3, 84);
        p.drawPath(nz);
        p.setPen(Qt::NoPen);
    } else {
        p.drawEllipse(QPointF(cx + 1, 84), 2.0, 1.6);
    }

    // --- Cheeks: optional blush and/or freckles. ---
    if (chance(28)) {
        QColor blush("#ff7a90");
        blush.setAlpha(95);
        p.setPen(Qt::NoPen);
        p.setBrush(blush);
        p.drawEllipse(QPointF(cx - 20, 86), 6, 4);
        p.drawEllipse(QPointF(cx + 20, 86), 6, 4);
    }
    if (chance(16)) {
        p.setPen(Qt::NoPen);
        p.setBrush(skinShadow);
        for (int s = -1; s <= 1; s += 2)
            for (int i = 0; i < 3; ++i)
                p.drawEllipse(QPointF(cx + s * (13 + i * 4), 82 + (i % 2) * 3),
                              1.3, 1.3);
    }

    // --- Facial hair (drawn under the mouth so the mouth still reads). ---
    if (chance(26)) {
        if (chance(60)) { // beard along the jaw
            p.save();
            QPainterPath clip;
            clip.addEllipse(faceRect);
            p.setClipPath(clip);
            p.setPen(Qt::NoPen);
            p.setBrush(hair);
            QPainterPath beard;
            beard.addRoundedRect(QRectF(faceRect.left(), 86, faceRect.width(),
                                        faceRect.bottom() - 86 + 4),
                                 14, 14);
            p.drawPath(beard);
            p.restore();
        }
        if (chance(70)) { // moustache
            p.setPen(Qt::NoPen);
            p.setBrush(hair);
            QPainterPath m;
            m.moveTo(cx, 89);
            m.cubicTo(cx - 6, 86, cx - 13, 87, cx - 15, 92);
            m.cubicTo(cx - 9, 90, cx - 4, 91, cx, 90);
            m.cubicTo(cx + 4, 91, cx + 9, 90, cx + 15, 92);
            m.cubicTo(cx + 13, 87, cx + 6, 86, cx, 89);
            p.drawPath(m);
        }
    }

    // --- Mouth. ---
    const QColor lip("#8a3324");
    const int mouthStyle = rnd(5);
    p.setPen(Qt::NoPen);
    if (mouthStyle == 0) { // smile
        p.setPen(QPen(lip, 3.2, Qt::SolidLine, Qt::RoundCap));
        p.setBrush(Qt::NoBrush);
        p.drawArc(QRectF(cx - 11, mouthY - 9, 22, 18), 200 * 16, 140 * 16);
        p.setPen(Qt::NoPen);
    } else if (mouthStyle == 1) { // open grin with a tooth strip
        p.setBrush(QColor("#5e241c"));
        QPainterPath m;
        m.moveTo(cx - 12, mouthY - 1);
        m.quadTo(cx, mouthY + 14, cx + 12, mouthY - 1);
        m.closeSubpath();
        p.drawPath(m);
        p.save();
        p.setClipPath(m);
        p.setBrush(Qt::white);
        p.drawRect(QRectF(cx - 13, mouthY - 3, 26, 4.5));
        p.restore();
    } else if (mouthStyle == 2) { // neutral
        p.setPen(QPen(lip, 3.0, Qt::SolidLine, Qt::RoundCap));
        p.setBrush(Qt::NoBrush);
        p.drawLine(QPointF(cx - 8, mouthY + 2), QPointF(cx + 8, mouthY + 2));
        p.setPen(Qt::NoPen);
    } else if (mouthStyle == 3) { // surprised
        p.setBrush(QColor("#6e2b22"));
        p.drawEllipse(QPointF(cx, mouthY + 2), 5.0, 6.2);
    } else { // smirk
        p.setPen(QPen(lip, 3.2, Qt::SolidLine, Qt::RoundCap));
        p.setBrush(Qt::NoBrush);
        p.drawArc(QRectF(cx - 9, mouthY - 6, 20, 16), 210 * 16, 95 * 16);
        p.setPen(Qt::NoPen);
    }

    p.end();

    QByteArray png;
    QBuffer buffer(&png);
    buffer.open(QIODevice::WriteOnly);
    img.save(&buffer, "PNG");
    return png;
}

// Deterministic procedural node avatar: compact machine/server marks rather
// than faces, so node identities read differently from user identities.
inline QByteArray forkMeshNodeAvatarPng(const QString &seed)
{
    const QByteArray h =
        QCryptographicHash::hash(seed.toUtf8(), QCryptographicHash::Sha256);
    quint64 state = 0x84222325CBF29CE4ULL;
    for (char c : h)
        state = (state ^ static_cast<quint8>(c)) * 0x100000001B3ULL;
    auto nextU64 = [&state]() {
        state += 0x9E3779B97F4A7C15ULL;
        quint64 z = state;
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
        return z ^ (z >> 31);
    };
    auto rnd = [&](int n) { return n > 0 ? int(nextU64() % quint64(n)) : 0; };
    auto chance = [&](int pct) { return int(nextU64() % 100) < pct; };

    static const char *kBackdrops[][2] = {
        {"#0f766e", "#042f2e"}, {"#2563eb", "#111827"},
        {"#7c3aed", "#1f1235"}, {"#dc2626", "#2b0b0b"},
        {"#0891b2", "#082f49"}, {"#65a30d", "#1a2e05"},
        {"#4f46e5", "#0f172a"}, {"#ca8a04", "#3b2600"}};
    static const char *kPanels[] = {
        "#dbeafe", "#ccfbf1", "#e0e7ff", "#fef3c7", "#e5e7eb", "#dcfce7"};
    static const char *kAccents[] = {
        "#22c55e", "#38bdf8", "#f97316", "#f43f5e", "#a78bfa", "#facc15"};

    const int S = 128;
    QImage img(S, S, QImage::Format_ARGB32_Premultiplied);
    img.fill(Qt::transparent);
    QPainter p(&img);
    p.setRenderHint(QPainter::Antialiasing);

    const int bg = rnd(int(sizeof(kBackdrops) / sizeof(kBackdrops[0])));
    QLinearGradient grad(0, 0, S, S);
    grad.setColorAt(0.0, QColor(kBackdrops[bg][0]));
    grad.setColorAt(1.0, QColor(kBackdrops[bg][1]));
    p.fillRect(QRectF(0, 0, S, S), QBrush(grad));

    QColor grid("#ffffff");
    grid.setAlpha(24);
    p.setPen(QPen(grid, 1));
    const int pitch = 16 + rnd(9);
    for (int x = -S; x < S * 2; x += pitch)
        p.drawLine(QPointF(x, 0), QPointF(x + S, S));
    for (int y = pitch / 2; y < S; y += pitch)
        p.drawLine(QPointF(0, y), QPointF(S, y));

    const QColor panel(kPanels[rnd(int(sizeof(kPanels) / sizeof(kPanels[0])))]);
    const QColor shade = panel.darker(122);
    const QColor accent(kAccents[rnd(int(sizeof(kAccents) / sizeof(kAccents[0])))]);
    const int form = rnd(4);
    QRectF body(31, 33, 66, 62);
    QPainterPath chassis;
    if (form == 0) {
        chassis.addRoundedRect(body, 10, 10);
    } else if (form == 1) {
        chassis.moveTo(39, 31);
        chassis.lineTo(89, 31);
        chassis.lineTo(99, 48);
        chassis.lineTo(92, 96);
        chassis.lineTo(36, 96);
        chassis.lineTo(29, 48);
        chassis.closeSubpath();
    } else if (form == 2) {
        chassis.addRoundedRect(QRectF(27, 39, 74, 50), 8, 8);
        chassis.addRoundedRect(QRectF(43, 26, 42, 20), 7, 7);
    } else {
        chassis.addRoundedRect(QRectF(35, 25, 58, 78), 14, 14);
    }
    p.setPen(QPen(QColor(0, 0, 0, 80), 3));
    p.setBrush(panel);
    p.drawPath(chassis);
    p.setPen(QPen(shade, 2));
    p.setBrush(Qt::NoBrush);
    p.drawPath(chassis);

    p.setPen(Qt::NoPen);
    p.setBrush(accent);
    if (chance(55)) {
        p.drawRoundedRect(QRectF(43, 48, 42, 12), 4, 4);
    } else {
        p.drawEllipse(QPointF(50, 55), 7, 7);
        p.drawEllipse(QPointF(78, 55), 7, 7);
    }
    QColor dark("#111827");
    dark.setAlpha(210);
    p.setBrush(dark);
    if (chance(70)) {
        for (int i = 0; i < 3; ++i)
            p.drawRoundedRect(QRectF(43 + i * 15, 72, 10, 4), 2, 2);
    } else {
        p.drawRoundedRect(QRectF(48, 72, 32, 5), 2, 2);
    }

    p.setPen(QPen(accent, 3, Qt::SolidLine, Qt::RoundCap));
    if (chance(65)) {
        p.drawLine(QPointF(64, 32), QPointF(64, 17));
        p.setBrush(accent);
        p.setPen(Qt::NoPen);
        p.drawEllipse(QPointF(64, 15), 4, 4);
        p.setPen(QPen(accent, 3, Qt::SolidLine, Qt::RoundCap));
    }
    if (chance(55)) {
        p.drawLine(QPointF(32, 64), QPointF(18, 58));
        p.drawLine(QPointF(96, 64), QPointF(110, 58));
        p.setPen(Qt::NoPen);
        p.setBrush(shade);
        p.drawEllipse(QPointF(17, 58), 5, 5);
        p.drawEllipse(QPointF(111, 58), 5, 5);
    }

    p.setPen(QPen(QColor(255, 255, 255, 115), 2));
    p.drawLine(QPointF(42, 38), QPointF(77, 38));
    if (chance(45)) {
        p.setPen(QPen(accent.lighter(130), 2));
        p.drawArc(QRectF(38, 83, 52, 30), 20 * 16, 140 * 16);
    }

    p.end();
    QByteArray png;
    QBuffer buffer(&png);
    buffer.open(QIODevice::WriteOnly);
    img.save(&buffer, "PNG");
    return png;
}

// Clip avatar PNG bytes into a rounded-rect pixmap for the nav button. The
// corner radius is a fraction of the side, so 0.5 gives a full circle (what the
// website shows for an account's picture).
inline QPixmap roundedAvatar(const QByteArray &png, int side,
                             qreal radiusRatio = 0.28)
{
    QPixmap src;
    if (png.isEmpty() || !src.loadFromData(png))
        return QPixmap();
    const qreal dpr = iconDevicePixelRatio();
    QPixmap out = crispIconPixmap(side, dpr);
    QPainter p(&out);
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    QPainterPath clip;
    const qreal radius = side * radiusRatio;
    clip.addRoundedRect(0, 0, side, side, radius, radius);
    p.setClipPath(clip);
    QPixmap scaled = src.scaled(out.width(), out.height(),
                                Qt::KeepAspectRatioByExpanding,
                                Qt::SmoothTransformation);
    scaled.setDevicePixelRatio(dpr);
    p.drawPixmap(0, 0, scaled);
    return out;
}

inline QPixmap nodeMachineFavicon(const QString &seed, int side = 36)
{
    QPixmap pm = roundedAvatar(forkMeshNodeAvatarPng(seed), side);
    if (!pm.isNull())
        return pm;
    return QPixmap();
}

// OS badge for a node row: a small Linux / Windows / macOS mark, tinted by the
// OS when the node is online and grey when offline so it still signals presence.
inline QIcon osBadgeIcon(const QString &platform, bool online, int size)
{
    const QString p = platform.toLower();
    const QColor grey("#6e7681");
    // Connected nodes are tinted green (a clear "online" signal); offline grey.
    const QColor online_green("#2ea043");
    auto col = [&](const QColor &) { return online ? online_green : grey; };

    QPixmap pm = crispIconPixmap(size, iconDevicePixelRatio());
    QPainter g(&pm);
    g.setRenderHint(QPainter::Antialiasing);
    g.setPen(Qt::NoPen);

    if (p.contains("win")) {
        // Four panes.
        g.setBrush(col(QColor("#3fa0ef")));
        const qreal m = size * 0.18, gap = size * 0.12;
        const qreal cell = (size - 2 * m - gap) / 2.0;
        g.drawRect(QRectF(m, m, cell, cell));
        g.drawRect(QRectF(m + cell + gap, m, cell, cell));
        g.drawRect(QRectF(m, m + cell + gap, cell, cell));
        g.drawRect(QRectF(m + cell + gap, m + cell + gap, cell, cell));
    } else if (p.contains("mac") || p.contains("ios") || p.contains("darwin") ||
               p.contains("os x")) {
        // Apple silhouette: a bitten body plus a leaf.
        g.setBrush(col(QColor("#c7ccd1")));
        QPainterPath body;
        body.addEllipse(QPointF(size * 0.46, size * 0.60), size * 0.30, size * 0.33);
        QPainterPath bite;
        bite.addEllipse(QPointF(size * 0.88, size * 0.52), size * 0.17, size * 0.20);
        g.drawPath(body.subtracted(bite));
        g.save();
        g.translate(size * 0.56, size * 0.22);
        g.rotate(-35);
        g.drawEllipse(QPointF(0, 0), size * 0.13, size * 0.07);
        g.restore();
    } else if (p.contains("linux") || p.contains("bsd") || p.contains("unix")) {
        // Minimal penguin: dark body, light belly, orange beak.
        g.setBrush(col(QColor("#2b2b2b")));
        g.drawEllipse(QPointF(size * 0.5, size * 0.54), size * 0.30, size * 0.40);
        g.setBrush(online ? QColor("#f5f5f5") : QColor("#cfcfcf"));
        g.drawEllipse(QPointF(size * 0.5, size * 0.62), size * 0.17, size * 0.26);
        g.setBrush(col(QColor("#f0a020")));
        QPainterPath beak;
        beak.moveTo(size * 0.5, size * 0.32);
        beak.lineTo(size * 0.40, size * 0.40);
        beak.lineTo(size * 0.60, size * 0.40);
        beak.closeSubpath();
        g.drawPath(beak);
    } else {
        // Unknown OS → a generic desktop monitor.
        g.setBrush(col(QColor("#8b949e")));
        const qreal m = size * 0.16;
        g.drawRoundedRect(QRectF(m, m, size - 2 * m, size * 0.5),
                          size * 0.06, size * 0.06);
        g.drawRect(QRectF(size * 0.44, m + size * 0.5, size * 0.12, size * 0.14));
        g.drawRoundedRect(QRectF(size * 0.30, size * 0.80, size * 0.40, size * 0.07),
                          size * 0.03, size * 0.03);
    }
    g.end();
    return QIcon(pm);
}

inline QString defaultDisplayName(const ForkMeshIdentity &identity)
{
    const QString suffix = accountNameFromInput(identity.publicKey(), QString()).left(8);
    return suffix.isEmpty() ? QStringLiteral("node")
                            : QStringLiteral("node") + suffix;
}

inline void saveProfileName(const QString &name)
{
    // Single source of truth for the node identity (was triplicated across
    // profile/handle, profile/displayName and account/nodeName).
    const QString trimmed = accountNameFromInput(name, QString());
    if (!trimmed.isEmpty())
        QSettings().setValue(kAccountNameSetting, trimmed);
}

inline QIcon statusDotIcon(bool online)
{
    QPixmap pixmap = crispIconPixmap(12, iconDevicePixelRatio());
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setBrush(online ? QColor("#2ea043") : QColor("#6e7681"));
    painter.setPen(Qt::NoPen);
    painter.drawEllipse(1, 1, 10, 10);
    return QIcon(pixmap);
}

enum class PreviewSyntax {
    Plain,
    Markdown,
    Json,
    Cpp,
    JavaScript,
    Python,
    Yaml,
    Diff
};

inline PreviewSyntax previewSyntaxForPath(const QString &path)
{
    const QString lower = path.toLower();
    if (lower.endsWith(".md") || lower.endsWith(".markdown"))
        return PreviewSyntax::Markdown;
    if (lower.endsWith(".json") || lower.endsWith(".jsonc"))
        return PreviewSyntax::Json;
    if (lower.endsWith(".yml") || lower.endsWith(".yaml") || lower.endsWith(".toml"))
        return PreviewSyntax::Yaml;
    if (lower.endsWith(".diff") || lower.endsWith(".patch"))
        return PreviewSyntax::Diff;
    if (lower.endsWith(".py"))
        return PreviewSyntax::Python;
    if (lower.endsWith(".js") || lower.endsWith(".jsx") || lower.endsWith(".ts") ||
        lower.endsWith(".tsx") || lower.endsWith(".mjs") || lower.endsWith(".cjs"))
        return PreviewSyntax::JavaScript;
    if (lower.endsWith(".cpp") || lower.endsWith(".cc") || lower.endsWith(".cxx") ||
        lower.endsWith(".c") || lower.endsWith(".h") || lower.endsWith(".hpp") ||
        lower.endsWith(".hh") || lower.endsWith(".rs") || lower.endsWith(".go") ||
        lower.endsWith(".java") || lower.endsWith(".swift"))
        return PreviewSyntax::Cpp;
    return PreviewSyntax::Plain;
}

inline QTextCharFormat previewFormat(const QColor &color, int weight = QFont::Normal,
                              bool italic = false)
{
    QTextCharFormat format;
    format.setForeground(color);
    format.setFontWeight(weight);
    format.setFontItalic(italic);
    return format;
}

struct HighlightRule {
    QRegularExpression pattern;
    QTextCharFormat format;
};

class CodePreviewHighlighter : public QSyntaxHighlighter
{
public:
    CodePreviewHighlighter(QTextDocument *document, const QString &path)
        : QSyntaxHighlighter(document), m_syntax(previewSyntaxForPath(path))
    {
        configureRules();
    }

protected:
    void highlightBlock(const QString &text) override
    {
        if (m_syntax == PreviewSyntax::Diff) {
            if (text.startsWith("+++ ") || text.startsWith("--- "))
                setFormat(0, text.length(), m_keywordFormat);
            else if (text.startsWith("+"))
                setFormat(0, text.length(), m_addedFormat);
            else if (text.startsWith("-"))
                setFormat(0, text.length(), m_removedFormat);
            else if (text.startsWith("@@"))
                setFormat(0, text.length(), m_headingFormat);
        }

        if (m_syntax == PreviewSyntax::Markdown) {
            if (text.startsWith("#")) {
                const auto match =
                    QRegularExpression(QStringLiteral("^#{1,6}\\s+.*$")).match(text);
                if (match.hasMatch())
                    setFormat(match.capturedStart(), match.capturedLength(),
                              m_headingFormat);
            }
            const auto quoteMatch =
                QRegularExpression(QStringLiteral("^\\s*>.*$")).match(text);
            if (quoteMatch.hasMatch())
                setFormat(quoteMatch.capturedStart(), quoteMatch.capturedLength(),
                          m_commentFormat);
        }

        for (const HighlightRule &rule : std::as_const(m_rules)) {
            auto matches = rule.pattern.globalMatch(text);
            while (matches.hasNext()) {
                const auto match = matches.next();
                setFormat(match.capturedStart(), match.capturedLength(), rule.format);
            }
        }
    }

private:
    void addKeywords(const QStringList &keywords, const QTextCharFormat &format)
    {
        m_rules.push_back({
            QRegularExpression(QStringLiteral("\\b(%1)\\b").arg(keywords.join('|'))),
            format});
    }

    void configureRules()
    {
        const bool dark = currentThemeIsDark();
        m_headingFormat =
            previewFormat(QColor(dark ? "#7ee787" : "#1a7f37"), QFont::Bold);
        m_keywordFormat =
            previewFormat(QColor(dark ? "#ff7b72" : "#cf222e"), QFont::Bold);
        m_stringFormat = previewFormat(QColor(dark ? "#a5d6ff" : "#0a3069"));
        m_numberFormat = previewFormat(QColor(dark ? "#79c0ff" : "#0550ae"));
        m_commentFormat =
            previewFormat(QColor(dark ? "#8b949e" : "#6e7781"), QFont::Normal, true);
        m_keyFormat =
            previewFormat(QColor(dark ? "#d2a8ff" : "#8250df"), QFont::Bold);
        m_addedFormat = previewFormat(QColor(dark ? "#7ee787" : "#1a7f37"));
        m_removedFormat = previewFormat(QColor(dark ? "#ffa198" : "#cf222e"));
        const QTextCharFormat punctuationFormat =
            previewFormat(QColor(dark ? "#8b949e" : "#6e7781"));

        if (m_syntax == PreviewSyntax::Markdown) {
            m_rules.push_back({QRegularExpression(QStringLiteral("`[^`]+`")),
                               m_stringFormat});
            m_rules.push_back({QRegularExpression(QStringLiteral("\\*\\*[^*]+\\*\\*")),
                               m_keywordFormat});
            m_rules.push_back({QRegularExpression(QStringLiteral("\\[[^\\]]+\\]\\([^\\)]+\\)")),
                               m_keyFormat});
            m_rules.push_back({QRegularExpression(QStringLiteral("^\\s*[-*+]\\s+")),
                               m_keywordFormat});
            m_rules.push_back({QRegularExpression(QStringLiteral("^\\s*```.*$")),
                               m_commentFormat});
            return;
        }

        if (m_syntax == PreviewSyntax::Json) {
            m_rules.push_back({QRegularExpression(QStringLiteral("\"([^\"\\\\]|\\\\.)*\"")),
                               m_stringFormat});
            m_rules.push_back({QRegularExpression(QStringLiteral("\"([^\"\\\\]|\\\\.)+\"(?=\\s*:)")),
                               m_keyFormat});
            m_rules.push_back({QRegularExpression(QStringLiteral("\\b-?(0|[1-9]\\d*)(\\.\\d+)?([eE][+-]?\\d+)?\\b")),
                               m_numberFormat});
            addKeywords({"true", "false", "null"}, m_keywordFormat);
            m_rules.push_back({QRegularExpression(QStringLiteral("[{}\\[\\],:]")),
                               punctuationFormat});
            return;
        }

        if (m_syntax == PreviewSyntax::Yaml) {
            m_rules.push_back({QRegularExpression(QStringLiteral("#[^\\n]*")),
                               m_commentFormat});
            m_rules.push_back({QRegularExpression(QStringLiteral("\"([^\"\\\\]|\\\\.)*\"|'[^']*'")),
                               m_stringFormat});
            m_rules.push_back({QRegularExpression(QStringLiteral("^\\s*-?\\s*[A-Za-z0-9_.-]+(?=:)")),
                               m_keyFormat});
            m_rules.push_back({QRegularExpression(QStringLiteral("\\b[0-9]+(\\.[0-9]+)?\\b")),
                               m_numberFormat});
            addKeywords({"true", "false", "yes", "no", "null"}, m_keywordFormat);
            return;
        }

        if (m_syntax == PreviewSyntax::Python) {
            addKeywords({"and", "as", "assert", "async", "await", "break", "class",
                         "continue", "def", "elif", "else", "except", "False", "finally",
                         "for", "from", "if", "import", "in", "is", "lambda", "None",
                         "not", "or", "pass", "raise", "return", "True", "try", "while",
                         "with", "yield"},
                        m_keywordFormat);
            m_rules.push_back({QRegularExpression(QStringLiteral("#[^\\n]*")),
                               m_commentFormat});
        } else if (m_syntax == PreviewSyntax::JavaScript) {
            addKeywords({"async", "await", "break", "case", "catch", "class", "const",
                         "continue", "default", "else", "export", "extends", "false",
                         "for", "from", "function", "if", "import", "let", "new", "null",
                         "return", "switch", "this", "throw", "true", "try", "typeof",
                         "undefined", "var", "while"},
                        m_keywordFormat);
            m_rules.push_back({QRegularExpression(QStringLiteral("//[^\\n]*")),
                               m_commentFormat});
        } else if (m_syntax == PreviewSyntax::Cpp) {
            addKeywords({"auto", "bool", "break", "case", "class", "const", "constexpr",
                         "continue", "else", "enum", "false", "for", "if", "namespace",
                         "nullptr", "private", "protected", "public", "return", "static",
                         "struct", "switch", "template", "true", "typename", "using",
                         "void", "while"},
                        m_keywordFormat);
            m_rules.push_back({QRegularExpression(QStringLiteral("^\\s*#[^\\n]*")),
                               m_commentFormat});
            m_rules.push_back({QRegularExpression(QStringLiteral("//[^\\n]*")),
                               m_commentFormat});
        }

        if (m_syntax == PreviewSyntax::Cpp || m_syntax == PreviewSyntax::JavaScript ||
            m_syntax == PreviewSyntax::Python) {
            m_rules.push_back({QRegularExpression(QStringLiteral("\"([^\"\\\\]|\\\\.)*\"|'([^'\\\\]|\\\\.)*'")),
                               m_stringFormat});
            m_rules.push_back({QRegularExpression(QStringLiteral("\\b[0-9]+(\\.[0-9]+)?\\b")),
                               m_numberFormat});
        }
    }

    PreviewSyntax m_syntax = PreviewSyntax::Plain;
    QVector<HighlightRule> m_rules;
    QTextCharFormat m_headingFormat;
    QTextCharFormat m_keywordFormat;
    QTextCharFormat m_stringFormat;
    QTextCharFormat m_numberFormat;
    QTextCharFormat m_commentFormat;
    QTextCharFormat m_keyFormat;
    QTextCharFormat m_addedFormat;
    QTextCharFormat m_removedFormat;
};

// Apply a true fixed-width font to a log/terminal view and, crucially, register
// a colour-emoji fallback family. On Linux a bare QFont("monospace") both fails
// to guarantee a real monospace face (causing the ASCII-table misalignment seen
// in tool output) and disables the colour-emoji fallback, so emoji render as
// flat black-and-white glyphs. Building the family list explicitly fixes both.
inline void applyLogFont(QPlainTextEdit *view)
{
    if (!view)
        return;
    QFont mono = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    QStringList families;
    families << mono.family();
    // Common Linux fixed faces, then the colour-emoji font so 🎉/✅/🌐 paint in
    // colour while text stays monospaced.
    for (const QString &fallback :
         {QStringLiteral("DejaVu Sans Mono"), QStringLiteral("Noto Sans Mono"),
          QStringLiteral("Noto Color Emoji"), QStringLiteral("Apple Color Emoji"),
          QStringLiteral("Segoe UI Emoji")}) {
        if (!families.contains(fallback))
            families << fallback;
    }
    mono.setFamilies(families);
    mono.setStyleHint(QFont::Monospace);
    mono.setFixedPitch(true);
    view->setFont(mono);
    // Consistent tab stops so any tab-aligned tool output lines up.
    view->setTabStopDistance(4 * QFontMetricsF(mono).horizontalAdvance(QLatin1Char(' ')));
}

inline QString displaySafePlainLog(QString text)
{
    constexpr qsizetype kMaxChars = 2 * 1024 * 1024;
    constexpr qsizetype kMaxLineChars = 4096;

    QString prefix;
    if (text.size() > kMaxChars) {
        text = text.right(kMaxChars);
        prefix = QStringLiteral("...[log truncated for display; showing tail]...\n");
    }

    QString out;
    out.reserve(prefix.size() + text.size());
    qsizetype col = 0;
    for (QChar ch : text) {
        const ushort u = ch.unicode();
        if (ch == QLatin1Char('\r') || ch == QLatin1Char('\n')) {
            out += QLatin1Char('\n');
            col = 0;
            continue;
        }
        if (ch == QLatin1Char('\t')) {
            out += ch;
            col += 4;
        } else if (u < 0x20 || (u >= 0x7f && u <= 0x9f)) {
            continue;
        } else {
            out += ch;
            ++col;
        }
        if (col >= kMaxLineChars) {
            out += QStringLiteral("\n...[long line wrapped for display]...\n");
            col = 0;
        }
    }
    return prefix + out;
}

// Colourises agent / workflow logs so streamed Claude & Codex output reads like
// a modern editor terminal: system markers, shell commands, tool results,
// network traffic, and errors each get a distinct style. Works incrementally as
// lines stream in (one QTextBlock at a time), so it's safe on a live log.
class AgentLogHighlighter : public QSyntaxHighlighter
{
public:
    explicit AgentLogHighlighter(QTextDocument *document)
        : QSyntaxHighlighter(document)
    {
        const bool dark = currentThemeIsDark();
        auto fmt = [](const QColor &c, bool bold = false, bool italic = false) {
            QTextCharFormat f;
            f.setForeground(c);
            if (bold)
                f.setFontWeight(QFont::Bold);
            f.setFontItalic(italic);
            return f;
        };
        m_net = fmt(QColor(dark ? "#d2a8ff" : "#8250df"), true);     // network traffic
        m_system = fmt(QColor(dark ? "#58a6ff" : "#0969da"), true);  // ==> markers
        m_success = fmt(QColor(dark ? "#3fb950" : "#1a7f37"), true); // success
        m_error = fmt(QColor(dark ? "#ff7b72" : "#cf222e"), true);   // !! errors
        m_command = fmt(QColor(dark ? "#79c0ff" : "#0550ae"), true); // $ shell command
        m_muted = fmt(QColor(dark ? "#8b949e" : "#6e7781"), false, true); // tool output
        m_tool = fmt(QColor(dark ? "#e3b341" : "#9a6700"), true);    // tool-use headers
    }

protected:
    void highlightBlock(const QString &text) override
    {
        if (text.size() > 8192)
            return;
        const QString trimmed = text.trimmed();
        const int len = text.length();
        if (trimmed.startsWith(QLatin1String("==> [net]")) ||
            trimmed.startsWith(QLatin1String("[net]"))) {
            setFormat(0, len, m_net);
        } else if (trimmed.startsWith(QLatin1String("==>"))) {
            // A "==>" marker may carry a leading emoji (✅/❌/🔧/…); key the
            // colour off the words rather than an exact prefix so the emoji
            // decorations in the workflow log still colourise.
            if (trimmed.contains(QLatin1String("SUCCESS")) ||
                trimmed.contains(QLatin1String("Agent finished")) ||
                trimmed.contains(QLatin1String("Created pull request")))
                setFormat(0, len, m_success);
            else if (trimmed.contains(QLatin1String("FAILED")))
                setFormat(0, len, m_error);
            else
                setFormat(0, len, m_system);
        } else if (trimmed.startsWith(QLatin1String("!!")) ||
                   trimmed.contains(QLatin1String("Traceback"))) {
            setFormat(0, len, m_error);
        } else if (trimmed.startsWith(QLatin1String("$ "))) {
            setFormat(0, len, m_command);
        } else if (trimmed.startsWith(QLatin1String("(exit code")) ||
                   trimmed.startsWith(QLatin1String("...[output"))) {
            setFormat(0, len, m_muted);
        } else if (trimmed.startsWith(QString::fromUtf8("\xE2\x97\x8F ")) ||  // ●
                   trimmed.startsWith(QString::fromUtf8("\xE2\x8F\xBA"))) {   // ⏺
            setFormat(0, len, m_tool);
        }
    }

private:
    QTextCharFormat m_net, m_system, m_success, m_error, m_command, m_muted, m_tool;
};

// One conflict region in a file carrying git merge markers. Line indices are
// 0-based into the file's lines: [start..sep) is "ours" (HEAD/base), the marker
// lines are start, sep and end, and (sep..end) is "theirs" (the PR).
struct ConflictRegion {
    int startLine = -1; // the "<<<<<<<" line
    int sepLine = -1;   // the "=======" line
    int endLine = -1;   // the ">>>>>>>" line
};

inline QList<ConflictRegion> findConflicts(const QStringList &lines)
{
    QList<ConflictRegion> regions;
    ConflictRegion cur;
    for (int i = 0; i < lines.size(); ++i) {
        const QString &l = lines.at(i);
        if (l.startsWith(QLatin1String("<<<<<<< "))) {
            cur = ConflictRegion();
            cur.startLine = i;
        } else if (l.startsWith(QLatin1String("=======")) && l.trimmed().length() == 7 &&
                   cur.startLine >= 0 && cur.sepLine < 0) {
            cur.sepLine = i;
        } else if (l.startsWith(QLatin1String(">>>>>>> ")) && cur.sepLine >= 0) {
            cur.endLine = i;
            regions.append(cur);
            cur = ConflictRegion();
        }
    }
    return regions;
}

// Tints git conflict markers and the two sides so the merge editor reads clearly.
class ConflictHighlighter : public QSyntaxHighlighter
{
public:
    explicit ConflictHighlighter(QTextDocument *document)
        : QSyntaxHighlighter(document)
    {
        const bool dark = currentThemeIsDark();
        auto bg = [](const QColor &c) {
            QTextCharFormat f;
            f.setBackground(c);
            return f;
        };
        m_marker.setForeground(QColor(dark ? "#8b949e" : "#6e7781"));
        m_marker.setFontWeight(QFont::Bold);
        m_ours = bg(QColor(dark ? "#0b2a4a" : "#ddf4ff"));    // HEAD / base side
        m_theirs = bg(QColor(dark ? "#0b3a1e" : "#e6ffec"));  // PR side
    }

protected:
    void highlightBlock(const QString &text) override
    {
        // Track which side each block sits in across the document (block states:
        // 0 outside, 1 ours, 2 theirs).
        int prev = previousBlockState();
        int state = prev == 1 || prev == 2 ? prev : 0;
        if (text.startsWith(QLatin1String("<<<<<<< "))) {
            setFormat(0, text.length(), m_marker);
            state = 1;
        } else if (text.startsWith(QLatin1String("=======")) &&
                   text.trimmed().length() == 7 && state == 1) {
            setFormat(0, text.length(), m_marker);
            state = 2;
        } else if (text.startsWith(QLatin1String(">>>>>>> ")) && state == 2) {
            setFormat(0, text.length(), m_marker);
            state = 0;
        } else if (state == 1) {
            setFormat(0, text.length(), m_ours);
        } else if (state == 2) {
            setFormat(0, text.length(), m_theirs);
        }
        setCurrentBlockState(state);
    }

private:
    QTextCharFormat m_marker, m_ours, m_theirs;
};

// A small self-animating "busy" spinner: the rotating refresh glyph used on the
// Refresh buttons, sized to sit inline next to a section heading while that
// section's content is being (re)loaded. The animation timer only runs while the
// spinner is visible (see show/hideEvent) so a hidden, idle one costs nothing.
class BusySpinner : public QWidget
{
public:
    explicit BusySpinner(QWidget *parent = nullptr, int size = 16)
        : QWidget(parent), m_size(size)
    {
        setFixedSize(size, size);
        setAttribute(Qt::WA_TransparentForMouseEvents);
        m_timer = new QTimer(this);
        m_timer->setInterval(60);
        connect(m_timer, &QTimer::timeout, this, [this] {
            m_angle = (m_angle + 30) % 360;
            update();
        });
    }

protected:
    void showEvent(QShowEvent *e) override
    {
        m_timer->start();
        QWidget::showEvent(e);
    }
    void hideEvent(QHideEvent *e) override
    {
        m_timer->stop();
        QWidget::hideEvent(e);
    }
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        p.drawPixmap(0, 0,
                     refreshPixmap(QColor(Theme::kRunning), m_angle, m_size));
    }

private:
    QTimer *m_timer = nullptr;
    int m_size;
    int m_angle = 0;
};

// A one-shot "done" mark: a ring draws itself in, a check strokes through it, and
// the ring then pulses a couple of times before the animation stops for good.
// Left in the Branches table where a branch used to be once "Merge & delete all"
// removed it, so the row reads as "merged, gone" instead of the list sliding the
// next branch under the cursor (adhoc #15). Self-animating like the spinners
// above: the timer only runs while the mark is visible and never restarts once
// the pulses are done, so a finished mark costs nothing.
class DoneCheckMark : public QWidget
{
public:
    explicit DoneCheckMark(QWidget *parent = nullptr, int size = 18,
                           const QColor &color = QColor("#3fb950"))
        : QWidget(parent), m_size(size), m_color(color)
    {
        setFixedSize(size, size);
        setAttribute(Qt::WA_TransparentForMouseEvents);
        m_timer = new QTimer(this);
        m_timer->setInterval(kTickMs);
        connect(m_timer, &QTimer::timeout, this, [this] {
            m_elapsedMs += kTickMs;
            if (m_elapsedMs >= kDrawMs + kPulseMs * kPulses)
                m_timer->stop();
            update();
        });
    }

protected:
    void showEvent(QShowEvent *e) override
    {
        if (m_elapsedMs < kDrawMs + kPulseMs * kPulses)
            m_timer->start();
        QWidget::showEvent(e);
    }
    void hideEvent(QHideEvent *e) override
    {
        m_timer->stop();
        QWidget::hideEvent(e);
    }
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        // 0 -> 1 over the draw-in, then pinned at 1 for the pulses.
        const double t = qBound(0.0, double(m_elapsedMs) / kDrawMs, 1.0);
        // 0 -> 1 -> 0 once per pulse period, and 0 while the mark is still drawing.
        double pulse = 0.0;
        if (m_elapsedMs > kDrawMs) {
            const double phase =
                double((m_elapsedMs - kDrawMs) % kPulseMs) / double(kPulseMs);
            pulse = std::sin(phase * M_PI);
        }

        const double penWidth = 1.6;
        const double inset = penWidth / 2.0 + 1.0;
        const QRectF box(inset, inset, m_size - 2 * inset, m_size - 2 * inset);
        QColor ringColor = m_color;
        ringColor.setAlpha(int(110 + 145 * pulse));
        QPen ring(ringColor);
        ring.setWidthF(penWidth);
        p.setPen(ring);
        p.setBrush(Qt::NoBrush);
        // Qt arc angles are 1/16° counter-clockwise from 3 o'clock; start at the
        // top and sweep clockwise so the ring closes as the check is drawn.
        p.drawArc(box, 90 * 16, -int(t * 360) * 16);

        // The check itself: two segments stroked in as one continuous line, so at
        // t=0.5 the pen sits at the mark's elbow.
        const QPointF a(m_size * 0.28, m_size * 0.52);
        const QPointF b(m_size * 0.43, m_size * 0.68);
        const QPointF c(m_size * 0.73, m_size * 0.34);
        const double len1 = QLineF(a, b).length();
        const double len2 = QLineF(b, c).length();
        const double drawn = t * (len1 + len2);
        QPen stroke(m_color);
        stroke.setWidthF(2.0);
        stroke.setCapStyle(Qt::RoundCap);
        stroke.setJoinStyle(Qt::RoundJoin);
        p.setPen(stroke);
        if (drawn <= len1) {
            p.drawLine(QLineF(a, a + (b - a) * (len1 > 0 ? drawn / len1 : 1.0)));
        } else {
            p.drawLine(QLineF(a, b));
            const double rest = qMin(drawn - len1, len2);
            p.drawLine(QLineF(b, b + (c - b) * (len2 > 0 ? rest / len2 : 1.0)));
        }
    }

private:
    static constexpr int kTickMs = 30;
    static constexpr int kDrawMs = 420;  // ring + check stroke in
    static constexpr int kPulseMs = 900; // one breath of the ring
    static constexpr int kPulses = 2;
    QTimer *m_timer = nullptr;
    int m_size;
    QColor m_color;
    int m_elapsedMs = 0;
};

// A thin rotating "processing ring" meant to encircle a small widget it's overlaid
// on. Used to ring the mic button while a just-recorded clip is still being
// transcribed after the button was released (adhoc #18), so the wait reads as
// "still working", not "nothing happened". A faint full track shows the circle and a
// brighter arc sweeps around it. Self-animating: the timer only runs while the
// spinner is visible (see show/hideEvent), so a hidden one is free. Drawn with a
// translucent background and transparent to mouse events so the widget beneath stays
// visible and clickable.
class RingSpinner : public QWidget
{
public:
    explicit RingSpinner(QWidget *parent = nullptr,
                         const QColor &color = QColor(Theme::kRunning))
        : QWidget(parent), m_color(color)
    {
        setAttribute(Qt::WA_TranslucentBackground);
        setAttribute(Qt::WA_TransparentForMouseEvents);
        m_timer = new QTimer(this);
        m_timer->setInterval(40);
        connect(m_timer, &QTimer::timeout, this, [this] {
            m_angle = (m_angle + 8) % 360;
            update();
        });
    }

protected:
    void showEvent(QShowEvent *e) override
    {
        m_timer->start();
        QWidget::showEvent(e);
    }
    void hideEvent(QHideEvent *e) override
    {
        m_timer->stop();
        QWidget::hideEvent(e);
    }
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        const double pen = 2.0;
        const double inset = pen / 2.0 + 1.0;
        const QRectF box(inset, inset, width() - 2 * inset, height() - 2 * inset);
        // Faint full track so the ring always reads as a complete circle...
        QPen track(QColor(m_color.red(), m_color.green(), m_color.blue(), 60));
        track.setWidthF(pen);
        p.setPen(track);
        p.setBrush(Qt::NoBrush);
        p.drawEllipse(box);
        // ...with a brighter arc sweeping around it. Qt arc angles are in 1/16°
        // counter-clockwise, so negating m_angle makes the sweep run clockwise.
        QPen arc(m_color);
        arc.setWidthF(pen);
        arc.setCapStyle(Qt::RoundCap);
        p.setPen(arc);
        p.drawArc(box, -m_angle * 16, 100 * 16);
    }

private:
    QTimer *m_timer = nullptr;
    QColor m_color;
    int m_angle = 0;
};

// Compact "issue looper" toggle placed inline in the Issues heading row, next
// to "New issue" (adhoc #130, moved from floating over the tab in #354). It is
// both the control and the indicator: a small on/off switch and the open issue
// currently being worked ("#124") — clicking that "#N" jumps to its agent
// (adhoc #134), while clicking elsewhere toggles the loop. While on, a single
// neon-green segment travels slowly around the rounded-rect border. Replaces
// the old in-page "working the backlog" banner and the tiny Issues-tab braille
// snake.
class LooperToggle : public QWidget
{
public:
    explicit LooperToggle(QWidget *parent = nullptr) : QWidget(parent)
    {
        setCursor(Qt::PointingHandCursor);
        setAttribute(Qt::WA_TranslucentBackground);
        setToolTip(QString::fromUtf8(
            "Issue looper \xE2\x80\x94 run the default agent on every open issue in "
            "turn. Click to start; click again to stop."));
        m_loopTimer = new QTimer(this);
        m_loopTimer->setInterval(40); // smooth travel; lap speed set by the step
        connect(m_loopTimer, &QTimer::timeout, this, [this] {
            m_loopPos += 0.0035; // ~one slow lap every ~11s
            if (m_loopPos >= 1.0)
                m_loopPos -= 1.0;
            update();
        });
    }

    void setActive(bool on)
    {
        if (m_active == on)
            return;
        m_active = on;
        if (m_active)
            m_loopTimer->start();
        else
            m_loopTimer->stop();
        update();
    }
    bool isActive() const { return m_active; }

    // The open issue the looper is currently working (0 = none yet). Shown as
    // "#N" after the label so the toggle doubles as a "what's it on" readout.
    void setIssueNumber(int n)
    {
        if (m_issue == n)
            return;
        m_issue = n;
        updateGeometry(); // width depends on the "#N" suffix
        update();
    }
    void setOnClick(std::function<void()> cb) { m_onClick = std::move(cb); }
    // Invoked when the "#N" itself is clicked (jump to that issue's agent).
    void setOnNumberClick(std::function<void()> cb)
    {
        m_onNumberClick = std::move(cb);
    }

    QSize sizeHint() const override
    {
        // Measure with a bold font: the label is drawn bold while active (the
        // wider state), so sizing for it keeps the width stable across toggles
        // and never clips the "#N" suffix.
        QFont bold = font();
        bold.setBold(true);
        const int textW = QFontMetrics(bold).horizontalAdvance(labelText());
        return QSize(kPadX + int(kSwitchW) + kGap + textW + kPadX, 26);
    }
    QSize minimumSizeHint() const override { return sizeHint(); }

protected:
    void mousePressEvent(QMouseEvent *e) override
    {
        if (e->button() == Qt::LeftButton) {
            // Clicking the "#N" jumps to that agent; clicking elsewhere toggles.
            if (m_onNumberClick && m_issue > 0
                && m_labelRect.contains(e->position())) {
                m_onNumberClick();
                return;
            }
            if (m_onClick) {
                m_onClick();
                return;
            }
        }
        QWidget::mousePressEvent(e);
    }
    void enterEvent(QEnterEvent *) override
    {
        m_hover = true;
        update();
    }
    void leaveEvent(QEvent *) override
    {
        m_hover = false;
        update();
    }
    void paintEvent(QPaintEvent *) override
    {
        QPainter g(this);
        g.setRenderHint(QPainter::Antialiasing);
        const QColor neon(57, 255, 110); // neon green

        // Pill background. Inset enough that the travelling glow (a ~5px stroke
        // centred on the border) stays inside the widget rather than clipping.
        const QRectF box = QRectF(rect()).adjusted(3.0, 3.0, -3.0, -3.0);
        const qreal radius = box.height() / 2.0;
        QPainterPath pill;
        pill.addRoundedRect(box, radius, radius);
        g.fillPath(pill, QColor(m_hover ? "#21262d" : "#161b22"));
        // Static dim outline always; the bright travelling segment rides on top.
        QPen base(QColor(m_active ? "#1f3d29" : "#30363d"));
        base.setWidthF(1.4);
        g.strokePath(pill, base);
        if (m_active)
            drawTravellingLoop(g, pill, neon);

        // Toggle switch.
        const qreal sx = box.left() + kPadX - 2.0;
        QRectF track(sx, box.center().y() - kSwitchH / 2.0, kSwitchW, kSwitchH);
        g.setPen(Qt::NoPen);
        g.setBrush(m_active ? neon : QColor("#484f58"));
        g.drawRoundedRect(track, kSwitchH / 2.0, kSwitchH / 2.0);
        const qreal knobR = kSwitchH / 2.0 - 2.0;
        const qreal knobCx = m_active ? track.right() - knobR - 2.0
                                      : track.left() + knobR + 2.0;
        g.setBrush(QColor("#f0f6fc"));
        g.drawEllipse(QPointF(knobCx, track.center().y()), knobR, knobR);

        // Label: the "#N" of the issue being worked. Record its hit rect so a
        // click on the number jumps to that agent (mousePressEvent), while a
        // click anywhere else on the pill toggles the loop.
        QFont f = font();
        f.setBold(m_active);
        g.setFont(f);
        g.setPen(m_active ? neon : QColor("#8b949e"));
        const qreal tx = sx + kSwitchW + kGap;
        const QString label = labelText();
        g.drawText(QRectF(tx, box.top(), box.right() - tx - 4, box.height()),
                   Qt::AlignVCenter | Qt::AlignLeft, label);
        m_labelRect = label.isEmpty()
                          ? QRectF()
                          : QRectF(tx, box.top(),
                                   QFontMetricsF(f).horizontalAdvance(label),
                                   box.height());
    }

private:
    QString labelText() const
    {
        // The open issue currently being worked, e.g. "#124". No word "looper" —
        // the switch and travelling loop already say what this control is.
        return m_issue > 0 ? QStringLiteral("#%1").arg(m_issue) : QString();
    }
    // A single bright neon segment travelling around the pill's border, with a
    // soft wider pass underneath for the "glow tube" look.
    void drawTravellingLoop(QPainter &g, const QPainterPath &pill,
                            const QColor &neon) const
    {
        const qreal seg = 0.30; // fraction of the loop lit at once
        QPainterPath lit;
        const int steps = 40;
        for (int i = 0; i <= steps; ++i) {
            qreal t = m_loopPos + seg * i / steps;
            if (t >= 1.0)
                t -= 1.0; // wrap into [0,1); m_loopPos<1 and seg<1, so one wrap
            const QPointF p = pill.pointAtPercent(t);
            if (i == 0)
                lit.moveTo(p);
            else
                lit.lineTo(p);
        }
        QPen glow(QColor(neon.red(), neon.green(), neon.blue(), 70));
        glow.setWidthF(5.0);
        glow.setCapStyle(Qt::RoundCap);
        g.strokePath(lit, glow);
        QPen core(neon);
        core.setWidthF(2.0);
        core.setCapStyle(Qt::RoundCap);
        g.strokePath(lit, core);
    }

    static constexpr int kPadX = 11;
    static constexpr int kGap = 8;
    static constexpr qreal kSwitchW = 26.0;
    static constexpr qreal kSwitchH = 15.0;

    bool m_active = false;
    bool m_hover = false;
    int m_issue = 0;
    qreal m_loopPos = 0.0;
    QTimer *m_loopTimer = nullptr;
    QRectF m_labelRect; // hit rect of the "#N" text, set in paintEvent
    std::function<void()> m_onClick;
    std::function<void()> m_onNumberClick;
};

class CodePreviewEditor;

class CodeLineNumberArea : public QWidget
{
public:
    explicit CodeLineNumberArea(CodePreviewEditor *editor);

    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    CodePreviewEditor *m_editor = nullptr;
};

class CodePreviewEditor : public QPlainTextEdit
{
public:
    explicit CodePreviewEditor(const QString &path, QWidget *parent = nullptr)
        : QPlainTextEdit(parent), m_lineNumberArea(new CodeLineNumberArea(this))
    {
        setObjectName("codeEditor");
        setProperty("previewPath", path);
        setReadOnly(true);
        setLineWrapMode(QPlainTextEdit::NoWrap);
        setFrameShape(QFrame::NoFrame);

        QFont mono = font();
        mono.setFamily(QStringLiteral("Menlo"));
        mono.setStyleHint(QFont::Monospace);
        mono.setPointSize(12);
        setFont(mono);
        setTabStopDistance(fontMetrics().horizontalAdvance(QLatin1Char(' ')) * 4);

        connect(this, &QPlainTextEdit::blockCountChanged, this,
                [this] { updateLineNumberAreaWidth(); });
        connect(this, &QPlainTextEdit::updateRequest, this,
                [this](const QRect &rect, int dy) { updateLineNumberArea(rect, dy); });
        connect(this, &QPlainTextEdit::cursorPositionChanged, this,
                [this] { highlightCurrentLine(); });

        updateLineNumberAreaWidth();
        highlightCurrentLine();
    }

    int lineNumberAreaWidth() const
    {
        int digits = 1;
        int max = qMax(1, blockCount());
        while (max >= 10) {
            max /= 10;
            ++digits;
        }
        return qMax(42, 14 + fontMetrics().horizontalAdvance(QLatin1Char('9')) * digits);
    }

    // Markdown files can be flipped between source and a rendered preview from a
    // tiny toolbar toggle (see MainWindow::toggleRepoFileMarkdownPreview). The
    // rendered view is an overlay child so this editor — and all the
    // save / commit / history wiring keyed off the tab widget — stays put.
    bool markdownPreviewVisible() const
    {
        return m_markdownPreview && m_markdownPreview->isVisible();
    }

    void setMarkdownPreviewVisible(bool on)
    {
        if (!on) {
            if (m_markdownPreview)
                m_markdownPreview->hide();
            return;
        }
        if (!m_markdownPreview) {
            m_markdownPreview = new QTextBrowser(this);
            m_markdownPreview->setObjectName("markdownPreview");
            m_markdownPreview->setOpenExternalLinks(true);
            m_markdownPreview->setFrameShape(QFrame::NoFrame);
        }
        m_markdownPreview->setMarkdown(toPlainText());
        m_markdownPreview->setGeometry(contentsRect());
        m_markdownPreview->show();
        m_markdownPreview->raise();
    }

    void lineNumberAreaPaintEvent(QPaintEvent *event)
    {
        const bool dark = currentThemeIsDark();
        QPainter painter(m_lineNumberArea);
        painter.fillRect(event->rect(), QColor(dark ? "#0d1117" : "#f6f8fa"));
        painter.setPen(QColor(dark ? "#6e7681" : "#8c959f"));

        QTextBlock block = firstVisibleBlock();
        int blockNumber = block.blockNumber();
        int top = qRound(blockBoundingGeometry(block).translated(contentOffset()).top());
        int bottom = top + qRound(blockBoundingRect(block).height());
        const int rightPadding = 10;

        while (block.isValid() && top <= event->rect().bottom()) {
            if (block.isVisible() && bottom >= event->rect().top()) {
                const QString number = QString::number(blockNumber + 1);
                painter.drawText(0, top, m_lineNumberArea->width() - rightPadding,
                                 fontMetrics().height(), Qt::AlignRight, number);
            }
            block = block.next();
            top = bottom;
            bottom = top + qRound(blockBoundingRect(block).height());
            ++blockNumber;
        }
    }

protected:
    void resizeEvent(QResizeEvent *event) override
    {
        QPlainTextEdit::resizeEvent(event);
        const QRect cr = contentsRect();
        m_lineNumberArea->setGeometry(
            QRect(cr.left(), cr.top(), lineNumberAreaWidth(), cr.height()));
        if (m_markdownPreview && m_markdownPreview->isVisible())
            m_markdownPreview->setGeometry(cr);
    }

    void changeEvent(QEvent *event) override
    {
        QPlainTextEdit::changeEvent(event);
        if (event->type() == QEvent::PaletteChange ||
            event->type() == QEvent::ApplicationPaletteChange ||
            event->type() == QEvent::StyleChange) {
            highlightCurrentLine();
            m_lineNumberArea->update();
        }
    }

private:
    void updateLineNumberAreaWidth()
    {
        setViewportMargins(lineNumberAreaWidth(), 0, 0, 0);
    }

    void updateLineNumberArea(const QRect &rect, int dy)
    {
        if (dy)
            m_lineNumberArea->scroll(0, dy);
        else
            m_lineNumberArea->update(0, rect.y(), m_lineNumberArea->width(),
                                     rect.height());
        if (rect.contains(viewport()->rect()))
            updateLineNumberAreaWidth();
    }

    void highlightCurrentLine()
    {
        QList<QTextEdit::ExtraSelection> selections;
        QTextEdit::ExtraSelection selection;
        selection.format.setBackground(
            QColor(currentThemeIsDark() ? "#161b22" : "#f6f8fa"));
        selection.format.setProperty(QTextFormat::FullWidthSelection, true);
        selection.cursor = textCursor();
        selection.cursor.clearSelection();
        selections.append(selection);
        setExtraSelections(selections);
    }

    CodeLineNumberArea *m_lineNumberArea = nullptr;
    QTextBrowser *m_markdownPreview = nullptr; // lazy rendered-markdown overlay
};

inline CodeLineNumberArea::CodeLineNumberArea(CodePreviewEditor *editor)
    : QWidget(editor), m_editor(editor)
{
    setObjectName("codeLineNumberArea");
}

inline QSize CodeLineNumberArea::sizeHint() const
{
    return QSize(m_editor ? m_editor->lineNumberAreaWidth() : 0, 0);
}

inline void CodeLineNumberArea::paintEvent(QPaintEvent *event)
{
    if (m_editor)
        m_editor->lineNumberAreaPaintEvent(event);
}


inline QPixmap tintedOcticonPixmap(const QString &name, const QColor &color, int size)
{
    // Rendering an SVG (parse the resource + raster + tint) is expensive, and the
    // same handful of icons are requested over and over while building lists â a
    // 300-row commit table alone asks for the "trash" glyph 900 times. Cache the
    // finished pixmaps keyed on the inputs so each (name,color,size) renders once.
    // UI-thread only, so a plain static map needs no locking.
    static QHash<QString, QPixmap> cache;
    const qreal dpr = iconDevicePixelRatio();
    const QString key = name + QLatin1Char('|') +
                        QString::number(color.rgba(), 16) + QLatin1Char('|') +
                        QString::number(size) + QLatin1Char('|') +
                        QString::number(dpr);
    const auto cached = cache.constFind(key);
    if (cached != cache.constEnd())
        return cached.value();

    QPixmap pixmap = crispIconPixmap(size, dpr);

    // A few glyphs have no octicon (the git worktree symbol, for one); those come
    // from VS Code's codicons under /icons/codicons. Both sets are 16x16 line art
    // of the same weight, so they mix cleanly. The existence check comes first
    // because handing QSvgRenderer a missing resource logs a qt.svg warning; only
    // a cache miss pays for it at all.
    QString path = QStringLiteral(":/icons/octicons/%1.svg").arg(name);
    if (!QFile::exists(path))
        path = QStringLiteral(":/icons/codicons/%1.svg").arg(name);

    QSvgRenderer renderer(path);
    if (!renderer.isValid())
        return pixmap;

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    renderer.render(&painter, QRectF(0, 0, size, size));
    painter.setCompositionMode(QPainter::CompositionMode_SourceIn);
    painter.fillRect(QRect(0, 0, size, size), color);
    painter.end();
    cache.insert(key, pixmap);
    return pixmap;
}

inline QIcon themedOcticon(const QString &name, const QColor &color, int size)
{
    QIcon icon;
    icon.addPixmap(tintedOcticonPixmap(name, color, size), QIcon::Normal, QIcon::Off);
    icon.addPixmap(tintedOcticonPixmap(name, color.darker(120), size),
                   QIcon::Active, QIcon::Off);
    icon.addPixmap(tintedOcticonPixmap(name, QColor("#6e7681"), size),
                   QIcon::Disabled, QIcon::Off);
    return icon;
}

// Tick rate for the running-agent spinners (the Agents table's "#" cells).
// Fast enough that a flat-out session reads as a smooth
// spin; how far each session turns per tick comes from its own tok/s (see
// agentSpinStepDegrees in MainWindowAgents.cpp).
inline constexpr int kAgentSpinTickMs = 60;

// A tinted octicon rotated `angleDeg` about its centre — used to spin the blue
// "running" glyph in the agents list (issue #108). Not cached, since the angle
// changes every animation frame; callers keep it to the handful of running rows.
inline QPixmap rotatedTintedOcticonPixmap(const QString &name, const QColor &color,
                                   int size, qreal angleDeg)
{
    const QPixmap base = tintedOcticonPixmap(name, color, size);
    QPixmap out = crispIconPixmap(size, iconDevicePixelRatio());
    QPainter painter(&out);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    painter.translate(size / 2.0, size / 2.0);
    painter.rotate(angleDeg);
    painter.translate(-size / 2.0, -size / 2.0);
    painter.drawPixmap(0, 0, base);
    return out;
}

// The status light drawn on top of each Mirror-nodes row (adhoc #230): a small
// lit lamp in the node's health colour. Steady when everything is green (and
// for the grey offline lamp); caution (out of sync) and error (failing the
// integrity pin) lamps spin a bright beacon beam instead, driven frame by frame
// by MainWindow::animateMirrorNodeLights. Not cached — the angle changes every
// frame, and only the handful of caution/error rows redraw.
inline QPixmap nodeStatusLightPixmap(const QColor &color, int size, qreal angleDeg,
                                     bool spinning)
{
    QPixmap out = crispIconPixmap(size, iconDevicePixelRatio());
    QPainter p(&out);
    p.setRenderHint(QPainter::Antialiasing, true);
    const QPointF c(size / 2.0, size / 2.0);
    const qreal r = size / 2.0 - 1.5;
    p.setPen(Qt::NoPen);
    // The lamp body, dimmed while spinning so the rotating beam reads against it.
    p.setBrush(spinning ? color.darker(160) : color);
    p.drawEllipse(c, r, r);
    if (spinning) {
        // Rotating beacon beam: a bright wedge fading behind its leading edge,
        // the same construction the retired relay radar's sweep used.
        QConicalGradient sweep(c, -angleDeg);
        QColor lead = color.lighter(130);
        QColor tail = color;
        tail.setAlpha(0);
        sweep.setColorAt(0.0, lead);
        sweep.setColorAt(0.45, tail);
        sweep.setColorAt(1.0, tail);
        p.setBrush(sweep);
        p.drawEllipse(c, r, r);
    } else {
        // A soft specular glint so the steady lamp reads as lit, not a flat dot.
        QColor glint = color.lighter(170);
        glint.setAlpha(200);
        p.setBrush(glint);
        p.drawEllipse(QPointF(c.x() - r * 0.30, c.y() - r * 0.30), r * 0.32,
                      r * 0.32);
    }
    return out;
}

inline void applyStoredOcticon(QPushButton *button)
{
    if (!button)
        return;
    const QString name = button->property("forkmeshOcticon").toString();
    if (name.isEmpty())
        return;
    const int size = button->property("forkmeshOcticonSize").toInt();
    const qreal rotation = button->property("forkmeshOcticonRotation").toReal();
    const QColor color(
        Theme::iconColorForButton(button->objectName(), currentThemeIsDark()));
    const int px = size > 0 ? size : 16;
    if (rotation != 0.0) {
        // A statically-rotated glyph (e.g. the footer's up-pointing send icon,
        // adhoc #99) — same tinting as themedOcticon, just rotated once rather
        // than every animation frame like rotatedTintedOcticonPixmap's callers.
        QIcon icon;
        icon.addPixmap(rotatedTintedOcticonPixmap(name, color, px, rotation));
        icon.addPixmap(rotatedTintedOcticonPixmap(name, color.darker(120), px, rotation),
                       QIcon::Active, QIcon::Off);
        icon.addPixmap(
            rotatedTintedOcticonPixmap(name, QColor("#6e7681"), px, rotation),
            QIcon::Disabled, QIcon::Off);
        button->setIcon(icon);
    } else {
        button->setIcon(themedOcticon(name, color, px));
    }
    button->setIconSize(QSize(px, px));
}

inline void setOcticon(QPushButton *button, const QString &name, int size = 16,
                       qreal rotationDeg = 0.0)
{
    if (!button)
        return;
    button->setProperty("forkmeshOcticon", name);
    button->setProperty("forkmeshOcticonSize", size);
    button->setProperty("forkmeshOcticonRotation", rotationDeg);
    applyStoredOcticon(button);
}

// A push button whose label never pins its pane open: the text is elided to
// whatever width the button is actually given, and both its preferred and its
// minimum width are capped instead of tracking the full string. A long branch
// name in the commits search row otherwise set the minimum width of the whole
// left column, so dragging the workspace splitter narrower "got stuck" hundreds
// of pixels short of where it could go (adhoc #74). Keep the untruncated text on
// the tooltip at the call site.
class ElidingPushButton : public QPushButton
{
public:
    using QPushButton::QPushButton;

    // Full, untruncated label. What's painted is derived from it on every
    // resize; setText() alone would be overwritten by the next elide.
    void setFullText(const QString &text)
    {
        m_fullText = text;
        applyElide();
    }
    QString fullText() const { return m_fullText; }

    // Both hints are computed from the *full* text, never from the elided one,
    // so a re-elide can never feed back into the layout that caused it.
    QSize sizeHint() const override
    {
        QSize hint = QPushButton::sizeHint();
        hint.setWidth(qBound(kMinWidth,
                             fontMetrics().horizontalAdvance(m_fullText) +
                                 chromeWidth(),
                             kMaxWidth));
        return hint;
    }
    QSize minimumSizeHint() const override
    {
        QSize hint = QPushButton::minimumSizeHint();
        hint.setWidth(qMin(hint.width(), kMinWidth));
        return hint;
    }

protected:
    void resizeEvent(QResizeEvent *event) override
    {
        QPushButton::resizeEvent(event);
        applyElide();
    }

private:
    static constexpr int kMinWidth = 56;  // still shows a few characters
    static constexpr int kMaxWidth = 240; // long refs stop growing the row here

    // The frame padding, the leading octicon and the menu indicator all eat
    // into the width the label actually gets.
    int chromeWidth() const
    {
        return 28 + (icon().isNull() ? 0 : iconSize().width() + 6) +
               (menu() ? 14 : 0);
    }

    void applyElide()
    {
        const QString elided = fontMetrics().elidedText(
            m_fullText, Qt::ElideMiddle, qMax(0, width() - chromeWidth()));
        if (elided != text())
            QPushButton::setText(elided);
    }

    QString m_fullText;
};

// A push button that stacks its octicon above a small caption — the same
// icon-over-words form as the activity rail's entries (adhoc #91) — but driven
// by the button's live text(), so the existing "Issues (60)" / "Fork 0" count
// updates keep working. Two forms: Tab paints the repo tabs' checked underline,
// Action paints the repoAction pill's fill and border. Fully custom-painted
// (like ActivityRailButton), so the QPushButton QSS box — including
// #repoAction's max-height, which would squash the stacked layout — never
// shapes what's drawn.
class VerticalIconButton : public QPushButton
{
public:
    enum Form { Action, Tab };
    explicit VerticalIconButton(const QString &text, Form form,
                                QWidget *parent = nullptr)
        : QPushButton(text, parent), m_form(form)
    {
        setCursor(Qt::PointingHandCursor);
    }

    QSize sizeHint() const override
    {
        QFont f = font();
        f.setPixelSize(10);
        f.setWeight(QFont::DemiBold);
        const int textW = QFontMetrics(f).horizontalAdvance(text());
        return QSize(qMax(44, qMax(kIconPx, textW) + 16), kHeight);
    }
    QSize minimumSizeHint() const override { return sizeHint(); }

    // The count riding the icon's upper-right corner as a rail-style circle
    // badge (0 hides it), so "Fork 12" / "147 branches" style counts read the
    // same as the activity rail's badges instead of living in the caption.
    void setBadgeCount(qint64 count)
    {
        if (m_badge == count)
            return;
        m_badge = count;
        update();
    }
    qint64 badgeCount() const { return m_badge; }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        const bool dark = currentThemeIsDark();
        const bool hovered = isEnabled() && underMouse();
        QColor fg;
        if (m_form == Tab)
            fg = dark ? QColor(isChecked() || hovered ? "#e6edf3" : "#8b949e")
                      : QColor(isChecked() || hovered ? "#1f2328" : "#656d76");
        else
            fg = dark ? QColor("#e6edf3") : QColor("#1f2328");
        if (!isEnabled())
            fg = QColor("#6e7681");

        if (m_form == Action) {
            p.setPen(QColor(dark ? "#30363d" : "#d0d7de"));
            p.setBrush(QColor(dark ? (hovered ? "#30363d" : "#21262d")
                                   : (hovered ? "#d0d7de" : "#eaeef2")));
            p.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), 6,
                              6);
        } else if (isChecked()) {
            p.fillRect(QRect(0, height() - 2, width(), 2),
                       QColor(dark ? "#2ea043" : "#1f883d"));
        }

        const QRect iconRect((width() - kIconPx) / 2, 6, kIconPx, kIconPx);
        icon().paint(&p, iconRect, Qt::AlignCenter,
                     isEnabled() ? QIcon::Normal : QIcon::Disabled);

        QFont f = font();
        f.setPixelSize(10);
        f.setWeight(QFont::DemiBold);
        p.setFont(f);
        p.setPen(fg);
        p.drawText(QRect(2, iconRect.bottom() + 2, width() - 4, 14),
                   Qt::AlignHCenter | Qt::AlignTop,
                   QFontMetrics(f).elidedText(text(), Qt::ElideRight,
                                              width() - 4));

        // Count badge on the icon's upper-right corner, the same geometry and
        // blue as ActivityRailButton's, but never capped at 99+ — a repo can
        // legitimately advertise hundreds of branches.
        if (m_badge > 0) {
            const QString badgeText = formatCount(m_badge);
            QFont bf = font();
            bf.setPixelSize(9);
            bf.setBold(true);
            p.setFont(bf);
            const int h = 14;
            const int w =
                qMax(h, QFontMetrics(bf).horizontalAdvance(badgeText) + 8);
            const QRectF badge(iconRect.right() - w + h / 2.0 + 2,
                               qMax(0.0, double(iconRect.top() - 5)), w, h);
            p.setPen(Qt::NoPen);
            p.setBrush(QColor("#1f6feb"));
            p.drawRoundedRect(badge, h / 2.0, h / 2.0);
            p.setPen(QColor("#ffffff"));
            p.drawText(badge, Qt::AlignCenter, badgeText);
        }
    }

private:
    static constexpr int kIconPx = 16;
    static constexpr int kHeight = 44;
    Form m_form;
    qint64 m_badge = 0;
};

// Width and height of one activity-rail entry, and the width of the rail
// (scroll area) itself. Every badge in the rail rides its own icon's corner
// rather than the item's outer edge, so an item only has to be as wide as its
// icon plus its caption — the rail no longer reserves a column of empty space
// for a count (adhoc #19). Adhoc #117 slimmed the rail: every destination is
// the same icon-over-caption item, so the item is exactly wide enough for the
// longest caption and the rail only adds its own slim 6px scrollbar (see the
// #appNavigationRail QScrollBar rule in Theme.h).
constexpr int kRailItemWidth = 42;
constexpr int kRailItemHeight = 44; // 20px icon + 10px caption + breathing room

// The 42px floor fits every rail caption in each of main()'s preferred UI
// families (Inter/SF/Segoe/Roboto/Noto/Ubuntu/Cantarell measure "Network",
// the widest word, at <=40px in the 10px demi-bold caption font). A box that
// has none of them can fall back to a wider face (DejaVu draws it at 48px),
// so measure the app rail's actual caption set once in the real UI font and
// widen just enough that no word is ever cut. Font is fixed at startup, so a
// once-computed static is safe.
inline int railItemWidth()
{
    static const int width = [] {
        QFont f = QGuiApplication::font();
        f.setPixelSize(10);
        f.setWeight(QFont::DemiBold);
        const QFontMetrics metrics(f);
        int widest = kRailItemWidth;
        for (const char *caption :
             {"Agents", "Code", "Git", "Repos", "Chat", "Control", "Network",
              "Settings", "Log", "Capture", "Resize", "Tasks", "Pings",
              "Account"})
            widest = qMax(widest, metrics.horizontalAdvance(
                                      QString::fromLatin1(caption)) + 4);
        return widest;
    }();
    return width;
}
inline int railWidth() { return railItemWidth() + 6; } // + slim scrollbar

// One entry in the app-wide activity rail: an octicon over an optional small
// label, VS-Code style, with the selected state drawn as a 2px accent line along
// the item's left edge. A blue count badge rides above the icon, where it cannot
// obscure the caption; a small rotating sync glyph can replace it while a repo
// is publishing/syncing. Fully custom-painted (icon tint follows the live theme
// on every repaint), so no QSS or stored-octicon re-tinting applies.
class ActivityRailButton : public QPushButton
{
public:
    explicit ActivityRailButton(const QString &iconName, const QString &label,
                                QWidget *parent = nullptr)
        : QPushButton(label, parent), m_iconName(iconName), m_label(label)
    {
        setCheckable(true);
        setCursor(Qt::PointingHandCursor);
        setFlat(true);
        setAccessibleName(m_label);
        setFixedSize(railItemWidth(), m_label.isEmpty() ? 40 : kRailItemHeight);
        // The sync spinner's timer only runs while syncing *and* visible (see
        // show/hideEvent), so an idle or hidden item costs nothing.
        m_spinTimer = new QTimer(this);
        m_spinTimer->setInterval(60);
        connect(m_spinTimer, &QTimer::timeout, this, [this] {
            m_spinAngle = (m_spinAngle + 30) % 360;
            update();
        });
    }

    // The app-wide rail has more destinations than the old repo-only rail.
    // Its compact mode keeps the same icon, badge, selection line, tooltip and
    // accessible text while omitting the painted caption.
    void setCompact(bool compact)
    {
        if (m_compact == compact)
            return;
        m_compact = compact;
        setFixedSize(railItemWidth(),
                     m_compact ? 30 : (m_label.isEmpty() ? 40 : kRailItemHeight));
        update();
    }

    // The count riding the icon's corner (0 hides the badge).
    void setBadgeCount(int count)
    {
        if (m_badge == count)
            return;
        m_badge = count;
        update();
    }
    int badgeCount() const { return m_badge; }

    // Commits that exist locally but have not reached the upstream/mirror yet.
    // This is a separate upper-left upload marker so it can coexist with the
    // working-tree count badge (or the in-flight sync spinner) on Git.
    void setPendingSyncCount(int count)
    {
        count = qMax(0, count);
        if (m_pendingSync == count)
            return;
        m_pendingSync = count;
        update();
    }
    int pendingSyncCount() const { return m_pendingSync; }

    // Red "needs you" badge (Chat unread, pending Pings) instead of the default
    // blue count — the same corner geometry either way, so the two badge
    // languages stay aligned across the rail.
    void setBadgeUrgent(bool urgent)
    {
        if (m_badgeUrgent == urgent)
            return;
        m_badgeUrgent = urgent;
        update();
    }

    // Amber icon+caption tint while something is waiting (the Pings bell) —
    // the painted replacement for the old QSS [alert="true"] accent.
    void setAlertTint(bool alert)
    {
        if (m_alert == alert)
            return;
        m_alert = alert;
        update();
    }

    void setSyncing(bool on)
    {
        if (m_syncing == on)
            return;
        m_syncing = on;
        if (m_syncing && isVisible())
            m_spinTimer->start();
        else
            m_spinTimer->stop();
        update();
    }

protected:
    void showEvent(QShowEvent *e) override
    {
        if (m_syncing)
            m_spinTimer->start();
        QPushButton::showEvent(e);
    }
    void hideEvent(QHideEvent *e) override
    {
        m_spinTimer->stop();
        QPushButton::hideEvent(e);
    }
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        const bool dark = currentThemeIsDark();
        const bool lit = isEnabled() && (isChecked() || underMouse());
        // The alert tint outranks the resting grey but still brightens on
        // hover/checked, mirroring the old QSS [alert="true"] rules. A disabled
        // item (the agent detail reuses this class for its action buttons, which
        // grey out per session) drops to a low-contrast grey.
        const QColor fg =
            !isEnabled() ? QColor(dark ? "#484f58" : "#b6bdc4")
            : m_alert    ? QColor(dark ? (lit ? "#f0b72f" : "#d29922")
                                       : (lit ? "#7d4e00" : "#9a6700"))
                         : (dark ? QColor(lit ? "#e6edf3" : "#8b949e")
                                 : QColor(lit ? "#1f2328" : "#656d76"));
        const bool showLabel = !m_compact && !m_label.isEmpty();

        // Selection line along the left edge — same accent green as the repo
        // tabs' checked underline.
        if (isChecked())
            p.fillRect(QRectF(0, 4, 2, height() - 8), QColor("#2ea043"));

        const int iconPx = 20;
        const QRect iconRect((width() - iconPx) / 2,
                             showLabel ? 6 : (height() - iconPx) / 2,
                             iconPx, iconPx);
        p.drawPixmap(iconRect.topLeft(),
                     tintedOcticonPixmap(m_iconName, fg, iconPx));

        // An amber upload arrow on the opposite corner from the ordinary count
        // badge makes "commits waiting to sync" visible without hiding dirty
        // file count or an active-sync spinner.
        if (m_pendingSync > 0) {
            const int s = 12;
            const QPoint at(qMax(1, iconRect.left() - 5),
                            qMax(0, iconRect.top() - 4));
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(dark ? "#0d1117" : "#ffffff"));
            p.drawEllipse(QRect(at, QSize(s, s)).adjusted(-1, -1, 1, 1));
            p.drawPixmap(
                at, tintedOcticonPixmap(
                        QStringLiteral("upload"),
                        QColor(dark ? "#d29922" : "#9a6700"), s));
        }

        if (showLabel) {
            QFont f = font();
            f.setPixelSize(10);
            f.setWeight(QFont::DemiBold);
            p.setFont(f);
            p.setPen(fg);
            // Elide as a guard for wide fallback fonts; the item width is sized
            // so the longest caption fits in every preferred UI family.
            p.drawText(QRect(1, iconRect.bottom() + 2, width() - 2, 14),
                       Qt::AlignHCenter | Qt::AlignTop,
                       QFontMetrics(f).elidedText(m_label, Qt::ElideRight,
                                                  width() - 2));
        }

        // Badge / sync spinner on the icon's upper-right corner.
        if (m_syncing) {
            const int s = 14;
            const QPoint at(iconRect.right() - s / 2 + 4,
                            qMax(0, iconRect.top() - 4));
            // Knock out a disc behind the glyph so it reads over the icon.
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(dark ? "#0d1117" : "#ffffff"));
            p.drawEllipse(QRect(at, QSize(s, s)).adjusted(-1, -1, 1, 1));
            p.drawPixmap(at, refreshPixmap(QColor(Theme::kRunning), m_spinAngle, s));
        } else if (m_badge > 0) {
            const QString text = m_badge > 99 ? QStringLiteral("99+")
                                              : QString::number(m_badge);
            QFont f = font();
            f.setPixelSize(9);
            f.setBold(true);
            p.setFont(f);
            const int h = 14;
            const int w = qMax(h, QFontMetrics(f).horizontalAdvance(text) + 8);
            const QRectF badge(
                iconRect.right() - w + h / 2.0 + 2,
                qMax(0.0, double(iconRect.top() - 5)),
                w, h);
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(m_badgeUrgent ? (dark ? "#da3633" : "#cf222e")
                                            : "#1f6feb"));
            p.drawRoundedRect(badge, h / 2.0, h / 2.0);
            p.setPen(QColor("#ffffff"));
            p.drawText(badge, Qt::AlignCenter, text);
        }
    }
    void enterEvent(QEnterEvent *e) override
    {
        update();
        QPushButton::enterEvent(e);
    }
    void leaveEvent(QEvent *e) override
    {
        update();
        QPushButton::leaveEvent(e);
    }

private:
    QString m_iconName;
    QString m_label;
    int m_badge = 0;
    int m_pendingSync = 0;
    bool m_badgeUrgent = false;
    bool m_alert = false;
    bool m_syncing = false;
    bool m_compact = false;
    QTimer *m_spinTimer = nullptr;
    int m_spinAngle = 0;
};

// ---- voice input (whisper.cpp) helpers --------------------------------------
// Where whisper.cpp is cloned/built. Defaults to the app's local-data dir; the
// installer records the chosen dir so detection survives across launches.
inline QString whisperDir()
{
    const QString stored =
        QSettings().value(kWhisperDirSetting).toString().trimmed();
    if (!stored.isEmpty())
        return stored;
    const QString base =
        QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    return QDir(base.isEmpty() ? QDir::homePath() : base)
        .filePath(QStringLiteral("whisper.cpp"));
}

// Which ggml model was downloaded (English-only variants keep transcription fast
// and accurate for prompts). Defaults to base.en.
inline QString whisperModelName()
{
    const QString stored =
        QSettings().value(kWhisperModelSetting).toString().trimmed();
    return stored.isEmpty() ? QStringLiteral("base.en") : stored;
}

inline QString whisperModelPath()
{
    return QDir(whisperDir())
        .filePath(QStringLiteral("models/ggml-%1.bin").arg(whisperModelName()));
}

// whisper.cpp's CLI moved from ./main to ./build/bin/whisper-cli across releases,
// so probe the new name first then the legacy ones. Empty == not built yet.
inline QString whisperBinaryPath()
{
    const QDir dir(whisperDir());
    static const char *const candidates[] = {
        "build/bin/whisper-cli", "build/bin/main", "main", "whisper-cli"};
    for (const char *c : candidates) {
        const QString p = dir.filePath(QString::fromLatin1(c));
        if (QFileInfo::exists(p))
            return p;
    }
    return QString();
}

inline bool whisperInstalled()
{
    return !whisperBinaryPath().isEmpty() && QFileInfo::exists(whisperModelPath());
}

// ---- voice input (Parakeet) helpers -----------------------------------------
// Which speech engine the mic uses. Defaults to whisper.cpp; "parakeet" opts into
// the NVIDIA Parakeet runner provisioned from Settings.
inline QString voiceEngine()
{
    const QString e =
        QSettings().value(kVoiceEngineSetting).toString().trimmed().toLower();
    return e == QStringLiteral("parakeet") ? QStringLiteral("parakeet")
                                           : QStringLiteral("whisper");
}

// Parakeet is run from a self-contained Python venv (parakeet-mlx on Apple
// Silicon, NeMo elsewhere) living in the app's local-data dir, alongside the
// transcribe.py wrapper the installer drops there.
inline QString parakeetDir()
{
    const QString base =
        QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    return QDir(base.isEmpty() ? QDir::homePath() : base)
        .filePath(QStringLiteral("parakeet"));
}

inline QString parakeetPython()
{
    const QDir dir(parakeetDir());
#if defined(Q_OS_WIN)
    return dir.filePath(QStringLiteral("venv/Scripts/python.exe"));
#else
    return dir.filePath(QStringLiteral("venv/bin/python"));
#endif
}

inline QString parakeetScriptPath()
{
    return QDir(parakeetDir()).filePath(QStringLiteral("transcribe.py"));
}

// Which Parakeet checkpoint to load. A bare name is mapped to the right HF repo by
// transcribe.py (mlx-community/… for MLX, nvidia/… for NeMo).
inline QString parakeetModelName()
{
    const QString stored =
        QSettings().value(kParakeetModelSetting).toString().trimmed();
    return stored.isEmpty() ? QStringLiteral("parakeet-tdt-0.6b-v2") : stored;
}

inline bool parakeetInstalled()
{
    return QFileInfo::exists(parakeetPython()) &&
           QFileInfo::exists(parakeetScriptPath());
}

// Whether the currently-selected voice engine is provisioned and ready to use.
inline bool voiceInputReady()
{
    return voiceEngine() == QStringLiteral("parakeet") ? parakeetInstalled()
                                                       : whisperInstalled();
}

// Short human-readable name of the engine + model dictation will run, e.g.
// "Whisper base.en" or "Parakeet parakeet-tdt-0.6b-v2". Surfaced in the mic
// tooltips so it's clear which speech-to-text model is transcribing.
inline QString voiceModelLabel()
{
    return voiceEngine() == QStringLiteral("parakeet")
               ? QStringLiteral("Parakeet %1").arg(parakeetModelName())
               : QStringLiteral("Whisper %1").arg(whisperModelName());
}

// A CLI audio recorder + the args to capture 16 kHz mono 16-bit WAV (what
// whisper.cpp expects) into `outWav`, running until the process is terminated.
// An empty program means no supported recorder is installed.
struct AudioRecorderCommand {
    QString program;
    QStringList args;
};

// Which of the supported CLI recorders is installed, or empty if none is. On
// Linux the priority order matches audioRecorderFor() (arecord > parecord >
// ffmpeg); on macOS/Windows neither ships a capture CLI, so we drive ffmpeg
// (avfoundation / dshow), which is the only portable recorder there. Kept
// separate so the Settings mic-picker enumerates devices for the same tool that
// will actually capture.
inline QString preferredAudioRecorder()
{
#if defined(Q_OS_MACOS) || defined(Q_OS_WIN)
    if (!QStandardPaths::findExecutable(QStringLiteral("ffmpeg")).isEmpty())
        return QStringLiteral("ffmpeg");
    return QString();
#else
    for (const char *p : {"arecord", "parecord", "ffmpeg"})
        if (!QStandardPaths::findExecutable(QString::fromLatin1(p)).isEmpty())
            return QString::fromLatin1(p);
    return QString();
#endif
}

// Available microphone/input devices for the installed recorder, as
// {display label, device id} pairs. The id is what audioRecorderFor() hands the
// recorder (-D for arecord / -i for ffmpeg-alsa, --device= for parecord, ":N" for
// avfoundation, the device name for dshow); an empty id means "system default".
// Best-effort: returns just the default entry when the listing command is missing
// or unparseable.
inline QList<QPair<QString, QString>> voiceInputDevices()
{
    QList<QPair<QString, QString>> out;
    out.append({QStringLiteral("System default"), QString()});
    const QString tool = preferredAudioRecorder();
    if (tool.isEmpty())
        return out;
    // Run a listing command and return its combined output. ffmpeg dumps its
    // device list to stderr and then exits non-zero, which is expected here, so we
    // merge the channels and ignore the exit code.
    auto runCmd = [](const QString &prog, const QStringList &args) -> QString {
        if (QStandardPaths::findExecutable(prog).isEmpty())
            return QString();
        QProcess p;
        p.setProcessChannelMode(QProcess::MergedChannels);
        p.start(prog, args);
        if (!p.waitForFinished(3000))
            return QString();
        return QString::fromUtf8(p.readAll());
    };
#if defined(Q_OS_MACOS)
    // avfoundation device dump: "[N] Device Name" lines under the "AVFoundation
    // audio devices:" header. The capture id ffmpeg wants is ":N".
    const QString listing = runCmd(
        QStringLiteral("ffmpeg"),
        {QStringLiteral("-hide_banner"), QStringLiteral("-f"),
         QStringLiteral("avfoundation"), QStringLiteral("-list_devices"),
         QStringLiteral("true"), QStringLiteral("-i"), QStringLiteral("")});
    static const QRegularExpression re(QStringLiteral("\\[(\\d+)\\]\\s+(.+?)\\s*$"));
    bool inAudio = false;
    for (const QString &line :
         listing.split(QLatin1Char('\n'), Qt::SkipEmptyParts)) {
        if (line.contains(QStringLiteral("audio devices"))) {
            inAudio = true;
            continue;
        }
        if (line.contains(QStringLiteral("video devices"))) {
            inAudio = false;
            continue;
        }
        if (!inAudio)
            continue;
        const QRegularExpressionMatch m = re.match(line);
        if (m.hasMatch())
            out.append(
                {m.captured(2).trimmed(), QStringLiteral(":") + m.captured(1)});
    }
    return out;
#elif defined(Q_OS_WIN)
    // dshow device dump: audio devices appear as quoted names, either grouped
    // under a "DirectShow audio devices" header (older ffmpeg) or suffixed with
    // "(audio)" (newer ffmpeg). The capture id is the bare device name.
    const QString listing = runCmd(
        QStringLiteral("ffmpeg"),
        {QStringLiteral("-hide_banner"), QStringLiteral("-list_devices"),
         QStringLiteral("true"), QStringLiteral("-f"), QStringLiteral("dshow"),
         QStringLiteral("-i"), QStringLiteral("dummy")});
    static const QRegularExpression re(QStringLiteral("\"([^\"]+)\""));
    bool inAudio = false;
    for (const QString &line :
         listing.split(QLatin1Char('\n'), Qt::SkipEmptyParts)) {
        if (line.contains(QStringLiteral("audio devices"))) {
            inAudio = true;
            continue;
        }
        if (line.contains(QStringLiteral("video devices"))) {
            inAudio = false;
            continue;
        }
        // The alternative-name line is a device path, not a friendly name.
        if (line.contains(QStringLiteral("Alternative name")))
            continue;
        const bool audioLine = inAudio || line.contains(QStringLiteral("(audio)"));
        if (!audioLine)
            continue;
        const QRegularExpressionMatch m = re.match(line);
        if (m.hasMatch())
            out.append({m.captured(1), m.captured(1)});
    }
    return out;
#else
    if (tool == QLatin1String("parecord")) {
        // PulseAudio/PipeWire capture sources via pactl; skip the ".monitor"
        // loopbacks (those tap output, not a mic).
        const QString listing =
            runCmd(QStringLiteral("pactl"),
                   {QStringLiteral("list"), QStringLiteral("short"),
                    QStringLiteral("sources")});
        const QStringList lines =
            listing.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
        for (const QString &line : lines) {
            const QStringList cols = line.split(QLatin1Char('\t'), Qt::SkipEmptyParts);
            if (cols.size() < 2)
                continue;
            const QString name = cols.at(1);
            if (name.endsWith(QStringLiteral(".monitor")))
                continue;
            out.append({name, name});
        }
    } else if (tool == QLatin1String("arecord") || tool == QLatin1String("ffmpeg")) {
        // ALSA capture devices from `arecord -l`:
        //   "card X: ID [Friendly Name], device Y: ... [...]"
        const QString listing =
            runCmd(QStringLiteral("arecord"), {QStringLiteral("-l")});
        static const QRegularExpression re(QStringLiteral(
            "card (\\d+): \\S+ \\[([^\\]]*)\\], device (\\d+):"));
        QRegularExpressionMatchIterator it = re.globalMatch(listing);
        while (it.hasNext()) {
            const QRegularExpressionMatch m = it.next();
            const QString card = m.captured(1);
            const QString dev = m.captured(3);
            const QString cardName = m.captured(2).trimmed();
            // plughw converts whatever the card offers to our 16 kHz mono S16_LE.
            const QString id = QStringLiteral("plughw:%1,%2").arg(card, dev);
            QString label = cardName.isEmpty() ? id : cardName;
            if (dev != QLatin1String("0"))
                label += QStringLiteral(" (device %1)").arg(dev);
            out.append({label, id});
        }
    }
    return out;
#endif
}

inline AudioRecorderCommand audioRecorderFor(const QString &outWav)
{
    auto have = [](const char *p) {
        return !QStandardPaths::findExecutable(QString::fromLatin1(p)).isEmpty();
    };
    // The mic chosen in Settings (empty == the recorder's own default device).
    const QString device =
        QSettings().value(kVoiceInputDeviceSetting).toString().trimmed();
#if defined(Q_OS_MACOS)
    // macOS has no capture CLI: drive ffmpeg's avfoundation input. The device id
    // is ":N" (audio index); ":default" follows the system default mic.
    // -flush_packets keeps the WAV growing in near-real-time so the live level
    // meter moves while you speak.
    if (have("ffmpeg"))
        return {QStringLiteral("ffmpeg"),
                {QStringLiteral("-loglevel"), QStringLiteral("error"),
                 QStringLiteral("-y"), QStringLiteral("-f"),
                 QStringLiteral("avfoundation"), QStringLiteral("-i"),
                 device.isEmpty() ? QStringLiteral(":default") : device,
                 QStringLiteral("-ar"), QStringLiteral("16000"),
                 QStringLiteral("-ac"), QStringLiteral("1"),
                 QStringLiteral("-flush_packets"), QStringLiteral("1"), outWav}};
    return {};
#elif defined(Q_OS_WIN)
    // Windows has no capture CLI: drive ffmpeg's dshow input. dshow has no
    // "default" device, so when none is chosen fall back to the first enumerated
    // microphone.
    if (have("ffmpeg")) {
        QString name = device;
        if (name.isEmpty()) {
            for (const auto &d : voiceInputDevices())
                if (!d.second.isEmpty()) {
                    name = d.second;
                    break;
                }
        }
        if (name.isEmpty())
            return {};
        return {QStringLiteral("ffmpeg"),
                {QStringLiteral("-loglevel"), QStringLiteral("error"),
                 QStringLiteral("-y"), QStringLiteral("-f"), QStringLiteral("dshow"),
                 QStringLiteral("-i"), QStringLiteral("audio=") + name,
                 QStringLiteral("-ar"), QStringLiteral("16000"),
                 QStringLiteral("-ac"), QStringLiteral("1"),
                 QStringLiteral("-flush_packets"), QStringLiteral("1"), outWav}};
    }
    return {};
#else
    if (have("arecord")) {
        QStringList args = {QStringLiteral("-q"), QStringLiteral("-f"),
                            QStringLiteral("S16_LE"), QStringLiteral("-c"),
                            QStringLiteral("1"), QStringLiteral("-r"),
                            QStringLiteral("16000"), QStringLiteral("-t"),
                            QStringLiteral("wav")};
        if (!device.isEmpty())
            args << QStringLiteral("-D") << device;
        args << outWav;
        return {QStringLiteral("arecord"), args};
    }
    if (have("parecord")) {
        QStringList args = {QStringLiteral("--rate=16000"),
                            QStringLiteral("--channels=1"),
                            QStringLiteral("--format=s16le"),
                            QStringLiteral("--file-format=wav")};
        if (!device.isEmpty())
            args << (QStringLiteral("--device=") + device);
        args << outWav;
        return {QStringLiteral("parecord"), args};
    }
    if (have("ffmpeg"))
        return {QStringLiteral("ffmpeg"),
                {QStringLiteral("-loglevel"), QStringLiteral("error"),
                 QStringLiteral("-y"), QStringLiteral("-f"), QStringLiteral("alsa"),
                 QStringLiteral("-i"),
                 device.isEmpty() ? QStringLiteral("default") : device,
                 QStringLiteral("-ar"), QStringLiteral("16000"),
                 QStringLiteral("-ac"), QStringLiteral("1"),
                 QStringLiteral("-flush_packets"), QStringLiteral("1"), outWav}};
    return {};
#endif
}

// Peak amplitude (0..1, fraction of full scale) of a 16-bit mono PCM WAV, or -1
// if the file can't be parsed. Used to tell speech from a silent capture: whisper
// invents words ("you", "thank you") when fed silence, so a near-zero peak means
// "nothing was said" no matter what whisper printed. Tolerates a partially-written
// file (data length over-declared by a still-running recorder) by clamping to EOF.
inline double wavPeakAmplitude(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return -1.0;
    const QByteArray d = f.readAll();
    if (d.size() < 44 || !d.startsWith("RIFF") || d.mid(8, 4) != "WAVE")
        return -1.0;
    // Walk RIFF chunks to the "data" payload (normally right after "fmt ").
    int pos = 12, dataOff = -1;
    qint64 dataLen = 0;
    while (pos + 8 <= d.size()) {
        const quint32 sz = quint8(d[pos + 4]) | (quint8(d[pos + 5]) << 8) |
                           (quint8(d[pos + 6]) << 16) |
                           (quint32(quint8(d[pos + 7])) << 24);
        if (d.mid(pos, 4) == "data") {
            dataOff = pos + 8;
            dataLen = sz;
            break;
        }
        pos += 8 + int(sz) + (sz & 1);
    }
    if (dataOff < 0)
        return -1.0;
    qint64 end = d.size();
    if (dataLen > 0)
        end = qMin<qint64>(end, dataOff + dataLen);
    int peak = 0;
    const uchar *b = reinterpret_cast<const uchar *>(d.constData());
    for (qint64 i = dataOff; i + 1 < end; i += 2) {
        const qint16 s = qint16(quint16(b[i]) | (quint16(b[i + 1]) << 8));
        peak = qMax(peak, qAbs(int(s)));
    }
    return double(peak) / 32768.0;
}

// Below this peak (≈ -34 dBFS) a clip is treated as silence rather than speech.
inline constexpr double kVoiceSpokeThreshold = 0.02;

// Live-meter helper: peak amplitude (0..1) of the PCM samples appended to a
// growing 16-bit mono WAV since byte offset *pos, advancing *pos to the new end.
// On the first call (*pos < 44) the RIFF header is parsed to locate the data
// chunk; thereafter it just reads forward from where it left off, so each meter
// tick only scans freshly-captured audio. Returns -1 when there are no new
// samples yet or the file can't be read.
inline double wavLevelSince(const QString &path, qint64 *pos)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return -1.0;
    const qint64 size = f.size();
    qint64 start = *pos;
    if (start < 44) {
        const QByteArray head = f.read(qMin<qint64>(size, 4096));
        if (head.size() < 44 || !head.startsWith("RIFF") || head.mid(8, 4) != "WAVE")
            return -1.0;
        int p = 12, dataOff = -1;
        while (p + 8 <= head.size()) {
            const quint32 sz = quint8(head[p + 4]) | (quint8(head[p + 5]) << 8) |
                               (quint8(head[p + 6]) << 16) |
                               (quint32(quint8(head[p + 7])) << 24);
            if (head.mid(p, 4) == "data") {
                dataOff = p + 8;
                break;
            }
            p += 8 + int(sz) + (sz & 1);
        }
        if (dataOff < 0)
            return -1.0;
        start = dataOff;
    }
    if (start >= size) {
        *pos = start;
        return -1.0;
    }
    if (!f.seek(start))
        return -1.0;
    const QByteArray chunk = f.readAll();
    const int n = chunk.size() & ~1; // whole 16-bit samples only
    int peak = 0;
    const uchar *b = reinterpret_cast<const uchar *>(chunk.constData());
    for (int i = 0; i + 1 < n; i += 2) {
        const qint16 s = qint16(quint16(b[i]) | (quint16(b[i + 1]) << 8));
        peak = qMax(peak, qAbs(int(s)));
    }
    *pos = start + n;
    return double(peak) / 32768.0;
}

// whisper.cpp hallucinates a handful of stock phrases out of silence/near-silence
// ("you", "thank you", "thanks for watching", …). When one of those is the ENTIRE
// transcript it's almost certainly noise, not a dictated prompt, so we drop it
// rather than typing a stray word into the box.
inline bool isWhisperSilenceHallucination(const QString &textIn)
{
    QString norm;
    for (const QChar c : textIn)
        if (c.isLetter() || c == QLatin1Char(' '))
            norm.append(c.toLower());
    norm = norm.simplified();
    static const QStringList kStock = {
        QStringLiteral("you"),
        QStringLiteral("thank you"),
        QStringLiteral("thank you very much"),
        QStringLiteral("thanks for watching"),
        QStringLiteral("thanks for watching the video"),
        QStringLiteral("please subscribe"),
        QStringLiteral("bye"),
        QStringLiteral("bye bye"),
    };
    return kStock.contains(norm);
}

// Widen a changed-files list so its longest entry opens fully visible instead of
// being elided, capped so an unusually long path doesn't crowd out the diff. Only
// the minimum width is set, so the user can still drag the panel wider.
inline void fitFileListToWidestEntry(QListWidget *list, int minW = 180, int maxW = 520)
{
    if (!list)
        return;
    const QFontMetrics fm(list->fontMetrics());
    int widest = 0;
    for (int i = 0; i < list->count(); ++i)
        widest = qMax(widest, fm.horizontalAdvance(list->item(i)->text()));
    // Leave room for the leading status icon, row padding, and the scrollbar.
    const int chrome = 52 + list->verticalScrollBar()->sizeHint().width();
    list->setMinimumWidth(qBound(minW, widest + chrome, maxW));
}

// Same idea for the Source Control file tree (column 0 holds the full relative
// path), so the changes panel opens wide enough that filenames aren't clipped.
// Items live one level under their group header, hence the 2x indentation.
inline void fitTreeToWidestEntry(QTreeWidget *tree, int minW = 240, int maxW = 620)
{
    if (!tree)
        return;
    const QFontMetrics fm(tree->fontMetrics());
    const int indent = tree->indentation();
    int widest = 0;
    for (int g = 0; g < tree->topLevelItemCount(); ++g) {
        QTreeWidgetItem *grp = tree->topLevelItem(g);
        for (int c = 0; c < grp->childCount(); ++c)
            widest = qMax(widest, fm.horizontalAdvance(grp->child(c)->text(0))
                                      + 2 * indent + 24); // indents + file icon
    }
    if (widest == 0)
        return;
    const int chrome =
        16 + tree->columnWidth(1) + tree->verticalScrollBar()->sizeHint().width();
    tree->setMinimumWidth(qBound(minW, widest + chrome, maxW));
}

// Inline octicon for rich-text QLabels: a tinted SVG rendered to a base64 PNG
// data URI so it can sit next to text in setText() HTML.
inline QString octiconMarkup(const QString &name, int size,
                      const QColor &color = QColor("#8b949e"))
{
    const QPixmap pixmap = tintedOcticonPixmap(name, color, size);
    QByteArray png;
    QBuffer buffer(&png);
    buffer.open(QIODevice::WriteOnly);
    pixmap.save(&buffer, "PNG");
    return QStringLiteral(
               "<img src='data:image/png;base64,%1' width='%2' height='%2'>")
        .arg(QString::fromLatin1(png.toBase64()))
        .arg(size);
}

// "Add to prompt" affordance for the log views (adhoc #114): a tiny plus glyph
// pinned at the very left of every entry, wrapped in an anchor that carries the
// entry's own text. Clicking it appends that line to the footer prompt box (see
// MainWindow::eventFilter), so a line worth asking an agent about takes one
// click instead of a select-copy-paste round trip.
const QString kLogPromptAnchorPrefix = QStringLiteral("fmlogprompt:");
// The document-resource URL the glyph is registered under, one copy per log
// document (the same trick the site favicons use — far cheaper than a base64
// data URI repeated on every one of a few hundred rendered lines).
const QString kLogPromptIconResource = QStringLiteral("logprompt://add");

inline QString logPromptAnchorHref(const QString &storedLine)
{
    // Percent-encoded, so the line's own quotes and ampersands can't break out
    // of the href attribute.
    return kLogPromptAnchorPrefix +
           QString::fromLatin1(QUrl::toPercentEncoding(storedLine.trimmed()));
}

// The log line an anchor href carries, or an empty string when the href is not
// one of ours (a plain http(s) link in the message body, most often).
inline QString logPromptAnchorLine(const QString &href)
{
    if (!href.startsWith(kLogPromptAnchorPrefix))
        return QString();
    return QUrl::fromPercentEncoding(
        href.mid(kLogPromptAnchorPrefix.size()).toLatin1());
}

// The leading icon markup for one log entry, registering the glyph on `view`'s
// document so the <img> resolves there. Grey enough to read on both the Log
// view's themed canvas and the footer strip's forced-white one.
inline QString logPromptIconTag(QTextEdit *view, const QString &storedLine)
{
    if (!view || storedLine.trimmed().isEmpty())
        return QString();
    view->document()->addResource(
        QTextDocument::ImageResource, QUrl(kLogPromptIconResource),
        tintedOcticonPixmap(QStringLiteral("plus"), QColor("#8b949e"), 12));
    return QStringLiteral(
               "<a href='%1' style='text-decoration:none'><img src='%2' "
               "width='11' height='11' style='vertical-align:middle'></a>&nbsp;")
        .arg(logPromptAnchorHref(storedLine), kLogPromptIconResource);
}

inline QString serverHost(const QString &serverUrl)
{
    const QString trimmed = serverUrl.trimmed();
    QUrl url(trimmed);
    if (!url.host().isEmpty())
        return url.host().toLower();

    // Legacy entries and manual input sometimes omit scheme (e.g.
    // "forkmesh.com"). Treat the leading authority token as host so relays can
    // resolve even before migration/canonicalization runs.
    QString host = trimmed;
    const int slash = host.indexOf(QLatin1Char('/'));
    if (slash >= 0)
        host = host.left(slash);
    return host.toLower();
}

// The first-run setup screen shows only the relay *host* (e.g. "forkmesh.com").
// The full wss:// API/room path is an implementation detail assembled here, so
// users never see or have to type it. A bare host is expanded to the standard
// relay URL; an explicit scheme (advanced override / local ws:// relay) is kept
// as-is.
inline QString canonicalServerUrl(const QString &input)
{
    QString s = input.trimmed();
    if (s.isEmpty())
        return kDefaultServerUrl;
    if (s.contains(QStringLiteral("://")))
        return s; // already a full URL
    int slash = s.indexOf(QLatin1Char('/'));
    if (slash >= 0)
        s = s.left(slash); // strip any accidental path, keep host[:port]
    const bool local = s.startsWith(QStringLiteral("127.0.0.1")) ||
                       s.startsWith(QStringLiteral("localhost"));
    return QStringLiteral("%1://%2")
               .arg(local ? QStringLiteral("ws") : QStringLiteral("wss"), s) +
           kMainnodeRoomPath;
}

inline QString normalizedRepoWebsite(QString input, QString *error = nullptr)
{
    input = input.trimmed();
    if (input.isEmpty())
        return {};
    if (!input.contains(QStringLiteral("://")))
        input.prepend(QStringLiteral("https://"));
    const QUrl url(input);
    if (!url.isValid() || url.host().isEmpty() ||
        (url.scheme() != QLatin1String("http") &&
         url.scheme() != QLatin1String("https"))) {
        if (error)
            *error = QStringLiteral("Enter a valid http or https website URL.");
        return {};
    }
    return url.toString(QUrl::RemovePassword);
}

// Inverse of canonicalServerUrl for display: the host (with port when present)
// pulled back out of a stored full URL.
inline QString serverHostDisplay(const QString &fullUrl)
{
    const QUrl u(fullUrl);
    QString host = u.host();
    if (host.isEmpty())
        return fullUrl.trimmed();
    if (u.port() > 0)
        host += QStringLiteral(":") + QString::number(u.port());
    return host;
}

// Map a file name to a vscode-icons SVG base name (without ".svg"). Falls back
// to "default_file"; the caller verifies the file exists.
inline QString fileTypeIconName(const QString &fileNameLower)
{
    static const QHash<QString, QString> byName = {
        {"cmakelists.txt", "file_type_cmake"},
        {"dockerfile", "file_type_docker"},
        {"makefile", "file_type_makefile"},
        {"package.json", "file_type_npm"},
        {"package-lock.json", "file_type_npm"},
        {".gitignore", "file_type_git"},
        {".gitattributes", "file_type_git"},
        {".gitmodules", "file_type_git"},
        {"license", "file_type_license"},
        {"license.md", "file_type_license"},
        {"license.txt", "file_type_license"},
        {"copying", "file_type_license"},
        {"readme.md", "file_type_markdown"},
        {"todo", "file_type_todo"},
        {".env", "file_type_config"},
    };
    if (byName.contains(fileNameLower))
        return byName.value(fileNameLower);

    static const QHash<QString, QString> byExt = {
        {"js", "file_type_js"}, {"mjs", "file_type_js"}, {"cjs", "file_type_js"},
        {"jsx", "file_type_reactjs"}, {"ts", "file_type_typescript"},
        {"tsx", "file_type_reactts"}, {"py", "file_type_python"},
        {"pyw", "file_type_python"}, {"rb", "file_type_ruby"},
        {"rs", "file_type_rust"}, {"go", "file_type_go"},
        {"java", "file_type_java"}, {"kt", "file_type_kotlin"},
        {"kts", "file_type_kotlin"}, {"swift", "file_type_swift"},
        {"c", "file_type_c"}, {"h", "file_type_cheader"},
        {"hpp", "file_type_cpp"}, {"hh", "file_type_cpp"}, {"hxx", "file_type_cpp"},
        {"cpp", "file_type_cpp"}, {"cc", "file_type_cpp"}, {"cxx", "file_type_cpp"},
        {"cs", "file_type_csharp"}, {"php", "file_type_php"},
        {"pl", "file_type_perl"}, {"pm", "file_type_perl"},
        {"lua", "file_type_lua"}, {"r", "file_type_r"},
        {"scala", "file_type_scala"}, {"hs", "file_type_haskell"},
        {"ex", "file_type_elixir"}, {"exs", "file_type_elixir"},
        {"erl", "file_type_erlang"}, {"dart", "file_type_dart"},
        {"html", "file_type_html"}, {"htm", "file_type_html"},
        {"css", "file_type_css"}, {"scss", "file_type_scss"},
        {"sass", "file_type_sass"}, {"less", "file_type_less"},
        {"json", "file_type_json"}, {"yaml", "file_type_yaml"},
        {"yml", "file_type_yaml"}, {"toml", "file_type_toml"},
        {"xml", "file_type_xml"}, {"ini", "file_type_ini"},
        {"cfg", "file_type_config"}, {"conf", "file_type_config"},
        {"md", "file_type_markdown"}, {"markdown", "file_type_markdown"},
        {"txt", "file_type_text"}, {"text", "file_type_text"},
        {"log", "file_type_log"}, {"sql", "file_type_sql"},
        {"sh", "file_type_shell"}, {"bash", "file_type_shell"},
        {"zsh", "file_type_shell"}, {"ps1", "file_type_powershell"},
        {"gradle", "file_type_gradle"}, {"svg", "file_type_svg"},
        {"png", "file_type_image"}, {"jpg", "file_type_image"},
        {"jpeg", "file_type_image"}, {"gif", "file_type_image"},
        {"webp", "file_type_image"}, {"bmp", "file_type_image"},
        {"ico", "file_type_image"}, {"mp3", "file_type_audio"},
        {"wav", "file_type_audio"}, {"flac", "file_type_audio"},
        {"ogg", "file_type_audio"}, {"mp4", "file_type_video"},
        {"mov", "file_type_video"}, {"mkv", "file_type_video"},
        {"webm", "file_type_video"}, {"pdf", "file_type_pdf"},
        {"zip", "file_type_zip"}, {"tar", "file_type_zip"},
        {"gz", "file_type_zip"}, {"7z", "file_type_zip"}, {"rar", "file_type_zip"},
        {"ttf", "file_type_font"}, {"otf", "file_type_font"},
        {"woff", "file_type_font"}, {"woff2", "file_type_font"},
        {"exe", "file_type_binary"}, {"bin", "file_type_binary"},
        {"o", "file_type_binary"}, {"a", "file_type_binary"},
        {"so", "file_type_binary"}, {"dll", "file_type_binary"},
        {"key", "file_type_key"}, {"pem", "file_type_key"},
        {"crt", "file_type_cert"}, {"cert", "file_type_cert"},
        {"cer", "file_type_cert"}, {"cmake", "file_type_cmake"},
    };
    const int dot = fileNameLower.lastIndexOf('.');
    if (dot >= 0) {
        const QString ext = fileNameLower.mid(dot + 1);
        if (byExt.contains(ext))
            return byExt.value(ext);
    }
    return QStringLiteral("default_file");
}

// Display language for a file path (empty = ignore for the language bar).
inline QString languageForFile(const QString &name)
{
    static const QHash<QString, QString> byExt = {
        {"js", "JavaScript"}, {"mjs", "JavaScript"}, {"cjs", "JavaScript"},
        {"jsx", "JavaScript"}, {"ts", "TypeScript"}, {"tsx", "TypeScript"},
        {"py", "Python"}, {"rb", "Ruby"}, {"rs", "Rust"}, {"go", "Go"},
        {"java", "Java"}, {"kt", "Kotlin"}, {"swift", "Swift"}, {"c", "C"},
        {"h", "C"}, {"hpp", "C++"}, {"cpp", "C++"}, {"cc", "C++"}, {"cxx", "C++"},
        {"cs", "C#"}, {"php", "PHP"}, {"pl", "Perl"}, {"lua", "Lua"},
        {"scala", "Scala"}, {"hs", "Haskell"}, {"ex", "Elixir"}, {"exs", "Elixir"},
        {"erl", "Erlang"}, {"dart", "Dart"}, {"html", "HTML"}, {"htm", "HTML"},
        {"css", "CSS"}, {"scss", "SCSS"}, {"sass", "Sass"}, {"less", "Less"},
        {"json", "JSON"}, {"yaml", "YAML"}, {"yml", "YAML"}, {"toml", "TOML"},
        {"xml", "XML"}, {"md", "Markdown"}, {"sh", "Shell"}, {"bash", "Shell"},
        {"sql", "SQL"}, {"vue", "Vue"},
    };
    const int dot = name.lastIndexOf('.');
    if (dot < 0)
        return {};
    return byExt.value(name.mid(dot + 1).toLower());
}

// GitHub linguist-ish color for a language.
inline QString languageColor(const QString &lang)
{
    static const QHash<QString, QString> colors = {
        {"JavaScript", "#f1e05a"}, {"TypeScript", "#3178c6"}, {"Python", "#3572A5"},
        {"Ruby", "#701516"}, {"Rust", "#dea584"}, {"Go", "#00ADD8"},
        {"Java", "#b07219"}, {"Kotlin", "#A97BFF"}, {"Swift", "#F05138"},
        {"C", "#555555"}, {"C++", "#f34b7d"}, {"C#", "#178600"}, {"PHP", "#4F5D95"},
        {"Perl", "#0298c3"}, {"Lua", "#000080"}, {"Scala", "#c22d40"},
        {"Haskell", "#5e5086"}, {"Elixir", "#6e4a7e"}, {"Erlang", "#B83998"},
        {"Dart", "#00B4AB"}, {"HTML", "#e34c26"}, {"CSS", "#563d7c"},
        {"SCSS", "#c6538c"}, {"Sass", "#a53b70"}, {"Less", "#1d365d"},
        {"JSON", "#959595"}, {"YAML", "#cb171e"}, {"TOML", "#9c4221"},
        {"XML", "#0060ac"}, {"Markdown", "#083fa1"}, {"Shell", "#89e051"},
        {"SQL", "#e38c00"}, {"Vue", "#41b883"},
    };
    return colors.value(lang, "#8b949e");
}

// Depth of GitKeepAlive scopes. waitForGit now pumps on every GUI-thread wait
// regardless (see its comment), so the counter no longer gates anything; the
// scopes remain because they document interactive multi-read loads and their
// re-entrancy guards (openRepoDetail's m_repoDetailLoading, the ScopedFlag
// pattern below). User input is excluded from the pump so a stray click can't
// re-enter a load mid-flight.
// inline so the counter is a single shared instance across every TU that
// includes this header (the per-feature MainWindow*.cpp files all use GitKeepAlive).
inline int g_gitKeepAliveDepth = 0;

// Process-wide monotonic clock + the time we last pumped the GUI under a
// keep-alive scope. Shared across every git read so a burst of separate-but-fast
// calls can be throttled as one stream (see waitForGit).
inline QElapsedTimer &keepAliveClock()
{
    static QElapsedTimer c;
    if (!c.isValid())
        c.start();
    return c;
}
inline qint64 g_lastKeepAlivePumpMs = 0;

// Service the GUI (timers — incl. the stall-watchdog heartbeat — paints, queued
// slots, but not user input) and record when. One place so the per-call throttle
// and the in-wait poll share a single "last pumped" timestamp.
inline void pumpKeepAlive()
{
    g_lastKeepAlivePumpMs = keepAliveClock().elapsed();
    QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents, 12);
}

// A compact one-line breadcrumb naming a git subprocess (subcommand + repo) for
// stall reports — e.g. "git log --numstat (forkmesh)". Drops the "-C <dir>" prefix
// our helpers use to target a working tree but keeps the repo's basename, and caps
// length so a long --pretty format or path can't bloat the stall log line.
inline QString gitBlockingCrumb(const QProcess &process)
{
    QStringList args = process.arguments();
    QString repo;
    if (args.size() >= 2 && args.first() == QLatin1String("-C")) {
        repo = QFileInfo(args.at(1)).fileName();
        args = args.mid(2);
    }
    QString cmd = (process.program() + QLatin1Char(' ') + args.join(QLatin1Char(' ')))
                      .simplified();
    constexpr int kMax = 80;
    if (cmd.size() > kMax)
        cmd = cmd.left(kMax - 1) + QStringLiteral("…");
    if (!repo.isEmpty())
        cmd += QStringLiteral(" (%1)").arg(repo);
    return cmd;
}

// Wait up to 8s for a git subprocess. On the GUI thread, poll in short slices
// and service the GUI between them so the window stays responsive and spinners
// animate; off-thread there is no window to keep painted (and pumping would
// drain the wrong event queue), so block as before. The pump used to be gated
// on a GitKeepAlive scope, but the stall log kept filling with >500ms freezes
// from unscoped call paths (agent-table refreshes, publish/mirror counts,
// branch lists, run-status handlers …), so every GUI-thread wait now pumps.
inline bool waitForGit(QProcess &process, QString *err)
{
    const QCoreApplication *app = QCoreApplication::instance();
    const bool onGuiThread = app && QThread::currentThread() == app->thread();

    // Announce the wait to the footer's background strip (adhoc #421). This is
    // the one chokepoint every git subprocess passes through, on the GUI thread
    // and off it, so a single ticket here is what makes "git" appear while a
    // slow fetch/clone/log runs. Fast reads never reach the strip's show delay,
    // so the hot path pays only an atomic increment.
    const forkmesh::BackgroundScope gitActivity(
        QStringLiteral("git"), gitBlockingCrumb(process),
        onGuiThread ? forkmesh::ActionTelemetry::Execution::UiBlocking
                    : forkmesh::ActionTelemetry::Execution::Worker);

    // Breadcrumb for the stall watchdog: if this synchronous wait freezes the GUI
    // thread, the stall report can name the git command instead of leaving only a
    // raw backtrace. Only the main thread is watched, so leave the breadcrumb alone
    // for off-thread reads rather than clobbering what the GUI thread set.
    std::optional<BlockingCallScope> crumb;
    if (onGuiThread)
        crumb.emplace(gitBlockingCrumb(process));

    if (!onGuiThread) {
        if (process.waitForFinished(8000))
            return true;
        process.kill();
        if (err)
            *err = QStringLiteral("git timed out");
        return false;
    }
    // A burst of individually fast (<40ms) git reads — refreshAgentTable shells two
    // per session, so a repo with many sessions runs dozens back-to-back — each
    // returns from waitForFinished(40) on the first poll, so the loop below never
    // pumps and the GUI (and the watchdog heartbeat) starves across the whole burst
    // even though no single call is slow. Pump up front when enough wall time has
    // elapsed since the last pump so the window keeps breathing between calls too.
    if (keepAliveClock().elapsed() - g_lastKeepAlivePumpMs >= 100)
        pumpKeepAlive();
    QElapsedTimer timer;
    timer.start();
    while (!process.waitForFinished(40)) {
        if (process.state() == QProcess::NotRunning)
            return true; // exited between polls; caller inspects the exit code
        if (timer.hasExpired(8000)) {
            process.kill();
            if (err)
                *err = QStringLiteral("git timed out");
            return false;
        }
        pumpKeepAlive();
    }
    return true;
}

// Give an *asynchronous* subprocess a background-strip ticket (adhoc #421):
// waitForGit only covers the blocking waits, so long-lived children started with
// start() and a finished() handler announce themselves here instead. The ticket
// is retired on finished() or on the QProcess's destruction, whichever comes
// first, so a killed or abandoned child can't strand a spinner.
inline void trackProcessActivity(QProcess *process, const QString &kind,
                                 const QString &detail = QString())
{
    if (!process)
        return;
    const quint64 id = forkmesh::BackgroundActivity::begin(kind, detail);
    auto retired = std::make_shared<bool>(false);
    auto retire = [id, retired](const QString &outcome) {
        if (*retired)
            return;
        *retired = true;
        forkmesh::BackgroundActivity::end(id, outcome);
    };
    QObject::connect(process, &QProcess::finished, process,
                     [retire](int code, QProcess::ExitStatus status) {
                         retire(status == QProcess::NormalExit && code == 0
                                    ? QStringLiteral("succeeded")
                                    : QStringLiteral("failed"));
                     });
    QObject::connect(process, &QObject::destroyed, process,
                     [retire](QObject *) {
                         retire(QStringLiteral("cancelled"));
                     });
}

// RAII: marks the run of synchronous git reads in an interactive load (a node
// switch or opening a repo). Nestable. waitForGit pumps on the GUI thread with
// or without this scope now; the marker is kept for the depth counter and as
// documentation that the enclosing flow expects pumped re-entrancy.
struct GitKeepAlive {
    GitKeepAlive() { ++g_gitKeepAliveDepth; }
    ~GitKeepAlive() { --g_gitKeepAliveDepth; }
    GitKeepAlive(const GitKeepAlive &) = delete;
    GitKeepAlive &operator=(const GitKeepAlive &) = delete;
};

// RAII: hold a bool true for the scope's lifetime. Used as a re-entrancy guard so
// a heavy slot serviced by GitKeepAlive's event-loop pump can't re-enter and stack
// its synchronous git work mid-flight.
struct ScopedFlag {
    bool &flag;
    explicit ScopedFlag(bool &f) : flag(f) { flag = true; }
    ~ScopedFlag() { flag = false; }
    ScopedFlag(const ScopedFlag &) = delete;
    ScopedFlag &operator=(const ScopedFlag &) = delete;
};

// Run a git command in `dir`, capturing stdout. Returns false (with stderr in
// `err`) on failure. Used by the in-client repo file browser.
inline bool runGitCapture(const QString &dir, const QStringList &args, QByteArray *out,
                   QString *err)
{
    QProcess process;
    process.start("git", QStringList{"-C", dir} + args);
    if (!waitForGit(process, err))
        return false;
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
        if (err)
            *err = QString::fromUtf8(process.readAllStandardError()).trimmed();
        return false;
    }
    if (out)
        *out = process.readAllStandardOutput();
    return true;
}

// Run a git command in `dir` feeding `input` on stdin (e.g. cat-file --batch),
// capturing stdout. Returns false (with stderr in `err`) on failure. QProcess
// buffers writes issued before the child has spawned, so no waitForStarted is
// needed.
inline bool runGitCaptureInput(const QString &dir, const QStringList &args,
                               const QByteArray &input, QByteArray *out,
                               QString *err)
{
    QProcess process;
    process.start("git", QStringList{"-C", dir} + args);
    process.write(input);
    process.closeWriteChannel();
    if (!waitForGit(process, err))
        return false;
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
        if (err)
            *err = QString::fromUtf8(process.readAllStandardError()).trimmed();
        return false;
    }
    if (out)
        *out = process.readAllStandardOutput();
    return true;
}

inline QString worktreeHeadBranch(const QString &workTree)
{
    if (workTree.trimmed().isEmpty() || !QDir(workTree).exists(QStringLiteral(".git")))
        return QString();
    QByteArray out;
    if (!runGitCapture(workTree,
                       {QStringLiteral("rev-parse"), QStringLiteral("--abbrev-ref"),
                        QStringLiteral("HEAD")},
                       &out, nullptr))
        return QString();
    const QString branch = QString::fromUtf8(out).trimmed();
    return branch == QLatin1String("HEAD") ? QString() : branch;
}

inline QString worktreeHeadCommit(const QString &workTree)
{
    if (workTree.trimmed().isEmpty() || !QDir(workTree).exists(QStringLiteral(".git")))
        return QString();
    QByteArray out;
    if (!runGitCapture(workTree,
                       {QStringLiteral("rev-parse"), QStringLiteral("--verify"),
                        QStringLiteral("HEAD")},
                       &out, nullptr))
        return QString();
    return QString::fromUtf8(out).trimmed();
}

inline QString worktreeBranchCommit(const QString &workTree, const QString &branch)
{
    if (workTree.trimmed().isEmpty() || branch.trimmed().isEmpty() ||
        !QDir(workTree).exists(QStringLiteral(".git")))
        return QString();
    QByteArray out;
    if (!runGitCapture(workTree,
                       {QStringLiteral("rev-parse"), QStringLiteral("--verify"),
                        branch + QStringLiteral("^{commit}")},
                       &out, nullptr))
        return QString();
    return QString::fromUtf8(out).trimmed();
}

// True when `workTree`'s issue metadata subtree has no uncommitted *tracked* changes —
// a clean base for the auto-issue-sync to land issue commits on (issue #193).
// Scoped to issue metadata (not the whole tree) because IssueStore::commit() only ever
// stages/commits that path: unrelated in-progress work elsewhere in the repo
// (which, on an actively developed source-of-truth checkout, is close to
// always) must not permanently block the auto-drain of mirror-filed issues.
// Untracked files (build output, scratch notes) are ignored: an issues-only
// commit never touches them. A failed status check is treated as "not clean"
// so we err on the side of leaving incoming issues in the inbox rather than
// committing.
inline bool worktreeTrackedClean(const QString &workTree)
{
    QByteArray status;
    if (!runGitCapture(workTree,
                       {"status", "--porcelain", "--untracked-files=no", "--",
                        ".forkmesh/issues"},
                       &status, nullptr))
        return false;
    return QString::fromUtf8(status).trimmed().isEmpty();
}

// Drop any other worktree currently holding `branch` checked out so this working
// tree can switch to it. Agent sessions run in a temp worktree under
// /tmp/forkmesh-worktrees/…; one left behind (an app restart skips its cleanup)
// keeps the branch reserved, so `git checkout <branch>` here fails with "is
// already used by worktree at …". Removing the worktree frees the branch while
// keeping its ref intact. Returns true if it released something so the caller
// can retry the checkout. (Mirrors releaseWorktreeHoldingBranch in PullStore.)
inline bool releaseWorktreeHoldingBranch(const QString &dir, const QString &branch)
{
    if (branch.trimmed().isEmpty())
        return false;
    QByteArray out;
    if (!runGitCapture(dir, {"worktree", "list", "--porcelain"}, &out, nullptr))
        return false;
    const QString want = QStringLiteral("refs/heads/%1").arg(branch);
    QString currentPath;
    QString held;
    const QList<QByteArray> lines = out.split('\n');
    for (const QByteArray &raw : lines) {
        const QString line = QString::fromUtf8(raw).trimmed();
        if (line.startsWith(QLatin1String("worktree ")))
            currentPath = line.mid(QStringLiteral("worktree ").size()).trimmed();
        else if (line.startsWith(QLatin1String("branch ")) &&
                 line.mid(QStringLiteral("branch ").size()).trimmed() == want &&
                 !currentPath.isEmpty() &&
                 QDir(currentPath).absolutePath() != QDir(dir).absolutePath()) {
            held = currentPath;
            break;
        }
    }
    if (held.isEmpty())
        return false;
    runGitCapture(dir, {"worktree", "remove", "--force", held}, nullptr, nullptr);
    QDir(held).removeRecursively();
    runGitCapture(dir, {"worktree", "prune"}, nullptr, nullptr);
    return true;
}

// Check out `branch` in `dir`, first clearing any leftover agent worktree that
// has it reserved (see releaseWorktreeHoldingBranch). On failure returns false
// with stderr in `err`; for the "already used by worktree" case that the auto
// release could not resolve, `err` is rewritten into a short why/how-to-fix the
// caller can show, instead of leaking git's raw "fatal: …" line.
inline bool checkoutReleasingWorktree(const QString &dir, const QString &branch,
                               QString *err)
{
    if (runGitCapture(dir, {"checkout", branch}, nullptr, err))
        return true;
    if (err && err->contains(QLatin1String("already used by worktree"))) {
        if (releaseWorktreeHoldingBranch(dir, branch) &&
            runGitCapture(dir, {"checkout", branch}, nullptr, err))
            return true;
        // Still held — name the offending worktree (git puts its path after
        // "worktree at ") and how to clear it.
        static const QString marker = QStringLiteral("worktree at ");
        const int at = err->indexOf(marker);
        const QString where =
            at >= 0 ? err->mid(at + marker.size()).trimmed() : QString();
        *err = where.isEmpty()
                   ? QStringLiteral("it is checked out in another worktree. Close "
                                    "that agent session (or remove that worktree), "
                                    "then try again.")
                   : QStringLiteral("it is checked out in the worktree at %1. Close "
                                    "that agent session (or run `git worktree "
                                    "remove --force` on it), then try again.")
                         .arg(where);
    }
    return false;
}

// Capture git's stdout regardless of exit code. Some diff commands exit non-zero
// when differences exist (`diff --no-index` returns 1), which runGitCapture
// treats as failure and discards the output we actually want.
inline QByteArray gitCaptureStdout(const QString &dir, const QStringList &args)
{
    QProcess process;
    process.start("git", QStringList{"-C", dir} + args);
    QString err;
    if (!waitForGit(process, &err))
        return QByteArray();
    return process.readAllStandardOutput();
}

inline bool runGitCaptureWithEnv(const QString &dir, const QStringList &args,
                          const QProcessEnvironment &env, QByteArray *out,
                          QString *err)
{
    QProcess process;
    process.setProcessEnvironment(env);
    process.start("git", QStringList{"-C", dir} + args);
    if (!waitForGit(process, err))
        return false;
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
        if (err)
            *err = QString::fromUtf8(process.readAllStandardError()).trimmed();
        return false;
    }
    if (out)
        *out = process.readAllStandardOutput();
    return true;
}

// Drop refs outside refs/heads/* and refs/tags/* from a bare mirror. Tool refs
// (refs/codex/*, refs/remotes/*, refs/pull/*, ...) churn on active repos; once
// advertised by our host they make peers' clones want refs that vanish, failing
// the whole upload-pack with "not our ref" (HTTP 502). Branches and tags carry
// everything we serve (issues and pulls live in refs/heads), so pruning the
// rest keeps the mirror serviceable. Best-effort: never blocks a sync.
inline void pruneNonStableMirrorRefs(const QString &mirrorPath)
{
    QByteArray out;
    if (!runGitCapture(mirrorPath, {"for-each-ref", "--format=%(refname)"}, &out,
                       nullptr))
        return;
    QByteArray deletions;
    for (const QByteArray &line : out.split('\n')) {
        const QByteArray ref = line.trimmed();
        if (ref.isEmpty() || ref.startsWith("refs/heads/") ||
            ref.startsWith("refs/tags/"))
            continue;
        deletions += "delete " + ref + "\n";
    }
    if (deletions.isEmpty())
        return;
    QProcess process;
    process.start("git", {"-C", mirrorPath, "update-ref", "--stdin"});
    if (!process.waitForStarted(4000))
        return;
    process.write(deletions);
    process.closeWriteChannel();
    process.waitForFinished(8000);
}

// Parse `git count-objects -v` output (loose-object + packed totals, both in
// KiB) into a byte count. Split out of mirrorRepoSizeBytes so an async caller
// (updateRepoCodeSize, via runGitDetached) can reuse the parsing without
// re-running the blocking runGitCapture path below.
inline qint64 parseCountObjectsSizeBytes(const QByteArray &countObjectsOutput)
{
    qint64 sizeKiB = 0;
    for (const QString &line :
         QString::fromUtf8(countObjectsOutput).split(QLatin1Char('\n'))) {
        const int colon = line.indexOf(QLatin1Char(':'));
        if (colon < 0)
            continue;
        const QString key = line.left(colon).trimmed();
        if (key == QLatin1String("size") || key == QLatin1String("size-pack"))
            sizeKiB += line.mid(colon + 1).trimmed().toLongLong();
    }
    return sizeKiB * 1024;
}

// On-disk size of a bare git mirror, in bytes: the loose-object total plus the
// packed total reported by `git count-objects -v` (both given in KiB). This is
// the storage a node spends mirroring the repo, and what it advertises to peers
// so the mirror-nodes view can show how much data each node is holding.
inline qint64 mirrorRepoSizeBytes(const QString &mirrorPath)
{
    if (mirrorPath.trimmed().isEmpty() || !QDir(mirrorPath).exists())
        return 0;
    QByteArray out;
    if (!runGitCapture(mirrorPath, {"count-objects", "-v"}, &out, nullptr))
        return 0;
    return parseCountObjectsSizeBytes(out);
}

// The content-addressed release store a node keeps alongside its bare mirror.
// Release binaries are never committed to git (issue #304); they live here as
// forkmesh-releases/sha256/<aa>/<full-hash>/data and are self-verifying (the
// path IS the sha256). Both the artifact tally and the mirror-side replicator
// resolve a blob's on-disk path through this one helper.
inline QString mirrorReleaseCasRoot(const QString &mirrorPath)
{
    return QDir(mirrorPath).filePath(QStringLiteral("forkmesh-releases/sha256"));
}
inline QString mirrorReleaseBlobPath(const QString &mirrorPath, const QString &hash)
{
    return QDir(mirrorReleaseCasRoot(mirrorPath))
        .filePath(QStringLiteral("%1/%2/data").arg(hash.left(2), hash));
}

// How many release artifact blobs this node is actually hosting for download —
// the files present in the mirror's content-addressed release store. A mirror
// replicates these separately from the git refs, so the figure shows how many
// artifacts a node can serve. Advertised to peers for the Mirror nodes view.
// Returns -1 when the mirror path can't be read, so "unknown" (older peer) stays
// distinct from a genuine zero; a store with no blobs yet counts as zero.
inline int releaseCasBlobCount(const QDir &casDir)
{
    if (!casDir.exists())
        return 0; // no artifacts stored yet
    int count = 0;
    // Two-level fanout: sha256/<aa>/<full-hash>/data.
    const QStringList shards =
        casDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QString &shard : shards) {
        const QDir shardDir(casDir.filePath(shard));
        const QStringList hashes =
            shardDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
        for (const QString &hash : hashes)
            if (QFile::exists(shardDir.filePath(hash + QStringLiteral("/data"))))
                ++count;
    }
    return count;
}
inline int mirrorArtifactCount(const QString &mirrorPath)
{
    if (mirrorPath.trimmed().isEmpty() || !QDir(mirrorPath).exists())
        return -1;
    return releaseCasBlobCount(QDir(mirrorReleaseCasRoot(mirrorPath)));
}
// The same tally for a working copy: the standalone release publisher's
// default CAS lives inside the checkout at .forkmesh/release-blobs (gitignored;
// see .forkmesh/release.yml), not at <mirror>/forkmesh-releases. Lets the
// source of truth — which may serve straight from its working copy with no
// bare mirror at all — still report the artifacts it hosts.
inline int checkoutArtifactCount(const QString &localPath)
{
    if (localPath.trimmed().isEmpty() || !QDir(localPath).exists())
        return -1;
    return releaseCasBlobCount(QDir(
        QDir(localPath).filePath(QStringLiteral(".forkmesh/release-blobs/sha256"))));
}

// One release artifact blob physically stored in a node's mirror CAS, resolved
// from its content-addressed path. Used by the Artifacts tab to list what a node
// is actually holding on disk (name/tag come from the release manifests).
struct MirrorReleaseBlob {
    QString hash; // the blob's sha256 (its own directory name / identity)
    QString path; // absolute path to the "data" file on disk
    qint64 size = 0; // byte size of the stored blob
};

// Every release artifact blob present in the mirror's content-addressed store,
// largest first. Walks the same forkmesh-releases/sha256/<aa>/<hash>/data fanout
// mirrorArtifactCount tallies, but returns each blob's on-disk size so the
// Artifacts tab can show what's using space and offer to delete it.
inline QList<MirrorReleaseBlob> mirrorReleaseBlobs(const QString &mirrorPath)
{
    QList<MirrorReleaseBlob> blobs;
    if (mirrorPath.trimmed().isEmpty() || !QDir(mirrorPath).exists())
        return blobs;
    const QDir casDir(mirrorReleaseCasRoot(mirrorPath));
    if (!casDir.exists())
        return blobs;
    const QStringList shards =
        casDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QString &shard : shards) {
        const QDir shardDir(casDir.filePath(shard));
        const QStringList hashes =
            shardDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
        for (const QString &hash : hashes) {
            const QString dataPath =
                shardDir.filePath(hash + QStringLiteral("/data"));
            const QFileInfo info(dataPath);
            if (!info.exists())
                continue;
            blobs.append({hash, info.absoluteFilePath(), info.size()});
        }
    }
    std::sort(blobs.begin(), blobs.end(),
              [](const MirrorReleaseBlob &a, const MirrorReleaseBlob &b) {
                  return a.size > b.size;
              });
    return blobs;
}

// Carry a node's release artifact store from a retiring mirror directory into
// the one that will serve the repository next. An encrypted repository is
// served out of a temporary materialization that every sealing pass replaces,
// and release binaries live beside the git data rather than in it (issue #304),
// so without this every artifact this node hosts is dropped the moment the old
// materialization is released — install.sh then 404s on a release it just
// published. Blobs are content-addressed and immutable, so a hard link is
// enough (and costs nothing); copying is only the cross-device fallback.
// Returns the number of blobs carried over.
inline int carryMirrorReleaseCas(const QString &fromMirror,
                                 const QString &toMirror)
{
    if (fromMirror.trimmed().isEmpty() || toMirror.trimmed().isEmpty() ||
        QDir::cleanPath(fromMirror) == QDir::cleanPath(toMirror) ||
        !QDir(toMirror).exists())
        return 0;
    int carried = 0;
    for (const MirrorReleaseBlob &blob : mirrorReleaseBlobs(fromMirror)) {
        const QString destination = mirrorReleaseBlobPath(toMirror, blob.hash);
        if (QFile::exists(destination))
            continue;
        if (!QDir().mkpath(QFileInfo(destination).absolutePath()))
            continue;
        bool linked = false;
#ifndef Q_OS_WIN
        linked = ::link(QFile::encodeName(blob.path).constData(),
                        QFile::encodeName(destination).constData()) == 0;
#endif
        if (linked || QFile::copy(blob.path, destination))
            ++carried;
    }
    return carried;
}

// How many numbered subdirectories a node's bare mirror holds under <subdir>/ on
// the served branch (the same tally the issues / pulls / discussions tabs show).
// Advertised to peers so the mirror-nodes view can show what each node is
// mirroring. Returns -1 when the mirror/branch can't be read at all, so "unknown"
// stays distinct from a genuine zero; a missing <subdir>/ folder counts as zero.
inline int mirrorNumberedDirCount(const QString &mirrorPath, const QString &branch,
                                  const QString &subdir)
{
    if (mirrorPath.trimmed().isEmpty() || branch.isEmpty() ||
        !QDir(mirrorPath).exists())
        return -1;
    QByteArray out;
    if (!runGitCapture(mirrorPath, {"ls-tree", "-z", branch + ":" + subdir}, &out,
                       nullptr))
        return 0; // no <subdir>/ folder yet -> nothing filed
    static const QRegularExpression numericName(QStringLiteral("^[0-9]+$"));
    int count = 0;
    for (const QByteArray &record : out.split('\0')) {
        if (record.isEmpty())
            continue;
        const int tab = record.indexOf('\t');
        if (tab < 0)
            continue;
        const QList<QByteArray> meta = record.left(tab).simplified().split(' ');
        if (meta.size() < 2 || meta.at(1) != "tree")
            continue;
        if (numericName.match(QString::fromUtf8(record.mid(tab + 1))).hasMatch())
            ++count;
    }
    return count;
}

// Highest numbered subdir under a metadata folder on the served branch, i.e.
// the largest issue/PR number ever assigned (closed and deleted ones still
// occupy their slot on disk). Returns 0 when the folder is empty/absent. This
// is the anchor the relay uses to propose the SAME next number the desktop's
// nextNumber() (= max + 1) would — so a ForkBot-filed issue can be given its
// real number immediately (see _forkbot_next_issue_number in the worker).
inline int mirrorNumberedDirMax(const QString &mirrorPath, const QString &branch,
                                const QString &subdir)
{
    if (mirrorPath.trimmed().isEmpty() || branch.isEmpty() ||
        !QDir(mirrorPath).exists())
        return 0;
    QByteArray out;
    if (!runGitCapture(mirrorPath, {"ls-tree", "-z", branch + ":" + subdir}, &out,
                       nullptr))
        return 0; // no <subdir>/ folder yet -> nothing filed
    static const QRegularExpression numericName(QStringLiteral("^[0-9]+$"));
    int maxNumber = 0;
    for (const QByteArray &record : out.split('\0')) {
        if (record.isEmpty())
            continue;
        const int tab = record.indexOf('\t');
        if (tab < 0)
            continue;
        const QList<QByteArray> meta = record.left(tab).simplified().split(' ');
        if (meta.size() < 2 || meta.at(1) != "tree")
            continue;
        const QString name = QString::fromUtf8(record.mid(tab + 1));
        if (!numericName.match(name).hasMatch())
            continue;
        maxNumber = qMax(maxNumber, name.toInt());
    }
    return maxNumber;
}

// Stable identity used to group a roster member into one row of the Nodes list
// (and to look its telemetry back up). A headless mirror node often shares — or
// omits — the chat display name of the account that owns it, which collapsed
// several distinct mirror nodes into a single row: mirror2/mirror3 vanished from
// the Nodes list even though the per-repo Mirror nodes view (which keys on the
// advert / nodeName via displayNodeName) listed them correctly. Prefer the
// registered nodeName so each physical node keeps its own row — self included:
// our own frame now advertises machineNodeName() (never the username), so the
// self row reads as the machine it is rather than as the user.
inline QString nodeListIdentityKey(const MemberInfo &m)
{
    const QString nodeName = m.nodeName.trimmed();
    if (!nodeName.isEmpty())
        return nodeName;
    return m.name.trimmed();
}

// A temporary world/website chat visitor — a human passing through the public
// room, never a serving node — so every node surface (Nodes directory, top-bar
// node dropdown, relay nodes dialog) must skip them (adhoc #308: "World Guest
// fb9d" rows in the Nodes list). The web chat stamps these accountKind "guest";
// frames sent before that stamp existed are recognised by the placeholder names
// the world assigns ("World visitor · <name>", "World Guest ab12", "Guest 1234")
// — but only when the peer never advertised a node identity, so a real node
// keeps its row. That nodeName escape now matters for "guest" too: a fresh
// desktop install chats as "Guest ####" until someone picks a username (adhoc
// #113), yet the machine itself still advertises its generated node name and
// belongs in node surfaces. Browser-tab guests advertise no nodeName and stay
// filtered.
inline bool isTemporaryChatGuest(const MemberInfo &m)
{
    const QString kind = m.accountKind.trimmed().toLower();
    if (kind == QLatin1String("guest"))
        return m.nodeName.trimmed().isEmpty();
    if (!kind.isEmpty() || !m.nodeName.trimmed().isEmpty())
        return false;
    static const QRegularExpression legacyGuestName(
        QString::fromUtf8("^(?:world visitor\\s*\xC2\xB7.*|world guest\\s+\\S+|"
                          "guest\\s+\\d+)$"),
        QRegularExpression::CaseInsensitiveOption);
    return legacyGuestName.match(m.name.trimmed()).hasMatch();
}

// Open issue count for the advertised catalog issueCount. Closed issues keep
// their .forkmesh/issues/<n>/ directory on disk, so a bare directory count
// (mirrorNumberedDirCount) overstates the open total the website badges the
// Issues tab with — the badge listed all issues, open and closed (adhoc #29,
// following issue #397 which only fixed the live-served tree counts). Reads
// each record's top-level status and counts anything not "closed" (open,
// reopened, or unreadable) as open, matching RepoHost::countOpenIssues and the
// web's parseIssueJson default. Returns -1 when the mirror/branch can't be read
// (advertised as "unknown"), 0 when no issues have been filed.
inline int mirrorOpenIssueCount(const QString &mirrorPath, const QString &branch)
{
    if (mirrorPath.trimmed().isEmpty() || branch.isEmpty() ||
        !QDir(mirrorPath).exists())
        return -1;
    // The count only changes when the served branch tip moves, but the publish
    // and mirror-advert timers recompute it over and over. Reading one status
    // blob per issue also used to spawn one `git cat-file -p` per issue — a
    // repo with ~200 issues ran 200+ sequential subprocesses on the GUI thread
    // and the stall watchdog clocked individual publishes at 1.8s+ (adhoc #33).
    // Cache per mirror+branch keyed on the tip commit, and on a miss read every
    // status through a single `git cat-file --batch` process. Advert snapshots
    // are gathered on a worker, while explicit publishes can still request the
    // count elsewhere, so protect the process-wide cache.
    QByteArray tip;
    runGitCapture(mirrorPath, {"rev-parse", "--verify", branch}, &tip, nullptr);
    tip = tip.trimmed();
    struct OpenIssueCacheEntry {
        QByteArray tip;
        int count = 0;
    };
    static QHash<QString, OpenIssueCacheEntry> cache;
    static QMutex cacheMutex;
    const QString cacheKey = mirrorPath + QLatin1Char('\n') + branch;
    if (!tip.isEmpty()) {
        QMutexLocker lock(&cacheMutex);
        const auto cached = cache.constFind(cacheKey);
        if (cached != cache.constEnd() && cached->tip == tip)
            return cached->count;
    }
    static const QRegularExpression numericName(QStringLiteral("^[0-9]+$"));
    // Numeric child folders of one tree on the served branch; empty when the
    // folder is absent.
    auto numberedNames = [&](const QString &path) {
        QStringList found;
        QByteArray out;
        if (!runGitCapture(mirrorPath, {"ls-tree", "-z", branch + ":" + path},
                           &out, nullptr))
            return found;
        for (const QByteArray &record : out.split('\0')) {
            if (record.isEmpty())
                continue;
            const int tab = record.indexOf('\t');
            if (tab < 0)
                continue;
            const QList<QByteArray> meta = record.left(tab).simplified().split(' ');
            if (meta.size() < 2 || meta.at(1) != "tree")
                continue;
            const QString name = QString::fromUtf8(record.mid(tab + 1));
            if (numericName.match(name).hasMatch())
                found.append(name);
        }
        return found;
    };
    // Post-split layout (adhoc #14): the folder IS the status — open/<n>
    // counts as open, closed/<n> as closed. An issue its own creator deleted is
    // not open — the Issues tab and lists drop it (Issue::isDeleted), so the
    // advertised count must too, or it drifts above the tab (adhoc #16). A
    // delete/self event from anyone else is an unauthorized attempt that still
    // counts. Closed/ folders are never open regardless.
    auto recordTombstoned = [](const QByteArray &blob) {
        const QJsonArray events = QJsonDocument::fromJson(blob)
                                      .object()
                                      .value(QStringLiteral("events"))
                                      .toArray();
        QString creator;
        for (const QJsonValue &value : events) {
            const QJsonObject event = value.toObject();
            if (event.value(QStringLiteral("type")).toString() ==
                QLatin1String("open")) {
                creator = event.value(QStringLiteral("author")).toString();
                break;
            }
        }
        for (const QJsonValue &value : events) {
            const QJsonObject event = value.toObject();
            if (event.value(QStringLiteral("type")).toString() ==
                    QLatin1String("delete") &&
                event.value(QStringLiteral("target")).toString() ==
                    QLatin1String("self") &&
                !creator.isEmpty() &&
                event.value(QStringLiteral("author")).toString() == creator)
                return true;
        }
        return false;
    };
    QSet<QString> counted;
    int open = 0;
    const QStringList openNames =
        numberedNames(QStringLiteral(".forkmesh/issues/open"));
    for (const QString &name : openNames)
        counted.insert(name);
    if (!openNames.isEmpty()) {
        QByteArray batchIn;
        for (const QString &name : openNames) {
            batchIn +=
                (branch +
                 QStringLiteral(":.forkmesh/issues/open/%1/issue-%1.json")
                     .arg(name))
                    .toUtf8() +
                '\n';
        }
        QByteArray batchOut;
        int remaining = openNames.size();
        if (runGitCaptureInput(mirrorPath, {"cat-file", "--batch"}, batchIn,
                               &batchOut, nullptr)) {
            int pos = 0;
            while (remaining > 0 && pos < batchOut.size()) {
                const int eol = batchOut.indexOf('\n', pos);
                if (eol < 0)
                    break;
                const QByteArray header = batchOut.mid(pos, eol - pos);
                pos = eol + 1;
                --remaining;
                const QList<QByteArray> parts = header.split(' ');
                bool sizeOk = false;
                const qlonglong size =
                    parts.size() >= 3 ? parts.at(2).toLongLong(&sizeOk) : 0;
                if (!sizeOk || size < 0 || pos + size > batchOut.size()) {
                    ++open; // unreadable records remain live, matching loadAll
                    continue;
                }
                const QByteArray blob = batchOut.mid(pos, size);
                pos += size + 1; // skip the record's trailing LF
                if (!recordTombstoned(blob))
                    ++open;
            }
        }
        open += remaining;
    }
    for (const QString &name :
         numberedNames(QStringLiteral(".forkmesh/issues/closed")))
        counted.insert(name);
    // Pre-split legacy folders (numbered dirs directly under the root) still
    // carry the status only inside the record; batch-read those as before.
    QStringList names;
    for (const QString &name : numberedNames(QStringLiteral(".forkmesh/issues")))
        if (!counted.contains(name))
            names.append(name);
    if (!names.isEmpty()) {
        QByteArray batchIn;
        for (const QString &name : std::as_const(names))
            batchIn += (branch + QStringLiteral(":.forkmesh/issues/%1/issue-%1.json")
                                     .arg(name))
                           .toUtf8() +
                       '\n';
        QByteArray batchOut;
        // Anything not readable as status "closed" (open, reopened, missing or
        // malformed record) counts as open — matching RepoHost::countOpenIssues
        // and the web's parseIssueJson default.
        int remaining = names.size();
        if (runGitCaptureInput(mirrorPath, {"cat-file", "--batch"}, batchIn,
                               &batchOut, nullptr)) {
            int pos = 0;
            while (remaining > 0 && pos < batchOut.size()) {
                const int eol = batchOut.indexOf('\n', pos);
                if (eol < 0)
                    break;
                const QByteArray header = batchOut.mid(pos, eol - pos);
                pos = eol + 1;
                --remaining;
                const QList<QByteArray> parts = header.split(' ');
                bool sizeOk = false;
                const qlonglong size =
                    parts.size() >= 3 ? parts.at(2).toLongLong(&sizeOk) : 0;
                if (!sizeOk || size < 0) {
                    ++open; // "<path> missing" or unparsable header
                    continue;
                }
                const QString status =
                    QJsonDocument::fromJson(batchOut.mid(pos, size))
                        .object()
                        .value(QStringLiteral("status"))
                        .toString();
                pos += size + 1; // skip the record's trailing LF
                if (status != QLatin1String("closed"))
                    ++open;
            }
        }
        open += remaining; // records the batch never answered default to open
    }
    if (!tip.isEmpty()) {
        QMutexLocker lock(&cacheMutex);
        cache.insert(cacheKey, {tip, open});
    }
    return open;
}

// Issues / pull requests / discussions a node's mirror holds. Issues report the
// OPEN count (the catalog's documented contract; see catalog.py); pulls and
// discussions are the count of numbered subdirs under their metadata folder on
// the served branch.
inline int mirrorIssueCount(const QString &mirrorPath, const QString &branch)
{
    return mirrorOpenIssueCount(mirrorPath, branch);
}
inline int mirrorIssueMaxNumber(const QString &mirrorPath, const QString &branch)
{
    // Issues are split into open/ and closed/ status folders (adhoc #14);
    // pre-split mirrors keep numbered dirs directly under the root. The max
    // spans all three.
    return qMax(mirrorNumberedDirMax(mirrorPath, branch,
                                     QStringLiteral(".forkmesh/issues")),
                qMax(mirrorNumberedDirMax(
                         mirrorPath, branch,
                         QStringLiteral(".forkmesh/issues/open")),
                     mirrorNumberedDirMax(
                         mirrorPath, branch,
                         QStringLiteral(".forkmesh/issues/closed"))));
}
inline int mirrorPullCount(const QString &mirrorPath, const QString &branch)
{
    // Pulls live on the dedicated forkmesh/pulls metadata branch (issue #399),
    // not the served head branch; count there when it exists, falling back to
    // the head branch for pre-#399 mirrors that never migrated.
    if (!mirrorPath.trimmed().isEmpty() && QDir(mirrorPath).exists() &&
        runGitCapture(mirrorPath,
                      {"rev-parse", "--verify", "-q",
                       QStringLiteral("refs/heads/forkmesh/pulls^{commit}")},
                      nullptr, nullptr))
        return mirrorNumberedDirCount(
            mirrorPath, QStringLiteral("forkmesh/pulls"), QStringLiteral("pulls"));
    return mirrorNumberedDirCount(mirrorPath, branch, QStringLiteral("pulls"));
}
inline int mirrorDiscussionCount(const QString &mirrorPath, const QString &branch)
{
    return mirrorNumberedDirCount(mirrorPath, branch,
                                  QStringLiteral(".forkmesh/discussions"));
}

// How many commits are reachable on the node's served branch (`git rev-list
// --count`). Advertised so the mirror-nodes view can show each node's history
// depth. Returns -1 when the mirror/branch can't be read.
inline int mirrorCommitCount(const QString &mirrorPath, const QString &branch)
{
    if (mirrorPath.trimmed().isEmpty() || branch.isEmpty() ||
        !QDir(mirrorPath).exists())
        return -1;
    QByteArray out;
    if (!runGitCapture(mirrorPath, {"rev-list", "--count", branch}, &out, nullptr))
        return -1;
    bool ok = false;
    const int n = QString::fromUtf8(out).trimmed().toInt(&ok);
    return ok ? n : -1;
}

// Subject / author / commit time of one commit, read from a git directory (a
// bare mirror or a working copy). Returns an empty identity when the repo or
// the commit isn't there — a peer can advertise a commit we never fetched.
inline CommitIdentity gitCommitIdentity(const QString &gitPath,
                                        const QString &commit)
{
    CommitIdentity identity;
    if (gitPath.trimmed().isEmpty() || commit.trimmed().isEmpty() ||
        !QDir(gitPath).exists())
        return identity;
    QByteArray out;
    if (!runGitCapture(gitPath,
                       {"show", "-s", "--format=%s%n%an%n%ct", commit}, &out,
                       nullptr))
        return identity;
    const QStringList lines = QString::fromUtf8(out).split('\n');
    if (lines.size() < 3)
        return identity;
    identity.subject = lines.at(0).trimmed().left(kMaxCommitSubjectChars);
    identity.author = lines.at(1).trimmed().left(kMaxCommitAuthorChars);
    identity.committedAtMs =
        qMax(qint64(0), lines.at(2).trimmed().toLongLong() * 1000);
    return identity;
}

// The same lookup across a node's two copies: prefer the served bare mirror,
// falling back to the working copy for a source node whose primary-branch tip
// is ahead of the mirror it serves.
inline CommitIdentity mirrorCommitIdentity(const QString &mirrorPath,
                                           const QString &workTreePath,
                                           const QString &commit)
{
    CommitIdentity identity = gitCommitIdentity(mirrorPath, commit);
    if (identity.subject.isEmpty() && identity.author.isEmpty())
        identity = gitCommitIdentity(workTreePath, commit);
    return identity;
}

// Commit activity histogram for the website repository list: 52 weekly buckets,
// oldest to newest, across every served ref in the bare mirror.
inline QJsonArray mirrorCommitActivityWeeks(const QString &mirrorPath,
                                            const QString &branch)
{
    constexpr int kWeeks = 52;
    constexpr qint64 kWeekSeconds = 7LL * 24LL * 60LL * 60LL;
    QVector<int> buckets(kWeeks, 0);
    auto toArray = [&buckets]() {
        QJsonArray arr;
        for (int n : buckets)
            arr.append(n);
        return arr;
    };

    if (mirrorPath.trimmed().isEmpty() || branch.trimmed().isEmpty() ||
        !QDir(mirrorPath).exists())
        return toArray();
    QByteArray out;
    if (!runGitCapture(mirrorPath,
                       {"log", "--since=52 weeks ago", "--format=%ct", branch},
                       &out, nullptr))
        return toArray();

    const qint64 now = QDateTime::currentSecsSinceEpoch();
    const qint64 start = now - (qint64(kWeeks) * kWeekSeconds);
    for (const QByteArray &line : out.split('\n')) {
        bool ok = false;
        const qint64 ts = QString::fromUtf8(line).trimmed().toLongLong(&ok);
        if (!ok)
            continue;
        const int idx =
            qBound(0, int((ts - start) / kWeekSeconds), kWeeks - 1);
        buckets[idx] += 1;
    }
    return toArray();
}

// How many local branches (refs/heads/*) the node's bare mirror holds. Returns
// -1 when the mirror can't be read.
inline int mirrorBranchCount(const QString &mirrorPath)
{
    if (mirrorPath.trimmed().isEmpty() || !QDir(mirrorPath).exists())
        return -1;
    QByteArray out;
    if (!runGitCapture(mirrorPath,
                       {"for-each-ref", "--format=%(refname)", "refs/heads/"}, &out,
                       nullptr))
        return -1;
    int count = 0;
    for (const QByteArray &line : out.split('\n'))
        if (!line.trimmed().isEmpty())
            ++count;
    return count;
}

// How many git worktrees this node's working copy has checked out (`git worktree
// list`), including the main checkout — in ForkMesh each extra worktree is a live
// agent task, so the figure shows how busy the node is. Only meaningful for a node
// that holds a working copy; pass its localPath (empty/mirror-only -> -1 unknown).
inline int mirrorWorktreeCount(const QString &localPath)
{
    if (localPath.trimmed().isEmpty() || !QDir(localPath).exists())
        return -1;
    QByteArray out;
    if (!runGitCapture(localPath, {"worktree", "list", "--porcelain"}, &out,
                       nullptr))
        return -1;
    int count = 0;
    for (const QByteArray &line : out.split('\n'))
        if (line.startsWith("worktree "))
            ++count;
    return count;
}

inline bool buildWorkingTreeDiff(const QString &dir, const QString &base, QByteArray *out,
                          QString *err)
{
    QTemporaryDir temp;
    if (!temp.isValid()) {
        if (err)
            *err = QStringLiteral("Could not create a temporary Git index.");
        return false;
    }

    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert(QStringLiteral("GIT_INDEX_FILE"), temp.path() + QStringLiteral("/index"));

    QByteArray ignored;
    auto stageSnapshot = [&] {
        if (!runGitCaptureWithEnv(dir, {"read-tree", base}, env, &ignored, err))
            return false;
        return runGitCaptureWithEnv(dir, {"add", "-A", "--", "."}, env,
                                    &ignored, err);
    };
    if (!stageSnapshot()) {
        // Agent worktrees change while they are being reviewed. If a file is
        // deleted between Git's directory scan and stat call, `git add -A` can
        // transiently fail with "unable to stat … No such file" even though a
        // deletion is a perfectly valid diff. Rebuild the temporary index and
        // take one fresh snapshot; the real worktree/index remain untouched.
        const QString firstError = err ? *err : QString();
        const bool racedDeletion =
            firstError.contains(QStringLiteral("unable to stat"),
                                Qt::CaseInsensitive) ||
            firstError.contains(QStringLiteral("No such file"),
                                Qt::CaseInsensitive);
        if (!racedDeletion || !stageSnapshot())
            return false;
    }
    return runGitCaptureWithEnv(dir, {"diff", "--binary", "--cached", base}, env, out, err);
}

// Readable text color (black or white) for a label pill's background.
inline QString pillTextColor(const QString &backgroundHex)
{
    const QColor c(backgroundHex);
    const double luminance =
        0.299 * c.red() + 0.587 * c.green() + 0.114 * c.blue();
    return luminance > 150 ? QStringLiteral("#1f2328") : QStringLiteral("#ffffff");
}

// A http(s) favicon URL derived from a ws(s) mainnode URL.
inline QUrl faviconUrl(const QString &serverUrl)
{
    const QUrl url(serverUrl);
    if (url.host().isEmpty())
        return {};
    QUrl out;
    out.setScheme(url.scheme() == QStringLiteral("ws") ? QStringLiteral("http")
                                                       : QStringLiteral("https"));
    out.setHost(url.host());
    if (url.port() > 0)
        out.setPort(url.port());
    out.setPath(QStringLiteral("/favicon.ico"));
    return out;
}

inline QString faviconCacheDir()
{
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) +
           "/favicons";
}

inline QString faviconCachePath(const QString &host)
{
    QString safe = host;
    safe.replace(QRegularExpression("[^a-zA-Z0-9._-]"), "_");
    return faviconCacheDir() + "/" + safe + ".png";
}

// A circular fallback badge showing the first letter of the host, used until a
// real favicon is fetched (or when the server has none).
// Clip a pixmap into a rounded-rectangle (Discord/GitHub-style "squircle"),
// scaling to fill and centering. Used so all server favicons render as rounded
// rects rather than circles.
inline QPixmap roundedRectPixmap(const QPixmap &src, int side, qreal radius)
{
    const qreal dpr = iconDevicePixelRatio();
    QPixmap out = crispIconPixmap(side, dpr);
    if (src.isNull())
        return out;
    QPainter p(&out);
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    QPainterPath path;
    path.addRoundedRect(QRectF(0, 0, side, side), radius, radius);
    p.setClipPath(path);
    QPixmap scaled = src.scaled(out.width(), out.height(),
                                Qt::KeepAspectRatioByExpanding,
                                Qt::SmoothTransformation);
    scaled.setDevicePixelRatio(dpr);
    const QSizeF ls = scaled.deviceIndependentSize();
    p.drawPixmap(QPointF((side - ls.width()) / 2.0, (side - ls.height()) / 2.0),
                 scaled);
    return out;
}

// Wide variant of roundedRectPixmap: scales `src` to cover a w*h banner
// (center-cropped, no distortion) and clips it to rounded corners. Used for the
// full-width avatar header at the top of the node profile panel.
inline QPixmap roundedBannerPixmap(const QPixmap &src, int w, int h, qreal radius)
{
    const qreal dpr = iconDevicePixelRatio();
    QPixmap out = crispIconPixmap(w, h, dpr);
    if (src.isNull() || w <= 0 || h <= 0)
        return out;
    QPainter p(&out);
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    QPainterPath path;
    path.addRoundedRect(QRectF(0, 0, w, h), radius, radius);
    p.setClipPath(path);
    QPixmap scaled = src.scaled(out.width(), out.height(),
                                Qt::KeepAspectRatioByExpanding,
                                Qt::SmoothTransformation);
    scaled.setDevicePixelRatio(dpr);
    const QSizeF ls = scaled.deviceIndependentSize();
    p.drawPixmap(QPointF((w - ls.width()) / 2.0, (h - ls.height()) / 2.0), scaled);
    return out;
}

// Proportional, correctly-colored language bar. `langs` must be sorted by size
// descending; the first `shown` entries are drawn as contiguous segments whose
// widths reflect their share of `total`, over a muted track. Painted onto a wide
// pixmap and stretched by the label (scaledContents), so segment proportions
// stay exact at any width — the old block-glyph span approach rendered every
// segment in the label's foreground color, so the language colors never showed.
inline QPixmap languageBarPixmap(const QList<QPair<QString, qint64>> &langs,
                          qint64 total, int shown, int w, int h)
{
    QPixmap out = crispIconPixmap(w, h, iconDevicePixelRatio());
    if (total <= 0 || shown <= 0 || w <= 0 || h <= 0)
        return out;
    QPainter p(&out);
    p.setRenderHint(QPainter::Antialiasing);
    QPainterPath clip;
    clip.addRoundedRect(QRectF(0, 0, w, h), h / 2.0, h / 2.0);
    p.setClipPath(clip);
    p.fillRect(QRectF(0, 0, w, h), QColor("#30363d")); // track behind segments
    double x = 0;
    for (int i = 0; i < shown && i < langs.size(); ++i) {
        const double seg = double(langs.at(i).second) / total * w;
        // +1 so adjacent segments overlap by a hair and leave no seam line.
        p.fillRect(QRectF(x, 0, seg + 1, h),
                   QColor(languageColor(langs.at(i).first)));
        x += seg;
    }
    return out;
}

inline QPixmap letterFavicon(const QString &host, int side = 36)
{
    QPixmap pixmap = crispIconPixmap(side, iconDevicePixelRatio());
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    const uint hash = qHash(host);
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(Theme::kSenderPalette[hash % Theme::kSenderPaletteSize]));
    painter.drawRoundedRect(QRectF(0, 0, side, side), side * 0.25, side * 0.25);
    const QChar letter = host.isEmpty() ? QChar('?') : host.at(0).toUpper();
    QFont font = painter.font();
    font.setPixelSize(qMax(8, qRound(side * 0.5)));
    font.setBold(true);
    painter.setFont(font);
    painter.setPen(QColor("#0f172a"));
    painter.drawText(QRect(0, 0, side, side), Qt::AlignCenter, QString(letter));
    return pixmap;
}

// Hosts that get a hardcoded, locally drawn icon instead of a /favicon.ico
// fetch. API endpoints do not serve favicons, so their request lines must not
// cause an additional failed favicon request in the network log. Returns an
// empty string for hosts with no builtin mark.
inline QString builtinFaviconKey(const QString &host)
{
    const QString h = host.toLower();
    if (h == QStringLiteral("anthropic.com") || h.endsWith(".anthropic.com") ||
        h == QStringLiteral("claude.ai") || h.endsWith(".claude.ai"))
        return QStringLiteral("anthropic");
    if (h == QStringLiteral("api.mainnet-beta.solana.com"))
        return QStringLiteral("solana");
    return {};
}

inline bool hasBuiltinFavicon(const QString &host)
{
    return !builtinFaviconKey(host).isEmpty();
}

// The hardcoded mark for a builtin host, drawn at `side` px as the same rounded
// rect the fetched favicons are clipped to. Null pixmap when the host has none.
inline QPixmap builtinFavicon(const QString &host, int side = 36)
{
    const QString key = builtinFaviconKey(host);
    if (key.isEmpty() || side <= 0)
        return {};
    QPixmap pixmap = crispIconPixmap(side, iconDevicePixelRatio());
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(Qt::NoPen);
    painter.setBrush(key == QStringLiteral("solana")
                         ? QColor("#17172e") // Solana navy
                         : QColor("#d97757")); // Anthropic clay
    painter.drawRoundedRect(QRectF(0, 0, side, side), side / 4.0, side / 4.0);
    if (key == QStringLiteral("solana")) {
        QPen stroke(QColor("#14f195"));
        stroke.setWidthF(qMax(1.0, side * 0.11));
        stroke.setCapStyle(Qt::RoundCap);
        painter.setPen(stroke);
        const qreal inset = side * 0.25;
        const qreal width = side * 0.5;
        for (const qreal y : {side * 0.32, side * 0.50, side * 0.68})
            painter.drawLine(QPointF(inset, y), QPointF(inset + width, y));
        return pixmap;
    }
    // Burst mark: rounded strokes radiating from the centre.
    QPen stroke(QColor("#ffffff"));
    stroke.setWidthF(qMax(1.0, side * 0.09));
    stroke.setCapStyle(Qt::RoundCap);
    painter.setPen(stroke);
    const QPointF center(side / 2.0, side / 2.0);
    const qreal radius = side * 0.28;
    for (int i = 0; i < 6; ++i) {
        const qreal angle = qDegreesToRadians(qreal(i) * 30.0);
        const QPointF arm(radius * std::cos(angle), radius * std::sin(angle));
        painter.drawLine(center - arm, center + arm);
    }
    return pixmap;
}

#if defined(Q_OS_WIN)
const QString kWinRunKey =
    QStringLiteral("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run");
#endif

inline QString autostartDesktopPath()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) +
           "/autostart/forkmesh.desktop";
}

#if defined(Q_OS_MACOS)
inline QString launchAgentPath()
{
    return QDir::homePath() + "/Library/LaunchAgents/com.forkmesh.app.plist";
}
#endif

inline bool isAutostartEnabled()
{
#if defined(Q_OS_WIN)
    QSettings run(kWinRunKey, QSettings::NativeFormat);
    return run.contains("ForkMesh");
#elif defined(Q_OS_MACOS)
    return QFileInfo::exists(launchAgentPath());
#else
    return QFileInfo::exists(autostartDesktopPath());
#endif
}

// Short name of the OS mechanism used to launch ForkMesh at login. Shown in
// Settings so the user can see how autostart is wired, not just that it is on.
inline QString autostartMechanismName()
{
#if defined(Q_OS_WIN)
    return QStringLiteral("Windows registry Run key");
#elif defined(Q_OS_MACOS)
    return QStringLiteral("macOS LaunchAgent");
#else
    return QStringLiteral("XDG autostart entry");
#endif
}

// The exact on-disk file (or registry key) that makes ForkMesh start at login.
// This is what the "Remove auto startup" button deletes. Shown in Settings so
// a stale entry left by an installer or an older build is visible and findable.
inline QString autostartLocation()
{
#if defined(Q_OS_WIN)
    return kWinRunKey + QStringLiteral("\\ForkMesh");
#elif defined(Q_OS_MACOS)
    return QDir::toNativeSeparators(launchAgentPath());
#else
    return QDir::toNativeSeparators(autostartDesktopPath());
#endif
}

// Enable/disable launching ForkMesh at login. Returns true on success. The old
// version silently ignored a failed remove(): if the autostart entry couldn't
// be deleted (e.g. a root-owned file left by the curl installer) the checkbox
// looked off but ForkMesh kept starting at login (issue #393). The bool lets
// the UI re-read the real state and warn instead of lying.
inline bool setAutostartEnabled(bool enabled)
{
    const QString exe = QCoreApplication::applicationFilePath();
#if defined(Q_OS_WIN)
    QSettings run(kWinRunKey, QSettings::NativeFormat);
    if (enabled)
        run.setValue("ForkMesh", QDir::toNativeSeparators(exe));
    else
        run.remove("ForkMesh");
    run.sync();
    return run.status() == QSettings::NoError &&
           run.contains("ForkMesh") == enabled;
#elif defined(Q_OS_MACOS)
    const QString path = launchAgentPath();
    if (!enabled) {
        // Treat "already gone" as success; only a real deletion failure counts.
        return !QFileInfo::exists(path) || QFile::remove(path);
    }
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;
    const QString plist = QStringLiteral(
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        "<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" "
        "\"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">\n"
        "<plist version=\"1.0\"><dict>\n"
        "  <key>Label</key><string>com.forkmesh.app</string>\n"
        "  <key>ProgramArguments</key><array><string>%1</string></array>\n"
        "  <key>RunAtLoad</key><true/>\n"
        "</dict></plist>\n").arg(exe);
    return file.write(plist.toUtf8()) >= 0;
#else
    const QString path = autostartDesktopPath();
    if (!enabled) {
        // Treat "already gone" as success; only a real deletion failure counts.
        return !QFileInfo::exists(path) || QFile::remove(path);
    }
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;
    const QString desktop = QStringLiteral(
        "[Desktop Entry]\n"
        "Type=Application\n"
        "Name=ForkMesh\n"
        "Exec=%1\n"
        "Terminal=false\n"
        "X-GNOME-Autostart-enabled=true\n").arg(exe);
    return file.write(desktop.toUtf8()) >= 0;
#endif
}


} // namespace forkmesh::ui
