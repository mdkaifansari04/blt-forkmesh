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
#include "BackoffNetworkAccessManager.h"
#include "ClaudeAgentScript.h"
#include "ClaudeIdeBridge.h"
#include "ClaudeStreamSession.h"
#include "ClaudeTranscriptView.h"
#include "ScrollJumpButtons.h"
#include "CommitCommentStore.h"
#include "StallWatchdog.h"
#include "IssueBurnup.h"
#include "QrCode.h"
#include "ReferenceLinks.h"

#include "MarkdownEditor.h"
#include "MessageRow.h"
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
#include <QFontDatabase>
#include <QFormLayout>
#include <QFrame>
#include <QGraphicsOpacityEffect>
#include <QGridLayout>
#include <QGuiApplication>
#include <QStandardPaths>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QWidgetAction>
#include <QEnterEvent>
#include <QMessageBox>
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
#include <QStackedWidget>
#include <QStringListModel>
#include <QStyle>
#include <QStyledItemDelegate>
#include <QStyleHints>
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
#include <QWindow>
#include <QVBoxLayout>

#include <algorithm>
#include <functional>
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
bool autoMarkViewedOnScrollPref();
void setAutoMarkViewedOnScrollPref(bool on);
QString diffStickyStyleSheet(int fontPt);
QString diffStickyPathHtml(const QString &path);
QString agentCostText(double usd);
QString agentStatusText(const QString &status);
QColor agentStatusColor(const QString &status);
bool agentSessionActive(const AgentSession *s);
QString solanaDisplayCurrency();
QIcon agentStatusOcticon(const AgentSession &s, int px = 13);
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
                             m_ignoreScroll = true;
                             m_diff->scrollToAnchor(anchor);
                             m_ignoreScroll = false;
                             refresh(/*syncSelection=*/false);
                         });
    }

    // Recompute the file-header positions after the diff HTML was (re)rendered.
    // `files` is the same in-order list used to fill the file list, so anchors map
    // a span back to its row.
    void rebuild(const QList<DiffFileEntry> &files, int fontPt)
    {
        m_sticky->setStyleSheet(diffStickyStyleSheet(fontPt));
        m_spans.clear();
        QTextDocument *doc = m_diff->document();
        int idx = 0;
        for (QTextBlock b = doc->begin(); b.isValid() && idx < files.size();
             b = b.next()) {
            const int at = b.text().indexOf(files.at(idx).path);
            if (at >= 0) {
                m_spans.append({b.position() + at, files.at(idx).path,
                                files.at(idx).anchor});
                ++idx;
            }
        }
        refresh(/*syncSelection=*/false);
    }

private:
    struct Span {
        int pos;
        QString path;
        QString anchor;
    };

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
// Cadence on which a node re-fetches its mirrors from source (mirrors
// m_mirrorSyncTimer); a behind node is expected to catch up at the next tick.
constexpr qint64 kMirrorSyncIntervalMs = 5LL * 60 * 1000;
// Defined further down; used early by MirrorSyncDelegate to pick chart colors.
bool currentThemeIsDark();

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

// URL scheme for the clickable worktree-location link in the agent session
// header; the percent-encoded branch name follows. Clicking it opens that
// branch's row in the Worktrees tab (issue #265). Shared by the link builder
// and its handler.
const QLatin1String kWorktreeLinkScheme("forkmesh-worktree:");

// URL scheme for the clickable branch-name link in the agent session header; the
// percent-encoded branch name follows. Clicking it opens that branch's row in
// the Branches tab (adhoc #123). Shared by the link builder and its handler.
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
constexpr int kGraphLaneWidth = 14;
constexpr int kGraphMargin = 9;
// Commit node is drawn as a "bullseye": a hollow ring with a filled centre,
// matching the VS Code git-graph look.
constexpr qreal kGraphNodeOuter = 4.5; // outer ring radius
constexpr qreal kGraphNodeInner = 1.8; // centre-dot radius

// Stable per-lane colour so a branch keeps its hue down the whole graph.
inline QColor commitGraphLaneColor(int lane)
{
    static const QColor palette[] = {
        QColor("#3fb950"), QColor("#58a6ff"), QColor("#d29922"),
        QColor("#bc8cff"), QColor("#f85149"), QColor("#39c5cf"),
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
// that merges into the commit (or branches out of it) is a smooth bezier curve
// into/out of the node. The node itself is a bullseye (a hollow ring with a
// filled centre). Each row carries the lanes present at its top and bottom
// edges; comparing the two boundaries tells us which lanes pass through, merge
// in, or branch out. Topology is meaningful only while the list is in git-log
// order (the default Date-descending sort), which is why that ordering is
// pinned when the list loads.
class CommitGraphDelegate : public QStyledItemDelegate
{
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    void paint(QPainter *painter, const QStyleOptionViewItem &option,
               const QModelIndex &index) const override
    {
        // Strip the selection flag so the solid green band isn't filled, then
        // draw the row's green outline (issue #252). The item has no text.
        QStyleOptionViewItem opt(option);
        opt.state &= ~QStyle::State_Selected;
        QStyledItemDelegate::paint(painter, opt, index);
        paintRowSelectionBorder(painter, option, index);
        const QVariantList topLanes = index.data(kGraphLanesRole).toList();
        const QVariantList botLanes = index.data(kGraphBottomLanesRole).toList();
        const int nodeLane = index.data(kGraphNodeLaneRole).toInt();
        if (topLanes.isEmpty() && botLanes.isEmpty() && nodeLane < 0)
            return;
        const QRect r = option.rect;
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

        // A smooth connector between two points that leaves and arrives
        // vertically — a straight line when the columns match, otherwise an
        // S-curve that bends across the middle (the git-graph house style).
        auto connect = [&](qreal x0, qreal y0, qreal x1, qreal y1,
                           const QColor &c) {
            painter->setPen(QPen(c, 2));
            if (qFuzzyCompare(x0, x1)) {
                painter->setBrush(Qt::NoBrush);
                painter->drawLine(QPointF(x0, y0), QPointF(x1, y1));
                return;
            }
            QPainterPath path(QPointF(x0, y0));
            const qreal cy = (y0 + y1) / 2.0;
            path.cubicTo(QPointF(x0, cy), QPointF(x1, cy), QPointF(x1, y1));
            painter->setBrush(Qt::NoBrush);
            painter->drawPath(path);
        };

        // Every lane other than the node's: straight through if present at both
        // edges, a merge curve if it only enters from the top, a branch curve if
        // it only leaves at the bottom.
        for (int lane = 0; lane <= maxLane; ++lane) {
            if (lane == nodeLane)
                continue;
            const bool inTop = topSet.contains(lane);
            const bool inBot = botSet.contains(lane);
            const QColor c = commitGraphLaneColor(lane);
            if (inTop && inBot)
                connect(laneX(lane), yTop, laneX(lane), yBot, c);
            else if (inTop)
                connect(laneX(lane), yTop, laneX(nodeLane), yMid, c);
            else if (inBot)
                connect(laneX(nodeLane), yMid, laneX(lane), yBot, c);
        }

        if (nodeLane >= 0) {
            const QColor c = commitGraphLaneColor(nodeLane);
            const qreal nx = laneX(nodeLane);
            // The node's own lane: a straight stub above (it was reached from a
            // child) and below (its first parent continues here).
            if (topSet.contains(nodeLane))
                connect(nx, yTop, nx, yMid, c);
            if (botSet.contains(nodeLane))
                connect(nx, yMid, nx, yBot, c);
            // Bullseye node: hollow ring + filled centre, drawn over the lines.
            painter->setBrush(Qt::NoBrush);
            painter->setPen(QPen(c, 1.6));
            painter->drawEllipse(QPointF(nx, yMid), kGraphNodeOuter, kGraphNodeOuter);
            painter->setPen(Qt::NoPen);
            painter->setBrush(c);
            painter->drawEllipse(QPointF(nx, yMid), kGraphNodeInner, kGraphNodeInner);
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

// A super-tiny two-row usage meter for the top bar, sized to tuck in next to the
// node's earnings/avatar (issue #266). The top row is the rolling 5-hour window,
// the bottom row the weekly window; each draws a horizontal track that fills
// 0..100% of that window's utilisation and is tinted green/amber/red as it nears
// the cap. Values are fed from Claude Code rate-limit events (see usageChanged);
// a value of -1 means "unknown" and leaves an empty track. Stored as a plain
// QWidget* on MainWindow and poked via static_cast, like ProgressSlider.
class TokenUsageMiniChart : public QWidget
{
public:
    explicit TokenUsageMiniChart(QWidget *parent = nullptr) : QWidget(parent)
    {
        setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
        setFixedSize(60, 30);
        refreshTooltip();
    }

    // Update one window's utilisation (0..100); pass -1 to mark it unknown.
    void setUsage(bool weekly, int percent)
    {
        int &slot = weekly ? m_weekly : m_fiveHour;
        const int clamped = percent < 0 ? -1 : qBound(0, percent, 100);
        if (slot == clamped)
            return;
        slot = clamped;
        refreshTooltip();
        update();
    }

    // Update one window's "resets in ..." text (e.g. "2h 13m"), shown next to its
    // utilisation in the tooltip so the user can see how long until the limit
    // clears (issue #50). Pass an empty string to mark it unknown.
    void setReset(bool weekly, const QString &remaining)
    {
        QString &slot = weekly ? m_weeklyReset : m_fiveHourReset;
        if (slot == remaining)
            return;
        slot = remaining;
        refreshTooltip();
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
        QFont f = font();
        f.setPointSizeF(qMax(6.0, f.pointSizeF() - 2.0));
        p.setFont(f);
        const QFontMetrics fm(f);

        const char *labels[2] = {"5h", "wk"};
        const int vals[2] = {m_fiveHour, m_weekly};
        const int labelW = fm.horizontalAdvance(QStringLiteral("wk")) + 4;
        const int barH = 5;
        const int rowH = height() / 2;
        for (int i = 0; i < 2; ++i) {
            const QRect rowRect(0, i * rowH, width(), rowH);
            p.setPen(textColor(170));
            p.drawText(QRect(rowRect.left(), rowRect.top(), labelW, rowRect.height()),
                       Qt::AlignVCenter | Qt::AlignLeft, QString::fromLatin1(labels[i]));
            const qreal top = rowRect.center().y() - barH / 2.0;
            const QRectF track(labelW, top, width() - labelW, barH);
            p.setPen(Qt::NoPen);
            p.setBrush(textColor(38));
            p.drawRoundedRect(track, barH / 2.0, barH / 2.0);
            if (vals[i] > 0) {
                QRectF fill(track);
                fill.setWidth(track.width() * vals[i] / 100.0);
                p.setBrush(barColor(vals[i]));
                p.drawRoundedRect(fill, barH / 2.0, barH / 2.0);
            }
        }
    }

private:
    QColor textColor(int alpha) const
    {
        QColor c = palette().color(QPalette::WindowText);
        c.setAlpha(alpha);
        return c;
    }
    static QColor barColor(int pct)
    {
        if (pct >= 90)
            return QColor("#f85149"); // red: near the cap
        if (pct >= 70)
            return QColor("#d29922"); // amber: getting close
        return QColor("#3fb950");     // green: plenty left
    }
    void refreshTooltip()
    {
        auto line = [](const QString &label, int v, const QString &reset) {
            QString s =
                QStringLiteral("%1: %2").arg(
                    label, v < 0 ? QString::fromUtf8("\xE2\x80\x94") // em dash
                                 : QStringLiteral("%1%").arg(v));
            if (!reset.isEmpty())
                s += QString::fromUtf8(" \xC2\xB7 resets in ") + reset; // ·
            return s;
        };
        QString tip = QStringLiteral("Claude Code usage\n%1\n%2")
                          .arg(line(QStringLiteral("5-hour"), m_fiveHour,
                                    m_fiveHourReset),
                               line(QStringLiteral("Weekly"), m_weekly,
                                    m_weeklyReset));
        if (!m_stats.isEmpty())
            tip += QStringLiteral("\n\n") + m_stats;
        setToolTip(tip);
    }

    int m_fiveHour = -1;
    int m_weekly = -1;
    QString m_fiveHourReset; // "resets in ..." text for the 5-hour window
    QString m_weeklyReset;   // "resets in ..." text for the weekly window
    QString m_stats; // per-session token/cost line, shown under the gauges
};

// A tiny moving line chart for one system resource (CPU, memory or disk). New
// per-second samples push in from the right and scroll the history left, so the
// recent load is visible at a glance; the current figure prints beside the
// label. Replaces the static "CPU x% MEM y MB" footer text (adhoc #17). Kept
// header-only (no Q_OBJECT) like the other Internal.h mini-charts; the click
// hook is a std::function so a left-click can still open the stall dialog.
class ResourceSparkline : public QWidget
{
public:
    explicit ResourceSparkline(const QString &label, QWidget *parent = nullptr)
        : QWidget(parent), m_label(label)
    {
        setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
        setFixedSize(kSide, kSide); // a little button-sized square
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
        while (m_history.size() > kMaxPoints)
            m_history.removeFirst();
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

        // Rounded card so each chart reads as its own little square.
        const QRectF box = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
        QColor card = palette().color(QPalette::WindowText);
        card.setAlpha(20);
        p.setPen(Qt::NoPen);
        p.setBrush(card);
        p.drawRoundedRect(box, 4, 4);

        // A header font that shrinks until the label and value both fit on one
        // line, so neither is clipped however the app's base font is sized.
        QFont f = font();
        double pt = f.pointSizeF() > 0 ? qMin(8.0, f.pointSizeF()) : 7.0;
        const double avail = width() - 6;
        for (; pt > 5.5; pt -= 0.5) {
            f.setPointSizeF(pt);
            const QFontMetrics fm(f);
            if (fm.horizontalAdvance(m_label) + fm.horizontalAdvance(m_value) +
                    4 <=
                avail)
                break;
        }
        f.setPointSizeF(pt);
        p.setFont(f);
        const QFontMetrics fm(f);
        const int headH = fm.height();

        // Header: the resource label (left, dim) and its current value (right,
        // in the load colour) share the top line; the chart gets the rest.
        QColor lab = palette().color(QPalette::WindowText);
        lab.setAlpha(150);
        p.setPen(lab);
        p.drawText(QRectF(3, 1, width() - 6, headH),
                   Qt::AlignVCenter | Qt::AlignLeft, m_label);
        const double lastPct =
            m_history.isEmpty() ? 0.0 : m_history.last() / m_max * 100.0;
        p.setPen(gaugeColor(lastPct));
        p.drawText(QRectF(3, 1, width() - 6, headH),
                   Qt::AlignVCenter | Qt::AlignRight, m_value);

        // The sparkline track fills the area below the header, with the most
        // recent sample at its right edge so the curve scrolls left over time.
        const QRectF area(3, headH + 2, width() - 6, height() - headH - 5);
        if (area.height() < 2)
            return;
        QColor track = palette().color(QPalette::WindowText);
        track.setAlpha(28);
        p.setPen(Qt::NoPen);
        p.setBrush(track);
        p.drawRoundedRect(area, 2, 2);
        if (m_history.size() < 2)
            return;
        const QColor line = gaugeColor(m_history.last() / m_max * 100.0);
        const double step = area.width() / double(kMaxPoints - 1);
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
        under.setAlpha(55);
        p.setBrush(under);
        p.setPen(Qt::NoPen);
        p.drawPolygon(fill);
        QPen pen(line);
        pen.setWidthF(1.2);
        p.setPen(pen);
        p.setBrush(Qt::NoBrush);
        p.drawPolyline(curve);
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

    static constexpr int kSide = 40;      // button-sized square (w == h)
    static constexpr int kMaxPoints = 60; // ~1 minute of history at 1 Hz
    QString m_label;
    QString m_value;
    double m_max = 100.0;
    QVector<double> m_history;
};

// Tiny spinning-radar dish + latency readout shown just left of the relay name.
// The dish always sweeps (a continuously rotating wedge) so the relay looks
// "alive"; a one-minute probe feeds in the round-trip time, which renders as
// "33ms" beside it. When the relay stops answering the whole control flips to a
// red alert (red dish + "offline"). Colour-grades the latency green/amber so a
// degrading link is visible at a glance.
class RelayRadarWidget : public QWidget
{
public:
    explicit RelayRadarWidget(QWidget *parent = nullptr) : QWidget(parent)
    {
        setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
        setFixedSize(58, 24);
        refreshTooltip();
        // Drive the sweep: a slow, steady rotation independent of probe timing.
        m_sweep = new QTimer(this);
        m_sweep->setInterval(60);
        connect(m_sweep, &QTimer::timeout, this, [this] {
            m_angle = (m_angle + 9) % 360;
            update();
        });
        m_sweep->start();
    }

    // Record a successful probe (round-trip milliseconds).
    void setLatency(int ms)
    {
        m_latencyMs = qMax(0, ms);
        m_unreachable = false;
        refreshTooltip();
        update();
    }

    // The relay failed to answer the last probe: show the red alert.
    void setUnreachable()
    {
        if (m_unreachable)
            return;
        m_unreachable = true;
        refreshTooltip();
        update();
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);

        const int dish = qMin(height() - 4, 18);
        const QRectF dishRect(2, (height() - dish) / 2.0, dish, dish);
        const QPointF c = dishRect.center();
        const qreal r = dish / 2.0;
        const QColor accent = m_unreachable ? QColor("#f85149")  // red alert
                                            : statusColor();

        // Faint radar rings.
        QColor ring = accent;
        ring.setAlpha(70);
        p.setPen(QPen(ring, 1.0));
        p.setBrush(Qt::NoBrush);
        p.drawEllipse(dishRect);
        p.drawEllipse(c, r * 0.5, r * 0.5);

        // Rotating sweep wedge, fading behind the leading edge.
        QConicalGradient sweep(c, -m_angle);
        QColor lead = accent;
        QColor tail = accent;
        tail.setAlpha(0);
        sweep.setColorAt(0.0, lead);
        sweep.setColorAt(0.18, tail);
        sweep.setColorAt(1.0, tail);
        p.setPen(Qt::NoPen);
        p.setBrush(sweep);
        p.drawPie(dishRect, -m_angle * 16, 70 * 16);

        // Centre blip.
        p.setBrush(accent);
        p.drawEllipse(c, 1.4, 1.4);

        // Latency text / alert to the right of the dish.
        QFont f = font();
        f.setPointSizeF(qMax(6.5, f.pointSizeF() - 2.0));
        p.setFont(f);
        const QRectF textRect(dishRect.right() + 4, 0,
                              width() - dishRect.right() - 4, height());
        QString label;
        QColor textCol;
        if (m_unreachable) {
            label = QStringLiteral("offline");
            textCol = QColor("#f85149");
        } else if (m_latencyMs < 0) {
            label = QString::fromUtf8("\xE2\x80\xA6"); // ellipsis: probing
            textCol = palette().color(QPalette::WindowText);
            textCol.setAlpha(150);
        } else {
            label = QStringLiteral("%1ms").arg(m_latencyMs);
            textCol = statusColor();
        }
        p.setPen(textCol);
        p.drawText(textRect, Qt::AlignVCenter | Qt::AlignLeft, label);
    }

private:
    // Green when snappy, amber when sluggish, red when very slow.
    QColor statusColor() const
    {
        if (m_latencyMs < 0)
            return palette().color(QPalette::WindowText);
        if (m_latencyMs >= 1000)
            return QColor("#f85149"); // red
        if (m_latencyMs >= 300)
            return QColor("#d29922"); // amber
        return QColor("#3fb950");     // green
    }
    void refreshTooltip()
    {
        if (m_unreachable) {
            setToolTip(QStringLiteral(
                "Relay not responding \xE2\x80\x94 last probe timed out"));
        } else if (m_latencyMs < 0) {
            setToolTip(QStringLiteral("Measuring relay latency\xE2\x80\xA6"));
        } else {
            setToolTip(QStringLiteral(
                           "Relay round-trip latency: %1 ms\nProbed every minute")
                           .arg(m_latencyMs));
        }
    }

    int m_latencyMs = -1;       // last measured round-trip; -1 = unknown/probing
    bool m_unreachable = false; // relay failed to answer the last probe
    int m_angle = 0;            // sweep rotation (degrees)
    QTimer *m_sweep = nullptr;  // drives the spin
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

// A compact strip of activity dots shown atop the Mirror nodes tab: one dot per
// active node mirroring this repo. A dot flashes green when its node serves a
// clone (git-upload-pack) and orange when it serves codebase browsing/fetches;
// idle dots sit at a steady online green. Only this node generates live serve
// events, so its own dot is the one that blinks in practice, but the strip is
// keyed by node id so any node's activity can be surfaced as the mesh grows.
// A node the relay's integrity gate is rejecting draws as a green triangle
// instead of a circle, and is kept in the strip even while offline, so the
// warning stays visible instead of the node just disappearing (adhoc #196).
class MirrorActivityStrip : public QWidget
{
public:
    struct Dot
    {
        QString id;
        QString name;
        bool online = false;
        bool self = false;
        // Relay's integrity gate is rejecting this node's clones (adhoc #196).
        // Normally only online nodes get a dot at all, so an offline node
        // failing the check would otherwise vanish from the strip entirely;
        // it's kept and drawn as a triangle instead of a circle so the warning
        // stays visible even while the node is offline.
        bool integrityFailing = false;
    };

    explicit MirrorActivityStrip(QWidget *parent = nullptr) : QWidget(parent)
    {
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        setFixedHeight(18);
        // Drives the fade while any dot is mid-blink; idle when nothing pulses.
        m_anim = new QTimer(this);
        m_anim->setInterval(40);
        connect(m_anim, &QTimer::timeout, this, [this] {
            if (!stepPulses())
                m_anim->stop();
            update();
        });
    }

    void setNodes(const QVector<Dot> &dots)
    {
        m_dots = dots;
        // Drop pulses for nodes no longer present; keep the rest so a roster
        // refresh doesn't reset a blink already in flight.
        QSet<QString> ids;
        for (const Dot &d : dots)
            ids.insert(d.id);
        for (auto it = m_pulse.begin(); it != m_pulse.end();) {
            if (ids.contains(it.key()))
                ++it;
            else
                it = m_pulse.erase(it);
        }
        update();
    }

    // Flash the dot for `nodeId`: green for a served clone, orange for browsing.
    void pulse(const QString &nodeId, bool clone)
    {
        bool known = false;
        for (const Dot &d : m_dots)
            if (d.id == nodeId) {
                known = true;
                break;
            }
        if (!known)
            return;
        m_pulse.insert(nodeId, Pulse{1.0, clone});
        if (!m_anim->isActive())
            m_anim->start();
        update();
    }

    bool isEmpty() const { return m_dots.isEmpty(); }

    // Width needed to show the dots, used to size the floating overlay above the
    // Mirror nodes tab. At most kMaxDots dots are drawn; any beyond collapse into
    // a "+N" tally, whose label width is added here. Capped so a large mesh can't
    // stretch the band; paintEvent already stops drawing once it runs out of room.
    int preferredWidth() const
    {
        if (m_dots.isEmpty())
            return 0;
        const int shown = qMin<qsizetype>(m_dots.size(), kMaxDots);
        const qreal last = kLeftInset + (shown - 1) * kSpacing;
        qreal w = last + kRadius + 4.0;
        if (m_dots.size() > shown)
            w += fontMetrics().horizontalAdvance(
                     QStringLiteral("+%1").arg(m_dots.size() - shown)) +
                 6.0;
        return qMin(240, int(w));
    }

protected:
    QSize sizeHint() const override { return QSize(160, 18); }

    bool event(QEvent *e) override
    {
        if (e->type() == QEvent::ToolTip) {
            auto *he = static_cast<QHelpEvent *>(e);
            if (const Dot *d = dotAt(he->pos())) {
                QToolTip::showText(
                    he->globalPos(),
                    QStringLiteral("%1%2 \xC2\xB7 %3%4")
                        .arg(d->name,
                             d->self ? QStringLiteral(" (you)") : QString(),
                             d->online ? QStringLiteral("online")
                                       : QStringLiteral("offline"),
                             d->integrityFailing
                                 ? QStringLiteral(" \xC2\xB7 failing integrity pin")
                                 : QString()),
                    this);
            } else {
                QToolTip::hideText();
            }
            return true;
        }
        return QWidget::event(e);
    }

    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        const qreal cy = height() / 2.0;
        qreal x = kLeftInset;
        const int shown = qMin<qsizetype>(m_dots.size(), kMaxDots);
        int drawn = 0;
        for (int i = 0; i < shown; ++i) {
            const Dot &d = m_dots.at(i);
            const QColor base =
                d.online ? QColor("#3fb950") : QColor("#484f58");
            QColor col = base;
            const Pulse ph = m_pulse.value(d.id, Pulse{});
            if (ph.level > 0.0) {
                const QColor flash =
                    ph.clone ? QColor("#3fb950") : QColor("#d29922");
                col = blend(base, flash, ph.level);
                QColor halo = flash;
                halo.setAlphaF(0.40 * ph.level);
                p.setPen(Qt::NoPen);
                p.setBrush(halo);
                const qreal hr = kRadius + 4.0 * ph.level;
                p.drawEllipse(QPointF(x, cy), hr, hr);
            }
            p.setPen(Qt::NoPen);
            if (d.integrityFailing) {
                // Offline-but-failing nodes would otherwise be an invisible
                // gap in the strip; draw a green warning triangle in their
                // place so the problem stays visible even while offline.
                p.setBrush(QColor("#3fb950"));
                const QPolygonF triangle({QPointF(x, cy - kRadius - 1.0),
                                          QPointF(x + kRadius + 1.0, cy + kRadius - 1.0),
                                          QPointF(x - kRadius - 1.0, cy + kRadius - 1.0)});
                p.drawPolygon(triangle);
            } else {
                p.setBrush(col);
                p.drawEllipse(QPointF(x, cy), kRadius, kRadius);
            }
            ++drawn;
            x += kSpacing;
            if (x > width() - kRadius)
                break; // ran out of room; the table still lists every node
        }
        // Beyond kMaxDots, collapse the remaining nodes into a "+N" tally rather
        // than drawing a dot each — a 100-node mesh otherwise paints a wall of
        // dots (issue #306). The table below still lists every node.
        const int hidden = m_dots.size() - drawn;
        if (hidden > 0) {
            p.setPen(QColor("#8b949e"));
            p.drawText(
                QRectF(x - kRadius + 2.0, 0, width() - (x - kRadius), height()),
                Qt::AlignLeft | Qt::AlignVCenter,
                QStringLiteral("+%1").arg(hidden));
        }
    }

private:
    struct Pulse
    {
        double level = 0.0; // remaining brightness, fades 1 -> 0
        bool clone = false; // green (clone) vs orange (browse)
    };

    static constexpr qreal kRadius = 5.0;
    static constexpr qreal kSpacing = 15.0;
    static constexpr int kMaxDots = 10; // most-recent dots; rest become "+N"
    // Centre x of the first dot. The strip floats just above the Mirror nodes
    // tab, anchored at that tab's left edge, so inset the dots to line the
    // leftmost one up over the tab's icon: #repoTab has 10px left padding and a
    // 16px octicon, putting the icon centre at 10 + 8 = 18 (adhoc #21).
    static constexpr qreal kLeftInset = 18.0;

    const Dot *dotAt(const QPoint &pos) const
    {
        const qreal cy = height() / 2.0;
        qreal x = kLeftInset;
        const int shown = qMin<qsizetype>(m_dots.size(), kMaxDots);
        for (int i = 0; i < shown; ++i) {
            const Dot &d = m_dots.at(i);
            const qreal dx = pos.x() - x;
            const qreal dy = pos.y() - cy;
            if (dx * dx + dy * dy <= (kRadius + 3.0) * (kRadius + 3.0))
                return &d;
            x += kSpacing;
        }
        return nullptr;
    }

    // Advance every pulse one frame; true while any remain active.
    bool stepPulses()
    {
        bool any = false;
        for (auto it = m_pulse.begin(); it != m_pulse.end();) {
            it.value().level -= 0.06; // ~0.7s flash
            if (it.value().level <= 0.0) {
                it = m_pulse.erase(it);
            } else {
                any = true;
                ++it;
            }
        }
        return any;
    }

    static QColor blend(const QColor &a, const QColor &b, double t)
    {
        t = qBound(0.0, t, 1.0);
        return QColor::fromRgbF(a.redF() + (b.redF() - a.redF()) * t,
                                a.greenF() + (b.greenF() - a.greenF()) * t,
                                a.blueF() + (b.blueF() - a.blueF()) * t);
    }

    QVector<Dot> m_dots;
    QHash<QString, Pulse> m_pulse; // nodeId -> in-flight flash
    QTimer *m_anim = nullptr;
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
// turned off. Fixed button columns are left exactly as the caller set them. Call
// once after the header has been configured.
inline void makeColumnsResizable(QTableWidget *table)
{
    if (!table || !table->model())
        return;
    QHeaderView *header = table->horizontalHeader();
    auto done = std::make_shared<bool>(false);
    QObject::connect(
        table->model(), &QAbstractItemModel::rowsInserted, table,
        [table, header, done]() {
            if (*done)
                return;
            *done = true;
            // Defer to the next event-loop turn so the fit reflects the
            // freshly-set cell contents rather than the just-inserted empty rows.
            QTimer::singleShot(0, table, [table, header]() {
                // The last column may auto-fill via stretchLastSection rather than
                // a per-section Stretch mode; capture that before turning it off.
                const bool stretchLast = header->stretchLastSection();
                const int last = header->count() - 1;
                header->setStretchLastSection(false);
                for (int i = 0; i < header->count(); ++i) {
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
// Persisted Hosts list (adhoc #263): JSON array of {name, ip, user, pass}.
const QString kHostsSetting = QStringLiteral("hosts/list");
const QString kSolanaSetting = QStringLiteral("profile/solana");
const QString kAvatarSetting = QStringLiteral("profile/avatarPng");
const QString kServerUrlSetting = QStringLiteral("server/url");
// The mainnode is ForkMesh's canonical coordination point: a well-known
// owner/repo/room triple that every node's shared rooms and inbox routes
// converge on. The *host* is fully configurable (the first-run Relay server
// field; self-hosting one is a first-class target — see docs/protocol.md), but
// this path shape is a network-wide protocol constant, so it lives in one place
// instead of being spelled out at each call site.
const QString kMainnodeDefaultHost = QStringLiteral("forkmesh.com");
const QString kMainnodeRoomPath =
    QStringLiteral("/api/repo/mainnode/forkmesh/rooms/general/ws");
const QString kLocalServerUrl =
    QStringLiteral("ws://127.0.0.1:8787") + kMainnodeRoomPath;
const QString kDefaultServerUrl =
    QStringLiteral("wss://") + kMainnodeDefaultHost + kMainnodeRoomPath;
const QString kRoomNameSetting = QStringLiteral("server/room");
// Last account this node key authenticated as; lets the app start offline once a
// registered account has been confirmed at least once on this machine.
const QString kAuthedAccountSetting = QStringLiteral("account/authedName");
const QString kEmailVerifiedSettingPrefix =
    QStringLiteral("account/emailVerified/");
const QString kServersArray = QStringLiteral("servers/items");
const QString kActiveServerSetting = QStringLiteral("servers/active");
const QString kDefaultRoomName = QStringLiteral("general");
const QString kRepositoriesArray = QStringLiteral("repositories/items");
const QString kMirrorRootSetting = QStringLiteral("repositories/mirrorRoot");
const QString kLastRepositorySetting = QStringLiteral("repositories/lastOpen");
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
// The shared welcome room every node's one-time "just joined" greeting posts
// to (issue #192). Whether a given identity has already greeted it is tracked
// by ForkMeshIdentity itself (see hasAnnouncedWelcome/markWelcomeAnnounced),
// not here; the QSettings prefix below is the flag's pre-move location, read
// only to migrate nodes that greeted before it moved (adhoc #109).
const QString kWelcomeChannel = QStringLiteral("#welcome");
const QString kLegacyWelcomeAnnouncedSettingPrefix =
    QStringLiteral("chat/welcomeAnnounced/");

// True when a notification category is enabled. Default false: notifications are
// off until the user turns them on, so a fresh install is silent.
inline bool notifyEnabled(const QString &key)
{
    return QSettings().value(key, false).toBool();
}
// Per-PR bounty (issue #347): when enabled, every merged pull request rewards
// its author with a fixed bounty. The amount is USD-priced (reusing the same
// SOL pricing pipeline as issue bounties). Mode selects how it's funded:
// "perPr" shows a funding QR at each merge; "wallet" auto-pays by debiting the
// owner's pre-funded inbuilt bounty wallet (worker action "wallet").
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
// On by default: when the periodic inbox poll finds new issues, merge and
// commit them automatically — but only while the owner's working tree has no
// uncommitted tracked changes, so issue commits never interleave with work in
// progress (issue #193). Off → incoming issues wait in the inbox for a manual
// "Sync inbox" click. The manual button is never gated by this.
const QString kAutoSyncIssuesSetting = QStringLiteral("repos/autoSyncIssues");
// When on, MainWindow::maybeAutoUpdate() periodically checks the update remote
// and, on finding a new tagged release (not just any commit on main), runs the
// same update/rebuild/relaunch flow as the manual "Update, rebuild & restart"
// button — quietly, and never while an agent is running. Off by default on
// desktop; seeded on for headless installs in main.cpp (an operator-run VM has
// no one around to click "update").
const QString kAutoUpdateSetting = QStringLiteral("update/autoUpdate");
// When a new UI stall is detected, hand its backtrace to a coding agent so the
// freeze gets fixed automatically. On by default (adhoc #205).
const QString kAutoAgentOnStallSetting =
    QStringLiteral("diagnostics/autoAgentOnStall");
// Opt-in (OFF by default): on startup, upload the previous session's crash
// summary and UI-stall records to the mainnode so bugs users hit reach a triage
// queue instead of dying in a local log (issue #354). Only the app version, OS,
// and an anonymized node hash go with it; repo names and filesystem paths are
// scrubbed client-side before the payload is built. kTelemetryCrashOffsetSetting
// / kTelemetryStallOffsetSetting remember how many bytes of each log were already
// sent, so a restart never re-uploads the same records.
const QString kUploadTelemetrySetting =
    QStringLiteral("diagnostics/uploadTelemetry");
const QString kTelemetryCrashOffsetSetting =
    QStringLiteral("diagnostics/telemetryCrashOffset");
const QString kTelemetryStallOffsetSetting =
    QStringLiteral("diagnostics/telemetryStallOffset");
// How many bytes of crashes.log had already been seen as of the last startup,
// so a crash that ended the previous session (which never gets a chance to log
// itself — the process is gone) shows up as a line in *this* session's own log
// instead of only ever living in crashes.log/stderr/journalctl (adhoc #200).
// Independent of kTelemetryCrashOffsetSetting/telemetry opt-in: this in-app
// notice always fires, regardless of whether the user enabled the upload.
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
constexpr qint64 kAgentLimit5hMs = 5LL * 60 * 60 * 1000;
constexpr qint64 kAgentLimitWeekMs = 7LL * 24 * 60 * 60 * 1000;
// Issue #346: whether either window has been seen maxed out (>=99%) since it
// last refilled, so the drop back down can be told apart from "just polled
// while still low". Cleared the moment the refill notification fires.
const QString kClaudeUsage5hExhaustedSetting = QStringLiteral("agents/claudeUsage5hExhausted");
const QString kClaudeUsageWeekExhaustedSetting = QStringLiteral("agents/claudeUsageWeekExhausted");
// Opt-in: email the node's account when a previously-maxed-out usage window
// refills. Off by default — most nodes are watched interactively.
const QString kEmailOnCreditsRefillSetting = QStringLiteral("agents/emailOnCreditsRefill");

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
// Disk cache of the last successful /v1/models fetch (see
// MainWindow::refreshClaudeModelCombo), loaded back into m_liveClaudeModels at
// startup so a model combo built before this session's first live fetch
// completes still lists the real models instead of just "Auto".
const QString kClaudeModelsCacheSetting = QStringLiteral("agents/claudeModelsCache");
// Composer "Auto mode" toggle: true => run Claude Code unattended (skip the
// permission prompts). Read when a transcript session launches.
const QString kClaudeAutoModeSetting = QStringLiteral("agents/claudeAutoMode");
// Slash-actions menu (adhoc #116), mirroring the Claude Code extension's "/"
// actions popup. Effort level for Claude Code runs ("low"/"medium"/"high"/
// "xhigh"/"max"), passed to the CLI as `--effort`.
const QString kClaudeEffortSetting = QStringLiteral("agents/claudeEffort");
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
// When an idle agent session's branch would conflict with base (the same
// condition that shows the "Fix conflicts with agent" button), automatically
// ask the agent to merge base and resolve the conflicts instead of waiting for
// a manual click. Default on; can be disabled in Settings.
const QString kAutoFixAgentConflictsSetting =
    QStringLiteral("agents/autoFixConflicts");
// Footer quick-add "Auto-send" toggle (adhoc #45): true => submit the prompt as
// soon as a voice dictation finishes transcribing, without pressing Enter/Send.
const QString kVoiceAutoSubmitSetting = QStringLiteral("agents/voiceAutoSubmit");
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
constexpr int kNetworkLogLimit = 2000;

// Provider family helper: the Anthropic-backed "Claude API" script (plus the
// legacy "claude"/"claude-code" values) shares usage windows, spend tracking and
// iconography; everything else is OpenAI-backed.
inline bool agentIsClaudeProvider(const QString &provider)
{
    return provider.startsWith(QLatin1String("claude"));
}

// User's preferred default agent (Settings → Agents). One of the canonical
// provider ids "openai", "claude-api" or "claude-code"; the quick-add and
// issue-detail provider pickers start on this value. Falls back to OpenAI API
// for an unset/unknown stored value.
const QString kDefaultAgentProviderSetting =
    QStringLiteral("agents/defaultProvider");
const QString kFallbackAgentProvider = QStringLiteral("openai");

inline QString defaultAgentProvider()
{
    const QString value =
        QSettings()
            .value(kDefaultAgentProviderSetting, kFallbackAgentProvider)
            .toString()
            .trimmed();
    if (value == QLatin1String("openai") ||
        value == QLatin1String("claude-api") ||
        value == QLatin1String("claude-code"))
        return value;
    return kFallbackAgentProvider;
}

// Point a provider QComboBox (built with the openai/claude-api/claude-code item
// data) at the user's saved default agent, falling back to the first item when
// the stored value isn't present.
inline void selectDefaultAgentProvider(QComboBox *combo)
{
    if (!combo)
        return;
    const int index = combo->findData(defaultAgentProvider());
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
class FullPopupComboBox : public QComboBox {
public:
    using QComboBox::QComboBox;

protected:
    void showPopup() override
    {
        QComboBox::showPopup();
        QAbstractItemView *v = view();
        QWidget *popup = v ? v->window() : nullptr;
        if (!v || !popup || popup == v || count() == 0)
            return;
        // Height for every row plus the view frame. sizeHintForRow under-reports
        // the styled row height (the rows aren't laid out with their stylesheet
        // metrics yet when the base showPopup returns) and the view's own
        // sizeHint is just QListView's fixed default, so take the per-row hint
        // and add a small cushion per row to cover the styling — generous is
        // fine, it only adds a little bottom padding and is capped to the screen.
        int rowH = v->sizeHintForRow(0);
        if (rowH <= 0)
            rowH = fontMetrics().height() + 8;
        rowH += 8;
        int height = 2 * v->frameWidth() + rowH * count();
        QRect geo = popup->geometry();
        const QRect avail =
            popup->screen() ? popup->screen()->availableGeometry() : geo;
        height = qMin(height, avail.height());
        if (height <= geo.height())
            return; // already tall enough (or genuinely too many items to fit)
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
    combo->setProperty("allowAutoModel", true);
    combo->addItem(QStringLiteral("Auto"), kClaudeAutoModelId);
}

// Friendly label for a session's `model` field, so the agent header can show
// which LLM actually did the work alongside its worktree location. Known short
// aliases and full IDs are mapped to display names; anything else is shown as-is.
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
    };
    return kLabels.value(model.trimmed(), model.trimmed());
}

// Fill an agent-provider model combo for one of the three agent providers
// (adhoc #56; shared by the branch "Fix with agent" bar and the Actions "Fix
// with agent" bar). Item data is the model id passed straight to the caller's
// start function. claude-code combos are left empty for mergeLiveClaudeModels
// to fill; openai/claude-api combos keep static lists (no live fetch for those).
inline void fillAgentFixModelCombo(QComboBox *combo, const QString &provider)
{
    if (!combo)
        return;
    combo->clear();
    if (provider == QLatin1String("claude-code")) {
        // Live models populated by refreshClaudeModelCombo / mergeLiveClaudeModels
    } else if (provider == QLatin1String("openai")) {
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
        combo->addItem(m.value(QStringLiteral("display_name")).toString(id), id);
    }
    const int idx = combo->findData(picked);
    // No restorable pick: land on the first live model, not the synthetic
    // "Auto" entry — an untouched chooser keeps showing the concrete default
    // that actually runs, and auto routing stays strictly opt-in.
    const int fallback =
        combo->property("allowAutoModel").toBool() && combo->count() > 1 ? 1 : 0;
    combo->setCurrentIndex(idx >= 0 ? idx : fallback);
}

// User's preferred tab a repository opens on (Settings → General). Stored as
// the m_repoDetailStack / m_repoDetailTabs index. Restricted to the tabs whose
// data is eagerly loaded when a repo opens — Code(0), Commits(1), Issues(2),
// Agents(3), Pull requests(4), Discussions(5) — so landing there shows content
// without a manual click. Defaults to the Agents tab.
const QString kDefaultRepoTabSetting = QStringLiteral("ui/defaultRepoTab");
constexpr int kFallbackRepoTab = 3; // Agents

inline int defaultRepoTabIndex()
{
    const int value =
        QSettings().value(kDefaultRepoTabSetting, kFallbackRepoTab).toInt();
    return (value >= 0 && value <= 5) ? value : kFallbackRepoTab;
}

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
        "swift",   "silent",   "nimble",  "resilient", "distributed", "encrypted",
        "parallel", "wired",   "forked",  "meshed",    "decentralized", "redundant",
        "synced",  "cascading", "rebased", "cloned",   "merged",      "threaded",
        "routed",  "tunneled", "relayed", "mirrored",  "hashed",      "committed",
        "branched", "patched", "stitched", "woven",    "linked",      "looped",
    };
    static const char *const nouns[] = {
        "fork",    "mirror",   "node",    "mesh",      "relay",       "branch",
        "commit",  "patch",    "packet",  "socket",    "daemon",      "kernel",
        "cache",   "gateway",  "tunnel",  "beacon",    "router",      "hub",
        "thread",  "loom",     "weaver",  "forge",     "anchor",      "compass",
        "lantern", "ember",    "spark",   "comet",     "satellite",   "byte",
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

// A small platform emoji for a node's operating system.
// Crisp vector icons for the server-rail footer (glyph fonts render these
// inconsistently across platforms, so we draw them).
inline QPixmap refreshPixmap(const QColor &color, double angleDeg, int size)
{
    QPixmap pm(size, size);
    pm.fill(Qt::transparent);
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

// Clip avatar PNG bytes into a rounded-rect pixmap for the nav button.
inline QPixmap roundedAvatar(const QByteArray &png, int side)
{
    QPixmap src;
    if (png.isEmpty() || !src.loadFromData(png))
        return QPixmap();
    QPixmap out(side, side);
    out.fill(Qt::transparent);
    QPainter p(&out);
    p.setRenderHint(QPainter::Antialiasing);
    QPainterPath clip;
    clip.addRoundedRect(0, 0, side, side, side * 0.28, side * 0.28);
    p.setClipPath(clip);
    p.drawPixmap(0, 0, src.scaled(side, side, Qt::KeepAspectRatioByExpanding,
                                  Qt::SmoothTransformation));
    return out;
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

    QPixmap pm(size, size);
    pm.fill(Qt::transparent);
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
    QPixmap pixmap(12, 12);
    pixmap.fill(Qt::transparent);
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
                     refreshPixmap(QColor(Theme::kTextTertiary), m_angle, m_size));
    }

private:
    QTimer *m_timer = nullptr;
    int m_size;
    int m_angle = 0;
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
                         const QColor &color = QColor("#58a6ff"))
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

// Compact "issue looper" toggle that floats just above the Issues tab (adhoc
// #130). It is both the control and the indicator: a small on/off switch and
// the open issue currently being worked ("#124") — clicking that "#N" jumps to
// its agent (adhoc #134), while clicking elsewhere toggles the loop. While on,
// a single neon-green segment travels slowly around the rounded-rect border — a
// bright loop circling "the whole thing" so the running loop reads from any
// tab. Replaces the old in-page "working the backlog" banner and the tiny
// Issues-tab braille snake.
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
    const QString key = name + QLatin1Char('|') +
                        QString::number(color.rgba(), 16) + QLatin1Char('|') +
                        QString::number(size);
    const auto cached = cache.constFind(key);
    if (cached != cache.constEnd())
        return cached.value();

    QPixmap pixmap(size, size);
    pixmap.fill(Qt::transparent);

    QSvgRenderer renderer(QStringLiteral(":/icons/octicons/%1.svg").arg(name));
    if (!renderer.isValid())
        return pixmap;

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    renderer.render(&painter, QRectF(0, 0, size, size));
    painter.setCompositionMode(QPainter::CompositionMode_SourceIn);
    painter.fillRect(pixmap.rect(), color);
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

// A tinted octicon rotated `angleDeg` about its centre — used to spin the green
// "running" glyph in the agents list (issue #108). Not cached, since the angle
// changes every animation frame; callers keep it to the handful of running rows.
inline QPixmap rotatedTintedOcticonPixmap(const QString &name, const QColor &color,
                                   int size, qreal angleDeg)
{
    const QPixmap base = tintedOcticonPixmap(name, color, size);
    QPixmap out(size, size);
    out.fill(Qt::transparent);
    QPainter painter(&out);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    painter.translate(size / 2.0, size / 2.0);
    painter.rotate(angleDeg);
    painter.translate(-size / 2.0, -size / 2.0);
    painter.drawPixmap(0, 0, base);
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

inline QString serverHost(const QString &serverUrl)
{
    return QUrl(serverUrl).host();
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

// While >0, the git wait below keeps the GUI event loop breathing instead of
// blocking the main thread outright. A multi-second git read (a big ls-tree,
// log --numstat, count-objects, …) would otherwise stop the app answering
// window-manager pings and get flagged "Not Responding". User input is excluded
// from the pump so a stray click can't re-enter a load mid-flight; openRepoDetail's
// m_repoDetailLoading guard backstops anything that still slips through.
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

// Wait up to 8s for a git subprocess. With a keep-alive scope active, poll in
// short slices and service the GUI between them so the window stays responsive
// and spinners animate; otherwise block as before.
inline bool waitForGit(QProcess &process, QString *err)
{
    // Breadcrumb for the stall watchdog: if this synchronous wait freezes the GUI
    // thread, the stall report can name the git command instead of leaving only a
    // raw backtrace. Only the main thread is watched, so leave the breadcrumb alone
    // for off-thread reads rather than clobbering what the GUI thread set.
    std::optional<BlockingCallScope> crumb;
    const QCoreApplication *app = QCoreApplication::instance();
    if (app && QThread::currentThread() == app->thread())
        crumb.emplace(gitBlockingCrumb(process));

    if (g_gitKeepAliveDepth <= 0) {
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

// RAII: keep the GUI responsive across the run of synchronous git reads in an
// interactive load (a node switch or opening a repo). Nestable.
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

// True when `workTree`'s issues/ subtree has no uncommitted *tracked* changes —
// a clean base for the auto-issue-sync to land issue commits on (issue #193).
// Scoped to issues/ (not the whole tree) because IssueStore::commit() only ever
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
                        "issues"},
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
inline int mirrorArtifactCount(const QString &mirrorPath)
{
    if (mirrorPath.trimmed().isEmpty() || !QDir(mirrorPath).exists())
        return -1;
    const QDir casDir(mirrorReleaseCasRoot(mirrorPath));
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

// Issues / pull requests / discussions a node's mirror holds — each the count of
// numbered subdirs under the matching top-level folder on the served branch.
inline int mirrorIssueCount(const QString &mirrorPath, const QString &branch)
{
    return mirrorNumberedDirCount(mirrorPath, branch, QStringLiteral("issues"));
}
inline int mirrorPullCount(const QString &mirrorPath, const QString &branch)
{
    return mirrorNumberedDirCount(mirrorPath, branch, QStringLiteral("pulls"));
}
inline int mirrorDiscussionCount(const QString &mirrorPath, const QString &branch)
{
    return mirrorNumberedDirCount(mirrorPath, branch,
                                  QStringLiteral("discussions"));
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
    if (!runGitCaptureWithEnv(dir, {"read-tree", base}, env, &ignored, err))
        return false;
    if (!runGitCaptureWithEnv(dir, {"add", "-A", "--", "."}, env, &ignored, err))
        return false;
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
    QPixmap out(side, side);
    out.fill(Qt::transparent);
    if (src.isNull())
        return out;
    QPainter p(&out);
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    QPainterPath path;
    path.addRoundedRect(QRectF(0, 0, side, side), radius, radius);
    p.setClipPath(path);
    const QPixmap scaled = src.scaled(side, side, Qt::KeepAspectRatioByExpanding,
                                      Qt::SmoothTransformation);
    p.drawPixmap((side - scaled.width()) / 2, (side - scaled.height()) / 2, scaled);
    return out;
}

// Wide variant of roundedRectPixmap: scales `src` to cover a w*h banner
// (center-cropped, no distortion) and clips it to rounded corners. Used for the
// full-width avatar header at the top of the node profile panel.
inline QPixmap roundedBannerPixmap(const QPixmap &src, int w, int h, qreal radius)
{
    QPixmap out(w, h);
    out.fill(Qt::transparent);
    if (src.isNull() || w <= 0 || h <= 0)
        return out;
    QPainter p(&out);
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    QPainterPath path;
    path.addRoundedRect(QRectF(0, 0, w, h), radius, radius);
    p.setClipPath(path);
    const QPixmap scaled = src.scaled(w, h, Qt::KeepAspectRatioByExpanding,
                                      Qt::SmoothTransformation);
    p.drawPixmap((w - scaled.width()) / 2, (h - scaled.height()) / 2, scaled);
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
    QPixmap out(w, h);
    out.fill(Qt::transparent);
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

inline QPixmap letterFavicon(const QString &host)
{
    constexpr int side = 36;
    QPixmap pixmap(side, side);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    const uint hash = qHash(host);
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(Theme::kSenderPalette[hash % Theme::kSenderPaletteSize]));
    painter.drawRoundedRect(0, 0, side, side, 9, 9);
    const QChar letter = host.isEmpty() ? QChar('?') : host.at(0).toUpper();
    QFont font = painter.font();
    font.setPixelSize(18);
    font.setBold(true);
    painter.setFont(font);
    painter.setPen(QColor("#0f172a"));
    painter.drawText(pixmap.rect(), Qt::AlignCenter, QString(letter));
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

inline void setAutostartEnabled(bool enabled)
{
    const QString exe = QCoreApplication::applicationFilePath();
#if defined(Q_OS_WIN)
    QSettings run(kWinRunKey, QSettings::NativeFormat);
    if (enabled)
        run.setValue("ForkMesh", QDir::toNativeSeparators(exe));
    else
        run.remove("ForkMesh");
#elif defined(Q_OS_MACOS)
    const QString path = launchAgentPath();
    if (!enabled) {
        QFile::remove(path);
        return;
    }
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    if (file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        const QString plist = QStringLiteral(
            "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
            "<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" "
            "\"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">\n"
            "<plist version=\"1.0\"><dict>\n"
            "  <key>Label</key><string>com.forkmesh.app</string>\n"
            "  <key>ProgramArguments</key><array><string>%1</string></array>\n"
            "  <key>RunAtLoad</key><true/>\n"
            "</dict></plist>\n").arg(exe);
        file.write(plist.toUtf8());
    }
#else
    const QString path = autostartDesktopPath();
    if (!enabled) {
        QFile::remove(path);
        return;
    }
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    if (file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        const QString desktop = QStringLiteral(
            "[Desktop Entry]\n"
            "Type=Application\n"
            "Name=ForkMesh\n"
            "Exec=%1\n"
            "Terminal=false\n"
            "X-GNOME-Autostart-enabled=true\n").arg(exe);
        file.write(desktop.toUtf8());
    }
#endif
}


} // namespace forkmesh::ui
