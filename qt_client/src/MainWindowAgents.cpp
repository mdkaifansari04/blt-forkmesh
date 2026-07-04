// MainWindowAgents: MainWindow feature methods, split out of MainWindow.cpp.
// Agents: the agent sessions table plus external Claude Code sessions.
//
// These are MainWindow member functions defined in their own translation unit;
// the class itself is declared in MainWindow.h. Shared helpers live in
// MainWindowInternal.h / MainWindowShared.cpp (namespace forkmesh::ui).

#include "MainWindow.h"
#include "MainWindowInternal.h"
#include "KebabHeaderView.h"

using namespace forkmesh::ui;

// ---- Agents ---------------------------------------------------------------

QString MainWindow::agentProviderName(const QString &provider) const
{
    // "Claude Code" runs the real `claude` CLI; the two API-key providers use the
    // bundled script / Codex. Legacy "claude" sessions map to Claude API;
    // everything else (incl. legacy "codex") to OpenAI API.
    if (provider == QLatin1String("claude-code"))
        return QStringLiteral("Claude Code");
    if (provider.startsWith(QLatin1String("claude")))
        return QStringLiteral("Claude API");
    return QStringLiteral("OpenAI API");
}

namespace {

// The base branch an agent session landed in, defaulting to "main" when the
// session never recorded one (issue #291).
QString agentMergeBase(const AgentSession &s)
{
    return s.baseBranch.isEmpty() ? QStringLiteral("main") : s.baseBranch;
}

// Fill the agent table's Status cell for a session. A session whose worktree/PR
// has landed in the base branch (issue #291) simply reads "merged" in the merged-
// purple foreground used across the app, with a tooltip spelling out the branch
// and time so the note is visible straight from the list. The Claude run summary
// ("N turns · Ms") that used to be appended here now lives in dedicated Turns/Time
// columns (see applyAgentTurnsCell / applyAgentTimeCell).
void applyAgentStatusCell(QTableWidgetItem *cell, const AgentSession &s)
{
    cell->setText(s.merged ? QStringLiteral("merged") : agentStatusText(s.status));
    cell->setForeground(s.merged ? QColor("#a371f7") : agentStatusColor(s.status));
    // Status glyph next to the text (issue #108): a green spinner while running, a
    // purple merge mark once it lands, a green check on success, a red stop sign
    // when halted, an orange hand while it waits on the user, and a red X circle
    // on failure (issue #322). The running glyph is seeded at frame 0 here;
    // animateRunningAgentIcons() spins it. Other states carry no icon.
    if (s.merged)
        cell->setIcon(themedOcticon("git-merge", QColor("#a371f7"), 14));
    else if (s.status == AgentStatus::Running)
        cell->setIcon(themedOcticon("sync", QColor("#3fb950"), 14));
    else if (s.status == AgentStatus::Success)
        cell->setIcon(themedOcticon("check-circle", QColor("#3fb950"), 14));
    else if (s.status == AgentStatus::Stopped)
        cell->setIcon(themedOcticon("stop", QColor("#f85149"), 14));
    else if (s.status == AgentStatus::Waiting)
        cell->setIcon(themedOcticon("hand", QColor("#e3742f"), 14));
    else if (s.status == AgentStatus::Failed)
        cell->setIcon(themedOcticon("x", QColor("#f85149"), 14));
    else
        cell->setIcon(QIcon());
    cell->setToolTip(
        s.merged
            ? QStringLiteral("Worktree/PR merged into %1%2")
                  .arg(agentMergeBase(s),
                       s.mergedAtMs > 0
                           ? QStringLiteral(" on %1").arg(
                                 QDateTime::fromMSecsSinceEpoch(s.mergedAtMs)
                                     .toString(QStringLiteral("MMM d  hh:mm")))
                           : QString())
            : QString());
}

// Fill the Turns cell — the conversation-turn count the CLI reports on finish.
// Sorts on the raw number via kTableSortRole (so the item must be a
// SortTableWidgetItem), shows "-" until a run summary lands.
void applyAgentTurnsCell(QTableWidgetItem *cell, const AgentSession &s)
{
    cell->setData(Qt::DisplayRole,
                  s.numTurns > 0 ? QString::number(s.numTurns)
                                 : QStringLiteral("-"));
    cell->setData(kTableSortRole, s.numTurns);
    cell->setToolTip(QStringLiteral("Conversation turns this agent ran"));
}

// Effective run duration for the Time/Speed figures. While a session is running
// the live path matters most: measure against the wall clock from the session's
// start so the figure ticks up as the run proceeds. Once finished, prefer the
// CLI-reported active run time (durationMs), falling back to the start→finish span.
qint64 agentEffectiveDurationMs(const AgentSession &s)
{
    if (s.durationMs > 0)
        return s.durationMs;
    if (s.finishedAtMs > s.startedAtMs && s.startedAtMs > 0)
        return s.finishedAtMs - s.startedAtMs;
    if (s.startedAtMs > 0 && s.status == AgentStatus::Running) {
        const qint64 now = QDateTime::currentMSecsSinceEpoch();
        if (now > s.startedAtMs)
            return now - s.startedAtMs;
    }
    return 0;
}

// Fill the Time cell — the agent's wall-clock run time in whole seconds. For a
// running agent this is the live elapsed time since it started (issue #245), kept
// ticking by the agents-tab spin timer, so the figure climbs while it works; a
// finished agent shows the CLI's final run time. Sorts on the raw millisecond value.
void applyAgentTimeCell(QTableWidgetItem *cell, const AgentSession &s)
{
    const qint64 ms = agentEffectiveDurationMs(s);
    cell->setData(Qt::DisplayRole,
                  ms > 0 ? QStringLiteral("%1s").arg(ms / 1000)
                         : QStringLiteral("-"));
    cell->setData(kTableSortRole, static_cast<qlonglong>(ms));
    cell->setToolTip(s.status == AgentStatus::Running
                         ? QStringLiteral("Time elapsed since this agent started")
                         : QStringLiteral("Wall-clock time this agent ran"));
}

// Fill the Model cell — the LLM model selected for this session. Displays a
// human-readable label (e.g. "Opus", "Sonnet") or empty if no specific model
// was chosen, letting the provider's default apply.
void applyAgentModelCell(QTableWidgetItem *cell, const AgentSession &s)
{
    cell->setText(agentModelLabel(s.model));
    // Text only — no icon belongs in this column (a reused item must not keep
    // one a stray write elsewhere left behind).
    cell->setIcon(QIcon());
    cell->setToolTip(s.model.isEmpty() ? QStringLiteral("Using provider's default model")
                                       : QStringLiteral("Selected model: %1")
                                            .arg(s.model.toHtmlEscaped()));
}

// Fill the Speed cell — the throughput at which this agent exchanged tokens with
// the service over the task, in tokens/second (total tokens ÷ run time). A rough
// gauge of how fast the model and network served the task. Only shown while the
// session is running (a live tok/s gauge); finished sessions show "-" since the
// figure is no longer ticking. Sorts on the raw rate via kTableSortRole.
void applyAgentSpeedCell(QTableWidgetItem *cell, const AgentSession &s, qint64 tokens)
{
    const qint64 durationMs = agentEffectiveDurationMs(s);
    const double rate = (s.status == AgentStatus::Running && tokens > 0 && durationMs > 0)
                            ? tokens * 1000.0 / static_cast<double>(durationMs)
                            : 0.0;
    cell->setData(Qt::DisplayRole,
                  rate > 0 ? QStringLiteral("%1 tok/s").arg(rate, 0, 'f', 1)
                           : QStringLiteral("-"));
    cell->setData(kTableSortRole, rate);
    cell->setToolTip(
        QStringLiteral("Communication speed with the service (tokens/second)"));
}

// Fill the Diff cell (issue #170) — a "very small" at-a-glance summary of what
// this agent changed: the number of files its captured patch touched, plus how
// far its branch sits ahead of / behind the base branch (only when both differ).
// Reads "-" until a finished run has a patch and/or a still-existing branch to
// measure. Sorts on the file count via kTableSortRole.
void applyAgentDiffCell(QTableWidgetItem *cell, const AgentDiffStat &stat,
                        const QString &base)
{
    QStringList parts;
    if (stat.files >= 0)
        parts << (stat.files == 1 ? QStringLiteral("1 file")
                                  : QStringLiteral("%1 files").arg(stat.files));
    // Up arrow = ahead, down arrow = behind. Omit when the branch matches base
    // (both zero) so a tidy, merged-in session stays uncluttered.
    if (stat.ahead >= 0 && stat.behind >= 0 && (stat.ahead > 0 || stat.behind > 0))
        parts << QString::fromUtf8("\xE2\x86\x91%1 \xE2\x86\x93%2")
                     .arg(stat.ahead)
                     .arg(stat.behind);
    // Conflict marker (adhoc #229): lead the cell with a warning-sign badge when
    // the branch can no longer merge cleanly into base, so it stands out in the
    // list. The whole Diff cell is tinted red below to reinforce it.
    if (stat.conflicted)
        parts.prepend(QString::fromUtf8("\xE2\x9A\xA0 conflict"));
    cell->setData(Qt::DisplayRole,
                  parts.isEmpty()
                      ? QStringLiteral("-")
                      : parts.join(QString::fromUtf8("  \xC2\xB7 ")));
    cell->setData(kTableSortRole, stat.files);
    // Reset the brush explicitly in the clean case: refreshAgentTable() now reuses
    // row items in place (adhoc #74), so a cell that was red for a conflict must
    // clear back to the default colour once the conflict is gone rather than
    // keeping the stale red tint.
    cell->setForeground(stat.conflicted ? QBrush(QColor(QStringLiteral("#f85149")))
                                        : QBrush());
    QStringList tip;
    if (stat.conflicted)
        tip << QStringLiteral("Conflicts with %1 — merge base in and resolve")
                   .arg(base.isEmpty() ? QStringLiteral("base") : base);
    if (stat.files >= 0)
        tip << QStringLiteral("%1 file%2 changed")
                   .arg(stat.files)
                   .arg(stat.files == 1 ? QString() : QStringLiteral("s"));
    if (stat.ahead >= 0 && stat.behind >= 0)
        tip << QString::fromUtf8("%1 ahead \xC2\xB7 %2 behind %3")
                   .arg(stat.ahead)
                   .arg(stat.behind)
                   .arg(base.isEmpty() ? QStringLiteral("base") : base);
    cell->setToolTip(tip.join(QLatin1Char('\n')));
}

// "Night rider" scanner light shown in the agents list. Each session gets a
// small Larson-scanner bar that sweeps left<->right while its raw output is
// streaming, so the list shows real-time activity at a glance. The sweep is
// gated on recent raw output: after this many ms with no new output the light
// drops back to a dim resting state and the driving timer stops.
static constexpr qint64 kScannerIdleMs = 1500;
// Far-right "Activity" column the scanner is painted into.
static constexpr int kAgentActivityColumn = 12;

// Paints a session's Larson-scanner light from MainWindow's per-session state,
// looked up by the sessionId stored in the cell's Qt::UserRole. Reading from a
// side table keyed by sessionId — rather than per-row child widgets — keeps the
// animation alive across the agents table's frequent full rebuilds.
class AgentScannerDelegate : public QStyledItemDelegate
{
public:
    AgentScannerDelegate(const QHash<int, AgentScannerState> *states, QObject *parent)
        : QStyledItemDelegate(parent), m_states(states)
    {
    }

    QSize sizeHint(const QStyleOptionViewItem &opt, const QModelIndex &idx) const override
    {
        const QSize s = QStyledItemDelegate::sizeHint(opt, idx);
        return QSize(qMax(s.width(), 96), s.height());
    }

    void paint(QPainter *p, const QStyleOptionViewItem &opt,
               const QModelIndex &idx) const override
    {
        // Let the style draw the row background (hover) but no text. The solid
        // green selection fill is suppressed so the row reads as a green outline
        // (drawn below) over a transparent band, matching the rest of the row.
        QStyleOptionViewItem o(opt);
        initStyleOption(&o, idx);
        o.text.clear();
        o.state &= ~QStyle::State_Selected;
        const QWidget *w = o.widget;
        QStyle *style = w ? w->style() : QApplication::style();
        style->drawControl(QStyle::CE_ItemViewItem, &o, p, w);
        paintRowSelectionBorder(p, opt, idx);

        const int sessionId = idx.data(Qt::UserRole).toInt();
        const qint64 now = QDateTime::currentMSecsSinceEpoch();
        double phase = 0.0;
        double intensity = 0.0; // live-output rate, drives the reactive effect
        bool active = false;
        if (m_states) {
            const auto it = m_states->constFind(sessionId);
            if (it != m_states->constEnd()) {
                phase = it->phase;
                intensity = it->intensity;
                // Keep painting the sweep until the meter has fully wound down,
                // so the light stays continuously going between output bursts.
                active = (now - it->lastActivityMs) < kScannerIdleMs
                         || intensity > 0.0;
            }
        }

        const QRect r = opt.rect.adjusted(8, 0, -8, 0);
        if (r.width() <= 0)
            return;
        const int n = qBound(6, r.width() / 7, 16);
        const double gap = double(r.width()) / n;
        const int dotW = qMax(2, int(gap) - 3);
        const int dotH = qBound(3, r.height() - 10, 7);
        const double cy = r.center().y() + 0.5;

        // Triangle wave: 0 -> (n-1) -> 0, the back-and-forth night-rider sweep.
        const double tri = phase < 0.5 ? phase * 2.0 : (1.0 - phase) * 2.0;
        const double pos = tri * (n - 1);
        // Real-time reactive effect: the comet's tail streaks longer the more raw
        // output is pouring in, so the trail length tracks throughput at a glance.
        const double trail = 1.8 + 2.6 * intensity; // LEDs the comet's glow spans

        p->save();
        p->setRenderHint(QPainter::Antialiasing, true);
        p->setPen(Qt::NoPen);
        // Hot output shifts KITT red toward bright amber, so the colour itself
        // climbs with the live stream rate (cool #f85149 -> hot #ffc75c).
        auto mix = [](int a, int b, double t) { return int(a + (b - a) * t); };
        const QColor base(mix(248, 255, intensity), mix(81, 199, intensity),
                          mix(73, 92, intensity));
        const double restAlpha = active ? 0.10 + 0.10 * intensity : 0.06;
        for (int i = 0; i < n; ++i) {
            double glow = 0.0;
            if (active) {
                const double d = qAbs(i - pos);
                glow = qMax(0.0, 1.0 - d / trail);
                glow *= glow;                       // sharpen the comet head
                glow *= 0.6 + 0.4 * intensity;      // brighter head when busy
            }
            QColor c = base;
            c.setAlphaF(restAlpha + (1.0 - restAlpha) * glow);
            const double x = r.left() + i * gap + (gap - dotW) / 2.0;
            p->setBrush(c);
            p->drawRoundedRect(QRectF(x, cy - dotH / 2.0, dotW, dotH), 1.5, 1.5);
        }
        p->restore();
    }

private:
    const QHash<int, AgentScannerState> *m_states;
};

QString replyHeader(QNetworkReply *reply, const char *name)
{
    return QString::fromUtf8(reply->rawHeader(name)).trimmed();
}

double jsonNumber(const QJsonValue &value)
{
    if (value.isDouble())
        return value.toDouble();
    if (value.isString()) {
        bool ok = false;
        const double number = value.toString().toDouble(&ok);
        if (ok)
            return number;
    }
    return 0.0;
}

qint64 jsonCount(const QJsonObject &obj, const QString &key)
{
    return static_cast<qint64>(jsonNumber(obj.value(key)));
}

double costAmount(const QJsonObject &amount, QString *currency, bool *found)
{
    if (!amount.contains("value"))
        return 0.0;
    if (found)
        *found = true;
    if (currency && currency->isEmpty())
        *currency = amount.value("currency").toString();
    return jsonNumber(amount.value("value"));
}

double costResultTotal(const QJsonObject &result, QString *currency, bool *found)
{
    bool foundTopLevelAmount = false;
    const double topLevelTotal =
        costAmount(result.value("amount").toObject(), currency, &foundTopLevelAmount);
    if (foundTopLevelAmount) {
        if (found)
            *found = true;
        return topLevelTotal;
    }

    double total = 0.0;
    const QJsonArray lineItems = result.value("line_items").toArray();
    for (const QJsonValue &lineItemValue : lineItems) {
        const QJsonObject lineItem = lineItemValue.toObject();
        total += costAmount(lineItem.value("amount").toObject(), currency, found);
    }
    return total;
}

QString moneyString(double amount, QString currency)
{
    if (currency.isEmpty())
        currency = QStringLiteral("usd");
    const QString formatted = QString::number(amount, 'f', amount < 1.0 ? 4 : 2);
    if (currency.compare(QStringLiteral("usd"), Qt::CaseInsensitive) == 0)
        return QStringLiteral("$%1 USD").arg(formatted);
    return QStringLiteral("%1 %2").arg(formatted, currency.toUpper());
}

} // namespace

QWidget *MainWindow::buildAgentsTab()
{
    auto *page = new QWidget;

    auto *listPane = new QWidget;
    listPane->setMinimumWidth(380);
    auto *heading = new QLabel("Agent sessions");
    heading->setObjectName("channelTitle");
    auto *headingRow = new QHBoxLayout;
    headingRow->setContentsMargins(0, 0, 0, 0);
    headingRow->setSpacing(8);
    headingRow->addWidget(heading, 1);

    m_agentTable = new QTableWidget(0, 13);
    m_agentTable->setObjectName("issueTable");
    installColumnHeaderMenu(m_agentTable); // 3-dots per-column menu (issue #318)
    // Selected agent rows get a green outline with a transparent fill (rather
    // than the solid green band the other issueTable lists use); the per-column
    // Activity delegate below draws the matching outline slice for its cell.
    m_agentTable->setItemDelegate(new SelectionBorderRowDelegate(m_agentTable));
    // Stripping State_Selected in the delegate stops the delegate from filling
    // the row, but the view still paints the selection band itself from the
    // app-wide #issueTable stylesheet (selection-background-color, plus the
    // ::item:selected background rule) — that's the green bar that survived. Blank
    // both for this table only, so the delegate's green outline is all that shows.
    m_agentTable->setStyleSheet(
        "#issueTable { selection-background-color: transparent; }"
        "#issueTable::item:selected { background: transparent; }");
    // Turns/Time are the run-summary figures the Claude CLI reports on finish;
    // they used to be crammed into the Status text and now get their own columns.
    // "Diff" (issue #170) is a compact files-changed + branch ahead/behind badge.
    m_agentTable->setHorizontalHeaderLabels(
        {"#", "Issue", "Agent", "Model", "Status", "Turns", "Time", "Cost", "Tokens",
         "Speed", "Updated", "Diff", "Activity"});
    m_agentTable->verticalHeader()->setVisible(false);
    m_agentTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_agentTable->setSelectionMode(QAbstractItemView::SingleSelection);
    m_agentTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_agentTable->setShowGrid(false);
    m_agentTable->setWordWrap(false);
    // Don't tail long titles with a "…" ellipsis (issue #69): ad-hoc sessions
    // carry a full-sentence, prompt-derived title that overruns the Issue column,
    // and Qt::ElideRight peppered every row with trailing dots. Clip cleanly at
    // the cell edge instead — the column is user-widenable to read a title in full.
    m_agentTable->setTextElideMode(Qt::ElideNone);
    m_agentTable->setSortingEnabled(true);
    QHeaderView *agentHeader = m_agentTable->horizontalHeader();
    agentHeader->setHighlightSections(false);
    // Let the user drag column headers into a new order (resizing is wired up
    // by makeColumnsResizable() below). The custom-painted Activity delegate is
    // bound to its logical column, so it follows the header wherever it lands.
    agentHeader->setSectionsMovable(true);
    agentHeader->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    // The Issue (title) column is user-expandable: a draggable Interactive column
    // with a generous default width rather than a locked Stretch flex column, so a
    // long issue title can be widened to read in full.
    agentHeader->setSectionResizeMode(1, QHeaderView::Interactive);
    m_agentTable->setColumnWidth(1, 320);
    for (int c = 2; c < kAgentActivityColumn; ++c)
        agentHeader->setSectionResizeMode(c, QHeaderView::ResizeToContents);
    // The night-rider light column is a fixed-width, custom-painted scanner.
    agentHeader->setSectionResizeMode(kAgentActivityColumn, QHeaderView::Fixed);
    m_agentTable->setColumnWidth(kAgentActivityColumn, 104);
    m_agentTable->setItemDelegateForColumn(
        kAgentActivityColumn, new AgentScannerDelegate(&m_scannerStates, m_agentTable));
    // ~22fps timer that advances + repaints the active scanner lights. It is
    // started on demand by noteAgentActivity and self-stops once all lights idle.
    m_scannerTimer = new QTimer(this);
    m_scannerTimer->setInterval(45);
    connect(m_scannerTimer, &QTimer::timeout, this, &MainWindow::onScannerTick);
    makeColumnsResizable(m_agentTable);

    // Detect Claude Code sessions running outside ForkMesh and stream the open
    // one. Light enough (a directory scan + small tail reads) to poll often.
    m_externalClaudeTimer = new QTimer(this);
    m_externalClaudeTimer->setInterval(3 * 1000);
    connect(m_externalClaudeTimer, &QTimer::timeout, this,
            &MainWindow::onExternalClaudeTick);
    m_externalClaudeTimer->start();

    auto *listLayout = new QVBoxLayout(listPane);
    listLayout->setContentsMargins(18, 18, 12, 18);
    listLayout->setSpacing(8);
    listLayout->addLayout(headingRow);

    // The top-of-list "Start agent" compose row (adhoc #234) was removed from
    // the desktop app (adhoc #271) — starting a new ad-hoc agent now happens
    // from the footer quick-add bar in-app and from the website's Agents view.
    // The m_agentCompose* members stay nullptr; every reader is null-guarded.

    // Free-text filter over the session list (issue #82): type to narrow the
    // table to sessions whose issue number/title, agent, status or PR match.
    m_agentSearch = new QLineEdit;
    m_agentSearch->setObjectName("issueSearch");
    m_agentSearch->setPlaceholderText(
        QString::fromUtf8("Search agents by issue, agent, status or PR\xE2\x80\xA6"));
    m_agentSearch->setClearButtonEnabled(true);
    connect(m_agentSearch, &QLineEdit::textChanged, this,
            [this] { refreshAgentTable(); });

    // "Delete all merged" sits on top of the list and wipes every merged session's
    // worktree, branch and agent in one batch (adhoc #235). It shares the search
    // row to keep the toolbar compact and stays disabled until something is merged.
    m_agentDeleteMergedButton = new QPushButton("Delete all merged");
    m_agentDeleteMergedButton->setObjectName("dangerButton");
    m_agentDeleteMergedButton->setCursor(Qt::PointingHandCursor);
    m_agentDeleteMergedButton->setToolTip(
        "Delete the worktree, branch and session of every merged agent");
    setOcticon(m_agentDeleteMergedButton, "trash", 16);
    connect(m_agentDeleteMergedButton, &QPushButton::clicked, this,
            &MainWindow::deleteAllMergedAgentSessions);

    // "Hide detail" toggle (issue #54): collapse the detail panel so the session
    // list spans the full tab width. Re-checking restores it for the open row.
    // A small icon button next to "Delete all merged" (adhoc #118) rather than a
    // labeled button up in the heading, to keep the toolbar compact.
    m_agentHideDetailButton = new QPushButton;
    m_agentHideDetailButton->setObjectName("issueIconButton");
    m_agentHideDetailButton->setFixedSize(30, 30);
    m_agentHideDetailButton->setCursor(Qt::PointingHandCursor);
    m_agentHideDetailButton->setCheckable(true);
    m_agentHideDetailButton->setToolTip(
        "Hide the detail panel and show the session list full width");
    setOcticon(m_agentHideDetailButton, "chevron-right", 16);
    connect(m_agentHideDetailButton, &QPushButton::toggled, this, [this](bool hidden) {
        m_agentDetailHidden = hidden;
        m_agentHideDetailButton->setToolTip(
            hidden ? "Show the detail panel"
                   : "Hide the detail panel and show the session list full width");
        setOcticon(m_agentHideDetailButton, hidden ? "arrow-left" : "chevron-right", 16);
        if (hidden) {
            if (m_agentDetail)
                m_agentDetail->hide();
        } else if (m_agentDetail && findAgentSession(m_selectedAgentSessionId)) {
            m_agentDetail->show(); // reopen for the still-selected row
        }
    });

    auto *agentListToolbar = new QHBoxLayout;
    agentListToolbar->setContentsMargins(0, 0, 0, 0);
    agentListToolbar->setSpacing(8);
    agentListToolbar->addWidget(m_agentSearch, 1);
    agentListToolbar->addWidget(m_agentDeleteMergedButton, 0);
    agentListToolbar->addWidget(m_agentHideDetailButton, 0);
    listLayout->addLayout(agentListToolbar);

    listLayout->addWidget(m_agentTable, 1);

    auto *detailPane = new QWidget;
    m_agentTitle = new QLabel("Select a session");
    m_agentTitle->setObjectName("channelTitle");
    m_agentTitle->setWordWrap(true);
    m_agentMeta = new QLabel;
    m_agentMeta->setObjectName("statusLine");
    // Selectable text plus clickable links: the branch name links to its row in
    // the Worktrees tab (issue #265). The meta string is HTML-escaped and built as
    // rich text, so pin the format rather than relying on auto-detection.
    m_agentMeta->setTextFormat(Qt::RichText);
    m_agentMeta->setTextInteractionFlags(Qt::TextSelectableByMouse |
                                         Qt::LinksAccessibleByMouse);
    m_agentMeta->setWordWrap(true);
    connect(m_agentMeta, &QLabel::linkActivated, this, [this](const QString &href) {
        if (href.startsWith(kBranchLinkScheme))
            switchToBranch(QUrl::fromPercentEncoding(
                href.mid(kBranchLinkScheme.size()).toUtf8()));
        else if (href.startsWith(kWorktreeLinkScheme))
            switchToWorktree(QUrl::fromPercentEncoding(
                href.mid(kWorktreeLinkScheme.size()).toUtf8()));
        else if (href.startsWith(kPullLinkScheme))
            switchToPullTab(href.mid(kPullLinkScheme.size()).toInt());
        else if (href.startsWith(kIssueLinkScheme)) {
            // Open the issue in its own repo's Issues tab (the session may belong to
            // a repo other than the one currently shown), reusing the notification
            // navigation that handles the section + repo switch (adhoc #138).
            if (const AgentSession *s = findAgentSession(m_selectedAgentSessionId)) {
                NotificationLink link;
                link.kind = QStringLiteral("issue");
                link.owner = s->owner;
                link.name = s->name;
                link.number = href.mid(kIssueLinkScheme.size()).toInt();
                openNotificationLink(link);
            }
        }
    });
    m_agentStopButton = new QPushButton("Stop");
    m_agentStopButton->setObjectName("dangerButton");
    m_agentStopButton->setCursor(Qt::PointingHandCursor);
    setOcticon(m_agentStopButton, "circle-slash", 16);
    connect(m_agentStopButton, &QPushButton::clicked, this, [this] {
        // External (watch-only) rows have no runner/stream — stop the CLI process
        // ForkMesh detected running outside it instead.
        if (isExternalSession(m_selectedAgentSessionId)) {
            stopExternalSession(m_selectedAgentSessionId);
            return;
        }
        if (AgentRunner *runner = runnerForSession(m_selectedAgentSessionId))
            runner->stop();
        stopStreamSession(m_selectedAgentSessionId);
    });

    // Small icon-only button (adhoc #139): lives in the footer's "Agents:"
    // status strip (see buildNetworkLogDock in MainWindowChat.cpp).
    m_agentFixConflictsButton = new QPushButton;
    m_agentFixConflictsButton->setObjectName("agentStatusFixButton");
    m_agentFixConflictsButton->setFlat(true);
    m_agentFixConflictsButton->setFixedSize(22, 22);
    m_agentFixConflictsButton->setIconSize(QSize(14, 14));
    m_agentFixConflictsButton->setCursor(Qt::PointingHandCursor);
    m_agentFixConflictsButton->setToolTip(
        "Fix conflicts with agent \xE2\x80\x94 ask it to merge the base branch "
        "into this branch and resolve conflicts");
    setOcticon(m_agentFixConflictsButton, "git-merge", 14);
    m_agentFixConflictsButton->hide();
    connect(m_agentFixConflictsButton, &QPushButton::clicked, this, [this] {
        fixAgentConflictsWithAgent(m_selectedAgentSessionId);
    });

    m_agentDeleteButton = new QPushButton("Delete");
    m_agentDeleteButton->setObjectName("dangerButton");
    m_agentDeleteButton->setCursor(Qt::PointingHandCursor);
    m_agentDeleteButton->setToolTip("Delete just this agent session");
    setOcticon(m_agentDeleteButton, "trash", 16);
    connect(m_agentDeleteButton, &QPushButton::clicked, this,
            &MainWindow::deleteSelectedAgentSession);

    // Delete the agent together with its worktree folder and branch in one action.
    m_agentDeleteAllButton = new QPushButton("Delete all");
    m_agentDeleteAllButton->setObjectName("dangerButton");
    m_agentDeleteAllButton->setCursor(Qt::PointingHandCursor);
    m_agentDeleteAllButton->setToolTip(
        "Delete this agent session, its worktree folder and its branch");
    setOcticon(m_agentDeleteAllButton, "trash", 16);
    connect(m_agentDeleteAllButton, &QPushButton::clicked, this, [this] {
        AgentSession *s = findAgentSession(m_selectedAgentSessionId);
        if (!s || s->branchName.isEmpty())
            return;
        const int repoIndex = repoIndexFor(s->owner, s->name);
        if (repoIndex < 0)
            return;
        const QString branch = s->branchName;
        const QString wt =
            worktreePathForBranch(m_repositories.at(repoIndex).localPath, branch);
        deleteWorktreeBranchAndAgent(wt, branch, /*confirm=*/false);
    });

    // "View PR" — appears once the session produced a pull request.
    m_agentViewPrButton = new QPushButton("View PR");
    m_agentViewPrButton->setObjectName("primaryButton");
    m_agentViewPrButton->setCursor(Qt::PointingHandCursor);
    setOcticon(m_agentViewPrButton, "git-pull-request", 16);
    m_agentViewPrButton->hide();
    connect(m_agentViewPrButton, &QPushButton::clicked, this, [this] {
        AgentSession *s = findAgentSession(m_selectedAgentSessionId);
        if (s && s->prNumber > 0)
            switchToPullTab(s->prNumber);
    });

    // "Create linked issue" — for ad-hoc sessions (no issue) it files a tracked
    // issue from the run's prompt and links the two (adhoc #189). Hidden once the
    // session already has a linked issue.
    m_agentCreateIssueButton = new QPushButton("Create linked issue");
    m_agentCreateIssueButton->setObjectName("primaryButton");
    m_agentCreateIssueButton->setCursor(Qt::PointingHandCursor);
    m_agentCreateIssueButton->setToolTip(
        "Create a tracked issue from this run and link it to this session");
    setOcticon(m_agentCreateIssueButton, "issue-opened", 16);
    m_agentCreateIssueButton->hide();
    connect(m_agentCreateIssueButton, &QPushButton::clicked, this,
            &MainWindow::createLinkedIssueForSelectedSession);

    // Connected/working status pill next to the title.
    m_agentStatusPill = new QLabel;
    m_agentStatusPill->setObjectName("agentStatusPill");
    m_agentStatusPill->setTextFormat(Qt::RichText);
    m_agentStatusPill->setAlignment(Qt::AlignCenter);

    auto *titleCol = new QVBoxLayout;
    titleCol->setContentsMargins(0, 0, 0, 0);
    titleCol->setSpacing(4);
    titleCol->addWidget(m_agentTitle);
    titleCol->addWidget(m_agentStatusPill, 0, Qt::AlignLeft);

    auto *topRow = new QHBoxLayout;
    topRow->setContentsMargins(0, 0, 0, 0);
    topRow->addLayout(titleCol, 1);
    topRow->addWidget(m_agentCreateIssueButton, 0, Qt::AlignTop);
    topRow->addWidget(m_agentViewPrButton, 0, Qt::AlignTop);
    topRow->addWidget(m_agentStopButton, 0, Qt::AlignTop);
    topRow->addWidget(m_agentDeleteButton, 0, Qt::AlignTop);
    topRow->addWidget(m_agentDeleteAllButton, 0, Qt::AlignTop);

    m_agentLog = new QPlainTextEdit;
    m_agentLog->setReadOnly(true);
    m_agentLog->setObjectName("actionLog");
    applyLogFont(m_agentLog);
    new AgentLogHighlighter(m_agentLog->document());
    m_agentLog->setMaximumBlockCount(30000);
    // Same floating ▲/▼ jump corner the transcript has, so the raw log scrolls
    // to either end with one click.
    auto *rawJump = new ScrollJumpButtons(m_agentLog);
    connect(rawJump, &ScrollJumpButtons::topClicked, this, [this] {
        if (m_agentLog)
            m_agentLog->verticalScrollBar()->setValue(0);
    });
    connect(rawJump, &ScrollJumpButtons::bottomClicked, this, [this] {
        if (m_agentLog)
            m_agentLog->verticalScrollBar()->setValue(
                m_agentLog->verticalScrollBar()->maximum());
    });

    // Claude Code runs in a real embedded terminal; API-key agents keep the piped
    // log. Stack the two so the detail shows whichever fits the running session.
    m_agentTerminal = new TerminalWidget;
    connect(m_agentTerminal, &TerminalWidget::finished, this, [this](int) {
        if (m_terminalSessionId <= 0)
            return;
        if (AgentSession *s = findAgentSession(m_terminalSessionId)) {
            if (s->status == AgentStatus::Running) {
                s->status = AgentStatus::Success;
                s->finishedAtMs = QDateTime::currentMSecsSinceEpoch();
                m_agentStore->saveSession(*s);
                scheduleAgentSessionsPush(); // adhoc #182
                reloadAgents();
            }
        }
    });
    // Extension-style transcript for Claude Code (issue #191 follow-up): renders
    // the CLI's stream-json events as native cards.
    m_agentTranscript = new ClaudeTranscriptView;
    m_agentTranscript->setSplitDiffs(
        QSettings().value(kClaudeDiffSplitSetting, false).toBool());
    m_agentTranscript->setMinimumHeight(320); // never collapse to a thin strip
    m_agentTranscript->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    connect(m_agentTranscript, &ClaudeTranscriptView::usageChanged, this,
            [this](const QString &kind, const QString &text, int percent) {
                Q_UNUSED(text);
                applyClaudeUsage(kind != QLatin1String("5h"), percent);
            });
    // "Load earlier events" (button click or scroll-near-top) — don't truncate
    // the transcript (adhoc #115): the tail-capped initial render keeps opening
    // a long session fast, but the full history is still reachable a batch at a
    // time instead of being stuck behind "the Raw view has it".
    connect(m_agentTranscript, &ClaudeTranscriptView::loadEarlierRequested, this,
            &MainWindow::loadEarlierTranscriptEvents);
    // The user answered an AskUserQuestion multiple-choice card in the transcript
    // (issue #67). Satisfy the pending tool call so the CLI resumes, record the
    // answer in the session buffer (persists + replays the answered card), and
    // flip the session back to Running.
    connect(m_agentTranscript, &ClaudeTranscriptView::questionAnswered, this,
            [this](const QString &toolUseId, const QString &answer) {
                const int sid = m_selectedAgentSessionId;
                if (sid < 0)
                    return;
                ClaudeStreamSession *s = m_streamSessions.value(sid);
                if (!s || !s->running())
                    return;
                applyTranscriptEvent(
                    sid,
                    QJsonObject{
                        {QStringLiteral("type"), QStringLiteral("_local_ask_answer")},
                        {QStringLiteral("tool_use_id"), toolUseId},
                        {QStringLiteral("text"), answer}});
                s->sendToolResult(toolUseId, answer);
                bumpClaudeCodeUsage();
                if (AgentSession *as = findAgentSession(sid);
                    as && as->status != AgentStatus::Running) {
                    as->status = AgentStatus::Running;
                    as->finishedAtMs = 0;
                    as->lastError.clear();
                    if (m_agentStore)
                        m_agentStore->saveSession(*as);
                    updateAgentStatusCell(sid);
                }
            });
    // The user clicked an option on a heuristically-detected inline clarifying
    // question (plain prose, not the AskUserQuestion tool — issue #212). There's
    // no tool_use_id to satisfy here, so just send it like any other typed
    // follow-up; that already records the turn, resumes the CLI, and clears
    // "Waiting" back to Running.
    connect(m_agentTranscript, &ClaudeTranscriptView::inlineChoiceAnswered, this,
            [this](const QString &answer) { sendPromptToSelectedAgent(answer); });
    // Issue #84: the live token/cost counter (statsChanged) is now folded into
    // the top-bar chart's hover tooltip via setAgentUsageLabel(), which carries
    // the same totals plus the budget breakdown, so there's no separate label.

    m_agentOutputStack = new QStackedWidget;
    m_agentOutputStack->addWidget(m_agentLog);        // page 0: raw / piped log
    m_agentOutputStack->addWidget(m_agentTerminal);   // page 1: embedded terminal
    m_agentOutputStack->addWidget(m_agentTranscript); // page 2: rich transcript

    // Transcript | Raw toggle, shown only for Claude Code transcript sessions.
    m_transcriptModeButton = new QPushButton(QStringLiteral("Transcript"));
    m_terminalModeButton = new QPushButton(QStringLiteral("Raw output"));
    for (QPushButton *b : {m_transcriptModeButton, m_terminalModeButton}) {
        b->setCheckable(true);
        b->setCursor(Qt::PointingHandCursor);
        b->setObjectName("segButton");
    }
    m_transcriptModeButton->setChecked(true);
    connect(m_transcriptModeButton, &QPushButton::clicked, this, [this] {
        m_transcriptModeButton->setChecked(true);
        m_terminalModeButton->setChecked(false);
        if (m_agentOutputStack)
            m_agentOutputStack->setCurrentWidget(m_agentTranscript);
    });
    connect(m_terminalModeButton, &QPushButton::clicked, this, [this] {
        m_terminalModeButton->setChecked(true);
        m_transcriptModeButton->setChecked(false);
        showAgentRawOutput();
    });
    // Diff-style selector, sitting at the top of the output area (next to the
    // Transcript|Raw toggle): pick unified or side-by-side diffs.
    m_agentDiffModeCombo = new QComboBox;
    m_agentDiffModeCombo->setObjectName("agentDiffMode");
    m_agentDiffModeCombo->setCursor(Qt::PointingHandCursor);
    m_agentDiffModeCombo->addItem(QStringLiteral("Unified diff"), false);
    m_agentDiffModeCombo->addItem(QStringLiteral("Split diff"), true);
    m_agentDiffModeCombo->setToolTip(
        "How Edit/Write diffs are shown in the transcript");
    m_agentDiffModeCombo->setCurrentIndex(
        QSettings().value(kClaudeDiffSplitSetting, false).toBool() ? 1 : 0);
    connect(m_agentDiffModeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int) {
                const bool split = m_agentDiffModeCombo->currentData().toBool();
                QSettings().setValue(kClaudeDiffSplitSetting, split);
                if (m_agentTranscript)
                    m_agentTranscript->setSplitDiffs(split);
                if (m_selectedAgentSessionId != -1) // re-render with the new style
                    showAgentSession(m_selectedAgentSessionId);
            });

    // Search the transcript (adhoc #201): a query box with a "3/12" match counter
    // and prev/next steppers. Typing highlights every match in the transcript and
    // jumps to the first; Enter / the steppers walk through the hits.
    m_transcriptSearch = new QLineEdit;
    m_transcriptSearch->setObjectName("issueSearch"); // reuse the styled search look
    m_transcriptSearch->setPlaceholderText(
        QString::fromUtf8("Search transcript\xE2\x80\xA6"));
    m_transcriptSearch->setClearButtonEnabled(true);
    m_transcriptSearch->setFixedWidth(190);
    m_transcriptSearchCount = new QLabel;
    m_transcriptSearchCount->setObjectName("agentFilesHeading"); // small muted text
    m_transcriptSearchPrev = new QPushButton;
    m_transcriptSearchNext = new QPushButton;
    for (QPushButton *b : {m_transcriptSearchPrev, m_transcriptSearchNext}) {
        b->setObjectName("ghostButton");
        b->setProperty("buttonSize", "sm");
        b->setCursor(Qt::PointingHandCursor);
        b->setEnabled(false);
    }
    setOcticon(m_transcriptSearchPrev, "chevron-up", 14);
    setOcticon(m_transcriptSearchNext, "chevron-down", 14);
    m_transcriptSearchPrev->setToolTip(QStringLiteral("Previous match"));
    m_transcriptSearchNext->setToolTip(QStringLiteral("Next match"));
    connect(m_transcriptSearch, &QLineEdit::textChanged, this,
            [this](const QString &t) {
                if (!m_agentTranscript)
                    return;
                const QString q = t.trimmed();
                if (q.isEmpty())
                    m_agentTranscript->clearSearch();
                else
                    m_agentTranscript->search(q);
            });
    connect(m_transcriptSearch, &QLineEdit::returnPressed, this, [this] {
        if (m_agentTranscript)
            m_agentTranscript->searchNext();
    });
    connect(m_transcriptSearchPrev, &QPushButton::clicked, this, [this] {
        if (m_agentTranscript)
            m_agentTranscript->searchPrev();
    });
    connect(m_transcriptSearchNext, &QPushButton::clicked, this, [this] {
        if (m_agentTranscript)
            m_agentTranscript->searchNext();
    });
    connect(m_agentTranscript, &ClaudeTranscriptView::searchResultsChanged, this,
            [this](int current, int total) {
                if (m_transcriptSearchCount) {
                    const bool empty = !m_transcriptSearch
                                       || m_transcriptSearch->text().trimmed().isEmpty();
                    m_transcriptSearchCount->setText(
                        empty ? QString()
                              : QStringLiteral("%1/%2").arg(current).arg(total));
                }
                const bool any = total > 0;
                if (m_transcriptSearchPrev)
                    m_transcriptSearchPrev->setEnabled(any);
                if (m_transcriptSearchNext)
                    m_transcriptSearchNext->setEnabled(any);
            });

    auto *toggleRow = new QHBoxLayout;
    toggleRow->setContentsMargins(0, 0, 0, 0);
    toggleRow->setSpacing(0);
    toggleRow->addWidget(m_transcriptModeButton);
    toggleRow->addWidget(m_terminalModeButton);
    toggleRow->addStretch(1);
    toggleRow->addWidget(m_transcriptSearch);
    toggleRow->addSpacing(6);
    toggleRow->addWidget(m_transcriptSearchCount);
    toggleRow->addWidget(m_transcriptSearchPrev);
    toggleRow->addWidget(m_transcriptSearchNext);
    toggleRow->addSpacing(8);
    toggleRow->addWidget(m_agentDiffModeCombo);
    m_agentOutputToggle = new QWidget;
    m_agentOutputToggle->setLayout(toggleRow);
    m_agentOutputToggle->hide();

    // Edited-files list for the "Files changed" tab: the files this session has
    // touched in its branch (derived from Edit/Write/MultiEdit tool calls, and
    // refreshed from `git diff` hourly). Selecting a file scrolls the diff viewer
    // to it; activating (double-click/Enter) opens the file in the OS.
    m_agentFilesList = new QListWidget;
    m_agentFilesList->setObjectName("agentFilesList");
    m_agentFilesList->setMinimumWidth(190);
    // Selected file: green outline (no solid fill) like the agents list, so it
    // stays legible instead of vanishing into a default white highlight (#188).
    m_agentFilesList->setItemDelegate(
        new SelectionBorderRowDelegate(m_agentFilesList));
    connect(m_agentFilesList, &QListWidget::itemActivated, this,
            [](QListWidgetItem *it) {
                const QString path = it->data(Qt::UserRole).toString();
                if (!path.isEmpty())
                    QDesktopServices::openUrl(QUrl::fromLocalFile(path));
            });
    // Click-to-scroll and scroll-to-select are driven by DiffFileNavigator below.

    // The commits this branch adds on top of its base, newest first (adhoc #260).
    // A short, read-only list above the file list so the "5 commits" in the summary
    // is browsable rather than just a count. Kept compact so the file list still
    // owns most of the panel.
    m_agentCommitsHeading = new QLabel;
    m_agentCommitsHeading->setObjectName("agentFilesHeading");
    m_agentCommitsList = new QListWidget;
    m_agentCommitsList->setObjectName("agentCommitsList");
    m_agentCommitsList->setMinimumWidth(190);
    m_agentCommitsList->setMaximumHeight(120);
    m_agentCommitsList->setSelectionMode(QAbstractItemView::NoSelection);
    m_agentCommitsList->setFocusPolicy(Qt::NoFocus);
    m_agentCommitsHeading->setVisible(false); // shown once a render finds commits
    m_agentCommitsList->setVisible(false);

    auto *filesV = new QVBoxLayout;
    filesV->setContentsMargins(0, 0, 0, 0);
    filesV->setSpacing(4);
    m_agentFilesChangedSummary = new QLabel;
    m_agentFilesChangedSummary->setObjectName("agentFilesHeading");
    filesV->addWidget(m_agentFilesChangedSummary);
    filesV->addWidget(m_agentCommitsHeading);
    filesV->addWidget(m_agentCommitsList);
    filesV->addWidget(m_agentFilesList, 1);
    m_agentFilesPanel = new QWidget;
    m_agentFilesPanel->setLayout(filesV);

    // Diff viewer beside the file list (same pattern as the Worktrees tab).
    m_agentDiffView = new QTextBrowser;
    m_agentDiffView->setObjectName("diffView");
    m_agentDiffView->setOpenExternalLinks(false);
    m_agentDiffView->setLineWrapMode(QTextEdit::NoWrap);
    registerDiffView(m_agentDiffView);
    // The DiffFileNavigator (sticky header + scroll<->select wiring) is created
    // lazily on first render, where its complete type is in scope.

    // Per-session worktree actions, mirroring the Worktrees tab's detail bar but
    // acting on this session's branch (issue #131: "add the worktree functions
    // there too"). Enabled only for a real feature-branch worktree on disk.
    m_agentUpdateButton = new QPushButton("Update from main");
    m_agentUpdateButton->setObjectName("ghostButton");
    m_agentUpdateButton->setProperty("buttonSize", "sm");
    m_agentUpdateButton->setCursor(Qt::PointingHandCursor);
    setOcticon(m_agentUpdateButton, "sync", 14);
    m_agentUpdateButton->setToolTip(
        "Merge the default branch into this session's worktree branch");
    connect(m_agentUpdateButton, &QPushButton::clicked, this, [this] {
        AgentSession *s = findAgentSession(m_selectedAgentSessionId);
        if (!s || s->branchName.isEmpty())
            return;
        const int ri = repoIndexFor(s->owner, s->name);
        if (ri < 0)
            return;
        // Resolve the base from the session itself (not the open repo detail) so the
        // merge targets this session's base branch even when another repo's detail
        // is on screen (adhoc #28).
        updateWorktreeFromMain(
            worktreePathForBranch(m_repositories.at(ri).localPath, s->branchName),
            s->branchName, agentMergeBase(*s));
        // The merge changed the branch's diff vs main — redraw the Files-changed tab.
        refreshAgentFilesPanel(m_selectedAgentSessionId);
    });
    m_agentMergeButton = new QPushButton("Merge into main");
    m_agentMergeButton->setObjectName("primaryButton");
    m_agentMergeButton->setProperty("buttonSize", "sm");
    m_agentMergeButton->setCursor(Qt::PointingHandCursor);
    setOcticon(m_agentMergeButton, "check-circle", 14);
    m_agentMergeButton->setToolTip(
        "Merge this session's branch into the default branch, then delete the "
        "worktree and its branch");
    connect(m_agentMergeButton, &QPushButton::clicked, this, [this] {
        AgentSession *s = findAgentSession(m_selectedAgentSessionId);
        if (!s || s->branchName.isEmpty())
            return;
        const int ri = repoIndexFor(s->owner, s->name);
        if (ri < 0)
            return;
        mergeWorktreeIntoMain(
            s->branchName,
            worktreePathForBranch(m_repositories.at(ri).localPath, s->branchName));
    });
    // Same merge, but also tear down this agent session once its branch is in main
    // (mirrors the Worktrees tab's "Merge & delete agent").
    m_agentMergeDeleteButton = new QPushButton("Merge & delete agent");
    // Green/primary like "Merge into main" — both land the branch in main, so they
    // read as the affirmative actions on this bar (adhoc #254).
    m_agentMergeDeleteButton->setObjectName("primaryButton");
    m_agentMergeDeleteButton->setProperty("buttonSize", "sm");
    m_agentMergeDeleteButton->setCursor(Qt::PointingHandCursor);
    setOcticon(m_agentMergeDeleteButton, "check-circle", 14);
    m_agentMergeDeleteButton->setToolTip(
        "Merge this session's branch into the default branch, then delete the "
        "worktree, its branch and its agent session");
    connect(m_agentMergeDeleteButton, &QPushButton::clicked, this, [this] {
        AgentSession *s = findAgentSession(m_selectedAgentSessionId);
        if (!s || s->branchName.isEmpty())
            return;
        const int ri = repoIndexFor(s->owner, s->name);
        if (ri < 0)
            return;
        mergeWorktreeIntoMain(
            s->branchName,
            worktreePathForBranch(m_repositories.at(ri).localPath, s->branchName),
            /*deleteAgent=*/true);
    });
    m_agentWtDeleteButton = new QPushButton("Delete worktree");
    m_agentWtDeleteButton->setObjectName("ghostButton");
    m_agentWtDeleteButton->setProperty("buttonSize", "sm");
    m_agentWtDeleteButton->setCursor(Qt::PointingHandCursor);
    setOcticon(m_agentWtDeleteButton, "trash", 14);
    m_agentWtDeleteButton->setToolTip(
        "Remove this session's worktree, delete its branch and its agent session");
    connect(m_agentWtDeleteButton, &QPushButton::clicked, this, [this] {
        AgentSession *s = findAgentSession(m_selectedAgentSessionId);
        if (!s || s->branchName.isEmpty())
            return;
        const int ri = repoIndexFor(s->owner, s->name);
        if (ri < 0)
            return;
        deleteWorktreeBranchAndAgent(
            worktreePathForBranch(m_repositories.at(ri).localPath, s->branchName),
            s->branchName);
    });

    auto *filesActionBar = new QHBoxLayout;
    filesActionBar->setContentsMargins(0, 0, 0, 0);
    filesActionBar->addStretch();
    filesActionBar->addWidget(m_agentUpdateButton);
    filesActionBar->addWidget(m_agentMergeButton);
    filesActionBar->addWidget(m_agentMergeDeleteButton);
    filesActionBar->addWidget(m_agentWtDeleteButton);

    auto *filesDiffSplit = new QSplitter(Qt::Horizontal);
    filesDiffSplit->setChildrenCollapsible(false);
    filesDiffSplit->addWidget(m_agentFilesPanel);
    filesDiffSplit->addWidget(m_agentDiffView);
    filesDiffSplit->setStretchFactor(0, 0);
    filesDiffSplit->setStretchFactor(1, 1);
    filesDiffSplit->setSizes({220, 700});

    auto *filesChangedPage = new QWidget;
    auto *filesChangedLayout = new QVBoxLayout(filesChangedPage);
    filesChangedLayout->setContentsMargins(0, 8, 0, 0);
    filesChangedLayout->setSpacing(6);
    filesChangedLayout->addLayout(filesActionBar);
    filesChangedLayout->addWidget(filesDiffSplit, 1);

    // Agent tab: the Transcript|Raw toggle over the output stack.
    auto *agentOutputPage = new QWidget;
    auto *agentOutputLayout = new QVBoxLayout(agentOutputPage);
    agentOutputLayout->setContentsMargins(0, 8, 0, 0);
    agentOutputLayout->setSpacing(6);
    agentOutputLayout->addWidget(m_agentOutputToggle);
    agentOutputLayout->addWidget(m_agentOutputStack, 1);

    // The new "row with two tabs" (issue #131): Agent | Files changed.
    m_agentDetailTabs = new QTabWidget;
    m_agentDetailTabs->setObjectName("agentDetailTabs");
    m_agentDetailTabs->addTab(agentOutputPage, QStringLiteral("Agent"));
    m_agentFilesTabIndex =
        m_agentDetailTabs->addTab(filesChangedPage, QStringLiteral("Files changed"));
    // Opening the Files-changed tab recomputes the diff straight from git, so the
    // panel always reflects the branch's current state. It used to refresh only on
    // transcript events, so after a quiet spell (or once a run finished) it went
    // stale and the user had to click the branch link to see the real changes
    // (adhoc #20).
    connect(m_agentDetailTabs, &QTabWidget::currentChanged, this, [this](int index) {
        if (index == m_agentFilesTabIndex && m_selectedAgentSessionId > 0)
            refreshAgentFilesPanel(m_selectedAgentSessionId);
    });

    auto *outputContainer = m_agentDetailTabs;
    outputContainer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    // Issue #84: the 5-hour/weekly usage gauges and the live token/cost counter
    // no longer live here — they were moved into the top-bar mini chart and its
    // hover tooltip so the detail page stays focused on the transcript. The OAuth
    // poll (issue #290) feeds that chart directly via applyClaudeUsage().

    // Refresh spend + the edited-files list once an hour while the app runs.
    m_agentHourlyTimer = new QTimer(this);
    m_agentHourlyTimer->setInterval(60 * 60 * 1000);
    connect(m_agentHourlyTimer, &QTimer::timeout, this, [this] {
        refreshClaudeSpend();
        if (m_selectedAgentSessionId > 0)
            refreshAgentFilesPanel(m_selectedAgentSessionId);
    });
    m_agentHourlyTimer->start();

    // adhoc #182: push a snapshot of this node's agent sessions to the website
    // (repo owner can watch them there) and drain any steering prompts queued
    // from the browser. Same cadence family as the mirror sync / inbox poll
    // timers elsewhere in this window — a first pass shortly after launch, then
    // on a short interval so the website stays close to live.
    m_agentSyncPushTimer = new QTimer(this);
    connect(m_agentSyncPushTimer, &QTimer::timeout, this,
            &MainWindow::pushAgentSessionsSnapshot);
    m_agentSyncPushTimer->start(30 * 1000);
    QTimer::singleShot(10 * 1000, this, &MainWindow::pushAgentSessionsSnapshot);

    m_agentPromptDrainTimer = new QTimer(this);
    connect(m_agentPromptDrainTimer, &QTimer::timeout, this,
            &MainWindow::drainAgentPrompts);
    m_agentPromptDrainTimer->start(30 * 1000);
    QTimer::singleShot(15 * 1000, this, &MainWindow::drainAgentPrompts);

    // Issue #290 used to re-pull the OAuth usage endpoint on a steady one-minute
    // timer (plus a burst of polls on launch) so the top-bar gauge stayed current
    // even with no agent running. That meant a network round trip every minute
    // for the lifetime of the app. Polling is gone: the gauge now renders from
    // the last cached figures on launch (restored in buildBreadcrumb) and only
    // hits the network when there's a reason to believe usage moved — right
    // after a prompt is sent (bumpClaudeCodeUsage) or when the user hovers the
    // chart to check the current numbers (see the TokenUsageMiniChart::onHover
    // wiring in buildBreadcrumb).

    // adhoc #178 removed the composer frame that used to hold just the
    // Continue button (adhoc #139 had already stripped it down to that) — the
    // footer prompt bar's "send to agent" control already covers resuming
    // whichever session is open (see buildNetworkLogDock in MainWindowChat.cpp).

    m_agentNetPanel = new QLabel;
    m_agentNetPanel->setObjectName("agentNetPanel");
    m_agentNetPanel->setTextFormat(Qt::RichText);
    m_agentNetPanel->setWordWrap(true);
    m_agentNetPanel->setTextInteractionFlags(Qt::TextSelectableByMouse);

    auto *detailLayout = new QVBoxLayout(detailPane);
    detailLayout->setContentsMargins(12, 18, 22, 18);
    detailLayout->setSpacing(8);
    detailLayout->addLayout(topRow);
    detailLayout->addWidget(m_agentMeta);
    detailLayout->addWidget(m_agentNetPanel);
    detailLayout->addWidget(outputContainer, 1); // the Agent | Files changed tabs

    m_agentDetail = detailPane;
    // Open full width: the table fills the page until a session is selected, at
    // which point showAgentSession() reveals the detail pane beside it.
    detailPane->hide();

    auto *splitter = new QSplitter(Qt::Horizontal);
    splitter->setChildrenCollapsible(false);
    // Non-opaque resize: dragging the divider tracks a lightweight rubber-band
    // line and the panes only resize once, on release. With opaque resize (the
    // Qt default) every mouse-move re-lays-out the detail pane, whose transcript
    // is a scroll area of word-wrapped labels — an O(rows) reflow per pixel that
    // froze the GUI thread and made the drag crawl (see issue #234 relayout cost).
    splitter->setOpaqueResize(false);
    splitter->addWidget(listPane);
    splitter->addWidget(detailPane);
    splitter->setStretchFactor(0, 1);
    splitter->setStretchFactor(1, 1);
    // Equal split so the divider sits in the middle of the screen (issue #85).
    splitter->setSizes({600, 600});

    auto *layout = new QHBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(splitter);

    connect(m_agentTable, &QTableWidget::itemSelectionChanged, this, [this] {
        const QModelIndexList rows = m_agentTable->selectionModel()->selectedRows();
        if (rows.isEmpty())
            return;
        QTableWidgetItem *first = m_agentTable->item(rows.first().row(), 0);
        if (first) {
            showAgentSession(first->data(Qt::UserRole).toInt());
            // Always reveal the latest turn when a session is clicked (adhoc
            // #128). showAgentSession() also runs on every reload, so this lives
            // here in the genuine selection-change handler rather than there —
            // otherwise reloads would yank a scrolled-up reader back to the
            // bottom.
            if (m_agentTranscript)
                m_agentTranscript->jumpToBottom();
        }
    });
    return page;
}

// Steer the currently-selected agent session (m_selectedAgentSessionId) with a
// follow-up message. Shared by the agent detail page's "Send" composer and the
// footer quick-add's up-arrow ("send to the visible agent") button.
void MainWindow::sendPromptToSelectedAgent(const QString &prompt)
{
    // The composer's model dropdown is the user's live choice for what runs
    // next; without this the session kept coasting on whatever model it
    // happened to launch with, so switching the dropdown before following up
    // on an idle/stopped agent silently did nothing. Only a restart (the
    // no-live-process branch in sendPromptToAgentSession below) actually picks
    // the new model up — a still-running process can't be retargeted mid-turn
    // — but stashing it on the session now means the very next resume honors it.
    if (AgentSession *session = findAgentSession(m_selectedAgentSessionId);
        session && session->provider == QLatin1String("claude-code") &&
        m_quickAddClaudeModel) {
        const QString chosen = m_quickAddClaudeModel->currentData().toString();
        if (session->model != chosen) {
            session->model = chosen;
            if (m_agentStore)
                m_agentStore->saveSession(*session);
        }
    }
    sendPromptToAgentSession(m_selectedAgentSessionId, prompt);
}

// Same as sendPromptToSelectedAgent, but for an arbitrary session id rather
// than whichever one is currently open in the UI (adhoc #182: the website can
// steer any of this node's agent sessions, not just the locally-selected one).
void MainWindow::sendPromptToAgentSession(int sessionId, const QString &prompt)
{
    if (prompt.isEmpty() || sessionId < 0)
        return;
    if (ClaudeStreamSession *s = m_streamSessions.value(sessionId);
        s && s->running()) {
        // Steer the live Claude Code transcript session: record the turn in
        // this session's buffer so it survives view switches, then send it.
        const int sid = sessionId;
        QJsonObject turn{{QStringLiteral("type"), QStringLiteral("_local_user")},
                         {QStringLiteral("text"), prompt}};
        applyTranscriptEvent(sid, turn);
        s->sendUserText(prompt);
        // Issue #84: a new prompt nudges our rolling-window usage, so re-poll
        // it now (and once more shortly after) to keep the top-bar chart +
        // hover stats current rather than waiting for the next minute tick.
        bumpClaudeCodeUsage();
        // Replying puts the agent back to work — clear "Waiting", or the
        // Failed left by an error result whose process stayed alive, so the
        // list shows the session running again.
        if (AgentSession *as = findAgentSession(sid);
            as && as->status != AgentStatus::Running) {
            as->status = AgentStatus::Running;
            as->finishedAtMs = 0;
            as->lastError.clear();
            if (m_agentStore)
                m_agentStore->saveSession(*as);
            updateAgentStatusCell(sid);
        }
    } else if (AgentRunner *runner = runnerForSession(sessionId)) {
        runner->steer(prompt);
    } else if (AgentSession *session = findAgentSession(sessionId)) {
        // No live process: the session is stopped, waiting, failed or done.
        // Restart it and fold this message into the resumed run as a steering
        // instruction so the queued message actually takes effect (adhoc #177).
        const int sid = session->id;
        m_pendingSteerMessage.insert(sid, prompt);
        if (session->provider == QLatin1String("claude-code"))
            applyTranscriptEvent(
                sid, QJsonObject{
                         {QStringLiteral("type"), QStringLiteral("_local_user")},
                         {QStringLiteral("text"), prompt}});
        else
            m_agentStore->appendLog(
                *session,
                QStringLiteral("\n==> User steering prompt (queued for restart)\n%1")
                    .arg(prompt));
        continueAgentSession(sid);
    }
}

// Re-sends the full title + description + comment thread of the issue linked
// to the currently-open agent session (adhoc #256). Lets the user recover
// when the agent missed the context the first time — e.g. a resumed session
// only ever gets a bare "Continue where you left off." (see
// startClaudeCodeTranscript), which carries none of it.
void MainWindow::sendIssueContextToSelectedAgent()
{
    AgentSession *session = findAgentSession(m_selectedAgentSessionId);
    if (!session || session->issueNumber <= 0) {
        logSystem(QStringLiteral(
            "No issue-linked agent open above to send context to \xE2\x80\x94 "
            "open one first."));
        return;
    }
    const int repoIndex = repoIndexFor(session->owner, session->name);
    if (repoIndex < 0) {
        logSystem(QStringLiteral("Can't find this session's repository."));
        return;
    }
    const RepositoryRecord &repo = m_repositories.at(repoIndex);
    const QList<Issue> issues =
        IssueStore(repo.localPath, repo.mirrorPath, &m_profileIdentity, m_userName)
            .loadAll();
    const Issue *issue = nullptr;
    for (const Issue &candidate : issues)
        if (candidate.number == session->issueNumber) {
            issue = &candidate;
            break;
        }
    if (!issue) {
        logSystem(QStringLiteral("Could not find issue #%1 to resend its context.")
                      .arg(session->issueNumber));
        return;
    }
    sendPromptToSelectedAgent(issueContextPrompt(*issue));
}

// ---- Website agent sync (adhoc #182) ---------------------------------------
// The repo owner can watch this node's agent sessions on the website and
// steer a running one from the browser. Two directions: push a snapshot of
// local sessions up, and drain any prompts the owner queued there.

QUrl MainWindow::agentsApiUrl(const RepositoryRecord &repo) const
{
    QUrl url = catalogApiUrl();
    url.setPath("/api/repo/" + repoSegment(repo.owner, QStringLiteral("owner")) +
                "/" + repoSegment(repo.name, QStringLiteral("repository")) +
                "/agents");
    return url;
}

// Full-replace snapshot: send ALL of this repo's current sessions every call,
// not a diff (the worker overwrites its stored list). Called on a periodic
// timer (m_agentSyncPushTimer) and, debounced, right after a session's status
// changes (scheduleAgentSessionsPush).
void MainWindow::pushAgentSessionsSnapshot()
{
    if (!m_networkAccess || !m_agentStore || m_repositories.isEmpty())
        return;
    // AgentSession::owner/name are the repo's owner/name (see repoKey()), not
    // this node's own account — group sessions by the repo they belong to.
    QHash<QString, QList<AgentSession>> byRepo;
    for (const AgentSession &s : m_agentSessions) {
        if (s.owner.isEmpty() || s.name.isEmpty())
            continue;
        // Only publish sessions on repos this node's own account owns — a
        // mirror hosting someone else's repo has no local say over its agents.
        if (s.owner.compare(m_userName, Qt::CaseInsensitive) != 0)
            continue;
        byRepo[s.owner + "/" + s.name].append(s);
    }
    if (byRepo.isEmpty())
        return;

    // Dedup by owner/name so a preview and its owned copy don't double-push
    // (mirrors pollOwnedInboxes).
    QSet<QString> seen;
    for (const RepositoryRecord &repo : m_repositories) {
        // Only a repo actually published to the network has a website page to
        // show agents on in the first place — skip local-only/unpublished ones
        // rather than hitting an endpoint the worker has no catalog entry for.
        if (repo.previewOnly || !repo.publishToNetwork)
            continue;
        const QString key = repo.owner + "/" + repo.name;
        if (seen.contains(key) || !byRepo.contains(key))
            continue;
        seen.insert(key);
        // Only a repo actually hosted locally (a writable working copy) is
        // ours to publish agent state for, matching the inbox-drain guard.
        const RepositoryRecord writable = writableRecordFor(repo);
        IssueStore probe(writable.localPath, writable.mirrorPath, &m_profileIdentity,
                         m_userName);
        if (!probe.canWrite())
            continue;
        pushAgentSessionsForRepo(repo, byRepo.value(key));
    }
}

void MainWindow::pushAgentSessionsForRepo(RepositoryRecord repo,
                                          QList<AgentSession> sessions)
{
    if (!m_networkAccess)
        return;
    if (sessions.size() > 300) // worker contract caps at 300
        sessions = sessions.mid(0, 300);

    QJsonArray arr;
    for (const AgentSession &s : sessions) {
        // Bounded tail of the run log for the website's live transcript view
        // (adhoc #259). The cap matches the worker's MAX_AGENT_TRANSCRIPT so the
        // full-replace snapshot stays small even with several sessions per repo.
        constexpr int kMaxTranscript = 16000;
        QString transcript = m_agentStore ? m_agentStore->readLog(s) : QString();
        if (transcript.size() > kMaxTranscript)
            transcript = transcript.right(kMaxTranscript);
        arr.append(QJsonObject{
            {"id", s.id},
            {"issueNumber", s.issueNumber},
            {"issueTitle", s.issueTitle},
            {"status", s.status},
            {"provider", s.provider},
            {"model", s.model},
            {"branchName", s.branchName},
            {"createdAtMs", s.createdAtMs},
            {"startedAtMs", s.startedAtMs},
            {"finishedAtMs", s.finishedAtMs},
            {"numTurns", s.numTurns},
            {"durationMs", s.durationMs},
            {"costUsd", s.costUsd},
            {"lastError", s.lastError},
            {"transcript", transcript},
        });
    }

    QUrl url = agentsApiUrl(repo);
    const QString backoffKey = "agentPush:" + url.toString();
    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    if (!m_pollBackoff.ready(backoffKey, nowMs))
        return;

    const QString owner = repoSegment(repo.owner, QStringLiteral("owner"));
    const QString ts = QString::number(nowMs);
    const QByteArray canonical =
        ("forkmesh-issues-pull-v1\n" + owner + "\n" + ts).toUtf8();
    const QString sig = m_profileIdentity.signData(canonical);

    QUrlQuery query;
    query.addQueryItem("owner", owner);
    query.addQueryItem("ts", ts);
    query.addQueryItem("sig", sig);
    url.setQuery(query);

    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    const QJsonObject payload{{"sessions", arr}};
    QNetworkReply *reply = m_networkAccess->post(
        request, QJsonDocument(payload).toJson(QJsonDocument::Compact));
    connect(reply, &QNetworkReply::finished, this, [this, reply, backoffKey] {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            m_pollBackoff.noteFailure(backoffKey, QDateTime::currentMSecsSinceEpoch());
            return;
        }
        m_pollBackoff.noteSuccess(backoffKey);
    });
}

// Arms (or re-arms) a short debounce timer so a burst of status flips — e.g. a
// run finishing and immediately looper-starting the next one — coalesces into
// a single snapshot push instead of one request per flip.
void MainWindow::scheduleAgentSessionsPush()
{
    if (!m_agentSyncDebounceTimer) {
        m_agentSyncDebounceTimer = new QTimer(this);
        m_agentSyncDebounceTimer->setSingleShot(true);
        connect(m_agentSyncDebounceTimer, &QTimer::timeout, this,
                &MainWindow::pushAgentSessionsSnapshot);
    }
    m_agentSyncDebounceTimer->start(3000);
}

// Periodic drain of prompts the website owner queued for this node's agent
// sessions, across every repo we own/host locally.
void MainWindow::drainAgentPrompts()
{
    if (!m_networkAccess || m_repositories.isEmpty())
        return;
    QSet<QString> seen;
    for (const RepositoryRecord &repo : m_repositories) {
        // Same publish gate as pushAgentSessionsSnapshot: no website page, no
        // prompts to have been queued there.
        if (repo.previewOnly || !repo.publishToNetwork)
            continue;
        const QString key = repo.owner + "/" + repo.name;
        if (seen.contains(key))
            continue;
        const RepositoryRecord writable = writableRecordFor(repo);
        IssueStore probe(writable.localPath, writable.mirrorPath, &m_profileIdentity,
                         m_userName);
        if (!probe.canWrite())
            continue; // not the owner/hoster of this repo
        seen.insert(key);
        drainAgentPromptsFor(repo);
    }
}

void MainWindow::drainAgentPromptsFor(RepositoryRecord repo)
{
    if (!m_networkAccess)
        return;
    QUrl url = agentsApiUrl(repo);
    const QString backoffKey = "agentDrain:" + url.toString();
    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    if (!m_pollBackoff.ready(backoffKey, nowMs))
        return;

    const QString owner = repoSegment(repo.owner, QStringLiteral("owner"));
    const QString ts = QString::number(nowMs);
    const QByteArray canonical =
        ("forkmesh-issues-pull-v1\n" + owner + "\n" + ts).toUtf8();
    const QString sig = m_profileIdentity.signData(canonical);

    QUrlQuery query;
    query.addQueryItem("owner", owner);
    query.addQueryItem("ts", ts);
    query.addQueryItem("sig", sig);
    url.setQuery(query);

    QNetworkReply *reply = m_networkAccess->get(QNetworkRequest(url));
    connect(reply, &QNetworkReply::finished, this, [this, reply, backoffKey, repo] {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            m_pollBackoff.noteFailure(backoffKey, QDateTime::currentMSecsSinceEpoch());
            return;
        }
        m_pollBackoff.noteSuccess(backoffKey);
        const QJsonArray prompts = QJsonDocument::fromJson(reply->readAll())
                                       .object()
                                       .value("prompts")
                                       .toArray();
        for (const QJsonValue &value : prompts) {
            const QJsonObject item = value.toObject();
            const QString text = item.value("text").toString();
            const QString agentId = item.value("agentId").toString();
            if (text.isEmpty()) {
                logSystem(QStringLiteral(
                    "Dropped a malformed website agent prompt."));
                continue;
            }
            // Sentinel "new" (adhoc #266): the website's top-of-list composer asks
            // to spin up a brand-new ad-hoc agent for this repo from the prompt,
            // rather than steer an existing session.
            if (agentId == QLatin1String("new")) {
                // Optional provider chosen in the website composer's dropdown
                // (adhoc #271); empty leaves the node's default in place.
                startWebNewAgentForRepo(repo, text,
                                        item.value("provider").toString());
                continue;
            }
            bool ok = false;
            const int sessionId = agentId.toInt(&ok);
            if (!ok) {
                logSystem(QStringLiteral(
                    "Dropped a malformed website agent prompt."));
                continue;
            }
            deliverQueuedAgentPrompt(sessionId, text);
        }
    });
}

// Steer an agent session with a prompt queued from the website, by session id
// rather than whichever one happens to be open locally — this is
// sendPromptToAgentSession's caller for the website-drain path (adhoc #182).
void MainWindow::deliverQueuedAgentPrompt(int sessionId, const QString &text)
{
    if (!findAgentSession(sessionId)) {
        logSystem(QStringLiteral(
            "Dropped a website prompt: no local agent session #%1.")
                      .arg(sessionId));
        return;
    }
    // Delegate to the same steer-or-resume logic as the in-app composer
    // (sendPromptToSelectedAgent's per-id sibling): a running session gets the
    // text sent straight to its live process, an idle runner is steered, and a
    // stopped/finished session is resumed with the message folded in as a
    // steering instruction (adhoc #177) — a website prompt shouldn't be
    // dropped just because the session isn't running right now.
    sendPromptToAgentSession(sessionId, text);
}

// Start a brand-new ad-hoc agent for a repo from a prompt the website's
// top-of-list composer queued (adhoc #266). Resolves the repo's index in
// m_repositories, then hands off to the same startAdHocAgentForRepo the in-app
// compose row uses, honouring the node's default provider/model choice.
void MainWindow::startWebNewAgentForRepo(const RepositoryRecord &repo,
                                         const QString &task,
                                         const QString &providerOverride)
{
    int repoIndex = -1;
    for (int i = 0; i < m_repositories.size(); ++i) {
        const RepositoryRecord &r = m_repositories.at(i);
        if (r.owner.compare(repo.owner, Qt::CaseInsensitive) == 0
            && r.name.compare(repo.name, Qt::CaseInsensitive) == 0
            && !r.localPath.isEmpty()) {
            repoIndex = i;
            break;
        }
    }
    if (repoIndex < 0) {
        logSystem(QStringLiteral(
            "Dropped a website \"new agent\" prompt: no local checkout for %1/%2.")
                      .arg(repo.owner, repo.name));
        return;
    }
    // Honour the provider the website composer chose (adhoc #271) when it names a
    // known one; otherwise fall back to the node's default provider.
    const QString provider =
        (providerOverride == QLatin1String("claude-code")
         || providerOverride == QLatin1String("claude-api")
         || providerOverride == QLatin1String("openai"))
            ? providerOverride
            : defaultAgentProvider();
    const QString model = provider == QLatin1String("claude-code")
                              ? QSettings().value(kClaudeCodeModelSetting).toString()
                              : QString();
    logSystem(QStringLiteral("Starting a new agent for %1/%2 from a website prompt.")
                  .arg(repo.owner, repo.name));
    startAdHocAgentForRepo(repoIndex, task, provider, /*createPr=*/true, model);
}

void MainWindow::testOpenAiAgentKey()
{
    QSettings settings;
    const QString apiKey = settings.value(kCodexApiKeySetting).toString().trimmed();
    const QString adminKey =
        settings.value(kOpenAiAdminKeySetting).toString().trimmed();
    const QString usageKey = adminKey.isEmpty() ? apiKey : adminKey;
    if (apiKey.isEmpty()) {
        if (m_agentApiKeyStatus)
            m_agentApiKeyStatus->setText("No OpenAI API key saved in Settings.");
        return;
    }
    if (!m_networkAccess) {
        if (m_agentApiKeyStatus)
            m_agentApiKeyStatus->setText("Network client is not ready.");
        return;
    }

    if (m_agentTestApiKeyButton)
        m_agentTestApiKeyButton->setEnabled(false);
    if (m_agentApiKeyStatus)
        m_agentApiKeyStatus->setText("Testing OpenAI key...");

    struct KeyTestState {
        bool modelsOk = false;
        bool usageOk = false;
        bool costsOk = false;
        int modelCount = 0;
        qint64 requests = 0;
        qint64 inputTokens = 0;
        qint64 cachedTokens = 0;
        qint64 outputTokens = 0;
        double costs = 0.0;
        QString currency;
        QString requestId;
        QString organization;
        QString modelsError;
        QString usageError;
        QString costsError;
    };
    auto state = std::make_shared<KeyTestState>();
    const QDateTime now = QDateTime::currentDateTimeUtc();
    const qint64 end = now.toSecsSinceEpoch();
    const qint64 usageStart = end - 24 * 60 * 60;
    const qint64 costsStart =
        QDate(now.date().year(), now.date().month(), 1)
            .startOfDay(QTimeZone(QTimeZone::UTC))
            .toSecsSinceEpoch();
    // Costs are returned in whole UTC-day buckets. Since end_time is
    // exclusive, use the next midnight so the still-open bucket for today is
    // included instead of silently dropping all current-day spend.
    const qint64 costsEnd =
        now.date()
            .addDays(1)
            .startOfDay(QTimeZone(QTimeZone::UTC))
            .toSecsSinceEpoch();

    auto finish = [this, state] {
        if (m_agentTestApiKeyButton)
            m_agentTestApiKeyButton->setEnabled(true);
        if (!m_agentApiKeyStatus)
            return;

        if (!state->modelsOk) {
            if (m_agentOpenAiSpend)
                m_agentOpenAiSpend->setText("OpenAI spend: unavailable");
            m_agentApiKeyStatus->setText(
                QStringLiteral("OpenAI key rejected. %1").arg(state->modelsError));
            return;
        }

        if (m_agentOpenAiSpend) {
            if (state->costsOk) {
                m_openAiSpendUsd = state->costs; // already in dollars
                const QString text =
                    QStringLiteral("OpenAI spend, month to date: %1")
                        .arg(moneyString(state->costs, state->currency));
                m_agentOpenAiSpend->setText(text);
                cacheSpendLabel(kOpenAiSpendTextSetting, kOpenAiSpendTsSetting,
                                text);
            } else {
                m_agentOpenAiSpend->setText("OpenAI spend: unavailable");
            }
        }
        updateAgentTotalSpend();

        const qint64 totalTokens = state->inputTokens + state->outputTokens;
        QStringList lines;
        if (state->usageOk) {
            lines << QStringLiteral(
                         "Last 24h OpenAI usage: %1 requests, %2 total tokens (%3 input, %4 cached input, %5 output).")
                         .arg(formatCount(state->requests))
                         .arg(formatCount(totalTokens))
                         .arg(formatCount(state->inputTokens))
                         .arg(formatCount(state->cachedTokens))
                         .arg(formatCount(state->outputTokens));
        }
        lines << QStringLiteral("OpenAI key works. %1 models visible.")
                     .arg(formatCount(state->modelCount));
        if (!state->organization.isEmpty())
            lines << QStringLiteral("Organization: %1").arg(state->organization);
        if (!state->requestId.isEmpty())
            lines << QStringLiteral("Request ID: %1").arg(state->requestId);
        if (!state->usageOk) {
            lines << QStringLiteral("Usage stats unavailable: %1")
                         .arg(state->usageError);
        }
        if (!state->costsOk) {
            lines << QStringLiteral("Cost stats unavailable: %1")
                         .arg(state->costsError);
        }
        if (!state->usageOk || !state->costsOk)
            lines << QStringLiteral(
                "Organization usage/cost endpoints may require an Admin API key.");
        m_agentApiKeyStatus->setText(lines.join(QStringLiteral("<br>")));
    };

    auto requestCosts = [this, usageKey, costsStart, costsEnd, state, finish] {
        QUrl url(QStringLiteral("https://api.openai.com/v1/organization/costs"));
        QUrlQuery query;
        query.addQueryItem(QStringLiteral("start_time"),
                           QString::number(costsStart));
        query.addQueryItem(QStringLiteral("end_time"),
                           QString::number(costsEnd));
        query.addQueryItem(QStringLiteral("bucket_width"), QStringLiteral("1d"));
        query.addQueryItem(QStringLiteral("limit"), QStringLiteral("31"));
        url.setQuery(query);
        QNetworkReply *reply = m_networkAccess->get(openAiRequest(url, usageKey));
        connect(reply, &QNetworkReply::finished, this, [reply, state, finish] {
            const QByteArray body = reply->readAll();
            if (reply->error() == QNetworkReply::NoError) {
                // A successful response with empty result arrays means the
                // organization spent zero in this period, not that cost data
                // is unavailable.
                state->costsOk = true;
                const QJsonArray buckets =
                    QJsonDocument::fromJson(body).object().value("data").toArray();
                for (const QJsonValue &bucketValue : buckets) {
                    const QJsonArray results =
                        bucketValue.toObject().value("results").toArray();
                    for (const QJsonValue &resultValue : results) {
                        state->costs += costResultTotal(resultValue.toObject(),
                                                        &state->currency,
                                                        nullptr);
                    }
                }
            } else {
                state->costsError = apiErrorSummary(reply, body);
            }
            reply->deleteLater();
            finish();
        });
    };

    auto requestUsage = [this, usageKey, usageStart, end, state, requestCosts] {
        QUrl url(QStringLiteral(
            "https://api.openai.com/v1/organization/usage/completions"));
        QUrlQuery query;
        query.addQueryItem(QStringLiteral("start_time"),
                           QString::number(usageStart));
        query.addQueryItem(QStringLiteral("end_time"), QString::number(end));
        query.addQueryItem(QStringLiteral("bucket_width"), QStringLiteral("1d"));
        url.setQuery(query);
        QNetworkReply *reply = m_networkAccess->get(openAiRequest(url, usageKey));
        connect(reply, &QNetworkReply::finished, this,
                [reply, state, requestCosts] {
                    const QByteArray body = reply->readAll();
                    if (reply->error() == QNetworkReply::NoError) {
                        const QJsonArray buckets =
                            QJsonDocument::fromJson(body)
                                .object()
                                .value("data")
                                .toArray();
                        for (const QJsonValue &bucketValue : buckets) {
                            const QJsonArray results =
                                bucketValue.toObject().value("results").toArray();
                            for (const QJsonValue &resultValue : results) {
                                const QJsonObject result = resultValue.toObject();
                                state->requests +=
                                    jsonCount(result, "num_model_requests");
                                state->inputTokens +=
                                    jsonCount(result, "input_tokens");
                                state->cachedTokens +=
                                    jsonCount(result, "input_cached_tokens");
                                state->outputTokens +=
                                    jsonCount(result, "output_tokens");
                            }
                        }
                        state->usageOk = true;
                    } else {
                        state->usageError = apiErrorSummary(reply, body);
                    }
                    reply->deleteLater();
                    requestCosts();
                });
    };

    // Fetch remaining credit grants (prepaid balance). This endpoint works with
    // a regular API key and returns total granted vs used credit amounts.
    QNetworkReply *creditReply = m_networkAccess->get(
        openAiRequest(QUrl(QStringLiteral(
            "https://api.openai.com/v1/dashboard/billing/credit_grants")),
            apiKey));
    connect(creditReply, &QNetworkReply::finished, this,
            [this, creditReply] {
        const QByteArray body = creditReply->readAll();
        creditReply->deleteLater();
        if (!m_agentOpenAiCredit)
            return;
        if (creditReply->error() != QNetworkReply::NoError) {
            m_agentOpenAiCredit->setText(
                QStringLiteral("OpenAI remaining credits: unavailable (%1)")
                    .arg(apiErrorSummary(creditReply, body)));
            return;
        }
        const QJsonObject obj = QJsonDocument::fromJson(body).object();
        // The response contains total_granted and total_used in USD cents.
        // total_available = total_granted - total_used.
        const double granted = obj.value(QStringLiteral("total_granted")).toDouble();
        const double used    = obj.value(QStringLiteral("total_used")).toDouble();
        const double available = obj.value(QStringLiteral("total_available")).toDouble(
            granted - used);
        const QString text =
            QStringLiteral("OpenAI remaining credits: $%1 USD (of $%2 granted)")
                .arg(QString::number(available, 'f', 2),
                     QString::number(granted, 'f', 2));
        m_agentOpenAiCredit->setText(text);
        cacheSpendLabel(kOpenAiCreditTextSetting, kOpenAiCreditTsSetting, text);
    });

    QNetworkReply *reply =
        m_networkAccess->get(openAiRequest(
            QUrl(QStringLiteral("https://api.openai.com/v1/models")), apiKey));
    connect(reply, &QNetworkReply::finished, this,
            [reply, state, requestUsage, finish] {
                const QByteArray body = reply->readAll();
                if (reply->error() == QNetworkReply::NoError) {
                    state->modelsOk = true;
                    state->requestId = replyHeader(reply, "x-request-id");
                    state->organization =
                        replyHeader(reply, "openai-organization");
                    state->modelCount = QJsonDocument::fromJson(body)
                                            .object()
                                            .value("data")
                                            .toArray()
                                            .size();
                } else {
                    state->modelsError = apiErrorSummary(reply, body);
                }
                reply->deleteLater();
                if (state->modelsOk)
                    requestUsage();
                else
                    finish();
            });
}

void MainWindow::cacheSpendLabel(const QString &textKey, const QString &tsKey,
                                 const QString &text)
{
    QSettings settings;
    settings.setValue(textKey, text);
    settings.setValue(tsKey, QDateTime::currentMSecsSinceEpoch());
}

void MainWindow::applyCachedSpendLabels()
{
    QSettings settings;
    auto restore = [&settings](QLabel *label, const QString &textKey,
                               const QString &tsKey) {
        if (!label)
            return;
        const QString text = settings.value(textKey).toString();
        if (text.isEmpty())
            return;
        const qint64 ts = settings.value(tsKey).toLongLong();
        QString suffix;
        if (ts > 0)
            suffix = QStringLiteral(" (cached %1)")
                         .arg(QDateTime::fromMSecsSinceEpoch(ts).toString(
                             QStringLiteral("MMM d hh:mm")));
        label->setText(text + suffix);
    };
    restore(m_agentOpenAiSpend, kOpenAiSpendTextSetting, kOpenAiSpendTsSetting);
    restore(m_agentClaudeSpend, kClaudeSpendTextSetting, kClaudeSpendTsSetting);
    restore(m_agentOpenAiCredit, kOpenAiCreditTextSetting, kOpenAiCreditTsSetting);
    restore(m_agentClaudeCredit, kClaudeCreditTextSetting, kClaudeCreditTsSetting);
}

void MainWindow::markAgentLimitWindow(const QString &provider)
{
    const bool claude = agentIsClaudeProvider(provider);
    const QString k5h =
        claude ? kClaudeLimit5hStartSetting : kCodexLimit5hStartSetting;
    const QString kWeek =
        claude ? kClaudeLimitWeekStartSetting : kCodexLimitWeekStartSetting;
    QSettings settings;
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    // A rolling window only restarts once the previous one has fully elapsed;
    // activity inside an open window keeps the same reset time.
    auto refreshAnchor = [&](const QString &key, qint64 windowMs) {
        const qint64 start = settings.value(key).toLongLong();
        if (start <= 0 || now - start >= windowMs)
            settings.setValue(key, now);
    };
    refreshAnchor(k5h, kAgentLimit5hMs);
    refreshAnchor(kWeek, kAgentLimitWeekMs);
    refreshAgentLimitLabel();
}

void MainWindow::refreshAgentLimitLabel()
{
    if (!m_agentLimitsLabel)
        return;
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    QSettings settings;
    auto windowText = [&](const QString &key, qint64 windowMs) -> QString {
        const qint64 start = settings.value(key).toLongLong();
        if (start <= 0)
            return QStringLiteral("ready");
        const qint64 remaining = windowMs - (now - start);
        if (remaining <= 0)
            return QStringLiteral("ready");
        return QStringLiteral("resets in %1").arg(humanizeRemaining(remaining));
    };
    auto providerLine = [&](const QString &label, const QString &k5h,
                            const QString &kWeek) {
        return QStringLiteral("%1 — 5h %2 · weekly %3")
            .arg(label, windowText(k5h, kAgentLimit5hMs),
                 windowText(kWeek, kAgentLimitWeekMs));
    };
    m_agentLimitsLabel->setText(
        QStringLiteral("Usage limits · %1 · %2")
            .arg(providerLine(QStringLiteral("Codex"), kCodexLimit5hStartSetting,
                              kCodexLimitWeekStartSetting),
                 providerLine(QStringLiteral("Claude Code"),
                              kClaudeLimit5hStartSetting,
                              kClaudeLimitWeekStartSetting)));
}

void MainWindow::updateAgentTotalSpend()
{
    if (!m_agentTotalSpend)
        return;
    const bool haveOpenAi = !qIsNaN(m_openAiSpendUsd);
    const bool haveClaude = !qIsNaN(m_claudeSpendUsd);
    if (!haveOpenAi && !haveClaude) {
        m_agentTotalSpend->setText(
            "Total Agent API spend this month: not yet refreshed");
        return;
    }
    const double total = (haveOpenAi ? m_openAiSpendUsd : 0.0) +
                         (haveClaude ? m_claudeSpendUsd : 0.0);
    QString note;
    if (!haveOpenAi)
        note = QString::fromUtf8(" (Claude only \xE2\x80\x94 refresh OpenAI)");
    else if (!haveClaude)
        note = QString::fromUtf8(" (OpenAI only \xE2\x80\x94 refresh Claude)");
    m_agentTotalSpend->setText(
        QStringLiteral("Total Agent API spend, month to date: $%1 USD%2")
            .arg(QString::number(total, 'f', 2), note));
}

void MainWindow::applyClaudeUsage(bool weekly, int percent)
{
    const int pct = qBound(0, percent, 100);
    // Feed the figure into the top-bar mini chart (issue #266) and cache it so it
    // survives a restart and renders on the very first frame. The detail-page
    // gauges were retired in issue #84 in favour of this single chart.
    if (m_navTokenUsage)
        static_cast<TokenUsageMiniChart *>(m_navTokenUsage)->setUsage(weekly, pct);
    QSettings settings;
    settings.setValue(weekly ? kClaudeUsageWeekPctSetting
                             : kClaudeUsage5hPctSetting,
                      pct);
    // Issue #346: track "ran out" (>=99%) so a later drop can be recognised as
    // a refill rather than just another low-usage poll, and fire the opt-in
    // email once when that happens.
    const QString exhaustedKey = weekly ? kClaudeUsageWeekExhaustedSetting
                                        : kClaudeUsage5hExhaustedSetting;
    if (pct >= 99) {
        settings.setValue(exhaustedKey, true);
    } else if (pct < 90 && settings.value(exhaustedKey, false).toBool()) {
        settings.setValue(exhaustedKey, false);
        maybeEmailCreditsRefilled(weekly);
    }
}

void MainWindow::maybeEmailCreditsRefilled(bool weekly)
{
    if (!QSettings().value(kEmailOnCreditsRefillSetting, false).toBool())
        return;
    // Rides the existing signed heartbeat channel (accounts/heartbeat) rather
    // than a dedicated endpoint; the worker turns the flag into an in-app
    // notification that the email digest cron mails out (issue #361's rail).
    if (weekly)
        m_pendingCreditsRefilledWeekly = true;
    else
        m_pendingCreditsRefilled5h = true;
    sendNodeHeartbeat();
}

void MainWindow::applyClaudeReset(bool weekly, qint64 resetMs)
{
    QSettings().setValue(weekly ? kClaudeUsageWeekResetSetting
                                : kClaudeUsage5hResetSetting,
                         resetMs);
    if (!m_navTokenUsage)
        return;
    const qint64 remaining = resetMs - QDateTime::currentMSecsSinceEpoch();
    // A window already past its reset (or with no known time) shows no countdown.
    static_cast<TokenUsageMiniChart *>(m_navTokenUsage)
        ->setReset(weekly, remaining > 0 ? humanizeRemaining(remaining)
                                         : QString());
}

void MainWindow::bumpClaudeCodeUsage()
{
    // A just-started agent (or a freshly sent prompt) hasn't consumed anything
    // yet, so refreshing only at that instant leaves the top-bar gauge showing
    // the pre-start figure — which reads as "not updating". Refresh now for any
    // usage already on the clock, then once more after the first turn has had
    // time to land, so the gauge moves promptly. This (plus hovering the chart)
    // is the only thing that hits the usage endpoint now — no background timer.
    refreshClaudeCodeUsage();
    QTimer::singleShot(10 * 1000, this, &MainWindow::refreshClaudeCodeUsage);
}

void MainWindow::refreshClaudeCodeUsage()
{
    if (!m_networkAccess)
        return;
    // Claude Code authenticates with a claude.ai OAuth token, kept in
    // ~/.claude/.credentials.json. Read the access token fresh every poll so a
    // token the CLI has since rotated is picked up automatically; if it is
    // absent (API-key login, or not signed in) there is nothing to query and the
    // rate-limit-event path remains the only feed.
    const QString token = claudeCodeOAuthToken();
    if (token.isEmpty())
        return;
    // Back off exponentially while the usage endpoint is failing (offline /
    // HTTP 429) so a burst of prompt-send / hover refreshes doesn't hammer it.
    if (!m_pollBackoff.ready(QStringLiteral("claude-usage"),
                             QDateTime::currentMSecsSinceEpoch()))
        return;

    QNetworkRequest req(
        QUrl(QStringLiteral("https://api.anthropic.com/api/oauth/usage")));
    req.setRawHeader("Authorization", "Bearer " + token.toUtf8());
    req.setRawHeader("anthropic-beta", "oauth-2025-04-20");
    req.setRawHeader("Accept", "application/json");

    QNetworkReply *reply = m_networkAccess->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        const QByteArray body = reply->readAll();
        reply->deleteLater();
        // On any error (expired token, offline) keep the last-known figures
        // rather than blanking the gauge; the next poll retries — with a
        // growing backoff so a sustained failure stops hammering the endpoint.
        if (reply->error() != QNetworkReply::NoError) {
            m_pollBackoff.noteFailure(QStringLiteral("claude-usage"),
                                      QDateTime::currentMSecsSinceEpoch());
            return;
        }
        m_pollBackoff.noteSuccess(QStringLiteral("claude-usage"));
        const QJsonObject root = QJsonDocument::fromJson(body).object();
        auto pctOf = [&root](const QString &key) {
            return qRound(root.value(key)
                              .toObject()
                              .value(QStringLiteral("utilization"))
                              .toDouble());
        };
        // resets_at is the wall-clock instant the window clears. Accept either an
        // ISO 8601 string or a numeric Unix timestamp (seconds), and tolerate the
        // camelCase spelling, so a format tweak on the endpoint won't silently
        // drop the countdown. Returns 0 when absent/unparseable (issue #50).
        auto resetMsOf = [&root](const QString &key) -> qint64 {
            const QJsonObject win = root.value(key).toObject();
            QJsonValue v = win.value(QStringLiteral("resets_at"));
            if (v.isUndefined() || v.isNull())
                v = win.value(QStringLiteral("resetsAt"));
            if (v.isString()) {
                const QDateTime when =
                    QDateTime::fromString(v.toString(), Qt::ISODate);
                return when.isValid() ? when.toMSecsSinceEpoch() : 0;
            }
            if (v.isDouble()) {
                const double secs = v.toDouble();
                return secs > 0 ? static_cast<qint64>(secs * 1000.0) : 0;
            }
            return 0;
        };
        // five_hour = rolling session window; seven_day = the plan-wide weekly
        // window (matches the "weekly" rate-limit event and the CLI's /usage).
        if (root.contains(QStringLiteral("five_hour"))) {
            applyClaudeUsage(false, pctOf(QStringLiteral("five_hour")));
            if (const qint64 r = resetMsOf(QStringLiteral("five_hour")))
                applyClaudeReset(false, r);
        }
        if (root.contains(QStringLiteral("seven_day"))) {
            applyClaudeUsage(true, pctOf(QStringLiteral("seven_day")));
            if (const qint64 r = resetMsOf(QStringLiteral("seven_day")))
                applyClaudeReset(true, r);
        }
    });
}

// Apply the cached live model list to all claude-code model combos. Used both
// at startup (to apply an already-fetched list to a freshly built combo) and
// from the eventFilter popup-open path (throttle keeps it from hammering the API).
void MainWindow::refreshClaudeModelCombo()
{
    auto applyToAllCombos = [this](const QJsonArray &models) {
        mergeLiveClaudeModels(m_quickAddClaudeModel, models);
        // Restore saved quick-add model after replacing the list.
        if (m_quickAddClaudeModel) {
            const QString saved =
                QSettings().value(kClaudeCodeModelSetting).toString().trimmed();
            const int idx = m_quickAddClaudeModel->findData(saved);
            if (idx >= 0) {
                QSignalBlocker b(m_quickAddClaudeModel);
                m_quickAddClaudeModel->setCurrentIndex(idx);
            }
        }
        // Branch, action, and issue fix combos: only update when set to claude-code.
        const QString claudeCode = QStringLiteral("claude-code");
        if (m_branchFixModelCombo && m_branchFixAgentCombo &&
            m_branchFixAgentCombo->currentData().toString() == claudeCode)
            mergeLiveClaudeModels(m_branchFixModelCombo, models);
        if (m_actionFixModelCombo && m_actionFixAgentCombo &&
            m_actionFixAgentCombo->currentData().toString() == claudeCode)
            mergeLiveClaudeModels(m_actionFixModelCombo, models);
        if (m_issueAgentModel && m_issueAgentProvider &&
            m_issueAgentProvider->currentData().toString() == claudeCode)
            mergeLiveClaudeModels(m_issueAgentModel, models);
    };

    // Apply whatever we have cached so combos built after the last fetch still
    // show the live list without waiting for a new network round-trip.
    if (!m_liveClaudeModels.isEmpty())
        applyToAllCombos(m_liveClaudeModels);

    if (!m_networkAccess)
        return;
    // Throttle: at most one live fetch every 60 seconds. The dropdown-open
    // event filter calls this each time any model combo is opened so the list
    // stays current without hammering /v1/models on every click.
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (m_claudeModelsFetchedMs > 0 &&
        now - m_claudeModelsFetchedMs < 60LL * 1000)
        return;
    // Claude Code authenticates with the claude.ai OAuth token in
    // ~/.claude/.credentials.json. Without it we can't query the model list.
    const QString token = claudeCodeOAuthToken();
    if (token.isEmpty())
        return;
    // Arm the throttle on send so a persistently failing request doesn't retry.
    m_claudeModelsFetchedMs = now;

    QNetworkRequest req(QUrl(
        QStringLiteral("https://api.anthropic.com/v1/models?limit=1000")));
    req.setRawHeader("Authorization", "Bearer " + token.toUtf8());
    req.setRawHeader("anthropic-beta", "oauth-2025-04-20");
    req.setRawHeader("anthropic-version", "2023-06-01");
    req.setRawHeader("Accept", "application/json");

    QNetworkReply *reply = m_networkAccess->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply, applyToAllCombos] {
        const QByteArray body = reply->readAll();
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError)
            return;
        const QJsonArray models =
            QJsonDocument::fromJson(body).object().value("data").toArray();
        if (models.isEmpty())
            return;
        m_liveClaudeModels = models;
        // Persist to disk so the next launch's combos start with the real model
        // list instead of just "Auto" (see the ctor's kClaudeModelsCacheSetting
        // load above buildChatPage()).
        QSettings().setValue(kClaudeModelsCacheSetting,
                             QJsonDocument(models).toJson(QJsonDocument::Compact));
        applyToAllCombos(models);
    });
}

void MainWindow::refreshClaudeSpend()
{
    // Organization cost data comes from the Admin API and needs an Admin key
    // (sk-ant-admin01-...). A regular API key can't read it, so require the
    // admin key rather than silently failing with "unavailable".
    const QString adminKey =
        QSettings().value(kClaudeAdminKeySetting).toString().trimmed();
    if (adminKey.isEmpty()) {
        if (m_agentClaudeStatus)
            m_agentClaudeStatus->setText(
                "Add a Claude Admin API key (sk-ant-admin01-…) in Settings to "
                "see spend. A regular API key can't read organization costs.");
        if (m_agentClaudeSpend)
            m_agentClaudeSpend->setText("Claude spend this month: needs Admin key");
        if (m_agentClaudeCredit)
            m_agentClaudeCredit->setText(
                "Claude remaining credits: add an Admin key to see spend; "
                "remaining balance is visible in the Anthropic Console.");
        return;
    }
    if (!m_networkAccess) {
        if (m_agentClaudeStatus)
            m_agentClaudeStatus->setText("Network client is not ready.");
        return;
    }

    if (m_agentClaudeStatus)
        m_agentClaudeStatus->setText("Refreshing Claude spend...");

    // The Cost API takes RFC 3339 timestamps and daily buckets; start at the
    // first of the month (UTC) through the next midnight so today is included.
    const QDateTime now = QDateTime::currentDateTimeUtc();
    const QDateTime monthStart(QDate(now.date().year(), now.date().month(), 1),
                               QTime(0, 0), QTimeZone(QTimeZone::UTC));
    const QDateTime end(now.date().addDays(1), QTime(0, 0),
                        QTimeZone(QTimeZone::UTC));
    const QString iso = QStringLiteral("yyyy-MM-ddTHH:mm:ssZ");
    const QString startStr = monthStart.toString(iso);
    const QString endStr = end.toString(iso);

    // The cost report is paginated: a daily bucketing of a whole month easily
    // spans several pages, and the early pages can be all-empty buckets while
    // the actual spend lands on a later page. Reading only the first page is
    // what made this report $0.00 — walk every page via `next_page` and sum.
    auto cents = std::make_shared<double>(0.0);
    auto fetchPage = std::make_shared<std::function<void(const QString &)>>();
    *fetchPage = [this, adminKey, startStr, endStr, iso, cents,
                  fetchPage](const QString &page) {
        QUrl url(QStringLiteral(
            "https://api.anthropic.com/v1/organizations/cost_report"));
        QUrlQuery query;
        query.addQueryItem(QStringLiteral("starting_at"), startStr);
        query.addQueryItem(QStringLiteral("ending_at"), endStr);
        query.addQueryItem(QStringLiteral("bucket_width"), QStringLiteral("1d"));
        if (!page.isEmpty())
            query.addQueryItem(QStringLiteral("page"), page);
        url.setQuery(query);

        QNetworkRequest request(url);
        request.setRawHeader("x-api-key", adminKey.toUtf8());
        request.setRawHeader("anthropic-version", "2023-06-01");
        request.setRawHeader("Accept", "application/json");

        QNetworkReply *reply = m_networkAccess->get(request);
        connect(reply, &QNetworkReply::finished, this,
                [this, reply, cents, fetchPage] {
            const QByteArray body = reply->readAll();
            reply->deleteLater();
            if (reply->error() != QNetworkReply::NoError) {
                if (m_agentClaudeStatus)
                    m_agentClaudeStatus->setText(
                        QStringLiteral("Claude spend unavailable: %1")
                            .arg(apiErrorSummary(reply, body)));
                if (m_agentClaudeSpend)
                    m_agentClaudeSpend->setText(
                        "Claude spend this month: unavailable");
                return;
            }
            // Sum every result's `amount`, which is a decimal STRING in cents.
            const QJsonObject root = QJsonDocument::fromJson(body).object();
            for (const QJsonValue &bucket : root.value("data").toArray()) {
                for (const QJsonValue &result :
                     bucket.toObject().value("results").toArray()) {
                    *cents += result.toObject()
                                  .value("amount")
                                  .toString()
                                  .toDouble();
                }
            }
            // Keep paging until the API says there is nothing more.
            const QString next = root.value("next_page").toString();
            if (root.value("has_more").toBool() && !next.isEmpty()) {
                (*fetchPage)(next);
                return;
            }

            const double usd = *cents / 100.0;
            m_claudeSpendUsd = usd;
            if (m_agentClaudeSpend) {
                const QString text =
                    QStringLiteral("Claude spend, month to date: $%1 USD")
                        .arg(QString::number(usd, 'f', 2));
                m_agentClaudeSpend->setText(text);
                cacheSpendLabel(kClaudeSpendTextSetting, kClaudeSpendTsSetting,
                                text);
            }
            if (m_agentClaudeStatus)
                m_agentClaudeStatus->setText("Claude spend refreshed.");
            // Anthropic does not expose a remaining-credits endpoint for the
            // Admin API; derive the best available estimate from the session
            // token tracking data stored on each AgentSession and show it
            // alongside the spend figure.
            if (m_agentClaudeCredit) {
                double totalCreditUsed = 0.0;
                for (const AgentSession &s : std::as_const(m_agentSessions)) {
                    if (s.provider.startsWith(QLatin1String("claude")))
                        totalCreditUsed += s.costUsd;
                }
                const QString creditText =
                    QStringLiteral(
                        "Claude remaining credits: check the Anthropic Console "
                        "(API does not expose credit balance). "
                        "Estimated local session spend: $%1 USD.")
                        .arg(QString::number(totalCreditUsed, 'f', 4));
                m_agentClaudeCredit->setText(creditText);
                cacheSpendLabel(kClaudeCreditTextSetting, kClaudeCreditTsSetting, creditText);
            }
            updateAgentTotalSpend();
        });
    };
    (*fetchPage)(QString());
}

void MainWindow::initAgents()
{
    const QString root =
        QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) +
        QStringLiteral("/agents");
    m_agentStore = new AgentStore(root);
    // Runners are created lazily by acquireAgentRunner() so multiple sessions
    // can run concurrently.

    m_agentSessions = m_agentStore->loadAllSessions();
    for (AgentSession &session : m_agentSessions) {
        if (session.status == AgentStatus::Running) {
            // ForkMesh was restarted while this agent was working. The previous
            // run's process is gone (its output pipe died with the old app), so
            // resume the session automatically instead of abandoning it: re-queue
            // it to pick up from its saved branch, patch and transcript context.
            // AgentRunner clears any worktree the interrupted run leaked behind so
            // the resumed run can re-create one cleanly (issue #242).
            session.status = AgentStatus::Queued;
            session.lastError.clear();
            session.finishedAtMs = 0;
            m_agentStore->saveSession(session);
            m_agentStore->appendLog(
                session, QStringLiteral("\n==> Resuming after ForkMesh restart."));
            m_agentQueue.append(session.id);
        } else if (session.status == AgentStatus::Queued) {
            m_agentQueue.append(session.id);
        }
    }
    m_agentSessions = m_agentStore->loadAllSessions();
    seedSessionTokens();
    refreshAgentStatusRow(); // footer "Agents:" strip reflects sessions from the start
    // The re-queued sessions are NOT started here: initAgents() runs inside the
    // MainWindow constructor, and draining the queue starts Claude transcripts
    // whose assign-time UI jump (switchToAgentsTab → openRepoDetail) fired a
    // dozen cold git reads before the first frame could paint. The drain runs
    // from runDeferredStartup() instead — after the window is exposed and the
    // last repository is restored — with m_agentQuietResume suppressing the jump.
}

void MainWindow::reloadAgents()
{
    if (!m_agentStore)
        return;
    m_agentSessions = m_agentStore->loadAllSessions();
    seedSessionTokens(); // keep the live token counter from regressing on reload
    injectExternalSessions(); // append any surfaced external (watch-only) sessions
    refreshAgentMergeState();  // issue #291: note sessions landed in the base branch
    // issue #170/#289: recompute Diff cells against fresh data. Rather than wiping
    // the whole cache (which made every Agents-tab visit re-shell git for *every*
    // session — the "slight lag" switching from Issues), just arm the refresh:
    // the next refreshAgentTable() re-validates per-session fingerprints and drops
    // only the rows that actually changed. Skipped while a refresh is already in
    // flight — refreshAgentTable's keep-alive pump can re-enter here, and the
    // active pass already covers current data.
    if (!m_agentTableRefreshing)
        m_agentDiffRefreshPending = true;
    refreshAgentTable();
    if (m_selectedAgentSessionId > 0)
        showAgentSession(m_selectedAgentSessionId);
    updateAgentsTabIndicator();
    refreshAgentStatusRow();
    updateAgentsNavBadge();
}

// Count badge on the top-bar Agents nav button (adhoc #194), same look as the
// chat unread badge: sized to the text and pinned to the button's top-right
// corner, showing the total number of known agent sessions.
void MainWindow::updateAgentsNavBadge()
{
    if (!m_agentsNavButton)
        return;
    const int total = m_agentSessions.size();
    if (total > 0) {
        m_agentsNavButton->setText(
            QStringLiteral("Agents (%1)").arg(formatCount(total)));
        m_agentsNavButton->setToolTip(
            QStringLiteral("Agents \xE2\x80\x94 %1 session%2")
                .arg(total)
                .arg(total == 1 ? QString() : QStringLiteral("s")));
    } else {
        m_agentsNavButton->setText(QStringLiteral("Agents"));
        m_agentsNavButton->setToolTip(QStringLiteral("Agents"));
    }
}

void MainWindow::refreshAgentTable()
{
    if (!m_agentTable)
        return;
    // UI-stall fix: each session whose Diff stat isn't memoised yet shells two git
    // reads (agentDiffStat), so a cold refresh after reloadAgents() clears the cache
    // can block the GUI thread for seconds. GitKeepAlive pumps the event loop across
    // those waits so the window stays responsive; the guard stops a queued slot
    // (e.g. a terminal-finished -> reloadAgents firing during the pump) from
    // re-entering and corrupting the half-built table.
    if (m_agentTableRefreshing)
        return;
    m_agentTableRefreshing = true;
    struct RefreshGuard {
        bool &flag;
        ~RefreshGuard() { flag = false; }
    } refreshGuard{m_agentTableRefreshing};
    GitKeepAlive keepAlive;
    QString owner, name;
    if (m_repoDetailIndex >= 0 && m_repoDetailIndex < m_repositories.size()) {
        owner = m_repositories.at(m_repoDetailIndex).owner;
        name = m_repositories.at(m_repoDetailIndex).name;
    }

    const int keep = m_selectedAgentSessionId;
    // Remember where the list was scrolled so a rebuild doesn't snap it back to
    // the top (adhoc #207): queueing a message to the selected agent reloads the
    // table, and setRowCount(0) below resets the scroll. Without restoring it the
    // user is yanked to the top of the list mid-session even though the selection
    // is preserved. Captured here, reapplied after the rows + selection are back.
    const int scrollPos =
        m_agentTable->verticalScrollBar()
            ? m_agentTable->verticalScrollBar()->value()
            : 0;
    // Resolved once for every row's Diff cell (issue #170): the repo's git dir and
    // base branch the per-session ahead/behind probe measures against.
    const QString agentGitDir = repoGitDir();
    const QString agentBase = repoDefaultBranch(repoBranches());
    // Free-text filter (issue #82): substring-match the query against each
    // session's issue number/title, agent, status and PR number.
    const QString query =
        m_agentSearch ? m_agentSearch->text().trimmed() : QString();
    // Iterate a snapshot: GitKeepAlive's pump can run a queued reloadAgents() that
    // reassigns m_agentSessions mid-loop; the implicitly-shared (COW) copy keeps
    // this iterator valid even if the member vector is replaced underneath us.
    const QList<AgentSession> sessions = m_agentSessions;
    // Which rows this repo + search filter will show. Shared by the cache warm-up
    // and the render loop so the two stay in lock-step.
    auto passesFilter = [&](const AgentSession &session) {
        if (session.owner != owner || session.name != name)
            return false;
        if (query.isEmpty())
            return true;
        QStringList haystack{session.issueTitle,
                             agentProviderName(session.provider),
                             agentStatusText(session.status)};
        if (session.issueNumber > 0)
            haystack << QStringLiteral("#%1").arg(session.issueNumber);
        if (session.prNumber > 0)
            haystack << QStringLiteral("#%1").arg(session.prNumber);
        return haystack.join(QLatin1Char(' '))
            .contains(query, Qt::CaseInsensitive);
    };

    // issue #289: when this refresh follows a reloadAgents() (data may have moved),
    // re-validate the Diff cache instead of having reloadAgents() wipe it wholesale.
    // Only entries whose fingerprint changed — the session's own status/branch/merge/
    // finish fields, or the base tip every ahead/behind count is measured against —
    // are dropped, so agentDiffStat() below re-shells git for *those rows only*. An
    // idle Issues→Agents switch leaves every fingerprint unchanged and runs zero git,
    // which is the lag the user reported. A search-as-you-type refresh leaves the
    // flag clear and reuses the cache untouched, exactly as before.
    if (m_agentDiffRefreshPending) {
        m_agentDiffRefreshPending = false;
        // Base tip: one cheap probe (not per-session) since a move here shifts every
        // session's ahead/behind/conflict result.
        QByteArray tipOut;
        if (!agentGitDir.isEmpty() && !agentBase.isEmpty())
            runGitCapture(agentGitDir,
                          {"rev-parse", "--verify", "--quiet",
                           QStringLiteral("refs/heads/%1").arg(agentBase)},
                          &tipOut, nullptr);
        const QString baseTip = QString::fromUtf8(tipOut).trimmed();
        QSet<int> liveIds;
        for (const AgentSession &session : sessions) {
            if (session.owner != owner || session.name != name)
                continue;
            liveIds.insert(session.id);
            // A running/queued session's branch head can advance between refreshes
            // with none of the fields below changing, so always re-shell those — the
            // handful that are active, never the whole list.
            const bool active = session.status == AgentStatus::Running ||
                                session.status == AgentStatus::Waiting ||
                                session.status == AgentStatus::Queued;
            const QString sig =
                active ? QString()
                       : QStringLiteral("%1|%2|%3|%4|%5|%6|%7")
                             .arg(session.status, session.branchName,
                                  session.baseBranch, baseTip)
                             .arg(session.merged ? 1 : 0)
                             .arg(session.finishedAtMs)
                             .arg(session.prNumber);
            if (active || m_agentDiffSig.value(session.id) != sig) {
                m_agentDiffStats.remove(session.id);
                if (active)
                    m_agentDiffSig.remove(session.id);
                else
                    m_agentDiffSig.insert(session.id, sig);
            }
        }
        // Forget entries for sessions no longer shown here (deleted, or we switched
        // repos) so neither map grows without bound across a long session.
        for (auto it = m_agentDiffStats.begin(); it != m_agentDiffStats.end();) {
            if (liveIds.contains(it.key()))
                ++it;
            else
                it = m_agentDiffStats.erase(it);
        }
        for (auto it = m_agentDiffSig.begin(); it != m_agentDiffSig.end();) {
            if (liveIds.contains(it.key()))
                ++it;
            else
                it = m_agentDiffSig.erase(it);
        }
    }

    // Anti-blink: warm each visible row's Diff stat *before* the table is cleared.
    // agentDiffStat shells two git reads on a cold cache, and GitKeepAlive pumps the
    // event loop across those waits. Doing that inside the rebuild left the list
    // visibly blank/half-built across the pumps — the "blink". Pre-warming keeps the
    // previous rows on screen while git runs, so the render loop below is cache-hot
    // and never pumps, repainting in one atomic, flicker-free pass. After the
    // re-validation above this is a no-op for unchanged rows.
    for (const AgentSession &session : sessions)
        if (passesFilter(session))
            agentDiffStat(session, agentGitDir, agentBase);

    // adhoc #210: auto-fix runs over every session in this repo, not just the
    // ones the search box currently shows, so a query in the search field can't
    // hide a conflict from the auto-fix setting. Cache-hot for rows the warm-up
    // above already covered; only a search-filtered-out row costs an extra shell.
    for (const AgentSession &session : sessions)
        if (session.owner == owner && session.name == name)
            maybeAutoFixAgentConflict(
                session, agentDiffStat(session, agentGitDir, agentBase));

    // The rows this repo + search filter will show, in session order (the table's
    // own sort reorders them afterwards). Also count how many merged sessions the
    // "Delete all merged" batch could act on — across the whole repo, before the
    // search filter, since the batch ignores it (adhoc #235).
    QList<const AgentSession *> visible;
    int mergedDeletable = 0;
    for (const AgentSession &session : sessions) {
        if (session.owner != owner || session.name != name)
            continue;
        if (session.merged && !session.branchName.isEmpty()
            && !isExternalSession(session.id))
            ++mergedDeletable;
        if (passesFilter(session))
            visible.append(&session);
    }

    // Stability fix (adhoc #74): when the table already holds exactly this set of
    // session rows, rewrite each row's cells in place instead of clearing the
    // table and rebuilding it. setRowCount(0) + re-insert destroys and recreates
    // every item, and queueing a message re-refreshes the list several times in a
    // row (the status flips, the session may restart), so the wholesale rebuild
    // made the list visibly flash and its Status column blank out between
    // refreshes. Reusing the rows keeps the list steady; a real membership change
    // (a row added or removed) still falls back to a full rebuild.
    bool reuseRows = m_agentTable->rowCount() == visible.size();
    if (reuseRows) {
        QSet<int> present;
        for (int r = 0; r < m_agentTable->rowCount(); ++r)
            if (QTableWidgetItem *it = m_agentTable->item(r, 0))
                present.insert(it->data(Qt::UserRole).toInt());
        for (const AgentSession *s : std::as_const(visible))
            if (!present.contains(s->id)) {
                reuseRows = false;
                break;
            }
    }

    QSignalBlocker block(m_agentTable);
    TableRepaintGuard repaintGuard(m_agentTable);
    // Freeze sorting across the update so writing a cell's sort value can't reorder
    // rows mid-loop (which would move the row out from under us); re-enabling it
    // afterwards re-applies the user's chosen sort in a single pass.
    m_agentTable->setSortingEnabled(false);
    if (reuseRows) {
        for (const AgentSession *sp : std::as_const(visible)) {
            int row = -1;
            for (int r = 0; r < m_agentTable->rowCount(); ++r) {
                QTableWidgetItem *it = m_agentTable->item(r, 0);
                if (it && it->data(Qt::UserRole).toInt() == sp->id) {
                    row = r;
                    break;
                }
            }
            if (row >= 0)
                applyAgentRowCells(row, *sp, agentGitDir, agentBase);
        }
    } else {
        m_agentTable->setRowCount(0);
        for (const AgentSession *sp : std::as_const(visible)) {
            const int row = m_agentTable->rowCount();
            m_agentTable->insertRow(row);
            applyAgentRowCells(row, *sp, agentGitDir, agentBase);
        }
    }
    m_agentTable->setSortingEnabled(true);
    block.unblock();

    if (m_agentDeleteMergedButton) {
        m_agentDeleteMergedButton->setEnabled(mergedDeletable > 0);
        m_agentDeleteMergedButton->setToolTip(
            mergedDeletable > 0
                ? QStringLiteral("Delete the worktree, branch and session of %1 "
                                 "merged agent%2")
                      .arg(mergedDeletable)
                      .arg(mergedDeletable == 1 ? QString() : QStringLiteral("s"))
                : QStringLiteral("No merged agent sessions to delete"));
    }

    int selRow = -1;
    for (int row = 0; row < m_agentTable->rowCount(); ++row) {
        if (m_agentTable->item(row, 0)->data(Qt::UserRole).toInt() == keep) {
            selRow = row;
            break;
        }
    }
    if (selRow < 0 && m_agentTable->rowCount() > 0)
        selRow = 0;
    if (selRow >= 0)
        m_agentTable->selectRow(selRow);
    else
        showAgentSession(-1);
    // Reapply the saved scroll offset last (adhoc #207): selectRow() above only
    // scrolls far enough to make the kept row visible, so on a reload it leaves
    // the view pinned to the top. Restoring the prior offset keeps the user where
    // they were in the list while staying on the active agent's detail.
    if (m_agentTable->verticalScrollBar())
        m_agentTable->verticalScrollBar()->setValue(scrollPos);
}

// Write one Agents-table row's cells for `session`. Reuses each column's existing
// item when present (an in-place refresh, adhoc #74) and creates one of the right
// type when the row is fresh (a full rebuild). Every apply*/setData below fully
// overwrites the cell, so a reused item never keeps stale text/icon/colour.
void MainWindow::applyAgentRowCells(int row, const AgentSession &session,
                                    const QString &agentGitDir,
                                    const QString &agentBase)
{
    if (!m_agentTable)
        return;
    auto plain = [&](int col) -> QTableWidgetItem * {
        QTableWidgetItem *it = m_agentTable->item(row, col);
        if (!it) {
            it = new QTableWidgetItem;
            m_agentTable->setItem(row, col, it);
        }
        return it;
    };
    // Columns that sort on kTableSortRole need a SortTableWidgetItem.
    auto sortable = [&](int col) -> QTableWidgetItem * {
        QTableWidgetItem *it = m_agentTable->item(row, col);
        if (!it) {
            it = new SortTableWidgetItem;
            m_agentTable->setItem(row, col, it);
        }
        return it;
    };

    QTableWidgetItem *idItem = plain(0);
    idItem->setData(Qt::DisplayRole, session.id);
    idItem->setData(Qt::UserRole, session.id);
    // Issue-scoped sessions show "#<issue> <title>"; PR-scoped ones (e.g. the
    // conflict auto-fixer, issueNumber 0) just show their title.
    plain(1)->setText(session.issueNumber > 0
                          ? QStringLiteral("#%1 %2")
                                .arg(session.issueNumber)
                                .arg(session.issueTitle)
                          : session.issueTitle);
    plain(2)->setText(agentProviderName(session.provider));
    // Model column: the LLM model selected for this session.
    applyAgentModelCell(plain(3), session);
    // Status column: text + coloured glyph (issue #108).
    applyAgentStatusCell(plain(4), session);
    // Turns / Time columns: the run summary the CLI reports on finish, each sorting
    // on its raw value (SortTableWidgetItem reads kTableSortRole).
    applyAgentTurnsCell(sortable(5), session);
    applyAgentTimeCell(sortable(6), session);
    QTableWidgetItem *cost = plain(7);
    cost->setData(Qt::DisplayRole, agentCostText(session.costUsd));
    cost->setData(Qt::UserRole, session.costUsd);
    cost->setToolTip(QStringLiteral("Estimated cost of this agent task"));
    // Live token usage, refreshed in place as the session streams (see
    // updateAgentTokenCell). Sort by the raw number, not the formatted text.
    const qint64 toks = sessionTokenTotal(session);
    QTableWidgetItem *tokens = plain(8);
    tokens->setData(Qt::DisplayRole,
                    toks > 0 ? formatCount(toks) : QStringLiteral("-"));
    tokens->setData(Qt::UserRole, static_cast<qlonglong>(toks));
    tokens->setToolTip(QStringLiteral("Tokens used by this agent session"));
    // Speed column: token throughput derived from the token total and run duration.
    applyAgentSpeedCell(sortable(9), session, toks);
    // "Updated" column: the most recent of created/started/finished/merged, shown
    // as a friendly "x ago" string. The tooltip carries the full timestamp and the
    // raw millisecond value drives chronological sorting.
    const qint64 updatedMs =
        qMax(qMax(session.createdAtMs, session.startedAtMs),
             qMax(session.finishedAtMs, session.mergedAtMs));
    QTableWidgetItem *updated = sortable(10);
    updated->setData(Qt::DisplayRole,
                     updatedMs > 0 ? formatIssueRelativeTime(updatedMs)
                                   : QStringLiteral("-"));
    updated->setData(kTableSortRole, static_cast<qlonglong>(updatedMs));
    updated->setToolTip(updatedMs > 0
                            ? QDateTime::fromMSecsSinceEpoch(updatedMs).toString(
                                  QStringLiteral("yyyy-MM-dd HH:mm:ss"))
                            : QString());
    // Diff column (issue #170): files changed + branch ahead/behind, memoised.
    applyAgentDiffCell(sortable(11),
                       agentDiffStat(session, agentGitDir, agentBase), agentBase);
    // Night-rider light: a custom-painted scanner that sweeps while this session
    // streams raw output. AgentScannerDelegate looks the animation state up by the
    // sessionId stashed here in Qt::UserRole.
    QTableWidgetItem *activity = plain(kAgentActivityColumn);
    activity->setData(Qt::UserRole, session.id);
    activity->setToolTip(QStringLiteral(
        "Live activity — sweeps while the agent is streaming output"));
}

AgentSession *MainWindow::findAgentSession(int sessionId)
{
    for (AgentSession &session : m_agentSessions)
        if (session.id == sessionId)
            return &session;
    return nullptr;
}

#ifdef FORKMESH_WINDOW_TESTS
// issue #291: read back the Status-column text the agent list renders for a
// session — "merged" once its worktree/PR lands in the base branch, otherwise the
// run status — so a window test can prove the merge note reaches the list. Goes
// through applyAgentStatusCell (the same painter the live table uses) rather than
// duplicating its logic.
QString MainWindow::testAgentStatusCellText(int sessionId) const
{
    for (const AgentSession &s : m_agentSessions) {
        if (s.id == sessionId) {
            QTableWidgetItem item;
            applyAgentStatusCell(&item, s);
            return item.text();
        }
    }
    return QString();
}

bool MainWindow::testAgentSessionMerged(int sessionId) const
{
    for (const AgentSession &s : m_agentSessions)
        if (s.id == sessionId)
            return s.merged;
    return false;
}
#endif

const AgentSession *MainWindow::latestAgentSessionForIssue(int issueNumber) const
{
    if (issueNumber <= 0)
        return nullptr;
    const int idx = issuesRepoIndex();
    if (idx < 0 || idx >= m_repositories.size())
        return nullptr;
    const RepositoryRecord &repo = m_repositories.at(idx);
    for (const Issue &issue : m_currentIssues) {
        if (issue.number != issueNumber)
            continue;
        for (auto it = issue.events.crbegin(); it != issue.events.crend(); ++it) {
            if (it->type != QLatin1String("agent"))
                continue;
            if (it->agentSessionId <= 0 || it->agentStatus == AgentStatus::Cleared)
                return nullptr;
            for (const AgentSession &session : m_agentSessions) {
                if (session.id == it->agentSessionId && session.owner == repo.owner &&
                    session.name == repo.name && session.issueNumber == issueNumber)
                    return &session;
            }
            return nullptr;
        }
        break;
    }
    for (const AgentSession &session : m_agentSessions) {
        if (session.owner == repo.owner && session.name == repo.name &&
            session.issueNumber == issueNumber)
            return &session;
    }
    return nullptr;
}

const AgentSession *MainWindow::agentSessionForPull(int prNumber,
                                                    const QString &headBranch) const
{
    if (prNumber > 0) {
        for (const AgentSession &session : m_agentSessions) {
            if (session.prNumber == prNumber)
                return &session;
        }
    }
    // No PR-number link: an agent may still be attached through the head branch
    // it ran on (issue #257). Scope to the detail repo so a like-named branch in
    // another repo can't false-match, skip sessions already bound to a different
    // PR, and prefer the most recent matching session.
    if (!headBranch.isEmpty() && m_repoDetailIndex >= 0 &&
        m_repoDetailIndex < m_repositories.size()) {
        const RepositoryRecord &repo = m_repositories.at(m_repoDetailIndex);
        for (auto it = m_agentSessions.crbegin(); it != m_agentSessions.crend();
             ++it) {
            if (it->branchName == headBranch && it->owner == repo.owner &&
                it->name == repo.name &&
                (it->prNumber == 0 || it->prNumber == prNumber))
                return &*it;
        }
    }
    return nullptr;
}

// Issue #291: has this session's worktree/PR landed in the repo's base branch?
// PR-backed sessions defer to the loaded pull's status (so a PR merged here, or
// synced from a peer as merged, both count). Branch-only sessions check that the
// branch still exists and that every commit the run added since its fork point
// is now contained in the base branch — i.e. the work merged, not merely that an
// empty branch trivially shares history.
bool MainWindow::agentSessionLandedInBase(const AgentSession &session,
                                          const QString &dir,
                                          const QString &base) const
{
    if (session.prNumber > 0) {
        for (const PullRequest &pr : m_currentPulls)
            if (pr.number == session.prNumber)
                return pr.status == QLatin1String("merged");
    }
    if (dir.isEmpty() || session.branchName.isEmpty())
        return false;
    if (base.isEmpty() || session.branchName == base)
        return false;
    // The branch must still exist locally to reason about it.
    if (!runGitCapture(dir,
                       {"rev-parse", "--verify", "--quiet",
                        QStringLiteral("refs/heads/%1").arg(session.branchName)},
                       nullptr, nullptr))
        return false;
    // Without a recorded fork point we can't distinguish a merged branch from an
    // un-started one that shares the base's history, so don't guess.
    if (session.baseRef.isEmpty())
        return false;
    auto count = [&](const QString &range) -> int {
        QByteArray out;
        if (!runGitCapture(dir, {"rev-list", "--count", range}, &out, nullptr))
            return -1;
        return QString::fromUtf8(out).trimmed().toInt();
    };
    // The run must have produced commits since it forked …
    if (count(QStringLiteral("%1..%2").arg(session.baseRef, session.branchName)) <= 0)
        return false;
    // … and all of them must now be reachable from base (nothing left outside).
    return count(QStringLiteral("%1..%2").arg(base, session.branchName)) == 0;
}

// Issue #170: the files-changed + branch ahead/behind figures behind a session's
// Diff cell. Files come from the patch captured at run end (so the count survives
// the worktree being cleaned up); ahead/behind is measured against the base
// branch when the session's branch still exists. Results are memoised per session
// so the per-row refresh (incl. search-as-you-type) doesn't re-shell git.
AgentDiffStat MainWindow::agentDiffStat(const AgentSession &session,
                                        const QString &gitDir, const QString &base)
{
    auto cached = m_agentDiffStats.constFind(session.id);
    if (cached != m_agentDiffStats.constEnd())
        return cached.value();

    AgentDiffStat stat;
    // Count the file headers in the captured patch ("diff --git " at line start).
    // Counting newline-anchored occurrences avoids materialising a line list for
    // a large diff; an in-body "diff --git" line is always prefixed by +/-/space.
    if (m_agentStore) {
        const QString patch = m_agentStore->readPatch(session);
        if (!patch.isEmpty()) {
            int files = patch.startsWith(QLatin1String("diff --git ")) ? 1 : 0;
            files += patch.count(QStringLiteral("\ndiff --git "));
            stat.files = files;
        }
    }
    // Ahead/behind of the session branch vs the base branch, when both exist and
    // differ. Same probe the repo's branch list uses (left = base, right = branch
    // → behind, ahead).
    if (!gitDir.isEmpty() && !base.isEmpty() && !session.branchName.isEmpty() &&
        session.branchName != base &&
        runGitCapture(gitDir,
                      {"rev-parse", "--verify", "--quiet",
                       QStringLiteral("refs/heads/%1").arg(session.branchName)},
                      nullptr, nullptr)) {
        QByteArray counts;
        if (runGitCapture(gitDir,
                          {"rev-list", "--left-right", "--count",
                           base + "..." + session.branchName},
                          &counts, nullptr)) {
            const QStringList p = QString::fromUtf8(counts).trimmed().split(
                QRegularExpression(QStringLiteral("\\s+")));
            if (p.size() >= 2) {
                stat.behind = p.at(0).toInt();
                stat.ahead = p.at(1).toInt();
            }
        }
        // Conflict marker (adhoc #229): when the branch is behind base, an
        // in-memory merge (merge-tree --write-tree, which never touches the tree
        // or index) tells us whether re-merging base would conflict — a non-zero
        // exit means it would. Skip already-merged sessions and branches that are
        // up to date with base (no behind commits ⇒ a trivially clean merge).
        if (stat.behind > 0 && !session.merged &&
            !runGitCapture(gitDir,
                           {"merge-tree", "--write-tree", session.branchName, base},
                           nullptr, nullptr))
            stat.conflicted = true;
    }
    m_agentDiffStats.insert(session.id, stat);
    return stat;
}

// Eagerly flag the agent session(s) tied to a just-merged PR or worktree branch
// (issue #291): records the merge time, notes it in the transcript, and refreshes
// the status cell / detail page. Called from the in-app merge flows so the note
// appears even when the PR/branch is about to be deleted. Returns whether any
// session was newly marked.
bool MainWindow::markAgentSessionsMerged(int prNumber, const QString &branch)
{
    if (!m_agentStore)
        return false;
    bool changed = false;
    for (AgentSession &s : m_agentSessions) {
        if (s.merged)
            continue;
        const bool byPr = prNumber > 0 && s.prNumber == prNumber;
        const bool byBranch =
            !branch.isEmpty() && !s.branchName.isEmpty() && s.branchName == branch;
        if (!byPr && !byBranch)
            continue;
        s.merged = true;
        s.mergedAtMs = QDateTime::currentMSecsSinceEpoch();
        m_agentStore->saveSession(s);
        m_agentStore->appendLog(
            s, QStringLiteral("\n==> %1 merged into %2.")
                   .arg(byPr ? QStringLiteral("PR #%1").arg(prNumber)
                             : QStringLiteral("Branch %1").arg(branch),
                        agentMergeBase(s)));
        changed = true;
    }
    if (changed) {
        refreshAgentTable();
        if (m_selectedAgentSessionId > 0)
            showAgentSession(m_selectedAgentSessionId);
    }
    return changed;
}

// Issue #291 catch-all, run on every agent reload: pick up sessions whose
// worktree/PR has landed in the base branch through any path (an in-app merge, a
// peer's merge synced in, or a manual git merge) and record it once. The
// in-app merge flows mark eagerly via markAgentSessionsMerged(); this backs them
// up and covers everything else.
void MainWindow::refreshAgentMergeState()
{
    if (!m_agentStore)
        return;
    // Re-entrancy guard: the GitKeepAlive pump below services queued slots, and a
    // reloadAgents() among them reassigns m_agentSessions — a second pass over the
    // list mid-iteration would dangle the reference we're walking. (Mirrors the
    // m_repoDetailLoading guard in openRepoDetail.)
    if (m_agentMergeStateRefreshing)
        return;
    m_agentMergeStateRefreshing = true;
    QString owner, name;
    if (m_repoDetailIndex >= 0 && m_repoDetailIndex < m_repositories.size()) {
        owner = m_repositories.at(m_repoDetailIndex).owner;
        name = m_repositories.at(m_repoDetailIndex).name;
    }
    // The git dir and default branch are the same for every session of this repo,
    // so resolve them once instead of re-shelling `git branch` (and a possible
    // `symbolic-ref`) inside the per-session check — that repeated work was the
    // bulk of a multi-second GUI stall on repos with many sessions. The remaining
    // per-session reads run under a GitKeepAlive so the event loop keeps pumping
    // and the window stays responsive across the batch.
    GitKeepAlive keepAlive;
    const QString dir = repoGitDir();
    const QString base = repoDefaultBranch(repoBranches());
    for (AgentSession &s : m_agentSessions) {
        if (s.merged || s.owner != owner || s.name != name)
            continue;
        // Nothing has landed while a run is still queued or working; skip the git
        // checks until it has produced something.
        if (s.status == AgentStatus::Queued || s.status == AgentStatus::Running)
            continue;
        if (!agentSessionLandedInBase(s, dir, base))
            continue;
        s.merged = true;
        s.mergedAtMs = QDateTime::currentMSecsSinceEpoch();
        m_agentStore->saveSession(s);
        m_agentStore->appendLog(
            s, QStringLiteral("\n==> Worktree/PR merged into %1.").arg(agentMergeBase(s)));
    }
    m_agentMergeStateRefreshing = false;
}

// branchLinkHtml() — the clickable branch-name builder used here for the agent
// session header — now lives in MainWindowInternal.h so the pull-request header
// and other branch displays can render the same "open in Branches" link (#204).

// HTML for a worktree location shown next to the branch in the agent session
// header. Clicking it opens the branch's row in the Worktrees tab (handled by
// m_agentMeta's linkActivated -> switchToWorktree); the branch is carried in the
// href so the handler can match the row. Empty when there's no worktree on disk.
static QString worktreeLinkHtml(const QString &branch, const QString &worktreePath)
{
    if (branch.isEmpty() || worktreePath.isEmpty())
        return QString();
    const QString href = kWorktreeLinkScheme +
                         QString::fromUtf8(QUrl::toPercentEncoding(branch));
    return QStringLiteral(
               "<a href=\"%1\" style=\"color:#58a6ff;text-decoration:none\">%2</a>")
        .arg(href, worktreePath.toHtmlEscaped());
}

// "PR #N open" for the agent-detail meta line, as a link to that pull request's
// tab (forkmesh-pull:N, handled by m_agentMeta's linkActivated). Lets a session
// with a PR jump straight to it from the detail header.
static QString pullLinkHtml(int prNumber)
{
    const QString href = kPullLinkScheme + QString::number(prNumber);
    return QStringLiteral(
               "<a href=\"%1\" style=\"color:#58a6ff;text-decoration:none\">"
               "PR #%2 open</a>")
        .arg(href)
        .arg(prNumber);
}

// "#N <title>" for the agent-detail meta line, as a link to that issue's tab in
// the session's repo (forkmesh-issue:N, handled by m_agentMeta's linkActivated).
// Shown only when the session was started from an issue (adhoc #138).
static QString issueLinkHtml(int issueNumber, const QString &title)
{
    const QString href = kIssueLinkScheme + QString::number(issueNumber);
    const QString label =
        title.isEmpty()
            ? QStringLiteral("issue #%1").arg(issueNumber)
            : QStringLiteral("issue #%1 %2").arg(issueNumber).arg(title.toHtmlEscaped());
    return QStringLiteral(
               "<a href=\"%1\" style=\"color:#58a6ff;text-decoration:none\">%2</a>")
        .arg(href, label);
}

void MainWindow::showAgentSession(int sessionId)
{
    m_selectedAgentSessionId = sessionId;
    AgentSession *session = findAgentSession(sessionId);
    if (!session) {
        if (m_agentTitle)
            m_agentTitle->setText("Select a session");
        if (m_agentStatusPill)
            m_agentStatusPill->clear();
        if (m_agentMeta)
            m_agentMeta->clear();
        // Drop the per-session token line from the top-bar chart's hover tooltip.
        if (m_navTokenUsage)
            static_cast<TokenUsageMiniChart *>(m_navTokenUsage)->setStats(QString());
        if (m_agentNetPanel)
            m_agentNetPanel->clear();
        if (m_agentViewPrButton)
            m_agentViewPrButton->hide();
        if (m_agentFixConflictsButton)
            m_agentFixConflictsButton->hide();
        if (m_agentCreateIssueButton)
            m_agentCreateIssueButton->hide();
        if (m_agentLog)
            m_agentLog->clear();
        m_agentLogSession = -1; // log emptied out-of-band; force the next set to render
        m_agentDetailTabSession = -1; // next opened session re-starts on the Agent tab
        updateAgentActionState();
        return;
    }

    // Restore a finished/idle Claude Code session's transcript from disk so it
    // survives an app restart — parsed on a worker thread. The first click on a
    // long session used to block on reading events.jsonl right here (the radar
    // visibly froze); now the click paints immediately and the session is
    // re-shown when its history lands.
    ensureStreamEventsLoadedAsync(sessionId);

    // Keep the detail panel collapsed while "Hide detail" is engaged (issue #54);
    // its contents below still update for when the user reopens it.
    if (m_agentDetail && !m_agentDetailHidden)
        m_agentDetail->show();
    // Keep the model line-up fresh as the user browses sessions (throttled inside
    // refreshClaudeModelCombo() so this doesn't hit the provider on every click).
    refreshClaudeModelCombo();
    if (m_agentTitle) {
        if (isExternalSession(sessionId)) {
            const QString label = !session->issueTitle.isEmpty()
                                      ? session->issueTitle
                                      : (session->branchName.isEmpty()
                                             ? QStringLiteral("session")
                                             : session->branchName);
            m_agentTitle->setText(QStringLiteral("External Claude Code · %1").arg(label));
        } else {
            m_agentTitle->setText(
                session->issueNumber > 0
                    ? QStringLiteral("#%1 · %2")
                          .arg(session->issueNumber)
                          .arg(session->issueTitle.isEmpty()
                                   ? agentProviderName(session->provider)
                                   : session->issueTitle)
                    // Ad-hoc sessions (no issue) lead with the prompt-derived
                    // title rather than "pull #0" — before a PR exists prNumber
                    // is 0, and the prompt is what identifies the run anyway.
                    : QStringLiteral("%1 · %2")
                          .arg(agentProviderName(session->provider))
                          .arg(session->issueTitle.isEmpty()
                                   ? QStringLiteral("pull #%1").arg(session->prNumber)
                                   : session->issueTitle));
        }
    }
    // Issue #291: a "merged into <base>" note appended to the meta line once
    // the session's worktree/PR has landed in the base branch. Joined with the
    // block's separator below, like every other part.
    const QString mergedMeta =
        session->merged
            ? QStringLiteral("<span style='color:#a371f7'>merged into %1</span>")
                  .arg(agentMergeBase(*session).toHtmlEscaped())
            : QString();
    // Resolve this session's worktree folder from its branch so the header can
    // show its location next to the branch and link straight to it (adhoc #123).
    QString worktreePath;
    if (!session->branchName.isEmpty()) {
        const int repoIdx = repoIndexFor(session->owner, session->name);
        if (repoIdx >= 0)
            worktreePath = cachedSessionWorktree(
                sessionId, m_repositories.at(repoIdx).localPath,
                session->branchName);
    }
    if (m_agentMeta && isExternalSession(sessionId)) {
        // Rich text so the branch name links to its Branches-tab row and the
        // worktree location links to its Worktrees-tab row (issue #265, adhoc
        // #123); every other part is HTML-escaped to stay literal. Each part
        // sits on its own line (adhoc #156).
        const QString sep = QStringLiteral("<br>");
        QString meta = QStringLiteral("External Claude Code") + sep +
                       QStringLiteral("%1/%2")
                           .arg(session->owner.toHtmlEscaped(),
                                session->name.toHtmlEscaped()) +
                       sep + agentStatusText(session->status).toHtmlEscaped();
        if (!session->branchName.isEmpty()) {
            meta += sep + branchLinkHtml(session->branchName);
            if (!worktreePath.isEmpty())
                meta += sep + worktreeLinkHtml(session->branchName, worktreePath);
        }
        meta += sep + QStringLiteral("watch-only");
        if (!mergedMeta.isEmpty())
            meta += sep + mergedMeta;
        m_agentMeta->setText(meta);
    } else if (m_agentMeta) {
        // PR status, spelled out so it's always visible. When a PR exists it
        // links straight to that pull request's tab from the header (adhoc #53).
        QString pr =
            session->prNumber > 0
                ? pullLinkHtml(session->prNumber)
                : (session->createPr ? QStringLiteral("PR opens on finish")
                                     : QStringLiteral("no PR"))
                      .toHtmlEscaped();
        // Rich text so the branch name, worktree location, PR and issue are links
        // (issues #265, adhoc #53, adhoc #123, adhoc #138); every other part is
        // HTML-escaped to stay literal. Each part sits on its own line (adhoc #156),
        // captioned with a muted "Field:" label so the header reads as a key/value
        // list rather than a bare stack of strings (adhoc #189).
        const QString sep = QStringLiteral("<br>");
        auto labeled = [](const QString &label, const QString &valueHtml) {
            return QStringLiteral("<span style='color:#8b949e'>%1:</span> %2")
                .arg(label.toHtmlEscaped(), valueHtml);
        };
        // Linked issue line — always shown so the tracking state is explicit: a
        // link to the issue when one exists, otherwise a hint pointing at the
        // "Create linked issue" button in the header above (adhoc #189).
        const QString issueValue =
            session->issueNumber > 0
                ? issueLinkHtml(session->issueNumber, session->issueTitle)
                : QStringLiteral("<span style='color:#8b949e'>none yet</span>");
        QStringList lines;
        lines << labeled(QStringLiteral("Agent"),
                         agentProviderName(session->provider).toHtmlEscaped());
        // Which LLM actually did the work. If the session was launched without an
        // explicit model preference, fall back to the actual model reported by the
        // CLI's system:init event (first event in the stream), so the header never
        // shows a blank Model: line when the CLI used its own default.
        QString displayModel = session->model;
        if (displayModel.isEmpty()) {
            const auto &evts = m_streamEvents.value(sessionId);
            for (const QJsonObject &ev : evts) {
                if (ev.value(QStringLiteral("type")).toString() == QLatin1String("system")
                    && ev.value(QStringLiteral("subtype")).toString() == QLatin1String("init")) {
                    displayModel = ev.value(QStringLiteral("model")).toString().trimmed();
                    break;
                }
            }
        }
        lines << labeled(QStringLiteral("Model"),
                         agentModelLabel(displayModel).toHtmlEscaped());
        lines << labeled(QStringLiteral("Repo"),
                         QStringLiteral("%1/%2").arg(session->owner.toHtmlEscaped(),
                                                     session->name.toHtmlEscaped()));
        lines << labeled(QStringLiteral("Status"),
                         agentStatusText(session->status).toHtmlEscaped());
        lines << labeled(QStringLiteral("Issue"), issueValue);
        lines << labeled(QStringLiteral("Branch"),
                         session->branchName.isEmpty()
                             ? QStringLiteral("(no branch)")
                             : branchLinkHtml(session->branchName));
        if (!worktreePath.isEmpty())
            lines << labeled(QStringLiteral("Worktree"),
                             worktreeLinkHtml(session->branchName, worktreePath));
        lines << labeled(QStringLiteral("PR"), pr);
        const qint64 dur = agentEffectiveDurationMs(*session);
        if (dur > 0)
            lines << labeled(QStringLiteral("Ran for"),
                             QStringLiteral("%1s").arg(dur / 1000));
        if (!mergedMeta.isEmpty())
            lines << mergedMeta;
        m_agentMeta->setText(lines.join(sep));
    }
    setAgentUsageLabel(*session);
    refreshAgentStatusPill(sessionId);

    // View PR button appears once a pull request exists for this session.
    if (m_agentViewPrButton) {
        m_agentViewPrButton->setVisible(session->prNumber > 0);
        if (session->prNumber > 0)
            m_agentViewPrButton->setText(
                QStringLiteral("View PR #%1").arg(session->prNumber));
    }
    // "Fix conflicts with agent": visible only when the cached diff stat says this
    // branch conflicts with base (adhoc #28). The button is hidden until a stat is
    // available; it becomes visible on the next refreshAgentTable() that computes it.
    if (m_agentFixConflictsButton) {
        auto statIt = m_agentDiffStats.constFind(sessionId);
        const bool hasConflict =
            statIt != m_agentDiffStats.constEnd() && statIt->conflicted;
        m_agentFixConflictsButton->setVisible(hasConflict);
    }
    // "Create linked issue" only makes sense for an ad-hoc, owner-side session
    // that isn't already tracked by one. External (watch-only) sessions and
    // mirror checkouts can't write issue events, so hide it there (adhoc #189).
    if (m_agentCreateIssueButton) {
        const int repoIdx = repoIndexFor(session->owner, session->name);
        const bool canTrack =
            session->issueNumber == 0 && !isExternalSession(sessionId) &&
            repoIdx >= 0 &&
            IssueStore(m_repositories.at(repoIdx).localPath,
                       m_repositories.at(repoIdx).mirrorPath, &m_profileIdentity,
                       m_userName)
                .canWrite();
        m_agentCreateIssueButton->setVisible(canTrack);
    }

    // Pick the right output surface. A Claude Code session renders its OWN
    // buffered transcript (so output never leaks between sessions); legacy
    // terminal sessions show the embedded terminal; everything else the log. The
    // Transcript|Raw toggle and the edited-files panel show only for transcript
    // sessions. Each surface populates m_agentLog through setAgentLogText() so a
    // re-show of the same unchanged session skips the costly re-layout (adhoc #245).
    const bool external = isExternalSession(sessionId);
    const bool eventsLoading = m_streamEventsLoading.contains(sessionId);
    const bool transcript =
        external || eventsLoading || isStreamTranscriptSession(sessionId);
    // The session log (megabytes for a long run) feeds the [net] traffic panel
    // and the legacy log surface. Read + scan it on a worker thread: blocking
    // the click on that disk read is what paused the radar between agent clicks.
    if (m_agentStore) {
        const AgentSession snapshot = *session;
        AgentStore *store = m_agentStore;
        const QString status = session->status;
        const bool wantLogSurface = !transcript;
        runOffThread<AgentLogScan>(
            [store, snapshot] { return scanAgentLog(store->readLog(snapshot)); },
            [this, sessionId, status, wantLogSurface](AgentLogScan scan) {
                if (sessionId != m_selectedAgentSessionId)
                    return; // clicked away while the read ran
                applyAgentNetworkPanel(scan, status);
                if (wantLogSurface)
                    setAgentLogText(sessionId, scan.log);
            });
    }
    if (external) {
        // Skip the full tail re-read/rebuild when this session is already on
        // screen and its file hasn't grown — reloadAgents() re-shows the open
        // session constantly, and re-parsing a 400 KB tail per refresh was a
        // steady main-thread hitch.
        const qint64 read = m_externalReadOffset.value(sessionId, -1);
        const QString extPath = m_externalSurfaced.value(sessionId).path;
        if (m_renderedExternalSession != sessionId || read < 0 ||
            QFileInfo(extPath).size() > read)
            renderExternalTranscript(sessionId, /*full=*/true);
    } else if (eventsLoading) {
        // History is still being parsed off-thread: present the (cleared)
        // transcript surface now — the load's completion re-runs
        // showAgentSession and builds the rows.
        if (m_renderedTranscriptSession != sessionId && m_agentTranscript) {
            m_agentTranscript->clear();
            m_renderedTranscriptSession = -1;
            m_renderedExternalSession = -1; // the view no longer shows one
        }
    } else if (isStreamTranscriptSession(sessionId)) {
        // Only rebuild the transcript widget tree when it's actually stale: a
        // different session was shown, or events were added since the last render.
        // Re-showing the same unchanged session (the common reloadAgents() case)
        // now skips the expensive teardown/rebuild that was freezing the UI.
        if (m_renderedTranscriptSession != sessionId
            || m_renderedTranscriptCount != m_streamEvents.value(sessionId).size())
            renderTranscriptForSession(sessionId);
        refreshAgentFilesPanel(sessionId);
        // Only lay the raw log out when it's the surface on screen; while the
        // transcript is shown, showAgentRawOutput() rebuilds it from m_streamRaw
        // on toggle anyway, so laying out megabytes of JSON here was pure waste.
        if (m_agentOutputStack && m_agentOutputStack->currentWidget() == m_agentLog)
            setAgentLogText(sessionId, m_streamRaw.value(sessionId));
    } else if (m_agentLog && m_agentLogSession != sessionId) {
        // Legacy log/terminal session: m_agentLog is the visible surface; its
        // text lands from the async read above. Blank a *different* session's
        // leftover log rather than showing it while the read runs.
        m_agentLog->setPlainText(QString());
        m_agentLogSession = -1; // set out-of-band; the async set re-renders
    }
    if (m_agentOutputToggle)
        m_agentOutputToggle->setVisible(transcript);
    // The "Files changed" tab only applies to local transcript sessions (external
    // sessions have no worktree/diff here). Hide it otherwise and fall back to the
    // Agent tab so the user never lands on an empty tab.
    if (m_agentDetailTabs && m_agentFilesTabIndex >= 0) {
        const bool filesOk = transcript && !external;
        m_agentDetailTabs->setTabVisible(m_agentFilesTabIndex, filesOk);
        // Land on the Agent tab whenever a *different* session is opened, so the
        // detail page always starts on the transcript rather than re-showing the
        // last session's Files-changed tab (adhoc #189). A plain refresh of the
        // same session leaves the user's current tab choice untouched.
        if (m_agentDetailTabSession != sessionId) {
            m_agentDetailTabSession = sessionId;
            m_agentDetailTabs->setCurrentIndex(0);
        } else if (!filesOk &&
                   m_agentDetailTabs->currentIndex() == m_agentFilesTabIndex) {
            m_agentDetailTabs->setCurrentIndex(0);
        }
    }
    updateAgentFilesTabState(sessionId);
    if (m_agentOutputStack) {
        const bool termLive = sessionId == m_terminalSessionId && m_agentTerminal &&
                              m_agentTerminal->isRunning();
        if (transcript) {
            const bool raw = m_terminalModeButton && m_terminalModeButton->isChecked();
            m_agentOutputStack->setCurrentWidget(
                raw ? static_cast<QWidget *>(m_agentLog)
                    : static_cast<QWidget *>(m_agentTranscript));
        } else if (termLive) {
            m_agentOutputStack->setCurrentWidget(m_agentTerminal);
        } else {
            m_agentOutputStack->setCurrentWidget(m_agentLog);
        }
    }
    updateAgentActionState();
}

void MainWindow::setAgentLogText(int sessionId, const QString &text)
{
    if (!m_agentLog)
        return;
    // Skip the re-layout when the same session's log is already on screen with
    // identical text. setPlainText()+moveCursor(End) forces QPlainTextEdit to lay
    // out the whole document (cursorRect -> initCharAttributes over every block),
    // which for a large transcript blocked the GUI thread for ~2.9 s every time
    // refreshAgentTable() re-selected the open session (adhoc #245).
    if (m_agentLogSession == sessionId && m_agentLogText == text)
        return;
    // Streaming growth: the new text usually just extends what's on screen.
    // Insert only the delta at the end (incremental layout) instead of paying
    // setPlainText()'s full re-layout of a multi-megabyte document per burst.
    if (m_agentLogSession == sessionId && !m_agentLogText.isEmpty() &&
        text.startsWith(m_agentLogText)) {
        QTextCursor cursor(m_agentLog->document());
        cursor.movePosition(QTextCursor::End);
        cursor.insertText(text.mid(m_agentLogText.size()));
        m_agentLogText = text;
        m_agentLog->moveCursor(QTextCursor::End);
        return;
    }
    m_agentLogSession = sessionId;
    m_agentLogText = text;
    m_agentLog->setPlainText(text);
    m_agentLog->moveCursor(QTextCursor::End); // raw log opens at the tail
}

// Count a session log's "[net]" markers. Pure — runs on a worker thread (the
// log can be megabytes; this scan per click was part of the radar pause).
MainWindow::AgentLogScan MainWindow::scanAgentLog(QString log)
{
    AgentLogScan scan;
    static const QRegularExpression tokenRe(
        QStringLiteral("in=(\\d+)\\s+out=(\\d+)"));
    const auto lines = QStringView(log).split(QLatin1Char('\n'));
    for (const auto &lineView : lines) {
        const QString line = lineView.toString();
        if (!line.contains(QLatin1String("[net]")))
            continue;
        if (line.contains(QLatin1String("request #")))
            ++scan.requests;
        else if (line.contains(QLatin1String("response #"))) {
            ++scan.responses;
            const auto m = tokenRe.match(line);
            if (m.hasMatch()) {
                scan.inTokens += m.captured(1).toLongLong();
                scan.outTokens += m.captured(2).toLongLong();
            }
        } else if (line.contains(QLatin1String("error #")))
            ++scan.errors;
    }
    scan.log = std::move(log);
    return scan;
}

void MainWindow::applyAgentNetworkPanel(const AgentLogScan &scan, const QString &status)
{
    if (!m_agentNetPanel)
        return;
    const int requests = scan.requests, responses = scan.responses,
              errors = scan.errors;
    const long long inTokens = scan.inTokens, outTokens = scan.outTokens;

    // Codex (external CLI) sessions don't emit our markers — keep the panel out
    // of the way rather than showing an empty graphic.
    if (requests == 0 && responses == 0) {
        m_agentNetPanel->hide();
        return;
    }
    m_agentNetPanel->show();

    const bool live = status == AgentStatus::Running;
    const QString dot = live ? "#3fb950" : "#8b949e";
    auto fmtTokens = [](long long n) {
        if (n >= 1000)
            return QStringLiteral("%1k").arg(n / 1000.0, 0, 'f', 1);
        return QString::number(n);
    };
    // Proportional bars (▇) for input vs output token volume.
    const long long maxTok = qMax<long long>(1, qMax(inTokens, outTokens));
    auto bar = [&](long long n, const QString &color) {
        const int width = int((double(n) / double(maxTok)) * 22.0 + 0.5);
        return QStringLiteral("<span style='color:%1'>%2</span>")
            .arg(color, QString(qMax(n > 0 ? 1 : 0, width),
                                QChar(0x2587))); // ▇
    };
    const QString errText =
        errors > 0 ? QString::fromUtf8(
                         " \xC2\xB7 <span style='color:#f85149'>%1 error%2</span>")
                         .arg(errors)
                         .arg(errors == 1 ? "" : "s")
                   : QString();
    m_agentNetPanel->setText(
        QString::fromUtf8(
            "<table cellspacing='0' cellpadding='0' style='font-size:12px'>"
            "<tr><td style='padding-bottom:3px'>"
            "<span style='color:%1'>\xE2\x97\x8F</span> "
            "<b style='color:#8b949e'>\xF0\x9F\x8C\x90 API traffic</b> "
            "<span style='color:#8b949e'>\xC2\xB7 %2 request%3 \xC2\xB7 %4 response%5%6</span>"
            "</td></tr>"
            "<tr><td><span style='color:#8b949e'>\xE2\x86\x91 in&nbsp;</span>"
            "%7 <span style='color:#8b949e'>&nbsp;%8</span></td></tr>"
            "<tr><td><span style='color:#8b949e'>\xE2\x86\x93 out</span>&nbsp;"
            "%9 <span style='color:#8b949e'>&nbsp;%10</span></td></tr>"
            "</table>")
            .arg(dot)
            .arg(requests)
            .arg(requests == 1 ? "" : "s")
            .arg(responses)
            .arg(responses == 1 ? "" : "s")
            .arg(errText)
            .arg(bar(inTokens, "#58a6ff"), fmtTokens(inTokens))
            .arg(bar(outTokens, "#d2a8ff"), fmtTokens(outTokens)));
}

AgentRunner::Config MainWindow::agentConfigForProvider(const QString &provider) const
{
    AgentRunner::Config config;
    config.contextWindow =
        qMax(1000, QSettings().value(kAgentContextSetting, 32000).toInt());
    config.maxOutputTokens =
        qMax(256, QSettings().value(kAgentMaxOutputSetting, 2000).toInt());
    config.promptPreamble = agentPromptPreamble();
    if (provider == QLatin1String("claude-code")) {
        // Claude Code: the real `claude` CLI, run headlessly in the worktree.
        // Authenticate via the CLI's own claude.ai login, never an API key:
        // setting ANTHROPIC_API_KEY alongside a logged-in session makes the CLI
        // warn that auth "may not work as expected". Naming the key (without a
        // value) still lets AgentRunner strip any inherited ANTHROPIC_API_KEY.
        config.command = claudeCodeCommandSetting();
        config.apiKeyName = QStringLiteral("ANTHROPIC_API_KEY");
    } else if (provider.startsWith(QLatin1String("claude"))) {
        // Claude API: bundled Python script talking to api.anthropic.com. Legacy
        // "claude" sessions resolve here too.
        config.command = claudeCommandSetting();
        config.apiKeyName = QStringLiteral("ANTHROPIC_API_KEY");
        config.apiKey = QSettings().value(kClaudeApiKeySetting).toString().trimmed();
    } else {
        // OpenAI API: the Codex CLI driven with an isolated home so it
        // authenticates with the OPENAI/CODEX API key rather than a login. Legacy
        // "codex" sessions resolve here too.
        config.command = codexCommandSetting();
        config.apiKeyName = QStringLiteral("CODEX_API_KEY");
        config.apiKey = QSettings().value(kCodexApiKeySetting).toString().trimmed();
        config.model = QSettings().value(kCodexModelSetting).toString().trimmed();
        config.preferApiKeyAuth = true;
        config.isolatedHome =
            QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) +
            QStringLiteral("/agents/codex-api-home");
    }
    return config;
}

void MainWindow::assignIssueToAgent(const QString &provider, const QString &model)
{
    if (m_currentIssueNumber < 0)
        return;
    const Issue *issue = nullptr;
    for (const Issue &candidate : std::as_const(m_currentIssues))
        if (candidate.number == m_currentIssueNumber)
            issue = &candidate;
    if (!issue)
        return;
    const bool createPr =
        m_issueAgentCreatePrCheck && m_issueAgentCreatePrCheck->isChecked();
    startAgentForIssue(*issue, provider, createPr, /*quiet=*/false, model);
}

int MainWindow::startAgentForIssue(const Issue &issue, const QString &provider,
                                   bool createPr, bool quiet, const QString &model,
                                   const RepositoryRecord *repoHint)
{
    if (!m_agentStore || issue.number <= 0)
        return 0;
    RepositoryRecord repo;
    IssueStore issueStore(QString(), QString(), &m_profileIdentity, m_userName);
    if (repoHint) {
        repo = *repoHint;
        const RepositoryRecord &writable = writableRecordFor(repo);
        issueStore = IssueStore(writable.localPath, writable.mirrorPath,
                               &m_profileIdentity, m_userName);
    } else {
        const int idx = issuesRepoIndex();
        if (idx < 0 || idx >= m_repositories.size())
            return 0;
        repo = m_repositories.at(idx);
        issueStore = issueStoreForCurrentRepo();
    }
    // A node that only mirrors this repo (no working tree) can still run an
    // agent: it builds the change in a throwaway worktree off the mirror and
    // opens a pull request to the owner. So require a local copy to work from —
    // a working tree when we host it, or the network mirror — rather than write
    // access to the issue store, which only the host has (adhoc #191).
    if (repoAgentGitDir(repo).isEmpty()) {
        if (!quiet)
            setIssueInlineNotice(
                "No local copy of this repository to run a coding agent on.", true);
        return 0;
    }
    AgentSession session;
    session.owner = repo.owner;
    session.name = repo.name;
    session.issueNumber = issue.number;
    session.issueTitle = issue.title;
    session.provider = provider;
    session.createPr = createPr;
    session.model = model.trimmed(); // empty leaves the provider's own default
    session.contextWindow =
        qMax(1000, QSettings().value(kAgentContextSetting, 32000).toInt());
    session = m_agentStore->createSession(session);
    // A descriptive, related branch name: agent/issue-<n>-<title-slug>.
    QString slug;
    for (QChar ch : session.issueTitle.toLower()) {
        const char a = ch.toLatin1();
        if ((a >= 'a' && a <= 'z') || (a >= '0' && a <= '9'))
            slug.append(ch);
        else if (!slug.isEmpty() && !slug.endsWith(QLatin1Char('-')))
            slug.append(QLatin1Char('-'));
    }
    slug = slug.left(48);
    while (slug.endsWith(QLatin1Char('-')))
        slug.chop(1);
    if (slug.isEmpty())
        slug = provider;
    session.branchName =
        QStringLiteral("agent/issue-%1-%2").arg(session.issueNumber).arg(slug);
    m_agentStore->saveSession(session);
    m_agentStore->appendLog(
        session,
        QStringLiteral("==> Assigned from ForkMesh issue #%1.").arg(issue.number));

    // Record the assignment as a signed issue event so it syncs to other nodes —
    // but only when we can write the issue store. On a mirror we can't (and it
    // wouldn't reach the owner anyway), so the run is tracked locally only and
    // still lands as a pull request when it finishes (adhoc #191).
    if (issueStore.canWrite()) {
        QString error;
        if (!issueStore.assignAgent(issue.number, provider, session.id,
                                    session.createPr, AgentStatus::Queued, &error)) {
            session.status = AgentStatus::Failed;
            session.lastError = error.isEmpty()
                                    ? QStringLiteral("Could not write issue event.")
                                    : error;
            m_agentStore->saveSession(session);
            if (!quiet)
                setIssueInlineNotice(session.lastError, true);
            reloadAgents();
            return 0;
        }
        // Reflect the assignment in the Assignee field too: add the agent's name
        // so the issue shows who is working it (and its avatar in the list). A
        // best-effort follow-up — the agent is already running if this fails.
        const QString agentName = agentProviderName(provider);
        if (!issue.assignees.contains(agentName)) {
            QStringList assignees = issue.assignees;
            assignees << agentName;
            issueStore.setAssignees(issue.number, assignees);
        }
    }

    const int sessionId = session.id;
    m_agentQueue.append(sessionId);
    reloadAgents();
    reloadIssues();
    if (!quiet) {
        const bool autoSwitch =
            QSettings().value(kAutoSwitchToAgentSetting, true).toBool();
        const bool onAgentsTab =
            m_repoDetailStack && m_repoDetailStack->currentIndex() == 3;
        const bool onIssuesTab =
            m_repoDetailStack && m_repoDetailStack->currentIndex() == 2;
        if (autoSwitch && !onAgentsTab && !onIssuesTab) {
            switchToAgentsTab(sessionId);
        } else {
            setIssueInlineNotice(
                QStringLiteral("Assigned %1 session #%2.")
                    .arg(agentProviderName(provider))
                    .arg(sessionId));
        }
    }
    processAgentQueue();
    return sessionId;
}

void MainWindow::createLinkedIssueForSelectedSession()
{
    AgentSession *session = findAgentSession(m_selectedAgentSessionId);
    if (!session)
        return;
    if (session->issueNumber > 0) {
        flashMessage("This session is already linked to an issue.");
        return;
    }
    const int repoIndex = repoIndexFor(session->owner, session->name);
    if (repoIndex < 0) {
        flashMessage("Can't find this session's repository.", true);
        return;
    }
    const RepositoryRecord &repo = m_repositories.at(repoIndex);
    IssueStore issueStore(repo.localPath, repo.mirrorPath, &m_profileIdentity,
                          m_userName);
    if (!issueStore.canWrite()) {
        flashMessage("Only the host can create a linked issue.", true);
        return;
    }
    // Title from the run's prompt-derived title; the full prompt (when one was
    // captured for an ad-hoc run) becomes the issue body so the issue carries the
    // task the agent was actually given.
    const QString title =
        session->issueTitle.isEmpty() ? QStringLiteral("Agent run #%1").arg(session->id)
                                      : session->issueTitle;
    const QString body =
        session->prompt.trimmed() == title.trimmed() ? QString() : session->prompt;
    QString error;
    const int number = issueStore.createIssue(title, body, {}, QString(), 0, {}, {},
                                              &error);
    if (number < 0) {
        flashMessage(error.isEmpty() ? QStringLiteral("Could not create the issue.")
                                     : error,
                     true);
        return;
    }
    // Link both sides: stamp the session with the new issue, and record the agent
    // assignment on the issue so it shows the session like an issue-started run.
    session->issueNumber = number;
    session->issueTitle = title;
    m_agentStore->saveSession(*session);
    if (!issueStore.assignAgent(number, session->provider, session->id,
                                session->createPr, session->status, &error)) {
        // The issue exists and the session is linked locally; the assignment event
        // just couldn't be written. Surface it but don't roll back the link.
        flashMessage(error.isEmpty()
                         ? QStringLiteral("Linked issue #%1 created, but could not "
                                          "record the agent on it.")
                               .arg(number)
                         : error,
                     true);
    }
    reloadIssues();
    propagateRepoUpdate(repoIndex);
    reloadAgents();
    showAgentSession(session->id);
    flashMessage(QStringLiteral("Created and linked issue #%1.").arg(number));
}

// Toggle the issue looper (adhoc #92). On: capture the default agent and start
// the first open issue. Off: leave any in-flight session running but don't start
// any more once it finishes.
void MainWindow::toggleIssueLooper()
{
    if (m_looperActive) {
        m_looperActive = false;
        m_looperSessionId = 0;
        m_looperCurrentIssue = 0;
        m_looperCurrentTitle.clear();
        updateIssueLooperButton();
        setIssueInlineNotice(
            "Issue looper stopped. The current agent (if any) will finish; no more "
            "issues will be started.");
        return;
    }
    const int idx = issuesRepoIndex();
    if (idx < 0 || idx >= m_repositories.size()) {
        updateIssueLooperButton();
        return;
    }
    const RepositoryRecord &repo = m_repositories.at(idx);
    // Run wherever we can drive agents on this repo — our own working tree when we
    // host it, or a bare mirror of someone else's repo, which loops through the
    // backlog and opens pull requests back to the owner (adhoc #191).
    if (repoAgentGitDir(repo).isEmpty()) {
        setIssueInlineNotice(
            "No local copy of this repository to run the issue looper on.", true);
        updateIssueLooperButton();
        return;
    }
    m_looperActive = true;
    m_looperProvider = defaultAgentProvider();
    m_looperRepoSlug = repo.owner + QLatin1Char('/') + repo.name;
    updateIssueLooperButton();
    setIssueInlineNotice(
        QString::fromUtf8("Issue looper started with %1. Working through the open "
                          "backlog one issue at a time\xE2\x80\xA6")
            .arg(agentProviderName(m_looperProvider)));
    looperStartNext();
}

// Pick the highest-priority open issue that's free for the looper to pick up, or
// nullptr when none qualify. The looper only touches open issues that are
// unassigned, have a priority set, and are not already being handled (adhoc #42)
// — anything assigned, untriaged, or already covered by an agent session or a
// linked PR is left alone. "Already being handled" is supplied by the caller via
// hasLocalSession so this stays static + pure for the window tests; the caller
// folds both the agent-session and linked-PR checks into that predicate.
// Highest priority wins; ties go to the lowest issue number.
const Issue *MainWindow::looperPickNext(
    const QList<Issue> &issues,
    const std::function<bool(int)> &hasLocalSession)
{
    const Issue *next = nullptr;
    int bestPriority = 1 << 30;
    for (const Issue &issue : issues) {
        if (issue.isDeleted() || issue.status != QLatin1String("open"))
            continue;
        if (hasLocalSession(issue.number))
            continue; // already attempted by an agent or covered by a linked PR
        if (!issue.assignees.isEmpty())
            continue; // assigned to someone — leave it to them
        if (issue.priority <= 0)
            continue; // no priority set — not triaged for the looper yet
        const int p = issue.priority;
        if (p < bestPriority ||
            (p == bestPriority && (!next || issue.number < next->number))) {
            bestPriority = p;
            next = &issue;
        }
    }
    return next;
}

// Pick the highest-priority open, unclaimed issue that has no agent session yet
// and start the looper's agent on it. Stops the looper when nothing is left to
// do. Each issue is attempted at most once (any existing session — queued,
// running, done, or failed — disqualifies it), so the loop always makes forward
// progress. Before starting, the issue is assigned to this node (adhoc #38) so
// the claim syncs and no other looper grabs the same task.
void MainWindow::looperStartNext()
{
    if (!m_looperActive)
        return;
    const Issue *next = looperPickNext(m_currentIssues, [this](int number) {
        // Treat an issue as already handled if an agent has taken it or a PR is
        // already linked to it (adhoc #42 — leave covered issues alone).
        return latestAgentSessionForIssue(number) != nullptr ||
               !pullsLinkedToIssue(number).isEmpty();
    });
    if (!next) {
        m_looperActive = false;
        m_looperSessionId = 0;
        m_looperCurrentIssue = 0;
        m_looperCurrentTitle.clear();
        updateIssueLooperButton();
        setIssueInlineNotice(
            "Issue looper finished: no open, unassigned, prioritized issues left "
            "without an agent or linked PR.");
        return;
    }
    // startAgentForIssue()/looperClaimIssue() rebuild m_currentIssues, so copy the
    // issue out first (we still pass it by reference to startAgentForIssue below).
    const Issue picked = *next;
    const int issueNumber = picked.number;
    const QString issueTitle = picked.title;
    // Claim the issue for this node before starting, so a concurrent scan here or
    // on another mirror sees it as taken and skips it (adhoc #38).
    looperClaimIssue(issueNumber, picked.assignees);
    const int sessionId = startAgentForIssue(picked, m_looperProvider,
                                             /*createPr=*/true, /*quiet=*/true);
    if (sessionId <= 0) {
        m_looperActive = false;
        m_looperSessionId = 0;
        m_looperCurrentIssue = 0;
        m_looperCurrentTitle.clear();
        updateIssueLooperButton();
        setIssueInlineNotice("Issue looper stopped: could not start the next agent.",
                             true);
        return;
    }
    m_looperSessionId = sessionId;
    m_looperCurrentIssue = issueNumber;
    m_looperCurrentTitle = issueTitle;
    updateIssueLooperButton(); // refresh the banner subtitle for the new issue
    setIssueInlineNotice(
        QString::fromUtf8("Issue looper: started %1 on issue #%2 \xE2\x80\x94 %3")
            .arg(agentProviderName(m_looperProvider))
            .arg(issueNumber)
            .arg(issueTitle));
}

QString MainWindow::nodeAssigneeTag() const
{
    const QString name = m_userName.trimmed();
    if (!name.isEmpty())
        return name;
    const QString key = m_profileIdentity.publicKey();
    return key.isEmpty() ? QString() : key.left(12);
}

// Mark this node as an assignee of the issue the looper just took so the claim
// syncs to other nodes and no second looper (here or on another mirror) starts
// the same task. Best-effort: the host writes and commits the assignment (which
// syncs to mirrors), while a mirror with no write access files it to the owner's
// inbox to merge and sync back (adhoc #38).
void MainWindow::looperClaimIssue(int number, const QStringList &existingAssignees)
{
    const QString tag = nodeAssigneeTag();
    if (tag.isEmpty())
        return;
    for (const QString &a : existingAssignees)
        if (a.compare(tag, Qt::CaseInsensitive) == 0)
            return; // already claimed by this node
    QStringList assignees = existingAssignees;
    assignees.append(tag);

    IssueStore store = issueStoreForCurrentRepo();
    if (store.canWrite()) {
        store.setAssignees(number, assignees, nullptr);
        return;
    }
    submitIssueAssigneesToInbox(number, assignees);
}

// Called from both agent-completion paths. When the finished session is the one
// the looper is watching, advance to the next open issue.
void MainWindow::looperOnSessionFinished(int sessionId)
{
    if (!m_looperActive || sessionId <= 0 || sessionId != m_looperSessionId)
        return;
    m_looperSessionId = 0;
    looperStartNext();
}

void MainWindow::updateIssueLooperButton()
{
    // Drive the floating toggle above the Issues tab (adhoc #130): on/off state,
    // the issue currently being worked, and a neon loop that animates while on.
    if (auto *toggle = static_cast<LooperToggle *>(m_looperToggle)) {
        toggle->setActive(m_looperActive);
        toggle->setIssueNumber(m_looperActive ? m_looperCurrentIssue : 0);
        positionLooperToggle(); // anchor + reveal over the Issues tab
    }
    persistLooperState();
}

// Quick-add bar "No issue" mode (issue #299): start a brand-new agent from a
// free-form prompt in the open repository. Unlike the issue-assigned path this
// has no issue to anchor to, so the session is issue-less (issueNumber 0) and
// the typed prompt becomes the agent's task verbatim. It still runs in its own
// worktree/branch and opens a pull request on finish, like every transcript run.
int MainWindow::startAdHocAgentForRepo(int repoIndex, const QString &task,
                                       const QString &provider, bool createPr,
                                       const QString &model)
{
    if (!m_agentStore || task.isEmpty())
        return 0;
    if (repoIndex < 0 || repoIndex >= m_repositories.size()) {
        flashMessage("Open a repository first to start an agent.", true);
        return 0;
    }
    const RepositoryRecord &repo = m_repositories.at(repoIndex);
    if (repo.localPath.isEmpty()) {
        flashMessage("This repository has no local checkout to run the agent in.",
                     true);
        return 0;
    }

    AgentSession session;
    session.owner = repo.owner;
    session.name = repo.name;
    session.issueNumber = 0; // ad-hoc: not tied to any issue
    session.prompt = task;   // persisted so the run can resume after a restart
    session.provider = provider;
    session.createPr = createPr;
    session.model = model.trimmed(); // empty leaves the provider's own default
    session.contextWindow =
        qMax(1000, QSettings().value(kAgentContextSetting, 32000).toInt());
    // A short title from the prompt's first line, for the list row and the PR.
    QString title = task.section(QLatin1Char('\n'), 0, 0).simplified();
    if (title.size() > 80)
        title = title.left(77) + QString::fromUtf8("\xE2\x80\xA6");
    session.issueTitle =
        title.isEmpty() ? QStringLiteral("Ad-hoc agent run") : title;
    session = m_agentStore->createSession(session);
    // Descriptive branch: agent/adhoc-<id>-<title-slug>.
    QString slug;
    for (QChar ch : session.issueTitle.toLower()) {
        const char a = ch.toLatin1();
        if ((a >= 'a' && a <= 'z') || (a >= '0' && a <= '9'))
            slug.append(ch);
        else if (!slug.isEmpty() && !slug.endsWith(QLatin1Char('-')))
            slug.append(QLatin1Char('-'));
    }
    slug = slug.left(48);
    while (slug.endsWith(QLatin1Char('-')))
        slug.chop(1);
    if (slug.isEmpty())
        slug = QStringLiteral("agent");
    session.branchName =
        QStringLiteral("agent/adhoc-%1-%2").arg(session.id).arg(slug);
    m_agentStore->saveSession(session);
    m_agentStore->appendLog(
        session,
        QStringLiteral("==> Started from a prompt (%1).\n")
            .arg(agentProviderName(provider)));

    if (provider == QLatin1String("claude-code")) {
        // Claude Code renders as a native stream-json transcript; the typed
        // prompt is its task verbatim.
        startClaudeCodeTranscript(session, Issue(), repo.localPath, task);
    } else {
        // API-key agents run headlessly through a runner. There's no issue to
        // anchor to, so the task rides through the config as an override prompt.
        AgentRunner::Config config = agentConfigForProvider(provider);
        config.taskOverride = task;
        if (!session.model.isEmpty())
            config.model = session.model;
        markAgentLimitWindow(provider);
        acquireAgentRunner()->start(session, Issue(), repo.localPath, config);
        reloadAgents();
        switchToAgentsTab(session.id);
    }
    return session.id;
}

// Compose row at the top of the session list (adhoc #234): start an ad-hoc
// agent from the typed prompt in the picked repo with the chosen provider.
void MainWindow::startAgentFromComposer()
{
    if (!m_agentComposePrompt || !m_agentComposeRepo || !m_agentComposeProvider)
        return;
    const QString prompt = m_agentComposePrompt->text().trimmed();
    if (prompt.isEmpty()) {
        flashMessage("Type a task first to start an agent.", true);
        m_agentComposePrompt->setFocus();
        return;
    }
    if (m_agentComposeRepo->currentIndex() < 0) {
        flashMessage("Open a repository with a local checkout first.", true);
        return;
    }
    bool ok = false;
    const int repoIndex = m_agentComposeRepo->currentData().toInt(&ok);
    if (!ok || repoIndex < 0 || repoIndex >= m_repositories.size())
        return;
    const QString provider = m_agentComposeProvider->currentData().toString();
    // Claude Code honours the model saved by the footer/model chooser; the API
    // providers fall back to their own default (empty).
    const QString model = provider == QLatin1String("claude-code")
                              ? QSettings().value(kClaudeCodeModelSetting).toString()
                              : QString();
    if (startAdHocAgentForRepo(repoIndex, prompt, provider, /*createPr=*/true,
                               model) > 0)
        m_agentComposePrompt->clear();
}

// Save a pasted image to a stable temp file (not auto-removed: it must outlive
// this call and be readable once the agent starts). Returns the path, or empty.
QString MainWindow::saveNewAgentPromptImage(const QImage &image)
{
    if (image.isNull())
        return QString();
    const QString dir =
        QStandardPaths::writableLocation(QStandardPaths::TempLocation) +
        QStringLiteral("/forkmesh-agent-images");
    QDir().mkpath(dir);
    QTemporaryFile file(dir + QStringLiteral("/paste-XXXXXX.png"));
    file.setAutoRemove(false);
    if (!file.open())
        return QString();
    const QString path = file.fileName();
    const bool ok = image.save(&file, "PNG");
    file.close();
    if (!ok) {
        QFile::remove(path);
        return QString();
    }
    return path;
}

void MainWindow::continueSelectedAgentSession()
{
    continueAgentSession(m_selectedAgentSessionId);
}

// Same as continueSelectedAgentSession, but for an arbitrary session id
// (adhoc #182: a website-queued prompt may resume a session that isn't the
// one currently open locally). showAgentSession() moves the UI's selection
// and opens the detail pane — a background resume triggered from the browser
// must not yank the view away from whatever the user is looking at, so it's
// only called here when the resumed session was already the selected one
// (i.e. this is really the continueSelectedAgentSession path).
void MainWindow::continueAgentSession(int sessionId)
{
    if (!m_agentStore || sessionId <= 0)
        return;
    AgentSession *session = findAgentSession(sessionId);
    if (!session)
        return;
    // Already running (in its own runner) or queued — nothing to do. Other
    // sessions may run in parallel, so we don't block on a global "busy".
    if (session->status == AgentStatus::Running ||
        session->status == AgentStatus::Queued ||
        runnerForSession(session->id))
        return;

    session->status = AgentStatus::Queued;
    session->lastError.clear();
    session->finishedAtMs = 0;
    // The branch may already carry a "merged" badge from a prior run; continuing
    // it reuses that same branch for more work, so it's in progress again, not
    // a done-and-merged session — clear the flag so the list shows it queued/
    // running instead of stuck on the stale merged badge.
    session->merged = false;
    session->mergedAtMs = 0;
    m_agentStore->saveSession(*session);
    m_agentStore->appendLog(
        *session,
        QStringLiteral("\n==> Session continued from ForkMesh."));
    // Capture the id before reloadAgents() rebuilds m_agentSessions, which frees
    // the backing array and leaves `session` dangling (a use-after-free crash if
    // dereferenced afterwards).
    const int sid = session->id;
    if (!m_agentQueue.contains(sid))
        m_agentQueue.append(sid);
    reloadAgents();
    if (sid == m_selectedAgentSessionId)
        showAgentSession(sid);
    processAgentQueue();
}

// Ask sessionId's agent to merge base and resolve conflicts, then resume it.
// Used both by the "Fix conflicts with agent" button (selected session) and by
// maybeAutoFixAgentConflict() (any idle session whose branch conflicts with
// base, when the auto-fix setting is on).
void MainWindow::fixAgentConflictsWithAgent(int sessionId)
{
    AgentSession *s = findAgentSession(sessionId);
    if (!s)
        return;
    const QString base = agentMergeBase(*s);
    const QString prompt =
        QStringLiteral("Merge `%1` into your branch and resolve all merge conflicts. "
                       "Make sure the build and tests still pass, then commit.")
            .arg(base);
    const int sid = s->id;
    m_pendingSteerMessage.insert(sid, prompt);
    if (s->provider == QLatin1String("claude-code"))
        applyTranscriptEvent(
            sid,
            QJsonObject{{QStringLiteral("type"), QStringLiteral("_local_user")},
                        {QStringLiteral("text"), prompt}});
    continueAgentSession(sid);
}

// adhoc #210: with kAutoFixAgentConflictsSetting on (the default), an idle
// session whose branch would conflict with base gets the same treatment as a
// manual click on "Fix conflicts with agent" — no need to notice and click it
// by hand. m_agentAutoFixAttempted stops a conflict that survives a retry (or
// a session sitting Failed/Stopped) from re-queuing the agent on every
// refreshAgentTable(); it's cleared below once the conflict is actually gone,
// so a later, genuinely new conflict on the same session can auto-fix again.
void MainWindow::maybeAutoFixAgentConflict(const AgentSession &session,
                                           const AgentDiffStat &stat)
{
    if (!stat.conflicted) {
        m_agentAutoFixAttempted.remove(session.id);
        return;
    }
    if (session.status == AgentStatus::Running ||
        session.status == AgentStatus::Queued ||
        session.status == AgentStatus::Waiting)
        return; // already active; conflict will be re-checked once it finishes
    if (m_agentAutoFixAttempted.contains(session.id))
        return;
    if (!QSettings().value(kAutoFixAgentConflictsSetting, true).toBool())
        return;
    m_agentAutoFixAttempted.insert(session.id);
    fixAgentConflictsWithAgent(session.id);
}

void MainWindow::deleteSelectedAgentSession()
{
    // External (watch-only) rows aren't in the store — Delete kills the real CLI
    // process it mirrors (if still running), then drops the temporary mirror.
    if (isExternalSession(m_selectedAgentSessionId)) {
        deleteExternalSession(m_selectedAgentSessionId);
        return;
    }
    if (!deleteStoredAgentSession(m_selectedAgentSessionId))
        return;
    reloadAgents();
    reloadIssues();
    refreshIssueList();
    updateIssueActionState();
    flashMessage("Agent session deleted.");
}

bool MainWindow::deleteStoredAgentSession(int sessionId)
{
    if (!m_agentStore || sessionId <= 0)
        return false;
    AgentSession *session = findAgentSession(sessionId);
    if (!session)
        return true; // already gone — nothing to delete

    const AgentSession snapshot = *session;
    if (AgentRunner *runner = runnerForSession(snapshot.id)) {
        runner->stop();
        if (runner->busy()) {
            flashMessage("Stopping agent session. Delete it again once it exits.");
            return false;
        }
    }
    // A live Claude Code stream session (no runner) is killed by its own Stop
    // path; deleting it from the list must stop it too, then release its worktree
    // so the branch is freed (issue #74).
    if (m_streamSessions.contains(snapshot.id))
        stopStreamSession(snapshot.id, /*refreshUi=*/false);
    cleanupStreamWorktree(snapshot.id);
    m_agentQueue.removeAll(snapshot.id);
    m_streamPending.remove(snapshot.id); // drop any queued-but-undelivered messages

    const int repoIndex = repoIndexFor(snapshot.owner, snapshot.name);
    // PR-scoped sessions (issueNumber 0, e.g. the conflict auto-fixer) carry no
    // issue event to clear — skip straight to removing the session.
    if (repoIndex >= 0 && snapshot.issueNumber > 0) {
        const RepositoryRecord repo = m_repositories.at(repoIndex);
        IssueStore issueStore(repo.localPath, repo.mirrorPath, &m_profileIdentity,
                              m_userName);
        if (!issueStore.canWrite()) {
            flashMessage("Only the host can delete an agent session from the issue.",
                         true);
            return false;
        }
        QString error;
        if (!issueStore.assignAgent(snapshot.issueNumber, QString(), 0, false,
                                    AgentStatus::Cleared, &error)) {
            flashMessage(error.isEmpty()
                             ? QStringLiteral("Could not clear the issue agent.")
                             : error,
                         true);
            return false;
        }
    }

    if (!m_agentStore->deleteSession(snapshot)) {
        flashMessage("Could not delete the agent session.", true);
        return false;
    }
    // Wipe every in-memory buffer keyed by this id BEFORE the next createSession()
    // can hand the number back out (nextId() reuses the highest deleted id), or the
    // reused id would inherit this dead session's cached transcript/resume state.
    purgeSessionState(snapshot.id);
    if (m_selectedAgentSessionId == sessionId)
        m_selectedAgentSessionId = -1;
    return true;
}

// See the header: everything below is keyed by session id, and a deleted id can be
// re-issued to a brand-new session. Clearing it here keeps the new run from picking
// up the old one's transcript, files, worktree, tokens or resume id.
void MainWindow::purgeSessionState(int sessionId)
{
    if (sessionId <= 0)
        return;
    if (ClaudeStreamSession *stream = m_streamSessions.take(sessionId))
        stream->deleteLater();
    m_streamEvents.remove(sessionId);
    m_streamRaw.remove(sessionId);
    m_streamFiles.remove(sessionId);
    m_streamWorktree.remove(sessionId);
    m_streamPending.remove(sessionId);
    m_streamSessionInfo.remove(sessionId);
    m_sessionWorkdirCache.remove(sessionId);
    m_pendingSteerMessage.remove(sessionId);
    m_sessionTokens.remove(sessionId);
    m_lastAssistantText.remove(sessionId);
    m_scannerStates.remove(sessionId);
    m_agentDiffStats.remove(sessionId);
    m_agentDiffSig.remove(sessionId);
    m_streamEventsLoading.remove(sessionId);
    m_streamEventsAbsent.remove(sessionId);
    m_agentQueue.removeAll(sessionId);
    // Scalar "what's currently rendered" guards: reset any that point at the id so
    // the next showAgentSession() for a reused id rebuilds instead of no-op'ing.
    if (m_renderedTranscriptSession == sessionId) {
        m_renderedTranscriptSession = -1;
        m_renderedTranscriptCount = -1;
    }
    if (m_renderedExternalSession == sessionId)
        m_renderedExternalSession = -1;
    if (m_agentDiffRenderedSession == sessionId) {
        m_agentDiffRenderedSession = -1;
        m_agentDiffLastHtml.clear();
    }
    if (m_agentLogSession == sessionId) {
        m_agentLogSession = -1;
        m_agentLogText.clear();
    }
    if (m_terminalSessionId == sessionId)
        m_terminalSessionId = -1;
}

void MainWindow::openAgentSessionFromIssue()
{
    if (const AgentSession *session = latestAgentSessionForIssue(m_currentIssueNumber))
        switchToAgentsTab(session->id);
}

void MainWindow::switchToAgentsTab(int sessionId)
{
    const AgentSession *session = findAgentSession(sessionId);
    if (!session)
        return;
    // The Agents tab lives inside repo detail, which is only visible on the
    // Home section (index 0) — jump there first so this works no matter which
    // section (Settings, Chat, Notifications, ...) the click came from.
    showSection(0);
    const int repoIndex = repoIndexFor(session->owner, session->name);
    if (repoIndex >= 0 && repoIndex != m_repoDetailIndex)
        openRepoDetail(repoIndex);
    if (m_repoDetailTabs && m_repoDetailTabs->button(3))
        m_repoDetailTabs->button(3)->setChecked(true);
    if (m_repoDetailStack)
        m_repoDetailStack->setCurrentIndex(3);
    // Mark the Agents nav button as selected (adhoc #201).
    if (m_agentsNavButton)
        m_agentsNavButton->setChecked(true);
    reloadAgents();
    showAgentSession(sessionId);
}

// Rebuild the footer "Agents:" status strip (adhoc #111) from m_agentSessions:
// one small status glyph per known session (adhoc #114 swapped the plain
// colored dots for the same icon set the Agents table's Status column uses —
// a green spinner while running, purple merge mark once landed, orange hand
// while waiting, etc — via agentStatusOcticon), click-through to that
// session's Agents tab. Called after every reloadAgents() so the strip tracks
// the same data as the Agents table.
void MainWindow::refreshAgentStatusRow()
{
    if (!m_agentStatusIconsLayout || !m_agentStatusRow)
        return;
    QLayoutItem *item;
    while ((item = m_agentStatusIconsLayout->takeAt(0)) != nullptr) {
        delete item->widget();
        delete item;
    }
    m_agentStatusRow->setVisible(!m_agentSessions.isEmpty());
    bool anyRunning = false;
    for (const AgentSession &session : std::as_const(m_agentSessions)) {
        auto *dot = new QPushButton;
        dot->setObjectName("agentStatusDot");
        dot->setFlat(true);
        dot->setCursor(Qt::PointingHandCursor);
        dot->setFixedSize(18, 18);
        dot->setIconSize(QSize(14, 14));
        const bool running = !session.merged && session.status == AgentStatus::Running;
        // Running sessions are seeded at frame 0 here; animateAgentStatusIcons()
        // spins them the same way animateRunningAgentIcons() spins the table.
        dot->setIcon(agentStatusOcticon(session, 14));
        dot->setProperty("agentStatusSpin", running);
        anyRunning = anyRunning || running;
        const QString label = session.issueNumber > 0
            ? QStringLiteral("#%1 %2").arg(session.issueNumber).arg(session.issueTitle)
            : session.prompt.left(80);
        dot->setToolTip(QStringLiteral("%1/%2 \xE2\x80\x94 %3\n%4")
                             .arg(session.owner, session.name,
                                  session.merged ? QStringLiteral("merged")
                                                 : agentStatusText(session.status),
                                  label));
        const int sessionId = session.id;
        connect(dot, &QPushButton::clicked, this,
                [this, sessionId] { switchToAgentsTab(sessionId); });
        m_agentStatusIconsLayout->addWidget(dot);
    }
    m_agentStatusIconsLayout->addStretch(1);

    if (anyRunning) {
        if (!m_agentStatusSpinTimer) {
            m_agentStatusSpinTimer = new QTimer(this);
            connect(m_agentStatusSpinTimer, &QTimer::timeout, this,
                    &MainWindow::animateAgentStatusIcons);
        }
        if (!m_agentStatusSpinTimer->isActive())
            m_agentStatusSpinTimer->start(120);
    } else if (m_agentStatusSpinTimer) {
        m_agentStatusSpinTimer->stop();
    }
}

// Spin the green "sync" glyph on every running icon in the footer "Agents:"
// strip (adhoc #114), mirroring animateRunningAgentIcons()'s treatment of the
// Agents table. Driven by m_agentStatusSpinTimer, which only ticks while at
// least one session in the strip is running (see refreshAgentStatusRow).
void MainWindow::animateAgentStatusIcons()
{
    if (!m_agentStatusIconsLayout)
        return;
    m_agentStatusSpinFrame = (m_agentStatusSpinFrame + 1) % 10;
    const QIcon icon(rotatedTintedOcticonPixmap(
        "sync", QColor("#3fb950"), 14, m_agentStatusSpinFrame * 36.0));
    for (int i = 0; i < m_agentStatusIconsLayout->count(); ++i) {
        QWidget *w = m_agentStatusIconsLayout->itemAt(i)->widget();
        if (w && w->property("agentStatusSpin").toBool())
            static_cast<QPushButton *>(w)->setIcon(icon);
    }
}

// Clicking the "Agents:" label (as opposed to one of its dots): jump to the
// most relevant session's Agents tab, or just the open repo's Agents tab if
// no session exists yet.
void MainWindow::openAgentsOverview()
{
    if (m_selectedAgentSessionId > 0 && findAgentSession(m_selectedAgentSessionId)) {
        switchToAgentsTab(m_selectedAgentSessionId);
        return;
    }
    if (!m_agentSessions.isEmpty()) {
        const AgentSession *newest = &m_agentSessions.first();
        for (const AgentSession &s : std::as_const(m_agentSessions)) {
            if (s.createdAtMs > newest->createdAtMs)
                newest = &s;
        }
        switchToAgentsTab(newest->id);
        return;
    }
    if (m_repoDetailIndex >= 0 && m_repoDetailTabs && m_repoDetailTabs->button(3)) {
        showSection(0);
        m_repoDetailTabs->button(3)->setChecked(true);
        if (m_repoDetailStack)
            m_repoDetailStack->setCurrentIndex(3);
    }
}

AgentRunner *MainWindow::runnerForSession(int sessionId) const
{
    for (AgentRunner *runner : m_agentRunners)
        if (runner->busy() && runner->currentSessionId() == sessionId)
            return runner;
    return nullptr;
}

bool MainWindow::anyAgentRunning() const
{
    for (AgentRunner *runner : m_agentRunners)
        if (runner->busy())
            return true;
    return false;
}

AgentRunner *MainWindow::acquireAgentRunner()
{
    // Reuse an idle runner from the pool when possible.
    for (AgentRunner *runner : m_agentRunners)
        if (!runner->busy())
            return runner;
    // Otherwise grow the pool. Signals carry the session id, so handlers route
    // correctly no matter which runner emits.
    auto *runner = new AgentRunner(m_agentStore, this);
    connect(runner, &AgentRunner::logLine, this, &MainWindow::onAgentLog);
    connect(runner, &AgentRunner::statusChanged, this,
            &MainWindow::onAgentStatusChanged);
    connect(runner, &AgentRunner::finished, this, &MainWindow::onAgentFinished);
    connect(runner, &AgentRunner::needsAttention, this,
            &MainWindow::onAgentNeedsAttention);
    m_agentRunners.append(runner);
    return runner;
}

void MainWindow::processAgentQueue()
{
    if (!m_agentStore)
        return;
    // Start every queued session immediately in its own runner — no serial
    // queue. (Sessions already running stay put.)
    while (!m_agentQueue.isEmpty()) {
        const int sessionId = m_agentQueue.takeFirst();
        AgentSession *session = findAgentSession(sessionId);
        if (!session || session->status != AgentStatus::Queued)
            continue;
        if (runnerForSession(sessionId)) // already running somewhere
            continue;
        const int repoIndex = repoIndexFor(session->owner, session->name);
        if (repoIndex < 0) {
            session->status = AgentStatus::Failed;
            session->lastError = QStringLiteral("Repository not found.");
            m_agentStore->saveSession(*session);
            continue;
        }
        const RepositoryRecord repo = m_repositories.at(repoIndex);
        // The git dir agents run against: our working-tree checkout when we host
        // the repo, otherwise the bare network mirror so a node that only mirrors
        // it can still run agents (adhoc #191). Worktrees, diffs and PR patches
        // are all built off this; the agent never edits it in place.
        const QString agentGitDir = repoAgentGitDir(repo);
        if (agentGitDir.isEmpty()) {
            session->status = AgentStatus::Failed;
            session->lastError = QStringLiteral("No local checkout is configured.");
            m_agentStore->saveSession(*session);
            continue;
        }
        // Ad-hoc sessions (issueNumber == 0) carry no issue; their task lives in
        // session->prompt. Only issue-assigned sessions need the issue resolved —
        // requiring one for ad-hoc runs is what failed every resumed ad-hoc agent
        // on restart with "Issue not found".
        Issue issue;
        if (session->issueNumber > 0) {
            const QList<Issue> issues =
                IssueStore(repo.localPath, repo.mirrorPath, &m_profileIdentity, m_userName)
                    .loadAll();
            bool found = false;
            for (const Issue &candidate : issues)
                if (candidate.number == session->issueNumber) {
                    issue = candidate;
                    found = true;
                    break;
                }
            if (!found) {
                session->status = AgentStatus::Failed;
                session->lastError = QStringLiteral("Issue not found.");
                m_agentStore->saveSession(*session);
                continue;
            }
        }
        // Claude Code renders as a native stream-json transcript on the agent
        // detail screen (with a Raw-output toggle), not headlessly through a
        // runner. startClaudeCodeTerminal remains for the legacy embedded-TUI.
        if (session->provider == QLatin1String("claude-code")) {
            startClaudeCodeTranscript(*session, issue, agentGitDir, session->prompt);
            continue;
        }
        const AgentSession snapshot = *session;
        // A launched run consumes from this provider's rolling usage windows;
        // anchor them so the agent sessions screen can count down the time left.
        markAgentLimitWindow(snapshot.provider);
        AgentRunner::Config config = agentConfigForProvider(session->provider);
        // A model picked when the session was started (e.g. the issue sidebar's
        // agent/model dropdown) overrides the provider's default.
        if (!snapshot.model.isEmpty())
            config.model = snapshot.model;
        // Ad-hoc API-key runs ride their saved task through the config override,
        // mirroring startAdHocAgentForRepo so they resume the same way after a restart.
        if (snapshot.issueNumber == 0 && !snapshot.prompt.isEmpty())
            config.taskOverride = snapshot.prompt;
        // A message queued from the always-on composer while this session was
        // stopped/waiting (adhoc #177): hand it to the resumed run as a steer.
        if (const QString steer = m_pendingSteerMessage.take(sessionId); !steer.isEmpty()) {
            const QString base = config.taskOverride.isEmpty() ? snapshot.prompt
                                                               : config.taskOverride;
            config.taskOverride =
                base.isEmpty()
                    ? steer
                    : base + QStringLiteral("\n\nAdditional user instruction:\n%1").arg(steer);
        }
        acquireAgentRunner()->start(snapshot, issue, agentGitDir, config);
    }
    reloadAgents();
}

// Lazily create the IDE bridge that lets the `claude` CLI talk back to the app
// as if it were VS Code (issue #191). Parented to the window, so its destructor
// removes the lockfile on shutdown.
ClaudeIdeBridge *MainWindow::ensureIdeBridge()
{
    if (m_ideBridge)
        return m_ideBridge;
    m_ideBridge = new ClaudeIdeBridge(this);
    connect(m_ideBridge, &ClaudeIdeBridge::openDiffRequested, this,
            &MainWindow::onClaudeOpenDiff);
    connect(m_ideBridge, &ClaudeIdeBridge::openFileRequested, this,
            [this](const QString &path) {
                flashMessage(QStringLiteral("Claude Code opened %1")
                                 .arg(QFileInfo(path).fileName()));
            });
    connect(m_ideBridge, &ClaudeIdeBridge::clientConnected, this, [this] {
        flashMessage(QStringLiteral("Claude Code connected to the in-app IDE"));
    });
    connect(m_ideBridge, &ClaudeIdeBridge::log, this, [this](const QString &line) {
        if (m_terminalSessionId > 0 && m_agentStore) {
            if (AgentSession *s = findAgentSession(m_terminalSessionId))
                m_agentStore->appendLog(*s, line + QLatin1Char('\n'));
        }
    });
    return m_ideBridge;
}

// Show Claude's proposed change as a side-by-side diff and report the user's
// accept/reject decision back to the CLI. openDiff is a blocking tool: the CLI
// waits on this reply, so resolveDiff() must be called exactly once.
void MainWindow::onClaudeOpenDiff(const QString &tabName, const QString &oldPath,
                                  const QString &newPath, const QString &newContents)
{
    if (!m_ideBridge)
        return;
    QString oldContents;
    if (QFile f(oldPath); f.open(QIODevice::ReadOnly)) {
        oldContents = QString::fromUtf8(f.readAll());
        f.close();
    }

    QDialog dlg(this);
    dlg.setWindowTitle(tabName.isEmpty()
                           ? QStringLiteral("Claude Code: proposed change")
                           : tabName);
    dlg.resize(960, 640);
    auto *layout = new QVBoxLayout(&dlg);
    const QString shown = newPath.isEmpty() ? oldPath : newPath;
    layout->addWidget(new QLabel(
        QStringLiteral("Claude Code proposes changes to <b>%1</b>")
            .arg(shown.toHtmlEscaped()),
        &dlg));

    auto *split = new QSplitter(Qt::Horizontal, &dlg);
    auto makePane = [&](const QString &title, const QString &text) {
        auto *box = new QWidget(split);
        auto *v = new QVBoxLayout(box);
        v->setContentsMargins(0, 0, 0, 0);
        v->addWidget(new QLabel(title, box));
        auto *edit = new QPlainTextEdit(box);
        edit->setReadOnly(true);
        edit->setLineWrapMode(QPlainTextEdit::NoWrap);
        edit->setPlainText(text);
        edit->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
        v->addWidget(edit);
        return box;
    };
    split->addWidget(makePane(QStringLiteral("Current (on disk)"), oldContents));
    split->addWidget(makePane(QStringLiteral("Proposed by Claude"), newContents));
    layout->addWidget(split, 1);

    auto *buttons = new QDialogButtonBox(&dlg);
    buttons->addButton(QStringLiteral("Accept"), QDialogButtonBox::AcceptRole);
    buttons->addButton(QStringLiteral("Reject"), QDialogButtonBox::RejectRole);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

    const bool accepted = dlg.exec() == QDialog::Accepted;
    m_ideBridge->resolveDiff(tabName, accepted, newContents);
}

// Run Claude Code interactively in the built-in terminal on the agent detail
// screen, working in the repo checkout. The session already exists (created by
// assignIssueToAgent); here we just launch it and show the terminal.
void MainWindow::startClaudeCodeTerminal(AgentSession &session, const Issue &issue,
                                         const QString &repoPath)
{
    if (!m_agentTerminal || !m_agentStore)
        return;

    // Seed the agent with a prompt file pointing at the issue.
    const QString dir =
        QStandardPaths::writableLocation(QStandardPaths::TempLocation) +
        QStringLiteral("/forkmesh-agent");
    QDir().mkpath(dir);
    const QString promptFile =
        dir + QStringLiteral("/issue-%1.md").arg(session.issueNumber);
    if (QFile pf(promptFile); pf.open(QIODevice::WriteOnly)) {
        const QString prompt =
            QStringLiteral(
                "Resolve ForkMesh issue #%1: %2\n\n"
                "The full issue is in issues/%1/issue.md. Implement the change end "
                "to end, consistent with the surrounding code, then summarize what "
                "you changed and how to verify it.\n")
                .arg(session.issueNumber)
                .arg(issue.title);
        pf.write(prompt.toUtf8());
        pf.close();
    }

    QString cmd = QSettings()
                      .value(kClaudeCodeTerminalCommandSetting,
                             kDefaultClaudeCodeTerminalCommand)
                      .toString()
                      .trimmed();
    if (cmd.isEmpty() || cmd == kLegacyClaudeCodeTerminalCommand) {
        cmd = kDefaultClaudeCodeTerminalCommand;
        QSettings().setValue(kClaudeCodeTerminalCommandSetting, cmd);
    }
    cmd.replace(QStringLiteral("{promptFile}"), promptFile);
    cmd.replace(QStringLiteral("{issueNumber}"),
                QString::number(session.issueNumber));

    QStringList env;
    // The built-in Claude Code terminal authenticates with the user's claude.ai
    // login, so we deliberately do NOT inject the configured ANTHROPIC_API_KEY
    // here (that key is for the script-based AgentRunner path, which needs raw
    // API access). Injecting it makes Claude Code warn "Both claude.ai and
    // ANTHROPIC_API_KEY set" and silently switch to API-usage billing. Drop any
    // ANTHROPIC_API_KEY inherited from the shell too; an entry without '=' tells
    // the terminal to unset the variable in the child.
    env << QStringLiteral("ANTHROPIC_API_KEY");

    // Make ForkMesh act as the IDE this CLI connects to (issue #191): start the
    // localhost bridge for this checkout and inject the discovery env vars so
    // `claude` auto-connects (in-app diffs, selection, open-file context). The
    // user can also trigger it from the CLI with /ide.
    if (ClaudeIdeBridge *bridge = ensureIdeBridge()) {
        if (bridge->start(repoPath))
            env << bridge->env();
    }

    session.status = AgentStatus::Running;
    session.startedAtMs = QDateTime::currentMSecsSinceEpoch();
    m_agentStore->saveSession(session);
    m_agentStore->appendLog(
        session,
        QStringLiteral("\n==> Running Claude Code in the built-in terminal:\n%1\n")
            .arg(cmd));

    m_terminalSessionId = session.id;
    switchToAgentsTab(session.id);
    showAgentSession(session.id);
    reloadAgents();
    if (m_agentOutputStack)
        m_agentOutputStack->setCurrentWidget(m_agentTerminal);
    m_agentTerminal->runCommand(cmd, repoPath, env);
}

// Run Claude Code in stream-json mode and render its events as a native,
// extension-style transcript (issue #191 follow-up). Same auth model as the
// terminal path (claude.ai login, no ANTHROPIC_API_KEY) and same IDE bridge, but
// the output is parsed cards instead of a raw TUI. Each session keeps its own
// stream + event buffer so output never leaks across sessions; the raw stream
// stays reachable via the "Raw output" toggle, and a PR is opened on finish.
// ---- Auto model mode (adhoc #91) -------------------------------------------

// One routing decision as a transcript event. Rendered as a muted notice row by
// ClaudeTranscriptView and persisted with the other stream events, so the
// "why this model" trail survives restarts; when `model` is non-empty the event
// also records the routed pick for resumes to reuse.
static QJsonObject autoModelNotice(const QString &text,
                                   const QString &model = QString())
{
    QJsonObject ev{{QStringLiteral("type"), QStringLiteral("_local_notice")},
                   {QStringLiteral("text"), text}};
    if (!model.isEmpty())
        ev.insert(QStringLiteral("model"), model);
    return ev;
}

void MainWindow::resolveAutoClaudeModel(int sessionId, const QString &task,
                                        const QString &workdir,
                                        ClaudeStreamSession *live,
                                        std::function<void(const QString &)> launch)
{
    // A continued session sticks with the model that already holds the
    // conversation context; the routed pick was recorded on its notice event.
    const QList<QJsonObject> events = m_streamEvents.value(sessionId);
    for (int i = events.size() - 1; i >= 0; --i) {
        const QJsonObject &ev = events.at(i);
        if (ev.value(QStringLiteral("type")).toString()
            != QLatin1String("_local_notice"))
            continue;
        const QString prior = ev.value(QStringLiteral("model")).toString();
        if (prior.isEmpty())
            continue;
        applyTranscriptEvent(
            sessionId,
            autoModelNotice(QStringLiteral("Auto model: continuing on %1 — chosen "
                                           "earlier in this session.")
                                .arg(agentModelLabel(prior)),
                            prior));
        launch(prior);
        return;
    }
    // Pre-model pass: the local heuristic router decides the obvious cases for
    // free, with no LLM call at all.
    const ClaudeAutoRoute quick = claudeAutoHeuristicRoute(task);
    if (!quick.model.isEmpty()) {
        applyTranscriptEvent(
            sessionId,
            autoModelNotice(QStringLiteral("Auto model: heuristic router chose %1 "
                                           "because %2.")
                                .arg(agentModelLabel(quick.model), quick.reason),
                            quick.model));
        launch(quick.model);
        return;
    }
    applyTranscriptEvent(
        sessionId,
        autoModelNotice(QStringLiteral(
            "Auto model: no heuristic match — asking Haiku 4.5 whether it can "
            "handle this task.")));
    runClaudeAutoTriageRung(sessionId, 0, /*errorsOnly=*/true, task, workdir,
                            live, std::move(launch));
}

void MainWindow::runClaudeAutoTriageRung(int sessionId, int rung, bool errorsOnly,
                                         const QString &task,
                                         const QString &workdir,
                                         ClaudeStreamSession *live,
                                         std::function<void(const QString &)> launch)
{
    const QList<ClaudeAutoRung> &ladder = claudeAutoLadder();
    if (rung >= ladder.size() - 1) {
        // Top of the ladder. Reached through honest escalations => run the most
        // powerful model. Reached purely through triage failures => the router
        // itself is broken (CLI/auth trouble the main run may still survive),
        // so don't bill the most expensive model for an infra problem — fall
        // back to Opus, the everyday default.
        const ClaudeAutoRung &top = ladder.last();
        const ClaudeAutoRung &opus = ladder.at(ladder.size() - 2);
        const QString pick = errorsOnly ? opus.id : top.id;
        applyTranscriptEvent(
            sessionId,
            autoModelNotice(
                errorsOnly
                    ? QStringLiteral("Auto model: triage unavailable — defaulting "
                                     "to %1.")
                          .arg(opus.label)
                    : QStringLiteral("Auto model: every lighter model passed — "
                                     "running %1, the most powerful model.")
                          .arg(top.label),
                pick));
        launch(pick);
        return;
    }
    const ClaudeAutoRung r = ladder.at(rung);
    // One-shot triage: ask the rung's model whether it is confident it can do
    // the task itself — or to name the right model outright if it already
    // knows. --max-turns 1 keeps it a single, tool-free reply.
    const QString triage =
        QStringLiteral(
            "You are the Claude model \"%1\". ForkMesh is choosing which model "
            "should run a coding agent for the task below. Assess honestly "
            "whether YOU could complete it end to end with high confidence.\n"
            "Reply with ONLY one line of JSON, no other text and no tool use:\n"
            "{\"decision\":\"handle|escalate|pick\","
            "\"model\":\"haiku|sonnet|opus|fable\",\"confidence\":0.0,"
            "\"reason\":\"one short sentence\"}\n"
            "- handle: you are confident you can do it yourself (model = your "
            "own alias).\n"
            "- pick: you are confident a specific model is the right fit "
            "(model = its alias).\n"
            "- escalate: you are not confident; the next stronger model will "
            "re-assess.\n\nTask:\n%2")
            .arg(r.label, task.left(6000));
    // Parented to the stream session so stopping the agent kills the triage too.
    auto *proc = new QProcess(live);
    proc->setWorkingDirectory(workdir);
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.remove(QStringLiteral("ANTHROPIC_API_KEY")); // same auth as the agent run
    proc->setProcessEnvironment(env);
    // Don't let a hung triage stall the agent launch forever.
    QTimer::singleShot(45000, proc, [proc] { proc->kill(); });
    connect(
        proc, &QProcess::finished, this,
        [this, sessionId, rung, errorsOnly, task, workdir, live, launch, proc,
         r](int exitCode, QProcess::ExitStatus) mutable {
            const QByteArray out = proc->readAllStandardOutput();
            proc->deleteLater();
            if (m_streamSessions.value(sessionId) != live)
                return; // session stopped or replaced while the triage ran
            const QList<ClaudeAutoRung> &ladder = claudeAutoLadder();
            const ClaudeAutoRung &next = ladder.at(rung + 1);
            const int open = out.indexOf('{');
            const int close = out.lastIndexOf('}');
            QJsonObject verdict;
            if (exitCode == 0 && open >= 0 && close > open)
                verdict = QJsonDocument::fromJson(out.mid(open, close - open + 1))
                              .object();
            const QString decision =
                verdict.value(QStringLiteral("decision")).toString();
            const QString alias = verdict.value(QStringLiteral("model"))
                                      .toString()
                                      .trimmed()
                                      .toLower();
            const double confidence =
                verdict.value(QStringLiteral("confidence")).toDouble();
            QString reason =
                verdict.value(QStringLiteral("reason")).toString().trimmed();
            if (reason.isEmpty())
                reason = QStringLiteral("no reason given");
            QString aliasId, aliasLabel;
            for (const ClaudeAutoRung &c : ladder)
                if (c.alias == alias) {
                    aliasId = c.id;
                    aliasLabel = c.label;
                    break;
                }
            if (decision == QLatin1String("handle") && confidence >= 0.6) {
                applyTranscriptEvent(
                    sessionId,
                    autoModelNotice(QStringLiteral("Auto model: %1 is confident it "
                                                   "can handle this task (%2) — "
                                                   "running %1.")
                                        .arg(r.label, reason),
                                    r.id));
                launch(r.id);
                return;
            }
            if (decision == QLatin1String("pick") && !aliasId.isEmpty()) {
                applyTranscriptEvent(
                    sessionId,
                    autoModelNotice(QStringLiteral("Auto model: %1 picked %2 for "
                                                   "this task (%3).")
                                        .arg(r.label, aliasLabel, reason),
                                    aliasId));
                launch(aliasId);
                return;
            }
            if (decision.isEmpty()) {
                // CLI failure or unparseable reply — an infra problem, not a
                // difficulty verdict; keep errorsOnly as-is so an all-failure
                // climb ends on the safe default instead of the priciest model.
                applyTranscriptEvent(
                    sessionId,
                    autoModelNotice(QStringLiteral("Auto model: triage on %1 "
                                                   "failed (exit %2) — trying %3.")
                                        .arg(r.label)
                                        .arg(exitCode)
                                        .arg(next.label)));
                runClaudeAutoTriageRung(sessionId, rung + 1, errorsOnly, task,
                                        workdir, live, std::move(launch));
                return;
            }
            applyTranscriptEvent(
                sessionId,
                autoModelNotice(QStringLiteral("Auto model: %1 wasn't confident "
                                               "(%2) — escalating to %3.")
                                    .arg(r.label, reason, next.label)));
            runClaudeAutoTriageRung(sessionId, rung + 1, /*errorsOnly=*/false,
                                    task, workdir, live, std::move(launch));
        });
    // Through a login shell so the user's PATH resolves `claude` exactly like
    // the real agent session (ClaudeStreamSession) — the GUI process itself
    // often lacks ~/.local/bin. The triage prompt goes in on stdin, so nothing
    // user-controlled needs shell quoting; the ladder id is a fixed [a-z0-9-]
    // string, single-quoted defensively all the same.
    proc->start(QStringLiteral("bash"),
                {QStringLiteral("-lc"),
                 QStringLiteral("exec claude -p --model '%1' --max-turns 1")
                     .arg(r.id)});
    proc->write(triage.toUtf8());
    proc->closeWriteChannel();
}

// Title + full description + every comment, formatted as a self-contained
// block for an agent prompt (adhoc #256). The description lives in the
// issue's "open" event body (issue.md's body text); comments are every
// subsequent "comment" event, oldest first, matching the thread as read in
// the app.
QString MainWindow::issueContextPrompt(const Issue &issue) const
{
    QString description;
    for (const IssueEvent &ev : issue.events) {
        if (ev.type == QLatin1String("open")) {
            description = ev.body.trimmed();
            break;
        }
    }
    QString commentThread;
    for (const IssueEvent &ev : issue.events) {
        if (ev.type != QLatin1String("comment"))
            continue;
        const QString text = ev.body.trimmed();
        if (text.isEmpty())
            continue;
        const QString who = ev.authorName.isEmpty() ? ev.author : ev.authorName;
        commentThread += QStringLiteral("\n\n--- comment by %1 ---\n%2")
                             .arg(who, text.left(6000));
    }
    QString out = QStringLiteral("Issue #%1: %2\n")
                      .arg(issue.number)
                      .arg(issue.title);
    out += description.isEmpty()
               ? QStringLiteral("\n(No description was given.)\n")
               : QStringLiteral("\n%1\n").arg(description.left(20000));
    if (!commentThread.isEmpty())
        out += QStringLiteral("\nComments on the issue (newest last):%1\n")
                   .arg(commentThread);
    return out;
}

void MainWindow::startClaudeCodeTranscript(AgentSession &session, const Issue &issue,
                                           const QString &repoPath,
                                           const QString &customPrompt)
{
    if (!m_agentTranscript || !m_agentStore)
        return;
    const int sid = session.id;

    auto gitOut = [](const QString &dir, const QStringList &args) -> QString {
        QProcess git;
        git.setWorkingDirectory(dir);
        git.start(QStringLiteral("git"), args);
        if (git.waitForFinished(8000) && git.exitCode() == 0)
            return QString::fromUtf8(git.readAllStandardOutput()).trimmed();
        return QString();
    };
    // Capture the base commit/branch so we can diff this session into a PR.
    if (session.baseRef.isEmpty()) {
        session.baseRef = gitOut(repoPath, {QStringLiteral("rev-parse"), QStringLiteral("HEAD")});
        const QString b = gitOut(repoPath, {QStringLiteral("rev-parse"),
                                            QStringLiteral("--abbrev-ref"), QStringLiteral("HEAD")});
        if (!b.isEmpty() && b != QLatin1String("HEAD"))
            session.baseBranch = b;
    }
    if (session.baseBranch.isEmpty())
        session.baseBranch = session.baseRef;
    session.createPr = true; // always open a PR for a finished transcript session
    // Persist an ad-hoc task so it can be replayed after an app restart (the
    // composer's free-form prompt has no issue to re-read it from).
    if (session.prompt.isEmpty() && !customPrompt.trimmed().isEmpty())
        session.prompt = customPrompt.trimmed();

    const QString baseName =
        session.baseBranch.isEmpty() ? QStringLiteral("main") : session.baseBranch;
    // Ad-hoc sessions (issue #273) carry the user's task verbatim as the lead;
    // issue-assigned sessions get the full title + description + comment thread
    // embedded directly (adhoc #256) — pointing only at issues/<n>/issue.md left
    // the agent to go dig it up itself, and it sometimes never did. Both share
    // the same worktree/commit/PR workflow tail so the run lands as a pull request.
    const QString lead =
        customPrompt.trimmed().isEmpty()
            ? QStringLiteral(
                  "Resolve the following ForkMesh issue end to end.\n\n"
                  "%1\n"
                  "You are working in a dedicated git worktree on branch `%2` "
                  "(forked from `%3`). The description and comments above are "
                  "the full issue context; issues/%4/issue.md holds the same "
                  "description verbatim if you need to reference the raw file.\n")
                  .arg(issueContextPrompt(issue), session.branchName, baseName,
                       QString::number(session.issueNumber))
            : QStringLiteral(
                  "%1\n\n"
                  "You are working in a dedicated git worktree on branch `%2` "
                  "(forked from `%3`).")
                  .arg(customPrompt.trimmed())
                  .arg(session.branchName)
                  .arg(baseName);
    const QString body =
        QStringLiteral(
            "%1 Work end to end:\n"
            "1. Implement the change, consistent with the surrounding code.\n"
            "2. If `%2` has advanced, rebase or merge it into your branch and "
            "resolve any conflicts.\n"
            "3. Run the project's tests and linting, and fix any failures.\n"
            "4. Commit everything to `%3` with a clear message.\n"
            "ForkMesh will open the pull request from your branch. Finally, "
            "summarize what you changed and how to verify it.\n")
            .arg(lead)
            .arg(baseName)
            .arg(session.branchName);
    // Honor the user-editable instruction preamble from Settings → Agents, and let
    // it GOVERN the run when set. `body` above bundles the task/branch context
    // (`lead`) with a built-in "Work end to end" workflow; if we also prepended a
    // custom preamble the agent would receive two competing instruction sets
    // ("its sending both prompts — just send the one we have in settings"). So
    // when a custom preamble is configured we send it plus only the task/branch
    // context and drop the built-in workflow. A blank setting falls back to the
    // built-in default preamble followed by the full workflow body, as before.
    const QString customPreamble =
        QSettings().value(kAgentPromptPreambleSetting).toString().trimmed();
    QString prompt =
        customPreamble.isEmpty()
            ? AgentRunner::defaultPromptPreamble() + QStringLiteral("\n\n") + body
            : customPreamble + QStringLiteral("\n\n") + lead;

    // Per-session buffers; tear down any prior stream for THIS session only. The
    // stream object and the UI hand-off below are set up *before* the worktree is
    // created so assigning an agent feels instant — the slow checkout then runs
    // asynchronously and the CLI starts from its continuation (issue #262).
    //
    // Resuming a session that already streamed a transcript — an app-restart resume
    // of a still-Running agent (issue #242), or a user-driven Continue/Revision —
    // must KEEP that transcript: wiping events.jsonl here is what made a
    // recently-started agent look like it lost its history after a restart. Load any
    // persisted events back into memory and only start from a clean slate for a
    // genuinely fresh run (a brand-new ad-hoc or issue assignment has none). The
    // resumed CLI emits its own "session started" event, marking the boundary.
    ensureStreamEventsLoaded(sid);
    const bool resuming = !m_streamEvents.value(sid).isEmpty();
    if (!resuming) {
        m_streamEvents[sid].clear();
        m_streamRaw[sid].clear();
        m_streamFiles[sid].clear();
        m_agentStore->clearEvents(session);
    }
    // Pick up a stopped agent with its real conversation context: resume the
    // Claude session by id rather than relaunching a fresh process that just
    // replays the task prompt (adhoc #182). The queued composer message — which
    // the Send handler already recorded as a user turn — becomes the next turn;
    // a bare Continue with no message nudges the agent onward. Falls back to the
    // full-prompt replay when there's no recoverable session id (e.g. a legacy
    // transcript or a fresh run), so those still resume the way they used to.
    const QString steer = m_pendingSteerMessage.take(sid);
    const QString resumeId = resuming ? lastClaudeSessionId(sid) : QString();
    if (!resumeId.isEmpty()) {
        if (steer.isEmpty()) {
            prompt = QStringLiteral("Continue where you left off.");
            applyTranscriptEvent(
                sid, QJsonObject{{QStringLiteral("type"), QStringLiteral("_local_user")},
                                 {QStringLiteral("text"), prompt}});
        } else {
            prompt = steer; // already shown in the transcript by the composer
        }
    } else if (!steer.isEmpty()) {
        // No context to resume — fold the steer into the replayed prompt as before
        // (this is the original always-on-composer restart behaviour, adhoc #177).
        prompt += QStringLiteral("\n\nAdditional user instruction:\n%1\n").arg(steer);
    }
    // This session's transcript changed; force the next show to rebuild it.
    if (m_renderedTranscriptSession == sid)
        m_renderedTranscriptSession = -1;
    // Capture the session so applyTranscriptEvent can persist each turn to disk
    // (issue #41) — m_agentSessions doesn't yet hold a freshly created ad-hoc
    // session.
    m_streamSessionInfo[sid] = session;
    if (ClaudeStreamSession *old = m_streamSessions.take(sid))
        old->deleteLater();
    auto *stream = new ClaudeStreamSession(this);
    m_streamSessions.insert(sid, stream);
    // Record the initial user turn so it replays when switching back to this view.
    // On a resume the preserved transcript already holds the original prompt, so
    // only a fresh run logs it here.
    if (!resuming)
        applyTranscriptEvent(sid, QJsonObject{
                                      {QStringLiteral("type"), QStringLiteral("_local_user")},
                                      {QStringLiteral("text"), prompt}});
    connect(stream, &ClaudeStreamSession::event, this,
            [this, sid](const QJsonObject &ev) { applyTranscriptEvent(sid, ev); });
    connect(stream, &ClaudeStreamSession::rawLine, this, [this, sid](const QString &line) {
        noteAgentActivity(sid, line.size()); // pulse the list's night-rider light
        QString &buf = m_streamRaw[sid];
        // Separate each JSON object with a blank line so the raw view is readable.
        buf += line + QStringLiteral("\n\n");
        if (buf.size() > 400000)
            buf = buf.right(300000);
        if (sid == m_selectedAgentSessionId)
            appendAgentRawLog(line + QStringLiteral("\n\n"));
    });
    connect(stream, &ClaudeStreamSession::finished, this, [this, sid](int) {
        // The CLI process is meant to stay alive across turns — a genuinely
        // finished turn is what the `result` event handler above marks Success
        // (or Failed on an error result). Landing here with the session still
        // Running/Waiting means the process died without ever sending one (a
        // crash, or an app-restart resume whose `--resume` id no longer lined
        // up), not that the task completed. Stamping Success on that was
        // reported as "an agent I restarted mid-task shows as done" — re-queue
        // it instead so it stays active and gets another resume attempt,
        // mirroring initAgents()'s restart recovery (issue #242) rather than
        // abandoning it with a false result.
        if (AgentSession *as = findAgentSession(sid)) {
            if (as->status == AgentStatus::Running ||
                as->status == AgentStatus::Waiting) {
                as->status = AgentStatus::Queued;
                as->lastError.clear();
                m_agentStore->saveSession(*as);
                scheduleAgentSessionsPush(); // adhoc #182
                if (!m_agentQueue.contains(sid))
                    m_agentQueue.append(sid);
            }
        }
        maybeCreatePullForStreamSession(sid);
        // The PR captured the diff as a patch, so the worktree is no longer
        // needed; drop it to free the branch for checkout (issue #74).
        cleanupStreamWorktree(sid);
        if (ClaudeStreamSession *done = m_streamSessions.take(sid))
            done->deleteLater();
        reloadAgents();
        if (sid == m_selectedAgentSessionId)
            showAgentSession(sid);
        looperOnSessionFinished(sid); // adhoc #92: chain to the next open issue
        processAgentQueue(); // pick the re-queued session back up
    });

    // Snapshot the fields the async continuation needs *before* reloadAgents()
    // below rebuilds m_agentSessions and leaves the `session` reference dangling.
    const QString branchName = session.branchName;
    const QString baseRef = session.baseRef;
    const int issueNumber = session.issueNumber;
    const QString model = session.model;

    session.status = AgentStatus::Running;
    session.startedAtMs = QDateTime::currentMSecsSinceEpoch();
    m_agentStore->saveSession(session);
    m_agentStore->appendLog(
        session,
        QString::fromUtf8("\n==> Preparing an isolated worktree for branch %1\xE2\x80\xA6\n")
            .arg(branchName));

    // Jump the UI to the new session's transcript — but only for a user-driven
    // start. A restart resume (m_agentQuietResume, see runDeferredStartup) must
    // not yank the user off the restored view, and the jump's openRepoDetail()
    // is exactly the cold ~2s git load the deferred resume exists to avoid.
    // Skip the jump when the user is already viewing this session (e.g. they
    // pressed Enter in the composer on the Agents tab): switchToAgentsTab fires
    // extra reloadAgents() calls that can reset the table selection to row 0 and
    // navigate away from the session the user was working with.
    if (!m_agentQuietResume) {
        m_terminalSessionId = sid;
        if (m_selectedAgentSessionId != sid)
            switchToAgentsTab(sid);
        showAgentSession(sid); // renders the buffered turn + selects the surface
        if (m_transcriptModeButton)
            m_transcriptModeButton->setChecked(true);
        if (m_terminalModeButton)
            m_terminalModeButton->setChecked(false);
    }
    reloadAgents();

    // Start the CLI once the working directory is ready. Pulled into a lambda
    // because the worktree checkout below finishes asynchronously; the IDE bridge
    // and env are set up here since they depend on the final workdir.
    const bool autoMode = QSettings().value(kClaudeAutoModeSetting, true).toBool();
    // Which model the CLI runs as. A model set on the session itself (e.g. the
    // agent/model dropdown a caller picked before starting this run) wins;
    // otherwise fall back to the footer quick-add bar's persisted choice (adhoc
    // #261). Empty leaves the CLI on its own default; otherwise it's passed
    // through as `--model`.
    const QString claudeModel =
        !model.isEmpty() ? model
                         : QSettings().value(kClaudeCodeModelSetting).toString().trimmed();
    // Auto mode (adhoc #91) routes on the task itself, not the full workflow
    // prompt — `lead` carries the user's ask (or the issue + its comments).
    const QString routeTask = lead;
    auto launch = [this, sid, prompt, autoMode, branchName, resumeId,
                   claudeModel, routeTask](const QString &workdir) {
        ClaudeStreamSession *live = m_streamSessions.value(sid);
        if (!live)
            return; // session was stopped or deleted while the worktree was building
        QStringList env;
        env << QStringLiteral("ANTHROPIC_API_KEY"); // see startClaudeCodeTerminal
        if (ClaudeIdeBridge *bridge = ensureIdeBridge()) {
            if (bridge->start(workdir))
                env << bridge->env();
        }
        if (AgentSession *as = findAgentSession(sid))
            m_agentStore->appendLog(
                *as, QStringLiteral("\n==> Running Claude Code (stream-json transcript) "
                                    "on branch %1 in %2\n")
                         .arg(branchName, workdir));
        // Start the CLI once the model is concrete. The "auto" sentinel first
        // runs the router (adhoc #91), which is asynchronous — so `begin`
        // re-checks that this stream is still the session's live one (the user
        // may have stopped or restarted it while the triage ran).
        auto begin = [this, sid, live, workdir, env, prompt, autoMode,
                      resumeId](const QString &chosenModel) {
            if (m_streamSessions.value(sid) != live)
                return;
            // Footer slash-actions menu (adhoc #116): effort and model-fallback
            // ride into the CLI as --effort/--fallback-model; turning Thinking
            // off zeroes the thinking budget via MAX_THINKING_TOKENS.
            const QString effort =
                QSettings().value(kClaudeEffortSetting, QStringLiteral("high"))
                    .toString();
            const QString fallback =
                QSettings().value(kClaudeFallbackModelSetting, false).toBool()
                    ? QStringLiteral("opus,sonnet")
                    : QString();
            QStringList launchEnv = env;
            if (!QSettings().value(kClaudeThinkingSetting, true).toBool())
                launchEnv << QStringLiteral("MAX_THINKING_TOKENS=0");
            live->start(workdir, launchEnv, prompt, /*skipPermissions=*/autoMode,
                        resumeId, chosenModel, effort, fallback);
            // Issue #84: launching with an initial prompt is a send too — refresh
            // the top-bar usage chart + hover stats. Bump (now + a short
            // follow-up) so the first turn's usage shows without waiting for the
            // next minute tick.
            bumpClaudeCodeUsage();
        };
        if (claudeModel == kClaudeAutoModelId)
            resolveAutoClaudeModel(sid, routeTask, workdir, live, std::move(begin));
        else
            begin(claudeModel);
    };

    // Give the agent its own worktree + branch so concurrent agents never share a
    // working tree. The checkout can take a second or two on a large repo, so run
    // it asynchronously and start the CLI from the continuation; fall back to the
    // live checkout if there's no base commit or the worktree can't be made.
    if (baseRef.isEmpty()) {
        launch(repoPath);
        return;
    }
    const QString wtRoot =
        QStandardPaths::writableLocation(QStandardPaths::TempLocation)
        + QStringLiteral("/forkmesh-worktrees");
    QDir().mkpath(wtRoot);
    const QString wtPath =
        wtRoot + QStringLiteral("/issue-%1-s%2").arg(issueNumber).arg(sid);
    // Recreate the agent's worktree, robustly. A finished session tears its worktree
    // down asynchronously (cleanupStreamWorktree spawns a detached thread); when the
    // user continues that session the teardown can still be in flight, so prune AND
    // force-remove any lingering registration at this path *in the same chained
    // command* before re-adding. Otherwise `git worktree add` races the teardown and
    // fails ("branch already used by worktree"), we silently fall back to the main
    // checkout, and `claude --resume` — looking for a session recorded under the
    // worktree path — bails out instantly with a red "0 turns" error instead of
    // continuing the agent.
    //
    // On a resume the branch already holds the agent's committed work, so check it
    // out as-is to continue from where it left off; only a fresh run (no resume id)
    // creates the branch from baseRef. Branch names and the temp path are sanitised
    // to [a-z0-9/-] / a fixed tmp layout, so single-quoting is safe.
    const QString addStep =
        resumeId.isEmpty()
            ? QStringLiteral("git worktree add -B '%1' '%2' '%3'")
                  .arg(branchName, wtPath, baseRef)
            : QStringLiteral("git worktree add '%1' '%2'").arg(wtPath, branchName);
    const QString script =
        QStringLiteral("git worktree prune; git worktree remove --force '%1' 2>/dev/null; %2")
            .arg(wtPath, addStep);
    auto *add = new QProcess(this);
    add->setWorkingDirectory(repoPath);
    connect(add, &QProcess::finished, this,
            [this, sid, add, wtPath, repoPath, launch](int, QProcess::ExitStatus) {
                add->deleteLater();
                QString workdir = repoPath;
                if (QDir(wtPath).exists()) {
                    workdir = wtPath;
                    m_streamWorktree[sid] = wtPath;
                }
                launch(workdir);
            });
    add->start(QStringLiteral("bash"), {QStringLiteral("-lc"), script});
}

// Working directory for a session: its worktree if it has one, else the repo.
QString MainWindow::sessionWorkdir(int sessionId)
{
    if (m_streamWorktree.contains(sessionId))
        return m_streamWorktree.value(sessionId);
    if (const AgentSession *s = findAgentSession(sessionId)) {
        const int ri = repoIndexFor(s->owner, s->name);
        if (ri >= 0) {
            const QString repoLocal = m_repositories.at(ri).localPath;
            // The in-memory m_streamWorktree map only knows sessions launched in
            // *this* run. For a reloaded session (e.g. after restart, or one that
            // finished earlier) resolve its worktree from the branch, so the
            // Files-changed diff runs in the session's own tree rather than the
            // main checkout — otherwise the tab shows the wrong files. Cached so
            // this stays subprocess-free on the hot path.
            const QString wt =
                cachedSessionWorktree(sessionId, repoLocal, s->branchName);
            if (!wt.isEmpty() && QDir(wt).exists())
                return wt;
            return repoLocal;
        }
    }
    return QString();
}

// Resolve a session's dedicated worktree path ("" when its branch has no separate
// worktree / is the main checkout) without re-shelling `git worktree list` every
// time. worktreePathForBranch() spawns a synchronous git subprocess, and the
// click path resolved it twice per open — once for the header's worktree link and
// once to gate the Files-changed tab — plus refreshAgentFilesPanel() drove it on
// every transcript turn. Two subprocesses on the GUI thread is the ~0.5s stall
// opening a session showed (adhoc #78). Sessions launched this run already know
// their worktree from m_streamWorktree (no git at all); reloaded ones resolve it
// once and cache it — a branch->worktree binding is fixed for the session's
// lifetime — re-resolving only if a found path was since removed (adhoc #247).
QString MainWindow::cachedSessionWorktree(int sessionId, const QString &repoLocal,
                                          const QString &branch)
{
    if (repoLocal.isEmpty() || branch.trimmed().isEmpty())
        return QString();
    // This-run sessions: m_streamWorktree only ever holds a genuine /tmp worktree
    // (set only when the `git worktree add` produced one), so it's equivalent to
    // worktreePathForBranch() here but free.
    auto live = m_streamWorktree.constFind(sessionId);
    if (live != m_streamWorktree.constEnd())
        return *live;
    auto cached = m_sessionWorkdirCache.constFind(sessionId);
    if (cached != m_sessionWorkdirCache.constEnd()
        && (cached->isEmpty() || QDir(*cached).exists()))
        return *cached;
    const QString wt = worktreePathForBranch(repoLocal, branch);
    m_sessionWorkdirCache.insert(sessionId, wt);
    return wt;
}

bool MainWindow::isStreamTranscriptSession(int sessionId) const
{
    return m_streamSessions.contains(sessionId) || m_streamEvents.contains(sessionId);
}

// Stop the Claude Code CLI for a session and transition it to Stopped. The
// natural-finish path runs in ClaudeStreamSession::finished, but stop() kills
// the process without emitting `finished`, so the session would otherwise hang
// on "Running" with a dangling map entry. Do that teardown here instead.
void MainWindow::stopStreamSession(int sessionId, bool refreshUi)
{
    ClaudeStreamSession *stream = m_streamSessions.take(sessionId);
    if (!stream)
        return;
    stream->stop();
    stream->deleteLater();

    QString &raw = m_streamRaw[sessionId];
    raw += QStringLiteral("\n==> Stop requested by user.\n");
    if (sessionId == m_selectedAgentSessionId)
        appendAgentRawLog(QStringLiteral("\n==> Stop requested by user.\n"));

    if (AgentSession *as = findAgentSession(sessionId);
        as && as->status == AgentStatus::Running) {
        as->status = AgentStatus::Stopped;
        as->finishedAtMs = QDateTime::currentMSecsSinceEpoch();
        if (m_agentStore)
            m_agentStore->saveSession(*as);
        scheduleAgentSessionsPush(); // adhoc #182
    }

    // A delete path passes refreshUi=false: it removes the session next and
    // reloads itself, so re-rendering the (possibly large) transcript and
    // re-running the per-session merge checks here is wasted work that stalls
    // the UI during "Delete all".
    if (refreshUi) {
        reloadAgents();
        if (sessionId == m_selectedAgentSessionId)
            showAgentSession(sessionId);
        updateAgentActionState();
    }
}

// ---- External Claude Code sessions ----------------------------------------
// Watch-only mirrors of `claude` runs started outside ForkMesh. See the header.

// Directories ForkMesh's own stream sessions are driving, so the scanner doesn't
// report them back to us as "external".
QSet<QString> MainWindow::ownStreamCwds() const
{
    QSet<QString> out;
    for (const QString &p : m_streamWorktree)
        out.insert(QDir(p).absolutePath());
    return out;
}

// Stable synthetic id for a uuid (so a session keeps its row across rescans).
int MainWindow::externalTempIdFor(const QString &uuid)
{
    auto it = m_externalTempId.constFind(uuid);
    if (it != m_externalTempId.constEnd())
        return it.value();
    const int id = m_nextExternalTempId--; // -1000, -1001, … (all <= kExternalIdBase)
    m_externalTempId.insert(uuid, id);
    return id;
}

// Re-detect the external sessions for the open repo and refresh the metadata of
// any we've already surfaced.
void MainWindow::scanExternalClaudeSessions()
{
    m_externalClaude.clear();
    if (m_repoDetailIndex < 0 || m_repoDetailIndex >= m_repositories.size())
        return;
    const RepositoryRecord &repo = m_repositories.at(m_repoDetailIndex);
    if (repo.localPath.isEmpty())
        return;
    constexpr qint64 kActiveWindowMs = 90 * 1000; // written within 90s == "running"
    m_externalClaude =
        ClaudeSessionScan::scan(repo.localPath, ownStreamCwds(), kActiveWindowMs);
    for (const ExternalClaudeSession &ext : std::as_const(m_externalClaude)) {
        const int id = m_externalTempId.value(ext.uuid, 0);
        if (id != 0 && m_externalSurfaced.contains(id)) {
            ExternalClaudeSession &saved = m_externalSurfaced[id];
            saved.lastActivityMs = ext.lastActivityMs;
            saved.path = ext.path;
            if (!ext.title.isEmpty())
                saved.title = ext.title;
        }
    }
}

bool MainWindow::externalIsLive(const QString &uuid) const
{
    // A session we stopped stays idle even while its just-written transcript is
    // still inside the "active" window, so its row flips out of Running at once.
    if (m_externalStopped.contains(uuid))
        return false;
    for (const ExternalClaudeSession &e : m_externalClaude)
        if (e.uuid == uuid)
            return true;
    return false;
}

// Append the surfaced external sessions to m_agentSessions as read-only rows
// (negative ids, never persisted). Idempotent: strips any it added before.
void MainWindow::injectExternalSessions()
{
    m_agentSessions.erase(std::remove_if(m_agentSessions.begin(), m_agentSessions.end(),
                                         [](const AgentSession &s) {
                                             return s.id <= kExternalIdBase;
                                         }),
                          m_agentSessions.end());
    for (auto it = m_externalSurfaced.constBegin(); it != m_externalSurfaced.constEnd();
         ++it) {
        const int id = it.key();
        const ExternalClaudeSession &ext = it.value();
        const QString repoKey = m_externalSurfacedRepo.value(id);
        const int slash = repoKey.indexOf(QLatin1Char('/'));
        AgentSession s;
        s.id = id;
        s.owner = repoKey.left(slash);
        s.name = repoKey.mid(slash + 1);
        s.provider = QStringLiteral("claude-code");
        s.issueNumber = 0;
        s.issueTitle = ext.title.isEmpty() ? QStringLiteral("Claude Code (external)")
                                           : ext.title;
        s.branchName = ext.gitBranch;
        s.status = externalIsLive(ext.uuid) ? AgentStatus::Running : AgentStatus::Success;
        s.createdAtMs = ext.lastActivityMs;
        m_agentSessions.append(s);
    }
}

// Click handler for an external spinner: add (or re-select) its read-only row.
// Register a detected external session as a read-only list row WITHOUT navigating
// to it. Called for every detection so the agents list and "Agents (N)" count
// always reflect what's running, even before the user clicks. Returns its id (0
// if it couldn't be registered).
int MainWindow::registerExternalSession(const QString &uuid)
{
    const ExternalClaudeSession *found = nullptr;
    for (const ExternalClaudeSession &e : std::as_const(m_externalClaude))
        if (e.uuid == uuid) {
            found = &e;
            break;
        }
    if (!found || m_repoDetailIndex < 0 || m_repoDetailIndex >= m_repositories.size())
        return 0;
    const RepositoryRecord &repo = m_repositories.at(m_repoDetailIndex);
    const int id = externalTempIdFor(uuid);
    if (!m_externalSurfaced.contains(id)) {
        m_externalSurfaced.insert(id, *found);
        m_externalSurfacedRepo.insert(id, repo.owner + QLatin1Char('/') + repo.name);
        m_externalSig.clear(); // a new row — force a rebuild
    } else {
        m_externalSurfaced[id] = *found; // refresh metadata in place
    }
    return id;
}

// Clicking an external spinner just jumps to its (already auto-created) row.
void MainWindow::surfaceExternalSession(const QString &uuid)
{
    const int id = registerExternalSession(uuid);
    if (id == 0)
        return;
    m_externalReadOffset.remove(id); // fresh render on select
    injectExternalSessions();        // so findAgentSession(id) resolves before the switch
    switchToAgentsTab(id);           // shows the Agents tab + renders the detail
    if (m_agentTable) {
        for (int r = 0; r < m_agentTable->rowCount(); ++r) {
            QTableWidgetItem *cell = m_agentTable->item(r, 0);
            if (cell && cell->data(Qt::UserRole).toInt() == id) {
                m_agentTable->selectRow(r);
                break;
            }
        }
    }
}

// Delete on an external row: if the real process is still live, kill it first
// (same as Stop) so deleting the row doesn't leave it running invisibly outside
// ForkMesh; then drop the temporary mirror either way.
void MainWindow::unsurfaceExternalSession(int sessionId)
{
    if (!isExternalSession(sessionId))
        return;
    m_externalSurfaced.remove(sessionId);
    m_externalSurfacedRepo.remove(sessionId);
    m_externalReadOffset.remove(sessionId);
    m_externalSig.clear();
    if (m_selectedAgentSessionId == sessionId)
        m_selectedAgentSessionId = -1;
    reloadAgents();
}

// Send SIGTERM to every pid, with a delayed SIGKILL fallback for whichever are
// still alive after the grace period. kill(pid, 0) probes liveness without
// signalling. Shared by Stop and Delete on an external row.
void MainWindow::killExternalSessionPids(const QList<qint64> &pids)
{
    for (const qint64 pid : pids)
        ::kill(static_cast<pid_t>(pid), SIGTERM);
    QTimer::singleShot(4000, this, [pids] {
        for (const qint64 pid : pids)
            if (::kill(static_cast<pid_t>(pid), 0) == 0)
                ::kill(static_cast<pid_t>(pid), SIGKILL);
    });
}

// Stop on an external row terminates the real `claude` CLI process. ForkMesh
// holds no QProcess handle for it (it was started elsewhere), so we locate the
// process by uuid/cwd and signal it directly.
void MainWindow::stopExternalSession(int sessionId)
{
    auto it = m_externalSurfaced.constFind(sessionId);
    if (it == m_externalSurfaced.constEnd())
        return;
    const ExternalClaudeSession ext = it.value();
    const QList<qint64> pids = ClaudeSessionScan::findSessionPids(ext.uuid, ext.cwd);
    if (pids.isEmpty()) {
        flashMessage(QStringLiteral("Couldn't find the external Claude Code process "
                                    "to stop — it may have already exited."),
                     /*error=*/true);
        return;
    }
    if (QMessageBox::warning(
            this, QStringLiteral("Stop external Claude Code"),
            QStringLiteral("Stop the Claude Code session running in\n%1?\n\n"
                           "This ends a process ForkMesh didn't start.").arg(ext.cwd),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes)
        return;

    killExternalSessionPids(pids);
    // Reflect the stop immediately rather than waiting ~90s for the transcript to
    // fall out of the active window.
    m_externalStopped.insert(ext.uuid);
    m_externalSig.clear();
    flashMessage(QStringLiteral("Stopping external Claude Code session…"));
    reloadAgents();
    updateAgentActionState();
}

// Delete on an external row: kill the real `claude` CLI process (SIGTERM, then
// SIGKILL if it's still alive after the grace period) so it doesn't keep running
// unseen once its row is gone, then drop the temporary mirror. If the process
// already exited (row is idle, or the pid lookup comes up empty), just unsurface
// it without prompting — there's nothing left to kill.
void MainWindow::deleteExternalSession(int sessionId)
{
    auto it = m_externalSurfaced.constFind(sessionId);
    if (it == m_externalSurfaced.constEnd())
        return;
    const ExternalClaudeSession ext = it.value();
    if (externalIsLive(ext.uuid)) {
        const QList<qint64> pids = ClaudeSessionScan::findSessionPids(ext.uuid, ext.cwd);
        if (!pids.isEmpty()) {
            if (QMessageBox::warning(
                    this, QStringLiteral("Delete external Claude Code session"),
                    QStringLiteral("This Claude Code session is still running in\n%1.\n\n"
                                   "Deleting it will stop that process completely "
                                   "(ForkMesh didn't start it). Continue?").arg(ext.cwd),
                    QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes)
                return;
            killExternalSessionPids(pids);
            m_externalStopped.insert(ext.uuid);
        }
    }
    unsurfaceExternalSession(sessionId);
}

// Periodic rescan: refresh spinners always (cheap, guarded internally) and the
// list only when the detected/surfaced set actually changed; tail the open
// external transcript so its progress streams in live.
void MainWindow::onExternalClaudeTick()
{
    if (!m_agentStore)
        return;
    if (m_repoDetailIndex < 0 || m_repoDetailIndex >= m_repositories.size()) {
        if (!m_externalClaude.isEmpty()) {
            m_externalClaude.clear();
            updateAgentsTabIndicator();
        }
        return;
    }
    scanExternalClaudeSessions();
    // Auto-surface every detected session so it lands in the list (and the count)
    // immediately — the user can then click the row to watch its transcript.
    for (const ExternalClaudeSession &e : std::as_const(m_externalClaude))
        registerExternalSession(e.uuid);

    QStringList sig;
    for (const ExternalClaudeSession &e : std::as_const(m_externalClaude))
        sig << e.uuid;
    sig.sort();
    QStringList surfSig;
    for (auto it = m_externalSurfaced.constBegin(); it != m_externalSurfaced.constEnd();
         ++it)
        surfSig << QStringLiteral("%1:%2").arg(it.key()).arg(externalIsLive(it.value().uuid));
    surfSig.sort();
    const QString combined = sig.join(QLatin1Char(',')) + QLatin1Char('|') +
                             surfSig.join(QLatin1Char(','));
    if (combined != m_externalSig) {
        m_externalSig = combined;
        injectExternalSessions();
        refreshAgentTable();
        updateAgentsTabIndicator();
    }

    if (isExternalSession(m_selectedAgentSessionId) &&
        m_externalSurfaced.contains(m_selectedAgentSessionId))
        renderExternalTranscript(m_selectedAgentSessionId, /*full=*/false);
}

// Render (full) or tail (incremental) a surfaced external session's transcript
// into the shared transcript view + raw log.
void MainWindow::renderExternalTranscript(int sessionId, bool full)
{
    if (!m_agentTranscript)
        return;
    auto it = m_externalSurfaced.constFind(sessionId);
    if (it == m_externalSurfaced.constEnd())
        return;
    const ExternalClaudeSession ext = it.value();

    qint64 offset;
    if (full) {
        m_agentTranscript->clear();
        // The shared transcript view now holds an external session, so the stream
        // render guard must not believe its session is still on screen.
        m_renderedTranscriptSession = -1;
        m_renderedExternalSession = sessionId;
        offset = ClaudeSessionScan::tailStartOffset(ext.path, 400 * 1024);
    } else {
        offset = m_externalReadOffset.value(sessionId, 0);
    }
    qint64 newOffset = offset;
    const QList<QJsonObject> events =
        ClaudeSessionScan::readEvents(ext.path, offset, &newOffset);
    m_externalReadOffset[sessionId] = newOffset;
    if (!events.isEmpty())
        // Surfaced external transcripts arrive in event batches rather than raw
        // bytes; scale the meter bump by how many landed this read.
        noteAgentActivity(sessionId, events.size() * 200);

    // A full surface replays hundreds of tail events at once — no per-row
    // fade-in churn for those; incremental tails keep the animation.
    m_agentTranscript->setBulkPopulate(full);
    qint64 addedTokens = 0;
    for (const QJsonObject &ev : events) {
        if (ev.value(QStringLiteral("type")).toString() == QLatin1String("assistant"))
            addedTokens += static_cast<qint64>(
                ev.value(QStringLiteral("message")).toObject()
                    .value(QStringLiteral("usage")).toObject()
                    .value(QStringLiteral("output_tokens")).toDouble());
        if (ev.value(QStringLiteral("type")).toString() == QLatin1String("user")) {
            // The on-disk "user" lines carry both real prompts (text) and tool
            // results; handleEvent only renders the latter, so add prompt bubbles
            // here. (Mixed lines are rare, so doing both is harmless.)
            const QJsonValue cv = ev.value(QStringLiteral("message")).toObject()
                                      .value(QStringLiteral("content"));
            if (cv.isString()) {
                if (!cv.toString().trimmed().isEmpty())
                    m_agentTranscript->addUserTurn(cv.toString());
            } else {
                for (const QJsonValue &bv : cv.toArray()) {
                    const QJsonObject b = bv.toObject();
                    if (b.value(QStringLiteral("type")).toString() == QLatin1String("text")) {
                        const QString t = b.value(QStringLiteral("text")).toString();
                        if (!t.trimmed().isEmpty())
                            m_agentTranscript->addUserTurn(t);
                    }
                }
            }
        }
        m_agentTranscript->handleEvent(ev);
    }
    m_agentTranscript->setBulkPopulate(false);
    // A full re-render recounts from the rendered tail; an incremental tail adds
    // to what's already there. Either way clamp to the prior figure so surfacing
    // a long external session (whose 400 KB tail under-counts its real total)
    // never makes the token cell jump backwards.
    if (full)
        m_sessionTokens[sessionId] =
            qMax(m_sessionTokens.value(sessionId), addedTokens);
    else if (addedTokens > 0)
        m_sessionTokens[sessionId] += addedTokens;
    if (full || addedTokens > 0)
        updateAgentTokenCell(sessionId);

    if (full && m_agentLog) {
        QFile f(ext.path);
        if (f.open(QIODevice::ReadOnly)) {
            f.seek(ClaudeSessionScan::tailStartOffset(ext.path, 400 * 1024));
            // Separate each JSON object with a blank line so the raw view is readable.
            const QStringList objs =
                QString::fromUtf8(f.readAll()).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
            setAgentLogText(sessionId, objs.join(QStringLiteral("\n\n")));
        }
    }
    if (full)
        reapplyTranscriptSearch(); // re-highlight against the rebuilt transcript
}

// Buffer one event for a session and, if that session is the one on screen,
// render it live. Also collect the files it edits for the side panel.
void MainWindow::applyTranscriptEvent(int sessionId, const QJsonObject &ev)
{
    m_streamEvents[sessionId].append(ev);
    // Persist the turn so the transcript survives an app restart (issue #41).
    if (m_agentStore && m_streamSessionInfo.contains(sessionId))
        m_agentStore->appendEvent(m_streamSessionInfo.value(sessionId), ev);

    // Mirror a readable rendering of each turn into the plain-text run log so the
    // website's live transcript actually shows the conversation (adhoc #266). The
    // rich native transcript (m_agentTranscript) is built from the stream-json
    // events above, but the website only has the run log (readLog -> pushed as
    // "transcript"), which otherwise never sees anything past the "==>" preamble
    // for Claude Code sessions. Persisted history replays via
    // ensureStreamEventsLoaded (not through here), so this only appends live
    // turns and never double-writes on restart.
    if (m_agentStore && m_streamSessionInfo.contains(sessionId)) {
        const QString etype = ev.value(QStringLiteral("type")).toString();
        QString logText;
        if (etype == QLatin1String("_local_user")) {
            const QString t = ev.value(QStringLiteral("text")).toString().trimmed();
            if (!t.isEmpty())
                logText = QStringLiteral("\n> ") + t + QLatin1Char('\n');
        } else if (etype == QLatin1String("assistant")) {
            const QJsonArray content = ev.value(QStringLiteral("message")).toObject()
                                           .value(QStringLiteral("content")).toArray();
            for (const QJsonValue &bv : content) {
                const QJsonObject b = bv.toObject();
                const QString btype = b.value(QStringLiteral("type")).toString();
                if (btype == QLatin1String("text")) {
                    const QString t = b.value(QStringLiteral("text")).toString();
                    if (!t.trimmed().isEmpty())
                        logText += QLatin1Char('\n') + t.trimmed() + QLatin1Char('\n');
                } else if (btype == QLatin1String("tool_use")) {
                    const QString name = b.value(QStringLiteral("name")).toString();
                    const QJsonObject input = b.value(QStringLiteral("input")).toObject();
                    // Prefer the most descriptive single field per tool, else fall
                    // back to a compact JSON dump so every tool call is visible.
                    QString arg = input.value(QStringLiteral("file_path")).toString();
                    if (arg.isEmpty())
                        arg = input.value(QStringLiteral("command")).toString();
                    if (arg.isEmpty())
                        arg = input.value(QStringLiteral("path")).toString();
                    if (arg.isEmpty())
                        arg = input.value(QStringLiteral("pattern")).toString();
                    if (arg.isEmpty() && !input.isEmpty())
                        arg = QString::fromUtf8(
                            QJsonDocument(input).toJson(QJsonDocument::Compact));
                    if (arg.size() > 200)
                        arg = arg.left(200) + QString::fromUtf8("\xE2\x80\xA6");
                    logText += QStringLiteral("\n[%1%2]\n")
                                   .arg(name, arg.isEmpty() ? QString()
                                                            : QStringLiteral(" ") + arg);
                }
            }
        }
        if (!logText.isEmpty())
            m_agentStore->appendLog(m_streamSessionInfo.value(sessionId), logText);
    }

    if (ev.value(QStringLiteral("type")).toString() == QLatin1String("assistant")) {
        // Accumulate token usage so the agents list shows it live (see
        // updateAgentTokenCell) — counted for every session, selected or not.
        const qint64 out = static_cast<qint64>(
            ev.value(QStringLiteral("message")).toObject()
                .value(QStringLiteral("usage")).toObject()
                .value(QStringLiteral("output_tokens")).toDouble());
        if (out > 0) {
            m_sessionTokens[sessionId] += out;
            if (AgentSession *as = findAgentSession(sessionId))
                as->totalTokens = static_cast<int>(
                    qMin<qint64>(m_sessionTokens.value(sessionId), 2'000'000'000));
            updateAgentTokenCell(sessionId);
        }
        const QJsonArray content = ev.value(QStringLiteral("message")).toObject()
                                       .value(QStringLiteral("content")).toArray();
        QString assistantText;
        bool askedQuestion = false;
        for (const QJsonValue &bv : content) {
            const QJsonObject b = bv.toObject();
            const QString btype = b.value(QStringLiteral("type")).toString();
            if (btype == QLatin1String("text"))
                assistantText += b.value(QStringLiteral("text")).toString();
            if (btype != QLatin1String("tool_use"))
                continue;
            const QString name = b.value(QStringLiteral("name")).toString();
            if (name == QLatin1String("AskUserQuestion"))
                askedQuestion = true;
            if (name == QLatin1String("Edit") || name == QLatin1String("Write")
                || name == QLatin1String("MultiEdit") || name == QLatin1String("NotebookEdit")) {
                const QString p = b.value(QStringLiteral("input")).toObject()
                                      .value(QStringLiteral("file_path")).toString();
                if (!p.isEmpty() && !m_streamFiles[sessionId].contains(p))
                    m_streamFiles[sessionId].append(p);
            }
        }
        if (!assistantText.trimmed().isEmpty())
            m_lastAssistantText[sessionId] = assistantText.trimmed();
        // Some clarifying questions never call the AskUserQuestion tool at all —
        // the CLI just lays out a numbered list of options in plain prose (e.g.
        // "do you want me to: 1. ... or 2. ...?"). Detect that the same way the
        // transcript view renders its clickable card for it (issue #212), so
        // the agent still needs to show the hand icon here too.
        if (!askedQuestion) {
            QStringList inlineOptions;
            askedQuestion = ClaudeTranscriptView::parseInlineChoices(assistantText, inlineOptions);
        }
        // A clarifying question (AskUserQuestion, or the plain-prose case above)
        // stops the turn with no `result` event — the agent is waiting on the
        // user's answer, so flag it "Waiting" and notify just as an ended turn
        // would.
        if (askedQuestion)
            notifyAgentWaiting(sessionId, false);
    }

    // The CLI is asking to use a tool while in manual mode: the agent is blocked
    // on the user's approval — surface it (see notifyAgentWaiting). A plain
    // `result` (the turn finishing normally, e.g. the agent reporting "Done") is
    // NOT a wait: it's handled below as a successful completion. Only genuine
    // input-required states — a permission prompt here, or an AskUserQuestion
    // multiple-choice handled above — mark the session "Waiting" (adhoc #163).
    const QString type = ev.value(QStringLiteral("type")).toString();
    if (type == QLatin1String("control_request"))
        notifyAgentWaiting(sessionId, /*needsPermission=*/true);

    // A system/init event is the CLI announcing a (re)started session — the
    // "● session started" transcript divider. Persisted history never replays
    // through here (ensureStreamEventsLoaded fills the buffers directly), so
    // this only fires for a live launch: make sure the session shows Running
    // again instead of the previous run's terminal state (adhoc #33).
    if (type == QLatin1String("system")
        && ev.value(QStringLiteral("subtype")).toString() == QLatin1String("init"))
        markAgentSessionRunning(sessionId);

    // The CLI's final `result` event carries the run summary the transcript shows
    // as "done · N turns · Ms · $X". Persist those figures on the session and
    // refresh the list cells in place so the summary survives a restart and shows
    // in the agents list, not just the open transcript (issue #296).
    if (type == QLatin1String("result")) {
        if (AgentSession *as = findAgentSession(sessionId)) {
            const int turns = ev.value(QStringLiteral("num_turns")).toInt();
            const qint64 dur = static_cast<qint64>(
                ev.value(QStringLiteral("duration_ms")).toDouble());
            const double cost = ev.value(QStringLiteral("total_cost_usd")).toDouble();
            if (turns > 0)
                as->numTurns = turns;
            if (dur > 0)
                as->durationMs = dur;
            if (cost > 0)
                as->costUsd = cost;
            // A `result` with is_error (or an "error_*" subtype, e.g.
            // error_max_turns / error_during_execution) means the CLI failed the
            // run, not finished it. Mark the session Failed so the list/header show
            // the red error state instead of a green "Success" — the finished()
            // handler only promotes Running/Waiting to Success, so this sticks.
            const QString subtype = ev.value(QStringLiteral("subtype")).toString();
            if (ev.value(QStringLiteral("is_error")).toBool()
                || subtype.startsWith(QLatin1String("error"))) {
                as->status = AgentStatus::Failed;
                as->finishedAtMs = QDateTime::currentMSecsSinceEpoch();
                const QString detail = ev.value(QStringLiteral("result")).toString().trimmed();
                as->lastError = detail.isEmpty() ? subtype : detail;
            } else if (as->status == AgentStatus::Running) {
                // A clean `result` means the turn finished successfully — the agent
                // said its piece (e.g. "Done") and isn't blocked on the user. Mark
                // it "Done" (Success) rather than "Waiting" (adhoc #163). The
                // process stays alive for follow-ups; a new user turn flips it back
                // to Running. Guarded on Running so a prior AskUserQuestion/permission
                // "Waiting" set earlier in this turn isn't clobbered.
                as->status = AgentStatus::Success;
                as->finishedAtMs = QDateTime::currentMSecsSinceEpoch();
                as->lastError.clear();
            }
            if (m_agentStore && !isExternalSession(sessionId))
                m_agentStore->saveSession(*as);
            updateAgentCostCell(sessionId);
            updateAgentRunSummaryCells(sessionId); // fill the Turns/Time columns
            updateAgentStatusCell(sessionId);
        }
    }

    if (sessionId == m_selectedAgentSessionId && m_agentTranscript) {
        // A modal dialog (e.g. the UI-stall diagnostics window) spins its own
        // nested event loop. Building transcript rows into the view sitting behind
        // it blocks the GUI thread for no benefit — the user can't see or scroll
        // the transcript while the dialog is up, and the per-row widget
        // reparenting/style-resolution is exactly what froze the loop for ~1.5s
        // (sampled in addRow -> insertWidget -> setStyle_helper). Defer instead:
        // leave the render guard behind so the events buffered while the dialog was
        // open are flushed in a single rebuild the moment the view is live again.
        const int rendered = m_renderedTranscriptSession == sessionId
                                 ? m_renderedTranscriptCount
                                 : -1;
        const int have = m_streamEvents.value(sessionId).size();
        if (QApplication::activeModalWidget()) {
            // Skipped on purpose; the render guard stays at `rendered` (< have) so
            // the next live event or showAgentSession() rebuilds from the buffer.
        } else if (rendered == have - 1) {
            // The view is in sync with the buffer: append just this newest event
            // (the cheap incremental fast path).
            if (ev.value(QStringLiteral("type")).toString() == QLatin1String("_local_user"))
                m_agentTranscript->addUserTurn(ev.value(QStringLiteral("text")).toString());
            else
                m_agentTranscript->handleEvent(ev);
            // Refresh the Files-changed panel on real turns only, never on the
            // high-frequency `stream_event` partials. With --include-partial-messages
            // those deltas arrive far faster than the diff debounce's 400ms interval,
            // so refreshing on every one perpetually restarted (starved) the timer and
            // the `git diff` never fired while the agent streamed — the panel only
            // caught up once output paused. Partial deltas can't change the file set.
            if (type != QLatin1String("stream_event"))
                refreshAgentFilesPanel(sessionId);
            // The view was just kept in sync incrementally, so the render guard's
            // count must track the append — otherwise the next reload would force a
            // full rebuild of a transcript that's already up to date.
            m_renderedTranscriptCount = have;
        } else {
            // We fell behind (a modal owned the loop, or the view was rebuilt for a
            // different session): rebuild once from the buffer so no events are
            // dropped, then resume the incremental fast path above.
            renderTranscriptForSession(sessionId);
            refreshAgentFilesPanel(sessionId);
        }
    }
}

// Walk this session's stream-json events newest-first for the conversation id
// the `claude` CLI stamps on each one. Returned id feeds `--resume` so a stopped
// agent is picked up with its full context (adhoc #182). Events are held in
// memory while a session is live and reloaded from disk on resume
// (ensureStreamEventsLoaded), so this is the authoritative source after a
// restart too. Synthetic `_local_user` turns carry no id and are skipped.
QString MainWindow::lastClaudeSessionId(int sessionId) const
{
    const QList<QJsonObject> &events = m_streamEvents.value(sessionId);
    for (auto it = events.crbegin(); it != events.crend(); ++it) {
        const QString id = it->value(QStringLiteral("session_id")).toString();
        if (!id.isEmpty())
            return id;
    }
    return QString();
}

// The session started working again — a resumed CLI announced itself
// (system/init, the "session started" divider) or a new user turn was steered
// into a live one. Whatever terminal state the previous turn left behind
// (Waiting, Failed from an error result, Success), the list must show it
// Running now (adhoc #33). Mirrors continueSelectedAgentSession's reset: the
// stale error/finish stamps belong to the previous run. A session whose branch
// was already merged shows the purple "merged" badge instead of status/icon
// (see applyAgentStatusCell) — but reusing that same branch for another turn
// means it's no longer just a merged, done session, so clear the flag and let
// the running spinner show again.
void MainWindow::markAgentSessionRunning(int sessionId)
{
    AgentSession *s = findAgentSession(sessionId);
    if (!s || s->status == AgentStatus::Running)
        return;
    s->status = AgentStatus::Running;
    s->lastError.clear();
    s->finishedAtMs = 0;
    s->merged = false;
    s->mergedAtMs = 0;
    if (s->startedAtMs <= 0)
        s->startedAtMs = QDateTime::currentMSecsSinceEpoch();
    if (m_agentStore && !isExternalSession(sessionId))
        m_agentStore->saveSession(*s);
    updateAgentStatusCell(sessionId);
}

// The agent's turn ended (or it needs permission) and it's now waiting on the
// user: flag the session "Waiting" in the list and raise a top-bar notification.
void MainWindow::notifyAgentWaiting(int sessionId, bool needsPermission)
{
    AgentSession *s = findAgentSession(sessionId);
    if (!s || s->status != AgentStatus::Running)
        return; // only meaningful for a session that was actively running
    s->status = AgentStatus::Waiting;
    if (m_agentStore && !isExternalSession(sessionId))
        m_agentStore->saveSession(*s);
    updateAgentStatusCell(sessionId);

    const QString who = s->issueNumber > 0 ? QStringLiteral("#%1").arg(s->issueNumber)
                                           : QStringLiteral("Agent");
    const QString last = m_lastAssistantText.value(sessionId);
    const bool question = last.endsWith(QLatin1Char('?'));
    QString snippet = last;
    if (snippet.size() > 80)
        snippet = snippet.left(79) + QString::fromUtf8("\xE2\x80\xA6");
    const QString robot = QString::fromUtf8("\xF0\x9F\xA4\x96"); // 🤖
    QString msg;
    if (needsPermission)
        msg = QStringLiteral("%1 %2 needs your approval to continue").arg(robot, who);
    else if (question)
        msg = QStringLiteral("%1 %2 has a question: %3").arg(robot, who, snippet);
    else
        msg = QStringLiteral("%1 %2 is waiting for your reply").arg(robot, who);
    // Make the toast clickable straight through to the waiting session, so the user
    // doesn't have to hunt for it in the agents list (adhoc #189).
    flashMessage(msg, /*error=*/false,
                 QStringLiteral("fm:agent:%1").arg(sessionId));
}

// Refresh just the Status cell for a session's row, in place — avoids the full
// table rebuild (which would re-render the open transcript) on status flips.
void MainWindow::updateAgentStatusCell(int sessionId)
{
    // Every call site here is a genuine status transition (queued->running,
    // running->waiting/success/failed, etc.) — piggyback the debounced website
    // push so the browser view picks it up shortly after, without a request
    // per flip (adhoc #182).
    scheduleAgentSessionsPush();
    if (!m_agentTable)
        return;
    AgentSession *s = findAgentSession(sessionId);
    if (!s)
        return;
    for (int r = 0; r < m_agentTable->rowCount(); ++r) {
        QTableWidgetItem *idItem = m_agentTable->item(r, 0);
        if (!idItem || idItem->data(Qt::UserRole).toInt() != sessionId)
            continue;
        QSignalBlocker block(m_agentTable);
        // Column 4 is Status (column 3 is Model — see applyAgentRowCells for the
        // column layout); writing here used to stomp the Model cell instead.
        QTableWidgetItem *cell = m_agentTable->item(r, 4);
        if (!cell) {
            cell = new QTableWidgetItem;
            m_agentTable->setItem(r, 4, cell);
        }
        applyAgentStatusCell(cell, *s);
        break;
    }
    // Keep the footer "Agents:" strip's per-session dot (the ones above the
    // prompt) in step with every status flip, not just a full reloadAgents() —
    // otherwise it only catches up once the user opens the Agents tab.
    refreshAgentStatusRow();
    if (sessionId == m_selectedAgentSessionId) {
        updateAgentActionState();
        // The list row is only half the picture: when this session's detail
        // view is open, the "Connected · working…" pill above the transcript
        // is what the user is actually looking at (adhoc #33 — a status flip
        // like a resumed session going back to Running otherwise left that
        // pill on its stale text until the next full showAgentSession()).
        refreshAgentStatusPill(sessionId);
    }
}

// Rebuild the "Connected · working on the task…" pill in the session detail
// header from the session's current status. Split out of showAgentSession so
// a targeted status flip (updateAgentStatusCell) can refresh just this pill
// without paying for a full header/meta rebuild.
void MainWindow::refreshAgentStatusPill(int sessionId)
{
    if (!m_agentStatusPill || sessionId != m_selectedAgentSessionId)
        return;
    AgentSession *session = findAgentSession(sessionId);
    if (!session)
        return;
    const QString s = session->status;
    QString dotColor = agentStatusColor(s).name();
    QString label;
    if (s == AgentStatus::Running)
        label = "Connected \xC2\xB7 working on the task\xE2\x80\xA6";
    else if (s == AgentStatus::Queued)
        label = "Queued";
    else if (s == AgentStatus::Waiting)
        label = "Waiting";
    else if (s == AgentStatus::Success)
        label = "Done";
    else if (s == AgentStatus::Failed)
        label = "Failed";
    else
        label = agentStatusText(s);
    QString pill =
        QString::fromUtf8("<span style='color:%1'>\xE2\x97\x8F</span> "
                          "<span style='color:#8b949e'>%2</span>")
            .arg(dotColor, label.toHtmlEscaped());
    // Issue #291: once the worktree/PR has landed in the base branch, flag
    // it right on the status pill in the merged-purple used elsewhere.
    if (session->merged)
        pill += QString::fromUtf8(
                    " <span style='color:#a371f7'>\xE2\x97\x8F merged into %1</span>")
                    .arg(agentMergeBase(*session).toHtmlEscaped());
    m_agentStatusPill->setText(pill);
}

// Spin the green "sync" glyph on every running row's Status cell so the agents
// list shows a live spinner (issue #108). Driven by m_agentsSpinTimer, which only
// ticks while a session is running, so finished rows keep their static icon.
void MainWindow::animateRunningAgentIcons()
{
    if (!m_agentTable)
        return;
    const QIcon icon(rotatedTintedOcticonPixmap(
        "sync", QColor("#3fb950"), 14, m_agentsSpinFrame * 36.0));
    QSignalBlocker block(m_agentTable);
    for (int r = 0; r < m_agentTable->rowCount(); ++r) {
        QTableWidgetItem *idItem = m_agentTable->item(r, 0);
        if (!idItem)
            continue;
        const AgentSession *s = findAgentSession(idItem->data(Qt::UserRole).toInt());
        if (!s || s->merged || s->status != AgentStatus::Running)
            continue;
        // Column 4 is Status (column 3 is Model — the spinner belongs here, not
        // there; see applyAgentRowCells for the column layout).
        if (QTableWidgetItem *cell = m_agentTable->item(r, 4))
            cell->setIcon(icon);
        // Keep the Time column's live elapsed figure ticking for running rows
        // (issue #245) — this timer already visits exactly the running sessions,
        // so refresh the cell here rather than spinning up a second timer.
        if (QTableWidgetItem *runTime = m_agentTable->item(r, 6))
            applyAgentTimeCell(runTime, *s);
    }
}

qint64 MainWindow::sessionTokenTotal(const AgentSession &session) const
{
    // The live counter is the source of truth while a session streams; the
    // persisted field covers sessions that finished in a previous run (and API
    // agents, which set totalTokens wholesale on completion). Taking the max of
    // the two guarantees the displayed figure only ever grows — it can't jump
    // back to zero when m_agentSessions is reloaded from disk mid-run.
    return qMax(m_sessionTokens.value(session.id, 0),
                static_cast<qint64>(session.totalTokens));
}

void MainWindow::seedSessionTokens()
{
    for (const AgentSession &s : std::as_const(m_agentSessions))
        m_sessionTokens[s.id] =
            qMax(m_sessionTokens.value(s.id, 0), static_cast<qint64>(s.totalTokens));
}

void MainWindow::setAgentUsageLabel(const AgentSession &session)
{
    if (!m_navTokenUsage)
        return;
    const int window = session.contextWindow > 0 ? session.contextWindow : 32000;
    const int maxOutput =
        session.maxOutputTokens > 0
            ? session.maxOutputTokens
            : qMax(256, QSettings().value(kAgentMaxOutputSetting, 2000).toInt());
    const int pct = window > 0 ? qMin(100, session.contextTokens * 100 / window) : 0;
    static_cast<TokenUsageMiniChart *>(m_navTokenUsage)->setStats(
        QStringLiteral("Session token usage: %1 total (%2 prompt estimate, %3 transcript estimate) · budget: context %4/%5 (%6%), max output %7 tokens · credits ~%8 · cost ~%9")
            .arg(formatCount(sessionTokenTotal(session)))
            .arg(formatCount(session.promptTokens))
            .arg(formatCount(session.completionTokens))
            .arg(formatCount(session.contextTokens))
            .arg(formatCount(window))
            .arg(pct)
            .arg(formatCount(maxOutput))
            .arg(formatCount(session.estimatedCredits))
            .arg(agentCostText(session.costUsd)));
}

// Refresh just the Tokens cell for a session's row, in place — cheap enough to
// call on every assistant message without rebuilding the whole table.
void MainWindow::updateAgentTokenCell(int sessionId)
{
    if (!m_agentTable)
        return;
    const qint64 toks = m_sessionTokens.value(sessionId, 0);
    for (int r = 0; r < m_agentTable->rowCount(); ++r) {
        QTableWidgetItem *idItem = m_agentTable->item(r, 0);
        if (!idItem || idItem->data(Qt::UserRole).toInt() != sessionId)
            continue;
        QSignalBlocker block(m_agentTable);
        QTableWidgetItem *cell = m_agentTable->item(r, 8);
        if (!cell) {
            cell = new QTableWidgetItem;
            m_agentTable->setItem(r, 8, cell);
        }
        cell->setData(Qt::DisplayRole, toks > 0 ? formatCount(toks) : QStringLiteral("-"));
        cell->setData(Qt::UserRole, static_cast<qlonglong>(toks));
        // Refresh the Speed cell in the same pass so the tok/s figure climbs in
        // real time while the agent streams — agentEffectiveDurationMs measures
        // a running session against the wall clock, so this recomputes the live
        // rate from the freshly-bumped token total.
        if (QTableWidgetItem *speed = m_agentTable->item(r, 9))
            if (const AgentSession *s = findAgentSession(sessionId))
                applyAgentSpeedCell(speed, *s, sessionTokenTotal(*s));
        break;
    }
    // Keep the open detail panel's "Session token usage" line in step with the
    // live counter so it climbs in real time instead of only on the next reload.
    if (sessionId == m_selectedAgentSessionId)
        if (const AgentSession *s = findAgentSession(sessionId))
            setAgentUsageLabel(*s);
}

// Refresh just the Cost cell for a session's row, in place, from its stored
// costUsd — used when a Claude Code run reports its final cost via the `result`
// event so the list updates without a full table rebuild (which would re-render
// the open transcript). Sibling of updateAgentTokenCell (issue #296).
void MainWindow::updateAgentCostCell(int sessionId)
{
    if (!m_agentTable)
        return;
    const AgentSession *s = findAgentSession(sessionId);
    if (!s)
        return;
    for (int r = 0; r < m_agentTable->rowCount(); ++r) {
        QTableWidgetItem *idItem = m_agentTable->item(r, 0);
        if (!idItem || idItem->data(Qt::UserRole).toInt() != sessionId)
            continue;
        QSignalBlocker block(m_agentTable);
        // Column 7 is Cost (column 6 is Time — see applyAgentRowCells for the
        // column layout); this used to stomp the Time cell instead.
        QTableWidgetItem *cell = m_agentTable->item(r, 7);
        if (!cell) {
            cell = new QTableWidgetItem;
            m_agentTable->setItem(r, 7, cell);
        }
        cell->setData(Qt::DisplayRole, agentCostText(s->costUsd));
        cell->setData(Qt::UserRole, s->costUsd);
        break;
    }
}

// Refresh the Turns and Time cells for a session's row, in place — used when a
// Claude Code run reports its final `num_turns`/`duration_ms` via the `result`
// event so the new columns fill without a full table rebuild (which would
// re-render the open transcript). Sibling of updateAgentCostCell.
void MainWindow::updateAgentRunSummaryCells(int sessionId)
{
    if (!m_agentTable)
        return;
    const AgentSession *s = findAgentSession(sessionId);
    if (!s)
        return;
    for (int r = 0; r < m_agentTable->rowCount(); ++r) {
        QTableWidgetItem *idItem = m_agentTable->item(r, 0);
        if (!idItem || idItem->data(Qt::UserRole).toInt() != sessionId)
            continue;
        QSignalBlocker block(m_agentTable);
        // Turns/Time/Speed are columns 5/6/9 (see applyAgentRowCells for the
        // column layout) — this used to write one column early into
        // Status/Turns/Tokens instead.
        if (QTableWidgetItem *turns = m_agentTable->item(r, 5))
            applyAgentTurnsCell(turns, *s);
        if (QTableWidgetItem *runTime = m_agentTable->item(r, 6))
            applyAgentTimeCell(runTime, *s);
        // Speed needs both the token total and the now-known run duration.
        if (QTableWidgetItem *speed = m_agentTable->item(r, 9))
            applyAgentSpeedCell(speed, *s, sessionTokenTotal(*s));
        break;
    }
}

// Pulse a session's night-rider light so its activity column sweeps while raw
// output is streaming. Called from every raw-output path (headless AgentRunner
// logs, live Claude stream lines, surfaced external transcripts). The driving
// timer is started on demand and self-stops once every light has gone idle.
void MainWindow::noteAgentActivity(int sessionId, int bytes)
{
    if (sessionId <= 0)
        return;
    AgentScannerState &st = m_scannerStates[sessionId];
    st.lastActivityMs = QDateTime::currentMSecsSinceEpoch();
    // Top up the live-output intensity meter by how much just streamed (a ~512B
    // chunk pins it). onScannerTick decays this every frame, so a steady stream
    // holds it hot while a pause fades it out — that's what the sweep reacts to.
    const double bump = bytes > 0 ? qMin(1.0, bytes / 512.0) : 0.5;
    st.intensity = qMin(1.0, st.intensity + bump);
    if (m_scannerTimer && !m_scannerTimer->isActive())
        m_scannerTimer->start();
}

// Advance every active scanner's sweep, repaint the Activity cells, and stop the
// timer once no session has produced output recently — so idle agents cost
// nothing while running ones wave left<->right in real time.
void MainWindow::onScannerTick()
{
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    // Baseline bounce ~one back-and-forth per 1.7s; the live-output intensity
    // accelerates it up to ~2.5x so a busy agent visibly races.
    const double base = (m_scannerTimer ? m_scannerTimer->interval() : 45) / 1700.0;
    bool anyActive = false;
    for (auto it = m_scannerStates.begin(); it != m_scannerStates.end(); ++it) {
        // Decay the live-output meter every frame; noteAgentActivity re-bumps it
        // per chunk, so a steady stream holds it high and a pause fades it out.
        it->intensity *= 0.85;
        if (it->intensity < 0.01)
            it->intensity = 0.0;
        // Keep the light continuously sweeping while there's any activity left:
        // either output landed recently or the meter is still winding down.
        if (it->intensity <= 0.0 && now - it->lastActivityMs >= kScannerIdleMs)
            continue;
        it->phase += base * (0.6 + 1.9 * it->intensity);
        if (it->phase >= 1.0)
            it->phase -= 1.0;
        anyActive = true;
    }
    // Repaint the visible Activity cells (cheap for these per-repo tables). The
    // final, just-went-idle tick still repaints, so lights settle to rest.
    if (m_agentTable) {
        for (int r = 0; r < m_agentTable->rowCount(); ++r) {
            if (m_agentTable->item(r, kAgentActivityColumn))
                m_agentTable->update(
                    m_agentTable->model()->index(r, kAgentActivityColumn));
        }
    }
    if (!anyActive && m_scannerTimer)
        m_scannerTimer->stop();
}

// Restore a Claude Code session's transcript from disk (issue #41). The events
// were persisted as the run streamed, so the rich transcript survives an app
// restart even though the live stream object is gone. Only populates when the
// session actually has persisted events, so non-transcript sessions keep
// showing their plain log instead of an empty transcript surface.
// Rebuild the side buffers the raw view and edited-files panel read from
// (mirrors applyTranscriptEvent). Pure — safe on a worker thread.
static void buildStreamSideBuffers(const QList<QJsonObject> &events, QString *raw,
                                   QStringList *files)
{
    for (const QJsonObject &ev : events) {
        if (ev.value(QStringLiteral("type")).toString() == QLatin1String("_local_user"))
            continue; // synthetic user turn, never part of the raw CLI stream
        *raw += QString::fromUtf8(QJsonDocument(ev).toJson(QJsonDocument::Compact))
                + QStringLiteral("\n\n");
        if (ev.value(QStringLiteral("type")).toString() != QLatin1String("assistant"))
            continue;
        // Collect edited files for the side panel (mirrors applyTranscriptEvent).
        const QJsonArray content = ev.value(QStringLiteral("message")).toObject()
                                       .value(QStringLiteral("content")).toArray();
        for (const QJsonValue &bv : content) {
            const QJsonObject b = bv.toObject();
            if (b.value(QStringLiteral("type")).toString() != QLatin1String("tool_use"))
                continue;
            const QString name = b.value(QStringLiteral("name")).toString();
            if (name == QLatin1String("Edit") || name == QLatin1String("Write")
                || name == QLatin1String("MultiEdit") || name == QLatin1String("NotebookEdit")) {
                const QString p = b.value(QStringLiteral("input")).toObject()
                                      .value(QStringLiteral("file_path")).toString();
                if (!p.isEmpty() && !files->contains(p))
                    files->append(p);
            }
        }
    }
}

void MainWindow::ensureStreamEventsLoaded(int sessionId)
{
    if (!m_agentStore || m_streamEvents.contains(sessionId)
        || isExternalSession(sessionId))
        return; // already loaded/live, or a watch-only external session
    const AgentSession *s = findAgentSession(sessionId);
    if (!s)
        return;
    const QList<QJsonObject> events = m_agentStore->loadEvents(*s);
    if (events.isEmpty())
        return;
    m_streamEvents[sessionId] = events;
    buildStreamSideBuffers(events, &m_streamRaw[sessionId],
                           &m_streamFiles[sessionId]);
}

bool MainWindow::ensureStreamEventsLoadedAsync(int sessionId)
{
    if (m_streamEvents.contains(sessionId))
        return true; // already loaded (or live-streaming)
    if (!m_agentStore || isExternalSession(sessionId) ||
        m_streamEventsAbsent.contains(sessionId) ||
        m_streamEventsLoading.contains(sessionId))
        return false;
    const AgentSession *s = findAgentSession(sessionId);
    if (!s)
        return false;
    m_streamEventsLoading.insert(sessionId);
    struct LoadedEvents {
        QList<QJsonObject> events;
        QString raw;
        QStringList files;
    };
    const AgentSession snapshot = *s;
    AgentStore *store = m_agentStore;
    runOffThread<LoadedEvents>(
        [store, snapshot] {
            LoadedEvents out;
            out.events = store->loadEvents(snapshot);
            buildStreamSideBuffers(out.events, &out.raw, &out.files);
            return out;
        },
        [this, sessionId](LoadedEvents out) {
            m_streamEventsLoading.remove(sessionId);
            if (out.events.isEmpty()) {
                m_streamEventsAbsent.insert(sessionId); // don't re-probe per click
            } else if (!m_streamEvents.contains(sessionId) &&
                       !m_streamSessions.contains(sessionId)) {
                // A live stream may have (re)started while we read — it owns the
                // buffers then, and this now-stale snapshot is dropped.
                m_streamEvents[sessionId] = std::move(out.events);
                m_streamRaw[sessionId] = std::move(out.raw);
                m_streamFiles[sessionId] = std::move(out.files);
            }
            if (sessionId == m_selectedAgentSessionId)
                showAgentSession(sessionId); // render the restored history
        });
    return false;
}

// Repaint the transcript view from a session's buffered events (on selection).
void MainWindow::renderTranscriptForSession(int sessionId)
{
    if (!m_agentTranscript)
        return;
    m_agentTranscript->clear();
    const QList<QJsonObject> &events = m_streamEvents[sessionId];
    // Rebuild only the last stretch as widget rows on the initial paint. Long
    // sessions replayed one widget per event froze the opening click for
    // seconds (stall log: repolish storms under renderTranscriptForSession <-
    // showAgentSession); events past the tail feed the token/cost totals only,
    // with a "Load earlier events" notice up top. Nothing is lost — clicking
    // the notice (or scrolling near the top) reveals more via
    // loadEarlierTranscriptEvents(), a batch at a time, all the way back to the
    // start (adhoc #115: don't truncate the transcript). Bulk mode also skips
    // the per-row fade-in animation (one QGraphicsOpacityEffect per row).
    constexpr int kTranscriptRenderTail = 300;
    const int skipped = qMax(0, int(events.size()) - kTranscriptRenderTail);
    m_agentTranscript->setBulkPopulate(true);
    for (int i = 0; i < skipped; ++i)
        m_agentTranscript->accumulateStatsOnly(events.at(i));
    if (skipped > 0)
        m_agentTranscript->addSkippedNotice(skipped);
    for (int i = skipped; i < events.size(); ++i) {
        const QJsonObject &ev = events.at(i);
        if (ev.value(QStringLiteral("type")).toString() == QLatin1String("_local_user"))
            m_agentTranscript->addUserTurn(ev.value(QStringLiteral("text")).toString());
        else
            m_agentTranscript->handleEvent(ev);
    }
    m_agentTranscript->setBulkPopulate(false);
    // Remember what's now built into the shared view so showAgentSession() can
    // skip a redundant rebuild on the next reload (see its stream branch).
    m_renderedTranscriptSession = sessionId;
    m_renderedTranscriptCount = events.size();
    m_transcriptSkipped = skipped;
    m_renderedExternalSession = -1; // the shared view no longer holds an external
    reapplyTranscriptSearch(); // re-highlight against the rebuilt transcript
}

// Reveal the next batch of the selected session's earlier events, driven by
// m_agentTranscript's loadEarlierRequested() signal. The full event history is
// already resident in memory (m_streamEvents; AgentStore::loadEvents() reads
// the whole events.jsonl up front), so this is pure widget construction — no
// disk I/O — sliced small enough per batch to stay smooth while scrolling.
void MainWindow::loadEarlierTranscriptEvents()
{
    if (!m_agentTranscript || m_renderedTranscriptSession != m_selectedAgentSessionId)
        return;
    const int sessionId = m_renderedTranscriptSession;
    const QList<QJsonObject> &events = m_streamEvents.value(sessionId);
    if (m_transcriptSkipped <= 0 || m_transcriptSkipped > events.size()) {
        m_transcriptSkipped = 0;
        m_agentTranscript->prependEarlierEvents({}, 0); // clears a stale notice, if any
        return;
    }
    constexpr int kBatch = 300; // same granularity as the initial tail
    const int newSkipped = qMax(0, m_transcriptSkipped - kBatch);
    QList<QJsonObject> batch;
    batch.reserve(m_transcriptSkipped - newSkipped);
    for (int i = newSkipped; i < m_transcriptSkipped; ++i)
        batch.append(events.at(i));
    m_transcriptSkipped = newSkipped;
    m_agentTranscript->prependEarlierEvents(batch, newSkipped);
}

// Re-run the search box's query so highlights persist across a session switch
// or full re-render (the rebuild dropped them when it cleared the view).
void MainWindow::reapplyTranscriptSearch()
{
    if (!m_agentTranscript || !m_transcriptSearch)
        return;
    const QString q = m_transcriptSearch->text().trimmed();
    if (q.isEmpty())
        m_agentTranscript->clearSearch();
    else
        m_agentTranscript->search(q);
}

// Fill the edited-files panel: the union of files seen in tool calls and the
// repo's current working-tree changes.
void MainWindow::refreshAgentFilesPanel(int sessionId)
{
    if (!m_agentFilesList)
        return;
    // Render the files we already know about from the session's tool calls right
    // away — that's in-memory and cheap, so an edit shows up the instant the agent
    // makes it. The working-tree `git diff` augmentation used to run here with a
    // blocking waitForFinished(), which fired on *every* transcript event and
    // froze the UI in ~1.5-2s bursts while an agent streamed. It's now coalesced
    // and run off the event loop instead (scheduleAgentFilesDiff).
    //
    // Only draw the plain placeholder once, before the first rich (icon + per-file
    // +/-) diff render for this session. Re-running it on every transcript turn is
    // what made the panel flash back and forth: the placeholder (no icons) replaced
    // the rich list, then ~400ms later the diff redrew the rich list, over and over.
    // Once renderAgentDiff() has drawn the icon view we leave it in place and just
    // reschedule the diff, which redraws in place without the flash (adhoc #260).
    if (m_agentDiffRenderedSession != sessionId)
        populateAgentFilesPanel(sessionId, QStringList());
    scheduleAgentFilesDiff(sessionId);
}

// Rebuild the edited-files list from the session's in-memory tool-call files plus
// any working-tree diff paths handed in. A no-op when the session is no longer the
// one on screen, so a late async diff callback can't clobber another session's list.
void MainWindow::populateAgentFilesPanel(int sessionId, const QStringList &diffFiles)
{
    if (!m_agentFilesList || sessionId != m_selectedAgentSessionId)
        return;
    const QString repoPath = sessionWorkdir(sessionId); // the worktree, if any
    QStringList absPaths;
    auto addAbs = [&](const QString &p) {
        QString abs = QDir::isAbsolutePath(p) || repoPath.isEmpty()
                          ? p : QDir(repoPath).filePath(p);
        if (!absPaths.contains(abs))
            absPaths.append(abs);
    };
    for (const QString &p : m_streamFiles.value(sessionId))
        addAbs(p);
    for (const QString &p : diffFiles)
        addAbs(p);

    m_agentFilesList->clear();
    for (const QString &abs : absPaths) {
        const QString label = repoPath.isEmpty() ? abs
                                                  : QDir(repoPath).relativeFilePath(abs);
        auto *it = new QListWidgetItem(label);
        // Carry a neutral file icon even on this pre-diff placeholder so the panel
        // reads as the same icon list the rich diff render produces — no jump from
        // a plain text list to an icon list when the diff lands (adhoc #260).
        it->setIcon(themedOcticon(QStringLiteral("file-diff"), QColor("#d29922"), 14));
        it->setToolTip(abs);
        it->setData(Qt::UserRole, abs);
        m_agentFilesList->addItem(it);
    }
}

// Coalesce the working-tree `git diff --name-only` that augments the edited-files
// panel: a burst of transcript events restarts a short single-shot timer, so the
// (async) diff runs once after the burst rather than once per event.
void MainWindow::scheduleAgentFilesDiff(int sessionId)
{
    if (sessionId != m_selectedAgentSessionId)
        return;
    const QString repoPath = sessionWorkdir(sessionId);
    if (repoPath.isEmpty())
        return;
    if (!m_agentFilesDiffTimer) {
        m_agentFilesDiffTimer = new QTimer(this);
        m_agentFilesDiffTimer->setSingleShot(true);
        m_agentFilesDiffTimer->setInterval(400);
        connect(m_agentFilesDiffTimer, &QTimer::timeout, this, [this] {
            const int sid = m_selectedAgentSessionId;
            const QString dir = sessionWorkdir(sid);
            if (sid <= 0 || dir.isEmpty())
                return;
            // Diff against the merge-base of the base branch and HEAD so committed
            // work counts too (agents auto-commit mid-run) *without* counting files
            // that only arrived by merging the base branch into this one — that
            // over-count is what made a one-file session read as "14 files changed"
            // (issue #183).
            const QString base = sessionDiffBase(sid, dir);
            // Gather every git read the render needs as four parallel async
            // subprocesses; render once, when the last one lands. A late result
            // for a session the user has since clicked away from is dropped.
            auto probe = std::make_shared<AgentDiffProbe>();
            probe->pending = base.isEmpty() ? 2 : 4; // ahead/behind need a base
            const auto finish = [this, sid, probe] {
                if (--probe->pending > 0)
                    return;
                // A failed diff read (worktree vanished mid-run) keeps the last
                // rendered view rather than blanking it, matching the old path.
                if (probe->patchOk && sid == m_selectedAgentSessionId)
                    renderAgentDiff(sid, *probe);
            };
            QStringList args{QStringLiteral("diff")};
            if (!base.isEmpty())
                args << base;
            runGitDetached(dir, args,
                           [probe, finish](bool ok, const QByteArray &out) {
                               probe->patchOk = ok;
                               if (ok)
                                   probe->patch = out;
                               finish();
                           });
            // One `status --porcelain` covers what used to be two reads (tracked
            // edits vs HEAD + untracked files): the ● "uncommitted" markers.
            runGitDetached(dir, {QStringLiteral("status"), QStringLiteral("--porcelain")},
                           [probe, finish](bool ok, const QByteArray &out) {
                               if (ok)
                                   for (QString line : QString::fromUtf8(out).split(
                                            QLatin1Char('\n'), Qt::SkipEmptyParts)) {
                                       QString p = line.mid(3);
                                       const int arrow =
                                           p.indexOf(QLatin1String(" -> "));
                                       if (arrow >= 0)
                                           p = p.mid(arrow + 4);
                                       if (p.startsWith(QLatin1Char('"')) &&
                                           p.endsWith(QLatin1Char('"')))
                                           p = p.mid(1, p.size() - 2);
                                       probe->uncommitted.insert(p.trimmed());
                                   }
                               finish();
                           });
            if (!base.isEmpty()) {
                // The commits this branch adds (list + count in one read)…
                runGitDetached(dir,
                               {QStringLiteral("log"), QStringLiteral("--format=%h %s"),
                                base + QStringLiteral("..HEAD")},
                               [probe, finish](bool ok, const QByteArray &out) {
                                   if (ok)
                                       probe->commitLines =
                                           QString::fromUtf8(out).split(
                                               QLatin1Char('\n'), Qt::SkipEmptyParts);
                                   finish();
                               });
                // …and how far it trails the base branch's live tip.
                runGitDetached(dir,
                               {QStringLiteral("rev-list"), QStringLiteral("--count"),
                                QStringLiteral("HEAD..") + base},
                               [probe, finish](bool ok, const QByteArray &out) {
                                   if (ok)
                                       probe->behind =
                                           QString::fromUtf8(out).trimmed().toInt();
                                   finish();
                               });
            }
        });
    }
    m_agentFilesDiffTimer->start();
}

// The base commit a session's diff is measured against (captured at run start).
QString MainWindow::sessionBaseRef(int sessionId)
{
    if (const AgentSession *s = findAgentSession(sessionId); s && !s->baseRef.isEmpty())
        return s->baseRef;
    if (m_streamSessionInfo.contains(sessionId))
        return m_streamSessionInfo.value(sessionId).baseRef;
    return QString();
}

// The branch a session's PR targets (e.g. main), captured at run start.
QString MainWindow::sessionBaseBranch(int sessionId)
{
    if (const AgentSession *s = findAgentSession(sessionId); s && !s->baseBranch.isEmpty())
        return s->baseBranch;
    if (m_streamSessionInfo.contains(sessionId))
        return m_streamSessionInfo.value(sessionId).baseBranch;
    return QString();
}

// Resolve what a session's diff is measured *from*. The Files-changed tab must
// show exactly what the branch link's destination shows — the Worktrees/Branches
// detail view diffs the worktree against the *live* base branch tip (`git diff
// <base>`, see showWorktreeDiff). So return the base branch name and let `git
// diff <base>` resolve its current tip too. Diffing against merge-base(base, HEAD)
// instead made this page disagree with that view every time the base branch moved
// on after the fork — "it always shows something different" (adhoc #28). Diffing
// against the live tip still avoids the #183 over-count: once the branch has main
// merged in, `git diff <base>` cancels main's own changes and leaves only this
// branch's net change. Fall back to the captured base commit only when the session
// never recorded a base branch, so a diff still renders.
QString MainWindow::sessionDiffBase(int sessionId, const QString &dir)
{
    Q_UNUSED(dir);
    const QString baseBranch = sessionBaseBranch(sessionId);
    if (!baseBranch.isEmpty())
        return baseBranch;
    return sessionBaseRef(sessionId);
}

// Render the session's diff into the Files-changed tab's viewer, rebuild the file
// list with per-file +/- counts and scroll anchors, and stamp the changed-file
// count onto the tab header (issue #131). A no-op for a stale/other session so a
// late async callback can't clobber the panel after the selection moved on.
// Runs no git: the probe carries everything (see scheduleAgentFilesDiff), so this
// can't pump the event loop mid-render and re-enter itself.
void MainWindow::renderAgentDiff(int sessionId, const AgentDiffProbe &probe)
{
    if (!m_agentDiffView || sessionId != m_selectedAgentSessionId)
        return;
    const QString dir = sessionWorkdir(sessionId);
    const QString base = sessionDiffBase(sessionId, dir);
    QList<DiffFileEntry> files;
    const QString html =
        renderDiffHtml(QString::fromUtf8(probe.patch), files, dir, base, QString(),
                       QString(), QHash<QString, QString>(), QSet<QString>());
    // Handing an enormous diff to QTextEdit::setHtml() parses, styles and lays it
    // all out on the UI thread, freezing it for many seconds (issue #187; the
    // Branches diff caps the same way). Past a sane size, show the changed-files
    // list with a notice instead of the full table.
    constexpr int kMaxDiffHtmlChars = 1'000'000;
    const QString shown =
        html.size() > kMaxDiffHtmlChars
            ? QStringLiteral(
                  "<p style='color:#d29922'>This diff is too large to render here "
                  "(%1 file%2). Use the changed-files list, or view the branch in "
                  "your editor.</p>")
                  .arg(files.size())
                  .arg(files.size() == 1 ? "" : "s")
            : (html.isEmpty()
                   ? QStringLiteral("<p style='color:#8b949e'>No changes yet.</p>")
                   : html);
    // Re-running setHtml when the rendered diff is byte-identical to what's
    // already on screen just re-freezes the UI for no visible change (this fires
    // on every transcript burst while an agent streams). Skip it when unchanged;
    // the file list below still rebuilds so committed/uncommitted markers stay
    // current.
    if (sessionId != m_agentDiffRenderedSession || shown != m_agentDiffLastHtml) {
        setDiffHtml(m_agentDiffView, shown);
        m_agentDiffLastHtml = shown;
    }

    // Which of these changes are still sitting in the working tree (not yet in any
    // commit on this branch): files in this set get a "●" marker so the panel
    // distinguishes work the agent has committed from work it hasn't (adhoc #260).
    // Pre-gathered async (one `git status --porcelain`) by scheduleAgentFilesDiff.
    const QSet<QString> &uncommitted = probe.uncommitted;

    if (m_agentFilesList) {
        QSignalBlocker block(m_agentFilesList);
        m_agentFilesList->clear();
        for (const DiffFileEntry &f : files) {
            const QString name = f.path.section(QLatin1Char('/'), -1);
            const bool isUncommitted = uncommitted.contains(f.path);
            auto *item = new QListWidgetItem(
                QString::fromUtf8("%1   +%2 \xE2\x88\x92%3%4")
                    .arg(name, QString::number(f.adds), QString::number(f.dels),
                         isUncommitted ? QString::fromUtf8("  \xE2\x97\x8F")
                                       : QString()));
            QColor tint("#d29922");
            QString icon = "file-diff";
            if (f.status == QLatin1String("added")) { icon = "diff"; tint = QColor("#3fb950"); }
            else if (f.status == QLatin1String("deleted")) { icon = "trash"; tint = QColor("#f85149"); }
            item->setIcon(themedOcticon(icon, tint, 14));
            const QString abs = dir.isEmpty() ? f.path : QDir(dir).filePath(f.path);
            item->setData(Qt::UserRole, abs);          // open on activate
            item->setData(Qt::UserRole + 1, f.anchor); // scroll diff on select
            item->setToolTip(
                isUncommitted
                    ? QString::fromUtf8("%1 \xC2\xB7 %2 \xC2\xB7 uncommitted")
                          .arg(f.status, f.path)
                    : QString::fromUtf8("%1 \xC2\xB7 %2").arg(f.status, f.path));
            m_agentFilesList->addItem(item);
        }
        fitFileListToWidestEntry(m_agentFilesList);
    }
    if (!m_agentDiffNav && m_agentFilesList)
        m_agentDiffNav = new DiffFileNavigator(m_agentDiffView, m_agentFilesList,
                                               Qt::UserRole + 1, this);
    if (m_agentDiffNav)
        m_agentDiffNav->rebuild(files, m_diffFontPt);

    const int n = files.size();
    if (m_agentDetailTabs && m_agentFilesTabIndex >= 0)
        m_agentDetailTabs->setTabText(
            m_agentFilesTabIndex,
            n > 0 ? QStringLiteral("Files changed (%1)").arg(n)
                  : QStringLiteral("Files changed"));
    if (m_agentFilesChangedSummary) {
        // Lead with which branch is merging into which — "<head> → <base>" — so the
        // direction of the change is explicit (adhoc #20). Then the (now merge-base-
        // accurate) file count and the wider "what's going on" picture the bare count
        // hid: total +/- lines, how many commits this branch adds, and how far it
        // trails the base branch (issue #183). Each clause is omitted when it's
        // zero/unknown so a clean session reads tidily.
        int adds = 0, dels = 0;
        for (const DiffFileEntry &f : files) { adds += f.adds; dels += f.dels; }
        QStringList parts;
        const AgentSession *summarySession = findAgentSession(sessionId);
        const QString headBranch =
            summarySession ? summarySession->branchName : QString();
        const QString baseBranch = sessionBaseBranch(sessionId);
        if (!headBranch.isEmpty() && !baseBranch.isEmpty() && headBranch != baseBranch)
            parts << QString::fromUtf8("%1 \xE2\x86\x92 %2").arg(headBranch, baseBranch);
        parts << QStringLiteral("%1 file%2 changed").arg(n).arg(n == 1 ? "" : "s");
        if (adds > 0 || dels > 0)
            parts << QString::fromUtf8("+%1 \xE2\x88\x92%2").arg(adds).arg(dels);
        // Commits the branch carries (ahead) and how far it trails base (behind),
        // measured against the base branch's live tip. Both pre-gathered async
        // (commit list ⇒ ahead; rev-list --count ⇒ behind).
        const int ahead = probe.commitLines.size();
        if (ahead > 0)
            parts << QStringLiteral("%1 commit%2").arg(ahead).arg(ahead == 1 ? "" : "s");
        if (probe.behind > 0 && !baseBranch.isEmpty())
            parts << QStringLiteral("%1 behind %2").arg(probe.behind).arg(baseBranch);
        m_agentFilesChangedSummary->setText(parts.join(QString::fromUtf8("  \xC2\xB7  ")));
    }

    // The commits this branch adds on top of its base, newest first — the browsable
    // form of the summary's "N commits" (adhoc #260). Hidden entirely when the
    // branch is even with its base so a clean session stays uncluttered.
    if (m_agentCommitsList && m_agentCommitsHeading) {
        m_agentCommitsList->clear();
        int commitCount = 0;
        for (const QString &line : probe.commitLines) {
            auto *item = new QListWidgetItem(line.trimmed());
            item->setIcon(themedOcticon(QStringLiteral("git-commit"),
                                        QColor("#8b949e"), 14));
            m_agentCommitsList->addItem(item);
            ++commitCount;
        }
        const bool any = commitCount > 0;
        m_agentCommitsHeading->setVisible(any);
        m_agentCommitsList->setVisible(any);
        if (any)
            m_agentCommitsHeading->setText(
                QStringLiteral("Commits (%1)").arg(commitCount));
    }

    // The rich icon list is now on screen; refreshAgentFilesPanel() can stop
    // redrawing the plain placeholder over it on every turn (adhoc #260).
    m_agentDiffRenderedSession = sessionId;
}

// Enable the per-session worktree actions (merge / update / delete) only for a
// real feature-branch worktree that exists on disk — never the default branch or
// the primary checkout. Mirrors showWorktreeDiff's button gating.
void MainWindow::updateAgentFilesTabState(int sessionId)
{
    AgentSession *s = findAgentSession(sessionId);
    QString branch, wt, repoLocal;
    if (s) {
        branch = s->branchName;
        const int ri = repoIndexFor(s->owner, s->name);
        if (ri >= 0) {
            repoLocal = m_repositories.at(ri).localPath;
            if (!branch.isEmpty())
                wt = cachedSessionWorktree(sessionId, repoLocal, branch);
        }
    }
    const QString base = repoDefaultBranch(repoBranches());
    const bool onDisk = !wt.isEmpty() && QDir(wt).exists();
    const bool isMain = !wt.isEmpty() && !repoLocal.isEmpty() &&
                        QDir(wt).absolutePath() == QDir(repoLocal).absolutePath();
    const bool feature = !branch.isEmpty() && branch != base && !isMain;
    if (m_agentMergeButton)
        m_agentMergeButton->setEnabled(feature && repoHasWorkingTree());
    if (m_agentMergeDeleteButton)
        m_agentMergeDeleteButton->setEnabled(feature && repoHasWorkingTree());
    if (m_agentUpdateButton)
        m_agentUpdateButton->setEnabled(feature && onDisk);
    if (m_agentWtDeleteButton)
        m_agentWtDeleteButton->setEnabled(feature && onDisk);
}

// Open a ForkMesh pull request from the session's changes (diff since baseRef),
// mirroring onAgentFinished's PR path but for the live-tree transcript session.
void MainWindow::maybeCreatePullForStreamSession(int sessionId)
{
    AgentSession *s = findAgentSession(sessionId);
    if (!s || !s->createPr || s->prNumber > 0 || !m_agentStore || s->baseRef.isEmpty())
        return;
    if (repoIndexFor(s->owner, s->name) < 0)
        return;
    const QString workdir = sessionWorkdir(sessionId); // diff in the worktree

    QString patch;
    {
        QProcess git;
        git.setWorkingDirectory(workdir);
        git.start(QStringLiteral("git"),
                  {QStringLiteral("diff"), QStringLiteral("--binary"), s->baseRef});
        if (git.waitForFinished(8000) && git.exitCode() == 0)
            patch = QString::fromUtf8(git.readAllStandardOutput());
    }
    if (patch.trimmed().isEmpty()) {
        m_agentStore->appendLog(
            *s, QStringLiteral("==> No code changes; no pull request created.\n"));
        return;
    }
    // The committed series base..HEAD as a format-patch mbox, so a mirror-node
    // submission can be replayed by the owner with `git am` and keep each commit's
    // author/message. Empty when the agent left the work uncommitted (the flat
    // patch above still carries it; the owner synthesizes a single-commit mbox).
    QString commits;
    {
        QProcess git;
        git.setWorkingDirectory(workdir);
        git.start(QStringLiteral("git"),
                  {QStringLiteral("format-patch"), QStringLiteral("--stdout"), s->baseRef});
        if (git.waitForFinished(8000) && git.exitCode() == 0)
            commits = QString::fromUtf8(git.readAllStandardOutput());
    }
    m_agentStore->writePatch(*s, patch);
    landAgentPullForSession(*s, patch, commits);
}

void MainWindow::landAgentPullForSession(AgentSession &session, const QString &patch,
                                         const QString &commits)
{
    if (!m_agentStore || patch.trimmed().isEmpty())
        return;
    const int ri = repoIndexFor(session.owner, session.name);
    if (ri < 0)
        return;
    const RepositoryRecord repo = m_repositories.at(ri);
    // Issue-less ad-hoc runs (issue #273) have no issue number to cite, so title
    // and body read off the session's prompt-derived title instead.
    const QString prTitle =
        session.issueNumber > 0
            ? QStringLiteral("Agent: issue #%1 %2").arg(session.issueNumber).arg(session.issueTitle)
            : QStringLiteral("Agent: %1").arg(session.issueTitle);
    const QString prBody =
        session.issueNumber > 0
            ? QStringLiteral("Created from a %1 session for issue #%2.")
                  .arg(agentProviderName(session.provider))
                  .arg(session.issueNumber)
            : QStringLiteral("Created from a %1 agent session.")
                  .arg(agentProviderName(session.provider));
    const QString base = session.baseBranch.isEmpty() ? session.baseRef : session.baseBranch;
    PullStore store(repo.localPath, repo.mirrorPath, &m_profileIdentity, m_userName);
    if (store.canWrite()) {
        // Source of truth: commit the pull request straight into the local repo.
        QString error;
        const int pr = store.createPull(prTitle, prBody, base, session.branchName, patch,
                                        commits, /*branchBacked=*/true, &error);
        if (pr > 0) {
            session.prNumber = pr;
            m_agentStore->saveSession(session);
            m_agentStore->appendLog(
                session, QStringLiteral("==> Created pull request #%1.\n").arg(pr));
            linkAgentPullToIssue(session, pr); // record it in the issue's Development section
            if (ri == m_repoDetailIndex)
                reloadPulls();
        } else {
            m_agentStore->appendLog(
                session, QStringLiteral("!! Could not create pull request: %1\n").arg(error));
        }
        return;
    }
    // Mirror node (not the source of truth): we can't write the owner's repo, so
    // sign the PR and deliver it to the owner's relay inbox, which queues it for
    // the source of truth even if that node is offline (adhoc #25). Label the head
    // with this node's name so the owner can tell which mirror it came from. The
    // PR's number is assigned by the owner and syncs back later, so none is
    // recorded on the session here.
    PullRequest pr;
    pr.title = prTitle;
    pr.description = prBody;
    pr.base = base;
    pr.head = session.branchName;
    pr.patch = patch;
    pr.commits = commits;
    const QString nodeName = accountNameFromInput(m_userName, QString());
    if (!nodeName.isEmpty() && !pr.head.contains(QLatin1Char(':')))
        pr.head = nodeName + QLatin1Char(':') + pr.head;
    submitPullToInbox(store.makeSignedPull(pr), repo, /*quiet=*/true);
    m_agentStore->appendLog(
        session, QStringLiteral("==> Sent pull request to %1/%2 (source of truth).\n")
                     .arg(repo.owner, repo.name));
}

// Release the temp worktree a stream session ran in once the run is over. The
// worktree at /tmp/forkmesh-worktrees/issue-N-sSID holds its branch checked out,
// so leaving it behind makes any later `git checkout <branch>` (e.g. opening the
// PR locally) fail with "already used by worktree at …". Removing the worktree
// frees the branch while keeping the branch ref, so the PR still resolves.
void MainWindow::cleanupStreamWorktree(int sessionId)
{
    m_sessionWorkdirCache.remove(sessionId); // worktree about to vanish — don't cache it
    const QString wtPath = m_streamWorktree.take(sessionId);
    if (wtPath.isEmpty())
        return;
    QString repoPath;
    if (const AgentSession *s = findAgentSession(sessionId)) {
        const int ri = repoIndexFor(s->owner, s->name);
        if (ri >= 0)
            repoPath = repoAgentGitDir(m_repositories.at(ri)); // mirror or checkout
    }
    // Removing a worktree shells out to `git worktree remove`/`prune` and then
    // recursively deletes a full source checkout — slow enough to freeze the UI for
    // a moment when a session is deleted. The paths are captured above on the UI
    // thread; the filesystem/git work touches nothing shared, so hand it to a
    // detached worker that cleans itself up.
    QThread *worker = QThread::create([wtPath, repoPath]() {
        if (!repoPath.isEmpty()) {
            QProcess::execute(QStringLiteral("git"),
                              {QStringLiteral("-C"), repoPath, QStringLiteral("worktree"),
                               QStringLiteral("remove"), QStringLiteral("--force"), wtPath});
        }
        QDir(wtPath).removeRecursively(); // fall back to deleting the folder either way
        if (!repoPath.isEmpty()) {
            QProcess::execute(QStringLiteral("git"),
                              {QStringLiteral("-C"), repoPath, QStringLiteral("worktree"),
                               QStringLiteral("prune")});
        }
    });
    connect(worker, &QThread::finished, worker, &QObject::deleteLater);
    worker->start();
}

// Append to the raw-output edit only when it's the surface actually on screen.
// While the rich transcript is shown the m_streamRaw buffer already captures the
// text, and showAgentRawOutput() rebuilds the edit from it on demand — streaming
// line-by-line into a hidden QPlainTextEdit still forces a full text layout per
// line, and shaping large JSON lines stalled the UI for seconds during bursts.
void MainWindow::appendAgentRawLog(const QString &text)
{
    if (!m_agentLog || !m_agentOutputStack ||
        m_agentOutputStack->currentWidget() != m_agentLog)
        return;
    QScrollBar *sb = m_agentLog->verticalScrollBar();
    const bool atBottom = !sb || sb->value() >= sb->maximum() - 4;
    // Insert through a local cursor (not moveCursor) so we skip the per-line
    // ensureCursorVisible → cursorRect layout the widget API forces.
    QTextCursor cursor(m_agentLog->document());
    cursor.movePosition(QTextCursor::End);
    cursor.insertText(text);
    m_agentLogSession = -1; // appended out-of-band; the dedup tracker is now stale
    if (atBottom && sb)
        sb->setValue(sb->maximum()); // keep following the tail only if already pinned
}

// Switch the agent output to the raw log, rebuilding it from the live buffer
// first: while the transcript is shown appendAgentRawLog() skips the edit, so it
// can be behind. setPlainText() lays out lazily (one pass), unlike the per-line
// inserts that caused the stalls.
void MainWindow::showAgentRawOutput()
{
    if (!m_agentOutputStack || !m_agentLog)
        return;
    if (m_streamRaw.contains(m_selectedAgentSessionId))
        m_agentLog->setPlainText(m_streamRaw.value(m_selectedAgentSessionId));
    m_agentLogSession = -1; // set out-of-band; the dedup tracker is now stale
    m_agentOutputStack->setCurrentWidget(m_agentLog);
    m_agentLog->moveCursor(QTextCursor::End); // always land on the tail when shown
}

void MainWindow::onAgentLog(int sessionId, const QString &text)
{
    noteAgentActivity(sessionId, text.size()); // pulse the night-rider light
    if (sessionId != m_selectedAgentSessionId || !m_agentLog)
        return;
    m_agentLog->moveCursor(QTextCursor::End);
    m_agentLog->insertPlainText(text);
    if (!text.endsWith(QLatin1Char('\n')))
        m_agentLog->insertPlainText(QStringLiteral("\n"));
    m_agentLog->moveCursor(QTextCursor::End);
    m_agentLogSession = -1; // appended out-of-band; the dedup tracker is now stale
    // Refresh the live traffic graphic when a network marker streams in — read
    // and scanned off-thread (the log grows to megabytes over a run; re-reading
    // it on the GUI thread for every [net] line was a steady hitch), and only
    // one scan in flight at a time so a marker burst costs one read.
    if (text.contains(QLatin1String("[net]")) && m_agentStore &&
        !m_agentNetScanInFlight) {
        if (AgentSession *session = findAgentSession(sessionId)) {
            m_agentNetScanInFlight = true;
            const AgentSession snapshot = *session;
            AgentStore *store = m_agentStore;
            const QString status = session->status;
            runOffThread<AgentLogScan>(
                [store, snapshot] { return scanAgentLog(store->readLog(snapshot)); },
                [this, sessionId, status](const AgentLogScan &scan) {
                    m_agentNetScanInFlight = false;
                    if (sessionId == m_selectedAgentSessionId)
                        applyAgentNetworkPanel(scan, status);
                });
        }
    }
}

void MainWindow::onAgentStatusChanged(int sessionId, const QString &)
{
    reloadAgents();
    if (sessionId == m_selectedAgentSessionId)
        showAgentSession(sessionId);
    refreshIssueList();
    // Keep the Branches tab's per-branch agent-status icon (adhoc #251) current as
    // the run progresses — but only while that tab is on screen, since rebuilding
    // it probes git for every branch (ahead/behind + conflicts).
    if (m_branchesTable && m_repoDetailStack &&
        m_repoDetailStack->currentIndex() == 0 && m_overviewBodyStack &&
        m_overviewBodyStack->currentIndex() == 2)
        loadBranchesPanel();
}

void MainWindow::onAgentNeedsAttention(int sessionId, const QString &message)
{
    // Bring the session into view and surface the actionable guidance so the run
    // doesn't just appear stuck while the CLI waits on sign-in / credits.
    switchToAgentsTab(sessionId);
    flashMessage(message, true);
    QMessageBox::warning(this, QStringLiteral("Agent needs attention"), message);
}

void MainWindow::onAgentFinished(int sessionId, bool ok)
{
    reloadAgents();
    AgentSession *session = findAgentSession(sessionId);
    // The provider is needed after the reloadAgents() below, which rebuilds
    // m_agentSessions and leaves `session` dangling — capture it now.
    const QString provider = session ? session->provider : QString();
    // Guard against opening a second PR for the same session: onAgentFinished can
    // be reached more than once (signal re-fire, requeue), and the session may
    // already carry a prNumber from a previous pass.
    if (ok && session && session->createPr && session->prNumber <= 0 && m_agentStore) {
        // Lands locally when we're the source of truth, or submits to the owner's
        // inbox when this is a mirror node (adhoc #25). The headless runner path has
        // no format-patch mbox handy, so the owner synthesizes one from the patch.
        const QString patch = m_agentStore->readPatch(*session);
        if (!patch.trimmed().isEmpty())
            landAgentPullForSession(*session, patch, QString());
    }
    reloadAgents(); // rebuilds m_agentSessions; `session` is dangling after this
    if (sessionId == m_selectedAgentSessionId)
        showAgentSession(sessionId);
    refreshIssueList();
    // Auto-refresh usage/spend after a session completes.
    if (!provider.isEmpty()) {
        if (agentIsClaudeProvider(provider))
            refreshClaudeSpend();
        else
            testOpenAiAgentKey();
    }
    processAgentQueue();
    looperOnSessionFinished(sessionId); // adhoc #92: chain to the next open issue
}

void MainWindow::updateAgentActionState()
{
    const bool selected = m_selectedAgentSessionId > 0;
    // External (watch-only) rows carry negative synthetic ids, so `selected` is
    // false for them — but Delete still applies: it kills the real CLI process
    // (if still running) and drops the local mirror. See deleteExternalSession.
    const bool externalSelected = isExternalSession(m_selectedAgentSessionId);
    // A session is "running" if a headless AgentRunner is driving it, OR a live
    // Claude Code stream-json session (no runner) is still attached.
    ClaudeStreamSession *stream =
        selected ? m_streamSessions.value(m_selectedAgentSessionId) : nullptr;
    const bool running = selected && (runnerForSession(m_selectedAgentSessionId) ||
                                      (stream && stream->running()));
    // External rows have negative ids (so `running` is false), but a live one can
    // still be stopped by signalling its CLI process. See stopExternalSession.
    const bool externalRunning =
        externalSelected && m_externalSurfaced.contains(m_selectedAgentSessionId) &&
        externalIsLive(m_externalSurfaced.value(m_selectedAgentSessionId).uuid);
    if (m_agentStopButton)
        m_agentStopButton->setEnabled(running || externalRunning);
    AgentSession *session = selected ? findAgentSession(m_selectedAgentSessionId)
                                     : nullptr;
    // Block deleting the session whose working-tree git-am the in-flight AI fix is
    // still holding open.
    const bool aiFixBusy = m_aiFix && m_aiFix->sessionId == m_selectedAgentSessionId;
    if (m_agentDeleteButton)
        m_agentDeleteButton->setEnabled((selected || externalSelected) && !aiFixBusy);
    // "Delete all" also nukes the worktree + branch, so it only applies to a real
    // stored session that has a branch (not external watch-only rows).
    if (m_agentDeleteAllButton)
        m_agentDeleteAllButton->setEnabled(
            selected && !aiFixBusy && session && !session->branchName.isEmpty()
            && !isExternalSession(m_selectedAgentSessionId));
}

