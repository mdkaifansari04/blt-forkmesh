#pragma once

#include "StartupTrace.h"


#include "MainWindow.h"
#include "TerminalWidget.h"

#include "ActionFile.h"
#include "ActionRunner.h"
#include "BackgroundActivity.h"
#include "BackoffNetworkAccessManager.h"
#include "FileTreeSupport.h"
#include "GuiPump.h"
#include "ClaudeAgentScript.h"
#include "CloudflareAgentScript.h"
#include "ClaudeIdeBridge.h"
#include "ClaudeStreamSession.h"
#include "ClaudeTranscriptView.h"
#include "CommitGraph.h"
#include "ScrollJumpButtons.h"
#include "DirectorySizeScan.h"
#include "StallWatchdog.h"
#include "IssueBurnup.h"
#include "QrCode.h"
#include "ReferenceLinks.h"

#include "MarkdownEditor.h"
#include "MessageRow.h"
#include "MainnodeRoom.h"
#include "NodeDiagnostics.h"
#include "PullReviewModel.h"
#include "RepoHost.h"
#include "RepoSecurity.h"
#include "ServerNode.h"
#include "SingleInstance.h"
#include "SystemStats.h"
#include "AgentStore.h"
#include "Theme.h"
#include "VirtualMachineRuntime.h"

#include <QAbstractButton>
#include <QAbstractAnimation>
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
#include <QEasingCurve>
#include <QFileInfo>
#include <QMimeData>
#include <QMutex>
#include <QMutexLocker>
#include <QFontDatabase>
#include <QFormLayout>
#include <QFrame>
#include <QFutureWatcher>
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
#include <QtConcurrent/QtConcurrentRun>
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
#include <QStylePainter>
#include <QStyledItemDelegate>
#include <QStyleHints>
#include <QStyleOptionComboBox>
#include <QSyntaxHighlighter>
#include <QAbstractItemView>
#include <QHeaderView>
#include <QTableWidget>
#include <QTabBar>
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
#include <utility>

#ifndef Q_OS_WIN
#include <csignal>
#include <pwd.h>
#include <sys/types.h>
#include <unistd.h>
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


struct DiffFileEntry {
    QString path;
    QString anchor;
    int adds = 0;
    int dels = 0;
    QString status = QStringLiteral("modified"); // added/deleted/modified/renamed
    bool binary = false;
};
QString diffStyleSheet(int fontPt = 12);
QString renderDiffHtml(const QString &patch, QList<DiffFileEntry> &files,
                       const QString &dir, const QString &base, const QString &head,
                       const QString &anchorFile = QString(),
                       const QHash<QString, QString> &lineNotes = {},
                       const QSet<QString> &viewedFiles = {});
QString renderDiffHtmlSplit(bool split, const QString &patch,
                            QList<DiffFileEntry> &files, const QString &dir,
                            const QString &base, const QString &head,
                            const QString &anchorFile = QString(),
                            const QHash<QString, QString> &lineNotes = {},
                            const QSet<QString> &viewedFiles = {});
bool diffSplitPref();
void setDiffSplitPref(bool split);
QString diffFileLabelHtml(const DiffFileEntry &f, bool viewed);
QString diffRowControlsHtml(const QString &path, double progress, bool viewed,
                            bool comments);
void renderDiffStreamed(QTextEdit *view, const QString &html,
                        const QString &styleSheet, bool endCap = true);
bool restyleDiffStreamed(QTextEdit *view, const QString &styleSheet);
void scrollDiffToAnchor(QTextEdit *view, const QString &anchor);
void flushDiffStream(QTextEdit *view);
void addDiffStreamFinishedHook(QTextEdit *view, std::function<void()> hook);
bool autoMarkViewedOnScrollPref();
void setAutoMarkViewedOnScrollPref(bool on);
void applyDiffSearchHighlights(QTextBrowser *diff,
                               const QList<QTextCursor> &matches, int activeIndex,
                               QLabel *countLabel, bool termEmpty);
QString diffStickyStyleSheet(int fontPt);
QString diffStickyPathHtml(const QString &fullPath);
QString diffStickyPathText(const QString &path);
void configureDiffStickyPathLabel(QLabel *label);
QString agentCostText(double usd);
QString agentStatusText(const QString &status);
QColor agentStatusColor(const QString &status);
bool agentSessionActive(const AgentSession *s);
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
        m_list->setFocusPolicy(Qt::StrongFocus);
        m_list->setSelectionMode(QAbstractItemView::SingleSelection);
        m_list->installEventFilter(this);
        m_sticky = new QLabel(m_diff->viewport());
        m_sticky->setObjectName(QStringLiteral("diffStickyHeader"));
        m_sticky->setTextFormat(Qt::RichText);
        configureDiffStickyPathLabel(m_sticky);
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
                             m_ignoreScroll = true;
                             scrollDiffToAnchor(m_diff, anchor);
                             m_ignoreScroll = false;
                             refresh(/*syncSelection=*/false);
                         });
        addDiffStreamFinishedHook(m_diff, [this] { rebuildSpans(); });
    }

    void rebuild(const QList<DiffFileEntry> &files, int fontPt)
    {
        m_files = files;
        m_filesByAnchor.clear();
        for (const DiffFileEntry &file : m_files)
            m_filesByAnchor.insert(file.anchor, file);
        m_sticky->setStyleSheet(diffStickyStyleSheet(fontPt));
        rebuildSpans();
    }

private:
    bool eventFilter(QObject *watched, QEvent *event) override
    {
        if (watched == m_list && event->type() == QEvent::KeyPress) {
            const auto *key = static_cast<QKeyEvent *>(event);
            if ((key->key() == Qt::Key_Up || key->key() == Qt::Key_Down) &&
                !(key->modifiers() &
                  (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier))) {
                const int delta = key->key() == Qt::Key_Up ? -1 : 1;
                int row = m_list->currentRow();
                if (row < 0)
                    row = delta > 0 ? -1 : m_list->count();
                do {
                    row += delta;
                } while (row >= 0 && row < m_list->count() &&
                         m_list->item(row)->isHidden());
                if (row >= 0 && row < m_list->count()) {
                    m_list->setCurrentRow(row);
                    m_list->scrollToItem(m_list->item(row));
                }
                return true;
            }
        }
        return QObject::eventFilter(watched, event);
    }

    struct Span {
        int pos;
        QString path;
        QString anchor;
    };

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
        const auto file = m_filesByAnchor.constFind(cur->anchor);
        m_sticky->setText(file == m_filesByAnchor.constEnd()
                              ? diffStickyPathHtml(cur->path)
                              : diffFileLabelHtml(file.value(), false));
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
    QHash<QString, DiffFileEntry> m_filesByAnchor;
    QList<Span> m_spans;
};
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

constexpr int kToastPadLeft = 10;
constexpr int kToastPadTop = 6;
constexpr int kToastPadRight = 8;
constexpr int kToastPadBottom = 6;
constexpr int kToastLineSpacing = 2;
constexpr int kToastRowSpacing = 4;
constexpr int kToastStackGap = 6;
constexpr int kToastPromptGap = 18;
constexpr int kToastEntryRise = 14;
static_assert(kToastEntryRise < kToastPromptGap,
              "the entry rise has to fit inside the gap above the prompt");
constexpr int kToastEntryMs = 180;
constexpr double kToastStackHeightShare = 0.5;
constexpr int kToastMinStackHeight = 140;
constexpr double kToastActiveCardShare = 0.6;
constexpr int kToastMinActiveCard = 100;
constexpr int kToastAgentIconPx = 20;
constexpr int kToastAvatarPx = 28;
constexpr int kToastAvatarGap = 8;
constexpr int kToastShiftMs = 150;

constexpr int kTableSortRole = Qt::UserRole + 10;
constexpr int kProgressBarRole = Qt::UserRole + 11;
constexpr int kPacmanAnchorRole = Qt::UserRole + 12;
constexpr int kNodeLightRole = Qt::UserRole + 13;
// Cadence on which a node re-fetches its mirrors from source (mirrors
// m_mirrorSyncTimer, which adds ±15% jitter — the pie is an approximation);
// a behind node is expected to catch up within roughly a few minutes. This is
// only the dropped-event safety net: push events still notify mirror peers the
// moment the source moves.
// Push/websocket events are the primary update path. This short poll is the
// bounded retry for a dropped event or for a peer that woke while the source's
// gateway generation was still swapping.
constexpr qint64 kMirrorSyncIntervalMs = 5LL * 60 * 1000;
constexpr int kMirrorSyncJitterPercent = 15;
constexpr int kMirrorSyncIntervalMinMinutes = 1;
constexpr int kMirrorSyncIntervalMaxMinutes = 24 * 60;
constexpr int kMirrorSyncIntervalDefaultMinutes = 5;
const QString kMirrorSyncIntervalSetting =
    QStringLiteral("repos/mirrorSyncIntervalMinutes");

inline int mirrorSyncIntervalMinutes()
{
    return qBound(
        kMirrorSyncIntervalMinMinutes,
        QSettings()
            .value(kMirrorSyncIntervalSetting,
                   kMirrorSyncIntervalDefaultMinutes)
            .toInt(),
        kMirrorSyncIntervalMaxMinutes);
}

inline qint64 mirrorSyncIntervalMs()
{
    return qint64(mirrorSyncIntervalMinutes()) * 60 * 1000;
}

constexpr auto kActionNodeLabelsSetting = "actions/nodeLabels";
bool currentThemeIsDark();
inline QIcon themedOcticon(const QString &name, const QColor &color, int size);

constexpr int kCommitSummaryCol = 6;
constexpr int kCommitHashCol = 2;
constexpr int kCommitActionCol = 7;
constexpr int kCommitGraphCol = 8;
constexpr int kGraphLanesRole = Qt::UserRole + 20;    // QVariantList<int> lanes at the row's top edge
constexpr int kGraphNodeLaneRole = Qt::UserRole + 21; // int lane of this commit's dot
constexpr int kGraphBottomLanesRole =
    Qt::UserRole + 22; // QVariantList<int> lanes at the row's bottom edge
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
constexpr int kCommitRefKindsRole =
    Qt::UserRole + 34; // QStringList aligned with kCommitRefsRole: local/remote/tag
constexpr int kCommitOutgoingRole = Qt::UserRole + 35;

const QLatin1String kBranchLinkScheme("forkmesh-branch:");

const QLatin1String kPullLinkScheme("forkmesh-pull:");

const QLatin1String kIssueLinkScheme("forkmesh-issue:");

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

const QLatin1String kRepoInfoPath(".forkmesh/info.json");

const QLatin1String kAgentLinkScheme("forkmesh-agent:");



inline void paintRowSelectionBorder(QPainter *painter,
                                    const QStyleOptionViewItem &option,
                                    const QModelIndex &index);


class CommitSummaryDelegate : public QStyledItemDelegate
{
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    void paint(QPainter *painter, const QStyleOptionViewItem &option,
               const QModelIndex &index) const override
    {
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

        const QModelIndex graphIdx =
            index.model()->index(index.row(), kCommitGraphCol);
        const QVariantList topLanes = graphIdx.data(kGraphLanesRole).toList();
        const QVariantList botLanes =
            graphIdx.data(kGraphBottomLanesRole).toList();
        const QVariant nodeLaneVar = graphIdx.data(kGraphNodeLaneRole);
        const int nodeLane = nodeLaneVar.isValid() ? nodeLaneVar.toInt() : -1;
        int rowMaxLane = std::max(nodeLane, 0);
        for (const QVariant &v : topLanes)
            rowMaxLane = std::max(rowMaxLane, v.toInt());
        for (const QVariant &v : botLanes)
            rowMaxLane = std::max(rowMaxLane, v.toInt());
        if (fileRow) // nested one step under its commit's lane
            rowMaxLane =
                std::max(rowMaxLane, index.data(kGraphNodeLaneRole).toInt());
        const int laneCount =
            std::max(rowMaxLane + 1,
                     w ? w->property(kGraphLaneCountProperty).toInt() : 0);
        const CommitGraphMetrics metrics = commitGraphMetrics(laneCount);
        paintCommitGraphGutter(painter, option.rect, topLanes, botLanes,
                               nodeLane,
                               graphIdx.data(kGraphIsMergeRole).toBool(),
                               index.data(kCommitOutgoingRole).toBool(),
                               metrics);
        const int indent = commitGraphTextIndent(rowMaxLane, metrics);

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

        int x = r.left();
        const QIcon icon = qvariant_cast<QIcon>(index.data(Qt::DecorationRole));
        if (!icon.isNull()) {
            const QRect ir(x, r.center().y() - 7, 14, 14);
            icon.paint(painter, ir);
            x += 18;
        }
        int rightEdge = r.right();
        const QStringList refs = index.data(kCommitRefsRole).toStringList();
        const QStringList refKinds =
            index.data(kCommitRefKindsRole).toStringList();
        if (index.data(kCommitUnsyncedRole).toBool()) {
            const QString mark = QString::fromUtf8("\xE2\x96\xB2");
            const int mw = fm.horizontalAdvance(mark);
            painter->setPen(QColor("#d29922"));
            painter->drawText(QRect(rightEdge - mw, r.top(), mw, r.height()),
                              Qt::AlignVCenter | Qt::AlignRight, mark);
            rightEdge -= mw + 8;
        }
        if (!refs.isEmpty()) {
            painter->setRenderHint(QPainter::Antialiasing, true);
            for (int refIndex = refs.size() - 1; refIndex >= 0; --refIndex) {
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
                rightEdge -= rw;
                const QRect br(rightEdge, r.center().y() - fm.height() / 2 - 1, rw,
                               fm.height() + 2);
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
                rightEdge -= 5;
            }
            painter->setBrush(Qt::NoBrush);
        }
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

static QColor insightContributorColor(const QString &name)
{
    quint32 h = 2166136261u; // FNV-1a
    for (const char c : name.toUtf8())
        h = (h ^ static_cast<quint8>(c)) * 16777619u;
    return QColor::fromHsv(int(h % 360u), 150, 205);
}

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

class TokenUsageMiniChart : public QWidget
{
public:
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
        setCursor(Qt::PointingHandCursor);
        setFixedSize(qRound(m_windows * kBarW + (m_windows - 1) * kGap) + 6, 24);
        refreshTooltip();
    }

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

    void setRemaining(bool weekly, int percent, const QString &note)
    {
        setUsage(weekly, percent);
        setWindowNote(weekly, note);
    }

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

    void setStats(const QString &stats)
    {
        if (m_stats == stats)
            return;
        m_stats = stats;
        refreshTooltip();
    }

    std::function<void()> onHover;
    std::function<void(const QPoint &)> onClick;

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

        if (m_flash != 0) {
            QPen pen(m_flash > 0 ? QColor("#3fb950") : QColor("#f85149"));
            pen.setWidthF(1.5);
            p.setPen(pen);
            p.setBrush(Qt::NoBrush);
            p.drawRoundedRect(QRectF(rect()).adjusted(0.75, 0.75, -0.75, -0.75),
                              4.0, 4.0);
        }
    }
    void mousePressEvent(QMouseEvent *event) override
    {
        if (event->button() == Qt::LeftButton && onClick) {
            onClick(mapToGlobal(event->position().toPoint()));
            event->accept();
            return;
        }
        QWidget::mousePressEvent(event);
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
        tip += QStringLiteral("\n\nClick to view accounts and usage.");
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

class TokenUsageMenuRow : public QWidget
{
public:
    TokenUsageMenuRow(const QString &label, const QString &value,
                      const QString &resetNote, int percent,
                      QWidget *parent = nullptr)
        : QWidget(parent), m_label(label), m_value(value),
          m_resetNote(resetNote), m_percent(percent)
    {
        setFixedHeight(34);
        setMinimumWidth(300);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        setAccessibleName(QStringLiteral("%1 usage").arg(label));
        setToolTip(QStringLiteral("%1: %2%3")
                       .arg(label, value,
                            resetNote.isEmpty()
                                ? QString()
                                : QStringLiteral(" · %1").arg(resetNote)));
    }

    void setUsageData(const QString &label, const QString &value,
                      const QString &resetNote, int percent)
    {
        m_label = label;
        m_value = value;
        m_resetNote = resetNote;
        m_percent = percent;
        setAccessibleName(QStringLiteral("%1 usage").arg(label));
        setToolTip(QStringLiteral("%1: %2%3")
                       .arg(label, value,
                            resetNote.isEmpty()
                                ? QString()
                                : QStringLiteral(" · %1").arg(resetNote)));
        update();
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);

        const QColor text = palette().color(QPalette::WindowText);
        const QColor muted(text.red(), text.green(), text.blue(), 150);
        const QColor track(text.red(), text.green(), text.blue(), 38);
        painter.setPen(text);
        painter.drawText(QRectF(4, 2, 90, 15), Qt::AlignLeft | Qt::AlignVCenter,
                         m_label);

        const QRectF bar(96, 7, 132, 8);
        painter.setPen(Qt::NoPen);
        painter.setBrush(track);
        painter.drawRoundedRect(bar, 4, 4);
        if (m_percent >= 0) {
            const qreal width = bar.width() * qBound(0, m_percent, 100) / 100.0;
            if (width > 0) {
                QColor fill = m_percent >= 90 ? QColor("#f85149")
                                              : m_percent >= 70
                                                    ? QColor("#d29922")
                                                    : QColor("#3fb950");
                painter.setBrush(fill);
                painter.drawRoundedRect(QRectF(bar.left(), bar.top(), width,
                                               bar.height()),
                                        4, 4);
            }
        }

        painter.setPen(text);
        painter.drawText(QRectF(234, 2, width() - 238, 15),
                         Qt::AlignRight | Qt::AlignVCenter, m_value);
        if (!m_resetNote.isEmpty()) {
            painter.setPen(muted);
            painter.drawText(QRectF(96, 18, width() - 100, 13),
                             Qt::AlignLeft | Qt::AlignVCenter, m_resetNote);
        }
    }

private:
    QString m_label;
    QString m_value;
    QString m_resetNote;
    int m_percent = -1;
};

constexpr int kResourceSparklineSide = 34;

class ResourceSparkline : public QWidget
{
public:
    explicit ResourceSparkline(const QString &label, QWidget *parent = nullptr,
                               int side = kResourceSparklineSide,
                               int maxPoints = 60)
        : QWidget(parent), m_label(label), m_maxPoints(qMax(2, maxPoints))
    {
        setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
        setFixedSize(side, side); // a little button-sized square
        setCursor(Qt::PointingHandCursor);
    }

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
        m_loading = false;
        m_max = maxValue > 0 ? maxValue : 1.0;
        m_value = valueText;
        m_history = values.mid(qMax(0, values.size() - m_maxPoints));
        update();
    }

    void setLoading(bool loading)
    {
        m_loading = loading;
        if (loading) {
            m_value.clear();
            m_history.clear();
        }
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

        const QRectF box = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
        QPainterPath cardPath;
        cardPath.addRoundedRect(box, 4, 4);
        QColor card = palette().color(QPalette::WindowText);
        card.setAlpha(28);
        p.setPen(Qt::NoPen);
        p.setBrush(card);
        p.drawPath(cardPath);

        if (m_loading) {
            QColor placeholder = palette().color(QPalette::WindowText);
            placeholder.setAlpha(42);
            p.setPen(Qt::NoPen);
            p.setBrush(placeholder);
            p.drawRoundedRect(box.adjusted(7, 9, -7, -16), 2, 2);
            p.drawRoundedRect(box.adjusted(10, 21, -10, -7), 2, 2);
            return;
        }

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
    bool m_loading = false;
};

class ResourceQuadrantSparkline : public QWidget
{
public:
    enum Resource {
        Cpu = 0,
        Memory,
        Swap,
        Disk,
        ResourceCount
    };

    explicit ResourceQuadrantSparkline(QWidget *parent = nullptr,
                                       int side = kResourceSparklineSide,
                                       int maxPoints = 60)
        : QWidget(parent), m_maxPoints(qMax(2, maxPoints))
    {
        setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
        setFixedSize(side, side);
        setCursor(Qt::PointingHandCursor);
        setMouseTracking(true);
    }

    void addSample(Resource resource, double value, double maxValue)
    {
        Sample &sample = m_samples[resource];
        sample.max = maxValue > 0 ? maxValue : 100.0;
        sample.history.append(value);
        while (sample.history.size() > m_maxPoints)
            sample.history.removeFirst();
        update();
    }

    void setResourceToolTip(Resource resource, const QString &toolTip)
    {
        m_samples[resource].toolTip = toolTip;
    }

    void setClickHandler(Resource resource, std::function<void()> handler)
    {
        m_samples[resource].onClicked = std::move(handler);
    }

protected:
    bool event(QEvent *event) override
    {
        if (event->type() == QEvent::ToolTip) {
            const auto *helpEvent = static_cast<QHelpEvent *>(event);
            const int resource = resourceAt(helpEvent->pos());
            if (resource >= 0 && !m_samples[resource].toolTip.isEmpty())
                QToolTip::showText(helpEvent->globalPos(),
                                   m_samples[resource].toolTip, this);
            else
                QToolTip::hideText();
            return true;
        }
        return QWidget::event(event);
    }

    void mousePressEvent(QMouseEvent *event) override
    {
        const int resource = resourceAt(event->pos());
        if (event->button() == Qt::LeftButton && resource >= 0 &&
            m_samples[resource].onClicked) {
            m_samples[resource].onClicked();
            event->accept();
            return;
        }
        QWidget::mousePressEvent(event);
    }

    void mouseMoveEvent(QMouseEvent *event) override
    {
        const int resource = resourceAt(event->pos());
        if (resource != m_hoveredResource && QToolTip::isVisible()) {
            if (resource >= 0 && !m_samples[resource].toolTip.isEmpty())
                QToolTip::showText(mapToGlobal(event->pos()),
                                   m_samples[resource].toolTip, this);
            else
                QToolTip::hideText();
        }
        m_hoveredResource = resource;
        QWidget::mouseMoveEvent(event);
    }

    void leaveEvent(QEvent *event) override
    {
        m_hoveredResource = -1;
        QWidget::leaveEvent(event);
    }

    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        for (int resource = 0; resource < ResourceCount; ++resource)
            paintQuadrant(p, resource, quadrantRect(resource));
    }

private:
    struct Sample {
        QVector<double> history;
        double max = 100.0;
        QString toolTip;
        std::function<void()> onClicked;
    };

    static QColor gaugeColor(double pct)
    {
        if (pct >= 90)
            return QColor("#f85149"); // red: pegged
        if (pct >= 70)
            return QColor("#d29922"); // amber: getting busy
        return QColor("#3fb950");     // green: light load
    }

    QRectF quadrantRect(int resource) const
    {
        constexpr qreal gap = 1.0;
        const QRectF box = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
        const qreal cellWidth = (box.width() - gap) / 2.0;
        const qreal cellHeight = (box.height() - gap) / 2.0;
        const int row = resource / 2;
        const int column = resource % 2;
        return QRectF(box.left() + column * (cellWidth + gap),
                      box.top() + row * (cellHeight + gap), cellWidth, cellHeight);
    }

    int resourceAt(const QPoint &position) const
    {
        for (int resource = 0; resource < ResourceCount; ++resource) {
            if (quadrantRect(resource).contains(position))
                return resource;
        }
        return -1;
    }

    void paintQuadrant(QPainter &p, int resource, const QRectF &box) const
    {
        QPainterPath cardPath;
        cardPath.addRoundedRect(box, 2, 2);
        QColor card = palette().color(QPalette::WindowText);
        card.setAlpha(28);
        p.setPen(Qt::NoPen);
        p.setBrush(card);
        p.drawPath(cardPath);

        const Sample &sample = m_samples[resource];
        const QRectF area = box.adjusted(1.0, 1.0, -1.0, -1.0);
        if (area.height() < 2 || sample.history.size() < 2)
            return;

        p.save();
        p.setClipPath(cardPath);
        const QColor line = gaugeColor(sample.history.last() / sample.max * 100.0);
        const double step = area.width() / double(m_maxPoints - 1);
        const int n = sample.history.size();
        QPolygonF curve;
        for (int i = 0; i < n; ++i) {
            const double x = area.right() - (n - 1 - i) * step;
            const double norm = qBound(0.0, sample.history.at(i) / sample.max, 1.0);
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
        pen.setWidthF(1.0);
        p.setPen(pen);
        p.setBrush(Qt::NoBrush);
        p.drawPolyline(curve);
        p.restore();
    }

    Sample m_samples[ResourceCount];
    int m_maxPoints = 60;
    int m_hoveredResource = -1;
};

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

class RelaySpeedDot : public QWidget
{
public:
    explicit RelaySpeedDot(QWidget *parent = nullptr) : QWidget(parent)
    {
        setFixedSize(kWidth, kHeight);
        setAttribute(Qt::WA_TransparentForMouseEvents);
        if (parent)
            parent->installEventFilter(this);
        reposition();
    }

    void setLatency(int ms)
    {
        ms = qMax(0, ms);
        if (!m_unreachable && m_latencyMs == ms)
            return;
        m_latencyMs = ms;
        m_unreachable = false;
        update();
    }

    void setUnreachable()
    {
        if (m_unreachable)
            return;
        m_unreachable = true;
        update();
    }

    int latencyMs() const { return m_latencyMs; }
    bool unreachable() const { return m_unreachable; }

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

        const QRectF plate(rect());
        const qreal radius = plate.height() / 2.0;
        QColor wash = colour;
        wash.setAlpha(52);
        p.setPen(Qt::NoPen);
        p.setBrush(palette().color(QPalette::Window));
        p.drawRoundedRect(plate, radius, radius);
        p.setBrush(wash);
        p.drawRoundedRect(plate, radius, radius);

        const QPointF centre = plate.center();
        QPolygonF caret;
        caret << QPointF(centre.x() - 3.0, centre.y() - 1.6)
              << QPointF(centre.x() + 3.0, centre.y() - 1.6)
              << QPointF(centre.x(), centre.y() + 2.4);
        p.setBrush(colour);
        p.drawPolygon(caret);
    }

private:
    void reposition()
    {
        if (QWidget *owner = parentWidget())
            move(qMax(0, owner->width() - width()),
                 qMax(0, owner->height() - height()));
    }

    static constexpr int kWidth = 14;
    static constexpr int kHeight = 10;
    int m_latencyMs = -1;       // last measured round-trip; -1 = unknown/probing
    bool m_unreachable = false; // relay failed to answer the last probe
};

namespace ChromeDotGrid {

constexpr int kRows = 3;
constexpr int kPitch = 7;
constexpr int kGridHeight = kRows * kPitch;

inline QFont captionFont()
{
    static const QFont font = [] {
        QFont f = QGuiApplication::font();
        f.setWeight(QFont::Normal);
        f.setPixelSize(8); // as small as the caption can go and still read
        return f;
    }();
    return font;
}

inline int captionHeight()
{
    static const int height = QFontMetrics(captionFont()).height() + 3;
    return height;
}

inline int captionWidth(const QString &text)
{
    return QFontMetrics(captionFont()).horizontalAdvance(text);
}

inline int totalHeight() { return kGridHeight + captionHeight(); }

inline void paintCaption(QPainter &p, const QWidget *widget,
                         const QString &text)
{
    QColor ink = widget->palette().color(QPalette::WindowText);
    ink.setAlpha(170);
    p.save();
    p.setPen(ink);
    p.setFont(captionFont());
    p.drawText(QRect(0, kGridHeight, widget->width(), captionHeight()),
               Qt::AlignLeft | Qt::AlignVCenter, text);
    p.restore();
}

inline int widthFor(int columns, const QString &caption)
{
    return columns <= 0 ? 0 : qMax(columns * kPitch, captionWidth(caption));
}

} // namespace ChromeDotGrid

class AgentDotMatrix : public QWidget
{
public:
    struct Dot {
        int sessionId = 0;
        QColor color;
        bool running = false;
        double intensity = 0.0; // 0..1 live-output (bytes) meter
        double throughput = 0.0;
    };

    static constexpr const char *kCaption = "Agents";

    explicit AgentDotMatrix(QWidget *parent = nullptr) : QWidget(parent)
    {
        setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
        setFixedHeight(ChromeDotGrid::totalHeight());
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

    void setDots(const QVector<Dot> &dots)
    {
        m_dots = dots;
        const int columns = (m_dots.size() + kRows - 1) / kRows;
        setFixedWidth(
            ChromeDotGrid::widthFor(columns, QLatin1String(kCaption)));
        bool anyRunning = false;
        for (const Dot &d : std::as_const(m_dots))
            anyRunning = anyRunning || d.running;
        if (anyRunning && !m_sweep->isActive())
            m_sweep->start();
        else if (!anyRunning && m_sweep->isActive())
            m_sweep->stop();
        update();
    }

    int shownCount() const { return m_dots.size(); }

    std::function<void(int)> onDotClicked;

protected:
    void mousePressEvent(QMouseEvent *e) override
    {
        if (e->button() == Qt::LeftButton && onDotClicked) {
            const int index = dotAt(e->position().toPoint());
            onDotClicked(index >= 0 ? m_dots.at(index).sessionId : 0);
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
        ChromeDotGrid::paintCaption(p, this, QLatin1String(kCaption));
    }

private:
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

    static constexpr int kRows = ChromeDotGrid::kRows;   // squares per column
    static constexpr int kPitch = ChromeDotGrid::kPitch; // cell, including gap
    static constexpr double kSide = 4.5;   // painted square
    static constexpr double kSweepStep = 0.06; // per-square offset of the sweep

    QVector<Dot> m_dots;
    double m_phase = 0.0;      // 0..1 Larson sweep parameter
    QTimer *m_sweep = nullptr; // only ticks while something is running
};

class NodeDotMatrix : public QWidget
{
public:
    struct Dot {
        QString name;
        QColor color;
        bool self = false; // this machine, ringed so it's findable
    };

    static constexpr const char *kCaption = "Nodes";

    explicit NodeDotMatrix(QWidget *parent = nullptr) : QWidget(parent)
    {
        setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
        setFixedHeight(ChromeDotGrid::totalHeight());
        setFixedWidth(0); // nothing to show until the first setDots()
        setCursor(Qt::PointingHandCursor);
        hide();
    }

    void setDots(const QVector<Dot> &dots)
    {
        m_dots = dots.mid(0, kRows * kMaxColumns);
        const int columns = (m_dots.size() + kRows - 1) / kRows;
        setFixedWidth(
            ChromeDotGrid::widthFor(columns, QLatin1String(kCaption)));
        update();
    }

    int shownCount() const { return m_dots.size(); }

    std::function<void(const QString &)> onDotClicked;

protected:
    void mousePressEvent(QMouseEvent *e) override
    {
        if (e->button() == Qt::LeftButton && onDotClicked) {
            const int index = dotAt(e->position().toPoint());
            onDotClicked(index >= 0 ? m_dots.at(index).name : QString());
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
                QColor ring = palette().color(QPalette::WindowText);
                ring.setAlpha(190);
                p.setBrush(Qt::NoBrush);
                p.setPen(QPen(ring, 0.9));
                p.drawEllipse(center, kRadius + 1.0, kRadius + 1.0);
            }
        }
        ChromeDotGrid::paintCaption(p, this, QLatin1String(kCaption));
    }

private:
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

    static constexpr int kRows = ChromeDotGrid::kRows;   // dots per column
    static constexpr int kPitch = ChromeDotGrid::kPitch; // cell, including gap
    static constexpr double kRadius = 2.3; // painted dot
    static constexpr int kMaxColumns = 12; // ~36 nodes before the tooltip takes over

    QVector<Dot> m_dots;
};

class ActionRunStrip : public QWidget
{
public:
    struct Cell {
        int runId = 0;
        QColor color;
        bool running = false;
    };

    static constexpr int kRows = ChromeDotGrid::kRows; // squares per column
    static constexpr int kMaxColumns = 6; // before the tooltip takes over
    static constexpr int kMaxCells = kRows * kMaxColumns;
    static constexpr const char *kCaption = "Actions";

    explicit ActionRunStrip(QWidget *parent = nullptr) : QWidget(parent)
    {
        setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
        setFixedHeight(ChromeDotGrid::totalHeight());
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

    void setCells(const QVector<Cell> &cells)
    {
        m_cells = cells.mid(0, kMaxCells);
        const int columns = (m_cells.size() + kRows - 1) / kRows;
        setFixedWidth(
            ChromeDotGrid::widthFor(columns, QLatin1String(kCaption)));
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

    std::function<void(int)> onCellClicked;

protected:
    void mousePressEvent(QMouseEvent *e) override
    {
        if (e->button() == Qt::LeftButton && onCellClicked) {
            const int index = cellAt(e->position().toPoint());
            onCellClicked(index >= 0 ? m_cells.at(index).runId : 0);
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
                color.setAlpha(205); // same weight as an idle agent square
            }
            const QPointF center = cellCenter(i);
            p.setBrush(color);
            p.drawRoundedRect(QRectF(center.x() - kSide / 2.0,
                                     center.y() - kSide / 2.0, kSide, kSide),
                              1.2, 1.2);
        }
        ChromeDotGrid::paintCaption(p, this, QLatin1String(kCaption));
    }

private:
    QPointF cellCenter(int index) const
    {
        const int column = index / kRows;
        const int row = index % kRows;
        return QPointF(column * kPitch + kPitch / 2.0,
                       row * kPitch + kPitch / 2.0);
    }

    int cellAt(const QPoint &pos) const
    {
        const int column = pos.x() / kPitch;
        const int row = pos.y() / kPitch;
        if (column < 0 || row < 0 || row >= kRows)
            return -1;
        const int index = column * kRows + row;
        return index < m_cells.size() ? index : -1;
    }

    static constexpr int kPitch = ChromeDotGrid::kPitch; // cell, including gap
    static constexpr double kSide = 4.5; // painted square
    static constexpr double kPulseStep = 0.09; // per-square offset of the pulse

    QVector<Cell> m_cells;
    double m_phase = 0.0;
    QTimer *m_pulse = nullptr; // only ticks while a run is in flight
};

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

struct MirrorNodeDot
{
    QString id;
    QString name;
    bool online = false;
    bool self = false;
    bool behind = false;
    bool integrityFailing = false;
};

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
        opt.state &= ~QStyle::State_MouseOver;
        const bool sameRow = m_hovered.isValid() &&
                             index.row() == m_hovered.row() &&
                             index.parent() == m_hovered.parent();
        if (m_hoverFill && sameRow && !(option.state & QStyle::State_Selected))
            painter->fillRect(option.rect, QColor(46, 160, 67, 55)); // light green
        opt.state &= ~QStyle::State_Selected;
        QStyledItemDelegate::paint(painter, opt, index);
        paintRowSelectionBorder(painter, option, index);
    }

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

inline void enableHoverRowHighlight(QAbstractItemView *view)
{
    if (!view)
        return;
    view->setItemDelegate(new HoverRowDelegate(view));
    blankSelectionBand(view);
}

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

inline void paintRowSelectionBorder(QPainter *painter,
                                    const QStyleOptionViewItem &option,
                                    const QModelIndex &index)
{
    if (!(option.state & QStyle::State_Selected))
        return;
    paintRowBorder(painter, option, index, QColor(46, 160, 67)); // #2ea043 green
}

class SelectionBorderRowDelegate : public HoverRowDelegate
{
public:
    explicit SelectionBorderRowDelegate(QAbstractItemView *view)
        : HoverRowDelegate(view)
    {
        m_hoverFill = false;
    }
};

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
            QTimer::singleShot(0, table, [table, header, keepFlexibleColumn]() {
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
                    header->setSectionResizeMode(i, QHeaderView::Interactive);
                    table->resizeColumnToContents(i);
                }
            });
        });
}


inline int progressPctForX(const QRect &cellRect, int x, const QFontMetrics &fm)
{
    const QRect cell = cellRect.adjusted(8, 0, -8, 0);
    const int textW = fm.horizontalAdvance(QStringLiteral("100%")) + 4;
    const int barW = qMax(1, cell.width() - textW);
    return qBound(0, qRound((x - cell.left()) * 100.0 / barW), 100);
}

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
        HoverRowDelegate::paint(painter, option, index);
        const QVariant value = index.data(kProgressBarRole);
        if (!value.isValid())
            return;
        paintProgressBar(painter, option.rect, value.toInt(), option.fontMetrics);
    }
};

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

class ResourceBarDelegate : public HoverRowDelegate
{
public:
    using HoverRowDelegate::HoverRowDelegate;

    bool mutedSelectionBand = false;

    QSize sizeHint(const QStyleOptionViewItem &option,
                   const QModelIndex &index) const override
    {
        QSize base = HoverRowDelegate::sizeHint(option, index);
        return QSize(qMax(base.width(), 84), qMax(base.height(), 18));
    }

    void paint(QPainter *painter, const QStyleOptionViewItem &option,
               const QModelIndex &index) const override
    {
        QStyleOptionViewItem opt(option);
        if (mutedSelectionBand && (option.state & QStyle::State_Selected)) {
            painter->fillRect(option.rect, currentThemeIsDark()
                                               ? QColor("#21262d")
                                               : QColor("#eaeef2"));
            opt.state &= ~QStyle::State_Selected;
        }
        HoverRowDelegate::paint(painter, opt, index);
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
// column's name. A plain callback avoids needing Q_OBJECT/moc in this.cpp.
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

    std::function<void(int number, const QString &column)> onDrop;

protected:
    void dropEvent(QDropEvent *event) override
    {
        auto *src = qobject_cast<QListWidget *>(event->source());
        QListWidgetItem *item = src ? src->currentItem() : nullptr;
        if (src && src != this && item && onDrop) {
            const int number = item->data(Qt::UserRole).toInt();
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
        HoverRowDelegate::paint(painter, option, index);
        const QVariant anchor = index.data(kPacmanAnchorRole);
        if (!anchor.isValid())
            return;
        const qint64 intervalMs = qMax(1LL, mirrorSyncIntervalMs());
        qint64 elapsed =
            (QDateTime::currentMSecsSinceEpoch() - anchor.toLongLong()) %
            intervalMs;
        if (elapsed < 0)
            elapsed += intervalMs;
        const double frac =
            qBound(0.0, double(elapsed) / double(intervalMs), 1.0);

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

inline SortTableWidgetItem *makeResourceBarCell(int pct, const QString &tooltip)
{
    auto *item = new SortTableWidgetItem(QString());
    item->setData(kProgressBarRole, pct);
    item->setData(kTableSortRole, double(pct)); // unknown (-1) sorts below 0%
    if (!tooltip.isEmpty())
        item->setToolTip(tooltip);
    return item;
}

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

inline QString nodeHealthColor(int severity)
{
    switch (severity) {
    case NodeDiagnostics::Critical:
        return QStringLiteral("#f85149");
    case NodeDiagnostics::Warning:
        return QStringLiteral("#d29922");
    case NodeDiagnostics::Info:
        return QStringLiteral("#58a6ff");
    default:
        return QStringLiteral("#3fb950"); // all clear
    }
}

inline SortTableWidgetItem *makeNodeHealthCell(
    const QList<NodeDiagnostics::Finding> &findings, qint64 diagnosticsMs,
    qint64 nowMs)
{
    const bool reported = diagnosticsMs > 0;
    auto *item = new SortTableWidgetItem(
        NodeDiagnostics::summaryLabel(findings, reported));
    const int severity =
        reported ? NodeDiagnostics::worstSeverity(findings) : -1;
    item->setData(kTableSortRole, double(severity));
    if (!reported) {
        item->setToolTip(QStringLiteral(
            "This node has not reported a self-check — an older build, or "
            "self-diagnostics turned off in its settings."));
        return item;
    }
    item->setForeground(QColor(nodeHealthColor(severity)));
    QString tip = findings.isEmpty()
                      ? QStringLiteral("Self-check found no problems.")
                      : NodeDiagnostics::detailText(findings);
    const qint64 ageMs = nowMs > 0 ? nowMs - diagnosticsMs : 0;
    if (ageMs > 60 * 1000)
        tip += QStringLiteral("\n\nLast reported %1 min ago")
                   .arg(ageMs / (60 * 1000));
    item->setToolTip(tip);
    return item;
}

inline SortTableWidgetItem *makeCpuUsageCell(double cpuPercent)
{
    if (cpuPercent < 0.0)
        return makeResourceBarCell(-1, QString());
    const int pct = int(qRound(qBound(0.0, cpuPercent, 100.0)));
    return makeResourceBarCell(
        pct, QStringLiteral("CPU: %1% busy across all cores").arg(pct));
}

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
// speaks as "Guest ####" like the website does for anonymous visitors
// Renaming (setup screen or Settings) or claiming a user account makes
// account/nodeName diverge from this and thereby exits guest mode.
const QString kGeneratedNodeNameSetting =
    QStringLiteral("account/generatedNodeName");
const QString kMachineNodeNameSetting = QStringLiteral("node/machineName");
const QString kHostsSetting = QStringLiteral("hosts/list");
const QString kMirrorFleetEnabledSetting =
    QStringLiteral("hosts/healthyMirrorFleet/enabled");
const QString kMirrorFleetDesiredSetting =
    QStringLiteral("hosts/healthyMirrorFleet/desired");
constexpr int kMirrorFleetCheckIntervalMs = 5 * 60 * 1000;
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
const QString kWorldDevUrlSetting = QStringLiteral("world/localDevUrl");
// Last account this node key authenticated as; lets the app start offline once a
// registered account has been confirmed at least once on this machine.
const QString kAuthedAccountSetting = QStringLiteral("account/authedName");
const QString kRecentUserAccountsSetting =
    QStringLiteral("account/recentUsers");
const QString kRecentUserInstancesSetting =
    QStringLiteral("account/recentUserInstances");
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
const QString kLastRepoDetailTabSetting =
    QStringLiteral("repositories/lastOpenDetailTab");
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
const QString kNodeOfflineSetting = QStringLiteral("stats/nodeOffline");
const QString kThemeSetting = QStringLiteral("app/theme"); // system | dark | light
const QString kPushAlertSetting = QStringLiteral("actions/pushAlert");
const QString kActionAlertSetting = QStringLiteral("actions/runAlert");
const QString kActionAlertStartedSetting =
    QStringLiteral("actions/runAlertStarted");
const QString kActionAlertModeSetting = QStringLiteral("actions/runAlertMode");

inline QString actionAlertMode()
{
    QSettings settings;
    const QString mode = settings.value(kActionAlertModeSetting).toString();
    if (mode == QLatin1String("all") || mode == QLatin1String("failed") ||
        mode == QLatin1String("none"))
        return mode;
    return settings.value(kActionAlertSetting, false).toBool()
               ? QStringLiteral("all")
               : QStringLiteral("none");
}
const QString kNodeConnectAlertSetting = QStringLiteral("notifications/nodeConnect");
const QString kDisbursementAlertSetting = QStringLiteral("notifications/disbursement");
const QString kChatMessageAlertSetting = QStringLiteral("notifications/chatMessages");
const QString kMentionAlertSetting = QStringLiteral("notifications/mentions");
const QString kIssueAlertSetting = QStringLiteral("notifications/issues");
const QString kPullAlertSetting = QStringLiteral("notifications/pullRequests");
const QString kCommentAlertSetting = QStringLiteral("notifications/comments");
const QString kMirrorUpdateAlertSetting = QStringLiteral("notifications/mirrorUpdated");
const QString kCoveOpenAlertSetting = QStringLiteral("notifications/coveOpened");
const QString kNewUserAlertSetting = QStringLiteral("notifications/newUser");
const QString kInAppNotificationsSetting = QStringLiteral("notifications/inAppCards");
const QString kInAppNotificationDurationSetting =
    QStringLiteral("notifications/inAppCardDurationSeconds");
const QString kErrorLogAlertSetting = QStringLiteral("notifications/errorLogFlash");
const QString kSystemAlertSetting = QStringLiteral("notifications/systemAlert");
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
const QString kWelcomeChannel = QStringLiteral("#welcome");
const qint64 kWelcomePingFreshMs = 5 * 60 * 1000;
const QString kLegacyWelcomeAnnouncedSettingPrefix =
    QStringLiteral("chat/welcomeAnnounced/");

inline bool notifyEnabled(const QString &key)
{
    return QSettings().value(key, false).toBool();
}
const QString kAutoPrBountyEnabledSetting =
    QStringLiteral("bounty/autoPrEnabled");
const QString kAutoPrBountyAmountSetting =
    QStringLiteral("bounty/autoPrAmountUsd");
const QString kAutoPrBountyModeSetting = QStringLiteral("bounty/autoPrMode");
const QString kSolanaLastBalanceSettingPrefix =
    QStringLiteral("profile/solanaLastBalance/");
const QString kWindowGeometrySetting = QStringLiteral("ui/windowGeometry");
const QString kShowRebuildButtonSetting = QStringLiteral("ui/showRebuildButton");
const QString kShowDebugBarOnStartupSetting =
    QStringLiteral("ui/showDebugBarOnStartup");
const QString kCloudLogMonitorSetting =
    QStringLiteral("diagnostics/cloudLogMonitor");
constexpr int kCloudLogMonitorStartupDelayMs = 5000;
const QString kVerboseNetworkLogSetting =
    QStringLiteral("diagnostics/verboseNetworkLog");
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
const QString kAutoSyncIssuesSetting = QStringLiteral("repos/autoSyncIssues");
const QString kAutoSyncOnMergeSetting = QStringLiteral("repos/autoSyncOnMerge");
const QString kAutoUpdateSetting = QStringLiteral("update/autoUpdate");
const QString kAutoUpdateRestartSetting = QStringLiteral("update/autoUpdateRestart");
const QString kAutoBackupEnabledSetting = QStringLiteral("backup/hourlyEnabled");
const QString kAutoBackupKeepSetting = QStringLiteral("backup/keepCount");
const QString kAutoAgentOnStallSetting =
    QStringLiteral("diagnostics/autoAgentOnStall");
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
const QString kIdeIntegrationSetting = QStringLiteral("ide/integrationEnabled");
const QString kWhisperDirSetting = QStringLiteral("voice/whisperDir");
const QString kWhisperModelSetting = QStringLiteral("voice/whisperModel");
// Public World origin allowed to control local speech-to-text. This is not a
// capability; one-use/session secrets are memory-only inside WorldSpeechBridge.
const QString kWorldSpeechOriginSetting =
    QStringLiteral("voice/worldExactOrigin");
const QString kVoiceEngineSetting = QStringLiteral("voice/engine");
const QString kParakeetModelSetting = QStringLiteral("voice/parakeetModel");
const QString kVoiceInputDeviceSetting = QStringLiteral("voice/inputDevice");
const QString kCodexModelSetting = QStringLiteral("agents/codexModel");
const QString kIssueAskAiModel = QStringLiteral("gpt-4.1-nano");
const QString kQuickAddHistorySetting = QStringLiteral("issues/quickAddHistory");
const QString kQuickAddCreateIssueSetting =
    QStringLiteral("issues/quickAddCreateIssue");
// Genie: the website's remote-MCP setup, as generated by
// Organization Admin → "Remote ForkMesh MCP". The token is the revocable
// task-only bearer credential; the org names whose shared task list the agent
// works; the workflow picks the finishing sequence ("pr" or "deploy"). The URL
// defaults to the active relay's /mcp endpoint when left blank.
const QString kGenieTokenSetting = QStringLiteral("genie/token");
const QString kGenieOrgSetting = QStringLiteral("genie/org");
const QString kGenieWorkflowSetting = QStringLiteral("genie/workflow");
const QString kGenieUrlSetting = QStringLiteral("genie/mcpUrl");
const QString kClaudeApiKeySetting = QStringLiteral("agents/claudeApiKey");
const QString kClaudeAdminKeySetting = QStringLiteral("agents/claudeAdminKey");
const QString kCodexCommandSetting = QStringLiteral("agents/codexCommand");
const QString kClaudeCommandSetting = QStringLiteral("agents/claudeCommand");
const QString kAgentContextSetting = QStringLiteral("agents/contextWindow");
const QString kAgentMaxOutputSetting = QStringLiteral("agents/maxOutputTokens");
const QString kAgentPromptPreambleSetting =
    QStringLiteral("agents/promptPreamble");
const QString kPrioritizePromptSetting =
    QStringLiteral("agents/prioritizePrompt");
const QString kOpenAiSpendTextSetting = QStringLiteral("agents/openAiSpendText");
const QString kOpenAiSpendTsSetting = QStringLiteral("agents/openAiSpendTs");
const QString kClaudeSpendTextSetting = QStringLiteral("agents/claudeSpendText");
const QString kClaudeSpendTsSetting = QStringLiteral("agents/claudeSpendTs");
const QString kOpenAiCreditTextSetting = QStringLiteral("agents/openAiCreditText");
const QString kOpenAiCreditTsSetting = QStringLiteral("agents/openAiCreditTs");
const QString kClaudeCreditTextSetting = QStringLiteral("agents/claudeCreditText");
const QString kClaudeCreditTsSetting = QStringLiteral("agents/claudeCreditTs");
const QString kCodexLimit5hStartSetting = QStringLiteral("agents/codexLimit5hStart");
const QString kCodexLimitWeekStartSetting = QStringLiteral("agents/codexLimitWeekStart");
const QString kCodexUsage5hPctSetting = QStringLiteral("agents/codexUsage5hPct");
const QString kCodexUsageWeekPctSetting = QStringLiteral("agents/codexUsageWeekPct");
const QString kCodexUsage5hResetSetting = QStringLiteral("agents/codexUsage5hReset");
const QString kCodexUsageWeekResetSetting = QStringLiteral("agents/codexUsageWeekReset");
const QString kClaudeLimit5hStartSetting = QStringLiteral("agents/claudeLimit5hStart");
const QString kClaudeLimitWeekStartSetting = QStringLiteral("agents/claudeLimitWeekStart");
const QString kClaudeUsage5hPctSetting = QStringLiteral("agents/claudeUsage5hPct");
const QString kClaudeUsageWeekPctSetting = QStringLiteral("agents/claudeUsageWeekPct");
const QString kClaudeUsage5hResetSetting = QStringLiteral("agents/claudeUsage5hReset");
const QString kClaudeUsageWeekResetSetting = QStringLiteral("agents/claudeUsageWeekReset");
const QString kClaudeUsageFablePctSetting = QStringLiteral("agents/claudeUsageFablePct");
const QString kClaudeUsageFableResetSetting = QStringLiteral("agents/claudeUsageFableReset");
struct AgentAccountProfile {
    QString id;
    QString label;
    QString configDir;
    bool builtIn = false;
};

inline QString agentAccountProviderKey(const QString &provider)
{
    return provider == QLatin1String("codex") ? QStringLiteral("codex")
                                               : QStringLiteral("claude");
}

inline QString agentAccountDefaultConfigDir(const QString &provider)
{
    const bool codex =
        agentAccountProviderKey(provider) == QLatin1String("codex");
    const QString configured =
        qEnvironmentVariable(codex ? "CODEX_HOME" : "CLAUDE_CONFIG_DIR")
            .trimmed();
    if (!configured.isEmpty())
        return QDir::cleanPath(configured);
    return QDir::homePath() +
           (codex ? QStringLiteral("/.codex") : QStringLiteral("/.claude"));
}

inline QString agentAccountCredentialPath(const QString &provider,
                                          const QString &configDir)
{
    return QDir(configDir).filePath(
        agentAccountProviderKey(provider) == QLatin1String("codex")
            ? QStringLiteral("auth.json")
            : QStringLiteral(".credentials.json"));
}

inline QString agentAccountProfilesGroup(const QString &provider)
{
    return QStringLiteral("agents/accountProfiles/") +
           agentAccountProviderKey(provider);
}

inline QString agentActiveAccountSetting(const QString &provider)
{
    return QStringLiteral("agents/activeAccount/") +
           agentAccountProviderKey(provider);
}

inline QString agentAccountLabelSetting(const QString &provider,
                                        const QString &accountId)
{
    return QStringLiteral("agents/accountLabels/%1/%2")
        .arg(agentAccountProviderKey(provider), accountId);
}

inline QList<AgentAccountProfile> agentAccountProfiles(const QString &provider)
{
    QSettings settings;
    const QString defaultLabel =
        settings.value(agentAccountLabelSetting(provider, QStringLiteral("default")),
                       QStringLiteral("Default account"))
            .toString()
            .trimmed();
    QList<AgentAccountProfile> profiles{
        {QStringLiteral("default"),
         defaultLabel.isEmpty() ? QStringLiteral("Default account") : defaultLabel,
         agentAccountDefaultConfigDir(provider), true}};
    settings.beginGroup(agentAccountProfilesGroup(provider));
    const QStringList ids = settings.childGroups();
    for (const QString &id : ids) {
        settings.beginGroup(id);
        const QString dir = settings.value(QStringLiteral("configDir")).toString();
        if (!dir.trimmed().isEmpty()) {
            profiles.append({id,
                             settings.value(QStringLiteral("label"),
                                            QStringLiteral("Account"))
                                 .toString(),
                             dir, false});
        }
        settings.endGroup();
    }
    settings.endGroup();
    return profiles;
}

inline AgentAccountProfile activeAgentAccount(const QString &provider)
{
    const QList<AgentAccountProfile> profiles = agentAccountProfiles(provider);
    const QString selected =
        QSettings().value(agentActiveAccountSetting(provider),
                          QStringLiteral("default")).toString();
    for (const AgentAccountProfile &profile : profiles)
        if (profile.id == selected)
            return profile;
    return profiles.first();
}

inline QString agentAccountUsageSetting(const QString &provider,
                                        const QString &accountId,
                                        const QString &field)
{
    return QStringLiteral("agents/accountUsage/%1/%2/%3")
        .arg(agentAccountProviderKey(provider), accountId, field);
}

inline QString agentAccountUsageField(const QString &globalSetting)
{
    return globalSetting.section(QLatin1Char('/'), -1);
}

inline QStringList activeAgentAccountEnv(const QString &provider)
{
    const AgentAccountProfile profile = activeAgentAccount(provider);
    if (profile.builtIn)
        return {};
    return {QStringLiteral("%1=%2")
                .arg(agentAccountProviderKey(provider) == QLatin1String("codex")
                         ? QStringLiteral("CODEX_HOME")
                         : QStringLiteral("CLAUDE_CONFIG_DIR"),
                     profile.configDir)};
}
constexpr qint64 kAgentLimit5hMs = 5LL * 60 * 60 * 1000;
constexpr qint64 kAgentLimitWeekMs = 7LL * 24 * 60 * 60 * 1000;
const QString kClaudeUsage5hExhaustedSetting = QStringLiteral("agents/claudeUsage5hExhausted");
const QString kClaudeUsageWeekExhaustedSetting = QStringLiteral("agents/claudeUsageWeekExhausted");
const QString kClaudeUsageFableExhaustedSetting = QStringLiteral("agents/claudeUsageFableExhausted");
const QString kCodexUsage5hExhaustedSetting = QStringLiteral("agents/codexUsage5hExhausted");
const QString kCodexUsageWeekExhaustedSetting = QStringLiteral("agents/codexUsageWeekExhausted");
const QString kEmailOnCreditsRefillSetting = QStringLiteral("agents/emailOnCreditsRefill");
const QString kUsageLimitCalendarReminderSetting =
    QStringLiteral("agents/usageLimitCalendarReminder");
const QString kUsageLimitReminderScheduledPrefix =
    QStringLiteral("agents/usageLimitReminderScheduled/");
const QString kUsageLimitReminderNotifiedPrefix =
    QStringLiteral("agents/usageLimitReminderNotified/");

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
const QString kLegacyClaudeCommand =
    QStringLiteral("claude -p \"$(cat {promptFile})\" --dangerously-skip-permissions");
const QString kClaudeCodeCommandSetting = QStringLiteral("agents/claudeCodeCommand");
const QString kClaudeCodeModelSetting = QStringLiteral("agents/claudeCodeModel");
const QString kReleaseNotesClaudeModelSetting =
    QStringLiteral("agents/releaseNotesClaudeModel");
const QString kReleaseNotesGptModelSetting =
    QStringLiteral("agents/releaseNotesGptModel");
const QString kClaudeModelsCacheSetting = QStringLiteral("agents/claudeModelsCache");
const QString kCodexModelsCacheSetting = QStringLiteral("agents/codexModelsCache");
const QString kCloudflareAiModelsCacheSetting =
    QStringLiteral("agents/cloudflareAiModelsCache");
const QString kCloudflareAiModelSetting =
    QStringLiteral("agents/cloudflareAiModel");
// Composer "Auto mode" toggle: true => run Claude Code unattended (skip the
// permission prompts). Read when a transcript session launches.
const QString kClaudeAutoModeSetting = QStringLiteral("agents/claudeAutoMode");
const QString kAgentModeSetting = QStringLiteral("agents/cliPermissionMode");
// The composer mode-selector labels. One word each so the whole
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
const QString kClaudeEffortSetting = QStringLiteral("agents/claudeEffort");
const QString kClaudeEffortLevelsCacheSetting =
    QStringLiteral("agents/claudeEffortLevels");
inline QStringList defaultAgentEffortLevels()
{
    return {QStringLiteral("low"), QStringLiteral("medium"),
            QStringLiteral("high"), QStringLiteral("xhigh"),
            QStringLiteral("max")};
}
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
const QString kClaudeThinkingSetting = QStringLiteral("agents/claudeThinking");
const QString kClaudeFallbackModelSetting =
    QStringLiteral("agents/claudeFallbackModel");
const QString kAutoSwitchToAgentSetting = QStringLiteral("agents/autoSwitchToAgent");
// When on, agent sessions started here are owner-encrypted before their opaque
// snapshots reach the relay. The browser deliberately has no recipient private
// key; inspection and steering remain on the owner desktop. Off by default so
// no agent metadata leaves this machine unless the user opts in.
const QString kPublishAgentsToWebSetting =
    QStringLiteral("agents/publishToWeb");
const QString kAutoFixFailuresSetting =
    QStringLiteral("agents/autoFixFailures");
const QString kExcludeExternalClaudeSetting =
    QStringLiteral("agents/excludeExternalClaude");
const QString kAgentJailSetting = QStringLiteral("agents/jailEnabled");
const QString kAgentJailMemoryMbSetting = QStringLiteral("agents/jailMemoryMb");
constexpr int kDefaultAgentJailMemoryMb = 4096;
constexpr int kMinAgentJailMemoryMb = 256;

inline int agentJailMemoryMb()
{
    return qMax(kMinAgentJailMemoryMb,
                QSettings()
                    .value(kAgentJailMemoryMbSetting, kDefaultAgentJailMemoryMb)
                    .toInt());
}
const QString kMaxRunningAgentsSetting = QStringLiteral("agents/maxRunning");
constexpr int kDefaultMaxRunningAgents = 5;
constexpr int kMinMaxRunningAgents = 1;

inline int maxRunningAgents()
{
    return qMax(kMinMaxRunningAgents,
                QSettings()
                    .value(kMaxRunningAgentsSetting, kDefaultMaxRunningAgents)
                    .toInt());
}
const QString kVoiceAutoSubmitSetting = QStringLiteral("agents/voiceAutoSubmit");
const QString kOrganizationTaskOpenCountSetting =
    QStringLiteral("tasks/openCount");
// Canonical prefixes this desktop signs with its account key to open, update,
// and close an organization task when it has no account session token to present (the
// authenticateSilently path holds keys, not sessions). Must stay byte-identical
// to ORG_TASK_OPEN_PROOF / ORG_TASK_COMPLETE_PROOF in the worker's entry.py.
const QString kOrgTaskOpenProof = QStringLiteral("forkmesh-org-task-open-v1");
const QString kOrgTaskCompleteProof =
    QStringLiteral("forkmesh-org-task-complete-v1");
// Must match ORG_TASK_AGENT_STATUS_BATCH_PROOF in entry.py byte-for-byte.
const QString kOrgTaskAgentStatusBatchProof =
    QStringLiteral("forkmesh-org-task-agent-status-batch-v1");
constexpr int kOrgTaskAgentStatusAckTimeoutMs = 30000;
const QString kOrgTaskListProof = QStringLiteral("forkmesh-org-task-list-v1");
// Must match ORG_TASK_DELETE_PROOF in entry.py byte-for-byte.
const QString kOrgTaskDeleteProof =
    QStringLiteral("forkmesh-org-task-delete-v1");
// Must match GENIE_CREDENTIAL_PROOF in entry.py byte-for-byte.
const QString kGenieCredentialProof =
    QStringLiteral("forkmesh-genie-credential-v1");
// Must match the ACCOUNT_ALERT_*_PROOF constants in entry.py byte-for-byte.
const QString kAccountAlertListProof =
    QStringLiteral("forkmesh-account-alert-list-v1");
const QString kAccountAlertReadProof =
    QStringLiteral("forkmesh-account-alert-read-v1");
const QString kAccountAlertDeleteProof =
    QStringLiteral("forkmesh-account-alert-delete-v1");
// Clearing all website pings is broader than deleting one row, so it requires
// its own signed proof. Must stay byte-identical to ACCOUNT_ALERT_CLEAR_PROOF
// in entry.py.
const QString kAccountAlertClearProof =
    QStringLiteral("forkmesh-account-alert-clear-v1");
constexpr qint64 kWebAlertPushFloorMs = 5000;
const QString kClaudeDiffSplitSetting = QStringLiteral("agents/claudeDiffSplit");
const QString kDiffFontPtSetting = QStringLiteral("ui/diffFontPt");
const QString kDefaultClaudeCodeCommand =
    QStringLiteral("claude -p \"$(cat {promptFile})\" --dangerously-skip-permissions");
const QString kClaudeCodeTerminalCommandSetting =
    QStringLiteral("agents/claudeCodeTerminalCommand");
const QString kDefaultClaudeCodeTerminalCommand =
    QStringLiteral("claude \"$(cat {promptFile})\" --dangerously-skip-permissions");
// Prior interactive default that prompted for every permission. Migrated to the
// full-accept default above so existing sessions stop stalling on prompts.
const QString kLegacyClaudeCodeTerminalCommand =
    QStringLiteral("claude \"$(cat {promptFile})\"");
constexpr int kNetworkLogLimit = 20000;
constexpr int kNetworkLogSegmentSize = 300;
constexpr int kFooterLogSeedLines = 300;
constexpr int kDebugLogTailLines = 5;

const QString kCodexProvider = QStringLiteral("codex");

const QString kAgentDoneToastKind = QStringLiteral("agent-done");
constexpr int kAgentDoneSummaryChars = 400;
const QString kAgentDoneAlertSetting = QStringLiteral("notifications/agentDone");

inline bool agentIsClaudeProvider(const QString &provider)
{
    return provider.startsWith(QLatin1String("claude"));
}

inline bool agentIsCodexProvider(const QString &provider)
{
    return provider == kCodexProvider;
}

inline QString cliProviderLabel(const QString &provider)
{
    if (agentIsCodexProvider(provider))
        return QStringLiteral("Codex");
    if (provider == QLatin1String("claude-code"))
        return QStringLiteral("Claude Code");
    return provider;
}

inline bool agentUsesOpenAiKey(const QString &provider)
{
    return provider == QLatin1String("openai");
}

const QString kCloudflareAiProvider = QStringLiteral("cloudflare-ai");

inline bool agentIsCloudflareAiProvider(const QString &provider)
{
    return provider == kCloudflareAiProvider;
}

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
           provider == QLatin1String("claude-code") ||
           agentIsCloudflareAiProvider(provider);
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
    if (value == QLatin1String("manual"))
        return value;
    return agentProviderIsKnown(value) ? value : defaultAgentProvider();
}

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

constexpr int kAgentChoiceDescriptionRole = Qt::UserRole + 7;

constexpr int kAgentChoiceStatusRole = Qt::UserRole + 8;
inline const QString kAgentChoiceStatusOk = QStringLiteral("ok");
inline const QString kAgentChoiceStatusFailed = QStringLiteral("failed");

inline QString agentChoiceStatusGlyph(const QString &state)
{
    if (state == kAgentChoiceStatusOk)
        return QString::fromUtf8("\xE2\x9C\x93"); // ✓
    if (state == kAgentChoiceStatusFailed)
        return QString::fromUtf8("\xE2\x9C\x97"); // ✗
    return QString();
}

inline QColor agentChoiceStatusColour(const QString &state)
{
    return state == kAgentChoiceStatusOk ? QColor("#3fb950") : QColor("#f85149");
}

class FullPopupComboBox : public QComboBox {
public:
    explicit FullPopupComboBox(QWidget *parent = nullptr) : QComboBox(parent)
    {
        setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Fixed);
    }

    QSize sizeHint() const override { return currentTextSizeHint(); }
    QSize minimumSizeHint() const override { return currentTextSizeHint(); }

protected:
    void paintEvent(QPaintEvent *event) override
    {
        if (m_hintedText != currentText()) {
            m_hintedText = currentText();
            updateGeometry();
        }
        QComboBox::paintEvent(event);
    }

    QWidget *popupContainer() const
    {
        QAbstractItemView *v = view();
        if (!v)
            return nullptr;
        for (QWidget *w = v; w; w = w->parentWidget()) {
            if (w->windowFlags().testFlag(Qt::Popup))
                return w;
        }
        QWidget *top = v->window();
        if (top && top->windowFlags().testFlag(Qt::Popup))
            return top;
        return v;
    }

    QRect availableScreenRect(const QWidget *popup) const
    {
        QScreen *screen = popup ? popup->screen() : nullptr;
        if (!screen && windowHandle())
            screen = windowHandle()->screen();
        if (!screen)
            screen = QGuiApplication::primaryScreen();
        return screen ? screen->availableGeometry()
                      : QRect(QPoint(0, 0), QSize(10000, 10000));
    }

    void showPopup() override
    {
        setMaxVisibleItems(qMax(maxVisibleItems(), count()));
        QComboBox::showPopup();
        QAbstractItemView *v = view();
        if (!v || count() == 0)
            return;
        QWidget *popup = popupContainer();
        fitPopupWidth(v, popup);
        int rowsH = 0;
        for (int row = 0; row < count(); ++row) {
            int rowH = v->sizeHintForRow(row);
            if (rowH <= 0)
                rowH = fontMetrics().height() + 8;
            rowsH += rowH;
        }
        const int fullHeight = 2 * v->frameWidth() + rowsH;
        QRect geo = popup->geometry();
        const QRect avail = availableScreenRect(popup);
        const int height = qMin(fullHeight, avail.height());
        v->setVerticalScrollBarPolicy(fullHeight <= avail.height()
                                          ? Qt::ScrollBarAlwaysOff
                                          : Qt::ScrollBarAsNeeded);
        v->setMinimumHeight(height);
        popup->setMinimumHeight(height);
        if (height <= geo.height() && geo.height() >= fullHeight)
            return; // already tall enough
        geo.setHeight(height);
        if (geo.bottom() > avail.bottom())
            geo.moveBottom(avail.bottom());
        if (geo.top() < avail.top())
            geo.moveTop(avail.top());
        popup->setGeometry(geo);
    }

    void fitPopupWidth(QAbstractItemView *v, QWidget *popup)
    {
        if (!v || !popup || count() == 0)
            return;
        const QFontMetrics fm = v->fontMetrics();
        int content = 0;
        for (int row = 0; row < count(); ++row) {
            int rowW = fm.horizontalAdvance(itemText(row));
            const QString description =
                itemData(row, kAgentChoiceDescriptionRole).toString();
            if (!description.isEmpty())
                rowW += 12 + fm.horizontalAdvance(description);
            content = qMax(content, rowW);
        }
        content += v->iconSize().width() + 28;
        int width = qMax(content, v->sizeHintForColumn(0)) + 2 * v->frameWidth();
        QRect geo = popup->geometry();
        if (geo.width() >= width)
            return;
        const QRect avail = availableScreenRect(popup);
        width = qMin(width, avail.width());
        v->setMinimumWidth(width - 2 * v->frameWidth());
        popup->setMinimumWidth(width);
        geo.setWidth(width);
        if (geo.right() > avail.right())
            geo.moveRight(avail.right());
        if (geo.left() < avail.left())
            geo.moveLeft(avail.left());
        popup->setGeometry(geo);
    }

private:
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

class AgentChoiceDescriptionDelegate : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    QSize sizeHint(const QStyleOptionViewItem &option,
                   const QModelIndex &index) const override
    {
        QSize hint = QStyledItemDelegate::sizeHint(option, index);
        const QString description =
            index.data(kAgentChoiceDescriptionRole).toString();
        if (!description.isEmpty())
            hint.setWidth(hint.width() + kGap +
                          option.fontMetrics.horizontalAdvance(description));
        return hint;
    }

protected:
    void paint(QPainter *painter, const QStyleOptionViewItem &option,
               const QModelIndex &index) const override
    {
        paintWithStatusGlyph(painter, option, index);
        const QString description =
            index.data(kAgentChoiceDescriptionRole).toString();
        if (description.isEmpty())
            return;
        QStyleOptionViewItem styled(option);
        initStyleOption(&styled, index);
        const QWidget *widget = styled.widget;
        QStyle *style = widget ? widget->style() : QApplication::style();
        QRect textRect =
            style->subElementRect(QStyle::SE_ItemViewItemText, &styled, widget);
        textRect.setLeft(textRect.left() +
                         styled.fontMetrics.horizontalAdvance(styled.text) + kGap);
        if (textRect.width() <= 0)
            return;
        QColor colour = styled.palette.color(QPalette::Text);
        colour.setAlpha(160);
        painter->save();
        painter->setPen(colour);
        painter->drawText(textRect, Qt::AlignLeft | Qt::AlignVCenter,
                          styled.fontMetrics.elidedText(
                              description, Qt::ElideRight, textRect.width()));
        painter->restore();
    }

private:
    void paintWithStatusGlyph(QPainter *painter,
                              const QStyleOptionViewItem &option,
                              const QModelIndex &index) const
    {
        const QString state = index.data(kAgentChoiceStatusRole).toString();
        const QString glyph = agentChoiceStatusGlyph(state);
        QStyleOptionViewItem styled(option);
        initStyleOption(&styled, index);
        if (glyph.isEmpty() || !styled.text.endsWith(glyph)) {
            QStyledItemDelegate::paint(painter, option, index);
            return;
        }
        const QWidget *widget = styled.widget;
        QStyle *style = widget ? widget->style() : QApplication::style();
        QStyleOptionViewItem plain(styled);
        plain.text.chop(glyph.size()); // the separating space stays put
        style->drawControl(QStyle::CE_ItemViewItem, &plain, painter, widget);
        QRect glyphRect =
            style->subElementRect(QStyle::SE_ItemViewItemText, &styled, widget);
        glyphRect.setLeft(glyphRect.left() +
                          styled.fontMetrics.horizontalAdvance(plain.text));
        if (glyphRect.width() <= 0)
            return;
        painter->save();
        painter->setPen(agentChoiceStatusColour(state));
        painter->drawText(glyphRect, Qt::AlignLeft | Qt::AlignVCenter, glyph);
        painter->restore();
    }

    static constexpr int kGap = 12;
};

// Compact composer controls whose closed state is just the selected icon. The
// popup still uses the normal combo model, so opening it reveals the full icon
// + label rows (Auto / Ask / Plan / Edit or the effort ladder). This keeps the
// prompt chrome quiet without making the choices cryptic once clicked.
class IconOnlyFullPopupComboBox : public FullPopupComboBox {
public:
    explicit IconOnlyFullPopupComboBox(QWidget *parent = nullptr)
        : FullPopupComboBox(parent)
    {
        setIconSize(QSize(22, 22));
        setFixedWidth(30);
        setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
        setItemDelegate(new AgentChoiceDescriptionDelegate(this));
    }

    QSize sizeHint() const override
    {
        return QSize(30, qMax(26, FullPopupComboBox::sizeHint().height()));
    }

    QSize minimumSizeHint() const override { return sizeHint(); }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QStylePainter painter(this);
        QStyleOptionComboBox option;
        initStyleOption(&option);
        option.currentText.clear();
        option.iconSize = iconSize();
        painter.drawComplexControl(QStyle::CC_ComboBox, option);
        painter.drawControl(QStyle::CE_ComboBoxLabel, option);
    }
};

inline QIcon agentControlIcon(int index)
{
    static const char *const paths[] = {
        ":/agent-ui/icons/model-opus-face.png",
        ":/agent-ui/icons/model-sonnet-face.png",
        ":/agent-ui/icons/model-haiku-face.png",
        ":/agent-ui/icons/model-fable-face.png",
        ":/agent-ui/icons/model-sol-face.png",
        ":/agent-ui/icons/model-luna-face.png",
        ":/agent-ui/icons/model-terra-face.png",
        ":/agent-ui/icons/mode-auto.png",
        ":/agent-ui/icons/mode-ask.png",
        ":/agent-ui/icons/mode-plan.png",
        ":/agent-ui/icons/mode-edit.png",
        ":/agent-ui/icons/effort-low.png",
        ":/agent-ui/icons/effort-medium.png",
        ":/agent-ui/icons/effort-high.png",
        ":/agent-ui/icons/effort-ultra.png",
        ":/agent-ui/icons/effort-max.png",
        ":/agent-ui/icons/model-starburst.png",
        ":/agent-ui/icons/model-feather.png",
        ":/agent-ui/icons/model-mountain-blossom.png",
        ":/agent-ui/icons/model-book-quill.png",
        ":/agent-ui/icons/model-sun.png",
        ":/agent-ui/icons/model-crescent-moon.png",
        ":/agent-ui/icons/model-globe-leaf.png",
    };
    if (index < 0 || index >= 23)
        return QIcon();
    static QHash<int, QIcon> cache;
    if (const auto cached = cache.constFind(index); cached != cache.cend())
        return cached.value();
    const QIcon icon(QString::fromLatin1(paths[index]));
    cache.insert(index, icon);
    return icon;
}

const QString kClaudeAutoModelId = QStringLiteral("auto");

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

inline bool agentModelIsCloudflareStyle(const QString &model)
{
    return model.trimmed().startsWith(QLatin1String("@cf/"));
}

inline bool agentModelMatchesProvider(const QString &provider, const QString &model)
{
    if (model.trimmed().isEmpty())
        return true;
    if (agentIsCloudflareAiProvider(provider))
        return agentModelIsCloudflareStyle(model);
    if (provider == QLatin1String("claude-code") ||
        provider.startsWith(QLatin1String("claude")))
        return agentModelIsClaudeStyle(model);
    return !agentModelIsClaudeStyle(model) && !agentModelIsCloudflareStyle(model);
}

inline int agentModelPortraitIconIndex(const QString &model)
{
    static const char *const kFaces[] = {"opus", "sonnet", "haiku", "fable",
                                         "sol",  "luna",   "terra"};
    const QString m = model.trimmed().toLower();
    for (int i = 0; i < 7; ++i)
        if (m.contains(QLatin1String(kFaces[i])))
            return i;
    return -1;
}

inline int agentModelFaceIconIndex(const QString &provider, const QString &model)
{
    if (const int portrait = agentModelPortraitIconIndex(model); portrait >= 0)
        return portrait;
    constexpr int kMarkOffset = 16;
    constexpr int kSonnetMark = kMarkOffset + 1;
    constexpr int kSolMark = kMarkOffset + 4;
    constexpr int kLunaMark = kMarkOffset + 5;
    constexpr int kTerraMark = kMarkOffset + 6;
    const QString m = model.trimmed().toLower();
    const QString p = provider.trimmed().toLower();
    if (agentIsClaudeProvider(p) || agentModelIsClaudeStyle(m))
        return kSonnetMark;
    if (m.contains(QLatin1String("mini")) || m.contains(QLatin1String("nano")))
        return kLunaMark;
    if (agentIsCodexProvider(p) || m.contains(QLatin1String("codex")) ||
        agentUsesOpenAiKey(p))
        return kSolMark;
    return kTerraMark;
}

struct ClaudeAutoRoute {
    QString model;  // empty = not confident, fall through to LLM triage
    QString reason; // human-readable, shown in the transcript
};

inline ClaudeAutoRoute claudeAutoHeuristicRoute(const QString &task)
{
    const QString t = task.toLower();
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

inline void populateClaudeModelCombo(QComboBox *combo)
{
    if (!combo)
        return;
    combo->clear();
    combo->setEditable(false);
    combo->setProperty("allowAutoModel", true);
    combo->addItem(QStringLiteral("Auto"), kClaudeAutoModelId);
    for (const ClaudeAutoRung &rung : claudeAutoLadder())
        combo->addItem(rung.label, rung.id);
}

inline QString codexChatGptModelId(const QString &model)
{
    const QString trimmed = model.trimmed();
    if (trimmed.isEmpty())
        return QStringLiteral("gpt-5.5");
    if (trimmed == QLatin1String("gpt-5.3-spark"))
        return QStringLiteral("gpt-5.3-codex-spark");
    if (trimmed == QLatin1String("gpt-5.5-codex"))
        return QStringLiteral("gpt-5.5");
    if (trimmed == QLatin1String("gpt-5.4"))
        return QStringLiteral("gpt-5.4");
    if (trimmed == QLatin1String("gpt-5.4-mini") ||
        trimmed == QLatin1String("gpt-5.4-Mini"))
        return QStringLiteral("gpt-5.4-mini");
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
    QSet<QString> seen;
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
        id = codexChatGptModelId(id);
        if (!id.isEmpty() && !seen.contains(id)) {
            combo->addItem(model.value(QStringLiteral("displayName")).toString(id), id);
            seen.insert(id);
        }
    }
    if (combo->count() == 0) {
        combo->addItem(QStringLiteral("GPT-5.5"), QStringLiteral("gpt-5.5"));
        combo->addItem(QStringLiteral("GPT-5.4"), QStringLiteral("gpt-5.4"));
        combo->addItem(QStringLiteral("GPT-5.4-Mini"),
                       QStringLiteral("gpt-5.4-mini"));
    }
}

inline QVector<QPair<QString, QString>> cloudflareAiFallbackModels()
{
    return {
        {QStringLiteral("@cf/meta/llama-3.3-70b-instruct-fp8-fast"),
         QStringLiteral("Llama 3.3 70B (fast)")},
        {QStringLiteral("@cf/meta/llama-4-scout-17b-16e-instruct"),
         QStringLiteral("Llama 4 Scout 17B")},
        {QStringLiteral("@cf/google/gemma-4-26b-a4b-it"),
         QStringLiteral("Gemma 4 26B")},
        {QStringLiteral("@cf/meta/llama-3.1-8b-instruct-fast"),
         QStringLiteral("Llama 3.1 8B (fast)")},
    };
}

inline QString cloudflareAiModelLabel(const QString &model)
{
    const QString id = model.trimmed();
    if (id.isEmpty())
        return QString();
    for (const auto &choice : cloudflareAiFallbackModels()) {
        if (choice.first == id)
            return choice.second;
    }
    const QJsonArray live =
        QJsonDocument::fromJson(
            QSettings().value(kCloudflareAiModelsCacheSetting).toByteArray())
            .array();
    for (const QJsonValue &value : live) {
        const QJsonObject entry = value.toObject();
        if (entry.value(QStringLiteral("id")).toString().trimmed() != id)
            continue;
        const QString label =
            entry.value(QStringLiteral("label")).toString().trimmed();
        if (!label.isEmpty())
            return label;
    }
    return id.section(QLatin1Char('/'), -1);
}

inline void populateCloudflareAiModelCombo(QComboBox *combo)
{
    if (!combo)
        return;
    combo->clear();
    combo->setEditable(false);
    combo->setInsertPolicy(QComboBox::NoInsert);
    combo->setProperty("allowAutoModel", false);
    combo->setProperty("claudeModelCombo", false);
    const QJsonArray live =
        QJsonDocument::fromJson(
            QSettings().value(kCloudflareAiModelsCacheSetting).toByteArray())
            .array();
    for (const QJsonValue &value : live) {
        const QJsonObject entry = value.toObject();
        const QString id = entry.value(QStringLiteral("id")).toString().trimmed();
        if (id.isEmpty())
            continue;
        const QString label =
            entry.value(QStringLiteral("label")).toString().trimmed();
        combo->addItem(label.isEmpty() ? cloudflareAiModelLabel(id) : label, id);
        const QString description =
            entry.value(QStringLiteral("description")).toString().trimmed();
        if (!description.isEmpty())
            combo->setItemData(combo->count() - 1, description, Qt::ToolTipRole);
    }
    if (combo->count() > 0)
        return;
    for (const auto &choice : cloudflareAiFallbackModels())
        combo->addItem(choice.second, choice.first);
}

inline void mergeLiveCodexModels(QComboBox *combo, const QJsonArray &models)
{
    if (!combo || models.isEmpty())
        return;
    QSignalBlocker blocker(combo);
    const QString selected = codexChatGptModelId(combo->currentData().toString());
    combo->clear();
    QString defaultId;
    QSet<QString> seen;
    for (const QJsonValue &value : models) {
        const QJsonObject model = value.toObject();
        if (model.value(QStringLiteral("hidden")).toBool())
            continue;
        QString id = model.value(QStringLiteral("model")).toString().trimmed();
        if (id.isEmpty())
            id = model.value(QStringLiteral("id")).toString().trimmed();
        id = codexChatGptModelId(id);
        if (!id.isEmpty() && !seen.contains(id)) {
            combo->addItem(model.value(QStringLiteral("displayName")).toString(id), id);
            if (model.value(QStringLiteral("isDefault")).toBool())
                defaultId = id;
            seen.insert(id);
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

inline double agentModelVersionNumber(const QString &text)
{
    static const QRegularExpression version(
        QStringLiteral("(\\d+)(?:[.\\-](\\d{1,2})(?![0-9]))?"));
    const QRegularExpressionMatch match = version.match(text);
    if (!match.hasMatch())
        return 0.0;
    const double major = match.captured(1).toDouble();
    const QString minor = match.captured(2);
    return minor.isEmpty() ? major : major + minor.toDouble() / 10.0;
}

inline int agentModelPowerRank(const QString &model, const QString &label)
{
    const QString id = model.trimmed().toLower();
    const QString name = label.trimmed().toLower();
    if (id == kClaudeAutoModelId)
        return 100000;
    const QString text = id.isEmpty() ? name : id;
    double version = agentModelVersionNumber(text);
    if (version <= 0.0)
        version = agentModelVersionNumber(name);
    if (version <= 0.0)
        version = 5.0; // unknown/new: assume the current generation
    const bool claudeFamily =
        !text.startsWith(QLatin1String("gpt")) &&
        !text.startsWith(QLatin1String("o1")) &&
        !text.startsWith(QLatin1String("o3"));
    if (!claudeFamily) {
        int rank = int(version * 100.0 + 0.5);
        if (text.contains(QLatin1String("mini")) ||
            text.contains(QLatin1String("nano")) ||
            text.contains(QLatin1String("spark")))
            rank -= 10;
        return rank;
    }
    int tier = 20; // unrecognised Claude id: park it at the Sonnet tier
    if (text.contains(QLatin1String("fable")) ||
        text.contains(QLatin1String("mythos")))
        tier = 60;
    else if (text.contains(QLatin1String("opus")))
        tier = 40;
    else if (text.contains(QLatin1String("sonnet")))
        tier = 20;
    else if (text.contains(QLatin1String("haiku")))
        tier = 0;
    return 500 + int(version * 100.0 + 0.5) + tier;
}

const QString kComposerHiddenModelsSetting =
    QStringLiteral("agents/composerHiddenModels");

inline QString composerModelKey(const QString &provider, const QString &model)
{
    return provider.trimmed().toLower() + QLatin1Char('\x1f') +
           model.trimmed().toLower();
}

inline QSet<QString> hiddenComposerModels()
{
    const QStringList saved =
        QSettings().value(kComposerHiddenModelsSetting).toStringList();
    return QSet<QString>(saved.cbegin(), saved.cend());
}

inline void saveHiddenComposerModels(const QSet<QString> &hidden)
{
    QStringList keys(hidden.cbegin(), hidden.cend());
    keys.sort(); // stable on disk, so a no-op edit doesn't rewrite the file
    QSettings().setValue(kComposerHiddenModelsSetting, keys);
}

inline QString agentModelLabel(const QString &model)
{
    if (model.trimmed().isEmpty())
        return QString();
    if (model.trimmed().startsWith(QLatin1String("@cf/")))
        return cloudflareAiModelLabel(model);
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
        {QStringLiteral("gpt-5.3"), QStringLiteral("GPT-5.3")},
        {QStringLiteral("gpt-5.3-spark"), QStringLiteral("GPT-5.3 Spark")},
        {QStringLiteral("gpt-5.3-codex-spark"),
         QStringLiteral("GPT-5.3 Codex Spark")},
        {QStringLiteral("gpt-5.4"), QStringLiteral("GPT-5.4")},
        {QStringLiteral("gpt-5.4-mini"), QStringLiteral("GPT-5.4-Mini")},
        {QStringLiteral("gpt-5.5"), QStringLiteral("GPT-5.5")},
        {QStringLiteral("gpt-5.5-codex"), QStringLiteral("GPT-5.5 Codex")},
    };
    const QString id = model.trimmed();
    if (const QString mapped = kLabels.value(id); !mapped.isEmpty())
        return mapped;
    if (id.startsWith(QLatin1String("claude-"))) {
        const QStringList parts =
            id.mid(7).split(QLatin1Char('-'), Qt::SkipEmptyParts);
        const auto numeric = [](const QString &p) {
            return std::all_of(p.cbegin(), p.cend(),
                               [](QChar c) { return c.isDigit(); });
        };
        if (!parts.isEmpty() && !numeric(parts.first())) {
            QString family = parts.first();
            family[0] = family.at(0).toUpper();
            QStringList version;
            for (int i = 1; i < parts.size(); ++i) {
                if (!numeric(parts.at(i)) || parts.at(i).size() >= 8)
                    break;
                version << parts.at(i);
            }
            return version.isEmpty()
                       ? family
                       : family + QLatin1Char(' ') +
                             version.join(QLatin1Char('.'));
        }
    }
    return id;
}

inline QString agentModelShortLabel(const QString &model)
{
    const QString label = agentModelLabel(model);
    const int space = label.indexOf(QLatin1Char(' '));
    return space < 0 ? label : label.left(space);
}

// Fill an agent-provider model combo for one of the three agent providers
// (; shared by the branch "Fix with agent" bar and the Actions "Fix
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
    combo->setProperty("claudeModelCombo",
                       agentIsClaudeProvider(provider));
    if (provider == QLatin1String("claude-code")) {
        combo->addItem(QStringLiteral("Sonnet 5"), QStringLiteral("claude-sonnet-5"));
        combo->addItem(QStringLiteral("Opus 4.8"), QStringLiteral("claude-opus-4-8"));
        combo->addItem(QStringLiteral("Haiku 4.5"), QStringLiteral("claude-haiku-4-5"));
        combo->addItem(QStringLiteral("Fable 5"), QStringLiteral("claude-fable-5"));
    } else if (agentIsCodexProvider(provider)) {
        populateCodexModelCombo(combo);
    } else if (agentIsCloudflareAiProvider(provider)) {
        populateCloudflareAiModelCombo(combo);
    } else if (agentUsesOpenAiKey(provider)) {
        combo->addItem(QStringLiteral("GPT-5.5"), QStringLiteral("gpt-5.5"));
        combo->addItem(QStringLiteral("GPT-5.5 Codex"),
                       QStringLiteral("gpt-5.5-codex"));
        combo->addItem(QStringLiteral("GPT-5.3 Codex Spark"),
                       QStringLiteral("gpt-5.3-codex-spark"));
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
    const int fallback =
        combo->property("allowAutoModel").toBool() && combo->count() > 1 ? 1 : 0;
    combo->setCurrentIndex(idx >= 0 ? idx : fallback);
}

constexpr int kRepoLandingTab = 0; // Code

constexpr int kRepoAgentsTab = 3;

// Live claude.ai OAuth access token the Claude Code CLI stores in
// ~/.claude/.credentials.json. Empty when the user logged in with an API key
// (or isn't signed in). Read fresh each call so a token the CLI has rotated is
// picked up automatically.
inline QString claudeCodeOAuthToken(const QString &configDir = QString())
{
    const QString root = configDir.isEmpty()
                             ? activeAgentAccount(QStringLiteral("claude-code"))
                                   .configDir
                             : configDir;
    QFile credFile(agentAccountCredentialPath(QStringLiteral("claude-code"),
                                              root));
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

inline QString claudeAgentScriptPath(const QString &directory = QString())
{
    const QString dir = directory.isEmpty()
                            ? QStandardPaths::writableLocation(
                                  QStandardPaths::AppDataLocation) +
                                  QStringLiteral("/agents")
                            : directory;
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

inline QString defaultClaudeCommand()
{
    QString quoted = claudeAgentScriptPath();
    quoted.replace(QLatin1Char('\''), QStringLiteral("'\\''"));
    return QStringLiteral("python3 '%1' {promptFile}").arg(quoted);
}

// Materialize the bundled Cloudflare Workers AI agent script
// into the app data dir and return its path. The script drives the tool-use
// loop against the relay's POST /api/ai/agent using the signed run ticket
// AgentRunner injects as FORKMESH_AI_AGENT_AUTH, so no CLI or API key is
// required.
inline QString cloudflareAgentScriptPath(const QString &directory = QString())
{
    const QString dir = directory.isEmpty()
                            ? QStandardPaths::writableLocation(
                                  QStandardPaths::AppDataLocation) +
                                  QStringLiteral("/agents")
                            : directory;
    QDir().mkpath(dir);
    const QString path = dir + QStringLiteral("/forkmesh_cloudflare_agent.py");
    const QByteArray wanted = forkmeshCloudflareAgentScript().toUtf8();
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

inline QString defaultCloudflareAiCommand()
{
    QString quoted = cloudflareAgentScriptPath();
    quoted.replace(QLatin1Char('\''), QStringLiteral("'\\''"));
    return QStringLiteral("python3 '%1' {promptFile}").arg(quoted);
}

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

inline QString agentPromptPreamble()
{
    const QString stored =
        QSettings().value(kAgentPromptPreambleSetting).toString().trimmed();
    return stored.isEmpty() ? AgentRunner::defaultPromptPreamble() : stored;
}

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

inline QString prioritizePromptSetting()
{
    const QString stored =
        QSettings().value(kPrioritizePromptSetting).toString().trimmed();
    return stored.isEmpty() ? defaultPrioritizePrompt() : stored;
}

inline QString completenessPrompt()
{
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

inline QString runningClientDir()
{
#ifdef Q_OS_WIN
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) +
           "/src/desktop";
#else
    return QDir::homePath() + QStringLiteral("/.local/share/forkmesh/src/desktop");
#endif
}

inline QString workingClientDir()
{
    const QString baked = QStringLiteral(FORKMESH_SOURCE_DIR);
    if (!baked.isEmpty() && QDir(baked).exists("CMakeLists.txt"))
        return baked;
    return runningClientDir();
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

inline QString runningClientDirUnderHome(const QString &home)
{
#ifdef Q_OS_WIN
    Q_UNUSED(home);
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) +
           QStringLiteral("/src/desktop");
#else
    return home + QStringLiteral("/.local/share/forkmesh/src/desktop");
#endif
}

inline QString runningClientExecutableUnderHome(const QString &home)
{
#ifdef Q_OS_WIN
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) +
           QStringLiteral("/bin/forkmesh.exe");
#else
    return home + QStringLiteral("/.local/bin/forkmesh");
#endif
}

inline QString runningClientExecutable()
{
    return runningClientExecutableUnderHome(QDir::homePath());
}

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

inline int ramCappedBuildJobs()
{
    int jobs = QThread::idealThreadCount();
    const qint64 totalRam = SystemStats::totalMemoryBytes();
    if (totalRam > 0)
        jobs = qBound(1, int(totalRam / (3LL * 1024 * 1024 * 1024)), jobs);
    return jobs;
}

inline QString buildScratchDir(const QString &buildDir)
{
    return buildDir + QStringLiteral("/.forkmesh-tmp");
}

inline qint64 freeBytesForPath(const QString &path)
{
    QString probe = QDir::cleanPath(path);
    while (!probe.isEmpty() && !QFileInfo::exists(probe)) {
        const QString parent = QFileInfo(probe).absolutePath();
        if (parent == probe || parent.isEmpty())
            break;
        probe = parent;
    }
    const QStorageInfo volume(probe);
    if (!volume.isValid() || !volume.isReady())
        return -1;
    return volume.bytesAvailable();
}

inline qint64 rebuildFreeBytesRequired(const QString &buildDir)
{
    const bool incremental =
        QFileInfo::exists(buildDir + QStringLiteral("/CMakeFiles/forkmesh.dir"));
    return incremental ? 1024LL * 1024 * 1024 : 3LL * 1024 * 1024 * 1024;
}

inline QString updateFailureSummary(const QString &output)
{
    if (output.contains(QLatin1String("No space left on device")))
        return QStringLiteral(
            "the disk filled up during the build. Free space on the volume "
            "holding the build directory, then start the update again — "
            "nothing was replaced.");
    const QStringList lines = output.split(QLatin1Char('\n'));
    for (const QString &line : lines) {
        const QString trimmed = line.trimmed();
        if (trimmed.contains(QLatin1String("error:")) ||
            trimmed.startsWith(QLatin1String("fatal:")))
            return trimmed.left(300);
    }
    QString tail = output.trimmed().right(300);
    const int newline = tail.indexOf(QLatin1Char('\n'));
    if (newline >= 0 && newline < tail.size() - 1)
        tail = tail.mid(newline + 1);
    return tail.trimmed();
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

struct PullPreviewStep {
    QString program;
    QStringList args;
    QString dir;
    QString status;
};

inline QList<PullPreviewStep> pullPreviewSteps(const QString &gitDir,
                                        const QString &previewDir,
                                        const QString &clientDir,
                                        const QString &buildDir,
                                        const QString &commit, bool haveWorktree,
                                        int jobs)
{
    QList<PullPreviewStep> steps;
    steps << PullPreviewStep{QStringLiteral("git"),
                             {QStringLiteral("-C"), gitDir,
                              QStringLiteral("worktree"), QStringLiteral("prune")},
                             gitDir, QString::fromUtf8("Preparing worktree\xE2\x80\xA6")};
    if (haveWorktree) {
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

using forkmesh::SunburstNode;

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
                [target] { revealInDesktopFileManager(target); });
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
        qreal angle = std::atan2(g.center.y() - pos.y(), pos.x() - g.center.x())
                      * 180.0 / M_PI;
        for (int i = 0; i < m_segments.size(); ++i) {
            const Segment &seg = m_segments.at(i);
            const qreal r0 = g.hole + (seg.depth - 1) * g.ringWidth;
            const qreal r1 = r0 + g.ringWidth;
            if (r < r0 || r > r1)
                continue;
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

// One mounted filesystem beside the big size map: a small
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

inline qreal iconDevicePixelRatio()
{
    const qreal dpr = qGuiApp ? qGuiApp->devicePixelRatio() : 1.0;
    return dpr > 0.0 ? dpr : 1.0;
}

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

inline QPixmap restartProgressPixmap(const QColor &color, double angleDeg, int size,
                                     int percent, bool hourglass)
{
    QPixmap pm = crispIconPixmap(size, iconDevicePixelRatio());
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);

    const qreal stroke = qMax<qreal>(1.0, size * 0.075);
    const qreal inset = stroke * 0.5 + qMax<qreal>(0.5, size * 0.055);
    const QRectF ring(inset, inset, size - inset * 2, size - inset * 2);
    QColor track = color;
    track.setAlphaF(0.26);
    QPen ringPen(track, stroke, Qt::SolidLine, Qt::RoundCap);
    p.setPen(ringPen);
    p.setBrush(Qt::NoBrush);
    p.drawEllipse(ring);

    const int clampedPercent = qBound(0, percent, 100);
    if (clampedPercent > 0) {
        ringPen.setColor(color);
        p.setPen(ringPen);
        p.drawArc(ring, 90 * 16, -clampedPercent * 360 * 16 / 100);
    }

    const int glyphSize = qMax(8, qRound(size * 0.63));
    const QPixmap glyph = hourglass
                              ? hourglassPixmap(color, angleDeg, glyphSize)
                              : refreshPixmap(color, angleDeg, glyphSize);
    p.drawPixmap((size - glyphSize) / 2.0, (size - glyphSize) / 2.0, glyph);
    return pm;
}

inline QByteArray forkMeshAvatarPng(const QString &seed)
{
    const QByteArray h =
        QCryptographicHash::hash(seed.toUtf8(), QCryptographicHash::Sha256);
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

    if (hairStyle == 6) {
        p.setPen(Qt::NoPen);
        p.setBrush(hair);
        QPainterPath bk;
        bk.addRoundedRect(QRectF(cx - faceHW - 7, headTop - 2,
                                 (faceHW + 7) * 2, faceHH * 2 + 20),
                          28, 28);
        p.drawPath(bk);
    }

    p.setPen(Qt::NoPen);
    p.setBrush(skin);
    p.drawEllipse(QPointF(faceRect.left() + 3, faceCy + 3), 7, 9);
    p.drawEllipse(QPointF(faceRect.right() - 3, faceCy + 3), 7, 9);
    p.drawEllipse(faceRect);
    p.setBrush(Qt::NoBrush);
    p.setPen(QPen(skin.darker(123), 1.6));
    p.drawEllipse(faceRect);
    p.setPen(Qt::NoPen);

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

inline QPixmap roundedAvatar(const QPixmap &src, int side,
                             qreal radiusRatio = 0.28)
{
    if (src.isNull())
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

inline QPixmap roundedAvatar(const QByteArray &png, int side,
                             qreal radiusRatio = 0.28)
{
    QPixmap src;
    if (png.isEmpty() || !src.loadFromData(png))
        return QPixmap();
    return roundedAvatar(src, side, radiusRatio);
}

inline QPixmap nodeMachineFavicon(const QString &seed, int side = 36)
{
    QPixmap pm = roundedAvatar(forkMeshNodeAvatarPng(seed), side);
    if (!pm.isNull())
        return pm;
    return QPixmap();
}

inline QIcon osBadgeIcon(const QString &platform, bool online, int size)
{
    const QString p = platform.toLower();
    const QColor grey("#6e7681");
    const QColor online_green("#2ea043");
    auto col = [&](const QColor &) { return online ? online_green : grey; };

    QPixmap pm = crispIconPixmap(size, iconDevicePixelRatio());
    QPainter g(&pm);
    g.setRenderHint(QPainter::Antialiasing);
    g.setPen(Qt::NoPen);

    if (p.contains("win")) {
        g.setBrush(col(QColor("#3fa0ef")));
        const qreal m = size * 0.18, gap = size * 0.12;
        const qreal cell = (size - 2 * m - gap) / 2.0;
        g.drawRect(QRectF(m, m, cell, cell));
        g.drawRect(QRectF(m + cell + gap, m, cell, cell));
        g.drawRect(QRectF(m, m + cell + gap, cell, cell));
        g.drawRect(QRectF(m + cell + gap, m + cell + gap, cell, cell));
    } else if (p.contains("mac") || p.contains("ios") || p.contains("darwin") ||
               p.contains("os x")) {
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

inline void applyLogFont(QPlainTextEdit *view)
{
    if (!view)
        return;
    QFont mono = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    QStringList families;
    families << mono.family();
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

inline QPixmap tintedOcticonPixmap(const QString &name, const QColor &color,
                                   int size);

class LogActivityLights : public QWidget
{
public:
    enum Presentation {
        Compact,
        Header,
        Debug,
    };

    struct WebsiteStatus {
        QString id;
        QString label;
        QString status;
        QString reason;
        qint64 minuteTs = 0;
        bool local = false;
        QString localStatus;
        QString localReason;
        qint64 localCheckedTs = 0;
        bool checking = false;
    };

    explicit LogActivityLights(Presentation presentation = Compact,
                               QWidget *parent = nullptr)
        : QWidget(parent), m_presentation(presentation)
    {
        setObjectName(presentation == Compact
                          ? QStringLiteral("logActivityLights")
                          : presentation == Header
                                ? QStringLiteral("logActivityHeader")
                                : QStringLiteral("debugActivityLights"));
        if (presentation == Compact) {
            updateCompactSize();
        } else if (presentation == Header) {
            setFixedHeight(kHeaderHeight);
            setMinimumWidth(0);
            setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        } else {
            updateDebugSize();
        }
        setCursor(Qt::PointingHandCursor);
        setMouseTracking(true);
        setAccessibleName(
            presentation == Compact
                ? QStringLiteral("Live log activity by category")
                : presentation == Header
                      ? QStringLiteral("Live log category counts and website status")
                      : QStringLiteral("Debug activity counts and website status"));
        updateSummaryToolTip();
    }

    void pulse(const QString &badge)
    {
        const int lane = categoryIndex(badge);
        if (lane < 0)
            return;
        const quint64 previous = m_counts[lane]++;
        const int generation = ++m_generation[lane];
        m_blinking[lane] = previous > 0;
        m_blinkVisible[lane] = previous == 0;
        update();
        if (previous > 0) {
            for (int phase = 1; phase <= 4; ++phase) {
                QTimer::singleShot(phase * 110, this,
                                   [this, lane, generation, phase] {
                    if (m_generation[lane] != generation)
                        return;
                    m_blinkVisible[lane] = (phase % 2) != 0;
                    if (phase == 4) {
                        m_blinkVisible[lane] = true;
                        m_blinking[lane] = false;
                    }
                    update();
                });
            }
        }
        updateSummaryToolTip();
    }

    void setExpanded(bool expanded)
    {
        m_expanded = expanded;
        updateSummaryToolTip();
    }

    void reset()
    {
        for (int i = 0; i < categoryCount(); ++i) {
            m_counts[i] = 0;
            m_blinking[i] = false;
            m_blinkVisible[i] = false;
            ++m_generation[i];
        }
        updateSummaryToolTip();
        update();
    }

    void resetCategory(const QString &badge)
    {
        const int lane = categoryIndex(badge);
        if (lane < 0)
            return;
        m_counts[lane] = 0;
        m_blinking[lane] = false;
        m_blinkVisible[lane] = false;
        ++m_generation[lane];
        updateSummaryToolTip();
        update();
    }

    void setWebsiteStatuses(const QList<WebsiteStatus> &statuses)
    {
        m_websiteStatuses = statuses;
        noteWebsiteCheckCycles();
        if (m_presentation == Compact)
            updateCompactSize();
        else if (m_presentation == Debug)
            updateDebugSize();
        updateSummaryToolTip();
        updateWebsiteAnimation();
        update();
    }

    void setStallToolTip(const QString &toolTip)
    {
        m_stallToolTip = toolTip;
    }

    int lightCount() const { return categoryCount(); }

    static QStringList badges()
    {
        QStringList names;
        names.reserve(categoryCount());
        for (int i = 0; i < categoryCount(); ++i)
            names << QString::fromLatin1(categories()[i].badge);
        return names;
    }

    static QString iconForBadge(const QString &badge)
    {
        for (int i = 0; i < categoryCount(); ++i) {
            if (badge == QLatin1String(categories()[i].badge))
                return QString::fromLatin1(categories()[i].icon);
        }
        return QStringLiteral("info");
    }

    quint64 countFor(const QString &badge) const
    {
        const int lane = categoryIndex(badge);
        return lane >= 0 ? m_counts[lane] : 0;
    }
    bool isActive(const QString &badge) const { return countFor(badge) > 0; }
    bool isBlinking(const QString &badge) const
    {
        const int lane = categoryIndex(badge);
        return lane >= 0 && m_blinking[lane];
    }
    int websiteStatusCount() const { return m_websiteStatuses.size(); }
    QString websiteStatusFor(const QString &id) const
    {
        for (const WebsiteStatus &status : m_websiteStatuses) {
            if (status.id == id)
                return status.status;
        }
        return QString();
    }
    QString websiteLocalStatusFor(const QString &id) const
    {
        for (const WebsiteStatus &status : m_websiteStatuses) {
            if (status.id == id)
                return status.localStatus;
        }
        return QString();
    }
    bool isHeader() const { return m_presentation == Header; }
    bool isDebug() const { return m_presentation == Debug; }

    std::function<void(const QString &category)> onCategoryClicked;
    std::function<void(const QString &statusId)> onWebsiteClicked;
    std::function<void()> onStallClicked;
    std::function<void()> onStallContextMenu;
    std::function<void()> onClicked;

protected:
    void paintEvent(QPaintEvent *) override
    {
        const QVector<int> lanes = sortedCategoryLanes();
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);
        const bool dark = currentThemeIsDark();
        if (m_presentation == Compact) {
            painter.setPen(QPen(QColor(dark ? "#30363d" : "#d0d7de"), 1));
            painter.setBrush(QColor(dark ? "#161b22" : "#ffffff"));
            painter.drawRoundedRect(rect().adjusted(0, 0, -1, -1), 8, 8);
        } else if (m_presentation == Header) {
            painter.fillRect(rect(), QColor(dark ? "#0d1117" : "#f6f8fa"));
            painter.setPen(QColor(dark ? "#30363d" : "#d0d7de"));
            painter.drawLine(0, height() - 1, width(), height() - 1);
        }

        const QColor unseen(dark ? "#6e7681" : "#8c959f");
        const QColor blinkOff(dark ? "#30363d" : "#d0d7de");
        for (int i = 0; i < categoryCount(); ++i) {
            const int lane = lanes.at(i);
            QColor color = unseen;
            if (m_counts[lane] > 0)
                color = m_blinkVisible[lane]
                            ? QColor(QString::fromLatin1(categories()[lane].accent))
                            : blinkOff;
            const QRect iconRect = categoryIconRect(i);
            const int iconSize = qMax(7, qMin(iconRect.width(), iconRect.height()));
            const QPixmap pixmap = tintedOcticonPixmap(
                QString::fromLatin1(categories()[lane].icon), color, iconSize);
            painter.drawPixmap(iconRect.center().x() - iconSize / 2,
                               iconRect.center().y() - iconSize / 2, pixmap);

            if (m_presentation == Header) {
                QFont countFont = painter.font();
                countFont.setPixelSize(qMax(6, qMin(9, categorySlotWidth() - 2)));
                countFont.setBold(m_counts[lane] > 0);
                painter.setFont(countFont);
                painter.setPen(m_counts[lane] > 0 ? color : unseen);
                painter.drawText(categoryCountRect(i), Qt::AlignHCenter | Qt::AlignTop,
                                 QString::number(m_counts[lane]));
            } else if (m_presentation == Debug) {
                QFont countFont = painter.font();
                countFont.setPixelSize(6);
                countFont.setBold(m_counts[lane] > 0);
                painter.setFont(countFont);
                painter.setPen(m_counts[lane] > 0 ? color : unseen);
                const QString count = m_counts[lane] > 999
                                          ? QStringLiteral("999+")
                                          : QString::number(m_counts[lane]);
                painter.drawText(categoryCountRect(i),
                                 Qt::AlignLeft | Qt::AlignTop, count);

                QFont labelFont = painter.font();
                labelFont.setPixelSize(6);
                labelFont.setBold(false);
                painter.setFont(labelFont);
                painter.setPen(unseen);
                painter.drawText(categoryLabelRect(i),
                                 Qt::AlignHCenter | Qt::AlignVCenter,
                                 QString::fromLatin1(categories()[lane].badge));
            }
        }

        if (!m_websiteStatuses.isEmpty()) {
            const int separatorX = websiteSeparatorX();
            painter.setPen(QPen(QColor(dark ? "#484f58" : "#afb8c1"), 1));
            painter.drawLine(separatorX, 5, separatorX, height() - 6);
            painter.setPen(Qt::NoPen);
            for (int i = 0; i < m_websiteStatuses.size(); ++i) {
                const QRect dotRect = websiteStatusRect(i);
                const QColor verdict = websiteStatusColor(
                    m_websiteStatuses.at(i).status, dark);
                const bool checking = websiteChecking(i);
                const QColor color =
                    checking && !m_websiteBlinkOn ? blinkOff : verdict;
                painter.setBrush(color);
                painter.drawEllipse(dotRect);
                if (m_presentation == Debug) {
                    paintWebsiteCountdown(painter, i, verdict, dark);
                    QFont labelFont = painter.font();
                    labelFont.setPixelSize(7);
                    labelFont.setBold(false);
                    painter.setFont(labelFont);
                    painter.setPen(unseen);
                    painter.drawText(websiteStatusLabelRect(i),
                                     Qt::AlignHCenter | Qt::AlignVCenter,
                                     debugWebsiteLabel(m_websiteStatuses.at(i)));
                    painter.setPen(Qt::NoPen);
                }
            }
        }
    }

    bool event(QEvent *event) override
    {
        if (event->type() == QEvent::ToolTip) {
            auto *help = static_cast<QHelpEvent *>(event);
            const int lane = categoryAt(help->pos());
            if (lane >= 0) {
                const quint64 count = m_counts[lane];
                QString tip = QStringLiteral("%1 — %2 occurrence%3")
                                  .arg(QString::fromLatin1(categories()[lane].badge))
                                  .arg(count)
                                  .arg(count == 1 ? QString() : QStringLiteral("s"));
                if (lane == stallCategoryIndex() && !m_stallToolTip.isEmpty())
                    tip += QLatin1Char('\n') + m_stallToolTip;
                else if (m_presentation != Header)
                    tip += QStringLiteral("\nClick for full Log page filtered to this "
                                          "category");
                QToolTip::showText(help->globalPos(), tip, this);
                return true;
            }
            const int website = websiteStatusAt(help->pos());
            if (website >= 0) {
                const WebsiteStatus &status = m_websiteStatuses.at(website);
                QString tip = QStringLiteral("%1 — %2")
                                  .arg(status.label, status.status);
                if (status.minuteTs > 0) {
                    tip += QStringLiteral("\n%1: %2")
                               .arg(status.local
                                        ? QStringLiteral("Checked from this desktop")
                                        : QStringLiteral("Last completed minute"),
                                    QDateTime::fromMSecsSinceEpoch(status.minuteTs)
                                        .toLocalTime()
                                        .toString(QStringLiteral("yyyy-MM-dd HH:mm")));
                }
                if (!status.reason.isEmpty())
                    tip += QLatin1Char('\n') + status.reason;
                if (!status.localStatus.isEmpty()) {
                    tip += QStringLiteral("\nFrom this desktop: %1")
                               .arg(status.localStatus);
                    if (status.localCheckedTs > 0)
                        tip += QStringLiteral(" (checked %1)")
                                   .arg(QDateTime::fromMSecsSinceEpoch(
                                            status.localCheckedTs)
                                            .toLocalTime()
                                            .toString(QStringLiteral("HH:mm")));
                    if (!status.localReason.isEmpty())
                        tip += QLatin1Char('\n') + status.localReason;
                }
                if (websiteChecking(website))
                    tip += QStringLiteral("\nChecking now…");
                if (onWebsiteClicked)
                    tip += QStringLiteral(
                        "\nClick to check again now and open the related "
                        "website page.");
                QToolTip::showText(help->globalPos(), tip, this);
                return true;
            }
        }
        return QWidget::event(event);
    }

    void mouseReleaseEvent(QMouseEvent *event) override
    {
        if (event->button() == Qt::LeftButton && rect().contains(event->pos())) {
            const int lane = categoryAt(event->pos());
            if (lane == stallCategoryIndex() && onStallClicked) {
                onStallClicked();
            } else if (lane < 0) {
                const int website = websiteStatusAt(event->pos());
                if (website >= 0 && onWebsiteClicked) {
                    onWebsiteClicked(m_websiteStatuses.at(website).id);
                } else if (onClicked)
                    onClicked();
            } else if (m_presentation != Header) {
                if (onCategoryClicked)
                    onCategoryClicked(categories()[lane].badge);
                else if (onClicked)
                    onClicked();
            }
        }
        QWidget::mouseReleaseEvent(event);
    }

    void contextMenuEvent(QContextMenuEvent *event) override
    {
        if (categoryAt(event->pos()) == stallCategoryIndex() &&
            onStallContextMenu) {
            onStallContextMenu();
            event->accept();
            return;
        }
        QWidget::contextMenuEvent(event);
    }

    void showEvent(QShowEvent *event) override
    {
        QWidget::showEvent(event);
        updateWebsiteAnimation();
    }

    void hideEvent(QHideEvent *event) override
    {
        if (m_websiteAnimation)
            m_websiteAnimation->stop();
        QWidget::hideEvent(event);
    }

private:
    struct Category {
        const char *badge;
        const char *accent;
        const char *icon;
    };

    static const Category *categories()
    {
        static const Category values[] = {
            {"SESSION", "#f2cc60", "history"},
            {"STATUS", "#56d364", "check-circle"},
            {"PEER", "#3fb950", "people"},
            {"NODE", "#3fb950", "server"},
            {"FORK", "#3fb950", "repo-forked"},
            {"FORKED", "#3fb950", "repo-forked"},
            {"MIRROR", "#39c5cf", "sync"},
            {"SYNC", "#39c5cf", "sync"},
            {"ACCOUNT", "#8b949e", "person"},
            {"HOST", "#76e3ea", "device-desktop"},
            {"ACTIONS", "#f0883e", "workflow"},
            {"PIN", "#79c0ff", "shield-check"},
            {"GIT", "#58a6ff", "git-commit"},
            {"BGTASK", "#8b949e", "gear"},
            {"BGBLOCK", "#f0883e", "stop"},
            {"BGNOTE", "#6e7681", "note"},
            {"PUBLISH", "#58a6ff", "upload"},
            {"PULL", "#3fb950", "git-pull-request"},
            {"MERGE", "#a371f7", "git-merge"},
            {"ISSUE", "#bc8cff", "issue-opened"},
            {"PROMPT", "#bc8cff", "comment"},
            {"BOUNTY", "#d29922", "star"},
            {"WALLET", "#d29922", "credit-card"},
            {"CRYPTO", "#79c0ff", "lock"},
            {"IDENTITY", "#79c0ff", "key"},
            {"ADMIN", "#db6d28", "crown"},
            {"SAVE", "#3fb950", "check-circle"},
            {"CLIP", "#8b949e", "copy"},
            {"NETWORK", "#f2cc60", "broadcast"},
            {"CLOUD", "#f6821f", "cloud"},
            {"STALL", "#d29922", "alert"},
            {"ERROR", "#f85149", "x"},
            {"INFO", "#6e7681", "info"},
        };
        static_assert(sizeof(values) / sizeof(values[0]) == kCategoryCount,
                      "kCategoryCount must match the taxonomy above");
        return values;
    }

    static constexpr int kCategoryCount = 33;
    static constexpr int categoryCount() { return kCategoryCount; }
    static constexpr int kCompactRows = 3;
    static constexpr int kCompactColumns =
        (kCategoryCount + kCompactRows - 1) / kCompactRows;
    static constexpr int kCompactCell = 16;
    static constexpr int kCompactPadding = 7;
    static constexpr int kHeaderHeight = 32;
    static constexpr int kDebugHeight = 40;
    static constexpr int kDebugPadding = 4;
    static constexpr int kDebugCategorySlot = 25;
    static constexpr int kDebugWebsiteSlot = 36;
    static constexpr int kDebugWebsiteDot = 14;
    static constexpr int kDebugWebsiteRing = 21;
    static constexpr int kDebugWebsiteTop = 4;
    static constexpr int kWebsiteCheckIntervalMs = 60000;
    static constexpr int kWebsiteCheckBlinkTailMs = 900;

    static int categoryIndex(const QString &badge)
    {
        for (int i = 0; i < categoryCount(); ++i) {
            if (badge == QLatin1String(categories()[i].badge))
                return i;
        }
        return categoryCount() - 1; // unknown future categories pulse INFO
    }

    static int stallCategoryIndex()
    {
        static const int index = categoryIndex(QStringLiteral("STALL"));
        return index;
    }

    int compactCategoryWidth() const
    {
        return kCompactPadding * 2 + kCompactColumns * kCompactCell;
    }

    int compactWebsiteWidth() const
    {
        if (m_websiteStatuses.isEmpty())
            return 0;
        return 12 + ((m_websiteStatuses.size() + kCompactRows - 1) /
                     kCompactRows) * 9 + kCompactPadding;
    }

    void updateCompactSize()
    {
        setFixedSize(compactCategoryWidth() + compactWebsiteWidth(),
                     kCompactPadding * 2 + kCompactRows * kCompactCell);
    }

    int debugCategoryWidth() const
    {
        return kDebugPadding * 2 + categoryCount() * kDebugCategorySlot;
    }

    int debugWebsiteWidth() const
    {
        return m_websiteStatuses.isEmpty()
                   ? 0
                   : 8 + m_websiteStatuses.size() * kDebugWebsiteSlot;
    }

    void updateDebugSize()
    {
        setFixedSize(debugCategoryWidth() + debugWebsiteWidth(), kDebugHeight);
        setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    }

    int headerWebsiteWidth() const
    {
        if (m_websiteStatuses.isEmpty())
            return 0;
        const int wanted = 16 +
            ((m_websiteStatuses.size() + 1) / 2) * 7;
        return qBound(72, wanted, qMax(72, width() / 3));
    }

    int websiteSeparatorX() const
    {
        return m_presentation == Compact
                   ? compactCategoryWidth() + 3
                   : m_presentation == Debug
                         ? debugCategoryWidth() + 3
                   : qMax(1, width() - headerWebsiteWidth());
    }

    int categoryAreaWidth() const
    {
        if (m_presentation == Compact)
            return compactCategoryWidth();
        if (m_presentation == Debug)
            return debugCategoryWidth();
        return m_websiteStatuses.isEmpty() ? width()
                                           : websiteSeparatorX() - 3;
    }

    int categorySlotWidth() const
    {
        if (m_presentation == Debug)
            return kDebugCategorySlot;
        return qMax(1, categoryAreaWidth() / categoryCount());
    }

    QRect categoryIconRect(int index) const
    {
        if (m_presentation == Compact) {
            const int col = index % kCompactColumns;
            const int row = index / kCompactColumns;
            return QRect(kCompactPadding + col * kCompactCell,
                         kCompactPadding + row * kCompactCell,
                         kCompactCell, kCompactCell);
        }
        if (m_presentation == Debug) {
            const int x = kDebugPadding + index * kDebugCategorySlot;
            return QRect(x + (kDebugCategorySlot - 14) / 2, 5, 14, 14);
        }
        const int slot = categorySlotWidth();
        return QRect(index * slot, 2, slot, 15);
    }

    QRect categoryCountRect(int index) const
    {
        if (m_presentation == Debug) {
            const QRect icon = categoryIconRect(index);
            return QRect(icon.right() - 1, 1, 14, 8);
        }
        const int slot = categorySlotWidth();
        return QRect(index * slot, 17, slot, 12);
    }

    QRect categoryLabelRect(int index) const
    {
        const int x = kDebugPadding + index * kDebugCategorySlot;
        return QRect(x, 24, kDebugCategorySlot, 13);
    }

    int categoryAt(const QPoint &point) const
    {
        const int displayIndex = categoryDisplayIndexAt(point);
        if (displayIndex < 0)
            return -1;
        const QVector<int> lanes = sortedCategoryLanes();
        return lanes.value(displayIndex, -1);
    }

    int categoryDisplayIndexAt(const QPoint &point) const
    {
        if (m_presentation == Compact) {
            if (point.x() < kCompactPadding ||
                point.x() >= compactCategoryWidth() - kCompactPadding ||
                point.y() < kCompactPadding)
                return -1;
            const int col = (point.x() - kCompactPadding) / kCompactCell;
            const int row = (point.y() - kCompactPadding) / kCompactCell;
            const int index = row * kCompactColumns + col;
            return index >= 0 && index < categoryCount() ? index : -1;
        }
        if (m_presentation == Debug) {
            if (point.x() < kDebugPadding || point.x() >= debugCategoryWidth())
                return -1;
            const int index = (point.x() - kDebugPadding) / kDebugCategorySlot;
            return index >= 0 && index < categoryCount() ? index : -1;
        }
        if (point.x() < 0 || point.x() >= categoryAreaWidth())
            return -1;
        return qBound(0, point.x() / categorySlotWidth(), categoryCount() - 1);
    }

    QVector<int> sortedCategoryLanes() const
    {
        QVector<int> lanes;
        lanes.reserve(categoryCount());
        for (int i = 0; i < categoryCount(); ++i)
            lanes << i;
        std::stable_sort(lanes.begin(), lanes.end(),
                         [this](int left, int right) {
                             if (m_counts[left] == m_counts[right])
                                 return left < right;
                             return m_counts[left] > m_counts[right];
                         });
        return lanes;
    }

    QRect websiteStatusRect(int index) const
    {
        if (m_presentation == Compact) {
            const int col = index / kCompactRows;
            const int row = index % kCompactRows;
            const int x = websiteSeparatorX() + 7 + col * 9;
            const int y = kCompactPadding + row * kCompactCell + 5;
            return QRect(x, y, 6, 6);
        }
        if (m_presentation == Debug) {
            const int x = websiteSeparatorX() +
                          index * kDebugWebsiteSlot +
                          (kDebugWebsiteSlot - kDebugWebsiteDot) / 2;
            return QRect(x, kDebugWebsiteTop, kDebugWebsiteDot,
                         kDebugWebsiteDot);
        }
        const int col = index / 2;
        const int row = index % 2;
        const int x = websiteSeparatorX() + 8 + col * 7;
        const int y = 8 + row * 10;
        return QRect(x, y, 5, 5);
    }

    QRect websiteStatusLabelRect(int index) const
    {
        const int x = websiteSeparatorX() + index * kDebugWebsiteSlot;
        return QRect(x, 27, kDebugWebsiteSlot, 12);
    }

    QRect websiteCountdownRect(int index) const
    {
        const QRect dot = websiteStatusRect(index);
        const int grow = (kDebugWebsiteRing - dot.width()) / 2;
        return dot.adjusted(-grow, -grow, grow, grow);
    }

    qreal websiteCountdownFraction(int index) const
    {
        const qint64 checked =
            m_websiteCheckedAt.value(m_websiteStatuses.at(index).id, 0);
        if (checked <= 0)
            return 0.0;
        const qint64 elapsed = QDateTime::currentMSecsSinceEpoch() - checked;
        if (elapsed < 0 || elapsed >= kWebsiteCheckIntervalMs)
            return 0.0;
        return 1.0 - qreal(elapsed) / qreal(kWebsiteCheckIntervalMs);
    }

    void paintWebsiteCountdown(QPainter &painter, int index,
                               const QColor &verdict, bool dark) const
    {
        const QRectF ring(websiteCountdownRect(index));
        painter.setBrush(Qt::NoBrush);
        painter.setPen(QPen(QColor(dark ? "#30363d" : "#d0d7de"), 2));
        painter.drawEllipse(ring);
        const qreal remaining = websiteCountdownFraction(index);
        if (remaining > 0.0) {
            painter.setPen(QPen(verdict, 2, Qt::SolidLine, Qt::FlatCap));
            painter.drawArc(ring, 90 * 16,
                            -qRound(remaining * 360.0) * 16);
        }
        painter.setPen(Qt::NoPen);
        painter.setBrush(Qt::NoBrush);
    }

    bool websiteChecking(int index) const
    {
        if (index < 0 || index >= m_websiteStatuses.size())
            return false;
        const WebsiteStatus &status = m_websiteStatuses.at(index);
        if (status.checking)
            return true;
        return m_websiteBlinkUntil.value(status.id, 0) >
               QDateTime::currentMSecsSinceEpoch();
    }

    bool anyWebsiteChecking() const
    {
        for (int i = 0; i < m_websiteStatuses.size(); ++i) {
            if (websiteChecking(i))
                return true;
        }
        return false;
    }

    void noteWebsiteCheckCycles()
    {
        const qint64 now = QDateTime::currentMSecsSinceEpoch();
        QSet<QString> present;
        QSet<QString> checking;
        for (const WebsiteStatus &status : m_websiteStatuses) {
            present.insert(status.id);
            if (status.checking) {
                checking.insert(status.id);
                m_websiteBlinkUntil[status.id] = now + kWebsiteCheckBlinkTailMs;
            } else if (m_websiteChecking.contains(status.id) ||
                       !m_websiteCheckedAt.contains(status.id)) {
                m_websiteCheckedAt[status.id] = now;
            }
        }
        m_websiteChecking = checking;
        for (auto it = m_websiteCheckedAt.begin();
             it != m_websiteCheckedAt.end();) {
            if (present.contains(it.key()))
                ++it;
            else
                it = m_websiteCheckedAt.erase(it);
        }
        for (auto it = m_websiteBlinkUntil.begin();
             it != m_websiteBlinkUntil.end();) {
            if (present.contains(it.key()))
                ++it;
            else
                it = m_websiteBlinkUntil.erase(it);
        }
    }

    void updateWebsiteAnimation()
    {
        const bool blinking = anyWebsiteChecking();
        if (m_websiteStatuses.isEmpty() || !isVisible() ||
            (m_presentation != Debug && !blinking)) {
            if (m_websiteAnimation)
                m_websiteAnimation->stop();
            return;
        }
        if (!m_websiteAnimation) {
            m_websiteAnimation = new QTimer(this);
            connect(m_websiteAnimation, &QTimer::timeout, this, [this] {
                m_websiteBlinkOn = !m_websiteBlinkOn;
                updateWebsiteAnimation();
                update();
            });
        }
        const int interval = blinking ? 200 : 1000;
        if (m_websiteAnimation->interval() != interval)
            m_websiteAnimation->setInterval(interval);
        if (!m_websiteAnimation->isActive())
            m_websiteAnimation->start();
    }

    static QString debugWebsiteLabel(const WebsiteStatus &status)
    {
        static const QHash<QString, QString> labels = {
            {QStringLiteral("website"), QStringLiteral("Web")},
            {QStringLiteral("status_page"), QStringLiteral("Status page")},
            {QStringLiteral("api"), QStringLiteral("API")},
            {QStringLiteral("errors"), QStringLiteral("Errors")},
            {QStringLiteral("database"), QStringLiteral("DB")},
            {QStringLiteral("email"), QStringLiteral("Email")},
            {QStringLiteral("flagship_repository"), QStringLiteral("Repo")},
            {QStringLiteral("installer"), QStringLiteral("Installer")},
            {QStringLiteral("git_hosting"), QStringLiteral("Git host")},
            {QStringLiteral("realtime"), QStringLiteral("Realtime")},
            {QStringLiteral("durable_objects"), QStringLiteral("Durables")},
            {QStringLiteral("desktop_website"), QStringLiteral("Web here")},
            {QStringLiteral("desktop_status_page"), QStringLiteral("Status here")},
        };
        const auto known = labels.constFind(status.id);
        if (known != labels.cend())
            return known.value();
        return status.label.section(QLatin1Char(' '), 0, 1).left(12);
    }

    int websiteStatusAt(const QPoint &point) const
    {
        for (int i = 0; i < m_websiteStatuses.size(); ++i) {
            const QRect hit = m_presentation == Debug
                                  ? websiteCountdownRect(i)
                                  : websiteStatusRect(i);
            if (hit.adjusted(-2, -2, 2, 2).contains(point))
                return i;
        }
        return -1;
    }

    static QColor websiteStatusColor(const QString &status, bool dark)
    {
        if (status == QLatin1String("operational"))
            return QColor(dark ? "#3fb950" : "#1a7f37");
        if (status == QLatin1String("degraded"))
            return QColor(dark ? "#d29922" : "#9a6700");
        if (status == QLatin1String("down"))
            return QColor(dark ? "#f85149" : "#cf222e");
        return QColor(dark ? "#6e7681" : "#8c959f");
    }

    void updateSummaryToolTip()
    {
        if (m_presentation == Header) {
            setToolTip(QStringLiteral(
                "Log categories and session counts; website /status results "
                "for the newest completed minute are separated on the right."));
            return;
        }
        setToolTip(QStringLiteral(
            "%1 live-log categories%2 — hover an icon for its count; click for full "
            "Log page filtered to this category")
                       .arg(categoryCount())
                       .arg(m_websiteStatuses.isEmpty()
                                ? QString()
                                : QStringLiteral(" and %1 website status results")
                                      .arg(m_websiteStatuses.size())));
    }

    Presentation m_presentation = Compact;
    quint64 m_counts[kCategoryCount] = {};
    bool m_blinkVisible[kCategoryCount] = {};
    bool m_blinking[kCategoryCount] = {};
    int m_generation[kCategoryCount] = {};
    bool m_expanded = false;
    QList<WebsiteStatus> m_websiteStatuses;
    QString m_stallToolTip;
    QHash<QString, qint64> m_websiteCheckedAt;
    QHash<QString, qint64> m_websiteBlinkUntil;
    QSet<QString> m_websiteChecking;
    QTimer *m_websiteAnimation = nullptr;
    bool m_websiteBlinkOn = true;
};

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
        const double t = qBound(0.0, double(m_elapsedMs) / kDrawMs, 1.0);
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
        p.drawArc(box, 90 * 16, -int(t * 360) * 16);

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
        QPen track(QColor(m_color.red(), m_color.green(), m_color.blue(), 60));
        track.setWidthF(pen);
        p.setPen(track);
        p.setBrush(Qt::NoBrush);
        p.drawEllipse(box);
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

    void setIssueNumber(int n)
    {
        if (m_issue == n)
            return;
        m_issue = n;
        updateGeometry(); // width depends on the "#N" suffix
        update();
    }
    void setOnClick(std::function<void()> cb) { m_onClick = std::move(cb); }
    void setOnNumberClick(std::function<void()> cb)
    {
        m_onNumberClick = std::move(cb);
    }

    QSize sizeHint() const override
    {
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

        const QRectF box = QRectF(rect()).adjusted(3.0, 3.0, -3.0, -3.0);
        const qreal radius = box.height() / 2.0;
        QPainterPath pill;
        pill.addRoundedRect(box, radius, radius);
        g.fillPath(pill, QColor(m_hover ? "#21262d" : "#161b22"));
        QPen base(QColor(m_active ? "#1f3d29" : "#30363d"));
        base.setWidthF(1.4);
        g.strokePath(pill, base);
        if (m_active)
            drawTravellingLoop(g, pill, neon);

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
        return m_issue > 0 ? QStringLiteral("#%1").arg(m_issue) : QString();
    }
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

inline constexpr int kAgentSpinTickMs = 60;

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

inline QPixmap nodeStatusLightPixmap(const QColor &color, int size, qreal angleDeg,
                                     bool spinning)
{
    QPixmap out = crispIconPixmap(size, iconDevicePixelRatio());
    QPainter p(&out);
    p.setRenderHint(QPainter::Antialiasing, true);
    const QPointF c(size / 2.0, size / 2.0);
    const qreal r = size / 2.0 - 1.5;
    p.setPen(Qt::NoPen);
    p.setBrush(spinning ? color.darker(160) : color);
    p.drawEllipse(c, r, r);
    if (spinning) {
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
        QColor glint = color.lighter(170);
        glint.setAlpha(200);
        p.setBrush(glint);
        p.drawEllipse(QPointF(c.x() - r * 0.30, c.y() - r * 0.30), r * 0.32,
                      r * 0.32);
    }
    return out;
}

inline QString octiconForBackgroundTaskWord(const QString &word);

inline QString octiconForBackgroundTaskWord(const QString &word);

class BackgroundTaskChip : public QWidget
{
public:
    explicit BackgroundTaskChip(const QString &word, QWidget *parent = nullptr)
        : QWidget(parent), m_word(word)
    {
        setFixedSize(kSize, kSize);
        setFocusPolicy(Qt::NoFocus);
        setProperty("processKind", m_word);
        setProperty("processCount", m_count);
        setAccessibleName(QStringLiteral("%1 background work").arg(m_word));
        updateAccessibleDescription();
    }

    QString word() const { return m_word; }

    void setCount(int count)
    {
        count = qMax(1, count);
        if (m_count == count)
            return;
        m_count = count;
        setProperty("processCount", m_count);
        updateAccessibleDescription();
        update();
    }

    void setAngle(qreal degrees)
    {
        m_angle = degrees;
        update();
    }

    static QString octiconForWord(const QString &word)
    {
        return octiconForBackgroundTaskWord(word);
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        const QColor accent(QString::fromLatin1(Theme::kRunning));

        const int segments = qBound(1, m_count, kMaxSegments);
        const qreal inset = kPenWidth / 2.0 + 0.5;
        const QRectF ring = QRectF(rect()).adjusted(inset, inset, -inset, -inset);
        const int span = 5760 / segments;
        const int gap = segments == 1 ? 1440 : qMax(320, span / 3);
        QPen pen(accent, kPenWidth);
        pen.setCapStyle(Qt::FlatCap);
        p.setPen(pen);
        p.setBrush(Qt::NoBrush);
        for (int i = 0; i < segments; ++i)
            p.drawArc(ring, int(-m_angle * 16) + i * span, span - gap);

        const QPixmap glyph = tintedOcticonPixmap(
            octiconForWord(m_word),
            currentThemeIsDark() ? QColor("#c9d1d9") : QColor("#57606a"),
            kGlyphSize);
        p.drawPixmap(QPointF((width() - kGlyphSize) / 2.0,
                             (height() - kGlyphSize) / 2.0),
                     glyph);
    }

private:
    void updateAccessibleDescription()
    {
        setAccessibleDescription(
            QStringLiteral("%1 %2 in progress")
                .arg(m_count)
                .arg(m_count == 1 ? QStringLiteral("process")
                                  : QStringLiteral("processes")));
    }

    static constexpr int kSize = 18;
    static constexpr int kGlyphSize = 10;
    static constexpr qreal kPenWidth = 1.3;
    static constexpr int kMaxSegments = 8;
    QString m_word;
    int m_count = 1;
    qreal m_angle = 0.0;
};

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

inline void applyStoredLabelOcticon(QLabel *label)
{
    if (!label)
        return;
    const QString name = label->property("forkmeshOcticon").toString();
    if (name.isEmpty())
        return;
    const int size = label->property("forkmeshOcticonSize").toInt();
    const int px = size > 0 ? size : 12;
    const QColor color(currentThemeIsDark() ? QStringLiteral("#8b949e")
                                            : QStringLiteral("#656d76"));
    label->setPixmap(tintedOcticonPixmap(name, color, px));
    label->setFixedSize(px, px);
}

inline void setLabelOcticon(QLabel *label, const QString &name, int size = 12)
{
    if (!label)
        return;
    label->setProperty("forkmeshOcticon", name);
    label->setProperty("forkmeshOcticonSize", size);
    applyStoredLabelOcticon(label);
}

inline void setTabOcticon(QTabWidget *tabs, int index, const QString &name,
                          int size = 14)
{
    if (!tabs || index < 0 || index >= tabs->count())
        return;
    QTabBar *bar = tabs->tabBar();
    bar->setTabData(index, name);
    const QColor color(currentThemeIsDark() ? QStringLiteral("#8b949e")
                                            : QStringLiteral("#656d76"));
    tabs->setTabIcon(index, themedOcticon(name, color, size));
    tabs->setIconSize(QSize(size, size));
}

inline void refreshTabOcticons(QTabWidget *tabs)
{
    if (!tabs)
        return;
    QTabBar *bar = tabs->tabBar();
    for (int i = 0; i < bar->count(); ++i) {
        const QString name = bar->tabData(i).toString();
        if (!name.isEmpty())
            setTabOcticon(tabs, i, name, tabs->iconSize().width());
    }
}

class ColumnFlowWidget : public QWidget
{
public:
    explicit ColumnFlowWidget(QWidget *parent = nullptr) : QWidget(parent)
    {
        m_row = new QHBoxLayout(this);
        m_row->setContentsMargins(0, 0, 0, 0);
        m_row->setSpacing(kColumnGap);
    }

    void setMinimumColumnWidth(int px)
    {
        m_minColumnWidth = qMax(120, px);
        reflow(true);
    }
    void setMaximumColumns(int columns)
    {
        m_maxColumns = qMax(1, columns);
        reflow(true);
    }

    QVBoxLayout *addSection()
    {
        auto *card = new QWidget(this);
        auto *body = new QVBoxLayout(card);
        body->setContentsMargins(0, 0, 0, 0);
        body->setSpacing(8);
        m_sections.append(card);
        m_pending = true;
        return body;
    }

    QSize minimumSizeHint() const override
    {
        QSize hint = QWidget::minimumSizeHint();
        hint.setWidth(qMin(hint.width(), m_minColumnWidth));
        return hint;
    }

    int columnCount() const { return m_columnCount; }

protected:
    void resizeEvent(QResizeEvent *event) override
    {
        QWidget::resizeEvent(event);
        reflow(false);
    }
    void showEvent(QShowEvent *event) override
    {
        QWidget::showEvent(event);
        reflow(false);
    }

private:
    static constexpr int kColumnGap = 22;

    int columnsForWidth() const
    {
        int best = 1;
        for (int n = 2; n <= qMin(m_maxColumns, int(m_sections.size())); ++n) {
            if (width() >= n * m_minColumnWidth + (n - 1) * kColumnGap)
                best = n;
        }
        return best;
    }

    void reflow(bool force)
    {
        const int columns = columnsForWidth();
        if (!force && !m_pending && columns == m_columnCount)
            return;
        m_columnCount = columns;
        m_pending = false;

        for (QVBoxLayout *column : std::as_const(m_columns)) {
            while (QLayoutItem *item = column->takeAt(0))
                delete item;
        }
        qDeleteAll(m_columns);
        m_columns.clear();
        while (QLayoutItem *item = m_row->takeAt(0))
            delete item;

        for (int i = 0; i < columns; ++i) {
            auto *column = new QVBoxLayout;
            column->setContentsMargins(0, 0, 0, 0);
            column->setSpacing(14);
            m_columns.append(column);
            m_row->addLayout(column, 1);
        }

        QList<int> filled(columns, 0);
        for (QWidget *section : std::as_const(m_sections)) {
            int target = 0;
            for (int i = 1; i < columns; ++i) {
                if (filled.at(i) < filled.at(target))
                    target = i;
            }
            m_columns.at(target)->addWidget(section);
            filled[target] += qMax(1, section->sizeHint().height());
        }
        for (QVBoxLayout *column : std::as_const(m_columns))
            column->addStretch(1);
    }

    QHBoxLayout *m_row = nullptr;
    QList<QWidget *> m_sections;
    QList<QVBoxLayout *> m_columns;
    int m_columnCount = 0;
    int m_minColumnWidth = 300;
    int m_maxColumns = 3;
    bool m_pending = false;
};

class ElidingPushButton : public QPushButton
{
public:
    using QPushButton::QPushButton;

    void setFullText(const QString &text)
    {
        m_fullText = text;
        applyElide();
    }
    QString fullText() const { return m_fullText; }

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

class StackedIconButton : public QPushButton
{
public:
    using QPushButton::QPushButton;

    void setGlyph(const QString &glyph)
    {
        m_glyph = glyph;
        updateGeometry();
        update();
    }

    QSize sizeHint() const override
    {
        const QSize slot = iconSlotSize();
        const QFontMetrics fm(captionFont());
        const bool hasCaption = !text().isEmpty();
        const int captionW = hasCaption ? fm.horizontalAdvance(text()) + 2 : 0;
        const int captionH = hasCaption ? fm.height() + kGap : 0;
        return QSize(qMax(slot.width(), captionW) + 2 * kPadH,
                     slot.height() + captionH + 2 * kPadV);
    }
    QSize minimumSizeHint() const override { return sizeHint(); }

protected:
    bool event(QEvent *e) override
    {
        switch (e->type()) {
        case QEvent::FontChange:
        case QEvent::StyleChange:
        case QEvent::ApplicationFontChange:
            updateGeometry();
            break;
        default:
            break;
        }
        return QPushButton::event(e);
    }

    void paintEvent(QPaintEvent *) override
    {
        QStylePainter painter(this);
        QStyleOptionButton opt;
        initStyleOption(&opt);
        const QIcon glyphIcon = opt.icon;
        opt.text.clear();
        opt.icon = QIcon();
        opt.iconSize = QSize();
        painter.drawControl(QStyle::CE_PushButton, opt);

        const QSize slot = iconSlotSize();
        const QFont caption = captionFont();
        const QFontMetrics fm(caption);
        const bool hasCaption = !text().isEmpty();
        const int captionH = hasCaption ? fm.height() : 0;
        const int blockH = slot.height() + (hasCaption ? kGap + captionH : 0);
        int top = rect().top() + (height() - blockH) / 2;

        const QRect slotRect((width() - slot.width()) / 2, top, slot.width(),
                             slot.height());
        if (!glyphIcon.isNull()) {
            glyphIcon.paint(&painter, slotRect, Qt::AlignCenter,
                            isEnabled() ? QIcon::Normal : QIcon::Disabled,
                            QIcon::Off);
        } else if (!m_glyph.isEmpty()) {
            QFont glyphFont = font();
            glyphFont.setBold(true);
            if (glyphFont.pixelSize() > 0)
                glyphFont.setPixelSize(slot.height());
            else
                glyphFont.setPointSize(qMax(9, slot.height() - 3));
            painter.setFont(glyphFont);
            painter.setPen(labelColor(opt));
            painter.drawText(slotRect, Qt::AlignCenter, m_glyph);
        }

        if (!hasCaption)
            return;
        const QRect captionRect(kPadH, top + slot.height() + kGap,
                                qMax(0, width() - 2 * kPadH), captionH);
        painter.setFont(caption);
        painter.setPen(labelColor(opt));
        painter.drawText(captionRect, Qt::AlignHCenter | Qt::AlignVCenter,
                         fm.elidedText(text(), Qt::ElideRight, captionRect.width()));
    }

private:
    static constexpr int kPadH = 8;
    static constexpr int kPadV = 4;
    static constexpr int kGap = 3; // between the glyph and its caption

    QSize iconSlotSize() const
    {
        if (!icon().isNull() && iconSize().isValid())
            return iconSize();
        return QSize(16, 16);
    }

    QColor labelColor(const QStyleOptionButton &opt) const
    {
        return opt.palette.color(isEnabled() ? QPalette::Active
                                             : QPalette::Disabled,
                                 QPalette::ButtonText);
    }

    QFont captionFont() const
    {
        QFont f = font();
        if (f.pixelSize() > 0)
            f.setPixelSize(qMax(9, f.pixelSize() - 4));
        else
            f.setPointSize(qMax(7, f.pointSize() - 3));
        f.setWeight(QFont::DemiBold);
        return f;
    }

    QString m_glyph;
};

class ElidingStatusLabel : public QLabel
{
public:
    explicit ElidingStatusLabel(QWidget *parent = nullptr) : QLabel(parent)
    {
        setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        setTextInteractionFlags(Qt::NoTextInteraction);
    }

    void setFullText(const QString &text)
    {
        const QString line = text.simplified();
        if (line == m_fullText)
            return;
        m_fullText = line;
        setToolTip(line);
        applyElide();
    }
    QString fullText() const { return m_fullText; }

    QSize minimumSizeHint() const override
    {
        QSize hint = QLabel::minimumSizeHint();
        hint.setWidth(0);
        return hint;
    }

protected:
    void resizeEvent(QResizeEvent *event) override
    {
        QLabel::resizeEvent(event);
        applyElide();
    }

private:
    void applyElide()
    {
        const QString elided = fontMetrics().elidedText(
            m_fullText, Qt::ElideRight, qMax(0, width()));
        if (elided != text())
            QLabel::setText(elided);
    }

    QString m_fullText;
};

class VerticalIconButton : public QPushButton
{
public:
    enum Form { Action, Tab, Bare };
    explicit VerticalIconButton(const QString &text, Form form,
                                QWidget *parent = nullptr)
        : QPushButton(text, parent), m_form(form)
    {
        setCursor(Qt::PointingHandCursor);
        setFlat(true);
        setAttribute(Qt::WA_Hover, true);
    }

    QSize sizeHint() const override
    {
        QFont f = font();
        f.setPixelSize(10);
        f.setWeight(QFont::DemiBold);
        const int textW = QFontMetrics(f).horizontalAdvance(text());
        const int badgesW = badgeStripWidth() + kIconPx / 2;
        return QSize(qMax(44, qMax(qMax(kIconPx, textW), badgesW) + 16), kHeight);
    }
    QSize minimumSizeHint() const override { return sizeHint(); }

    void setBadgeCount(qint64 count)
    {
        if (m_badge == count)
            return;
        m_badge = count;
        updateGeometry(); // a longer count can need a wider tile
        update();
    }
    qint64 badgeCount() const { return m_badge; }

    void setAlertBadgeCount(qint64 count)
    {
        count = qMax<qint64>(0, count);
        if (m_alertBadge == count)
            return;
        m_alertBadge = count;
        updateGeometry();
        update();
    }
    qint64 alertBadgeCount() const { return m_alertBadge; }

    void setOcticonName(const QString &name)
    {
        if (m_iconName == name)
            return;
        m_iconName = name;
        update();
    }

    void setAccentColor(const QColor &accent)
    {
        if (m_accent == accent)
            return;
        m_accent = accent;
        update();
    }
    QColor accentColor() const { return m_accent; }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        const bool dark = currentThemeIsDark();
        const bool hovered = isEnabled() && underMouse();
        QColor fg;
        if (m_form == Action)
            fg = dark ? QColor("#e6edf3") : QColor("#1f2328");
        else
            fg = dark ? QColor(isChecked() || hovered ? "#e6edf3" : "#8b949e")
                      : QColor(isChecked() || hovered ? "#1f2328" : "#656d76");
        if (!isEnabled())
            fg = QColor("#6e7681");
        const bool accented = m_accent.isValid() && isEnabled();
        const QColor glyphColor = accented ? m_accent : fg;
        const QColor captionColor =
            accented && (isChecked() || hovered) ? m_accent : fg;

        if (m_form == Action) {
            if (hovered) {
                p.setPen(Qt::NoPen);
                p.setBrush(QColor(dark ? "#21262d" : "#eaeef2"));
                p.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5),
                                  6, 6);
            }
        } else if (m_form == Tab && isChecked()) {
            p.fillRect(QRect(0, height() - 2, width(), 2),
                       accented ? m_accent
                                : QColor(dark ? "#2ea043" : "#1f883d"));
        }

        const QRect iconRect((width() - kIconPx) / 2, 6, kIconPx, kIconPx);
        if (m_iconName.isEmpty())
            icon().paint(&p, iconRect, Qt::AlignCenter,
                         isEnabled() ? QIcon::Normal : QIcon::Disabled);
        else
            p.drawPixmap(iconRect.topLeft(),
                         tintedOcticonPixmap(m_iconName, glyphColor, kIconPx));

        QFont f = font();
        f.setPixelSize(10);
        f.setWeight(QFont::DemiBold);
        p.setFont(f);
        p.setPen(captionColor);
        p.drawText(QRect(2, iconRect.bottom() + 2, width() - 4, 14),
                   Qt::AlignHCenter | Qt::AlignTop,
                   QFontMetrics(f).elidedText(text(), Qt::ElideRight,
                                              width() - 4));

        double rightEdge = iconRect.right() + kBadgeH / 2.0 + 2;
        const auto paintBadge = [&](qint64 count, const QColor &fill) {
            if (count <= 0)
                return;
            const QString badgeText = formatCount(count);
            QFont bf = font();
            bf.setPixelSize(9);
            bf.setBold(true);
            p.setFont(bf);
            const int h = kBadgeH;
            const int w = badgeWidth(count);
            const QRectF badge(qMax(0.0, rightEdge - w),
                               qMax(0.0, double(iconRect.top() - 5)), w, h);
            p.setPen(Qt::NoPen);
            p.setBrush(fill);
            p.drawRoundedRect(badge, h / 2.0, h / 2.0);
            p.setPen(fill.lightness() > 155 ? QColor("#0d1117")
                                            : QColor("#ffffff"));
            p.drawText(badge, Qt::AlignCenter, badgeText);
            rightEdge = badge.left() - 2;
        };
        paintBadge(m_badge, accented ? m_accent : QColor("#1f6feb"));
        paintBadge(m_alertBadge, QColor(dark ? "#da3633" : "#cf222e"));
    }

private:
    int badgeWidth(qint64 count) const
    {
        if (count <= 0)
            return 0;
        QFont bf = font();
        bf.setPixelSize(9);
        bf.setBold(true);
        return qMax(kBadgeH,
                    QFontMetrics(bf).horizontalAdvance(formatCount(count)) + 8);
    }
    int badgeStripWidth() const
    {
        const int blue = badgeWidth(m_badge);
        const int red = badgeWidth(m_alertBadge);
        return blue + red + (blue > 0 && red > 0 ? 2 : 0);
    }

    static constexpr int kIconPx = 16;
    static constexpr int kHeight = 44;
    static constexpr int kBadgeH = 14; // badge height and rounded-end radius
    Form m_form;
    qint64 m_badge = 0;
    qint64 m_alertBadge = 0;
    QString m_iconName; // empty: paint the QIcon set by setOcticon instead
    QColor m_accent;    // invalid: the rail's neutral grey/hover colours
};

class RatchetToggleButton : public QToolButton
{
public:
    explicit RatchetToggleButton(QWidget *parent = nullptr)
        : QToolButton(parent)
    {
        setText(QStringLiteral("Ratchet"));
        setCheckable(true);
        setCursor(Qt::PointingHandCursor);
    }

    QSize sizeHint() const override
    {
        QFont f = font();
        f.setPixelSize(10);
        f.setWeight(QFont::DemiBold);
        const int textW = QFontMetrics(f).horizontalAdvance(text());
        return QSize(qMax(48, qMax(kIconPx, textW) + 16), kHeight);
    }
    QSize minimumSizeHint() const override { return sizeHint(); }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        const bool dark = currentThemeIsDark();
        const bool hovered = isEnabled() && underMouse();
        const bool on = isChecked();

        const QColor accent(dark ? "#2ea043" : "#1f883d");
        QRectF pill = QRectF(rect()).adjusted(0.5, 3.5, -0.5, -3.5);
        if (on) {
            QColor fill = accent;
            if (hovered)
                fill = fill.lighter(112);
            p.setPen(QPen(accent.darker(115), 1));
            p.setBrush(fill);
        } else {
            p.setPen(QPen(QColor(dark ? "#30363d" : "#d0d7de"), 1));
            p.setBrush(hovered ? QColor(dark ? "#21262d" : "#eaeef2")
                               : QColor(Qt::transparent));
        }
        p.drawRoundedRect(pill, 6, 6);

        const QColor fg =
            !isEnabled() ? QColor("#6e7681")
            : on         ? QColor("#ffffff")
                         : (dark ? QColor(hovered ? "#e6edf3" : "#8b949e")
                                 : QColor(hovered ? "#1f2328" : "#656d76"));
        const QRect iconRect((width() - kIconPx) / 2, int(pill.top()) + 5,
                             kIconPx, kIconPx);
        p.drawPixmap(iconRect.topLeft(),
                     tintedOcticonPixmap(on ? QStringLiteral("lock")
                                            : QStringLiteral("unlock"),
                                         fg, kIconPx));

        QFont f = font();
        f.setPixelSize(10);
        f.setWeight(QFont::DemiBold);
        p.setFont(f);
        p.setPen(fg);
        p.drawText(QRect(2, iconRect.bottom() + 2, width() - 4, 12),
                   Qt::AlignHCenter | Qt::AlignTop,
                   QFontMetrics(f).elidedText(text(), Qt::ElideRight,
                                              width() - 4));
    }
    void enterEvent(QEnterEvent *e) override
    {
        update();
        QToolButton::enterEvent(e);
    }
    void leaveEvent(QEvent *e) override
    {
        update();
        QToolButton::leaveEvent(e);
    }

private:
    static constexpr int kIconPx = 16;
    static constexpr int kHeight = 44;
};

constexpr int kRailItemWidth = 42;
constexpr int kRailItemHeight = 44; // 16px icon + 10px caption + breathing room
constexpr int kRailIconPx = 16;
constexpr int kRepoTabRowTopInset = 4;

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
              "Settings", "Log", "Capture", "Resize", "Restart", "Tasks",
              "Pings", "Account"})
            widest = qMax(widest, metrics.horizontalAdvance(
                                      QString::fromLatin1(caption)) + 4);
        return widest;
    }();
    return width;
}
inline int railWidth() { return railItemWidth() + 6; } // + slim scrollbar

inline QFont navBalanceFont()
{
    static const QFont font = [] {
        QFont f = QGuiApplication::font();
        f.setWeight(QFont::Normal);
        for (int px = 9; px > 6; --px) {
            f.setPixelSize(px);
            if (QFontMetrics(f).horizontalAdvance(
                    QStringLiteral("0.0000 SOL")) <= railItemWidth())
                return f;
        }
        f.setPixelSize(6);
        return f;
    }();
    return font;
}

inline int navBalanceLineHeight()
{
    static const int height = QFontMetrics(navBalanceFont()).height() + 2;
    return height;
}

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
        m_spinTimer = new QTimer(this);
        m_spinTimer->setInterval(60);
        connect(m_spinTimer, &QTimer::timeout, this, [this] {
            m_spinAngle = (m_spinAngle + 30) % 360;
            update();
        });
        m_blinkTimer = new QTimer(this);
        m_blinkTimer->setInterval(550);
        connect(m_blinkTimer, &QTimer::timeout, this, [this] {
            m_blinkOn = !m_blinkOn;
            update();
        });
    }

    void setCompact(bool compact)
    {
        if (m_compact == compact)
            return;
        m_compact = compact;
        setFixedSize(railItemWidth(),
                     m_compact ? 30 : (m_label.isEmpty() ? 40 : kRailItemHeight));
        update();
    }

    void setGlyph(const QString &iconName, const QString &label)
    {
        if (m_iconName == iconName && m_label == label)
            return;
        m_iconName = iconName;
        m_label = label;
        setText(label);
        setAccessibleName(label);
        update();
    }

    void setBadgeCount(int count)
    {
        if (m_badge == count)
            return;
        m_badge = count;
        update();
    }
    int badgeCount() const { return m_badge; }

    void setPendingSyncCount(int count)
    {
        count = qMax(0, count);
        if (m_pendingSync == count)
            return;
        m_pendingSync = count;
        update();
    }
    int pendingSyncCount() const { return m_pendingSync; }

    void setBadgeUrgent(bool urgent)
    {
        if (m_badgeUrgent == urgent)
            return;
        m_badgeUrgent = urgent;
        update();
    }

    void setAlertTint(bool alert)
    {
        if (m_alert == alert)
            return;
        m_alert = alert;
        update();
    }

    void setAccentTint(bool accent)
    {
        if (m_accent == accent)
            return;
        m_accent = accent;
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

    void setActivityBlink(bool on)
    {
        if (m_blink == on)
            return;
        m_blink = on;
        m_blinkOn = true;
        if (m_blink && isVisible())
            m_blinkTimer->start();
        else
            m_blinkTimer->stop();
        update();
    }
    bool activityBlink() const { return m_blink; }

protected:
    void showEvent(QShowEvent *e) override
    {
        if (m_syncing)
            m_spinTimer->start();
        if (m_blink)
            m_blinkTimer->start();
        QPushButton::showEvent(e);
    }
    void hideEvent(QHideEvent *e) override
    {
        m_spinTimer->stop();
        m_blinkTimer->stop();
        QPushButton::hideEvent(e);
    }
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        const bool dark = currentThemeIsDark();
        const bool lit = isEnabled() && (isChecked() || underMouse());
        const bool accentAction = isEnabled() && m_accent;
        const QColor fg =
            !isEnabled() ? QColor(dark ? "#484f58" : "#b6bdc4")
                : accentAction ? QColor("#ffffff")
                : m_alert ? QColor(dark ? (lit ? "#f0b72f" : "#d29922")
                                         : (lit ? "#7d4e00" : "#9a6700"))
                          : (dark ? QColor(lit ? "#e6edf3" : "#8b949e")
                                  : QColor(lit ? "#1f2328" : "#656d76"));
        const bool showLabel = !m_compact && !m_label.isEmpty();

        if (accentAction) {
            const QColor fill = dark
                                    ? QColor(lit ? "#2ea043" : "#238636")
                                    : QColor(lit ? "#2ea043" : "#1f883d");
            p.setPen(Qt::NoPen);
            p.setBrush(fill);
            p.drawRoundedRect(QRectF(rect()).adjusted(1, 2, -1, -2), 6, 6);
        }

        if (isChecked() && !accentAction)
            p.fillRect(QRectF(0, 4, 2, height() - 8), QColor("#2ea043"));

        const int iconPx = kRailIconPx;
        const QRect iconRect((width() - iconPx) / 2,
                             showLabel ? 6 : (height() - iconPx) / 2,
                             iconPx, iconPx);
        if (property("fmSpinning").toBool() && !icon().isNull())
            icon().paint(&p, iconRect, Qt::AlignCenter, QIcon::Normal, QIcon::Off);
        else
            p.drawPixmap(iconRect.topLeft(),
                         tintedOcticonPixmap(m_iconName, fg, iconPx));

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
            p.drawText(QRect(1, iconRect.bottom() + 2, width() - 2, 14),
                       Qt::AlignHCenter | Qt::AlignTop,
                       QFontMetrics(f).elidedText(m_label, Qt::ElideRight,
                                                  width() - 2));
        }

        if (m_syncing) {
            const int s = 14;
            const QPoint at(iconRect.right() - s / 2 + 4,
                            qMax(0, iconRect.top() - 4));
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

        if (m_blink && m_blinkOn) {
            const int d = 8;
            const QRect lamp(iconRect.right() - 2, iconRect.bottom() - d + 1,
                             d, d);
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(dark ? "#0d1117" : "#ffffff"));
            p.drawEllipse(lamp.adjusted(-2, -2, 2, 2));
            p.setBrush(QColor("#3fb950"));
            p.drawEllipse(lamp);
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
    bool m_accent = false;
    bool m_syncing = false;
    bool m_compact = false;
    bool m_blink = false;
    bool m_blinkOn = true;
    QTimer *m_spinTimer = nullptr;
    QTimer *m_blinkTimer = nullptr;
    int m_spinAngle = 0;
};

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

inline QString voiceEngine()
{
    const QString e =
        QSettings().value(kVoiceEngineSetting).toString().trimmed().toLower();
    return e == QStringLiteral("parakeet") ? QStringLiteral("parakeet")
                                           : QStringLiteral("whisper");
}

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

inline bool voiceInputReady()
{
    return voiceEngine() == QStringLiteral("parakeet") ? parakeetInstalled()
                                                       : whisperInstalled();
}

inline QString voiceModelLabel()
{
    return voiceEngine() == QStringLiteral("parakeet")
               ? QStringLiteral("Parakeet %1").arg(parakeetModelName())
               : QStringLiteral("Whisper %1").arg(whisperModelName());
}

struct AudioRecorderCommand {
    QString program;
    QStringList args;
};

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

inline QList<QPair<QString, QString>> voiceInputDevices()
{
    QList<QPair<QString, QString>> out;
    out.append({QStringLiteral("System default"), QString()});
    const QString tool = preferredAudioRecorder();
    if (tool.isEmpty())
        return out;
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
    const QString device =
        QSettings().value(kVoiceInputDeviceSetting).toString().trimmed();
#if defined(Q_OS_MACOS)
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

inline double wavPeakAmplitude(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return -1.0;
    const QByteArray d = f.readAll();
    if (d.size() < 44 || !d.startsWith("RIFF") || d.mid(8, 4) != "WAVE")
        return -1.0;
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

inline constexpr double kVoiceSpokeThreshold = 0.02;

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

inline void fitFileListToWidestEntry(QListWidget *list, int minW = 180, int maxW = 520)
{
    if (!list)
        return;
    const QFontMetrics fm(list->fontMetrics());
    int widest = 0;
    for (int i = 0; i < list->count(); ++i)
        widest = qMax(widest, fm.horizontalAdvance(list->item(i)->text()));
    const int chrome = 52 + list->verticalScrollBar()->sizeHint().width();
    list->setMinimumWidth(qBound(minW, widest + chrome, maxW));
}

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

const QString kLogPromptAnchorPrefix = QStringLiteral("fmlogprompt:");
const QString kLogPromptIconResource = QStringLiteral("logprompt://add");

inline QString logPromptAnchorHref(const QString &storedLine)
{
    return kLogPromptAnchorPrefix +
           QString::fromLatin1(QUrl::toPercentEncoding(storedLine.trimmed()));
}

inline QString logPromptAnchorLine(const QString &href)
{
    if (!href.startsWith(kLogPromptAnchorPrefix))
        return QString();
    return QUrl::fromPercentEncoding(
        href.mid(kLogPromptAnchorPrefix.size()).toLatin1());
}

const QString kLogSourceAnchorPrefix = QStringLiteral("fmlogsrc:");

inline QString logSourceAnchorHref(const QString &relativePath, int line)
{
    return kLogSourceAnchorPrefix + QString::number(line) + QLatin1Char(':') +
           relativePath;
}

inline bool logSourceAnchorTarget(const QString &href, QString *relativePath,
                                  int *line)
{
    if (!href.startsWith(kLogSourceAnchorPrefix))
        return false;
    const QString rest = href.mid(kLogSourceAnchorPrefix.size());
    const int colon = rest.indexOf(QLatin1Char(':'));
    if (colon <= 0)
        return false;
    bool numeric = false;
    const int parsed = rest.left(colon).toInt(&numeric);
    if (!numeric)
        return false;
    if (line)
        *line = parsed;
    if (relativePath)
        *relativePath = rest.mid(colon + 1);
    return true;
}

inline QString logSourceAnchorHtml(const QString &relativePath, int line,
                                   bool dark)
{
    if (relativePath.isEmpty() || line <= 0)
        return QString();
    return QStringLiteral(
               "&nbsp;&nbsp;<a href='%1' style='color:%2; "
               "text-decoration:none'>%3</a>")
        .arg(logSourceAnchorHref(relativePath, line),
             dark ? QStringLiteral("#6e7681") : QStringLiteral("#8c959f"),
             forkmesh::logSourceLabel(relativePath, line).toHtmlEscaped());
}

inline QString octiconForBackgroundTaskWord(const QString &word)
{
    static const QHash<QString, QString> icons{
        {QStringLiteral("agent"), QStringLiteral("sparkle")},
        {QStringLiteral("agents"), QStringLiteral("sparkle")},
        {QStringLiteral("actions"), QStringLiteral("workflow")},
        {QStringLiteral("avatars"), QStringLiteral("person")},
        {QStringLiteral("chat"), QStringLiteral("comment")},
        {QStringLiteral("cleanup"), QStringLiteral("trash")},
        {QStringLiteral("diff"), QStringLiteral("file-diff")},
        {QStringLiteral("fork"), QStringLiteral("repo-forked")},
        {QStringLiteral("git"), QStringLiteral("git-commit")},
        {QStringLiteral("issues"), QStringLiteral("issue-opened")},
        {QStringLiteral("mirrors"), QStringLiteral("server")},
        {QStringLiteral("net"), QStringLiteral("broadcast")},
        {QStringLiteral("pulls"), QStringLiteral("git-pull-request")},
        {QStringLiteral("releases"), QStringLiteral("tag")},
        {QStringLiteral("repo"), QStringLiteral("repo")},
        {QStringLiteral("scan"), QStringLiteral("search")},
        {QStringLiteral("sync"), QStringLiteral("sync")},
        {QStringLiteral("uibuild"), QStringLiteral("code")},
    };
    return icons.value(word, QStringLiteral("gear"));
}

inline QString backgroundTaskWordFromLogMessage(const QString &storedLine)
{
    QString line = storedLine.trimmed();
    const QString prefix = QStringLiteral("Background ");
    if (!line.startsWith(prefix))
        return QString();
    line = line.mid(prefix.size()).trimmed();
    if (line.startsWith(backgroundOkGlyph()) || line.startsWith(backgroundNotGlyph()))
        line = line.mid(1).trimmed();
    if (line.isEmpty())
        return QString();
    const int split = line.indexOf(QLatin1Char(' '));
    return line.left(split < 0 ? line.size() : split).toLower();
}

inline QString logIconSpacerTag(QTextEdit *view, int size)
{
    if (!view)
        return QString();
    static QHash<int, QPixmap> blanks;
    if (!blanks.contains(size)) {
        QPixmap blank(size, size);
        blank.fill(Qt::transparent);
        blanks.insert(size, blank);
    }
    const QString resource = QStringLiteral("logspacer://%1").arg(size);
    view->document()->addResource(QTextDocument::ImageResource, QUrl(resource),
                                  blanks.value(size));
    return QStringLiteral("<img src='%1' width='%2' height='%2' "
                          "style='vertical-align:middle'>&nbsp;")
        .arg(resource)
        .arg(size);
}

inline QString logBgtaskIconTag(QTextEdit *view, const QString &storedLine)
{
    if (!view)
        return QString();
    QString message = storedLine;
    if (storedLine.size() >= 21 && storedLine.at(10) == QLatin1Char(' '))
        message = storedLine.mid(21);
    const QString word = backgroundTaskWordFromLogMessage(message);
    if (word.isEmpty())
        return logIconSpacerTag(view, 11);
    const QString icon = octiconForBackgroundTaskWord(word);
    const QString resource = QStringLiteral("logbgtask://") + word + QLatin1String("-")
                             + icon;
    view->document()->addResource(
        QTextDocument::ImageResource, QUrl(resource),
        tintedOcticonPixmap(icon, QColor("#8b949e"), 11));
    return QStringLiteral("<img src='%1' width='11' height='11' "
                          "style='vertical-align:middle'>&nbsp;")
        .arg(resource);
}

inline QString logPromptIconTag(QTextEdit *view, const QString &storedLine)
{
    if (!view || storedLine.trimmed().isEmpty())
        return QString();
    view->document()->addResource(
        QTextDocument::ImageResource, QUrl(kLogPromptIconResource),
        tintedOcticonPixmap(QStringLiteral("plus"), QColor("#8b949e"), 12));
    const QString appIcon = logBgtaskIconTag(view, storedLine);
    return QStringLiteral(
               "<a href='%1' style='text-decoration:none'><img src='%2' "
               "width='11' height='11' style='vertical-align:middle'></a>&nbsp;")
        .arg(logPromptAnchorHref(storedLine), kLogPromptIconResource) +
        appIcon;
}

inline QString serverHost(const QString &serverUrl)
{
    const QString trimmed = serverUrl.trimmed();
    QUrl url(trimmed);
    if (!url.host().isEmpty())
        return url.host().toLower();

    QString host = trimmed;
    const int slash = host.indexOf(QLatin1Char('/'));
    if (slash >= 0)
        host = host.left(slash);
    return host.toLower();
}

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

inline QElapsedTimer &keepAliveClock()
{
    static QElapsedTimer c;
    if (!c.isValid())
        c.start();
    return c;
}
inline qint64 g_lastKeepAlivePumpMs = 0;

inline void pumpKeepAlive()
{
    g_lastKeepAlivePumpMs = keepAliveClock().elapsed();
    const KeepAlivePumpScope pumping;
    QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents, 12);
}

inline QString gitPathspecCrumbName(const QString &pathspec)
{
    QString path = pathspec;
    if (path.startsWith(QLatin1String(":("))) {
        const int close = path.indexOf(QLatin1Char(')'));
        if (close > 0)
            path = path.mid(close + 1);
    } else if (path.startsWith(QLatin1String(":!")) ||
               path.startsWith(QLatin1String(":^"))) {
        path = path.mid(2);
    }
    const int slash = path.lastIndexOf(QLatin1Char('/'));
    return slash < 0 ? path : path.mid(slash + 1);
}

inline QString gitArgsCrumb(const QString &program, const QStringList &argsIn)
{
    QStringList args = argsIn;
    QString repo;
    // Leading "-c key=value" overrides come first on our fetch/clone lines, and
    // the value is a secret: viewAuthGitArgs/importAuthGitArgs pass the signed
    // view token as "-c http.extraHeader=Authorization: Basic <base64>". A crumb
    // reaches the log file and actions.jsonl, so keep the key (an authed read is
    // worth seeing) and drop the credential.
    QStringList config;
    while (args.size() >= 2 && args.first() == QLatin1String("-c")) {
        const QString pair = args.at(1);
        const int eq = pair.indexOf(QLatin1Char('='));
        config << QStringLiteral("-c ") +
                      (eq < 0 ? pair
                              : pair.left(eq + 1) + QString::fromUtf8("…"));
        args = args.mid(2);
    }
    if (args.size() >= 2 && args.first() == QLatin1String("-C")) {
        repo = QFileInfo(args.at(1)).fileName();
        args = args.mid(2);
    }
    constexpr int kShownPaths = 5;
    QString paths;
    const int sep = args.indexOf(QLatin1String("--"));
    if (sep >= 0) {
        const QStringList pathspecs = args.mid(sep + 1);
        args = args.mid(0, sep + 1); // keep the "--" on the command
        if (pathspecs.size() > kShownPaths) {
            QStringList shown;
            for (int i = 0; i < kShownPaths; ++i)
                shown << gitPathspecCrumbName(pathspecs.at(i));
            paths = shown.join(QStringLiteral(", ")) +
                    QStringLiteral(" +%1 more")
                        .arg(pathspecs.size() - kShownPaths);
        } else {
            paths = pathspecs.join(QLatin1Char(' '));
        }
    }
    QString cmd = (program + QLatin1Char(' ') +
                   (config + args).join(QLatin1Char(' ')))
                      .simplified();
    constexpr int kMaxCommand = 80;
    if (cmd.size() > kMaxCommand)
        cmd = cmd.left(kMaxCommand - 1) + QStringLiteral("…");
    if (!paths.isEmpty()) {
        cmd += QLatin1Char(' ') + paths.simplified();
        constexpr int kMaxLine = 200; // backstop for five very long names
        if (cmd.size() > kMaxLine)
            cmd = cmd.left(kMaxLine - 1) + QStringLiteral("…");
    }
    if (!repo.isEmpty())
        cmd += QStringLiteral(" (%1)").arg(repo);
    return cmd;
}

inline QString gitBlockingCrumb(const QProcess &process)
{
    return gitArgsCrumb(process.program(), process.arguments());
}

inline QString gitCommandLine(const QProcess &process)
{
    return (process.program() + QLatin1Char(' ') +
            process.arguments().join(QLatin1Char(' ')))
        .simplified();
}

inline QString gitTimeoutError(QProcess &process, int waitedMs)
{
    const QString command = gitCommandLine(process);
    process.kill();
    process.waitForFinished(200); // reap so the child's pipes flush
    QString output = QString::fromUtf8(process.readAllStandardError()).trimmed();
    const QString stdOut =
        QString::fromUtf8(process.readAllStandardOutput()).trimmed();
    if (!stdOut.isEmpty())
        output += (output.isEmpty() ? QString() : QStringLiteral("\n")) + stdOut;
    constexpr int kMaxOutput = 4000;
    if (output.size() > kMaxOutput)
        output = output.left(kMaxOutput) + QStringLiteral("\n… (output truncated)");
    QString err = QStringLiteral("git timed out after %1s: %2")
                      .arg(waitedMs / 1000)
                      .arg(command);
    if (!output.isEmpty())
        err += QLatin1Char('\n') + output;
    return err;
}

inline bool isTransientGitError(const QString &err)
{
    static const char *const kMarkers[] = {
        "timed out",
        "index.lock",
        "unable to create",
        "cannot lock ref",
        "unable to stat",
        "no such file",
        "resource temporarily unavailable",
        "resource deadlock",
    };
    for (const char *marker : kMarkers)
        if (err.contains(QLatin1String(marker), Qt::CaseInsensitive))
            return true;
    return false;
}

inline QString worktreeTabKey(const QString &absPath)
{
    return QLatin1String("worktree:") + absPath;
}

inline QString gitignoreRuleForPath(const QString &relPath)
{
    QString rule;
    rule.reserve(relPath.size() + 8);
    for (const QChar c : relPath) {
        if (c == QLatin1Char('*') || c == QLatin1Char('?') ||
            c == QLatin1Char('[') || c == QLatin1Char(']') ||
            c == QLatin1Char('\\'))
            rule.append(QLatin1Char('\\'));
        rule.append(c);
    }
    if (rule.endsWith(QLatin1Char(' '))) {
        rule.chop(1);
        rule.append(QLatin1String("\\ "));
    }
    return QLatin1Char('/') + rule;
}

inline QString branchDiffErrorHtml(const QString &branch, const QString &err,
                                   int attempts)
{
    const QString message =
        err.trimmed().isEmpty() ? QStringLiteral("git failed") : err.trimmed();
    const int split = message.indexOf(QLatin1Char('\n'));
    const QString headline = split < 0 ? message : message.left(split);
    const bool dark = qApp->palette().color(QPalette::Base).lightness() < 128;
    QString html =
        QStringLiteral("<p style='color:#f85149'><b>Could not diff %1:</b> %2</p>")
            .arg(branch.toHtmlEscaped(), headline.toHtmlEscaped());
    html += QStringLiteral("<pre style='background:%1;color:%2;padding:8px;'>%3</pre>")
                .arg(dark ? QStringLiteral("#161b22") : QStringLiteral("#f6f8fa"),
                     dark ? QStringLiteral("#c9d1d9") : QStringLiteral("#24292f"),
                     message.toHtmlEscaped());
    html +=
        QStringLiteral("<p style='color:#8b949e'>Tried %1 %2. "
                       "<a href='retry:diff' style='color:#58a6ff'>Retry</a></p>")
            .arg(attempts)
            .arg(attempts == 1 ? QStringLiteral("time") : QStringLiteral("times"));
    return html;
}

inline QString inlinePixmapMarkup(const QPixmap &pixmap, int size)
{
    if (pixmap.isNull())
        return QString();
    QByteArray png;
    QBuffer buffer(&png);
    buffer.open(QIODevice::WriteOnly);
    pixmap.save(&buffer, "PNG");
    return QStringLiteral("<img src='data:image/png;base64,%1' width='%2' "
                          "height='%2'>")
        .arg(QString::fromLatin1(png.toBase64()))
        .arg(size);
}

inline QString mergedBranchFromMergeSubject(const QString &subject)
{
    const QString s = subject.trimmed();
    if (!s.startsWith(QLatin1String("Merge ")))
        return QString();
    if (s.startsWith(QLatin1String("Merge pull request #"))) {
        const int from = s.indexOf(QLatin1String(" from "));
        if (from < 0)
            return QString();
        const QString head = s.mid(from + 6).trimmed().section(QLatin1Char(' '), 0, 0);
        const int slash = head.indexOf(QLatin1Char('/'));
        return slash < 0 ? head : head.mid(slash + 1);
    }
    if (s.startsWith(QLatin1String("Merge branch '"))) {
        const int end = s.indexOf(QLatin1Char('\''), 14);
        return end < 0 ? QString() : s.mid(14, end - 14);
    }
    const int into = s.lastIndexOf(QLatin1String(" into "));
    if (into <= 6)
        return QString();
    return s.mid(6, into - 6).trimmed();
}

struct MergeCelebrationRow {
    QString branch;      // empty when the merge subject named no branch
    QString mergeCommit; // full sha — what the row links to once the branch is gone
    bool branchStillExists = false;
    qint64 whenSecs = 0;
    int files = 0;
    int insertions = 0;
    int deletions = 0;
    QString actor;   // "Opus 5" for an agent, the account/commit name for a person
    QString detail;
    QPixmap avatar;  // model portrait or identicon, already sized
    bool byAgent = false;
    bool current = false; // the landing being celebrated
};

inline QString mergeCelebrationRowHref(const MergeCelebrationRow &row)
{
    if (row.branchStillExists && !row.branch.isEmpty())
        return QLatin1String("fmbranch:") +
               QString::fromLatin1(QUrl::toPercentEncoding(row.branch));
    return row.mergeCommit.isEmpty() ? QString()
                                     : QLatin1String("fmcommit:") + row.mergeCommit;
}

struct MergeActorTally {
    QString actor;
    QPixmap avatar;
    int merges = 0;
    int files = 0;
    int insertions = 0;
    int deletions = 0;
};

inline QList<MergeActorTally> mergeCelebrationTallies(
    const QList<MergeCelebrationRow> &today)
{
    QList<MergeActorTally> tallies;
    for (const MergeCelebrationRow &row : today) {
        if (row.actor.isEmpty())
            continue; // an unattributed landing belongs to nobody's tally
        int at = -1;
        for (int i = 0; i < tallies.size(); ++i) {
            if (tallies.at(i).actor == row.actor) {
                at = i;
                break;
            }
        }
        if (at < 0) {
            MergeActorTally fresh;
            fresh.actor = row.actor;
            tallies.append(fresh);
            at = tallies.size() - 1;
        }
        MergeActorTally &tally = tallies[at];
        ++tally.merges;
        tally.files += qMax(0, row.files);
        tally.insertions += qMax(0, row.insertions);
        tally.deletions += qMax(0, row.deletions);
        if (tally.avatar.isNull())
            tally.avatar = row.avatar;
    }
    tallies.erase(std::remove_if(tallies.begin(), tallies.end(),
                                 [](const MergeActorTally &tally) {
                                     return tally.merges < 2;
                                 }),
                  tallies.end());
    std::sort(tallies.begin(), tallies.end(),
              [](const MergeActorTally &a, const MergeActorTally &b) {
                  if (a.merges != b.merges)
                      return a.merges > b.merges;
                  return a.actor.localeAwareCompare(b.actor) < 0;
              });
    return tallies;
}

inline QString mergeCelebrationHtml(const MergeCelebrationRow &landed,
                                    const QString &base,
                                    const QList<MergeCelebrationRow> &today,
                                    int commits, int truncated,
                                    const QList<MergeActorTally> &tallies = {})
{
    const bool dark = qApp->palette().color(QPalette::Base).lightness() < 128;
    const QString fg = dark ? QStringLiteral("#e6edf3") : QStringLiteral("#1f2328");
    const QString muted = QStringLiteral("#8b949e");
    const QString heroBg = dark ? QStringLiteral("#0c2318") : QStringLiteral("#eaf6ee");
    const QString heroBorder =
        dark ? QStringLiteral("#238636") : QStringLiteral("#8fd0a4");
    const QString rowBg = dark ? QStringLiteral("#161b22") : QStringLiteral("#f6f8fa");
    const QString currentBg =
        dark ? QStringLiteral("#12261c") : QStringLiteral("#e6ffec");
    const QString link = dark ? QStringLiteral("#58a6ff") : QStringLiteral("#0969da");
    const QString green = QStringLiteral("#3fb950");
    const QString escapedBase = base.toHtmlEscaped();

    const auto churn = [&](int files, int insertions, int deletions) {
        QStringList parts;
        if (files > 0)
            parts << (files == 1 ? QStringLiteral("1 file")
                                 : QStringLiteral("%1 files").arg(files));
        if (insertions > 0)
            parts << QStringLiteral("<span style='color:#3fb950'>+%1</span>")
                         .arg(insertions);
        if (deletions > 0)
            parts << QString::fromUtf8("<span style='color:#f85149'>\xE2\x88\x92%1"
                                       "</span>")
                         .arg(deletions);
        return parts.join(QStringLiteral(" "));
    };
    const auto rowChurnOf = [&](const MergeCelebrationRow &row) {
        return churn(row.files, row.insertions, row.deletions);
    };

    QString html = QStringLiteral("<div style='padding:6px 10px 10px 10px;'>");

    QStringList credit;
    if (!landed.actor.isEmpty())
        credit << QStringLiteral("by <b style='color:%1'>%2</b>")
                      .arg(fg, landed.actor.toHtmlEscaped());
    if (!landed.detail.isEmpty())
        credit << landed.detail.toHtmlEscaped();
    if (landed.whenSecs > 0)
        credit << formatIssueRelativeTime(landed.whenSecs * 1000);
    if (commits > 0)
        credit << (commits == 1 ? QStringLiteral("1 commit")
                                : QStringLiteral("%1 commits").arg(commits));
    const QString landedChurn = rowChurnOf(landed);
    if (!landedChurn.isEmpty())
        credit << landedChurn;

    html += QStringLiteral(
                "<table width='100%' cellspacing='0' cellpadding='0' "
                "style='background:%1;border:1px solid %2;'><tr>")
                .arg(heroBg, heroBorder);
    if (!landed.avatar.isNull())
        html += QStringLiteral("<td width='84' align='center' valign='middle' "
                               "style='padding:16px 0 16px 16px;'>%1</td>")
                    .arg(inlinePixmapMarkup(landed.avatar, 56));
    html +=
        QStringLiteral(
            "<td valign='middle' style='padding:16px;'>"
            "<div style='font-size:20px;font-weight:700;color:%1;'>"
            "&#10003; Merged!</div>"
            "<div style='font-size:15px;color:%2;'>"
            "<b style='font-family:monospace;'>%3</b> is part of "
            "<b style='font-family:monospace;'>%4</b> now.</div>"
            "<div style='font-size:12px;color:%5;'>%6</div></td></tr></table>")
            .arg(green, fg, landed.branch.toHtmlEscaped(), escapedBase, muted,
                 credit.join(QString::fromUtf8(" \xC2\xB7 ")));

    if (!tallies.isEmpty()) {
        html += QStringLiteral(
                    "<p style='color:%1;font-size:12px;font-weight:700;"
                    "margin-top:16px;margin-bottom:4px;'>"
                    "MORE THAN ONE TODAY</p>")
                    .arg(muted);
        html += QStringLiteral(
            "<table width='100%' cellspacing='3' cellpadding='0'>");
        for (const MergeActorTally &tally : tallies) {
            html += QStringLiteral("<tr><td width='38' align='center' "
                                   "valign='middle' style='background:%1;"
                                   "padding:7px 0 7px 8px;'>%2</td>")
                        .arg(rowBg,
                             tally.avatar.isNull()
                                 ? QString()
                                 : inlinePixmapMarkup(tally.avatar, 22));
            const QString tallyChurn =
                churn(tally.files, tally.insertions, tally.deletions);
            html += QStringLiteral(
                        "<td valign='middle' style='background:%1;"
                        "padding:7px 10px;'>"
                        "<b style='color:%2'>%3</b>"
                        "<div style='font-size:11px;color:%4;'>%5</div></td>")
                        .arg(rowBg, fg, tally.actor.toHtmlEscaped(), muted,
                             tallyChurn);
            html += QStringLiteral(
                        "<td width='90' align='right' valign='middle' "
                        "style='background:%1;padding:7px 10px 7px 0;'>"
                        "<b style='color:%2;font-size:16px;'>%4</b>"
                        "<span style='color:%3;font-size:11px;'> merges</span>"
                        "</td></tr>")
                        .arg(rowBg, green, muted)
                        .arg(tally.merges);
        }
        html += QStringLiteral("</table>");
    }

    if (!today.isEmpty()) {
        html += QStringLiteral(
                    "<p style='color:%1;font-size:12px;font-weight:700;"
                    "margin-top:16px;margin-bottom:4px;'>"
                    "MERGED INTO %2 TODAY &#183; %3</p>")
                    .arg(muted, escapedBase.toUpper())
                    .arg(today.size() + truncated);
        html += QStringLiteral(
            "<table width='100%' cellspacing='3' cellpadding='0'>");
        for (const MergeCelebrationRow &row : today) {
            const QString background = row.current ? currentBg : rowBg;
            html += QStringLiteral("<tr><td width='38' align='center' "
                                   "valign='middle' style='background:%1;"
                                   "padding:7px 0 7px 8px;'>%2</td>")
                        .arg(background,
                             row.avatar.isNull()
                                 ? QString()
                                 : inlinePixmapMarkup(row.avatar, 22));
            const QString name = row.branch.isEmpty()
                                     ? row.mergeCommit.left(8)
                                     : row.branch;
            const QString href = mergeCelebrationRowHref(row);
            const QString title =
                href.isEmpty()
                    ? QStringLiteral("<b style='color:%1'>%2</b>")
                          .arg(fg, name.toHtmlEscaped())
                    : QStringLiteral("<a href='%1' style='color:%2;"
                                     "text-decoration:none;font-weight:700;'>%3</a>")
                          .arg(href, link, name.toHtmlEscaped());
            QStringList meta;
            if (!row.actor.isEmpty())
                meta << row.actor.toHtmlEscaped();
            if (!row.detail.isEmpty())
                meta << row.detail.toHtmlEscaped();
            if (row.whenSecs > 0)
                meta << QDateTime::fromSecsSinceEpoch(row.whenSecs)
                            .toString(QStringLiteral("HH:mm"));
            const QString rowChurn = rowChurnOf(row);
            if (!rowChurn.isEmpty())
                meta << rowChurn;
            html += QStringLiteral(
                        "<td valign='middle' style='background:%1;padding:7px 10px;'>"
                        "%2%3<div style='font-size:11px;color:%4;'>%5</div>"
                        "</td></tr>")
                        .arg(background, title,
                             row.current
                                 ? QStringLiteral(" <span style='color:%1;"
                                                  "font-size:11px;'>just now</span>")
                                       .arg(green)
                                 : QString(),
                             muted, meta.join(QString::fromUtf8(" \xC2\xB7 ")));
        }
        html += QStringLiteral("</table>");
        if (truncated > 0)
            html += QStringLiteral("<p style='color:%1;font-size:11px;'>"
                                   "%2 earlier %3 today are not listed.</p>")
                        .arg(muted)
                        .arg(truncated)
                        .arg(truncated == 1 ? QStringLiteral("merge")
                                            : QStringLiteral("merges"));
    }

    QStringList ways;
    if (!base.isEmpty())
        ways << QStringLiteral("go back to <a href='fmbranch:%1' "
                               "style='color:%2'>%3</a>")
                    .arg(QString::fromLatin1(QUrl::toPercentEncoding(base)), link,
                         escapedBase);
    if (!landed.mergeCommit.isEmpty())
        ways << QStringLiteral("<a href='fmcommit:%1' style='color:%2'>open the "
                               "merge commit</a>")
                    .arg(landed.mergeCommit, link);
    html += QStringLiteral("<p style='color:%1;margin-top:14px;'>Pick a branch in "
                           "the list to see its changes%2.</p>")
                .arg(muted,
                     ways.isEmpty()
                         ? QString()
                         : QStringLiteral(", ") +
                               ways.join(QStringLiteral(", or ")));
    html += QStringLiteral("</div>");
    return html;
}

inline bool waitForGit(QProcess &process, QString *err)
{
    const QCoreApplication *app = QCoreApplication::instance();
    const bool onGuiThread = app && QThread::currentThread() == app->thread();

    // Announce the wait to the footer's background strip. This is
    // the one chokepoint every git subprocess passes through, on the GUI thread
    // and off it, so a single ticket here is what makes "git" appear while a
    // slow fetch/clone/log runs. Fast reads never reach the strip's show delay,
    // so the hot path pays only an atomic increment.
    const forkmesh::BackgroundScope gitActivity(
        QStringLiteral("git"), gitBlockingCrumb(process),
        onGuiThread ? forkmesh::ActionTelemetry::Execution::UiBlocking
                    : forkmesh::ActionTelemetry::Execution::Worker);

    std::optional<BlockingCallScope> crumb;
    if (onGuiThread)
        crumb.emplace(gitBlockingCrumb(process));

    if (!onGuiThread) {
        if (process.waitForFinished(8000))
            return true;
        const QString message = gitTimeoutError(process, 8000);
        if (err)
            *err = message;
        return false;
    }
    if (keepAliveClock().elapsed() - g_lastKeepAlivePumpMs >= 100)
        pumpKeepAlive();
    QElapsedTimer timer;
    timer.start();
    while (!process.waitForFinished(40)) {
        if (process.state() == QProcess::NotRunning)
            return true; // exited between polls; caller inspects the exit code
        if (timer.hasExpired(8000)) {
            const QString message = gitTimeoutError(process, 8000);
            if (err)
                *err = message;
            return false;
        }
        pumpKeepAlive();
    }
    return true;
}

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

inline QString resolveGitDirPath(const QString &dir)
{
    if (dir.trimmed().isEmpty())
        return QString();
    const QFileInfo dotGit(dir + QStringLiteral("/.git"));
    if (dotGit.isDir())
        return dotGit.absoluteFilePath();
    if (dotGit.isFile()) {
        QFile f(dotGit.absoluteFilePath());
        if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
            return QString();
        const QString line = QString::fromUtf8(f.readLine(4096)).trimmed();
        if (!line.startsWith(QLatin1String("gitdir:")))
            return QString();
        const QString target = line.mid(7).trimmed();
        if (target.isEmpty())
            return QString();
        return QDir::isAbsolutePath(target)
                   ? target
                   : QDir(dir).absoluteFilePath(target);
    }
    if (QFileInfo::exists(dir + QStringLiteral("/HEAD")))
        return dir;
    return QString();
}

inline QString headBranchFromFile(const QString &dir)
{
    const QString gitDir = resolveGitDirPath(dir);
    if (gitDir.isEmpty())
        return QString();
    QFile head(gitDir + QStringLiteral("/HEAD"));
    if (!head.open(QIODevice::ReadOnly | QIODevice::Text))
        return QString();
    const QString line = QString::fromUtf8(head.readLine(4096)).trimmed();
    if (!line.startsWith(QLatin1String("ref:")))
        return QString(); // detached HEAD: a raw object name
    const QString ref = line.mid(4).trimmed();
    if (!ref.startsWith(QLatin1String("refs/heads/")))
        return QString();
    return ref.mid(11);
}

struct GitCaptureResult {
    bool ok = false;
    QByteArray output;
    QString error;
};

inline GitCaptureResult runGitCaptureDirect(const QString &dir,
                                            const QStringList &args,
                                            const QByteArray *input = nullptr)
{
    GitCaptureResult result;
    QProcess process;
    process.start("git", QStringList{"-C", dir} + args);
    if (input) {
        process.write(*input);
        process.closeWriteChannel();
    }
    if (!waitForGit(process, &result.error))
        return result;
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
        result.error = QString::fromUtf8(process.readAllStandardError()).trimmed();
        return result;
    }
    result.output = process.readAllStandardOutput();
    result.ok = true;
    return result;
}

inline GitCaptureResult runGitCaptureBackgrounded(const QString &dir,
                                                  const QStringList &args,
                                                  const QByteArray *input = nullptr)
{
    const QCoreApplication *app = QCoreApplication::instance();
    if (!app || QThread::currentThread() != app->thread())
        return runGitCaptureDirect(dir, args, input);

    const QByteArray inputCopy = input ? *input : QByteArray();
    const bool hasInput = input != nullptr;
    QFutureWatcher<GitCaptureResult> watcher;
    QEventLoop loop;
    QObject::connect(&watcher, &QFutureWatcher<GitCaptureResult>::finished,
                     &loop, &QEventLoop::quit);
    watcher.setFuture(QtConcurrent::run([dir, args, inputCopy, hasInput] {
        return runGitCaptureDirect(dir, args,
                                   hasInput ? &inputCopy : nullptr);
    }));
    if (!watcher.isFinished())
        loop.exec(QEventLoop::ExcludeUserInputEvents);
    return watcher.result();
}

inline bool runGitCapture(const QString &dir, const QStringList &args,
                          QByteArray *out, QString *err)
{
    const GitCaptureResult result = runGitCaptureBackgrounded(dir, args);
    if (out)
        *out = result.output;
    if (err)
        *err = result.error;
    return result.ok;
}

inline bool runGitCaptureInput(const QString &dir, const QStringList &args,
                               const QByteArray &input, QByteArray *out,
                               QString *err)
{
    const GitCaptureResult result =
        runGitCaptureBackgrounded(dir, args, &input);
    if (out)
        *out = result.output;
    if (err)
        *err = result.error;
    return result.ok;
}

inline QString worktreeHeadBranch(const QString &workTree)
{
    if (workTree.trimmed().isEmpty() || !QDir(workTree).exists(QStringLiteral(".git")))
        return QString();
    if (const QString fast = headBranchFromFile(workTree); !fast.isEmpty())
        return fast;
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

inline bool checkoutReleasingWorktree(const QString &dir, const QString &branch,
                               QString *err)
{
    if (runGitCapture(dir, {"checkout", branch}, nullptr, err))
        return true;
    if (err && err->contains(QLatin1String("already used by worktree"))) {
        if (releaseWorktreeHoldingBranch(dir, branch) &&
            runGitCapture(dir, {"checkout", branch}, nullptr, err))
            return true;
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

inline qint64 mirrorRepoSizeBytes(const QString &mirrorPath)
{
    if (mirrorPath.trimmed().isEmpty() || !QDir(mirrorPath).exists())
        return 0;
    QByteArray out;
    if (!runGitCapture(mirrorPath, {"count-objects", "-v"}, &out, nullptr))
        return 0;
    return parseCountObjectsSizeBytes(out);
}

inline QString mirrorReleaseCasRoot(const QString &mirrorPath)
{
    return QDir(mirrorPath).filePath(QStringLiteral("forkmesh-releases/sha256"));
}
inline QString mirrorReleaseBlobPath(const QString &mirrorPath, const QString &hash)
{
    return QDir(mirrorReleaseCasRoot(mirrorPath))
        .filePath(QStringLiteral("%1/%2/data").arg(hash.left(2), hash));
}

inline int releaseCasBlobCount(const QDir &casDir)
{
    if (!casDir.exists())
        return 0; // no artifacts stored yet
    int count = 0;
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
inline int checkoutArtifactCount(const QString &localPath)
{
    if (localPath.trimmed().isEmpty() || !QDir(localPath).exists())
        return -1;
    return releaseCasBlobCount(QDir(
        QDir(localPath).filePath(QStringLiteral(".forkmesh/release-blobs/sha256"))));
}

struct MirrorReleaseBlob {
    QString hash; // the blob's sha256 (its own directory name / identity)
    QString path; // absolute path to the "data" file on disk
    qint64 size = 0; // byte size of the stored blob
};

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
// and release binaries live beside the git data rather than in it,
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

inline QString nodeListIdentityKey(const MemberInfo &m)
{
    const QString nodeName = m.nodeName.trimmed();
    if (!nodeName.isEmpty())
        return nodeName;
    return m.name.trimmed();
}

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

inline int mirrorOpenIssueCount(const QString &mirrorPath, const QString &branch)
{
    if (mirrorPath.trimmed().isEmpty() || branch.isEmpty() ||
        !QDir(mirrorPath).exists())
        return -1;
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

inline int mirrorIssueCount(const QString &mirrorPath, const QString &branch)
{
    return mirrorOpenIssueCount(mirrorPath, branch);
}
inline int mirrorIssueMaxNumber(const QString &mirrorPath, const QString &branch)
{
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
    if (!mirrorPath.trimmed().isEmpty() && QDir(mirrorPath).exists() &&
        runGitCapture(mirrorPath,
                      {"rev-parse", "--verify", "-q",
                       QStringLiteral("refs/heads/forkmesh/pulls^{commit}")},
                      nullptr, nullptr))
        return mirrorNumberedDirCount(
            mirrorPath, QStringLiteral("forkmesh/pulls"),
            QStringLiteral(".forkmesh/pulls"));
    return mirrorNumberedDirCount(mirrorPath, branch,
                                  QStringLiteral(".forkmesh/pulls"));
}
inline int mirrorDiscussionCount(const QString &mirrorPath, const QString &branch)
{
    return mirrorNumberedDirCount(mirrorPath, branch,
                                  QStringLiteral(".forkmesh/discussions"));
}

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

inline CommitIdentity mirrorCommitIdentity(const QString &mirrorPath,
                                           const QString &workTreePath,
                                           const QString &commit)
{
    CommitIdentity identity = gitCommitIdentity(mirrorPath, commit);
    if (identity.subject.isEmpty() && identity.author.isEmpty())
        identity = gitCommitIdentity(workTreePath, commit);
    return identity;
}

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

inline QString pillTextColor(const QString &backgroundHex)
{
    const QColor c(backgroundHex);
    const double luminance =
        0.299 * c.red() + 0.587 * c.green() + 0.114 * c.blue();
    return luminance > 150 ? QStringLiteral("#1f2328") : QStringLiteral("#ffffff");
}

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
