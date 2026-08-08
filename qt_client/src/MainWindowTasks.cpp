// Organization-private task catalog and management surface.

#include "MainWindow.h"
#include "MainWindowInternal.h"

#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QJsonDocument>
#include <QMessageBox>
#include <QNetworkReply>
#include <QPainter>
#include <QPlainTextEdit>
#include <QRegularExpression>
#include <QSpinBox>
#include <QSplitter>
#include <QStyledItemDelegate>
#include <QTextBrowser>

using namespace forkmesh::ui;

namespace {

// Rows painted per page. The relay returns the whole organization catalog
// (MAX_TASKS in world_office_tasks.py), so the table pages through it instead
// of stopping at the first hundred rows.
constexpr int kOrganizationTaskPageSize = 100;

QString taskText(const QJsonObject &task, const QString &field)
{
    return task.value(field).toString().trimmed();
}

QString taskErrorText(const QJsonObject &payload, const QString &fallback)
{
    QString error = payload.value(QStringLiteral("error")).toString().trimmed();
    if (error.isEmpty())
        return fallback;
    error.replace(QLatin1Char('_'), QLatin1Char(' '));
    return error;
}

// The canonical prefix this desktop signs with its account key for one task
// request when it holds no account session token. Empty when the relay accepts
// no key-signed form of the request: editing, timers, and QA verdicts
// deliberately still require a real session. `resource` receives the task id
// for a proof that names one. Must stay in lockstep with
// _org_task_signed_session in the worker's entry.py.
QString organizationTaskProof(const QByteArray &method, const QString &path,
                              QString *resource)
{
    static const QRegularExpression completeRe(
        QStringLiteral("^/api/tasks/([a-f0-9]{32})/complete/?$"));
    static const QRegularExpression itemRe(
        QStringLiteral("^/api/tasks/([a-f0-9]{32})/?$"));
    const bool collection = path == QLatin1String("/api/tasks") ||
                            path == QLatin1String("/api/tasks/");
    if (method == QByteArrayLiteral("GET"))
        return collection ? kOrgTaskListProof : QString();
    if (method == QByteArrayLiteral("DELETE")) {
        // Delete is a manage-permission action, and the relay applies that check
        // to a signed caller exactly as to a session one — so the account key
        // this install already signs the board read with is enough (adhoc
        // #1426). Before, Delete asked an operator who was demonstrably signed
        // in to sign in again.
        const QRegularExpressionMatch item = itemRe.match(path);
        if (!item.hasMatch())
            return QString();
        *resource = item.captured(1);
        return kOrgTaskDeleteProof;
    }
    if (method != QByteArrayLiteral("POST"))
        return QString();
    if (collection)
        return kOrgTaskOpenProof;
    const QRegularExpressionMatch complete = completeRe.match(path);
    if (!complete.hasMatch())
        return QString();
    *resource = complete.captured(1);
    return kOrgTaskCompleteProof;
}

// Tasks that are not finished — the number the rail badge shows. Counted
// exactly the way applyOrganizationTasks counts it while filling the table, so
// a badge refreshed without the page built agrees with one refreshed with it.
int openOrganizationTaskCount(const QJsonArray &tasks)
{
    int open = 0;
    for (const QJsonValue &value : tasks) {
        const QJsonObject task = value.toObject();
        const bool done =
            task.value(QStringLiteral("completedAt")).toDouble() > 0 ||
            taskText(task, QStringLiteral("status")) == QLatin1String("done");
        if (!done)
            ++open;
    }
    return open;
}

QString taskTimestamp(qint64 milliseconds)
{
    if (milliseconds <= 0)
        return QString::fromUtf8("\xE2\x80\x94");
    return QDateTime::fromMSecsSinceEpoch(milliseconds)
        .toLocalTime()
        .toString(QStringLiteral("yyyy-MM-dd HH:mm"));
}

// Keep the dense list useful at a glance: the full timestamp remains in the
// hover card, while the row itself says how long ago the task was created.
QString taskCreatedAgo(qint64 milliseconds)
{
    const QString age = formatShortRelativeTime(milliseconds / 1000);
    if (age.isEmpty())
        return QString::fromUtf8("\xE2\x80\x94");
    return age == QLatin1String("now") ? age
                                        : age + QStringLiteral(" ago");
}

QString taskAssigneeLabel(const QJsonObject &task)
{
    const QString kind = taskText(task, QStringLiteral("assigneeKind"));
    if (kind == QLatin1String("agent"))
        return QStringLiteral("Bot");
    if (kind == QLatin1String("unassigned"))
        return QStringLiteral("Unassigned");
    const QString assignee = taskText(task, QStringLiteral("assignee"));
    return assignee.isEmpty() ? QStringLiteral("Unassigned") : assignee;
}

// ---------------------------------------------------------------------------
// The task list paints one row as an icon strip plus a title — no header, no
// grid, no columns (adhoc #56). Everything the old Priority/Status/Department/
// Repository/Assignee/QA columns spelled out rides on the single cell as data
// roles, and the delegate below turns each one into a glyph. Qt::UserRole stays
// the task's absolute catalog index, which is what selectedOrganizationTask()
// resolves against.
constexpr int kTaskStatusRole = Qt::UserRole + 1;
constexpr int kTaskDepartmentRole = Qt::UserRole + 2;
constexpr int kTaskRepositoryRole = Qt::UserRole + 3;
constexpr int kTaskAssigneeKindRole = Qt::UserRole + 4;
constexpr int kTaskAssigneeRole = Qt::UserRole + 5;
constexpr int kTaskQaRole = Qt::UserRole + 6;
constexpr int kTaskQaRequestedRole = Qt::UserRole + 7;
constexpr int kTaskPriorityRole = Qt::UserRole + 8;
constexpr int kTaskCreatedAtRole = Qt::UserRole + 9;

// Icon strip geometry, in logical pixels. The slots are fixed so the titles all
// start at the same x even when a task has no repository or no QA verdict.
constexpr int kTaskRowHeight = 34;
constexpr int kTaskGlyphSize = 16;
constexpr int kTaskAvatarSize = 18;
constexpr int kTaskSlotWidth = 24;
constexpr int kTaskStripLeft = 10;
constexpr int kTaskPriorityWidth = 26;
constexpr int kTaskTitleGap = 10;

// "done" / "active" / "queued" / "ready" — the same four states the old Status
// column spelled out, and the same test the badge counts with.
QString taskStatusKey(const QJsonObject &task)
{
    if (task.value(QStringLiteral("completedAt")).toDouble() > 0 ||
        taskText(task, QStringLiteral("status")) == QLatin1String("done"))
        return QStringLiteral("done");
    if (taskText(task, QStringLiteral("status")) == QLatin1String("active"))
        return QStringLiteral("active");
    if (taskText(task, QStringLiteral("assigneeKind")) ==
            QLatin1String("agent") &&
        !taskText(task, QStringLiteral("agentSessionId")).isEmpty())
        return QStringLiteral("queued");
    return QStringLiteral("ready");
}

QString taskStatusLabel(const QString &key)
{
    if (key == QLatin1String("done"))
        return QStringLiteral("Done");
    if (key == QLatin1String("active"))
        return QStringLiteral("In progress");
    if (key == QLatin1String("queued"))
        return QStringLiteral("Queued");
    return QStringLiteral("Ready");
}

QString taskStatusIcon(const QString &key)
{
    if (key == QLatin1String("done"))
        return QStringLiteral("check-circle");
    if (key == QLatin1String("active"))
        return QStringLiteral("sync");
    if (key == QLatin1String("queued"))
        return QStringLiteral("list-unordered");
    return QStringLiteral("issue-opened");
}

QColor taskStatusColor(const QString &key, bool dark)
{
    if (key == QLatin1String("done"))
        return QColor(dark ? "#3fb950" : "#1a7f37");
    if (key == QLatin1String("active"))
        return QColor(Theme::kRunning);
    if (key == QLatin1String("queued"))
        return QColor(dark ? "#e3b341" : "#9a6700");
    return QColor(dark ? "#8b949e" : "#656d76");
}

// A stable accent for a free-text name (department, repository) so the same
// department always reads in the same colour without a hand-maintained table.
QColor taskAccentColor(const QString &seed)
{
    if (seed.isEmpty())
        return QColor(Theme::kTextTertiary);
    uint hash = 2166136261u;
    for (const QChar &ch : seed)
        hash = (hash ^ uint(ch.unicode())) * 16777619u;
    return QColor(
        Theme::kSenderPalette[hash % uint(Theme::kSenderPaletteSize)]);
}

// Departments are organization-defined, so the known ones get a glyph that
// actually says something and anything new falls back to the generic tag.
QString taskDepartmentIcon(const QString &department)
{
    static const QHash<QString, QString> known{
        {QStringLiteral("engineering"), QStringLiteral("code")},
        {QStringLiteral("product-design"), QStringLiteral("pencil")},
        {QStringLiteral("design"), QStringLiteral("pencil")},
        {QStringLiteral("product"), QStringLiteral("package")},
        {QStringLiteral("marketing"), QStringLiteral("broadcast")},
        {QStringLiteral("community"), QStringLiteral("people")},
        {QStringLiteral("support"), QStringLiteral("comment")},
        {QStringLiteral("security"), QStringLiteral("lock")},
        {QStringLiteral("infrastructure"), QStringLiteral("server")},
        {QStringLiteral("operations"), QStringLiteral("workflow")},
        {QStringLiteral("quality-assurance"), QStringLiteral("shield-check")},
        {QStringLiteral("research"), QStringLiteral("graph")},
        {QStringLiteral("finance"), QStringLiteral("credit-card")},
        {QStringLiteral("general"), QStringLiteral("tag")},
    };
    return known.value(department.toLower(), QStringLiteral("tag"));
}

// The QA slot only paints when there is something to report: "unknown" with no
// review requested is the relay's default, i.e. nothing happened yet.
bool taskQaGlyph(const QString &status, bool requested, QString *icon,
                 QColor *color, bool dark)
{
    if (status == QLatin1String("passed")) {
        *icon = QStringLiteral("shield-check");
        *color = QColor(dark ? "#3fb950" : "#1a7f37");
        return true;
    }
    if (status == QLatin1String("failed")) {
        *icon = QStringLiteral("circle-slash");
        *color = QColor(dark ? "#f85149" : "#cf222e");
        return true;
    }
    if (requested) {
        *icon = QStringLiteral("eye");
        *color = QColor(dark ? "#e3b341" : "#9a6700");
        return true;
    }
    return false;
}

QString taskTooltipRow(const QString &symbol, const QColor &symbolColor,
                       const QString &label, const QString &value)
{
    const QString shown = value.trimmed().isEmpty()
                              ? QString::fromUtf8("\xE2\x80\x94")
                              : value.trimmed();
    return QStringLiteral(
               "<tr>"
               "<td style='padding:2px 7px 2px 0;color:%1;font-weight:700'>%2</td>"
               "<td style='padding:2px 10px 2px 0;color:#57606a'>%3</td>"
               "<td style='padding:2px 0;color:#1f2328'>%4</td>"
               "</tr>")
        .arg(symbolColor.name(), symbol.toHtmlEscaped(), label.toHtmlEscaped(),
             shown.toHtmlEscaped());
}

// The app-wide tooltip follows the selected theme. This particular hover card
// is a compact task detail surface, so give its content a consistently light
// canvas and pair every value with the same visual cue used by the row strip.
QString taskRowTooltipHtml(const QJsonObject &task, const QString &statusKey,
                           const QString &department,
                           const QString &repository,
                           const QString &qaStatus, bool qaRequested)
{
    const QString title = taskText(task, QStringLiteral("title"));
    QString qaSymbol = QStringLiteral("?");
    QColor qaColor(QStringLiteral("#656d76"));
    if (qaStatus == QLatin1String("passed")) {
        qaSymbol = QString::fromUtf8("\xE2\x9C\x93");
        qaColor = QColor(QStringLiteral("#1a7f37"));
    } else if (qaStatus == QLatin1String("failed")) {
        qaSymbol = QString::fromUtf8("\xC3\x97");
        qaColor = QColor(QStringLiteral("#cf222e"));
    } else if (qaRequested) {
        qaSymbol = QString::fromUtf8("\xE2\x97\x89");
        qaColor = QColor(QStringLiteral("#9a6700"));
    }

    const qint64 createdAt =
        qint64(task.value(QStringLiteral("createdAt")).toDouble());
    const QString created = createdAt > 0
                                ? QStringLiteral("%1 \xC2\xB7 %2")
                                      .arg(taskCreatedAgo(createdAt),
                                           taskTimestamp(createdAt))
                                : QString::fromUtf8("\xE2\x80\x94");
    QString html = QStringLiteral(
        "<table cellspacing='0' cellpadding='0' bgcolor='#f6f8fa' "
        "style='background-color:#f6f8fa;color:#1f2328;"
        "border:1px solid #d0d7de;padding:8px'>"
        "<tr><td colspan='3' style='padding:0 0 6px 0;color:#1f2328;"
        "font-weight:700'>%1</td></tr>")
                       .arg(title.toHtmlEscaped());
    html += taskTooltipRow(QString::fromUtf8("\xE2\x97\x8F"),
                           taskStatusColor(statusKey, false),
                           QStringLiteral("Status"),
                           taskStatusLabel(statusKey));
    html += taskTooltipRow(QStringLiteral("!"), QColor(QStringLiteral("#9a6700")),
                           QStringLiteral("Priority"),
                           QString::number(task.value(QStringLiteral("priority")).toInt()));
    html += taskTooltipRow(QStringLiteral("#"), taskAccentColor(department),
                           QStringLiteral("Department"), department);
    html += taskTooltipRow(QString::fromUtf8("\xE2\x97\x88"),
                           QColor(QStringLiteral("#0969da")),
                           QStringLiteral("Repository"), repository);
    html += taskTooltipRow(QStringLiteral("@"), QColor(QStringLiteral("#8250df")),
                           QStringLiteral("Assignee"), taskAssigneeLabel(task));
    html += taskTooltipRow(qaSymbol, qaColor, QStringLiteral("QA"),
                           qaStatus.isEmpty() ? QStringLiteral("unknown")
                                              : qaStatus);
    html += taskTooltipRow(QString::fromUtf8("\xE2\x97\xB7"),
                           QColor(QStringLiteral("#57606a")),
                           QStringLiteral("Created"), created);
    return html + QStringLiteral("</table>");
}

// Repository and member marks are identicons derived from the name, so a repo
// or a teammate keeps the same logo everywhere in the app. Generating one means
// rasterising a PNG, and paint() runs on every repaint of every visible row —
// hence the cache, keyed on the device pixel ratio like every other icon raster
// in this app.
QPixmap taskIdenticon(const QString &seed, int side, qreal radiusRatio)
{
    static QHash<QString, QPixmap> cache;
    const QString key = seed + QLatin1Char('|') + QString::number(side) +
                        QLatin1Char('|') + QString::number(radiusRatio) +
                        QLatin1Char('|') +
                        QString::number(iconDevicePixelRatio());
    const auto cached = cache.constFind(key);
    if (cached != cache.constEnd())
        return cached.value();
    const QPixmap pixmap =
        roundedAvatar(forkMeshAvatarPng(seed.toLower()), side, radiusRatio);
    cache.insert(key, pixmap);
    return pixmap;
}

// One row: [priority] [status] [department] [repo logo] [assignee] [QA] Title.
// Painted rather than built from cell widgets because the list pages a hundred
// rows at a time and six widgets a row would be six hundred of them.
class OrganizationTaskRowDelegate : public QStyledItemDelegate
{
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    QSize sizeHint(const QStyleOptionViewItem &option,
                   const QModelIndex &index) const override
    {
        QSize size = QStyledItemDelegate::sizeHint(option, index);
        size.setHeight(qMax(size.height(), kTaskRowHeight));
        return size;
    }

    void paint(QPainter *painter, const QStyleOptionViewItem &option,
               const QModelIndex &index) const override
    {
        const bool dark = currentThemeIsDark();
        const bool selected = option.state.testFlag(QStyle::State_Selected);
        const bool hovered = option.state.testFlag(QStyle::State_MouseOver);
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing);
        painter->setRenderHint(QPainter::SmoothPixmapTransform);

        // No grid, no alternating bands: the only fill a row ever gets is its
        // own selected/hovered pill.
        if (selected || hovered) {
            painter->setPen(Qt::NoPen);
            painter->setBrush(
                selected ? QColor(dark ? "#1f6feb" : "#0969da")
                         : QColor(dark ? "#161b22" : "#f6f8fa"));
            painter->drawRoundedRect(option.rect.adjusted(2, 2, -2, -2), 6, 6);
        }

        const QString statusKey = index.data(kTaskStatusRole).toString();
        const QString department = index.data(kTaskDepartmentRole).toString();
        const QString repository = index.data(kTaskRepositoryRole).toString();
        const QString assigneeKind =
            index.data(kTaskAssigneeKindRole).toString();
        const QString assignee = index.data(kTaskAssigneeRole).toString();
        const QString qa = index.data(kTaskQaRole).toString();
        const QString createdAgo =
            taskCreatedAgo(index.data(kTaskCreatedAtRole).toLongLong());
        const bool done = statusKey == QLatin1String("done");

        const QColor muted(selected ? QColor("#c8e1ff")
                                    : QColor(dark ? "#8b949e" : "#656d76"));
        int x = option.rect.left() + kTaskStripLeft;

        // Priority: the list is ordered by it, so the rank stays readable even
        // though the spin-box column is gone (edit it from Edit / double-click).
        painter->setPen(muted);
        QFont rankFont = option.font;
        rankFont.setPointSizeF(qMax(7.0, option.font.pointSizeF() - 2.0));
        painter->setFont(rankFont);
        painter->drawText(QRect(x, option.rect.top(), kTaskPriorityWidth,
                                option.rect.height()),
                          Qt::AlignVCenter | Qt::AlignRight,
                          index.data(kTaskPriorityRole).toString());
        painter->setFont(option.font);
        x += kTaskPriorityWidth + 8;

        auto centreY = [&option](int side) {
            return option.rect.top() + (option.rect.height() - side) / 2;
        };
        auto drawGlyph = [&](const QString &name, const QColor &colour) {
            if (name.isEmpty())
                return;
            painter->drawPixmap(x, centreY(kTaskGlyphSize),
                                tintedOcticonPixmap(name,
                                                    selected ? QColor("#ffffff")
                                                             : colour,
                                                    kTaskGlyphSize));
        };

        drawGlyph(taskStatusIcon(statusKey), taskStatusColor(statusKey, dark));
        x += kTaskSlotWidth;

        if (!department.isEmpty())
            drawGlyph(taskDepartmentIcon(department),
                      taskAccentColor(department));
        x += kTaskSlotWidth;

        // Repository logo: the identicon the rest of the app already shows for
        // that name, falling back to the plain repo glyph if it cannot render.
        if (!repository.isEmpty()) {
            const QPixmap logo = taskIdenticon(repository, kTaskAvatarSize,
                                               0.28);
            if (logo.isNull())
                drawGlyph(QStringLiteral("repo"), taskAccentColor(repository));
            else
                painter->drawPixmap(x, centreY(kTaskAvatarSize), logo);
        }
        x += kTaskSlotWidth;

        if (assigneeKind == QLatin1String("agent")) {
            drawGlyph(QStringLiteral("sparkle"), QColor(Theme::kGenie));
        } else if (!assignee.isEmpty()) {
            const QPixmap face = taskIdenticon(assignee, kTaskAvatarSize, 0.5);
            if (face.isNull())
                drawGlyph(QStringLiteral("person"), taskAccentColor(assignee));
            else
                painter->drawPixmap(x, centreY(kTaskAvatarSize), face);
        } else {
            drawGlyph(QStringLiteral("person"), muted);
        }
        x += kTaskSlotWidth;

        QString qaIcon;
        QColor qaColour;
        if (taskQaGlyph(qa, index.data(kTaskQaRequestedRole).toBool(), &qaIcon,
                        &qaColour, dark))
            drawGlyph(qaIcon, qaColour);
        x += kTaskSlotWidth + kTaskTitleGap;

        QFont ageFont = option.font;
        ageFont.setPointSizeF(qMax(7.0, option.font.pointSizeF() - 1.0));
        painter->setFont(ageFont);
        const int ageWidth = painter->fontMetrics().horizontalAdvance(createdAgo);
        const QRect ageRect(option.rect.right() - ageWidth - 8,
                            option.rect.top(), ageWidth,
                            option.rect.height());
        painter->setPen(muted);
        painter->drawText(ageRect, Qt::AlignVCenter | Qt::AlignRight, createdAgo);

        const QRect titleRect(x, option.rect.top(),
                              qMax(0, ageRect.left() - x - 8),
                              option.rect.height());
        painter->setFont(option.font);
        painter->setPen(selected ? QColor("#ffffff")
                                 : done ? muted
                                        : QColor(dark ? "#e6edf3" : "#1f2328"));
        if (done) {
            QFont struck = option.font;
            struck.setStrikeOut(true);
            painter->setFont(struck);
        }
        painter->drawText(
            titleRect, Qt::AlignVCenter | Qt::AlignLeft,
            painter->fontMetrics().elidedText(index.data(Qt::DisplayRole)
                                                  .toString(),
                                              Qt::ElideRight,
                                              qMax(0, titleRect.width())));
        painter->restore();
    }
};

QJsonObject selectedOrganizationTask(
    QTableWidget *table, const QJsonArray &tasks)
{
    if (!table || table->currentRow() < 0)
        return {};
    QTableWidgetItem *item = table->item(table->currentRow(), 0);
    if (!item)
        return {};
    const int index = item->data(Qt::UserRole).toInt();
    return index >= 0 && index < tasks.size() ? tasks.at(index).toObject()
                                              : QJsonObject();
}

// The task as an agent reads it: the "[task:<id>]" anchor line the run's logs
// and the org board both key off, the repository it belongs to, and the two
// free-text blocks that say what to do and how to check it. Shared by the
// "Add to prompt" button and the one-click "Start agent" launch so a prompt an
// operator edits by hand and one that goes straight to a run say the same thing.
QString organizationTaskPromptText(const QJsonObject &task)
{
    const QJsonObject qa = task.value(QStringLiteral("qa")).toObject();
    QStringList lines;
    lines << QStringLiteral("[task:%1] %2")
                 .arg(taskText(task, QStringLiteral("id")),
                      taskText(task, QStringLiteral("title")));
    const QString repository = taskText(task, QStringLiteral("repository"));
    if (!repository.isEmpty())
        lines << QStringLiteral("Repository: %1").arg(repository);
    const QString details = taskText(task, QStringLiteral("details"));
    if (!details.isEmpty())
        lines << QString() << details;
    const QString howToTest = taskText(qa, QStringLiteral("howToTest"));
    if (!howToTest.isEmpty())
        lines << QString() << QStringLiteral("How to test: %1").arg(howToTest);
    return lines.join(QLatin1Char('\n'));
}

// localAgentRun names the desktop session bound to this task, so the detail
// answers "who is working this?" for a run started from the prompt box as well
// as for one dispatched to a remote agent node.
QString taskDetailHtml(const QJsonObject &task, const QString &localAgentRun)
{
    if (task.isEmpty()) {
        return QStringLiteral(
            "<p style='color:#8b949e'>Select a task to inspect its private "
            "details and available actions.</p>");
    }
    const QJsonObject qa = task.value(QStringLiteral("qa")).toObject();
    auto row = [](const QString &label, const QString &value) {
        const QString shown =
            value.trimmed().isEmpty() ? QString::fromUtf8("\xE2\x80\x94")
                                      : value.toHtmlEscaped();
        return QStringLiteral(
                   "<tr><th align='left' style='padding:3px 12px 3px 0;"
                   "color:#8b949e'>%1</th><td style='padding:3px 0'>%2</td></tr>")
            .arg(label.toHtmlEscaped(), shown);
    };
    QString html =
        QStringLiteral("<h2 style='margin:0 0 8px 0'>%1</h2>")
            .arg(taskText(task, QStringLiteral("title")).toHtmlEscaped());
    html += QStringLiteral("<table cellspacing='0'>");
    html += row(QStringLiteral("Status"),
                taskText(task, QStringLiteral("status")));
    html += row(QStringLiteral("Priority"),
                QString::number(task.value(QStringLiteral("priority")).toInt()));
    html += row(QStringLiteral("Department"),
                taskText(task, QStringLiteral("department")));
    html += row(QStringLiteral("Team"),
                taskText(task, QStringLiteral("team")));
    html += row(QStringLiteral("Destination"),
                taskText(task, QStringLiteral("destination")));
    html += row(QStringLiteral("Repository"),
                taskText(task, QStringLiteral("repository")));
    html += row(QStringLiteral("Assignee"), taskAssigneeLabel(task));
    html += row(QStringLiteral("Created by"),
                taskText(task, QStringLiteral("createdBy")));
    html += row(QStringLiteral("Follows task"),
                taskText(task, QStringLiteral("parentTaskId")));
    // A remotely dispatched bot leaves its relay session id here; a run started
    // from this desktop's prompt box has no relay id at all, so its local
    // session is named alongside (or instead of) it.
    QString agentSession = taskText(task, QStringLiteral("agentSessionId"));
    if (!localAgentRun.isEmpty())
        agentSession = agentSession.isEmpty()
                           ? localAgentRun
                           : agentSession + QString::fromUtf8(" \xC2\xB7 ") +
                                 localAgentRun;
    html += row(QStringLiteral("Agent session"), agentSession);
    html += row(QStringLiteral("QA status"),
                taskText(qa, QStringLiteral("status")));
    html += row(QStringLiteral("QA reviewer"),
                taskText(qa, QStringLiteral("reviewer")));
    html += row(QStringLiteral("QA requested"),
                taskTimestamp(
                    qint64(qa.value(QStringLiteral("requestedAt")).toDouble())));
    html += row(QStringLiteral("Updated"),
                taskTimestamp(qint64(
                    task.value(QStringLiteral("updatedAt")).toDouble())));
    html += row(QStringLiteral("Task ID"),
                taskText(task, QStringLiteral("id")));
    html += QStringLiteral("</table>");
    auto block = [&html](const QString &heading, const QString &value) {
        html += QStringLiteral(
                    "<h3 style='margin:14px 0 4px 0'>%1</h3>"
                    "<p style='white-space:pre-wrap;margin:0'>%2</p>")
                    .arg(heading.toHtmlEscaped(),
                         (value.isEmpty()
                              ? QString::fromUtf8("\xE2\x80\x94")
                              : value.toHtmlEscaped()));
    };
    block(QStringLiteral("Details"),
          taskText(task, QStringLiteral("details")));
    block(QStringLiteral("How to test"),
          taskText(qa, QStringLiteral("howToTest")));
    block(QStringLiteral("Completion note"),
          taskText(task, QStringLiteral("completionNote")));
    return html;
}

} // namespace

QWidget *MainWindow::buildOrganizationTasksSection()
{
    auto *page = new QWidget;
    page->setObjectName(QStringLiteral("organizationTasksSection"));
    auto *outer = new QVBoxLayout(page);
    outer->setContentsMargins(20, 18, 20, 20);
    outer->setSpacing(10);

    auto *headingRow = new QHBoxLayout;
    auto *heading = new QLabel(QStringLiteral("Organization tasks"));
    heading->setObjectName(QStringLiteral("pageTitle"));
    headingRow->addWidget(heading);
    m_organizationTasksSummary = new QLabel;
    m_organizationTasksSummary->setObjectName(QStringLiteral("mutedLabel"));
    headingRow->addWidget(m_organizationTasksSummary);
    headingRow->addStretch();

    m_organizationTaskNewButton = new QPushButton(QStringLiteral("New task"));
    m_organizationTaskNewButton->setObjectName(QStringLiteral("primaryButton"));
    setOcticon(m_organizationTaskNewButton, "plus", 14);
    connect(m_organizationTaskNewButton, &QPushButton::clicked, this,
            &MainWindow::createOrganizationTask);
    headingRow->addWidget(m_organizationTaskNewButton);

    auto *refresh = new QPushButton(QStringLiteral("Refresh"));
    refresh->setObjectName(QStringLiteral("ghostButton"));
    setOcticon(refresh, "sync", 14);
    connect(refresh, &QPushButton::clicked, this,
            &MainWindow::refreshOrganizationTasks);
    headingRow->addWidget(refresh);
    outer->addLayout(headingRow);

    m_organizationTasksStatus = new QLabel(
        QStringLiteral("Open Tasks to load the private organization catalog."));
    m_organizationTasksStatus->setObjectName(
        QStringLiteral("organizationTasksStatus"));
    m_organizationTasksStatus->setWordWrap(true);
    outer->addWidget(m_organizationTasksStatus);

    m_organizationTasksSearch = new QLineEdit;
    m_organizationTasksSearch->setObjectName(
        QStringLiteral("organizationTasksSearch"));
    m_organizationTasksSearch->setPlaceholderText(
        QStringLiteral("Search title, details, assignee, repository, status…"));
    m_organizationTasksSearch->setClearButtonEnabled(true);
    outer->addWidget(m_organizationTasksSearch);

    auto *splitter = new QSplitter(Qt::Horizontal);
    splitter->setChildrenCollapsible(false);
    // One column, no headers, no grid, no alternating bands (adhoc #56): the
    // delegate paints an icon strip on the left and the title on the right, so
    // the list reads as a list rather than as a spreadsheet. Everything the old
    // columns showed is still there — as a glyph, with the full text on the
    // row's tooltip and in the detail pane.
    m_organizationTasksTable = new QTableWidget(0, 1);
    m_organizationTasksTable->setObjectName(
        QStringLiteral("organizationTasksTable"));
    m_organizationTasksTable->horizontalHeader()->hide();
    m_organizationTasksTable->verticalHeader()->hide();
    m_organizationTasksTable->setShowGrid(false);
    m_organizationTasksTable->setFrameShape(QFrame::NoFrame);
    m_organizationTasksTable->setSelectionBehavior(
        QAbstractItemView::SelectRows);
    m_organizationTasksTable->setSelectionMode(
        QAbstractItemView::SingleSelection);
    m_organizationTasksTable->setEditTriggers(
        QAbstractItemView::NoEditTriggers);
    m_organizationTasksTable->setAlternatingRowColors(false);
    m_organizationTasksTable->setSortingEnabled(false);
    m_organizationTasksTable->setWordWrap(false);
    m_organizationTasksTable->setMouseTracking(true);
    m_organizationTasksTable->setItemDelegate(
        new OrganizationTaskRowDelegate(m_organizationTasksTable));
    m_organizationTasksTable->verticalHeader()->setDefaultSectionSize(
        kTaskRowHeight);
    m_organizationTasksTable->horizontalHeader()->setSectionResizeMode(
        0, QHeaderView::Stretch);
    connect(m_organizationTasksTable, &QTableWidget::itemSelectionChanged,
            this, [this] {
                renderOrganizationTaskDetail();
                updateOrganizationTaskActions();
            });
    // Priority and assignee lost their in-row editors with the columns; the
    // edit dialog carries both, so a double-click opens it.
    connect(m_organizationTasksTable, &QTableWidget::itemDoubleClicked, this,
            [this](QTableWidgetItem *) { editOrganizationTask(); });
    // The search runs over the whole catalog, not just the painted page, so a
    // match on page 12 still surfaces — it re-pages the filtered set from the
    // first page.
    connect(m_organizationTasksSearch, &QLineEdit::textChanged, this,
            [this](const QString &) {
                m_organizationTasksPage = 0;
                renderOrganizationTaskRows();
            });

    auto *tableHost = new QWidget;
    auto *tableLayout = new QVBoxLayout(tableHost);
    tableLayout->setContentsMargins(0, 0, 0, 0);
    tableLayout->setSpacing(6);
    tableLayout->addWidget(m_organizationTasksTable, 1);

    auto *pager = new QHBoxLayout;
    pager->setContentsMargins(0, 0, 0, 0);
    pager->setSpacing(8);
    m_organizationTaskPrevPageButton =
        new QPushButton(QString::fromUtf8("\xE2\x80\xB9 Previous"));
    m_organizationTaskPrevPageButton->setObjectName(
        QStringLiteral("ghostButton"));
    m_organizationTaskNextPageButton =
        new QPushButton(QString::fromUtf8("Next \xE2\x80\xBA"));
    m_organizationTaskNextPageButton->setObjectName(
        QStringLiteral("ghostButton"));
    m_organizationTaskPageLabel = new QLabel;
    m_organizationTaskPageLabel->setObjectName(QStringLiteral("mutedLabel"));
    connect(m_organizationTaskPrevPageButton, &QPushButton::clicked, this,
            [this] {
                m_organizationTasksPage =
                    qMax(0, m_organizationTasksPage - 1);
                renderOrganizationTaskRows();
            });
    connect(m_organizationTaskNextPageButton, &QPushButton::clicked, this,
            [this] {
                ++m_organizationTasksPage;
                renderOrganizationTaskRows();
            });
    pager->addWidget(m_organizationTaskPrevPageButton);
    pager->addWidget(m_organizationTaskPageLabel);
    pager->addStretch();
    pager->addWidget(m_organizationTaskNextPageButton);
    tableLayout->addLayout(pager);
    splitter->addWidget(tableHost);

    auto *detailHost = new QWidget;
    auto *detailLayout = new QVBoxLayout(detailHost);
    detailLayout->setContentsMargins(12, 0, 0, 0);
    detailLayout->setSpacing(8);
    m_organizationTaskDetail = new QTextBrowser;
    m_organizationTaskDetail->setObjectName(
        QStringLiteral("organizationTaskDetail"));
    m_organizationTaskDetail->setOpenExternalLinks(false);
    detailLayout->addWidget(m_organizationTaskDetail, 1);

    auto *actions = new QGridLayout;
    actions->setContentsMargins(0, 0, 0, 0);
    actions->setSpacing(6);
    auto addAction = [this, actions](QPushButton **slot, const QString &text,
                                     const QString &icon, int row, int column) {
        *slot = new QPushButton(text);
        (*slot)->setObjectName(QStringLiteral("ghostButton"));
        setOcticon(*slot, icon, 14);
        actions->addWidget(*slot, row, column);
    };
    addAction(&m_organizationTaskEditButton, QStringLiteral("Edit"),
              QStringLiteral("pencil"), 0, 0);
    addAction(&m_organizationTaskAgentButton, QStringLiteral("Assign Bot"),
              QStringLiteral("hubot"), 0, 1);
    addAction(&m_organizationTaskStartButton, QStringLiteral("Start"),
              QStringLiteral("play"), 0, 2);
    addAction(&m_organizationTaskCompleteButton, QStringLiteral("Complete"),
              QStringLiteral("check"), 1, 0);
    addAction(&m_organizationTaskQaButton, QStringLiteral("Send to QA"),
              QStringLiteral("beaker"), 1, 1);
    addAction(&m_organizationTaskReturnButton,
              QStringLiteral("Return to list"),
              QStringLiteral("reply"), 1, 2);
    addAction(&m_organizationTaskDeleteButton, QStringLiteral("Delete"),
              QStringLiteral("trash"), 2, 0);
    addAction(&m_organizationTaskFollowUpButton, QStringLiteral("Follow up"),
              QStringLiteral("git-branch"), 2, 1);
    // Same one-click affordance the log views have (adhoc #114): hand the task
    // to the footer prompt box so it can be reworded before it goes to an
    // agent, instead of retyping the title and details by hand.
    addAction(&m_organizationTaskPromptButton,
              QStringLiteral("Add to prompt"), QStringLiteral("plus"), 2, 2);
    m_organizationTaskPromptButton->setToolTip(
        QStringLiteral("Add this task's title and details to the prompt box"));
    m_organizationTaskDeleteButton->setObjectName(
        QStringLiteral("dangerButton"));
    // "Add to prompt" still needs a second trip to the footer to actually send
    // it. This is the one-click version: attach the task to the prompt and
    // launch it right away under whatever the prompt box is currently set to,
    // with the run bound back to this task. Spans the grid because it is the
    // action an operator reaches for most on an agent task.
    m_organizationTaskStartAgentButton =
        new QPushButton(QStringLiteral("Start agent with prompt settings"));
    m_organizationTaskStartAgentButton->setObjectName(
        QStringLiteral("primaryButton"));
    setOcticon(m_organizationTaskStartAgentButton, "rocket", 14);
    m_organizationTaskStartAgentButton->setToolTip(
        QStringLiteral("Attach this task to the prompt box and start an agent "
                       "on it with the repository, provider, model and mode "
                       "the prompt box is set to"));
    actions->addWidget(m_organizationTaskStartAgentButton, 3, 0, 1, 3);
    connect(m_organizationTaskEditButton, &QPushButton::clicked, this,
            &MainWindow::editOrganizationTask);
    connect(m_organizationTaskAgentButton, &QPushButton::clicked, this,
            &MainWindow::assignOrganizationTaskToAgent);
    connect(m_organizationTaskStartButton, &QPushButton::clicked, this,
            [this] {
                const QJsonObject task = selectedOrganizationTask(
                    m_organizationTasksTable, m_organizationTasks);
                runOrganizationTaskAction(
                    taskText(task, QStringLiteral("status")) ==
                            QLatin1String("active")
                        ? QStringLiteral("stop")
                        : QStringLiteral("start"));
            });
    connect(m_organizationTaskCompleteButton, &QPushButton::clicked, this,
            [this] { runOrganizationTaskAction(QStringLiteral("complete")); });
    connect(m_organizationTaskQaButton, &QPushButton::clicked, this,
            [this] { runOrganizationTaskAction(QStringLiteral("qa")); });
    connect(m_organizationTaskReturnButton, &QPushButton::clicked, this,
            [this] { runOrganizationTaskAction(QStringLiteral("return")); });
    connect(m_organizationTaskDeleteButton, &QPushButton::clicked, this,
            &MainWindow::deleteOrganizationTask);
    connect(m_organizationTaskFollowUpButton, &QPushButton::clicked, this,
            &MainWindow::createOrganizationTaskFollowUp);
    connect(m_organizationTaskPromptButton, &QPushButton::clicked, this,
            &MainWindow::addOrganizationTaskToPrompt);
    connect(m_organizationTaskStartAgentButton, &QPushButton::clicked, this,
            &MainWindow::startOrganizationTaskAgentFromPrompt);
    detailLayout->addLayout(actions);
    splitter->addWidget(detailHost);
    splitter->setStretchFactor(0, 3);
    splitter->setStretchFactor(1, 2);
    outer->addWidget(splitter, 1);

    auto *queueHeading = new QHBoxLayout;
    auto *queueTitle = new QLabel(QStringLiteral("Claude / Codex queue"));
    queueTitle->setObjectName(QStringLiteral("sectionTitle"));
    queueHeading->addWidget(queueTitle);
    auto *queueHelp = new QLabel(
        QStringLiteral("Local queued items not yet linked to Tasks"));
    queueHelp->setObjectName(QStringLiteral("mutedLabel"));
    queueHeading->addWidget(queueHelp);
    queueHeading->addStretch();
    m_organizationTaskQueueMoveButton =
        new QPushButton(QStringLiteral("Move into Tasks"));
    m_organizationTaskQueueMoveButton->setObjectName(
        QStringLiteral("primaryButton"));
    setOcticon(m_organizationTaskQueueMoveButton, "plus", 14);
    m_organizationTaskQueueMoveButton->setEnabled(false);
    connect(m_organizationTaskQueueMoveButton, &QPushButton::clicked, this,
            &MainWindow::moveQueuedAgentItemToTasks);
    queueHeading->addWidget(m_organizationTaskQueueMoveButton);
    outer->addLayout(queueHeading);

    m_organizationTaskQueueTable = new QTableWidget(0, 5);
    m_organizationTaskQueueTable->setObjectName(
        QStringLiteral("organizationTaskQueueTable"));
    m_organizationTaskQueueTable->setHorizontalHeaderLabels(
        {QStringLiteral("Provider"), QStringLiteral("Item"),
         QStringLiteral("Status"), QStringLiteral("Repository"),
         QStringLiteral("Queued")});
    m_organizationTaskQueueTable->verticalHeader()->hide();
    m_organizationTaskQueueTable->setSelectionBehavior(
        QAbstractItemView::SelectRows);
    m_organizationTaskQueueTable->setSelectionMode(
        QAbstractItemView::SingleSelection);
    m_organizationTaskQueueTable->setEditTriggers(
        QAbstractItemView::NoEditTriggers);
    m_organizationTaskQueueTable->setMaximumHeight(190);
    auto *queueHeader = m_organizationTaskQueueTable->horizontalHeader();
    queueHeader->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    queueHeader->setSectionResizeMode(1, QHeaderView::Stretch);
    for (int column = 2; column < 5; ++column)
        queueHeader->setSectionResizeMode(column,
                                          QHeaderView::ResizeToContents);
    connect(m_organizationTaskQueueTable,
            &QTableWidget::itemSelectionChanged, this, [this] {
                m_organizationTaskQueueMoveButton->setEnabled(
                    m_organizationTaskQueueTable->currentRow() >= 0 &&
                    m_organizationTasksCanManage);
            });
    outer->addWidget(m_organizationTaskQueueTable);

    renderOrganizationTaskDetail();
    updateOrganizationTaskActions();
    refreshOrganizationTaskQueue();
    return page;
}

void MainWindow::requestOrganizationTasks(
    const QByteArray &method, const QString &path, const QJsonObject &body,
    OrganizationTaskReplyHandler handler)
{
    if (!m_networkAccess) {
        handler(false, {}, QStringLiteral(
            "Sign in to an organization account to use private tasks."));
        return;
    }
    QUrl url = catalogApiUrl();
    url.setPath(path);
    url.setQuery(QString());
    QNetworkRequest request(url);
    request.setAttribute(QNetworkRequest::CacheLoadControlAttribute,
                         QNetworkRequest::AlwaysNetwork);
    request.setTransferTimeout(15000);
    request.setRawHeader(QByteArrayLiteral("Accept"),
                         QByteArrayLiteral("application/json"));
    bool keySigned = false;
    if (!m_accountSessionToken.trimmed().isEmpty()) {
        request.setRawHeader(
            QByteArrayLiteral("Authorization"),
            QByteArrayLiteral("Bearer ") + m_accountSessionToken.toUtf8());
    } else {
        // Only a desktop that signed in with a password holds a session token.
        // The ordinary launch is authenticateSilently(), which proves this
        // install owns the account's key and mints no token at all — so
        // without the signed fallback the whole tab reported "Sign in to an
        // organization account" to an operator who was already signed in
        // (adhoc #52). The relay accepts the signature for reading the board
        // and for the two writes a desktop makes for its own run; anything
        // else still needs a real session.
        QString resource;
        const QString proof = organizationTaskProof(method, path, &resource);
        if (proof.isEmpty() ||
            !authenticateOrgTaskRequest(url, request, proof, resource)) {
            // Distinguish "not signed in at all" from "signed in with this
            // device's key, but this change (delete, edit, QA, another
            // member's timer, ...) deliberately needs a real session." The
            // first message told an operator who was plainly using their own
            // account to "sign in" to it, which reads as a no-op bug when the
            // actual, actionable step is a password login (adhoc #108).
            handler(false, {},
                    m_accountAuthenticated
                        ? QString::fromUtf8(
                              "This change needs a password sign-in on this "
                              "device, not just its saved account key \xE2"
                              "\x80\x94 use Settings \xE2\x86\x92 \"Log in to "
                              "a user account\", then try again.")
                        : QStringLiteral(
                              "Sign in to an organization account to use "
                              "private tasks."));
            return;
        }
        keySigned = true;
    }
    const QByteArray payload =
        body.isEmpty() ? QByteArray() : QJsonDocument(body).toJson(
                                             QJsonDocument::Compact);
    if (!payload.isEmpty())
        request.setHeader(QNetworkRequest::ContentTypeHeader,
                          QStringLiteral("application/json"));

    logSystem(QStringLiteral("Organization tasks: %1 %2 requested.")
                  .arg(QString::fromLatin1(method), path));
    QNetworkReply *reply = nullptr;
    if (method == QByteArrayLiteral("GET"))
        reply = m_networkAccess->get(request);
    else if (method == QByteArrayLiteral("POST"))
        reply = m_networkAccess->post(request, payload);
    else
        reply = m_networkAccess->sendCustomRequest(request, method, payload);
    connect(reply, &QNetworkReply::finished, this,
            [this, reply, method, path, keySigned,
             handler = std::move(handler)]() mutable {
                const QByteArray raw = reply->readAll();
                const QJsonDocument document = QJsonDocument::fromJson(raw);
                const QJsonObject data = document.object();
                const int status = reply->attribute(
                    QNetworkRequest::HttpStatusCodeAttribute).toInt();
                const bool ok = reply->error() == QNetworkReply::NoError &&
                                status >= 200 && status < 300 &&
                                data.value(QStringLiteral("ok")).toBool(true);
                QString error = ok
                    ? QString()
                    : taskErrorText(
                          data,
                          status > 0
                              ? QStringLiteral("HTTP %1").arg(status)
                              : reply->errorString());
                // "invalid session" reads as a bug to an operator who is
                // plainly signed in: what the relay actually rejected is this
                // machine's key. Say so, and name the one action that fixes it.
                if (keySigned && status == 401)
                    error += QString::fromUtf8(
                        " \xE2\x80\x94 this machine's key is not authorized "
                        "for the account. Use Settings \xE2\x86\x92 \"Log in "
                        "to a user account\" to register it.");
                logSystem(
                    QStringLiteral("Organization tasks: %1 %2 %3.")
                        .arg(QString::fromLatin1(method), path,
                             ok ? QStringLiteral("completed")
                                : QStringLiteral("failed: ") + error));
                reply->deleteLater();
                handler(ok, data, error);
            });
}

void MainWindow::refreshOrganizationTasks()
{
    if (m_organizationTasksLoading || !m_organizationTasksStatus)
        return;
    m_organizationTasksLoading = true;
    m_organizationTasksStatus->setText(
        QString::fromUtf8("Loading organization tasks\xE2\x80\xA6"));
    requestOrganizationTasks(
        QByteArrayLiteral("GET"), QStringLiteral("/api/tasks"), {},
        [this](bool ok, const QJsonObject &payload, const QString &error) {
            m_organizationTasksLoading = false;
            if (!ok) {
                m_organizationTasksStatus->setText(
                    QStringLiteral("Tasks unavailable: %1").arg(error));
                return;
            }
            applyOrganizationTasks(payload);
            m_organizationTasksStatus->setText(
                QStringLiteral("Private catalog refreshed at %1.")
                    .arg(QTime::currentTime().toString(
                        QStringLiteral("HH:mm:ss"))));
        });
}

// Paint the open-task count onto the Tasks rail button and remember it, so the
// next launch has a number to show before the relay has answered anything.
void MainWindow::setOrganizationTaskBadge(int openCount)
{
    QSettings().setValue(kOrganizationTaskOpenCountSetting, openCount);
    if (auto *button = dynamic_cast<ActivityRailButton *>(m_tasksNavButton))
        button->setBadgeCount(openCount);
}

// Launch-time counterpart: show the last count we knew about immediately. The
// background refresh below replaces it as soon as the relay answers.
void MainWindow::restoreOrganizationTaskBadge()
{
    if (auto *button = dynamic_cast<ActivityRailButton *>(m_tasksNavButton)) {
        button->setBadgeCount(
            QSettings().value(kOrganizationTaskOpenCountSetting, 0).toInt());
    }
}

// Refresh the count without requiring a visit to the Tasks page. The page is
// built lazily, so on a fresh launch there is no status label and no table to
// fill — count the payload directly then. A failed read (offline, or an account
// with no private catalog) deliberately leaves the restored badge alone.
void MainWindow::refreshOrganizationTaskBadge()
{
    if (m_headless && qEnvironmentVariableIsSet(
                          "FORKMESH_EXTERNAL_MIRROR_NODE"))
        return;
    if (m_organizationTasksLoading)
        return;
    if (m_organizationTasksStatus && m_organizationTasksTable) {
        refreshOrganizationTasks(); // page exists: keep table and badge in step
        return;
    }
    m_organizationTasksLoading = true;
    requestOrganizationTasks(
        QByteArrayLiteral("GET"), QStringLiteral("/api/tasks"), {},
        [this](bool ok, const QJsonObject &payload, const QString &) {
            m_organizationTasksLoading = false;
            if (!ok)
                return;
            setOrganizationTaskBadge(openOrganizationTaskCount(
                payload.value(QStringLiteral("tasks")).toArray()));
        });
}

void MainWindow::applyOrganizationTasks(const QJsonObject &payload)
{
    if (!m_organizationTasksTable)
        return;
    const QJsonObject prior = selectedOrganizationTask(
        m_organizationTasksTable, m_organizationTasks);
    const QString selectedId = taskText(prior, QStringLiteral("id"));
    m_organizationTasks =
        payload.value(QStringLiteral("tasks")).toArray();
    m_organizationTaskActor =
        payload.value(QStringLiteral("actor")).toString().toLower();
    m_organizationTasksCanManage =
        payload.value(QStringLiteral("canManage")).toBool();
    m_organizationTaskMembers.clear();
    for (const QJsonValue &value :
         payload.value(QStringLiteral("members")).toArray()) {
        const QString member = value.toString().trimmed().toLower();
        if (!member.isEmpty())
            m_organizationTaskMembers.append(member);
    }
    m_organizationTaskDepartments.clear();
    for (const QJsonValue &value :
         payload.value(QStringLiteral("departments")).toArray()) {
        const QString department = value.toString().trimmed().toLower();
        if (!department.isEmpty())
            m_organizationTaskDepartments.append(department);
    }

    renderOrganizationTaskRows(selectedId);
    const int openCount = openOrganizationTaskCount(m_organizationTasks);
    if (m_organizationTasksSummary) {
        // Same wording the dashboard's Tasks page shows, counted the same way,
        // so "84 open" means 84 open on both surfaces (adhoc #56).
        m_organizationTasksSummary->setText(
            QStringLiteral("%1 open \xC2\xB7 %2 closed%3")
                .arg(openCount)
                .arg(m_organizationTasks.size() - openCount)
                .arg(m_organizationTasksCanManage
                         ? QStringLiteral(" \xC2\xB7 manager")
                         : QString()));
    }
    setOrganizationTaskBadge(openCount);
    refreshOrganizationTaskQueue();
}

// Paint one page of the catalog. `m_organizationTasks` always holds every task
// the relay returned; the search narrows that whole list and the page then cuts
// a window out of the match set, so nothing past row 100 is unreachable.
// Column 0 keeps the task's absolute index in Qt::UserRole, which is what
// selectedOrganizationTask() resolves against.
void MainWindow::renderOrganizationTaskRows(const QString &selectTaskId)
{
    if (!m_organizationTasksTable)
        return;
    const QString selectedId =
        selectTaskId.isEmpty()
            ? taskText(selectedOrganizationTask(m_organizationTasksTable,
                                                m_organizationTasks),
                       QStringLiteral("id"))
            : selectTaskId;
    const QString query = m_organizationTasksSearch
                              ? m_organizationTasksSearch->text().trimmed()
                              : QString();
    QList<int> matches;
    matches.reserve(m_organizationTasks.size());
    for (int index = 0; index < m_organizationTasks.size(); ++index) {
        const QJsonObject task = m_organizationTasks.at(index).toObject();
        if (query.isEmpty()) {
            matches.append(index);
            continue;
        }
        const QJsonObject qa = task.value(QStringLiteral("qa")).toObject();
        const QString haystack =
            QStringList{
                taskText(task, QStringLiteral("title")),
                taskText(task, QStringLiteral("details")),
                taskAssigneeLabel(task),
                taskText(task, QStringLiteral("repository")),
                taskText(task, QStringLiteral("department")),
                taskText(task, QStringLiteral("status")),
                taskText(qa, QStringLiteral("howToTest")),
                taskText(task, QStringLiteral("id")),
            }.join(QLatin1Char('\n'));
        if (haystack.contains(query, Qt::CaseInsensitive))
            matches.append(index);
    }

    const int pageCount =
        qMax(1, (matches.size() + kOrganizationTaskPageSize - 1) /
                    kOrganizationTaskPageSize);
    m_organizationTasksPage =
        qBound(0, m_organizationTasksPage, pageCount - 1);
    const int first = m_organizationTasksPage * kOrganizationTaskPageSize;
    const int last =
        qMin(matches.size(), first + kOrganizationTaskPageSize);

    m_organizationTasksTable->setRowCount(0);
    m_organizationTasksTable->setRowCount(qMax(0, last - first));
    int selectedRow = -1;
    for (int row = 0; row + first < last; ++row) {
        const int index = matches.at(first + row);
        const QJsonObject task = m_organizationTasks.at(index).toObject();
        const QJsonObject qa = task.value(QStringLiteral("qa")).toObject();
        const QString statusKey = taskStatusKey(task);
        const QString department = taskText(task, QStringLiteral("department"));
        const QString repository = taskText(task, QStringLiteral("repository"));
        const QString qaStatus = taskText(qa, QStringLiteral("status"));
        const bool qaRequested =
            qa.value(QStringLiteral("requestedAt")).toDouble() > 0;

        auto *item =
            new QTableWidgetItem(taskText(task, QStringLiteral("title")));
        item->setData(Qt::UserRole, index);
        item->setData(kTaskStatusRole, statusKey);
        item->setData(kTaskDepartmentRole, department);
        item->setData(kTaskRepositoryRole, repository);
        item->setData(kTaskAssigneeKindRole,
                      taskText(task, QStringLiteral("assigneeKind")));
        item->setData(kTaskAssigneeRole,
                      taskText(task, QStringLiteral("assignee")));
        item->setData(kTaskQaRole, qaStatus);
        item->setData(kTaskQaRequestedRole, qaRequested);
        item->setData(
            kTaskPriorityRole,
            QString::number(task.value(QStringLiteral("priority")).toInt()));
        item->setData(
            kTaskCreatedAtRole,
            qint64(task.value(QStringLiteral("createdAt")).toDouble()));
        item->setToolTip(taskRowTooltipHtml(task, statusKey, department,
                                             repository, qaStatus, qaRequested));
        m_organizationTasksTable->setItem(row, 0, item);
        if (taskText(task, QStringLiteral("id")) == selectedId)
            selectedRow = row;
    }
    if (m_organizationTaskPageLabel) {
        m_organizationTaskPageLabel->setText(
            matches.isEmpty()
                ? (m_organizationTasks.isEmpty()
                       ? QStringLiteral("No tasks")
                       : QStringLiteral("No tasks match this search"))
                : QStringLiteral("%1\xE2\x80\x93%2 of %3 \xC2\xB7 page %4 of %5")
                      .arg(first + 1)
                      .arg(last)
                      .arg(matches.size())
                      .arg(m_organizationTasksPage + 1)
                      .arg(pageCount));
    }
    if (m_organizationTaskPrevPageButton)
        m_organizationTaskPrevPageButton->setEnabled(
            m_organizationTasksPage > 0);
    if (m_organizationTaskNextPageButton)
        m_organizationTaskNextPageButton->setEnabled(
            m_organizationTasksPage + 1 < pageCount);
    if (selectedRow >= 0)
        m_organizationTasksTable->selectRow(selectedRow);
    else if (m_organizationTasksTable->rowCount() > 0)
        m_organizationTasksTable->selectRow(0);
    renderOrganizationTaskDetail();
    updateOrganizationTaskActions();
}

void MainWindow::refreshOrganizationTaskQueue()
{
    if (!m_organizationTaskQueueTable)
        return;
    const int selectedId =
        m_organizationTaskQueueTable->currentRow() >= 0 &&
                m_organizationTaskQueueTable->item(
                    m_organizationTaskQueueTable->currentRow(), 0)
            ? m_organizationTaskQueueTable
                  ->item(m_organizationTaskQueueTable->currentRow(), 0)
                  ->data(Qt::UserRole)
                  .toInt()
            : 0;
    m_organizationTaskQueueTable->setRowCount(0);
    int selectedRow = -1;
    for (const AgentSession &session : std::as_const(m_agentSessions)) {
        const bool supported =
            session.provider == QLatin1String("codex") ||
            session.provider.startsWith(QLatin1String("claude"));
        if (!supported || !session.orgTaskId.trimmed().isEmpty())
            continue;
        const int row = m_organizationTaskQueueTable->rowCount();
        m_organizationTaskQueueTable->insertRow(row);
        QString title = session.issueTitle.trimmed();
        if (title.isEmpty())
            title = session.prompt.simplified().left(160);
        if (title.isEmpty())
            title = QStringLiteral("Queued agent item #%1").arg(session.id);
        const QString provider =
            session.provider == QLatin1String("codex")
                ? QStringLiteral("Codex")
                : QStringLiteral("Claude");
        const QStringList values{
            provider,
            title,
            session.status,
            session.owner + QLatin1Char('/') + session.name,
            taskTimestamp(session.createdAtMs),
        };
        for (int column = 0; column < values.size(); ++column) {
            auto *item = new QTableWidgetItem(values.at(column));
            item->setToolTip(values.at(column));
            if (column == 0)
                item->setData(Qt::UserRole, session.id);
            m_organizationTaskQueueTable->setItem(row, column, item);
        }
        if (session.id == selectedId)
            selectedRow = row;
    }
    if (selectedRow >= 0)
        m_organizationTaskQueueTable->selectRow(selectedRow);
    m_organizationTaskQueueMoveButton->setEnabled(
        selectedRow >= 0 && m_organizationTasksCanManage);
}

void MainWindow::moveQueuedAgentItemToTasks()
{
    const int row = m_organizationTaskQueueTable
                        ? m_organizationTaskQueueTable->currentRow()
                        : -1;
    QTableWidgetItem *item =
        row >= 0 ? m_organizationTaskQueueTable->item(row, 0) : nullptr;
    const int sessionId = item ? item->data(Qt::UserRole).toInt() : 0;
    auto it = std::find_if(
        m_agentSessions.begin(), m_agentSessions.end(),
        [sessionId](const AgentSession &session) {
            return session.id == sessionId;
        });
    if (it == m_agentSessions.end() || !m_organizationTasksCanManage)
        return;
    QString title = it->issueTitle.trimmed();
    if (title.isEmpty())
        title = it->prompt.simplified().left(160);
    if (title.isEmpty())
        title = QStringLiteral("Queued agent item #%1").arg(it->id);
    const QString details = it->prompt.trimmed().left(4000);
    const QJsonObject body{
        {QStringLiteral("title"), title},
        {QStringLiteral("details"), details},
        {QStringLiteral("department"), QStringLiteral("engineering")},
        {QStringLiteral("destination"), QStringLiteral("agent")},
        {QStringLiteral("assigneeKind"), QStringLiteral("agent")},
        {QStringLiteral("repository"),
         it->owner + QLatin1Char('/') + it->name},
        {QStringLiteral("priority"), 50},
        {QStringLiteral("agent"),
         QJsonObject{
             {QStringLiteral("provider"), it->provider},
             {QStringLiteral("model"), it->model},
             {QStringLiteral("mode"), it->mode},
             {QStringLiteral("strength"), it->strength},
             {QStringLiteral("sessionId"), QString::number(it->id)},
         }},
    };
    requestOrganizationTasks(
        QByteArrayLiteral("POST"), QStringLiteral("/api/tasks"), body,
        [this, sessionId](bool ok, const QJsonObject &payload,
                          const QString &error) {
            if (!ok) {
                m_organizationTasksStatus->setText(
                    QStringLiteral("Queue import failed: %1").arg(error));
                return;
            }
            const QString taskId =
                payload.value(QStringLiteral("task"))
                    .toObject()
                    .value(QStringLiteral("id"))
                    .toString();
            auto session = std::find_if(
                m_agentSessions.begin(), m_agentSessions.end(),
                [sessionId](const AgentSession &candidate) {
                    return candidate.id == sessionId;
                });
            if (session != m_agentSessions.end()) {
                session->orgTask = true;
                session->orgTaskId = taskId;
                if (m_agentStore)
                    m_agentStore->saveSession(*session);
            }
            m_organizationTasksStatus->setText(
                QStringLiteral("Queued item moved into Tasks."));
            refreshOrganizationTaskQueue();
            refreshOrganizationTasks();
        });
}

void MainWindow::renderOrganizationTaskDetail()
{
    if (!m_organizationTaskDetail)
        return;
    const QJsonObject task = selectedOrganizationTask(
        m_organizationTasksTable, m_organizationTasks);
    m_organizationTaskDetail->setHtml(taskDetailHtml(
        task, localAgentRunLabelForTask(taskText(task, QStringLiteral("id")))));
}

// The reverse of binding a run to a task: which local session (if any) carries
// this task's id, so the detail can name the run working it and how far it got.
QString MainWindow::localAgentRunLabelForTask(const QString &taskId) const
{
    if (taskId.isEmpty())
        return QString();
    for (const AgentSession &session : m_agentSessions) {
        if (session.orgTaskId != taskId)
            continue;
        QStringList parts;
        parts << QStringLiteral("#%1").arg(session.id);
        const QString provider = agentProviderName(session.provider);
        if (!provider.isEmpty())
            parts << provider;
        if (!session.status.isEmpty())
            parts << session.status;
        return parts.join(QStringLiteral(", ")) +
               QStringLiteral(" (this desktop)");
    }
    return QString();
}

void MainWindow::updateOrganizationTaskActions()
{
    const QJsonObject task = selectedOrganizationTask(
        m_organizationTasksTable, m_organizationTasks);
    const bool selected = !task.isEmpty();
    const bool done =
        task.value(QStringLiteral("completedAt")).toDouble() > 0 ||
        taskText(task, QStringLiteral("status")) == QLatin1String("done");
    const bool active =
        taskText(task, QStringLiteral("status")) == QLatin1String("active");
    const bool agent =
        taskText(task, QStringLiteral("assigneeKind")) ==
        QLatin1String("agent");
    const bool mine =
        taskText(task, QStringLiteral("assignee")).compare(
            m_organizationTaskActor, Qt::CaseInsensitive) == 0;
    const bool hasRepository =
        !taskText(task, QStringLiteral("repository")).isEmpty();
    if (m_organizationTaskNewButton)
        m_organizationTaskNewButton->setVisible(
            m_organizationTasksCanManage);
    if (m_organizationTaskEditButton)
        m_organizationTaskEditButton->setEnabled(
            selected && m_organizationTasksCanManage && !active && !done);
    if (m_organizationTaskAgentButton)
        m_organizationTaskAgentButton->setEnabled(
            selected && m_organizationTasksCanManage && !active && !done &&
            hasRepository);
    if (m_organizationTaskStartButton) {
        m_organizationTaskStartButton->setText(
            active ? QStringLiteral("Stop") : QStringLiteral("Start"));
        m_organizationTaskStartButton->setEnabled(
            selected && mine && !done);
    }
    if (m_organizationTaskCompleteButton)
        m_organizationTaskCompleteButton->setEnabled(
            selected && !done && (mine || m_organizationTasksCanManage));
    if (m_organizationTaskQaButton)
        m_organizationTaskQaButton->setEnabled(
            selected && !done && m_organizationTasksCanManage);
    if (m_organizationTaskReturnButton)
        m_organizationTaskReturnButton->setEnabled(
            selected && !done && agent && m_organizationTasksCanManage);
    if (m_organizationTaskDeleteButton)
        m_organizationTaskDeleteButton->setEnabled(
            selected && m_organizationTasksCanManage);
    if (m_organizationTaskFollowUpButton)
        m_organizationTaskFollowUpButton->setEnabled(
            selected &&
            (m_organizationTasksCanManage || mine ||
             taskText(task, QStringLiteral("createdBy")) ==
                 m_organizationTaskActor));
    // Copying a task into the prompt box changes nothing on the board, so it
    // needs no manage right — only a selection.
    if (m_organizationTaskPromptButton)
        m_organizationTaskPromptButton->setEnabled(selected);
    // Starting an agent runs on this desktop against a local checkout — it is
    // not a board write either, so a finished task is the only thing that rules
    // it out.
    if (m_organizationTaskStartAgentButton)
        m_organizationTaskStartAgentButton->setEnabled(selected && !done);
}

// Hands the selected task to the footer's prompt box (adhoc #114's "add to
// prompt", now on the task detail): title, id and the free-text blocks, so the
// operator can edit the wording before sending it to an agent.
void MainWindow::addOrganizationTaskToPrompt()
{
    const QJsonObject task = selectedOrganizationTask(
        m_organizationTasksTable, m_organizationTasks);
    if (task.isEmpty())
        return;
    appendTextToActivePrompt(organizationTaskPromptText(task));
    if (m_organizationTasksStatus)
        m_organizationTasksStatus->setText(
            QStringLiteral("Task added to the prompt box."));
}

// One click from a task to a running agent. The task is attached to the prompt
// box first — so what the agent was handed is exactly what the operator can see
// and could have edited — and the run is launched with that box's live
// settings: its repository, provider, model, permission mode, effort, PR
// toggle and any queued attachments. The session is then bound back to this
// task (orgTaskId), which is what makes the "vice versa" half work: the run's
// live status and its completion note report against this task rather than
// opening a second, duplicate one.
void MainWindow::startOrganizationTaskAgentFromPrompt()
{
    const QJsonObject task = selectedOrganizationTask(
        m_organizationTasksTable, m_organizationTasks);
    if (task.isEmpty() || !m_organizationTasksStatus)
        return;
    const QString id = taskText(task, QStringLiteral("id"));
    const QString title = taskText(task, QStringLiteral("title"));
    if (id.isEmpty())
        return;

    // "Manual (create issue)" files an issue and Cloudflare AI answers on the
    // relay: neither runs an agent in a checkout, so neither can work a task.
    const QString provider =
        m_quickAddAgentProvider
            ? m_quickAddAgentProvider->currentData().toString()
            : QStringLiteral("claude-code");
    if (provider == QLatin1String("manual") ||
        agentIsCloudflareAiProvider(provider)) {
        m_organizationTasksStatus->setText(QStringLiteral(
            "Pick a coding agent in the prompt box before starting one on a "
            "task."));
        return;
    }

    // Work the task in its own repository when this desktop has a checkout of
    // it; otherwise fall back to whatever the prompt box is pointed at, the
    // same repository a typed prompt would have run in. A repository this node
    // knows but has not cloned cannot host a run, so it is not a hit either.
    const QStringList repository =
        taskText(task, QStringLiteral("repository"))
            .split(QLatin1Char('/'), Qt::SkipEmptyParts);
    int repoIndex = repository.size() == 2
                        ? repoIndexFor(repository.at(0), repository.at(1))
                        : -1;
    if (repoIndex >= 0 && repoIndex < m_repositories.size() &&
        m_repositories.at(repoIndex).localPath.isEmpty())
        repoIndex = -1;
    if (repoIndex < 0)
        repoIndex = issuesRepoIndex();
    if (repoIndex < 0) {
        m_organizationTasksStatus->setText(QStringLiteral(
            "Open a repository with a local checkout to run this task's "
            "agent."));
        return;
    }

    // Attach the task to the prompt before reading the box back, so anything
    // already typed there rides along as context and the operator is left
    // looking at the exact text that was sent.
    addOrganizationTaskToPrompt();
    QString prompt = m_issueQuickAdd ? m_issueQuickAdd->toPlainText().trimmed()
                                     : organizationTaskPromptText(task);
    if (prompt.isEmpty())
        return;
    // Attachments queued in the prompt box reach the agent the way a typed
    // prompt's do (issue #79): one "Attached image: <path>" line per file.
    const QStringList images = m_quickAddImages;
    for (const QString &image : images) {
        if (!prompt.endsWith(QLatin1Char('\n')))
            prompt += QLatin1Char('\n');
        prompt += QStringLiteral("Attached image: %1").arg(image);
    }

    const QString model = (provider == QLatin1String("claude-code") ||
                           agentIsCodexProvider(provider))
                              ? selectedModelComboValue(m_quickAddClaudeModel)
                              : QString();
    const bool createPr =
        m_quickAddCreatePr && m_quickAddCreatePr->isChecked();
    const int sessionId = startAdHocAgentForRepo(
        repoIndex, prompt, provider, createPr, model,
        /*titleOverride=*/title, /*genie=*/false, /*switchToTab=*/true,
        /*orgTaskId=*/id);
    if (sessionId <= 0) {
        // startAdHocAgentForRepo already said why in a toast; leave the prompt
        // box loaded so the attempt is not lost.
        m_organizationTasksStatus->setText(
            QStringLiteral("Could not start an agent on this task."));
        return;
    }
    if (m_issueQuickAdd)
        m_issueQuickAdd->clear();
    clearQuickAddImages();
    QStringList details;
    const QString modelLabel = agentModelLabel(model);
    if (!modelLabel.isEmpty())
        details << modelLabel;
    if (m_quickAddModeSelector)
        details << m_quickAddModeSelector->currentText();
    const QString started =
        QStringLiteral("Started %1 agent session #%2 on this task%3.")
            .arg(agentProviderName(provider))
            .arg(sessionId)
            .arg(details.isEmpty()
                     ? QString()
                     : QStringLiteral(" (%1)")
                           .arg(details.join(QStringLiteral(", "))));
    m_organizationTasksStatus->setText(started);
    logSystem(started);
    // Repaint the detail so its "Agent session" row names the run that was just
    // bound to the task, without waiting for the next board refresh.
    renderOrganizationTaskDetail();
}

void MainWindow::createOrganizationTask()
{
    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("New organization task"));
    auto *layout = new QFormLayout(&dialog);
    auto *title = new QLineEdit;
    title->setMaxLength(160);
    auto *details = new QPlainTextEdit;
    details->setMaximumHeight(120);
    auto *department = new QComboBox;
    department->addItems(m_organizationTaskDepartments.isEmpty()
                             ? QStringList{QStringLiteral("general"),
                                           QStringLiteral("engineering")}
                             : m_organizationTaskDepartments);
    auto *repository = new QLineEdit(QStringLiteral("forkmesh/forkmesh"));
    auto *assignee = new QComboBox;
    assignee->addItem(QStringLiteral("Bot"), QStringLiteral("agent"));
    assignee->addItem(QStringLiteral("Unassigned"),
                      QStringLiteral("unassigned"));
    for (const QString &member : std::as_const(m_organizationTaskMembers))
        assignee->addItem(member, QStringLiteral("user:") + member);
    auto *priority = new QSpinBox;
    priority->setRange(1, 99);
    priority->setValue(50);
    layout->addRow(QStringLiteral("Title"), title);
    layout->addRow(QStringLiteral("Details"), details);
    layout->addRow(QStringLiteral("Department"), department);
    layout->addRow(QStringLiteral("Repository"), repository);
    layout->addRow(QStringLiteral("Assignee"), assignee);
    layout->addRow(QStringLiteral("Priority"), priority);
    auto *buttons = new QDialogButtonBox(
        QDialogButtonBox::Save | QDialogButtonBox::Cancel);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addRow(buttons);
    title->setFocus();
    if (dialog.exec() != QDialog::Accepted || title->text().trimmed().isEmpty())
        return;

    const QString assignment = assignee->currentData().toString();
    const bool agent = assignment == QLatin1String("agent");
    QJsonObject body{
        {QStringLiteral("title"), title->text().trimmed()},
        {QStringLiteral("details"), details->toPlainText().trimmed()},
        {QStringLiteral("department"), department->currentText()},
        {QStringLiteral("destination"),
         agent ? QStringLiteral("agent") : QStringLiteral("department")},
        {QStringLiteral("assigneeKind"),
         agent ? QStringLiteral("agent")
               : assignment == QLatin1String("unassigned")
                   ? QStringLiteral("unassigned")
                   : QStringLiteral("user")},
        {QStringLiteral("repository"), repository->text().trimmed()},
        {QStringLiteral("priority"), priority->value()},
    };
    if (assignment.startsWith(QLatin1String("user:")))
        body.insert(QStringLiteral("assignee"), assignment.mid(5));
    requestOrganizationTasks(
        QByteArrayLiteral("POST"), QStringLiteral("/api/tasks"), body,
        [this, agent](bool ok, const QJsonObject &payload,
                      const QString &error) {
            if (!ok) {
                m_organizationTasksStatus->setText(
                    QStringLiteral("Task creation failed: %1").arg(error));
                return;
            }
            const QJsonObject task =
                payload.value(QStringLiteral("task")).toObject();
            m_organizationTasksStatus->setText(
                QStringLiteral("Task created."));
            if (agent)
                queueOrganizationTaskAgent(task);
            else
                refreshOrganizationTasks();
        });
}

// File the prompt-bar text as an ordinary shared task. It is deliberately
// unassigned and routed to General: pressing "task" must not launch a genie (or
// any other agent) as a side effect. An operator can assign/start it from the
// Tasks page when it is ready.
void MainWindow::createQuickAddOrganizationTask()
{
    if (!m_issueQuickAdd)
        return;
    const QString prompt = m_issueQuickAdd->toPlainText().trimmed();
    if (prompt.isEmpty()) {
        flashMessage(QStringLiteral("Type a task first."), true);
        m_issueQuickAdd->setFocus();
        return;
    }

    QString title = prompt.section(QLatin1Char('\n'), 0, 0).simplified();
    if (title.size() > 160)
        title = title.left(159).trimmed() + QString::fromUtf8("\xE2\x80\xA6");
    QString repository;
    const int repoIndex = issuesRepoIndex();
    if (repoIndex >= 0 && repoIndex < m_repositories.size()) {
        const RepositoryRecord &repo = m_repositories.at(repoIndex);
        repository = repo.owner + QLatin1Char('/') + repo.name;
    }
    const QJsonObject body{
        {QStringLiteral("title"), title},
        {QStringLiteral("details"), prompt},
        {QStringLiteral("department"), QStringLiteral("general")},
        {QStringLiteral("destination"), QStringLiteral("department")},
        {QStringLiteral("assigneeKind"), QStringLiteral("unassigned")},
        {QStringLiteral("repository"), repository},
        {QStringLiteral("priority"), 50},
    };
    requestOrganizationTasks(
        QByteArrayLiteral("POST"), QStringLiteral("/api/tasks"), body,
        [this, prompt](bool ok, const QJsonObject &, const QString &error) {
            if (!ok) {
                flashMessage(QStringLiteral("Task creation failed: %1").arg(error),
                             true);
                return;
            }
            recordQuickAddHistory(prompt);
            m_issueQuickAdd->clear();
            clearQuickAddImages();
            showSection(kOrganizationTasksSectionIndex);
            refreshOrganizationTasks();
            flashMessage(QStringLiteral("Task added to General."));
        });
}

void MainWindow::createOrganizationTaskFollowUp()
{
    const QJsonObject parent = selectedOrganizationTask(
        m_organizationTasksTable, m_organizationTasks);
    const QString parentId = taskText(parent, QStringLiteral("id"));
    if (parentId.isEmpty())
        return;
    bool accepted = false;
    const QString title = QInputDialog::getText(
        this, QStringLiteral("Follow-up task"),
        QStringLiteral("Task title (assignee is inherited)"),
        QLineEdit::Normal, QString(), &accepted).trimmed();
    if (!accepted || title.isEmpty())
        return;
    requestOrganizationTasks(
        QByteArrayLiteral("POST"), QStringLiteral("/api/tasks"),
        {{QStringLiteral("title"), title},
         {QStringLiteral("parentTaskId"), parentId}},
        [this](bool ok, const QJsonObject &, const QString &error) {
            m_organizationTasksStatus->setText(
                ok ? QStringLiteral(
                         "Follow-up created with the same assignee.")
                   : QStringLiteral("Follow-up creation failed: %1")
                         .arg(error));
            if (ok)
                refreshOrganizationTasks();
        });
}

void MainWindow::editOrganizationTask()
{
    const QJsonObject task = selectedOrganizationTask(
        m_organizationTasksTable, m_organizationTasks);
    if (task.isEmpty())
        return;
    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("Edit organization task"));
    auto *layout = new QFormLayout(&dialog);
    auto *title = new QLineEdit(taskText(task, QStringLiteral("title")));
    title->setMaxLength(160);
    auto *details = new QPlainTextEdit(
        taskText(task, QStringLiteral("details")));
    details->setMaximumHeight(120);
    auto *repository = new QLineEdit(
        taskText(task, QStringLiteral("repository")));
    auto *assignee = new QComboBox;
    assignee->addItem(QStringLiteral("Bot"), QStringLiteral("agent"));
    assignee->addItem(QStringLiteral("Unassigned"),
                      QStringLiteral("unassigned"));
    for (const QString &member : std::as_const(m_organizationTaskMembers))
        assignee->addItem(member, QStringLiteral("user:") + member);
    QString current = taskText(task, QStringLiteral("assigneeKind"));
    if (current == QLatin1String("user"))
        current = QStringLiteral("user:") +
                  taskText(task, QStringLiteral("assignee"));
    const int assignmentIndex = assignee->findData(current);
    if (assignmentIndex >= 0)
        assignee->setCurrentIndex(assignmentIndex);
    auto *priority = new QSpinBox;
    priority->setRange(1, 99);
    priority->setValue(task.value(QStringLiteral("priority")).toInt(50));
    layout->addRow(QStringLiteral("Title"), title);
    layout->addRow(QStringLiteral("Details"), details);
    layout->addRow(QStringLiteral("Repository"), repository);
    layout->addRow(QStringLiteral("Assignee"), assignee);
    layout->addRow(QStringLiteral("Priority"), priority);
    auto *buttons = new QDialogButtonBox(
        QDialogButtonBox::Save | QDialogButtonBox::Cancel);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addRow(buttons);
    if (dialog.exec() != QDialog::Accepted || title->text().trimmed().isEmpty())
        return;
    const QString assignment = assignee->currentData().toString();
    const bool assignAgent = assignment == QLatin1String("agent");
    QJsonObject body{
        {QStringLiteral("title"), title->text().trimmed()},
        {QStringLiteral("details"), details->toPlainText().trimmed()},
        {QStringLiteral("repository"), repository->text().trimmed()},
        {QStringLiteral("priority"), priority->value()},
        {QStringLiteral("assigneeKind"),
         assignAgent ? QStringLiteral("agent")
                     : assignment == QLatin1String("unassigned")
                         ? QStringLiteral("unassigned")
                         : QStringLiteral("user")},
    };
    if (assignment.startsWith(QLatin1String("user:")))
        body.insert(QStringLiteral("assignee"), assignment.mid(5));
    const QString path = QStringLiteral("/api/tasks/") +
                         taskText(task, QStringLiteral("id"));
    requestOrganizationTasks(
        QByteArrayLiteral("PATCH"), path, body,
        [this, assignAgent](bool ok, const QJsonObject &payload,
                            const QString &error) {
            if (!ok) {
                m_organizationTasksStatus->setText(
                    QStringLiteral("Task update failed: %1").arg(error));
                return;
            }
            const QJsonObject changed =
                payload.value(QStringLiteral("task")).toObject();
            m_organizationTasksStatus->setText(
                QStringLiteral("Task updated."));
            if (assignAgent &&
                taskText(changed, QStringLiteral("agentSessionId")).isEmpty())
                queueOrganizationTaskAgent(changed);
            else
                refreshOrganizationTasks();
        });
}

void MainWindow::assignOrganizationTaskToAgent()
{
    const QJsonObject task = selectedOrganizationTask(
        m_organizationTasksTable, m_organizationTasks);
    if (task.isEmpty())
        return;
    if (taskText(task, QStringLiteral("assigneeKind")) ==
        QLatin1String("agent")) {
        queueOrganizationTaskAgent(task);
        return;
    }
    const QString path = QStringLiteral("/api/tasks/") +
                         taskText(task, QStringLiteral("id"));
    requestOrganizationTasks(
        QByteArrayLiteral("PATCH"), path,
        {{QStringLiteral("assigneeKind"), QStringLiteral("agent")},
         {QStringLiteral("repository"),
          taskText(task, QStringLiteral("repository"))}},
        [this](bool ok, const QJsonObject &payload, const QString &error) {
            if (!ok) {
                m_organizationTasksStatus->setText(
                    QStringLiteral("Agent assignment failed: %1").arg(error));
                return;
            }
            queueOrganizationTaskAgent(
                payload.value(QStringLiteral("task")).toObject());
        });
}

void MainWindow::queueOrganizationTaskAgent(const QJsonObject &task)
{
    const QStringList repository =
        taskText(task, QStringLiteral("repository"))
            .split(QLatin1Char('/'), Qt::SkipEmptyParts);
    if (repository.size() != 2) {
        m_organizationTasksStatus->setText(
            QStringLiteral("Agent assignment needs an owner/repository."));
        return;
    }
    const QString id = taskText(task, QStringLiteral("id"));
    const QString title = taskText(task, QStringLiteral("title"));
    const QString details = taskText(task, QStringLiteral("details"));
    const QString prompt =
        QStringLiteral("[task:%1] %2%3")
            .arg(id, title,
                 details.isEmpty() ? QString()
                                   : QStringLiteral("\n\n") + details);
    const QString endpoint =
        QStringLiteral("/api/orgs/%1/repos/%2/agent-bots")
            .arg(repository.at(0).toLower(), repository.at(1));
    m_organizationTasksStatus->setText(
        QString::fromUtf8("Assigning task to an eligible agent node\xE2\x80\xA6"));
    requestOrganizationTasks(
        QByteArrayLiteral("POST"), endpoint,
        {{QStringLiteral("provider"), QStringLiteral("agent")},
         {QStringLiteral("prompt"), prompt.left(8000)},
         {QStringLiteral("title"), title},
         {QStringLiteral("taskKey"), QStringLiteral("task:") + id}},
        [this, id](bool ok, const QJsonObject &payload,
                   const QString &error) {
            if (!ok) {
                m_organizationTasksStatus->setText(
                    QStringLiteral(
                        "Task is assigned to the agent queue, but dispatch "
                        "failed: %1")
                        .arg(error));
                refreshOrganizationTasks();
                return;
            }
            const QString sessionId =
                payload.value(QStringLiteral("session"))
                    .toObject()
                    .value(QStringLiteral("id"))
                    .toString();
            if (sessionId.isEmpty()) {
                m_organizationTasksStatus->setText(
                    QStringLiteral("Agent dispatch returned no session id."));
                refreshOrganizationTasks();
                return;
            }
            requestOrganizationTasks(
                QByteArrayLiteral("PATCH"),
                QStringLiteral("/api/tasks/") + id,
                {{QStringLiteral("agentSessionId"), sessionId}},
                [this](bool linked, const QJsonObject &,
                       const QString &linkError) {
                    m_organizationTasksStatus->setText(
                        linked
                            ? QStringLiteral(
                                  "Agent session queued and linked to the task.")
                            : QStringLiteral(
                                  "Agent queued, but linking its session failed: "
                                  "%1")
                                  .arg(linkError));
                    refreshOrganizationTasks();
                });
        });
}

void MainWindow::runOrganizationTaskAction(const QString &action)
{
    const QJsonObject task = selectedOrganizationTask(
        m_organizationTasksTable, m_organizationTasks);
    const QString id = taskText(task, QStringLiteral("id"));
    if (id.isEmpty())
        return;
    QJsonObject body;
    if (action == QLatin1String("complete")) {
        bool accepted = false;
        const QString note = QInputDialog::getMultiLineText(
            this, QStringLiteral("Complete organization task"),
            QStringLiteral("Completion note and evidence"),
            taskText(task, QStringLiteral("completionNote")), &accepted);
        if (!accepted)
            return;
        body.insert(QStringLiteral("completionNote"), note.trimmed());
    } else if (action == QLatin1String("qa")) {
        const QJsonObject qa = task.value(QStringLiteral("qa")).toObject();
        bool accepted = false;
        const QString howToTest = QInputDialog::getMultiLineText(
            this, QStringLiteral("Send organization task to QA"),
            QStringLiteral("Exact QA steps"),
            taskText(qa, QStringLiteral("howToTest")), &accepted);
        if (!accepted)
            return;
        body.insert(QStringLiteral("howToTest"), howToTest.trimmed());
    }
    requestOrganizationTasks(
        QByteArrayLiteral("POST"),
        QStringLiteral("/api/tasks/%1/%2").arg(id, action), body,
        [this, action](bool ok, const QJsonObject &, const QString &error) {
            m_organizationTasksStatus->setText(
                ok ? QStringLiteral("Task action completed: %1.").arg(action)
                   : QStringLiteral("Task action failed: %1").arg(error));
            if (ok)
                refreshOrganizationTasks();
        });
}

void MainWindow::deleteOrganizationTask()
{
    const QJsonObject task = selectedOrganizationTask(
        m_organizationTasksTable, m_organizationTasks);
    const QString id = taskText(task, QStringLiteral("id"));
    if (id.isEmpty())
        return;
    requestOrganizationTasks(
        QByteArrayLiteral("DELETE"), QStringLiteral("/api/tasks/") + id, {},
        [this](bool ok, const QJsonObject &, const QString &error) {
            m_organizationTasksStatus->setText(
                ok ? QStringLiteral("Task deleted.")
                   : QStringLiteral("Task deletion failed: %1").arg(error));
            if (ok) {
                refreshOrganizationTasks();
                return;
            }
            // A failed delete left the task sitting right where it was, with
            // only a status label above the table to explain why — easy to
            // miss, which read as "delete did nothing" (adhoc #108). Put the
            // reason somewhere the operator cannot scroll past.
            QMessageBox::warning(this, QStringLiteral("Delete task"), error);
        });
}
