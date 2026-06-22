#include "MainWindow.h"

#include "ActionFile.h"
#include "ActionRunner.h"
#include "ClaudeAgentScript.h"
#include "IssueBurnup.h"
#include "QrCode.h"

#include "MarkdownEditor.h"
#include "MessageRow.h"
#include "RepoHost.h"
#include "ServerNode.h"
#include "Theme.h"

#include <QAction>
#include <QApplication>
#include <QBuffer>
#include <QButtonGroup>
#include <QCheckBox>
#include <QClipboard>
#include <QCloseEvent>
#include <QCryptographicHash>
#include <QComboBox>
#include <QCompleter>
#include <QCoreApplication>
#include <QDateTime>
#include <QDesktopServices>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileInfo>
#include <QMimeData>
#include <QFontDatabase>
#include <QFormLayout>
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
#include <QMessageBox>
#include <QMimeDatabase>
#include <QMouseEvent>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSslSocket>
#include <QSslError>
#include <QImage>
#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
#include <QPlainTextEdit>
#include <QRandomGenerator>
#include <QEventLoop>
#include <QFileSystemWatcher>
#include <QProcess>
#include <QProcessEnvironment>
#include <QTextCursor>
#include <QTimeZone>
#include <QPushButton>
#include <QRegularExpression>
#include <QScrollArea>
#include <QScrollBar>
#include <QSettings>
#include <QSignalBlocker>
#include <QSize>
#include <QSplitter>
#include <QStackedWidget>
#include <QStringListModel>
#include <QStyle>
#include <QStyleHints>
#include <QSyntaxHighlighter>
#include <QHeaderView>
#include <QTableWidget>
#include <QTabWidget>
#include <QTextBrowser>
#include <QTextBlock>
#include <QTextDocument>
#include <QTextEdit>
#include <QTemporaryDir>
#include <QToolButton>
#include <QTreeWidget>
#include <QSystemTrayIcon>
#include <QSvgRenderer>
#include <QThread>
#include <QTimer>
#include <QUrl>
#include <QUrlQuery>
#include <QUuid>
#include <QVBoxLayout>

#include <algorithm>
#include <functional>
#include <memory>

#ifndef Q_OS_WIN
#include <pwd.h>
#include <unistd.h>
#endif

#ifndef FORKMESH_VERSION
#define FORKMESH_VERSION "dev"
#endif
#ifndef FORKMESH_SOURCE_DIR
#define FORKMESH_SOURCE_DIR ""
#endif

namespace {

constexpr int kTableSortRole = Qt::UserRole + 10;

// Column in the commits list that carries the Summary text + the commit hash
// (Qt::UserRole). The metadata columns sit to its left.
constexpr int kCommitSummaryCol = 5;

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

QString formatByteSize(qint64 bytes)
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
const QString kSolanaSetting = QStringLiteral("profile/solana");
const QString kAvatarSetting = QStringLiteral("profile/avatarPng");
const QString kServerUrlSetting = QStringLiteral("server/url");
const QString kLocalServerUrl =
    QStringLiteral("ws://127.0.0.1:8787/api/repo/mainnode/forkmesh/rooms/general/ws");
const QString kDefaultServerUrl =
    QStringLiteral("wss://forkmesh.com/api/repo/mainnode/forkmesh/rooms/general/ws");
const QString kRoomNameSetting = QStringLiteral("server/room");
const QString kPassphraseSetting = QStringLiteral("server/passphrase");
const QString kServersArray = QStringLiteral("servers/items");
const QString kActiveServerSetting = QStringLiteral("servers/active");
const QString kDefaultRoomName = QStringLiteral("general");
const QString kDefaultPassphrase = QStringLiteral("forkmesh-public-room");
const QString kRepositoriesArray = QStringLiteral("repositories/items");
const QString kMirrorRootSetting = QStringLiteral("repositories/mirrorRoot");
const QString kLastRepositorySetting = QStringLiteral("repositories/lastOpen");

QString savedSolanaAddress()
{
    return QSettings().value(kSolanaSetting).toString().trimmed();
}

void saveSolanaAddress(const QString &address)
{
    QSettings().setValue(kSolanaSetting, address.trimmed());
}
const QString kPreviewCacheRootSetting = QStringLiteral("repositories/previewCacheRoot");
const QString kConnectionTotalSetting = QStringLiteral("stats/connectionTotalMs");
const QString kThemeSetting = QStringLiteral("app/theme"); // system | dark | light
// Show a desktop alert when a push lands on one of this node's mirrors.
const QString kPushAlertSetting = QStringLiteral("actions/pushAlert");
// Show a desktop alert when an action run starts and finishes.
const QString kActionAlertSetting = QStringLiteral("actions/runAlert");
const QString kNodeConnectAlertSetting = QStringLiteral("notifications/nodeConnect");
const QString kDisbursementAlertSetting = QStringLiteral("notifications/disbursement");
const QString kSolanaLastBalanceSettingPrefix =
    QStringLiteral("profile/solanaLastBalance/");
const QString kWindowGeometrySetting = QStringLiteral("ui/windowGeometry");
const QString kVotesSpentSetting = QStringLiteral("votes/spent");
const QString kVotedSetting = QStringLiteral("votes/voted");
const QString kCodexApiKeySetting = QStringLiteral("agents/codexApiKey");
const QString kOpenAiAdminKeySetting = QStringLiteral("agents/openAiAdminKey");
const QString kCodexModelSetting = QStringLiteral("agents/codexModel");
const QString kClaudeApiKeySetting = QStringLiteral("agents/claudeApiKey");
const QString kCodexCommandSetting = QStringLiteral("agents/codexCommand");
const QString kClaudeCommandSetting = QStringLiteral("agents/claudeCommand");
const QString kAgentContextSetting = QStringLiteral("agents/contextWindow");
const QString kAgentMaxOutputSetting = QStringLiteral("agents/maxOutputTokens");
// Cached month-to-date spend labels (issue #115) so the figures persist and are
// shown immediately on restart instead of "not yet refreshed".
const QString kOpenAiSpendTextSetting = QStringLiteral("agents/openAiSpendText");
const QString kOpenAiSpendTsSetting = QStringLiteral("agents/openAiSpendTs");
const QString kClaudeSpendTextSetting = QStringLiteral("agents/claudeSpendText");
const QString kClaudeSpendTsSetting = QStringLiteral("agents/claudeSpendTs");
// Anchors (epoch ms) for the rolling 5-hour and weekly usage windows. They are
// reset to "now" whenever an agent runs after the previous window has elapsed,
// so the agent sessions screen can count down the time left in each window.
const QString kCodexLimit5hStartSetting = QStringLiteral("agents/codexLimit5hStart");
const QString kCodexLimitWeekStartSetting = QStringLiteral("agents/codexLimitWeekStart");
const QString kClaudeLimit5hStartSetting = QStringLiteral("agents/claudeLimit5hStart");
const QString kClaudeLimitWeekStartSetting = QStringLiteral("agents/claudeLimitWeekStart");
constexpr qint64 kAgentLimit5hMs = 5LL * 60 * 60 * 1000;
constexpr qint64 kAgentLimitWeekMs = 7LL * 24 * 60 * 60 * 1000;
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
constexpr int kNetworkLogLimit = 2000;

// Materialize the bundled Claude agent script into the app data dir and return
// its path. The script talks to the Anthropic API directly using
// ANTHROPIC_API_KEY, so no `claude` binary is required.
QString claudeAgentScriptPath()
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
QString defaultClaudeCommand()
{
    QString quoted = claudeAgentScriptPath();
    quoted.replace(QLatin1Char('\''), QStringLiteral("'\\''"));
    return QStringLiteral("python3 '%1' {promptFile}").arg(quoted);
}

// Read the Claude command, migrating any legacy `claude` CLI command (or a stale
// script path) to the current python-based default.
QString claudeCommandSetting()
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

QString codexCommandSetting()
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
QString updateClientDir()
{
    const QString baked = QStringLiteral(FORKMESH_SOURCE_DIR);
    if (!baked.isEmpty() && QDir(baked).exists("CMakeLists.txt"))
        return baked;
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) +
           "/src/qt_client";
}

// When ForkMesh runs as root (e.g. launched via `sudo`), updates must never be
// written under /root. Returns the invoking non-root user's name when we are
// root and SUDO_USER points at a real user, otherwise an empty string (meaning
// "run the update in-process as the current user").
QString invokingNonRootUser()
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
QString homeForUser(const QString &user)
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
QString clientDirUnderHome(const QString &home)
{
    return home + QStringLiteral("/.local/share/forkmesh/src/qt_client");
}

// Single-quote a string for safe use inside an `sh -c` command line.
QString shellSingleQuote(const QString &value)
{
    QString escaped = value;
    escaped.replace(QLatin1Char('\''), QStringLiteral("'\\''"));
    return QLatin1Char('\'') + escaped + QLatin1Char('\'');
}

QString builtExecutablePath(const QString &buildDir)
{
#ifdef Q_OS_MACOS
    const QString appExecutable = buildDir + "/ForkMesh.app/Contents/MacOS/ForkMesh";
    if (QFileInfo::exists(appExecutable))
        return appExecutable;
#endif
    return buildDir + "/forkmesh";
}

#ifdef Q_OS_MACOS
QString brewPrefix(const QString &formula)
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

QStringList cmakeConfigureArgs(const QString &clientDir, const QString &buildDir)
{
    QStringList args{"-S", clientDir, "-B", buildDir, "-DCMAKE_BUILD_TYPE=Release",
                     "-DFORKMESH_BUILD_TESTS=OFF"};
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

const QString kDmPrefix = QStringLiteral("@");

bool isDirectConversation(const QString &conversation)
{
    return conversation.startsWith(kDmPrefix);
}

QString dmKey(const QString &peerId)
{
    return kDmPrefix + peerId;
}

QString dmPeerId(const QString &conversation)
{
    return conversation.mid(1);
}

QString repoSegment(QString value, const QString &fallback)
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

QString repoNameFromUrl(QString url)
{
    url = url.trimmed();
    url.replace('\\', '/');
    QString name = url.section('/', -1);
    if (name.endsWith(".git"))
        name.chop(4);
    return repoSegment(name, QStringLiteral("repository"));
}

// Public node name = username: a single DNS-like label — lowercase letters,
// digits and hyphens, starting with a letter and ending with a letter or digit,
// max 63 chars. Mirrors valid_node_name in the worker and NAME_RE on the website.
bool isValidNodeName(const QString &value)
{
    static const QRegularExpression re(
        QStringLiteral("^[a-z](?:[a-z0-9-]{0,61}[a-z0-9])?$"));
    return re.match(value).hasMatch();
}

QString accountNameFromInput(QString value, const QString &fallback = QStringLiteral("node"))
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

bool textMentionsNodeName(const QString &text, const QString &nodeName)
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

QString savedProfileName()
{
    QSettings settings;
    const QString handle = settings.value(kHandleSetting).toString().trimmed();
    if (!handle.isEmpty())
        return handle;
    return settings.value(kDisplayNameSetting).toString().trimmed();
}

QString formatRepoDate(qint64 timestampMs)
{
    if (timestampMs <= 0)
        return QStringLiteral("never");
    return QDateTime::fromMSecsSinceEpoch(timestampMs).toString("yyyy-MM-dd hh:mm");
}

QString formatIssueRelativeTime(qint64 timestampMs)
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
QString formatShortRelativeTime(qint64 timestampSecs)
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

QString formatInsightBytes(qint64 bytes)
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

QString insightMetricCell(const QString &label, const QString &value,
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

QString insightMetricsTable(const QStringList &cells)
{
    QString html =
        QStringLiteral("<table width='100%' cellspacing='8' cellpadding='0'><tr>");
    for (const QString &cell : cells)
        html += cell;
    html += QStringLiteral("</tr></table>");
    return html;
}

bool currentThemeIsDark()
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

QString formatDuration(qint64 ms)
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

QString compactAddress(QString address)
{
    address = address.trimmed();
    if (address.size() <= 30)
        return address;
    return address.left(18) + QStringLiteral("...") + address.right(8);
}

QStringList splitIssueFieldList(const QString &text)
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
QPixmap refreshPixmap(const QColor &color, double angleDeg, int size)
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

// A modern, deterministic "mesh constellation" identicon (gravatar-style, but
// on-brand): nodes connected by edges over a green→cyan gradient tile, evoking
// ForkMesh's decentralized, networked ethos. Same seed → same avatar.
QByteArray forkMeshAvatarPng(const QString &seed)
{
    const QByteArray h =
        QCryptographicHash::hash(seed.toUtf8(), QCryptographicHash::Sha256);
    auto b = [&](int i) { return static_cast<quint8>(h.at(i % h.size())); };

    const int S = 128;
    QImage img(S, S, QImage::Format_ARGB32_Premultiplied);
    img.fill(Qt::transparent);
    QPainter p(&img);
    p.setRenderHint(QPainter::Antialiasing);

    // On-brand hue family: green → teal → blue.
    const int hue = 120 + (b(0) % 80); // 120..199
    QLinearGradient grad(0, 0, S, S);
    grad.setColorAt(0.0, QColor::fromHsv(hue, 130, 72));
    grad.setColorAt(1.0, QColor::fromHsv((hue + 25) % 360, 165, 40));
    QPainterPath tile;
    tile.addRoundedRect(0, 0, S, S, 30, 30);
    p.fillPath(tile, grad);
    p.setClipPath(tile);

    // Node positions seeded from the hash, padded inside the tile.
    const int n = 5 + (b(1) % 3); // 5..7 nodes
    const qreal pad = 26.0;
    QList<QPointF> pts;
    for (int i = 0; i < n; ++i)
        pts.append(QPointF(pad + (b(2 + i * 2) / 255.0) * (S - 2 * pad),
                           pad + (b(3 + i * 2) / 255.0) * (S - 2 * pad)));

    // Edges: a connected loop through the nodes.
    QColor edge = QColor::fromHsv(hue, 60, 235);
    edge.setAlpha(140);
    QPen edgePen(edge, 2.2);
    edgePen.setCapStyle(Qt::RoundCap);
    p.setPen(edgePen);
    for (int i = 0; i < pts.size(); ++i)
        p.drawLine(pts.at(i), pts.at((i + 1) % pts.size()));

    // Nodes: a soft glow plus a bright dot; the first node is the larger hub.
    const QColor node = QColor::fromHsv(hue, 35, 255);
    for (int i = 0; i < pts.size(); ++i) {
        const qreal r = (i == 0 ? 11.0 : 6.0 + (b(10 + i) % 4));
        QColor glow = QColor::fromHsv(hue, 80, 255);
        glow.setAlpha(70);
        p.setPen(Qt::NoPen);
        p.setBrush(glow);
        p.drawEllipse(pts.at(i), r + 5, r + 5);
        p.setBrush(node);
        p.drawEllipse(pts.at(i), r, r);
    }
    p.end();

    QByteArray png;
    QBuffer buffer(&png);
    buffer.open(QIODevice::WriteOnly);
    img.save(&buffer, "PNG");
    return png;
}

// Clip avatar PNG bytes into a rounded-rect pixmap for the nav button.
QPixmap roundedAvatar(const QByteArray &png, int side)
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
QIcon osBadgeIcon(const QString &platform, bool online, int size)
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

QString defaultDisplayName(const ForkMeshIdentity &identity)
{
    const QString suffix = accountNameFromInput(identity.publicKey(), QString()).left(8);
    return suffix.isEmpty() ? QStringLiteral("node")
                            : QStringLiteral("node") + suffix;
}

void saveProfileName(const QString &name)
{
    const QString trimmed = accountNameFromInput(name, QString());
    if (!trimmed.isEmpty()) {
        QSettings settings;
        settings.setValue(kDisplayNameSetting, trimmed);
        settings.setValue(kHandleSetting, trimmed);
        settings.setValue(kAccountNameSetting, trimmed);
    }
}

QIcon statusDotIcon(bool online)
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

PreviewSyntax previewSyntaxForPath(const QString &path)
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

QTextCharFormat previewFormat(const QColor &color, int weight = QFont::Normal,
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
        } else if (trimmed.startsWith(QLatin1String("==> SUCCESS")) ||
                   trimmed.startsWith(QLatin1String("==> Agent finished")) ||
                   trimmed.startsWith(QLatin1String("==> Created pull request"))) {
            setFormat(0, len, m_success);
        } else if (trimmed.startsWith(QLatin1String("==>"))) {
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
};

CodeLineNumberArea::CodeLineNumberArea(CodePreviewEditor *editor)
    : QWidget(editor), m_editor(editor)
{
    setObjectName("codeLineNumberArea");
}

QSize CodeLineNumberArea::sizeHint() const
{
    return QSize(m_editor ? m_editor->lineNumberAreaWidth() : 0, 0);
}

void CodeLineNumberArea::paintEvent(QPaintEvent *event)
{
    if (m_editor)
        m_editor->lineNumberAreaPaintEvent(event);
}


QPixmap tintedOcticonPixmap(const QString &name, const QColor &color, int size)
{
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
    return pixmap;
}

QIcon themedOcticon(const QString &name, const QColor &color, int size)
{
    QIcon icon;
    icon.addPixmap(tintedOcticonPixmap(name, color, size), QIcon::Normal, QIcon::Off);
    icon.addPixmap(tintedOcticonPixmap(name, color.darker(120), size),
                   QIcon::Active, QIcon::Off);
    icon.addPixmap(tintedOcticonPixmap(name, QColor("#6e7681"), size),
                   QIcon::Disabled, QIcon::Off);
    return icon;
}

void applyStoredOcticon(QPushButton *button)
{
    if (!button)
        return;
    const QString name = button->property("forkmeshOcticon").toString();
    if (name.isEmpty())
        return;
    const int size = button->property("forkmeshOcticonSize").toInt();
    const QColor color(
        Theme::iconColorForButton(button->objectName(), currentThemeIsDark()));
    button->setIcon(themedOcticon(name, color, size > 0 ? size : 16));
    button->setIconSize(QSize(size > 0 ? size : 16, size > 0 ? size : 16));
}

void setOcticon(QPushButton *button, const QString &name, int size = 16)
{
    if (!button)
        return;
    button->setProperty("forkmeshOcticon", name);
    button->setProperty("forkmeshOcticonSize", size);
    applyStoredOcticon(button);
}

QString serverHost(const QString &serverUrl)
{
    return QUrl(serverUrl).host();
}

// Map a file name to a vscode-icons SVG base name (without ".svg"). Falls back
// to "default_file"; the caller verifies the file exists.
QString fileTypeIconName(const QString &fileNameLower)
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
QString languageForFile(const QString &name)
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
QString languageColor(const QString &lang)
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

// Run a git command in `dir`, capturing stdout. Returns false (with stderr in
// `err`) on failure. Used by the in-client repo file browser.
bool runGitCapture(const QString &dir, const QStringList &args, QByteArray *out,
                   QString *err)
{
    QProcess process;
    process.start("git", QStringList{"-C", dir} + args);
    if (!process.waitForFinished(8000)) {
        if (err)
            *err = QStringLiteral("git timed out");
        return false;
    }
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
        if (err)
            *err = QString::fromUtf8(process.readAllStandardError()).trimmed();
        return false;
    }
    if (out)
        *out = process.readAllStandardOutput();
    return true;
}

bool runGitCaptureWithEnv(const QString &dir, const QStringList &args,
                          const QProcessEnvironment &env, QByteArray *out,
                          QString *err)
{
    QProcess process;
    process.setProcessEnvironment(env);
    process.start("git", QStringList{"-C", dir} + args);
    if (!process.waitForFinished(8000)) {
        if (err)
            *err = QStringLiteral("git timed out");
        return false;
    }
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
        if (err)
            *err = QString::fromUtf8(process.readAllStandardError()).trimmed();
        return false;
    }
    if (out)
        *out = process.readAllStandardOutput();
    return true;
}

bool buildWorkingTreeDiff(const QString &dir, const QString &base, QByteArray *out,
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
QString pillTextColor(const QString &backgroundHex)
{
    const QColor c(backgroundHex);
    const double luminance =
        0.299 * c.red() + 0.587 * c.green() + 0.114 * c.blue();
    return luminance > 150 ? QStringLiteral("#1f2328") : QStringLiteral("#ffffff");
}

// A http(s) favicon URL derived from a ws(s) mainnode URL.
QUrl faviconUrl(const QString &serverUrl)
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

QString faviconCacheDir()
{
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) +
           "/favicons";
}

QString faviconCachePath(const QString &host)
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
QPixmap roundedRectPixmap(const QPixmap &src, int side, qreal radius)
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

QPixmap letterFavicon(const QString &host)
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

QString autostartDesktopPath()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) +
           "/autostart/forkmesh.desktop";
}

#if defined(Q_OS_MACOS)
QString launchAgentPath()
{
    return QDir::homePath() + "/Library/LaunchAgents/com.forkmesh.app.plist";
}
#endif

bool isAutostartEnabled()
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

void setAutostartEnabled(bool enabled)
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

} // namespace

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent)
{
    setWindowTitle("ForkMesh v" FORKMESH_VERSION);
    setWindowIcon(QIcon(QStringLiteral(":/app/forkmesh.png")));
    resize(1060, 700);
    // Restore the last window size/position so it reopens where it was left.
    const QByteArray savedGeometry =
        QSettings().value(kWindowGeometrySetting).toByteArray();
    if (!savedGeometry.isEmpty())
        restoreGeometry(savedGeometry);

    m_trayIcon = new QSystemTrayIcon(this);
    m_trayIcon->setIcon(style()->standardIcon(QStyle::SP_MessageBoxInformation));
    m_trayIcon->setToolTip("ForkMesh");
    if (QSystemTrayIcon::isSystemTrayAvailable())
        m_trayIcon->show();

    m_networkAccess = new QNetworkAccessManager(this);
    m_totalConnectionMs = QSettings().value(kConnectionTotalSetting).toLongLong();

    loadServers();
    loadCachedFavicons();

    m_stack = new QStackedWidget(this);
    m_stack->addWidget(buildSetupPage());
    m_stack->addWidget(buildChatPage());
    setCentralWidget(m_stack);
    loadRepositories();
    refreshRepositoryList();
    initActions();
    initAgents();
    const QString lastRepository =
        QSettings().value(kLastRepositorySetting).toString();
    if (!lastRepository.isEmpty()) {
        const int slash = lastRepository.indexOf('/');
        if (slash > 0) {
            const int index =
                repoIndexFor(lastRepository.left(slash),
                             lastRepository.mid(slash + 1));
            if (index >= 0) {
                m_selectedNode = m_repositories.at(index).owner;
                refreshRepositoryList();
                openRepoDetail(index);
            }
        }
    }
    loadActiveServerIntoEdits();
    updateBreadcrumb();
    for (int i = 0; i < m_servers.size(); ++i)
        fetchFavicon(i);

    m_typingStopTimer = new QTimer(this);
    m_typingStopTimer->setSingleShot(true);
    connect(m_typingStopTimer, &QTimer::timeout, this, [this] {
        sendTypingState(false);
    });
    m_homeStatsTimer = new QTimer(this);
    // Rebuild the nodes/repos list each minute so the self node's uptime line
    // stays current (this also persists accumulated uptime via updateHomeStats).
    connect(m_homeStatsTimer, &QTimer::timeout, this,
            &MainWindow::refreshRepositoryList);
    m_homeStatsTimer->start(60000);

    // Keep mirrors fresh: periodically fetch each repo so a mirror tracks the
    // owner's repo as it updates. A first pass runs shortly after startup.
    m_mirrorSyncTimer = new QTimer(this);
    connect(m_mirrorSyncTimer, &QTimer::timeout, this, &MainWindow::autoSyncMirrors);
    m_mirrorSyncTimer->start(5 * 60 * 1000);
    QTimer::singleShot(15000, this, &MainWindow::autoSyncMirrors);
    // Bootstrap the flagship ForkMesh mirror shortly after launch so a freshly
    // installed client shows the project repo without manual setup.
    QTimer::singleShot(3000, this, &MainWindow::ensureFlagshipRepo);

    if (!m_profileIdentity.load()) {
        m_setupError->setText(m_profileIdentity.errorString());
        m_setupError->show();
    } else if (m_pubkeyLabel) {
        m_pubkeyLabel->setText("Ed25519 public key: " +
                               m_profileIdentity.shortPublicKey());
        m_pubkeyLabel->setToolTip(m_profileIdentity.publicKey());
        if (m_nameEdit->text().trimmed().isEmpty())
            m_nameEdit->setText(defaultDisplayName(m_profileIdentity));
        QTimer::singleShot(0, this, &MainWindow::startSession);
    }
    updateHomeStats();
}

void MainWindow::applyTheme()
{
    // Honour the user's override; otherwise follow the OS color scheme.
    qApp->setStyleSheet(Theme::styleSheetForDark(currentThemeIsDark()));
    for (QWidget *widget : QApplication::topLevelWidgets()) {
        if (auto *window = qobject_cast<MainWindow *>(widget))
            window->refreshThemedIcons();
    }
}

void MainWindow::refreshThemedIcons()
{
    const auto buttons = findChildren<QPushButton *>();
    for (QPushButton *button : buttons)
        applyStoredOcticon(button);
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    QSettings().setValue(kWindowGeometrySetting, saveGeometry());
    saveChatHistory();
    QMainWindow::closeEvent(event);
}

// ----------------------------------------------------------- chat persistence

QString MainWindow::chatHistoryKey() const
{
    if (m_activeServer < 0 || m_activeServer >= m_servers.size())
        return {};
    const ServerConfig &s = m_servers.at(m_activeServer);
    const QByteArray seed = (s.url + "\n" + s.room + "\n" + s.passphrase).toUtf8();
    return QString::fromLatin1(
        QCryptographicHash::hash(seed, QCryptographicHash::Sha256).toHex());
}

QString MainWindow::chatHistoryPath() const
{
    const QString key = chatHistoryKey();
    if (key.isEmpty())
        return {};
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) +
           "/chat_history/" + key + ".json";
}

void MainWindow::saveChatHistory()
{
    const QString path = chatHistoryPath();
    if (path.isEmpty())
        return;

    QJsonObject conversations;
    for (auto it = m_history.constBegin(); it != m_history.constEnd(); ++it) {
        QJsonArray arr;
        const QList<ChatMessage> &msgs = it.value();
        // Keep the file bounded: only the most recent messages per conversation.
        const int first = std::max(0, int(msgs.size()) - 1000);
        for (int i = first; i < msgs.size(); ++i) {
            const ChatMessage &m = msgs.at(i);
            QJsonObject obj{{"id", m.id},
                            {"senderId", m.senderId},
                            {"senderName", m.senderName},
                            {"text", m.text},
                            {"ts", m.timestampMs},
                            {"self", m.self},
                            {"edited", m.edited},
                            {"deleted", m.deleted}};
            if (m.hasFile()) {
                obj.insert("fileName", m.fileName);
                obj.insert("fileMime", m.fileMime);
                // Inline small attachments so they survive a restart.
                if (m.fileData.size() <= 512 * 1024)
                    obj.insert("fileData",
                               QString::fromLatin1(m.fileData.toBase64()));
            }
            arr.append(obj);
        }
        if (!arr.isEmpty())
            conversations.insert(it.key(), arr);
    }

    QJsonArray openDms;
    for (const QString &peer : std::as_const(m_openDms))
        openDms.append(peer);
    QJsonObject dmNames;
    for (auto it = m_dmNames.constBegin(); it != m_dmNames.constEnd(); ++it)
        dmNames.insert(it.key(), it.value());

    const QJsonObject root{{"current", m_currentConversation},
                           {"openDms", openDms},
                           {"dmNames", dmNames},
                           {"conversations", conversations}};
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    if (file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        file.write(QJsonDocument(root).toJson(QJsonDocument::Compact));
}

void MainWindow::loadChatHistory()
{
    const QString path = chatHistoryPath();
    if (path.isEmpty() || !QFileInfo::exists(path))
        return;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return;
    const QJsonObject root = QJsonDocument::fromJson(file.readAll()).object();

    const QJsonObject conversations = root.value("conversations").toObject();
    for (auto it = conversations.constBegin(); it != conversations.constEnd(); ++it) {
        const QString conversation = it.key();
        QList<ChatMessage> &dest = m_history[conversation];
        for (const QJsonValue &v : it.value().toArray()) {
            const QJsonObject obj = v.toObject();
            ChatMessage m;
            m.id = obj.value("id").toString();
            m.conversation = conversation;
            m.senderId = obj.value("senderId").toString();
            m.senderName = obj.value("senderName").toString();
            m.text = obj.value("text").toString();
            m.timestampMs = obj.value("ts").toVariant().toLongLong();
            m.self = obj.value("self").toBool();
            m.edited = obj.value("edited").toBool();
            m.deleted = obj.value("deleted").toBool();
            m.fileName = obj.value("fileName").toString();
            m.fileMime = obj.value("fileMime").toString();
            if (obj.contains("fileData"))
                m.fileData = QByteArray::fromBase64(
                    obj.value("fileData").toString().toLatin1());
            if (!m.id.isEmpty()) {
                if (m_historyIds.contains(m.id))
                    continue;
                m_historyIds.insert(m.id);
            }
            dest.append(m);
        }
    }

    // Restore the open DM tabs and their display names.
    const QJsonObject dmNames = root.value("dmNames").toObject();
    for (auto it = dmNames.constBegin(); it != dmNames.constEnd(); ++it)
        m_dmNames.insert(it.key(), it.value().toString());
    for (const QJsonValue &v : root.value("openDms").toArray()) {
        const QString peer = v.toString();
        if (!peer.isEmpty() && !m_openDms.contains(peer))
            m_openDms.append(peer);
    }
    refreshDmList();

    // Reopen the last conversation so history is visible immediately.
    const QString current = root.value("current").toString();
    if (!current.isEmpty() && current != m_currentConversation &&
        m_history.contains(current)) {
        switchConversation(current);
    } else if (!m_currentConversation.isEmpty()) {
        // History often loads *after* the relay has already selected a channel
        // (e.g. #general). switchConversation() no-ops when the target is the
        // current conversation, which left the view empty until you switched
        // away and back. Re-render the open conversation so the just-loaded
        // history shows up on first load.
        rebuildConversationView();
    }
}

void MainWindow::scheduleChatSave()
{
    if (!m_chatSaveTimer) {
        m_chatSaveTimer = new QTimer(this);
        m_chatSaveTimer->setSingleShot(true);
        connect(m_chatSaveTimer, &QTimer::timeout, this,
                &MainWindow::saveChatHistory);
    }
    m_chatSaveTimer->start(1500);
}

// ---------------------------------------------------------------- setup page

QWidget *MainWindow::buildSetupPage()
{
    auto *page = new QWidget;

    auto *card = new QWidget;
    card->setObjectName("setupCard");
    card->setFixedWidth(420);

    auto *title = new QLabel("<span style='color:#22c55e'>Fork</span>Mesh");
    title->setObjectName("appTitle");
    title->setAlignment(Qt::AlignHCenter);
    auto *subtitle = new QLabel(
        "Preserve code, mirror repositories, and chat through a mainnode");
    subtitle->setObjectName("appSubtitle");
    subtitle->setAlignment(Qt::AlignHCenter);
    auto *versionLabel = new QLabel("v" FORKMESH_VERSION);
    versionLabel->setObjectName("versionLabel");
    versionLabel->setAlignment(Qt::AlignHCenter);

    m_nameEdit = new QLineEdit;
    m_nameEdit->setPlaceholderText("Node name (e.g. ada-lovelace)");
    m_nameEdit->setMaxLength(63);
    m_nameEdit->setText(savedProfileName());
    auto *nameHint = new QLabel(
        "Your public node name: lowercase letters, numbers and hyphens. "
        "Start with a letter; up to 63 characters.");
    nameHint->setObjectName("modeHint");
    nameHint->setWordWrap(true);
    m_solanaEdit = new QLineEdit;
    m_solanaEdit->setPlaceholderText("Your Solana address (for payouts, optional)");
    m_solanaEdit->setMaxLength(64);
    m_solanaEdit->setText(savedSolanaAddress());
    m_pubkeyLabel = new QLabel("Ed25519 public key: generating...");
    m_pubkeyLabel->setObjectName("modeHint");
    m_pubkeyLabel->setWordWrap(true);

    m_serverUrlEdit = new QLineEdit;
    m_serverUrlEdit->setPlaceholderText(kDefaultServerUrl);
    m_serverUrlEdit->setMaxLength(2048);
    const QString savedServerUrl = QSettings().value(kServerUrlSetting).toString().trimmed();
    const bool legacyWorkersDevUrl = QUrl(savedServerUrl).host().endsWith(
        QStringLiteral(".workers.dev"));
    m_serverUrlEdit->setText(
        savedServerUrl.isEmpty() || savedServerUrl == kLocalServerUrl ||
                legacyWorkersDevUrl
            ? kDefaultServerUrl
            : savedServerUrl);
    m_roomNameEdit = new QLineEdit;
    m_roomNameEdit->setPlaceholderText("Default repository room");
    m_roomNameEdit->setMaxLength(80);
    m_roomNameEdit->setText(QSettings().value(kRoomNameSetting, kDefaultRoomName).toString());
    m_passphraseEdit = new QLineEdit;
    m_passphraseEdit->setPlaceholderText("Mainnode room passphrase");
    m_passphraseEdit->setMaxLength(256);
    m_passphraseEdit->setEchoMode(QLineEdit::Password);
    m_passphraseEdit->setText(
        QSettings().value(kPassphraseSetting, kDefaultPassphrase).toString());

    auto *mainnodeHint = new QLabel(
        "Mainnodes relay encrypted repository-room ciphertext only.");
    mainnodeHint->setObjectName("modeHint");

    m_setupError = new QLabel;
    m_setupError->setWordWrap(true);
    m_setupError->setStyleSheet("color:#ff6b6b; background:transparent;");
    m_setupError->hide();

    auto *startButton = new QPushButton("Start ForkMesh node");
    startButton->setObjectName("primaryButton");
    startButton->setMinimumHeight(40);

    auto *joinButton = new QPushButton("Join the network (donate \xE2\x89\xA5 $1)");
    joinButton->setObjectName("ghostButton");
    joinButton->setCursor(Qt::PointingHandCursor);
    joinButton->setToolTip("Reserve your node name, pick repos to mirror, and "
                           "donate to become an active member. You can also keep "
                           "using ForkMesh free (view-only) by just starting the node.");

    m_updateButton = new QPushButton("Quick update");
    m_updateButton->setObjectName("ghostButton");
    m_updateButton->setCursor(Qt::PointingHandCursor);
    m_updateButton->setToolTip("Pull the latest version, rebuild, and relaunch");
    setOcticon(m_updateButton, "sync", 16);
    m_updateStatus = new QLabel;
    m_updateStatus->setObjectName("modeHint");
    m_updateStatus->setWordWrap(true);
    m_updateStatus->setAlignment(Qt::AlignHCenter);
    m_updateStatus->hide();

    auto *cardLayout = new QVBoxLayout(card);
    cardLayout->setContentsMargins(28, 28, 28, 28);
    cardLayout->setSpacing(10);
    cardLayout->addWidget(title);
    cardLayout->addWidget(subtitle);
    cardLayout->addWidget(versionLabel);
    cardLayout->addSpacing(14);
    cardLayout->addWidget(m_nameEdit);
    cardLayout->addWidget(nameHint);
    cardLayout->addWidget(m_solanaEdit);
    cardLayout->addWidget(m_pubkeyLabel);
    cardLayout->addSpacing(8);
    cardLayout->addWidget(m_serverUrlEdit);
    cardLayout->addWidget(m_roomNameEdit);
    cardLayout->addWidget(m_passphraseEdit);
    cardLayout->addSpacing(8);
    cardLayout->addWidget(mainnodeHint);
    cardLayout->addSpacing(8);
    cardLayout->addWidget(m_setupError);
    cardLayout->addSpacing(10);
    cardLayout->addWidget(startButton);
    cardLayout->addWidget(joinButton);
    cardLayout->addWidget(m_updateButton, 0, Qt::AlignHCenter);
    cardLayout->addWidget(m_updateStatus);

    auto *layout = new QVBoxLayout(page);
    layout->addStretch();
    layout->addWidget(card, 0, Qt::AlignHCenter);
    layout->addStretch();

    connect(startButton, &QPushButton::clicked, this, &MainWindow::startSession);
    connect(joinButton, &QPushButton::clicked, this, [this]() {
        // Make sure the identity + session are ready, then run the staged join.
        startSession();
        const QString name = accountNameFromInput(m_nameEdit->text(), QString());
        if (name.isEmpty()) {
            if (m_setupError) {
                m_setupError->setText("Choose a node name first.");
                m_setupError->show();
            }
            return;
        }
        ensureNodeAccount(name, m_solanaEdit->text().trimmed());
    });
    connect(m_nameEdit, &QLineEdit::returnPressed, this, &MainWindow::startSession);
    connect(m_nameEdit, &QLineEdit::textEdited, this, [](const QString &name) {
        saveProfileName(name);
    });
    connect(m_solanaEdit, &QLineEdit::textEdited, this, [this](const QString &address) {
        saveSolanaAddress(address);
        updateSolanaNotice();
    });
    connect(m_serverUrlEdit, &QLineEdit::textEdited, this, [](const QString &url) {
        QSettings().setValue(kServerUrlSetting, url.trimmed());
    });
    connect(m_roomNameEdit, &QLineEdit::textEdited, this, [](const QString &room) {
        QSettings().setValue(kRoomNameSetting, room.trimmed());
    });
    // Persisted so the room can be rejoined automatically after a restart.
    connect(m_passphraseEdit, &QLineEdit::textEdited, this,
            [](const QString &passphrase) {
                QSettings().setValue(kPassphraseSetting, passphrase);
            });
    connect(m_updateButton, &QPushButton::clicked, this, &MainWindow::runQuickUpdate);

    return page;
}

void MainWindow::startSession()
{
    QString name = accountNameFromInput(m_nameEdit->text(), QString());
    if (name.isEmpty()) {
        name = defaultDisplayName(m_profileIdentity);
        m_nameEdit->setText(name);
    }
    m_nameEdit->setText(name);
    saveProfileName(name);
    if (!m_profileIdentity.isValid() && !m_profileIdentity.load()) {
        m_setupError->setText(m_profileIdentity.errorString());
        m_setupError->show();
        return;
    }
    if (m_serverUrlEdit->text().trimmed().isEmpty())
        m_serverUrlEdit->setText(kDefaultServerUrl);

    // The one visible name is also the account owner / repo namespace. The
    // registration/login gate is still disabled; re-enable ensureNodeAccount()
    // here when the full account flow returns.
    m_accountName = name;
    QSettings().setValue(kAccountNameSetting, name);

    if (m_roomNameEdit->text().trimmed().isEmpty())
        m_roomNameEdit->setText(kDefaultRoomName);
    if (m_passphraseEdit->text().isEmpty())
        m_passphraseEdit->setText(kDefaultPassphrase);
    m_setupError->hide();
    m_userName = name;
    persistProfile();
    m_userAvatar = QSettings().value(kAvatarSetting).toByteArray();

    // Seed the Settings section's profile controls and the avatar nav button
    // (which now stands in for the old settings gear).
    if (m_settingsNameEdit)
        m_settingsNameEdit->setText(m_userName);
    setSettingsAvatar(m_userAvatar);
    updateAvatarButton();

    // Reset chat state.
    m_channels.clear();
    m_currentConversation.clear();
    m_history.clear();
    m_historyIds.clear();
    m_visibleRows.clear();
    m_reactions.clear();
    m_avatars.clear();
    m_dmNames.clear();
    m_openDms.clear();
    m_unread.clear();
    m_typing.clear();
    m_typingConversation.clear();
    m_typingStopTimer->stop();
    m_channelList->clear();
    m_dmList->clear();
    rebuildConversationView();

    QSettings().setValue(kServerUrlSetting, m_serverUrlEdit->text().trimmed());
    QSettings().setValue(kRoomNameSetting, m_roomNameEdit->text().trimmed());
    QSettings().setValue(kPassphraseSetting, m_passphraseEdit->text());
    persistEditsToActiveServer();
    const QUrl url(m_serverUrlEdit->text().trimmed());
    auto *server = new ServerNode(name, m_profileIdentity.publicKey(), url,
                                  m_roomNameEdit->text().trimmed(),
                                  m_passphraseEdit->text(),
                                  m_solanaEdit->text().trimmed(), this);
    attachBackend(server);
    if (!server->start())
        return;

    if (m_backend) {
        if (m_connectedAtMs <= 0)
            m_connectedAtMs = QDateTime::currentMSecsSinceEpoch();
        // Broadcast the generated identicon when no custom avatar is set, so
        // peers always see something on-brand for this node.
        m_backend->setAvatar(effectiveAvatar());
        for (const RepositoryRecord &repo : std::as_const(m_repositories))
            m_backend->addChannel(repositoryChannel(repo));
        m_encryptionLabel->setText("Mainnode encrypted");
        logSystem("Encryption: client-side AES-256-GCM mainnode room encryption.");
        const QJsonObject signedProfile =
            m_profileIdentity.signedProfile(m_userName,
                                            m_userName,
                                            m_solanaEdit->text());
        const QString profileBytes = QString::fromUtf8(
            QJsonDocument(signedProfile).toJson(QJsonDocument::Compact));
        logSystem("Identity: signed profile for " +
                  m_profileIdentity.shortPublicKey() + " (" +
                  QString::number(profileBytes.toUtf8().size()) + " bytes).");
        m_stack->setCurrentIndex(1);
        showSection(0); // land on the Home overview after connecting
        updateBreadcrumb();
        updateSolanaNotice();
        // Restore locally-saved chat history for this server/room so past
        // conversations are visible right away (deduped against any replay).
        loadChatHistory();
        // Serve already-mirrored repos live to the web for this session.
        startRepoHosts();
        // Heartbeat so an active, online node stays eligible for the reward
        // split. The server ignores it unless the account is active.
        if (!m_heartbeatTimer) {
            m_heartbeatTimer = new QTimer(this);
            m_heartbeatTimer->setInterval(60000);
            connect(m_heartbeatTimer, &QTimer::timeout, this,
                    &MainWindow::sendNodeHeartbeat);
        }
        m_heartbeatTimer->start();
        sendNodeHeartbeat();
    }
}

// ---- Account / node registration (staged join + reward heartbeat) ----------

void MainWindow::sendNodeHeartbeat()
{
    const QString name = m_accountName.isEmpty()
                             ? QSettings().value(kAccountNameSetting).toString().trimmed()
                             : m_accountName;
    if (name.isEmpty() || !m_profileIdentity.isValid())
        return;
    const QString ts = QString::number(QDateTime::currentMSecsSinceEpoch());
    const QByteArray canonical =
        ("forkmesh-heartbeat-v1\n" + name + "\n" + ts).toUtf8();
    const QString solana = savedSolanaAddress();
    const QJsonObject body{{"nodeName", name}, {"solana", solana}, {"ts", ts},
                           {"sig", m_profileIdentity.signData(canonical)}};
    QNetworkRequest request(accountsApiUrl("heartbeat"));
    request.setHeader(QNetworkRequest::ContentTypeHeader,
                      QStringLiteral("application/json"));
    QNetworkReply *reply = m_networkAccess->post(
        request, QJsonDocument(body).toJson(QJsonDocument::Compact));
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        const QJsonObject resp = QJsonDocument::fromJson(reply->readAll()).object();
        reply->deleteLater();
        // The server tells us whether this node is an admin; if so, start
        // watching for newly-joined users that need email verification.
        m_isAdmin = resp.value("isAdmin").toBool();
        if (m_isAdmin) {
            if (!m_adminPollTimer) {
                m_adminPollTimer = new QTimer(this);
                m_adminPollTimer->setInterval(90000);
                connect(m_adminPollTimer, &QTimer::timeout, this,
                        &MainWindow::pollPendingUsers);
            }
            if (!m_adminPollTimer->isActive()) {
                m_adminPollTimer->start();
                pollPendingUsers();
            }
        }
        updateNavSolanaBalance();
    });
}

void MainWindow::pollPendingUsers()
{
    if (!m_isAdmin)
        return;
    const QString node = accountOwner();
    if (node.isEmpty() || !m_profileIdentity.isValid())
        return;
    const QString ts = QString::number(QDateTime::currentMSecsSinceEpoch());
    const QByteArray canonical =
        ("forkmesh-admin-pending-v1\n" + node + "\n" + ts).toUtf8();
    QUrl url = accountsApiUrl("admin-pending");
    QUrlQuery query;
    query.addQueryItem("node", node);
    query.addQueryItem("ts", ts);
    query.addQueryItem("sig", m_profileIdentity.signData(canonical));
    url.setQuery(query);
    QNetworkReply *reply = m_networkAccess->get(QNetworkRequest(url));
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        const QJsonObject resp = QJsonDocument::fromJson(reply->readAll()).object();
        reply->deleteLater();
        if (!resp.value("ok").toBool())
            return;
        QStringList current, fresh;
        for (const QJsonValue &v : resp.value("pending").toArray()) {
            const QString name = v.toObject().value("name").toString();
            if (name.isEmpty())
                continue;
            current.append(name);
            if (!m_seenPendingUsers.contains(name))
                fresh.append(name);
        }
        m_seenPendingUsers = current;
        if (fresh.isEmpty())
            return;
        const QString msg =
            QStringLiteral("%1 new user(s) joined and need email verification:\n\n%2")
                .arg(fresh.size())
                .arg(fresh.join(", "));
        if (m_trayIcon && QSystemTrayIcon::supportsMessages())
            m_trayIcon->showMessage("ForkMesh — new user", msg,
                                    QSystemTrayIcon::Information, 8000);
        if (QMessageBox::information(this, "New user joined",
                                     msg + "\n\nReview and verify now?",
                                     QMessageBox::Yes | QMessageBox::No) ==
            QMessageBox::Yes)
            showAdminVerifyDialog();
    });
}

void MainWindow::showAdminVerifyDialog()
{
    const QString node = accountOwner();
    if (node.isEmpty() || !m_profileIdentity.isValid())
        return;
    const QString ts = QString::number(QDateTime::currentMSecsSinceEpoch());
    const QByteArray canonical =
        ("forkmesh-admin-pending-v1\n" + node + "\n" + ts).toUtf8();
    QUrl url = accountsApiUrl("admin-pending");
    QUrlQuery query;
    query.addQueryItem("node", node);
    query.addQueryItem("ts", ts);
    query.addQueryItem("sig", m_profileIdentity.signData(canonical));
    url.setQuery(query);
    QNetworkReply *reply = m_networkAccess->get(QNetworkRequest(url));
    QEventLoop loop;
    connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();
    const QJsonObject resp = QJsonDocument::fromJson(reply->readAll()).object();
    reply->deleteLater();
    const QJsonArray pending = resp.value("pending").toArray();

    QDialog dialog(this);
    dialog.setWindowTitle("Verify new users");
    dialog.resize(480, 420);
    auto *layout = new QVBoxLayout(&dialog);
    layout->addWidget(new QLabel(
        "New users awaiting manual email verification (placeholder until an "
        "email service such as Amazon SES is connected):"));
    auto *scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    auto *inner = new QWidget;
    auto *rows = new QVBoxLayout(inner);
    if (pending.isEmpty())
        rows->addWidget(new QLabel("<i>No users awaiting verification.</i>"));
    for (const QJsonValue &v : pending) {
        const QJsonObject obj = v.toObject();
        const QString name = obj.value("name").toString();
        const QString email = obj.value("email").toString();
        if (name.isEmpty())
            continue;
        auto *row = new QHBoxLayout;
        auto *label = new QLabel(
            QStringLiteral("<b>%1</b><br><span style='color:#8b949e'>%2</span>")
                .arg(name.toHtmlEscaped(), email.toHtmlEscaped()));
        label->setTextFormat(Qt::RichText);
        row->addWidget(label, 1);
        auto *btn = new QPushButton("Verify email");
        btn->setObjectName("ghostButton");
        btn->setCursor(Qt::PointingHandCursor);
        row->addWidget(btn);
        rows->addLayout(row);
        connect(btn, &QPushButton::clicked, &dialog, [this, name, btn]() {
            btn->setEnabled(false);
            btn->setText(adminVerifyEmail(name) ? "Verified \xE2\x9C\x93" : "Failed");
        });
    }
    rows->addStretch();
    scroll->setWidget(inner);
    layout->addWidget(scroll, 1);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    dialog.exec();
}

bool MainWindow::adminVerifyEmail(const QString &target)
{
    const QString node = accountOwner();
    if (node.isEmpty() || target.isEmpty() || !m_profileIdentity.isValid())
        return false;
    const QString ts = QString::number(QDateTime::currentMSecsSinceEpoch());
    const QByteArray canonical =
        ("forkmesh-admin-verify-email-v1\n" + node + "\n" + target + "\n" + ts)
            .toUtf8();
    int status = 0;
    const QJsonObject resp = postAccountSync(
        "admin-verify-email",
        QJsonObject{{"node", node}, {"target", target}, {"ts", ts},
                    {"sig", m_profileIdentity.signData(canonical)}},
        &status);
    if (status == 200 && resp.value("ok").toBool()) {
        m_seenPendingUsers.removeAll(target);
        logSystem("Admin: verified email for " + target);
        return true;
    }
    return false;
}

QString MainWindow::accountOwner() const
{
    if (!m_accountName.isEmpty())
        return m_accountName;
    const QString stored = QSettings().value(kAccountNameSetting).toString();
    if (!stored.isEmpty())
        return stored;
    return accountNameFromInput(m_userName, QStringLiteral("owner"));
}

QString MainWindow::catalogOwner(const RepositoryRecord &repo) const
{
    const QString account = accountOwner();
    return account.isEmpty() ? repoSegment(repo.owner, QStringLiteral("owner"))
                             : account;
}

QUrl MainWindow::accountsApiUrl(const QString &leaf) const
{
    QUrl url = catalogApiUrl(); // same host, http(s) scheme
    url.setPath(QStringLiteral("/api/accounts/") + leaf);
    url.setQuery(QString());
    return url;
}

QJsonObject MainWindow::postAccountSync(const QString &leaf,
                                        const QJsonObject &body, int *status)
{
    QNetworkRequest request(accountsApiUrl(leaf));
    request.setHeader(QNetworkRequest::ContentTypeHeader,
                      QStringLiteral("application/json"));
    QNetworkReply *reply = m_networkAccess->post(
        request, QJsonDocument(body).toJson(QJsonDocument::Compact));
    QEventLoop loop;
    connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();
    if (status)
        *status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QByteArray data = reply->readAll();
    reply->deleteLater();
    return QJsonDocument::fromJson(data).object();
}

QJsonObject MainWindow::getAccountSync(const QString &leaf, int *status)
{
    QNetworkReply *reply = m_networkAccess->get(QNetworkRequest(accountsApiUrl(leaf)));
    QEventLoop loop;
    connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();
    if (status)
        *status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QByteArray data = reply->readAll();
    reply->deleteLater();
    return QJsonDocument::fromJson(data).object();
}

QJsonArray MainWindow::fetchCatalogRepos()
{
    QUrl url = catalogApiUrl();
    url.setPath(QStringLiteral("/api/repositories"));
    url.setQuery(QString());
    QNetworkReply *reply = m_networkAccess->get(QNetworkRequest(url));
    QEventLoop loop;
    connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();
    const QJsonObject obj = QJsonDocument::fromJson(reply->readAll()).object();
    reply->deleteLater();
    return obj.value("repositories").toArray();
}

int MainWindow::fetchNodesOnline()
{
    QUrl url = catalogApiUrl();
    url.setPath(QStringLiteral("/api/network/stats"));
    url.setQuery(QString());
    QNetworkReply *reply = m_networkAccess->get(QNetworkRequest(url));
    QEventLoop loop;
    connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();
    const QJsonObject obj = QJsonDocument::fromJson(reply->readAll()).object();
    reply->deleteLater();
    return obj.value("hosts").toInt();
}

void MainWindow::mirrorCatalogRepo(const QString &owner, const QString &name,
                                   const QString &cloneUrl)
{
    if (owner.isEmpty() || name.isEmpty() || cloneUrl.isEmpty())
        return;
    for (int i = 0; i < m_repositories.size(); ++i) {
        const RepositoryRecord &r = m_repositories.at(i);
        if (r.owner != owner || r.name != name)
            continue;
        if (r.previewOnly) {
            mirrorPreviewRepository(i);
            return;
        }
        return; // already mirroring this repo
    }
    RepositoryRecord repo;
    repo.owner = owner;
    repo.name = name;
    repo.cloneUrl = cloneUrl;
    repo.publishToNetwork = true;
    repo.hostedSinceMs = QDateTime::currentMSecsSinceEpoch();
    repo.mirrorPath = repositoryMirrorRoot() + "/" +
                      repoSegment(owner, QStringLiteral("owner")) + "-" +
                      repoSegment(name, QStringLiteral("repository")) + ".git";
    m_repositories.append(repo);
    saveRepositories();
    if (m_backend)
        m_backend->addChannel(repositoryChannel(repo));
    refreshRepositoryList();
    logSystem("Mirroring " + owner + "/" + name + " from " + cloneUrl);
    syncRepository(m_repositories.size() - 1);
}

QString MainWindow::hostedCloneUrl(const QString &owner, const QString &name) const
{
    const QString safeOwner = repoSegment(owner, QStringLiteral("owner"));
    const QString safeName = repoSegment(name, QStringLiteral("repository"));
    if (safeOwner.isEmpty() || safeName.isEmpty())
        return QString();
    // Catalog records published by local nodes omit cloneUrl; the repo is still
    // reachable through the mainnode's git smart-HTTP route, which forwards to
    // whichever client is currently hosting it.
    QUrl url = catalogApiUrl(); // http(s) on the mainnode host
    url.setPath(QStringLiteral("/") + safeOwner + QLatin1Char('/') + safeName);
    url.setQuery(QString());
    url.setFragment(QString());
    return url.toString();
}

void MainWindow::ensureFlagshipRepo()
{
    if (!m_networkAccess)
        return;
    // Already mirroring the ForkMesh project repo — nothing to bootstrap.
    for (const RepositoryRecord &repo : std::as_const(m_repositories))
        if (!repo.previewOnly &&
            repo.name.compare(QStringLiteral("forkmesh"), Qt::CaseInsensitive) == 0)
            return;

    const QJsonArray repos = fetchCatalogRepos();
    QString owner, cloneUrl;
    bool ownerLive = false;
    for (const QJsonValue &v : repos) {
        const QJsonObject r = v.toObject();
        if (r.value("name").toString().compare(QStringLiteral("forkmesh"),
                                               Qt::CaseInsensitive) != 0)
            continue;
        const QString candidateOwner = r.value("owner").toString();
        if (candidateOwner.isEmpty())
            continue;
        QString candidateUrl = r.value("cloneUrl").toString().trimmed();
        if (candidateUrl.isEmpty())
            candidateUrl = hostedCloneUrl(candidateOwner, QStringLiteral("forkmesh"));
        if (candidateUrl.isEmpty())
            continue;
        const bool live = r.value("liveHost").toBool();
        // Prefer a live host; otherwise keep the first usable entry as a fallback.
        if (live || owner.isEmpty()) {
            owner = candidateOwner;
            cloneUrl = candidateUrl;
            ownerLive = live;
        }
        if (live)
            break;
    }
    if (owner.isEmpty() || cloneUrl.isEmpty())
        return;
    if (!ownerLive)
        logSystem("No live ForkMesh host right now; mirroring " + owner +
                  "/forkmesh anyway so it appears once a host comes online.");
    mirrorCatalogRepo(owner, QStringLiteral("forkmesh"), cloneUrl);
}

bool MainWindow::ensureNodeAccount(const QString &accountName, const QString &solana)
{
    Q_UNUSED(solana);
    if (m_accountAuthenticated && m_accountName == accountName)
        return true;
    if (!isValidNodeName(accountName)) {
        QMessageBox::warning(this, "Join the network",
                             "Choose a valid node name first (lowercase letters, "
                             "numbers and hyphens; start with a letter).");
        return false;
    }

    int status = 0;
    const QJsonObject lookup = getAccountSync(accountName, &status);
    // An account that has already been finalized logs in; anything else (new,
    // reserved, or mid-donation) runs the staged join.
    if (lookup.value("exists").toBool() &&
        lookup.value("status").toString() == "active")
        return runLoginFlow(accountName);
    return runSignupFlow(accountName, solana);
}

// POST /api/accounts/login — log in by node name or email (+ optional TOTP).
bool MainWindow::verifyTotpLogin(const QString &accountName,
                                 const QString &password, const QString &totp)
{
    int status = 0;
    const QJsonObject resp = postAccountSync(
        "login",
        QJsonObject{{"identifier", accountName}, {"password", password},
                    {"totp", totp}},
        &status);
    if (status == 200 && resp.value("ok").toBool()) {
        m_accountAuthenticated = true;
        m_accountName = resp.value("nodeName").toString(accountName);
        m_accountTier = QStringLiteral("active");
        m_accountSolanaVerified = true; // joined = active network member
        return true;
    }
    const QString err = resp.value("error").toString();
    QMessageBox::warning(this, "Log in",
                         err == "bad_totp"
                             ? "Incorrect authenticator code."
                             : err == "bad_password"
                                   ? "Incorrect password."
                                   : err == "account_not_active"
                                         ? "That account hasn't finished signup yet."
                                         : "Login failed" +
                                               (err.isEmpty() ? QString() : ": " + err) +
                                               ".");
    return false;
}

bool MainWindow::runLoginFlow(const QString &accountName)
{
    QDialog dialog(this);
    dialog.setWindowTitle("Log in to " + accountName);
    auto *form = new QFormLayout(&dialog);
    form->addRow(new QLabel("Log in to your account to join the network."));
    auto *passEdit = new QLineEdit;
    passEdit->setEchoMode(QLineEdit::Password);
    auto *totpEdit = new QLineEdit;
    totpEdit->setPlaceholderText("6-digit code (only if you enabled 2FA)");
    totpEdit->setMaxLength(6);
    form->addRow("Password", passEdit);
    form->addRow("2FA code", totpEdit);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    buttons->button(QDialogButtonBox::Ok)->setText("Log in");
    form->addRow(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    while (dialog.exec() == QDialog::Accepted) {
        if (verifyTotpLogin(accountName, passEdit->text(), totpEdit->text().trimmed()))
            return true;
    }
    if (m_setupError) {
        m_setupError->setText("Account login is required to join the network.");
        m_setupError->show();
    }
    return false;
}

// Staged "join the network" flow: reserve the node name (signed) → pick at
// least one repo to mirror → donate with Solana → set the email + password that
// unlock universal login. Replaces the old one-shot signup. Returns true once
// the account is finalized (active).
bool MainWindow::runSignupFlow(const QString &accountName, const QString &solana)
{
    Q_UNUSED(solana);

    // Step 1 — reserve the node name, signed with the local identity so the
    // name is bound to this key.
    {
        const QString ts = QString::number(QDateTime::currentMSecsSinceEpoch());
        const QByteArray canonical =
            ("forkmesh-reserve-v1\n" + accountName + "\n" + ts).toUtf8();
        int status = 0;
        const QJsonObject resp = postAccountSync(
            "reserve",
            QJsonObject{{"nodeName", accountName},
                        {"pubkey", m_profileIdentity.publicKey()},
                        {"ts", ts},
                        {"sig", m_profileIdentity.signData(canonical)}},
            &status);
        if (!resp.value("ok").toBool()) {
            const QString err = resp.value("error").toString();
            QMessageBox::warning(this, "Join the network",
                                 err == "node_name_taken"
                                     ? "That node name is already taken — pick another."
                                     : "Could not reserve that name" +
                                           (err.isEmpty() ? QString() : ": " + err) + ".");
            return false;
        }
    }

    // Step 2 — choose at least one repository to mirror.
    if (!runRepoPickStep())
        return false;

    // Step 3 — donate to the generated address and wait for confirmation.
    if (!runDonationStep(accountName))
        return false;

    // Step 4 — set email + password (universal login), signed to prove key
    // ownership of the reserved name.
    QDialog dialog(this);
    dialog.setWindowTitle("Finish creating " + accountName);
    auto *form = new QFormLayout(&dialog);
    auto *intro = new QLabel(
        QStringLiteral("Donation received. Set the email and password that log you "
                       "into <b>%1</b> from any device.").arg(accountName.toHtmlEscaped()));
    intro->setWordWrap(true);
    intro->setTextFormat(Qt::RichText);
    auto *emailEdit = new QLineEdit;
    emailEdit->setPlaceholderText("you@example.com");
    auto *passEdit = new QLineEdit;
    passEdit->setEchoMode(QLineEdit::Password);
    passEdit->setPlaceholderText("At least 8 characters");
    auto *confirmEdit = new QLineEdit;
    confirmEdit->setEchoMode(QLineEdit::Password);
    form->addRow(intro);
    form->addRow("Email", emailEdit);
    form->addRow("Password", passEdit);
    form->addRow("Confirm", confirmEdit);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    buttons->button(QDialogButtonBox::Ok)->setText("Create account");
    form->addRow(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    while (dialog.exec() == QDialog::Accepted) {
        const QString email = emailEdit->text().trimmed();
        const QString password = passEdit->text();
        if (!email.contains('@')) {
            QMessageBox::warning(this, "Create account", "Enter a valid email.");
            continue;
        }
        if (password.size() < 8) {
            QMessageBox::warning(this, "Create account",
                                 "Password must be at least 8 characters.");
            continue;
        }
        if (password != confirmEdit->text()) {
            QMessageBox::warning(this, "Create account", "Passwords do not match.");
            continue;
        }
        const QString ts = QString::number(QDateTime::currentMSecsSinceEpoch());
        const QByteArray canonical =
            ("forkmesh-finalize-v1\n" + accountName + "\n" + email + "\n" + ts).toUtf8();
        int status = 0;
        const QJsonObject resp = postAccountSync(
            "finalize",
            QJsonObject{{"nodeName", accountName}, {"email", email},
                        {"password", password},
                        {"pubkey", m_profileIdentity.publicKey()},
                        {"ts", ts}, {"sig", m_profileIdentity.signData(canonical)}},
            &status);
        if (status != 201 || !resp.value("ok").toBool()) {
            const QString err = resp.value("error").toString();
            QMessageBox::warning(this, "Create account",
                                 err == "email_taken"
                                     ? "That email is already registered."
                                     : err == "donation_required"
                                           ? "We haven't confirmed your donation yet."
                                           : "Could not create the account" +
                                                 (err.isEmpty() ? QString() : ": " + err) +
                                                 ".");
            continue;
        }
        m_accountAuthenticated = true;
        m_accountName = accountName;
        m_accountTier = QStringLiteral("active");
        m_accountSolanaVerified = true;
        QMessageBox::information(this, "Welcome to ForkMesh",
                                 "You're in — your node is registered.");
        return true;
    }
    if (m_setupError) {
        m_setupError->setText("Set an email and password to finish joining.");
        m_setupError->show();
    }
    return false;
}

// Step 2 of the join: show the catalog of repositories across connected servers
// with a payout estimate, and require the user to pick at least one to mirror.
bool MainWindow::runRepoPickStep()
{
    QDialog dialog(this);
    dialog.setWindowTitle("Pick repositories to mirror");
    dialog.resize(560, 460);
    auto *outer = new QHBoxLayout(&dialog);

    // Left: catalog list with checkboxes.
    auto *leftCol = new QVBoxLayout;
    leftCol->addWidget(new QLabel("Choose at least one repository to mirror:"));
    auto *list = new QListWidget;
    leftCol->addWidget(list, 1);

    const QJsonArray repos = fetchCatalogRepos();
    for (const QJsonValue &v : repos) {
        const QJsonObject r = v.toObject();
        const QString owner = r.value("owner").toString();
        const QString name = r.value("name").toString();
        if (owner.isEmpty() || name.isEmpty())
            continue;
        QString cloneUrl = r.value("cloneUrl").toString().trimmed();
        if (cloneUrl.isEmpty())
            cloneUrl = hostedCloneUrl(owner, name);
        auto *item = new QListWidgetItem(owner + "/" + name, list);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(Qt::Unchecked);
        item->setData(Qt::UserRole, cloneUrl);
        item->setData(Qt::UserRole + 1, owner);
        item->setData(Qt::UserRole + 2, name);
    }
    if (list->count() == 0)
        leftCol->addWidget(new QLabel(
            "<i>No repositories are published yet — you can add one later from "
            "the Repos tab.</i>"));
    outer->addLayout(leftCol, 2);

    // Right: payout estimate (illustrative only — the reward engine is WIP).
    auto *rightCol = new QVBoxLayout;
    auto *calc = new QLabel;
    calc->setWordWrap(true);
    calc->setTextFormat(Qt::RichText);
    const int nodes = qMax(1, fetchNodesOnline());
    const double payoutPerJoin = 0.0001;
    calc->setText(QStringLiteral(
        "<b style='color:#3fb950'>Live earnings estimate</b><br><br>"
        "Nodes online: <b>%1</b><br>Payout per join: <b>%2 SOL</b><br><br>"
        "Est. earnings: <b>%3 SOL</b><br>"
        "<span style='color:#8b949e'>Illustrative only — the reward system is in "
        "progress. Roughly half of each join funds ForkMesh; the rest is shared "
        "across nodes by data mirrored and uptime.</span>")
        .arg(nodes)
        .arg(payoutPerJoin, 0, 'f', 4)
        .arg(nodes * payoutPerJoin, 0, 'f', 8));
    rightCol->addWidget(calc);
    rightCol->addStretch();
    outer->addLayout(rightCol, 1);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    buttons->button(QDialogButtonBox::Ok)->setText("Continue");
    auto *bottom = new QVBoxLayout;
    bottom->addWidget(buttons);
    rightCol->addLayout(bottom);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    while (dialog.exec() == QDialog::Accepted) {
        QList<QListWidgetItem *> chosen;
        for (int i = 0; i < list->count(); ++i)
            if (list->item(i)->checkState() == Qt::Checked)
                chosen.append(list->item(i));
        if (chosen.isEmpty() && list->count() > 0) {
            QMessageBox::warning(this, "Pick repositories",
                                 "Select at least one repository to mirror.");
            continue;
        }
        for (QListWidgetItem *item : chosen)
            mirrorCatalogRepo(item->data(Qt::UserRole + 1).toString(),
                              item->data(Qt::UserRole + 2).toString(),
                              item->data(Qt::UserRole).toString());
        return true;
    }
    return false;
}

// Step 3 of the join: fetch a Solana payment request for this signup, show it
// with a QR code, and poll until the donation confirms.
bool MainWindow::runDonationStep(const QString &accountName)
{
    int status = 0;
    const QJsonObject addr = postAccountSync(
        "donation-address", QJsonObject{{"nodeName", accountName}}, &status);
    if (!addr.value("ok").toBool()) {
        QMessageBox::warning(this, "Join the network",
                             "Could not generate a donation address. Try again.");
        return false;
    }
    const QString address = addr.value("address").toString();
    const QString uri = addr.value("uri").toString(address);
    const QString amountSol = addr.value("amountSol").toString("0.005000000");

    QDialog dialog(this);
    dialog.setWindowTitle("Join ForkMesh — donate to activate");
    auto *layout = new QVBoxLayout(&dialog);
    auto *info = new QLabel(
        QStringLiteral("Send at least <b>%1 SOL</b> to the Solana payment request "
                       "below to join the network. ForkMesh monitors the payment "
                       "reference for confirmation.").arg(amountSol));
    info->setWordWrap(true);
    info->setTextFormat(Qt::RichText);
    layout->addWidget(info);

    auto *qrLabel = new QLabel;
    qrLabel->setAlignment(Qt::AlignCenter);
    const QImage qr = QrCode::encodeToImage(uri, 5, 3);
    if (!qr.isNull())
        qrLabel->setPixmap(QPixmap::fromImage(qr));
    layout->addWidget(qrLabel);

    auto *addrLabel = new QLabel(address);
    addrLabel->setWordWrap(true);
    addrLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    addrLabel->setStyleSheet("font-family:monospace;");
    layout->addWidget(addrLabel);

    auto *statusLabel = new QLabel("Waiting for your donation…");
    statusLabel->setStyleSheet("color:#d29922;");
    layout->addWidget(statusLabel);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Cancel);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    bool paid = false;
    QTimer poll;
    poll.setInterval(4000);
    connect(&poll, &QTimer::timeout, &dialog, [&]() {
        QUrl url = accountsApiUrl("donation-status");
        url.setQuery("nodeName=" + QString::fromUtf8(QUrl::toPercentEncoding(accountName)));
        QNetworkReply *reply = m_networkAccess->get(QNetworkRequest(url));
        QEventLoop loop;
        connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
        loop.exec();
        const QJsonObject st =
            QJsonDocument::fromJson(reply->readAll()).object();
        reply->deleteLater();
        if (st.value("paid").toBool()) {
            paid = true;
            statusLabel->setText("Donation received!");
            statusLabel->setStyleSheet("color:#3fb950;");
            poll.stop();
            dialog.accept();
        } else {
            const qint64 got = st.value("receivedLamports").toVariant().toLongLong();
            statusLabel->setText(
                QStringLiteral("Waiting for your donation… (received %1 SOL)")
                    .arg(got / 1000000000.0, 0, 'f', 9));
        }
    });
    poll.start();
    dialog.exec();
    poll.stop();
    return paid;
}

void MainWindow::verifyWallet()
{
    // Repurposed: the "join / verify" button now drives the staged join flow,
    // which both registers the account and marks it an active network member.
    const QString name = accountOwner();
    if (name.isEmpty()) {
        QMessageBox::information(this, "Join the network",
                                 "Set your node name on the setup screen first.");
        return;
    }
    if (ensureNodeAccount(name, m_solanaEdit ? m_solanaEdit->text().trimmed() : QString())) {
        if (m_profileEligibility)
            m_profileEligibility->setText(
                QStringLiteral("<span style='color:#3fb950'>Active "
                               "\xC2\xB7 network member</span>"));
    }
}

void MainWindow::persistProfile()
{
    const QString name = accountNameFromInput(m_nameEdit->text(), m_userName);
    m_nameEdit->setText(name);
    saveProfileName(name);
    saveSolanaAddress(m_solanaEdit->text().trimmed());
    updateNavSolanaBalance();
}

// --------------------------------------------------------------- quick update

void MainWindow::setUpdateStatus(const QString &status, bool isError)
{
    QLabel *label = m_buildStatusLabel ? m_buildStatusLabel : m_updateStatus;
    if (!label)
        return;
    label->setStyleSheet(isError ? "color:#ff6b6b; background:transparent;"
                                  : "color:#9ca3af; background:transparent;");
    label->setText(status);
    label->show();
}

void MainWindow::runUpdateStep(const QString &program, const QStringList &arguments,
                               const QString &workingDir,
                               std::function<void()> onSuccess)
{
    auto *process = new QProcess(this);
    process->setWorkingDirectory(workingDir);
    connect(process, &QProcess::finished, this,
            [this, process, onSuccess](int exitCode, QProcess::ExitStatus) {
                const QString errors =
                    QString::fromUtf8(process->readAllStandardError()).trimmed();
                process->deleteLater();
                if (exitCode != 0) {
                    stopRefreshSpin();
                    setUpdateStatus("Update failed: " + errors.right(300), true);
                    if (m_buildButton)
                        m_buildButton->setEnabled(true);
                    return;
                }
                onSuccess();
            });
    connect(process, &QProcess::errorOccurred, this, [this, process] {
        stopRefreshSpin();
        setUpdateStatus("Update failed: could not run " + process->program(), true);
        process->deleteLater();
        if (m_buildButton)
            m_buildButton->setEnabled(true);
    });
    process->start(program, arguments);
}

void MainWindow::runQuickUpdate()
{
    saveProfileName(m_nameEdit->text());
    m_buildButton = m_updateButton;
    m_buildStatusLabel = m_updateStatus;
    m_updateButton->setEnabled(false);
    const QString clientDir = updateClientDir();

    if (QDir(clientDir).exists("CMakeLists.txt")) {
        setUpdateStatus("Pulling the latest version...");
        runUpdateStep("git", {"pull", "--ff-only"}, clientDir,
                      [this, clientDir] { buildAndRelaunch(clientDir); });
    } else {
        // No checkout anywhere (binary installed without one): clone a fresh
        // copy into the app data directory and update from there from now on.
        const QString repoDir = QFileInfo(clientDir).absolutePath(); // .../src
        QDir().mkpath(QFileInfo(repoDir).absolutePath());
        setUpdateStatus("Downloading the latest version...");
        runUpdateStep("git", {"clone", "--depth", "1", kRepoUrl, repoDir},
                      QFileInfo(repoDir).absolutePath(),
                      [this, clientDir] { buildAndRelaunch(clientDir); });
    }
}

void MainWindow::runUpdateStepUser(const QString &program,
                                   const QStringList &arguments,
                                   const QString &workingDir,
                                   std::function<void()> onSuccess)
{
    if (m_updateAsUser.isEmpty()) {
        runUpdateStep(program, arguments, workingDir, std::move(onSuccess));
        return;
    }
    // Run the command as the invoking non-root user so files it writes are owned
    // by them and land under their home, never under /root.
    QStringList wrapped{"-u", m_updateAsUser, "-H", program};
    wrapped += arguments;
    runUpdateStep("sudo", wrapped, workingDir, std::move(onSuccess));
}

void MainWindow::buildAndRelaunch(const QString &clientDir, const QString &asUser,
                                  const QString &relaunchPath)
{
    m_updateAsUser = asUser;
    const QString appPath = relaunchPath.isEmpty()
                                ? QCoreApplication::applicationFilePath()
                                : relaunchPath;
    const QString buildDir = clientDir + "/build";
    setUpdateStatus("Configuring...");
    runUpdateStepUser("cmake", cmakeConfigureArgs(clientDir, buildDir),
                      clientDir, [this, buildDir, appPath] {
        setUpdateStatus("Rebuilding...");
        runUpdateStepUser("cmake",
                          {"--build", buildDir, "-j",
                           QString::number(QThread::idealThreadCount())},
                          buildDir, [this, buildDir, appPath] {
            const QString built = builtExecutablePath(buildDir);
            installAndRelaunch(built, appPath);
        });
    });
}

void MainWindow::installAndRelaunch(const QString &built, const QString &appPath)
{
    if (!m_updateAsUser.isEmpty()) {
        // Install and relaunch as the user so the binary is theirs, not root's.
        const QString binDir = QFileInfo(appPath).absolutePath();
        const QString script =
            QStringLiteral("mkdir -p %1 && cp -f %2 %3 && chmod 0755 %3")
                .arg(shellSingleQuote(binDir), shellSingleQuote(built),
                     shellSingleQuote(appPath));
        setUpdateStatus("Installing for " + m_updateAsUser + "...");
        runUpdateStep("sudo", {"-u", m_updateAsUser, "-H", "sh", "-c", script},
                      QDir::tempPath(), [this, appPath] {
            setUpdateStatus("Relaunching...");
            const QString user = m_updateAsUser;
            QProcess::startDetached("sudo", {"-u", user, "-H", appPath});
            QCoreApplication::quit();
        });
        return;
    }

    // In-process update: replace the running binary over its own path (the
    // running inode stays valid) and relaunch directly.
    if (QFileInfo(built).canonicalFilePath() !=
        QFileInfo(appPath).canonicalFilePath()) {
        QFile::remove(appPath);
        if (!QFile::copy(built, appPath)) {
            setUpdateStatus("Update failed: could not replace " + appPath, true);
            if (m_buildButton)
                m_buildButton->setEnabled(true);
            return;
        }
        QFile::setPermissions(appPath,
                              QFile::ReadOwner | QFile::WriteOwner |
                              QFile::ExeOwner | QFile::ReadGroup |
                              QFile::ExeGroup | QFile::ReadOther |
                              QFile::ExeOther);
    }
    setUpdateStatus("Relaunching...");
    QProcess::startDetached(appPath, {});
    QCoreApplication::quit();
}

QString MainWindow::resolveInstallCloneUrl()
{
    if (!m_networkAccess)
        return QString();
    // Ask the mainnode which node is currently hosting a live forkmesh mirror,
    // then build the hosted git URL the installer would clone from.
    QUrl url = catalogApiUrl(); // http(s) on the mainnode host
    url.setPath(QStringLiteral("/api/install-source"));
    url.setQuery(QString());
    QNetworkReply *reply = m_networkAccess->get(QNetworkRequest(url));
    QEventLoop loop;
    connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();
    const QJsonObject obj = QJsonDocument::fromJson(reply->readAll()).object();
    reply->deleteLater();
    const QString node = obj.value("node").toString().trimmed();
    const QString repo = obj.value("repo").toString(QStringLiteral("forkmesh")).trimmed();
    if (node.isEmpty() || repo.isEmpty())
        return QString();
    QUrl clone = catalogApiUrl();
    clone.setPath(QStringLiteral("/") + node + QLatin1Char('/') + repo);
    clone.setQuery(QString());
    clone.setFragment(QString());
    return clone.toString();
}

void MainWindow::updateRebuildRestart()
{
    m_buildButton = m_rebuildButton;
    m_buildStatusLabel = m_rebuildStatus;

    // Under sudo, target the invoking user's home so nothing is written to /root.
    const QString user = invokingNonRootUser();
    const QString home = user.isEmpty() ? QString() : homeForUser(user);
    const QString clientDir =
        user.isEmpty() ? updateClientDir() : clientDirUnderHome(home);
    const QString relaunchPath =
        user.isEmpty() ? QCoreApplication::applicationFilePath()
                       : (home + QStringLiteral("/.local/bin/forkmesh"));
    // Build steps and the relaunch run as the user when we are root under sudo.
    m_updateAsUser = user;

    if (m_rebuildButton)
        m_rebuildButton->setEnabled(false);
    flashMessage(QStringLiteral("Updating ForkMesh from the install mirror..."));
    setUpdateStatus(user.isEmpty()
                        ? QStringLiteral("Finding an online ForkMesh mirror...")
                        : QStringLiteral("Finding an online ForkMesh mirror "
                                         "(installing for %1)...").arg(user));

    const QString installUrl = resolveInstallCloneUrl();
    if (installUrl.isEmpty()) {
        m_updateAsUser.clear();
        setUpdateStatus("No online ForkMesh mirror is available right now. "
                        "Try again shortly.",
                        true);
        flashMessage("No online ForkMesh mirror is available right now.", true);
        if (m_rebuildButton)
            m_rebuildButton->setEnabled(true);
        return;
    }

    const QString repoDir = QFileInfo(clientDir).absolutePath(); // .../src
    const bool haveCheckout = QDir(repoDir).exists(QStringLiteral(".git"));

    if (haveCheckout) {
        setUpdateStatus("Pulling a fresh copy from " + installUrl + "...");
        // Repoint origin at the freshly resolved live mirror, then fast-forward.
        runUpdateStepUser("git", {"-C", repoDir, "remote", "set-url", "origin",
                                  installUrl},
                          repoDir, [this, repoDir, clientDir, user, relaunchPath] {
            runUpdateStepUser("git", {"-C", repoDir, "pull", "--ff-only"}, repoDir,
                              [this, clientDir, user, relaunchPath] {
                buildAndRelaunch(clientDir, user, relaunchPath);
            });
        });
    } else {
        const QString parent = QFileInfo(repoDir).absolutePath();
        setUpdateStatus("Downloading a fresh copy from " + installUrl + "...");
        runUpdateStepUser("mkdir", {"-p", parent}, QDir::tempPath(),
                          [this, installUrl, repoDir, parent, clientDir, user,
                           relaunchPath] {
            runUpdateStepUser("git", {"clone", "--depth", "1", installUrl, repoDir},
                              parent, [this, clientDir, user, relaunchPath] {
                buildAndRelaunch(clientDir, user, relaunchPath);
            });
        });
    }
}

// -------------------------------------------------------------- server rail

void MainWindow::loadServers()
{
    m_servers.clear();
    const QString json = QSettings().value(kServersArray).toString();
    const QJsonArray array = QJsonDocument::fromJson(json.toUtf8()).array();
    for (const QJsonValue &value : array) {
        const QJsonObject obj = value.toObject();
        const QString url = obj.value("url").toString().trimmed();
        if (url.isEmpty())
            continue;
        ServerConfig server;
        server.url = url;
        server.room = obj.value("room").toString(kDefaultRoomName);
        server.passphrase = obj.value("passphrase").toString(kDefaultPassphrase);
        m_servers.append(server);
    }

    // Migration: seed the list from the legacy single-server keys (or defaults).
    if (m_servers.isEmpty()) {
        const QString savedUrl =
            QSettings().value(kServerUrlSetting).toString().trimmed();
        const bool legacyWorkersDevUrl =
            QUrl(savedUrl).host().endsWith(QStringLiteral(".workers.dev"));
        ServerConfig server;
        server.url = (savedUrl.isEmpty() || savedUrl == kLocalServerUrl ||
                      legacyWorkersDevUrl)
                         ? kDefaultServerUrl
                         : savedUrl;
        server.room =
            QSettings().value(kRoomNameSetting, kDefaultRoomName).toString();
        server.passphrase =
            QSettings().value(kPassphraseSetting, kDefaultPassphrase).toString();
        m_servers.append(server);
    }

    m_activeServer = QSettings().value(kActiveServerSetting, 0).toInt();
    if (m_activeServer < 0 || m_activeServer >= m_servers.size())
        m_activeServer = 0;
}

void MainWindow::saveServers()
{
    if (m_activeServer < 0 || m_activeServer >= m_servers.size())
        m_activeServer = qBound(0, m_activeServer, qMax(0, m_servers.size() - 1));

    QJsonArray array;
    for (const ServerConfig &server : std::as_const(m_servers)) {
        array.append(QJsonObject{{"url", server.url},
                                 {"room", server.room},
                                 {"passphrase", server.passphrase}});
    }
    QSettings settings;
    settings.setValue(kServersArray,
                      QString::fromUtf8(QJsonDocument(array).toJson(QJsonDocument::Compact)));
    settings.setValue(kActiveServerSetting, m_activeServer);

    // Mirror the active server into the legacy keys the rest of the app reads.
    if (!m_servers.isEmpty()) {
        const ServerConfig &active = m_servers.at(m_activeServer);
        settings.setValue(kServerUrlSetting, active.url);
        settings.setValue(kRoomNameSetting, active.room);
        settings.setValue(kPassphraseSetting, active.passphrase);
    }
}

void MainWindow::loadActiveServerIntoEdits()
{
    if (m_activeServer < 0 || m_activeServer >= m_servers.size())
        return;
    const ServerConfig &active = m_servers.at(m_activeServer);
    if (m_serverUrlEdit)
        m_serverUrlEdit->setText(active.url);
    if (m_roomNameEdit)
        m_roomNameEdit->setText(active.room);
    if (m_passphraseEdit)
        m_passphraseEdit->setText(active.passphrase);
}

void MainWindow::persistEditsToActiveServer()
{
    if (m_activeServer < 0 || m_activeServer >= m_servers.size())
        return;
    ServerConfig &active = m_servers[m_activeServer];
    active.url = m_serverUrlEdit->text().trimmed();
    active.room = m_roomNameEdit->text().trimmed();
    active.passphrase = m_passphraseEdit->text();
    saveServers();
}

QPixmap MainWindow::faviconFor(const ServerConfig &server) const
{
    const QString host = serverHost(server.url);
    if (m_faviconCache.contains(host))
        return roundedRectPixmap(m_faviconCache.value(host), 36, 9);
    return letterFavicon(host); // already drawn as a rounded rect
}

void MainWindow::switchToServer(int index)
{
    if (index < 0 || index >= m_servers.size())
        return;
    const bool live = m_backend != nullptr;
    if (index == m_activeServer && live) {
        showSection(0); // already connected here: just jump to its Home
        return;
    }

    if (live)
        persistEditsToActiveServer(); // capture any edits to the current server
    m_activeServer = index;
    saveServers();
    loadActiveServerIntoEdits();
    updateBreadcrumb();
    startSession(); // tears down the old backend and connects to the new server
}

void MainWindow::promptAddServer()
{
    QDialog dialog(this);
    dialog.setWindowTitle("Add mainnode server");
    auto *urlEdit = new QLineEdit(&dialog);
    urlEdit->setPlaceholderText(kDefaultServerUrl);
    auto *roomEdit = new QLineEdit(kDefaultRoomName, &dialog);
    auto *passEdit = new QLineEdit(kDefaultPassphrase, &dialog);

    auto *form = new QFormLayout;
    form->addRow("Server URL", urlEdit);
    form->addRow("Room", roomEdit);
    form->addRow("Passphrase", passEdit);
    auto *buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    auto *dialogLayout = new QVBoxLayout(&dialog);
    dialogLayout->addLayout(form);
    dialogLayout->addWidget(buttons);
    dialog.resize(460, 200);
    if (dialog.exec() != QDialog::Accepted)
        return;

    ServerConfig server;
    server.url = urlEdit->text().trimmed();
    if (server.url.isEmpty())
        server.url = kDefaultServerUrl;
    server.room = roomEdit->text().trimmed().isEmpty() ? kDefaultRoomName
                                                       : roomEdit->text().trimmed();
    server.passphrase =
        passEdit->text().isEmpty() ? kDefaultPassphrase : passEdit->text();
    m_servers.append(server);
    const int newIndex = m_servers.size() - 1;
    saveServers();
    updateBreadcrumb();
    fetchFavicon(newIndex);
    switchToServer(newIndex);
}

void MainWindow::removeServer(int index)
{
    if (index < 0 || index >= m_servers.size() || m_servers.size() <= 1)
        return;
    if (QMessageBox::question(
            this, "Remove server",
            QStringLiteral("Stop tracking %1?").arg(serverHost(m_servers.at(index).url))) !=
        QMessageBox::Yes)
        return;

    const bool removingActive = (index == m_activeServer);
    m_servers.removeAt(index);
    if (m_activeServer > index)
        --m_activeServer;
    if (m_activeServer >= m_servers.size())
        m_activeServer = m_servers.size() - 1;
    saveServers();
    loadActiveServerIntoEdits();
    updateBreadcrumb();
    if (removingActive)
        startSession(); // reconnect to whichever server is now active
}

// ------------------------------------------------------------------ favicons

void MainWindow::loadCachedFavicons()
{
    for (const ServerConfig &server : std::as_const(m_servers)) {
        const QString host = serverHost(server.url);
        const QString path = faviconCachePath(host);
        QPixmap pix;
        if (QFileInfo::exists(path) && pix.load(path) && !pix.isNull())
            m_faviconCache.insert(host, pix);
    }
}

void MainWindow::fetchFavicon(int index)
{
    if (index < 0 || index >= m_servers.size())
        return;
    const QString host = serverHost(m_servers.at(index).url);
    if (host.isEmpty() || m_faviconCache.contains(host))
        return;
    const QUrl url = faviconUrl(m_servers.at(index).url);
    if (!url.isValid())
        return;

    QNetworkReply *reply = m_networkAccess->get(QNetworkRequest(url));
    connect(reply, &QNetworkReply::finished, this, [this, reply, host] {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError)
            return;
        QPixmap pix;
        if (!pix.loadFromData(reply->readAll()) || pix.isNull())
            return;
        if (pix.width() > 64)
            pix = pix.scaled(64, 64, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        m_faviconCache.insert(host, pix);
        QDir().mkpath(faviconCacheDir());
        pix.save(faviconCachePath(host), "PNG");
        updateBreadcrumb();
    });
}

// ----------------------------------------------------------------- chat page

QWidget *MainWindow::buildChatPage()
{
    auto *page = new QWidget;

    // One page per "place": Home holds the repos, quest board and chat all at
    // once (no nav bar — you click a server to see everything). Repo detail and
    // Settings are opened on demand (clicking a repo / the server-rail gear).
    m_sectionStack = new QStackedWidget;
    // Home now hosts the nodes column, repositories column and the repo detail
    // panel (with Chat as a tab) all at once, so there is no separate repo-detail
    // section any more.
    m_sectionStack->addWidget(buildHomeSection());       // 0 Home (nodes + repos + detail)
    m_sectionStack->addWidget(buildSettingsSection());   // 1 Settings

    // No left rails any more: relays and nodes are top-bar dropdowns, so the
    // section fills the whole width.
    auto *content = new QWidget;
    auto *contentLayout = new QHBoxLayout(content);
    contentLayout->setContentsMargins(0, 0, 0, 0);
    contentLayout->setSpacing(0);
    contentLayout->addWidget(m_sectionStack, 1);

    // Global donation nudge: shown across the whole app until this node sets a
    // Solana address, so the network stays open to donations.
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(buildBreadcrumb());
    layout->addWidget(buildSolanaNotice());
    layout->addWidget(content, 1);
    layout->addWidget(buildNetworkLogDock());
    return page;
}

QWidget *MainWindow::buildNetworkLogDock()
{
    auto *dock = new QWidget;
    dock->setObjectName("logDock");

    m_issueQuickAdd = new QLineEdit;
    m_issueQuickAdd->setObjectName("issueQuickAdd");
    m_issueQuickAdd->setPlaceholderText("+ Quick issue title\xE2\x80\xA6 (Enter)");
    m_issueQuickAdd->setMaxLength(160);

    m_quickAddAssignAgent = new QCheckBox("Assign agent");
    m_quickAddAssignAgent->setToolTip(
        "When you add the issue, immediately assign a coding agent to it.");
    m_quickAddAgentProvider = new QComboBox;
    m_quickAddAgentProvider->addItem(QStringLiteral("Codex"), QStringLiteral("codex"));
    m_quickAddAgentProvider->addItem(QStringLiteral("Claude Code"),
                                     QStringLiteral("claude"));
    m_quickAddAgentProvider->setToolTip("Agent provider for quick-add assignment");
    m_quickAddCreatePr = new QCheckBox("Create PR");
    m_quickAddCreatePr->setToolTip(
        "When quick-add assigns an agent, create a pull request from its patch.");
    m_quickAddAssignAgent->setChecked(true);
    m_quickAddCreatePr->setChecked(true);
    m_quickAddCreatePr->setEnabled(true);
    m_quickAddAgentProvider->setEnabled(true);
    connect(m_quickAddAssignAgent, &QCheckBox::toggled, m_quickAddCreatePr,
            &QCheckBox::setEnabled);
    connect(m_quickAddAssignAgent, &QCheckBox::toggled, m_quickAddAgentProvider,
            &QComboBox::setEnabled);

    auto *quickAddSendButton = new QPushButton("Send");
    quickAddSendButton->setObjectName("primaryButton");
    quickAddSendButton->setProperty("buttonSize", "sm");
    quickAddSendButton->setCursor(Qt::PointingHandCursor);

    auto *quickAddRow = new QHBoxLayout;
    quickAddRow->setContentsMargins(12, 6, 8, 6);
    quickAddRow->setSpacing(8);
    quickAddRow->addWidget(m_issueQuickAdd, 1);
    quickAddRow->addWidget(quickAddSendButton);
    quickAddRow->addWidget(m_quickAddAssignAgent);
    quickAddRow->addWidget(m_quickAddAgentProvider);
    quickAddRow->addWidget(m_quickAddCreatePr);

    auto *label = new QLabel("NETWORK LOG");
    label->setObjectName("sectionLabel");
    auto *toggle = new QPushButton(QString());
    toggle->setObjectName("iconButton");
    toggle->setCursor(Qt::PointingHandCursor);
    toggle->setFixedWidth(26);
    toggle->setToolTip("Show/hide the network log");
    setOcticon(toggle, "chevron-down", 16);

    m_settingsLog = new QPlainTextEdit;
    m_settingsLog->setReadOnly(true);
    m_settingsLog->setObjectName("networkLog");
    m_settingsLog->setMaximumBlockCount(kNetworkLogLimit);
    m_settingsLog->setFixedHeight(96);
    for (const QString &line : std::as_const(m_networkLog))
        m_settingsLog->appendPlainText(line);

    auto *headerRow = new QHBoxLayout;
    headerRow->setContentsMargins(12, 3, 8, 3);
    headerRow->addWidget(label);
    headerRow->addStretch();
    headerRow->addWidget(toggle);

    auto *layout = new QVBoxLayout(dock);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addLayout(quickAddRow);
    layout->addLayout(headerRow);
    layout->addWidget(m_settingsLog);

    connect(m_issueQuickAdd, &QLineEdit::returnPressed, this,
            &MainWindow::quickAddIssue);
    connect(quickAddSendButton, &QPushButton::clicked, this,
            &MainWindow::quickAddIssue);
    connect(toggle, &QPushButton::clicked, this, [this, toggle] {
        const bool show = !m_settingsLog->isVisible();
        m_settingsLog->setVisible(show);
        setOcticon(toggle, show ? "chevron-down" : "chevron-right", 16);
    });
    return dock;
}

QWidget *MainWindow::buildBreadcrumb()
{
    auto *bar = new QWidget;
    bar->setObjectName("breadcrumbBar");

    // --- Relay switcher: bigger favicon (shows that relay's nodes when
    // clicked), a "domain ▾ count" dropdown (search / switch / add), and an
    // open-in-browser icon. ---------------------------------------------------
    m_relayIconButton = new QPushButton;
    m_relayIconButton->setObjectName("relayIconButton");
    m_relayIconButton->setCursor(Qt::PointingHandCursor);
    m_relayIconButton->setFixedSize(38, 38);
    m_relayIconButton->setIconSize(QSize(30, 30));
    m_relayIconButton->setToolTip("Show this relay's nodes");
    connect(m_relayIconButton, &QPushButton::clicked, this,
            [this] { showSection(0); });

    m_relayMenuButton = new QPushButton;
    m_relayMenuButton->setObjectName("relayMenuButton");
    m_relayMenuButton->setCursor(Qt::PointingHandCursor);
    m_relayMenuButton->setToolTip("Switch, search, or add relays");
    connect(m_relayMenuButton, &QPushButton::clicked, this,
            &MainWindow::showRelayMenu);

    m_relayOpenButton = new QPushButton;
    m_relayOpenButton->setObjectName("relayOpenButton");
    m_relayOpenButton->setCursor(Qt::PointingHandCursor);
    m_relayOpenButton->setFixedSize(30, 30);
    setOcticon(m_relayOpenButton, "link", 16);
    m_relayOpenButton->setToolTip("Open this relay in your browser");
    connect(m_relayOpenButton, &QPushButton::clicked, this,
            [this] { openServerWebsite(m_activeServer); });

    // Node switcher, to the right of the relay switcher: "node ▾ count".
    m_nodeMenuButton = new QPushButton;
    m_nodeMenuButton->setObjectName("nodeMenuButton");
    m_nodeMenuButton->setCursor(Qt::PointingHandCursor);
    m_nodeMenuButton->setToolTip("Pick a node to view its repositories");
    connect(m_nodeMenuButton, &QPushButton::clicked, this, &MainWindow::showNodeMenu);

    m_navSolanaBalance = new QLabel(QStringLiteral("SOL --"));
    m_navSolanaBalance->setObjectName("navSolanaBalance");
    m_navSolanaBalance->setToolTip("This node's Solana wallet balance");

    // Repo switcher, to the right of the node switcher: "repo ▾ count".
    m_repoMenuButton = new QPushButton;
    m_repoMenuButton->setObjectName("repoMenuButton");
    m_repoMenuButton->setCursor(Qt::PointingHandCursor);
    m_repoMenuButton->setToolTip("Open a repository, or add a local repo to mirror");
    connect(m_repoMenuButton, &QPushButton::clicked, this, &MainWindow::showRepoMenu);

    // Small "View" button beside the repo dropdown: jump straight to the open
    // repository's Code view (or open the picker when none is selected).
    m_repoViewButton = new QPushButton(QStringLiteral("View"));
    m_repoViewButton->setObjectName("ghostButton");
    m_repoViewButton->setCursor(Qt::PointingHandCursor);
    m_repoViewButton->setToolTip(QStringLiteral("View the current repository"));
    setOcticon(m_repoViewButton, "code", 14);
    connect(m_repoViewButton, &QPushButton::clicked, this, [this] {
        if (m_repoDetailIndex < 0 || m_repoDetailIndex >= m_repositories.size()) {
            showRepoMenu();
            return;
        }
        showSection(0);
        if (m_repoCodeTab)
            m_repoCodeTab->setChecked(true);
        if (m_repoDetailStack)
            m_repoDetailStack->setCurrentIndex(0); // Code
        showRepoOverview();
    });

    m_repoPushButton = new QPushButton;
    m_repoPushButton->setObjectName("primaryButton");
    m_repoPushButton->setCursor(Qt::PointingHandCursor);
    m_repoPushButton->hide();
    setOcticon(m_repoPushButton, "upload", 16);
    connect(m_repoPushButton, &QPushButton::clicked, this,
            &MainWindow::pushCurrentRepoUpstream);

    m_breadcrumb = new QLabel;
    m_breadcrumb->setObjectName("breadcrumb");
    m_breadcrumb->setTextFormat(Qt::RichText);
    m_breadcrumb->setTextInteractionFlags(Qt::TextBrowserInteraction);
    connect(m_breadcrumb, &QLabel::linkActivated, this, [this](const QString &href) {
        if (href == "repos") {
            showSection(0);
        } else if (href == "server") {
            openServerWebsite(m_activeServer);
        }
    });
    // Live connection indicator, pinned to the top-right of the window.
    m_connectionStatus = new QLabel;
    m_connectionStatus->setObjectName("connectionStatus");
    m_connectionStatus->setTextFormat(Qt::RichText);

    m_notificationButton = new QPushButton(QStringLiteral("Notifications"));
    m_notificationButton->setObjectName("notificationButton");
    m_notificationButton->setCursor(Qt::PointingHandCursor);
    setOcticon(m_notificationButton, "bell", 16);
    m_notificationButton->setToolTip("Notifications");
    connect(m_notificationButton, &QPushButton::clicked, this,
            &MainWindow::showNotifications);

    // Compact, centered success/failure toast. It lives in the middle of the
    // top bar (between the breadcrumb and the notifications bell) and is flanked
    // by stretches so it stays centered regardless of breadcrumb width.
    m_topMessage = new QLabel;
    m_topMessage->setObjectName("topMessage");
    m_topMessage->setTextFormat(Qt::RichText);
    m_topMessage->setAlignment(Qt::AlignCenter);
    m_topMessage->hide();

    // User avatar, pinned to the top-right-most of the bar. Clicking it opens a
    // dropdown with account-level actions.
    m_avatarNavButton = new QPushButton;
    m_avatarNavButton->setObjectName("serverFooterButton");
    m_avatarNavButton->setCursor(Qt::PointingHandCursor);
    m_avatarNavButton->setFixedSize(40, 40);
    m_avatarNavButton->setIconSize(QSize(34, 34));
    m_avatarNavButton->setToolTip("You");
    connect(m_avatarNavButton, &QPushButton::clicked, this, [this] {
        QMenu menu(this);
        menu.addAction(QStringLiteral("Settings"), this, [this] { showSection(1); });
        menu.addAction(QStringLiteral("Rebuild & Restart"), this,
                       [this] { quickRebuildRestart(); });
        menu.addAction(QStringLiteral("Update, rebuild & restart"), this,
                       [this] { updateRebuildRestart(); });
        menu.addSeparator();
        menu.addAction(QStringLiteral("Logout"), this, [this] { leaveSession(); });
        // Drop down from the avatar, right-aligned to its right edge.
        const QPoint corner = m_avatarNavButton->mapToGlobal(
            QPoint(m_avatarNavButton->width(), m_avatarNavButton->height()));
        menu.exec(corner - QPoint(menu.sizeHint().width(), 0));
    });
    updateAvatarButton();

    // Captions for the three top-bar dropdowns.
    auto makeCaption = [](const QString &t) {
        auto *l = new QLabel(t);
        l->setObjectName("navCaption");
        return l;
    };
    m_relayLabel = makeCaption(QStringLiteral("Relay"));
    m_nodeLabel = makeCaption(QStringLiteral("Node"));
    m_repoLabel = makeCaption(QStringLiteral("Repo"));

    // Chat toggle, next to the notification bell, with an unread indicator.
    m_chatButton = new QPushButton(QStringLiteral("Chat"));
    m_chatButton->setObjectName("notificationButton");
    m_chatButton->setCursor(Qt::PointingHandCursor);
    m_chatButton->setToolTip(QStringLiteral("Chat"));
    connect(m_chatButton, &QPushButton::clicked, this, &MainWindow::showChatView);

    auto *layout = new QHBoxLayout(bar);
    layout->setContentsMargins(16, 12, 16, 12);
    layout->setSpacing(8);
    layout->addWidget(m_relayLabel);
    layout->addWidget(m_relayIconButton);
    layout->addWidget(m_relayMenuButton);
    layout->addWidget(m_relayOpenButton);
    layout->addSpacing(10);
    layout->addWidget(m_nodeLabel);
    layout->addWidget(m_nodeMenuButton);
    layout->addSpacing(10);
    layout->addWidget(m_repoLabel);
    layout->addWidget(m_repoMenuButton);
    layout->addWidget(m_repoViewButton);
    layout->addWidget(m_repoPushButton);
    layout->addSpacing(6);
    layout->addWidget(m_breadcrumb);
    layout->addStretch();
    layout->addWidget(m_topMessage);
    layout->addStretch();
    layout->addWidget(m_chatButton);
    layout->addWidget(m_notificationButton);
    layout->addWidget(m_connectionStatus);
    layout->addWidget(m_navSolanaBalance);
    layout->addWidget(m_avatarNavButton);
    updateBreadcrumb();
    updateConnectionStatus();
    updateNotificationButton();
    updateChatButton();
    updateNavSolanaBalance();
    updateRepoPushButton();
    return bar;
}

void MainWindow::updateConnectionStatus()
{
    if (!m_connectionStatus)
        return;

    // Connected when our own node shows a live link in the roster; the online
    // count includes every node currently online (ourselves included).
    bool selfOnline = false;
    int onlineCount = 0;
    for (const MemberInfo &member : std::as_const(m_homeRoster)) {
        if (member.online)
            ++onlineCount;
        if (member.self && member.online)
            selfOnline = true;
    }
    const bool connected = m_backend && selfOnline;

    QString color, dot, text;
    if (connected) {
        color = "#3fb950"; // green
        dot = QString::fromUtf8("\xE2\x97\x8F");
        text = QString::fromUtf8("Connected \xC2\xB7 %1 %2 online")
                   .arg(onlineCount)
                   .arg(onlineCount == 1 ? "node" : "nodes");
    } else if (m_backend) {
        color = "#d29922"; // amber: connecting / backing off
        dot = QString::fromUtf8("\xE2\x97\x8F");
        text = QString::fromUtf8("Connecting\xE2\x80\xA6");
    } else {
        color = "#8b949e"; // grey: offline / not started
        dot = QString::fromUtf8("\xE2\x97\x8B");
        text = QStringLiteral("Offline");
    }
    m_connectionStatus->setText(
        QStringLiteral("<span style='color:%1'>%2</span> %3")
            .arg(color, dot, text.toHtmlEscaped()));
}

void MainWindow::updateBreadcrumb()
{
    // The active relay (favicon + domain) now lives in the relay switcher.
    updateRelaySwitcher();
    updateRepoPushButton();
    if (!m_breadcrumb)
        return;
    const int section = m_sectionStack ? m_sectionStack->currentIndex() : 0;
    // The relay / node / repo switchers already show the active location, so the
    // old "Home > node/repo" trail was redundant. Only label non-Home sections.
    if (section == 1) {
        m_breadcrumb->setText(QStringLiteral("Settings"));
        m_breadcrumb->show();
    } else {
        m_breadcrumb->clear();
        m_breadcrumb->hide();
    }
}

void MainWindow::updateRelaySwitcher()
{
    if (!m_relayMenuButton)
        return;
    QString host;
    if (m_activeServer >= 0 && m_activeServer < m_servers.size())
        host = serverHost(m_servers.at(m_activeServer).url);

    if (m_relayIconButton) {
        m_relayIconButton->setIcon(
            (m_activeServer >= 0 && m_activeServer < m_servers.size())
                ? QIcon(faviconFor(m_servers.at(m_activeServer)))
                : QIcon(letterFavicon(host.isEmpty() ? QStringLiteral("ForkMesh")
                                                     : host)));
    }
    if (m_relayOpenButton)
        m_relayOpenButton->setEnabled(!host.isEmpty());

    if (host.isEmpty())
        host = QStringLiteral("ForkMesh");
    // "domain ▾ count": the caret signals it drops down; the count is the
    // number of configured relays.
    const QString caret = QString::fromUtf8("\xE2\x96\xBE");
    m_relayMenuButton->setText(host + "  " + caret + "  " +
                               QString::number(m_servers.size()));
}

void MainWindow::openServerWebsite(int index)
{
    if (index < 0 || index >= m_servers.size())
        return;
    // Open the relay's website in the system browser (ws/wss -> http/https).
    QUrl url(m_servers.at(index).url);
    if (url.scheme() == "ws")
        url.setScheme(QStringLiteral("http"));
    else if (url.scheme() == "wss")
        url.setScheme(QStringLiteral("https"));
    url.setPath(QStringLiteral("/"));
    url.setQuery(QString());
    url.setFragment(QString());
    if (url.isValid() && !url.host().isEmpty())
        QDesktopServices::openUrl(url);
}

void MainWindow::openRepositoryWebsite()
{
    if (m_repoDetailIndex < 0 || m_repoDetailIndex >= m_repositories.size())
        return;
    const RepositoryRecord &repo = m_repositories.at(m_repoDetailIndex);
    const QUrl url(repositoryWebUrl(repo));
    if (url.isValid() && !url.host().isEmpty())
        QDesktopServices::openUrl(url);
}

void MainWindow::showRelayMenu()
{
    if (!m_relayMenuButton)
        return;
    QMenu menu(this);

    // Header showing the relay count.
    QAction *header =
        menu.addAction(QStringLiteral("Relays (%1)").arg(m_servers.size()));
    header->setEnabled(false);

    // Search box at the top; filters the relay list live.
    auto *searchEdit = new QLineEdit(&menu);
    searchEdit->setPlaceholderText(QStringLiteral("Search relays") +
                                   QString::fromUtf8("\xE2\x80\xA6"));
    searchEdit->setClearButtonEnabled(true);
    searchEdit->setMinimumWidth(240);
    auto *searchAction = new QWidgetAction(&menu);
    searchAction->setDefaultWidget(searchEdit);
    menu.addAction(searchAction);
    menu.addSeparator();

    // One checkable action per relay (active one checked).
    QList<QAction *> relayActions;
    for (int i = 0; i < m_servers.size(); ++i) {
        const ServerConfig &server = m_servers.at(i);
        QAction *act =
            menu.addAction(QIcon(faviconFor(server)), serverHost(server.url));
        act->setCheckable(true);
        act->setChecked(i == m_activeServer);
        connect(act, &QAction::triggered, this, [this, i] { switchToServer(i); });
        relayActions.append(act);
    }

    menu.addSeparator();
    QAction *addAct = menu.addAction(QStringLiteral("Add relay") +
                                     QString::fromUtf8("\xE2\x80\xA6"));
    connect(addAct, &QAction::triggered, this, &MainWindow::promptAddServer);

    connect(searchEdit, &QLineEdit::textChanged, &menu,
            [this, relayActions](const QString &text) {
                const QString needle = text.trimmed().toLower();
                for (int i = 0;
                     i < relayActions.size() && i < m_servers.size(); ++i)
                    relayActions.at(i)->setVisible(
                        needle.isEmpty() ||
                        serverHost(m_servers.at(i).url).toLower().contains(needle));
            });
    // Focus the search box once the menu's event loop is running.
    QTimer::singleShot(0, searchEdit, [searchEdit] { searchEdit->setFocus(); });

    menu.exec(m_relayMenuButton->mapToGlobal(
        QPoint(0, m_relayMenuButton->height())));
}

// Public Solana JSON-RPC endpoints. Tried in order with fallback so the UI can
// still show a balance if one public endpoint is unavailable.
namespace {
const char *kSolanaRpcEndpoints[] = {
    "https://api.mainnet-beta.solana.com",
    "https://solana-rpc.publicnode.com",
};

bool isLikelySolanaAddress(const QString &address)
{
    static const QRegularExpression re(
        QStringLiteral("^[1-9A-HJ-NP-Za-km-z]{32,44}$"));
    return re.match(address.trimmed()).hasMatch();
}

QString formatSolanaBalance(qint64 lamports)
{
    return QStringLiteral("%1 SOL").arg(lamports / 1000000000.0, 0, 'f', 9);
}

QString lastSolanaBalanceSetting(const QString &address)
{
    return kSolanaLastBalanceSettingPrefix + address.trimmed();
}
}  // namespace

void MainWindow::updateNodeSwitcher()
{
    if (!m_nodeMenuButton)
        return;
    const QString caret = QString::fromUtf8("\xE2\x96\xBE");
    const QString label =
        m_selectedNode.isEmpty() ? QStringLiteral("Nodes") : m_selectedNode;
    m_nodeMenuButton->setText(label + "  " + caret + "  " +
                              QString::number(m_nodeMenuEntries.size()));
    // Badge the button with the selected node's platform/online state.
    for (const NodeMenuEntry &e : std::as_const(m_nodeMenuEntries)) {
        if (e.name == m_selectedNode) {
            m_nodeMenuButton->setIcon(osBadgeIcon(e.platform, e.online, 16));
            return;
        }
    }
    m_nodeMenuButton->setIcon(QIcon());
}

void MainWindow::updateNavSolanaBalance()
{
    if (!m_navSolanaBalance)
        return;

    const QString addr = savedSolanaAddress();
    m_navSolanaBalanceAddress = addr;
    if (addr.isEmpty()) {
        m_navSolanaBalance->setText(QStringLiteral("SOL --"));
        m_navSolanaBalance->setToolTip("Add a Solana address to show this node's balance");
        return;
    }
    if (!isLikelySolanaAddress(addr)) {
        m_navSolanaBalance->setText(QStringLiteral("SOL invalid"));
        m_navSolanaBalance->setToolTip("Saved Solana address is invalid");
        return;
    }

    m_navSolanaBalance->setText(QStringLiteral("SOL ..."));
    m_navSolanaBalance->setToolTip(QStringLiteral("Checking this node's Solana balance"));
    queryNavSolanaBalance(addr, 0);
}

void MainWindow::queryNavSolanaBalance(const QString &addr, int endpointIndex)
{
    const int count = int(sizeof(kSolanaRpcEndpoints) / sizeof(kSolanaRpcEndpoints[0]));
    if (endpointIndex >= count) {
        if (m_navSolanaBalance && m_navSolanaBalanceAddress == addr) {
            m_navSolanaBalance->setText(QStringLiteral("SOL unavailable"));
            m_navSolanaBalance->setToolTip("Solana balance is temporarily unavailable");
        }
        return;
    }

    QNetworkRequest request(QUrl(QString::fromLatin1(kSolanaRpcEndpoints[endpointIndex])));
    request.setHeader(QNetworkRequest::ContentTypeHeader,
                      QStringLiteral("application/json"));
    const QJsonObject body{
        {"jsonrpc", "2.0"},
        {"id", 1},
        {"method", "getBalance"},
        {"params", QJsonArray{addr}},
    };
    QNetworkReply *reply = m_networkAccess->post(
        request, QJsonDocument(body).toJson(QJsonDocument::Compact));
    connect(reply, &QNetworkReply::finished, this, [this, reply, addr, endpointIndex]() {
        const QByteArray raw = reply->readAll();
        const QNetworkReply::NetworkError netError = reply->error();
        reply->deleteLater();
        if (!m_navSolanaBalance || m_navSolanaBalanceAddress != addr)
            return;

        const QJsonObject root = QJsonDocument::fromJson(raw).object();
        const QJsonObject result = root.value("result").toObject();
        if (netError != QNetworkReply::NoError || !result.contains("value")) {
            queryNavSolanaBalance(addr, endpointIndex + 1);
            return;
        }
        const qint64 lamports = result.value("value").toVariant().toLongLong();
        QSettings settings;
        const QString lastBalanceKey = lastSolanaBalanceSetting(addr);
        const QVariant previousValue = settings.value(lastBalanceKey);
        const qint64 previousLamports = previousValue.toLongLong();
        const QString balance = formatSolanaBalance(lamports);
        m_navSolanaBalance->setText(balance);
        m_navSolanaBalance->setToolTip(
            QStringLiteral("This node's Solana balance: %1").arg(balance));
        if (previousValue.isValid() && lamports > previousLamports &&
            QSettings().value(kDisbursementAlertSetting, true).toBool()) {
            const QString amount = formatSolanaBalance(lamports - previousLamports);
            QApplication::alert(this, 0);
            postNotification(QStringLiteral("New disbursement received"),
                             QStringLiteral("%1 added to this node's wallet. "
                                            "New balance: %2")
                                 .arg(amount, balance),
                             false, QStringLiteral("emblem-default"));
        }
        settings.setValue(lastBalanceKey, QString::number(lamports));
    });
}

void MainWindow::showNodeMenu()
{
    if (!m_nodeMenuButton)
        return;
    QMenu menu(this);

    QAction *header =
        menu.addAction(QStringLiteral("Nodes (%1)").arg(m_nodeMenuEntries.size()));
    header->setEnabled(false);

    auto *searchEdit = new QLineEdit(&menu);
    searchEdit->setPlaceholderText(QStringLiteral("Search nodes") +
                                   QString::fromUtf8("\xE2\x80\xA6"));
    searchEdit->setClearButtonEnabled(true);
    searchEdit->setMinimumWidth(240);
    auto *searchAction = new QWidgetAction(&menu);
    searchAction->setDefaultWidget(searchEdit);
    menu.addAction(searchAction);
    menu.addSeparator();

    if (m_nodeMenuEntries.isEmpty()) {
        QAction *empty = menu.addAction(QStringLiteral("No nodes yet"));
        empty->setEnabled(false);
    }

    QList<QAction *> nodeActions;
    QStringList nodeNames;
    for (const NodeMenuEntry &e : std::as_const(m_nodeMenuEntries)) {
        QString text = e.name;
        if (e.self)
            text += " (you)";
        text += QStringLiteral("   %1 repo%2")
                    .arg(e.repoCount)
                    .arg(e.repoCount == 1 ? "" : "s");
        QAction *act = menu.addAction(osBadgeIcon(e.platform, e.online, 16), text);
        act->setCheckable(true);
        act->setChecked(e.name == m_selectedNode);
        const QString node = e.name;
        connect(act, &QAction::triggered, this, [this, node] {
            selectNode(node);                 // fill the repositories column
            showNodeProfile(QString(), node); // and open the node's profile
        });
        nodeActions.append(act);
        nodeNames.append(e.name.toLower());
    }

    connect(searchEdit, &QLineEdit::textChanged, &menu,
            [nodeActions, nodeNames](const QString &text) {
                const QString needle = text.trimmed().toLower();
                for (int i = 0; i < nodeActions.size(); ++i)
                    nodeActions.at(i)->setVisible(needle.isEmpty() ||
                                                  nodeNames.at(i).contains(needle));
            });
    QTimer::singleShot(0, searchEdit, [searchEdit] { searchEdit->setFocus(); });

    menu.exec(m_nodeMenuButton->mapToGlobal(
        QPoint(0, m_nodeMenuButton->height())));
}

void MainWindow::updateRepoSwitcher()
{
    if (!m_repoMenuButton)
        return;
    // Nothing in the repo area when the selected node has no repos.
    const bool hasRepos = !m_repoMenuEntries.isEmpty();
    m_repoMenuButton->setVisible(hasRepos);
    if (m_repoViewButton)
        m_repoViewButton->setVisible(hasRepos);
    if (m_repoLabel)
        m_repoLabel->setVisible(hasRepos);
    if (!hasRepos)
        return;
    const QString caret = QString::fromUtf8("\xE2\x96\xBE");
    QString label = QStringLiteral("Repos");
    if (m_repoDetailIndex >= 0 && m_repoDetailIndex < m_repositories.size())
        label = m_repositories.at(m_repoDetailIndex).name;
    m_repoMenuButton->setText(label + "  " + caret + "  " +
                              QString::number(m_repoMenuEntries.size()));
}

void MainWindow::updateRepoPushButton()
{
    if (!m_repoPushButton)
        return;
    m_repoPushButton->hide();
    m_repoPushButton->setEnabled(false);

    if (m_repoDetailIndex < 0 || m_repoDetailIndex >= m_repositories.size())
        return;
    const RepositoryRecord &repo = m_repositories.at(m_repoDetailIndex);
    if (repo.localPath.isEmpty() || !QDir(repo.localPath).exists(".git"))
        return;

    if (m_pushingRepos.contains(m_repoDetailIndex)) {
        m_repoPushButton->setText(QStringLiteral("Pushing..."));
        m_repoPushButton->setToolTip(QStringLiteral("Pushing local commits upstream"));
        m_repoPushButton->show();
        return;
    }

    QByteArray upstreamOut;
    if (!runGitCapture(repo.localPath,
                       {QStringLiteral("rev-parse"), QStringLiteral("--abbrev-ref"),
                        QStringLiteral("--symbolic-full-name"),
                        QStringLiteral("@{upstream}")},
                       &upstreamOut, nullptr))
        return;
    const QString upstream = QString::fromUtf8(upstreamOut).trimmed();
    if (upstream.isEmpty())
        return;

    QByteArray countOut;
    if (!runGitCapture(repo.localPath,
                       {QStringLiteral("rev-list"), QStringLiteral("--count"),
                        QStringLiteral("@{upstream}..HEAD")},
                       &countOut, nullptr))
        return;
    const int ahead = QString::fromUtf8(countOut).trimmed().toInt();
    if (ahead <= 0)
        return;

    m_repoPushButton->setText(
        QStringLiteral("Push %1 commit%2 to %3")
            .arg(ahead)
            .arg(ahead == 1 ? QString() : QStringLiteral("s"),
                 upstream));
    m_repoPushButton->setToolTip(
        QStringLiteral("Push local commits from %1/%2 to %3")
            .arg(repo.owner, repo.name, upstream));
    m_repoPushButton->setEnabled(true);
    m_repoPushButton->show();
}

void MainWindow::pushCurrentRepoUpstream()
{
    if (m_repoDetailIndex < 0 || m_repoDetailIndex >= m_repositories.size() ||
        m_pushingRepos.contains(m_repoDetailIndex))
        return;

    const int index = m_repoDetailIndex;
    const RepositoryRecord repo = m_repositories.at(index);
    if (repo.localPath.isEmpty() || !QDir(repo.localPath).exists(".git"))
        return;

    QByteArray upstreamOut;
    if (!runGitCapture(repo.localPath,
                       {QStringLiteral("rev-parse"), QStringLiteral("--abbrev-ref"),
                        QStringLiteral("--symbolic-full-name"),
                        QStringLiteral("@{upstream}")},
                       &upstreamOut, nullptr)) {
        flashMessage(QStringLiteral("No upstream branch is configured for %1/%2.")
                         .arg(repo.owner, repo.name),
                     true);
        updateRepoPushButton();
        return;
    }
    const QString upstream = QString::fromUtf8(upstreamOut).trimmed();

    QByteArray countOut;
    runGitCapture(repo.localPath,
                  {QStringLiteral("rev-list"), QStringLiteral("--count"),
                   QStringLiteral("@{upstream}..HEAD")},
                  &countOut, nullptr);
    const int ahead = QString::fromUtf8(countOut).trimmed().toInt();

    m_pushingRepos.insert(index);
    updateRepoPushButton();
    logSystem(QStringLiteral("Git: pushing %1/%2 to %3.")
                  .arg(repo.owner, repo.name, upstream));

    auto *process = new QProcess(this);
    connect(process, &QProcess::finished, this,
            [this, process, index, repo, upstream, ahead](int exitCode,
                                                          QProcess::ExitStatus status) {
                if (process->property("handled").toBool())
                    return;
                process->setProperty("handled", true);
                const QString errors =
                    QString::fromUtf8(process->readAllStandardError()).trimmed();
                process->deleteLater();
                m_pushingRepos.remove(index);

                if (status == QProcess::NormalExit && exitCode == 0) {
                    const QString count =
                        ahead > 0 ? QString::number(ahead) + QLatin1Char(' ') : QString();
                    logSystem(QStringLiteral("Git: pushed %1commit%2 from %3/%4 to %5.")
                                  .arg(count,
                                       ahead == 1 ? QString() : QStringLiteral("s"),
                                       repo.owner, repo.name, upstream));
                    flashMessage(QStringLiteral("Pushed %1/%2 to %3.")
                                     .arg(repo.owner, repo.name, upstream));
                    if (index >= 0 && index < m_repositories.size()) {
                        if (!m_repositories.at(index).mirrorPath.isEmpty())
                            syncRepository(index, /*quiet=*/true);
                        else if (index == m_repoDetailIndex)
                            refreshOpenRepoDetail();
                    }
                } else {
                    const QString detail =
                        errors.isEmpty() ? QStringLiteral("git push failed")
                                         : errors.right(300);
                    logSystem(QStringLiteral("Git: push failed for %1/%2: %3")
                                  .arg(repo.owner, repo.name, detail));
                    flashMessage(QStringLiteral("Push failed for %1/%2: %3")
                                     .arg(repo.owner, repo.name, detail.left(160)),
                                 true);
                }
                updateRepoPushButton();
            });
    connect(process, &QProcess::errorOccurred, this,
            [this, process, index, repo](QProcess::ProcessError) {
                if (process->property("handled").toBool())
                    return;
                process->setProperty("handled", true);
                process->deleteLater();
                m_pushingRepos.remove(index);
                logSystem(QStringLiteral("Git: could not start push for %1/%2.")
                              .arg(repo.owner, repo.name));
                flashMessage(QStringLiteral("Could not run git push for %1/%2.")
                                 .arg(repo.owner, repo.name),
                             true);
                updateRepoPushButton();
            });
    process->start(QStringLiteral("git"),
                   {QStringLiteral("-C"), repo.localPath, QStringLiteral("push")});
}

void MainWindow::showRepoMenu()
{
    if (!m_repoMenuButton)
        return;
    QMenu menu(this);

    QAction *header = menu.addAction(
        QStringLiteral("Repositories (%1)").arg(m_repoMenuEntries.size()));
    header->setEnabled(false);

    auto *searchEdit = new QLineEdit(&menu);
    searchEdit->setPlaceholderText(QStringLiteral("Search repositories") +
                                   QString::fromUtf8("\xE2\x80\xA6"));
    searchEdit->setClearButtonEnabled(true);
    searchEdit->setMinimumWidth(260);
    auto *searchAction = new QWidgetAction(&menu);
    searchAction->setDefaultWidget(searchEdit);
    menu.addAction(searchAction);
    menu.addSeparator();

    if (m_repoMenuEntries.isEmpty()) {
        QAction *empty = menu.addAction(QStringLiteral("No repositories yet"));
        empty->setEnabled(false);
    }

    QList<QAction *> repoActions;
    QStringList repoNames;
    for (const RepoMenuEntry &e : std::as_const(m_repoMenuEntries)) {
        QAction *act = menu.addAction(e.icon, e.label);
        const int index = e.index;
        const QString advertised = e.advertised;
        connect(act, &QAction::triggered, this, [this, index, advertised] {
            if (index >= 0 && index < m_repositories.size())
                openRepoDetail(index); // files + issues for this repo
            else if (index == -2)      // advertised mirror: temporary preview
                previewAdvertisedRepo(advertised);
            updateRepoSwitcher();
        });
        repoActions.append(act);
        repoNames.append(e.label.toLower());
    }

    menu.addSeparator();
    QAction *addAct = menu.addAction(QStringLiteral("Add local repo") +
                                     QString::fromUtf8("\xE2\x80\xA6"));
    connect(addAct, &QAction::triggered, this, &MainWindow::promptAddRepository);

    connect(searchEdit, &QLineEdit::textChanged, &menu,
            [repoActions, repoNames](const QString &text) {
                const QString needle = text.trimmed().toLower();
                for (int i = 0; i < repoActions.size(); ++i)
                    repoActions.at(i)->setVisible(needle.isEmpty() ||
                                                  repoNames.at(i).contains(needle));
            });
    QTimer::singleShot(0, searchEdit, [searchEdit] { searchEdit->setFocus(); });

    menu.exec(m_repoMenuButton->mapToGlobal(
        QPoint(0, m_repoMenuButton->height())));
}

void MainWindow::showChatView()
{
    showSection(0); // Home hosts the repo-detail stack (which holds Chat)
    // Chat has no repo tab, so clear any checked tab while it's shown.
    if (m_repoDetailTabs) {
        if (QAbstractButton *checked = m_repoDetailTabs->checkedButton()) {
            m_repoDetailTabs->setExclusive(false);
            checked->setChecked(false);
            m_repoDetailTabs->setExclusive(true);
        }
    }
    if (m_repoDetailStack && m_chatStackIndex >= 0)
        m_repoDetailStack->setCurrentIndex(m_chatStackIndex);
    updateChatButton();
}

void MainWindow::updateChatButton()
{
    if (!m_chatButton)
        return;
    const bool unread = !m_unread.isEmpty();
    // A green comment glyph marks unread chats; otherwise the themed default.
    if (unread)
        m_chatButton->setIcon(themedOcticon("comment", QColor("#2ea043"), 18));
    else
        setOcticon(m_chatButton, "comment", 18);
    m_chatButton->setToolTip(unread ? QStringLiteral("Chat \xE2\x80\x94 unread messages")
                                    : QStringLiteral("Chat"));
}

QWidget *MainWindow::buildSolanaNotice()
{
    m_solanaBanner = new QWidget;
    m_solanaBanner->setObjectName("solanaBanner");
    m_solanaBannerLabel = new QLabel(
        "Add a Solana address so others can sponsor this node — it keeps "
        "the network open to donations and more sustainable.");
    m_solanaBannerLabel->setObjectName("solanaBannerLabel");
    m_solanaBannerLabel->setWordWrap(true);

    auto *addButton = new QPushButton("Add Solana address");
    addButton->setObjectName("primaryButton");
    addButton->setCursor(Qt::PointingHandCursor);
    setOcticon(addButton, "plus", 16);
    connect(addButton, &QPushButton::clicked, this, &MainWindow::promptSetSolanaAddress);

    auto *dismissButton = new QPushButton(QString());
    dismissButton->setObjectName("ghostButton");
    dismissButton->setCursor(Qt::PointingHandCursor);
    dismissButton->setToolTip("Hide for now");
    setOcticon(dismissButton, "x", 16);
    connect(dismissButton, &QPushButton::clicked, m_solanaBanner, &QWidget::hide);

    auto *layout = new QHBoxLayout(m_solanaBanner);
    layout->setContentsMargins(16, 10, 12, 10);
    layout->setSpacing(10);
    layout->addWidget(m_solanaBannerLabel, 1);
    layout->addWidget(addButton);
    layout->addWidget(dismissButton);
    m_solanaBanner->hide();
    return m_solanaBanner;
}

void MainWindow::updateSolanaNotice()
{
    if (!m_solanaBanner)
        return;
    const bool hasAddress = !savedSolanaAddress().isEmpty();
    m_solanaBanner->setVisible(!hasAddress);
    updateNavSolanaBalance();
}

void MainWindow::promptSetSolanaAddress()
{
    bool ok = false;
    const QString current = savedSolanaAddress();
    const QString address = QInputDialog::getText(
        this, "Solana address",
        "Enter a Solana address to receive donations:", QLineEdit::Normal,
        current, &ok);
    if (!ok)
        return;
    const QString trimmed = address.trimmed();
    saveSolanaAddress(trimmed);
    if (m_solanaEdit)
        m_solanaEdit->setText(trimmed);
    if (m_settingsSolanaEdit)
        m_settingsSolanaEdit->setText(trimmed);
    // The address is shared with peers on the next connect; the sponsor button
    // and donation notice pick it up immediately.
    updateSolanaNotice();
    updateHomeStats();
}

void MainWindow::showSection(int index)
{
    if (m_navGroup && m_navGroup->button(index))
        m_navGroup->button(index)->setChecked(true);
    if (m_sectionStack)
        m_sectionStack->setCurrentIndex(index);
    updateBreadcrumb();
    if (index == 0)
        updateHomeStats();
}

QWidget *MainWindow::buildHomeSection()
{
    auto *page = new QWidget;

    // Everything on one page, left to right: a Nodes column, a Repositories
    // column (the repos for the selected node), then the repo detail panel whose
    // tabs (Code, Commits, Issues, …, Chat) are the only thing that swaps as you
    // navigate — the two columns stay visible the whole time. The node profile
    // panel slides in on the far right when a node is clicked.
    auto *splitter = new QSplitter(Qt::Horizontal);
    splitter->setObjectName("homeSplitter");
    splitter->setChildrenCollapsible(false);
    // Nodes and repositories are now top-bar dropdowns (see buildBreadcrumb);
    // the repo detail panel fills the page, with the node profile sliding in.
    splitter->addWidget(buildRepoDetailSection());
    splitter->addWidget(buildNodeProfilePanel()); // hidden until a node is clicked
    splitter->setStretchFactor(0, 1);
    splitter->setStretchFactor(1, 0);
    splitter->setSizes({220, 260, 760, 320});

    auto *layout = new QHBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(splitter);
    return page;
}

QWidget *MainWindow::buildNodeProfilePanel()
{
    m_nodeProfilePanel = new QWidget;
    m_nodeProfilePanel->setObjectName("nodeProfilePanel");
    m_nodeProfilePanel->setMinimumWidth(280);
    m_nodeProfilePanel->setMaximumWidth(380);

    auto *closeButton = new QPushButton(QString());
    closeButton->setObjectName("ghostButton");
    closeButton->setCursor(Qt::PointingHandCursor);
    closeButton->setToolTip("Close");
    setOcticon(closeButton, "x", 16);
    connect(closeButton, &QPushButton::clicked, this, &MainWindow::hideNodeProfile);
    auto *titleLabel = new QLabel("Node profile");
    titleLabel->setObjectName("sectionLabel");
    auto *topRow = new QHBoxLayout;
    topRow->setContentsMargins(0, 0, 0, 0);
    topRow->addWidget(titleLabel);
    topRow->addStretch();
    topRow->addWidget(closeButton);

    m_profileAvatar = new QLabel;
    m_profileAvatar->setFixedSize(72, 72);
    m_profileAvatar->setScaledContents(true);
    m_profileName = new QLabel;
    m_profileName->setObjectName("channelTitle");
    m_profileName->setWordWrap(true);
    m_profileStatus = new QLabel;
    m_profileStatus->setObjectName("statusLine");
    m_profileStatus->setTextFormat(Qt::RichText);
    m_profilePlatform = new QLabel;
    m_profilePlatform->setObjectName("statusLine");
    m_profileVersion = new QLabel;
    m_profileVersion->setObjectName("statusLine");
    m_profileMirrors = new QLabel;
    m_profileMirrors->setObjectName("statusLine");
    m_profileMirrors->setWordWrap(true);
    m_profileStats = new QLabel;
    m_profileStats->setObjectName("statusLine");
    m_profileStats->setWordWrap(true);
    m_profileStats->setTextInteractionFlags(Qt::TextSelectableByMouse);
    // Per-repo hosting stats relocated from the repo detail view.
    m_profileHostingLabel = new QLabel("HOSTING");
    m_profileHostingLabel->setObjectName("sectionLabel");
    m_profileHosting = new QLabel;
    m_profileHosting->setObjectName("statusLine");
    m_profileHosting->setWordWrap(true);
    m_profileHosting->setTextFormat(Qt::RichText);
    m_profileHosting->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_profileNote = new QLabel;
    m_profileNote->setObjectName("statusLine");
    m_profileNote->setWordWrap(true);

    // Node ID = the node's Ed25519 public key. Selectable so it can be copied.
    auto *nodeKeyLabel = new QLabel("NODE ID (PUBLIC KEY)");
    nodeKeyLabel->setObjectName("sectionLabel");
    m_profileNodeKey = new QLabel;
    m_profileNodeKey->setObjectName("statusLine");
    m_profileNodeKey->setWordWrap(true);
    m_profileNodeKey->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_profileNodeKey->setStyleSheet("font-family:monospace;");
    auto *copyKey = new QPushButton("Copy node ID");
    copyKey->setObjectName("ghostButton");
    copyKey->setCursor(Qt::PointingHandCursor);
    connect(copyKey, &QPushButton::clicked, this, [this] {
        if (!m_profileNodeId.isEmpty()) {
            QApplication::clipboard()->setText(m_profileNodeId);
            logSystem("Copied node ID to clipboard.");
        }
    });

    m_profileMessageButton = new QPushButton("Message");
    m_profileMessageButton->setObjectName("ghostButton");
    m_profileMessageButton->setCursor(Qt::PointingHandCursor);
    setOcticon(m_profileMessageButton, "comment", 16);
    connect(m_profileMessageButton, &QPushButton::clicked, this, [this] {
        if (!m_profileNodeId.isEmpty())
            openDirectChat(m_profileNodeId, m_profileNodeName);
    });

    // --- Solana section: address, QR, on-demand balance.
    m_profileSolanaSection = new QWidget;
    auto *solanaLabel = new QLabel("SOLANA");
    solanaLabel->setObjectName("sectionLabel");
    m_profileSolanaAddr = new QLabel;
    m_profileSolanaAddr->setObjectName("statusLine");
    m_profileSolanaAddr->setWordWrap(true);
    m_profileSolanaAddr->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_profileSolanaAddr->setStyleSheet("font-family:monospace;");
    auto *copyAddr = new QPushButton("Copy address");
    copyAddr->setObjectName("ghostButton");
    copyAddr->setCursor(Qt::PointingHandCursor);
    connect(copyAddr, &QPushButton::clicked, this, [this] {
        if (!m_profileSolanaValue.isEmpty()) {
            QApplication::clipboard()->setText(m_profileSolanaValue);
            logSystem("Copied Solana address to clipboard.");
        }
    });
    m_profileQr = new QLabel;
    m_profileQr->setAlignment(Qt::AlignCenter);

    m_profileBalance = new QLabel("\xE2\x80\x94"); // em dash until checked
    m_profileBalance->setObjectName("channelTitle");
    m_profileBalanceButton = new QPushButton("Check balance");
    m_profileBalanceButton->setObjectName("ghostButton");
    m_profileBalanceButton->setCursor(Qt::PointingHandCursor);
    m_profileBalanceButton->setToolTip(
        "Query the Solana network through public JSON-RPC for this wallet's "
        "balance. This sends the address to the endpoint it connects to.");
    connect(m_profileBalanceButton, &QPushButton::clicked, this,
            &MainWindow::checkNodeBalance);
    auto *balanceRow = new QHBoxLayout;
    balanceRow->setContentsMargins(0, 0, 0, 0);
    balanceRow->addWidget(m_profileBalance, 1);
    balanceRow->addWidget(m_profileBalanceButton);

    auto *solanaLayout = new QVBoxLayout(m_profileSolanaSection);
    solanaLayout->setContentsMargins(0, 8, 0, 0);
    solanaLayout->setSpacing(6);
    solanaLayout->addWidget(solanaLabel);
    solanaLayout->addWidget(m_profileSolanaAddr);
    solanaLayout->addWidget(copyAddr, 0, Qt::AlignLeft);
    solanaLayout->addWidget(m_profileQr, 0, Qt::AlignCenter);
    auto *balLabel = new QLabel("BALANCE");
    balLabel->setObjectName("sectionLabel");
    solanaLayout->addWidget(balLabel);
    solanaLayout->addLayout(balanceRow);

    // Revenue-sharing eligibility (self only): verify >=0.001 SOL has reached
    // this wallet so the network knows the address is active.
    m_profileEligibility = new QLabel;
    m_profileEligibility->setObjectName("statusLine");
    m_profileEligibility->setWordWrap(true);
    m_profileEligibility->setTextFormat(Qt::RichText);
    m_profileVerifyButton = new QPushButton("Verify wallet (deposit >= 0.001 SOL)");
    m_profileVerifyButton->setObjectName("ghostButton");
    m_profileVerifyButton->setCursor(Qt::PointingHandCursor);
    connect(m_profileVerifyButton, &QPushButton::clicked, this,
            &MainWindow::verifyWallet);
    solanaLayout->addWidget(m_profileEligibility);
    solanaLayout->addWidget(m_profileVerifyButton, 0, Qt::AlignLeft);

    auto *layout = new QVBoxLayout(m_nodeProfilePanel);
    layout->setContentsMargins(16, 16, 16, 16);
    layout->setSpacing(8);
    layout->addLayout(topRow);
    layout->addWidget(m_profileAvatar, 0, Qt::AlignHCenter);
    layout->addWidget(m_profileName, 0, Qt::AlignHCenter);
    layout->addWidget(m_profileStatus, 0, Qt::AlignHCenter);
    layout->addWidget(m_profileNote, 0, Qt::AlignHCenter);
    layout->addWidget(m_profilePlatform);
    layout->addWidget(m_profileVersion);
    layout->addWidget(m_profileMirrors);
    layout->addWidget(m_profileStats);
    layout->addWidget(m_profileHostingLabel);
    layout->addWidget(m_profileHosting);
    layout->addWidget(nodeKeyLabel);
    layout->addWidget(m_profileNodeKey);
    layout->addWidget(copyKey, 0, Qt::AlignLeft);
    layout->addWidget(m_profileMessageButton, 0, Qt::AlignLeft);
    layout->addWidget(m_profileSolanaSection);
    layout->addStretch();

    m_nodeProfilePanel->hide();
    return m_nodeProfilePanel;
}

void MainWindow::hideNodeProfile()
{
    if (m_nodeProfilePanel)
        m_nodeProfilePanel->hide();
    m_profileNodeId.clear();
    m_profileNodeName.clear();
    m_profileSolanaValue.clear();
}

void MainWindow::showNodeProfile(const QString &nodeId, const QString &nodeName)
{
    if (!m_nodeProfilePanel)
        return;

    // Resolve the node from the live roster (by id, then by name).
    MemberInfo info;
    bool found = false;
    for (const MemberInfo &m : std::as_const(m_homeRoster)) {
        if ((!nodeId.isEmpty() && m.id == nodeId) ||
            (nodeId.isEmpty() && m.name == nodeName)) {
            info = m;
            found = true;
            break;
        }
    }
    if (!found) {
        info.id = nodeId;
        info.name = nodeName;
    }
    // Self's Solana address may only live in local settings.
    QString solana = info.solanaAddress.trimmed();
    if (info.self && solana.isEmpty())
        solana = savedSolanaAddress();

    m_profileNodeId = info.id;
    m_profileNodeName = info.name;
    m_profileSolanaValue = solana;

    // Avatar: real avatar if we have one, else a generated letter tile.
    QPixmap avatar = m_avatars.value(info.id);
    if (avatar.isNull())
        avatar = letterFavicon(info.name);
    m_profileAvatar->setPixmap(roundedRectPixmap(avatar, 72, 18));

    m_profileName->setText(info.name.toHtmlEscaped() +
                           (info.self ? " (you)" : QString()));
    const bool online = info.self ? (m_backend != nullptr) : info.online;
    m_profileStatus->setText(
        QString::fromUtf8("<span style='color:%1'>\xE2\x97\x8F</span> %2")
            .arg(online ? "#3fb950" : "#8b949e", online ? "Online" : "Offline"));

    m_profilePlatform->setText(
        info.platform.isEmpty()
            ? QStringLiteral("Platform: unknown")
            : QStringLiteral("Platform: %1").arg(info.platform));
    m_profilePlatform->setVisible(!info.platform.isEmpty());

    m_profileVersion->setText(
        info.version.isEmpty()
            ? QStringLiteral("Version: unknown")
            : QStringLiteral("Version: v%1").arg(info.version.toHtmlEscaped()));
    m_profileVersion->setVisible(!info.version.isEmpty());

    if (info.mirrors.isEmpty()) {
        m_profileMirrors->setText("Mirrors: none advertised");
    } else {
        m_profileMirrors->setText(
            QStringLiteral("Mirrors (%1): %2")
                .arg(info.mirrors.size())
                .arg(info.mirrors.join(", ").toHtmlEscaped()));
    }

    // Detailed node stats (repos/mirrored/online/chats, uptime, key) — moved
    // here from under the node in the list. Only meaningful for your own node.
    if (m_profileStats) {
        if (info.self) {
            m_profileStats->setText(selfNodeStats());
            m_profileStats->setVisible(true);
        } else {
            m_profileStats->clear();
            m_profileStats->setVisible(false);
        }
    }

    // Per-repo hosting stats (served/clones/hosted-since/last-sync) live here
    // now, for your own node only.
    m_profileIsSelf = info.self;
    refreshProfileHostingStats();

    // Discovery note (e.g. "(discovered)"), shown only when present.
    m_profileNote->setText(info.note.toHtmlEscaped());
    m_profileNote->setVisible(!info.note.trimmed().isEmpty());

    // Node ID = Ed25519 public key. For yourself, fall back to our own key when
    // the roster entry has no id yet.
    QString nodeKey = info.id;
    if (info.self && nodeKey.isEmpty())
        nodeKey = m_profileIdentity.publicKey();
    m_profileNodeId = nodeKey; // keep the copy button in sync with what's shown
    m_profileNodeKey->setText(nodeKey.isEmpty() ? QStringLiteral("unknown")
                                                : nodeKey);

    m_profileMessageButton->setVisible(!info.self && !info.id.isEmpty());

    // Wallet verification + eligibility badge are shown only on your own profile.
    if (m_profileVerifyButton)
        m_profileVerifyButton->setVisible(info.self);
    if (m_profileEligibility) {
        m_profileEligibility->setVisible(info.self);
        if (info.self)
            m_profileEligibility->setText(
                m_accountSolanaVerified
                    ? QStringLiteral("<span style='color:#3fb950'>Active "
                                     "\xC2\xB7 revenue-sharing eligible</span>")
                    : QStringLiteral("<span style='color:#d29922'>Not yet eligible "
                                     "\xE2\x80\x94 deposit >= 0.001 SOL and "
                                     "verify.</span>"));
    }

    // Solana address + QR + reset balance.
    if (solana.isEmpty()) {
        m_profileSolanaSection->hide();
    } else {
        m_profileSolanaSection->show();
        m_profileSolanaAddr->setText(solana);
        const QImage qr = QrCode::encodeToImage(QStringLiteral("solana:%1").arg(solana), 4, 3);
        if (!qr.isNull())
            m_profileQr->setPixmap(QPixmap::fromImage(qr));
        m_profileQr->setVisible(!qr.isNull());
        m_profileBalance->setText(info.solanaBalance.trimmed().isEmpty()
                                      ? QString::fromUtf8("\xE2\x80\x94")
                                      : info.solanaBalance.trimmed());
        m_profileBalanceButton->setEnabled(true);
        m_profileBalanceButton->setText("Check balance");
    }

    m_nodeProfilePanel->show();
}

void MainWindow::refreshProfileHostingStats()
{
    if (!m_profileHosting || !m_profileHostingLabel)
        return;
    // Only meaningful for your own node — served/clone counts are tracked locally.
    if (!m_profileIsSelf) {
        m_profileHosting->clear();
        m_profileHosting->setVisible(false);
        m_profileHostingLabel->setVisible(false);
        return;
    }
    QStringList lines;
    for (const RepositoryRecord &repo : std::as_const(m_repositories)) {
        if (repo.previewOnly)
            continue;
        const QPair<int, int> stats = m_repoStats.value(repo.owner + "/" + repo.name);
        lines << QStringLiteral(
                     "<b>%1</b> \xC2\xB7 %2 served \xC2\xB7 %3 clone%4<br>"
                     "<span style='color:#8b949e'>hosted since %5 \xC2\xB7 "
                     "last sync %6</span>")
                     .arg(repo.name.toHtmlEscaped())
                     .arg(stats.first)
                     .arg(stats.second)
                     .arg(stats.second == 1 ? QString() : QStringLiteral("s"),
                          formatRepoDate(repo.hostedSinceMs),
                          formatRepoDate(repo.lastSyncMs));
    }
    m_profileHosting->setText(
        lines.isEmpty() ? QStringLiteral("No hosted repositories yet.")
                        : lines.join(QStringLiteral("<br>")));
    m_profileHosting->setVisible(true);
    m_profileHostingLabel->setVisible(true);
}

void MainWindow::checkNodeBalance()
{
    const QString addr = m_profileSolanaValue.trimmed();
    if (addr.isEmpty())
        return;
    m_profileBalanceButton->setEnabled(false);
    m_profileBalanceButton->setText("Checking\xE2\x80\xA6");
    m_profileBalance->setText(QString::fromUtf8("\xE2\x80\xA6"));

    if (!isLikelySolanaAddress(addr)) {
        m_profileBalanceButton->setEnabled(true);
        m_profileBalanceButton->setText("Check balance");
        m_profileBalance->setText("Invalid address");
        return;
    }
    querySolanaBalance(addr, 0);
}

void MainWindow::querySolanaBalance(const QString &addr, int endpointIndex)
{
    const int count = int(sizeof(kSolanaRpcEndpoints) / sizeof(kSolanaRpcEndpoints[0]));
    if (endpointIndex >= count) {
        if (m_profileSolanaValue.trimmed() == addr) {
            m_profileBalanceButton->setEnabled(true);
            m_profileBalanceButton->setText("Refresh balance");
            m_profileBalance->setText("Unavailable");
        }
        return;
    }

    QNetworkRequest request(QUrl(QString::fromLatin1(kSolanaRpcEndpoints[endpointIndex])));
    request.setHeader(QNetworkRequest::ContentTypeHeader,
                      QStringLiteral("application/json"));
    const QJsonObject body{
        {"jsonrpc", "2.0"},
        {"id", 1},
        {"method", "getBalance"},
        {"params", QJsonArray{addr}},
    };
    QNetworkReply *reply = m_networkAccess->post(
        request, QJsonDocument(body).toJson(QJsonDocument::Compact));
    connect(reply, &QNetworkReply::finished, this, [this, reply, addr, endpointIndex]() {
        const QByteArray raw = reply->readAll();
        const QNetworkReply::NetworkError netError = reply->error();
        reply->deleteLater();
        if (m_profileSolanaValue.trimmed() != addr)
            return;  // panel moved to another node meanwhile
        const QJsonObject root = QJsonDocument::fromJson(raw).object();
        const QJsonObject result = root.value("result").toObject();
        if (netError != QNetworkReply::NoError || !result.contains("value")) {
            querySolanaBalance(addr, endpointIndex + 1);
            return;
        }
        const qint64 lamports = result.value("value").toVariant().toLongLong();
        m_profileBalanceButton->setEnabled(true);
        m_profileBalanceButton->setText("Refresh balance");
        m_profileBalance->setText(formatSolanaBalance(lamports));
    });
}

void MainWindow::selectNode(const QString &node)
{
    if (m_selectedNode == node)
        return;
    m_selectedNode = node;
    refreshRepositoryList();
}

// ---- Issues section --------------------------------------------------------

QWidget *MainWindow::buildIssuesSection()
{
    auto *page = new QWidget;

    // --- Left: a sortable issue table with filters above.
    auto *listPane = new QWidget;
    listPane->setMinimumWidth(360);

    auto *heading = new QLabel("Issues");
    heading->setObjectName("channelTitle");

    m_issuesRepoCombo = new QComboBox;
    m_issuesRepoCombo->setToolTip("Repository whose issues you are viewing");
    // The repo is fixed by the repo-detail view that hosts this panel; the combo
    // is kept for state but hidden from the user.
    m_issuesRepoCombo->hide();

    m_issueSearch = new QLineEdit;
    m_issueSearch->setObjectName("issueSearch");
    m_issueSearch->setPlaceholderText("Search issues\xE2\x80\xA6");
    m_issueSearch->setClearButtonEnabled(true);

    m_issueStatusFilter = new QComboBox;
    m_issueStatusFilter->setObjectName("issueControlSm");
    m_issueStatusFilter->addItems({"Open", "Closed", "All"});
    m_issueLabelFilter = new QComboBox;
    m_issueLabelFilter->setObjectName("issueControlSm");
    m_issueMilestoneFilter = new QComboBox;
    m_issueMilestoneFilter->setObjectName("issueControlSm");
    auto *filterRow = new QHBoxLayout;
    filterRow->setContentsMargins(0, 0, 0, 0);
    filterRow->addWidget(m_issueSearch, 1);
    filterRow->addWidget(m_issueLabelFilter);
    filterRow->addWidget(m_issueMilestoneFilter);

    m_issueNewButton = new QPushButton("New issue");
    m_issueNewButton->setObjectName("primaryButton");
    m_issueNewButton->setProperty("buttonSize", "sm");
    m_issueNewButton->setCursor(Qt::PointingHandCursor);
    setOcticon(m_issueNewButton, "plus", 16);
    m_issueSyncButton = new QPushButton("Sync inbox");
    m_issueSyncButton->setObjectName("ghostButton");
    m_issueSyncButton->setProperty("buttonSize", "sm");
    m_issueSyncButton->setCursor(Qt::PointingHandCursor);
    setOcticon(m_issueSyncButton, "sync", 16);
    m_issueSyncButton->setToolTip(
        "Pull issue/comment submissions filed by other nodes and merge them");
    auto *issueBurnupButton = new QPushButton("Burn-up chart");
    issueBurnupButton->setObjectName("ghostButton");
    issueBurnupButton->setProperty("buttonSize", "sm");
    issueBurnupButton->setCursor(Qt::PointingHandCursor);
    issueBurnupButton->setToolTip(
        "Show open and closed issue totals over time");
    setOcticon(issueBurnupButton, "graph", 16);
    m_issueDetailToggle = new QPushButton("Hide detail");
    m_issueDetailToggle->setObjectName("ghostButton");
    m_issueDetailToggle->setCursor(Qt::PointingHandCursor);
    m_issueDetailToggle->setToolTip("Show/hide the issue detail panel");
    m_issueCreditsLabel = new QLabel;
    m_issueCreditsLabel->setObjectName("statusLine");
    m_issueCreditsLabel->setToolTip("Voting credits — earn 1 per hour online");
    auto *actionRow = new QHBoxLayout;
    actionRow->setContentsMargins(0, 0, 0, 0);
    actionRow->addWidget(m_issueSyncButton);
    actionRow->addWidget(issueBurnupButton);
    actionRow->addWidget(m_issueStatusFilter);
    actionRow->addStretch();
    actionRow->addWidget(m_issueCreditsLabel);
    actionRow->addWidget(m_issueDetailToggle);

    m_issueTable = new QTableWidget(0, 8);
    m_issueTable->setObjectName("issueTable");
    m_issueTable->setHorizontalHeaderLabels(
        {"#", "Title", "Status", "Votes", "Labels", "Milestone", "Created", "Agent"});
    m_issueTable->verticalHeader()->setVisible(false);
    m_issueTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_issueTable->setSelectionMode(QAbstractItemView::SingleSelection);
    m_issueTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_issueTable->setShowGrid(false);
    m_issueTable->setWordWrap(false);
    m_issueTable->setSortingEnabled(true);
    // Default to newest issue first (highest number on top).
    m_issueTable->sortByColumn(0, Qt::DescendingOrder);
    m_issueTable->setToolTip("Click a column header to sort");
    QHeaderView *header = m_issueTable->horizontalHeader();
    header->setHighlightSections(false);
    header->setSectionResizeMode(0, QHeaderView::ResizeToContents); // #
    header->setSectionResizeMode(1, QHeaderView::Stretch);          // Title
    header->setSectionResizeMode(2, QHeaderView::ResizeToContents); // Status
    header->setSectionResizeMode(3, QHeaderView::ResizeToContents); // Votes
    header->setSectionResizeMode(4, QHeaderView::ResizeToContents); // Labels
    header->setSectionResizeMode(5, QHeaderView::ResizeToContents); // Milestone
    header->setSectionResizeMode(6, QHeaderView::ResizeToContents); // Created
    header->setSectionResizeMode(7, QHeaderView::ResizeToContents); // Agent

    auto *listLayout = new QVBoxLayout(listPane);
    listLayout->setContentsMargins(18, 18, 12, 18);
    listLayout->setSpacing(8);
    listLayout->addWidget(heading);
    listLayout->addWidget(m_issuesRepoCombo);
    listLayout->addLayout(filterRow);
    listLayout->addLayout(actionRow);
    listLayout->addWidget(m_issueTable, 1);

    // Center: GitHub-style selected issue page: title header, status, timeline and
    // comment composer.
    m_issueTitle = new QLabel("Select an issue");
    m_issueTitle->setObjectName("issuePageTitle");
    m_issueTitle->setTextFormat(Qt::RichText);
    m_issueTitle->setWordWrap(true);
    m_issueTitleEditor = new QLineEdit;
    m_issueTitleEditor->setObjectName("issueTitleEditor");
    m_issueTitleEditor->setPlaceholderText("Issue title");
    m_issueTitleEditor->hide();
    m_issueTitleEditButton = new QPushButton;
    m_issueTitleEditButton->setObjectName("issueIconButton");
    m_issueTitleEditButton->setFixedSize(30, 30);
    m_issueTitleEditButton->setCursor(Qt::PointingHandCursor);
    m_issueTitleEditButton->setToolTip("Edit title");
    setOcticon(m_issueTitleEditButton, "pencil", 15);
    m_issueTitleSaveButton = new QPushButton("Save");
    m_issueTitleSaveButton->setObjectName("primaryButton");
    m_issueTitleSaveButton->setProperty("buttonSize", "sm");
    m_issueTitleSaveButton->setCursor(Qt::PointingHandCursor);
    m_issueTitleSaveButton->hide();
    m_issueTitleCancelButton = new QPushButton("Cancel");
    m_issueTitleCancelButton->setObjectName("ghostButton");
    m_issueTitleCancelButton->setProperty("buttonSize", "sm");
    m_issueTitleCancelButton->setCursor(Qt::PointingHandCursor);
    m_issueTitleCancelButton->hide();
    m_issueCopyButton = new QPushButton;
    m_issueCopyButton->setObjectName("issueIconButton");
    m_issueCopyButton->setFixedSize(30, 30);
    m_issueCopyButton->setCursor(Qt::PointingHandCursor);
    m_issueCopyButton->setToolTip("Copy this issue (title and thread) to the clipboard");
    setOcticon(m_issueCopyButton, "copy", 16);
    m_issueVoteButton = new QPushButton("Vote");
    m_issueVoteButton->setObjectName("ghostButton");
    m_issueVoteButton->setCursor(Qt::PointingHandCursor);
    setOcticon(m_issueVoteButton, "thumbsup", 16);
    m_issueVoteButton->setToolTip("Upvote this issue (spends 1 voting credit)");
    auto *issueTitleRow = new QHBoxLayout;
    issueTitleRow->setContentsMargins(0, 0, 0, 0);
    issueTitleRow->setSpacing(8);
    issueTitleRow->addWidget(m_issueTitle, 1);
    issueTitleRow->addWidget(m_issueTitleEditor, 1);
    issueTitleRow->addWidget(m_issueTitleSaveButton, 0, Qt::AlignTop);
    issueTitleRow->addWidget(m_issueTitleCancelButton, 0, Qt::AlignTop);
    issueTitleRow->addWidget(m_issueTitleEditButton, 0, Qt::AlignTop);
    issueTitleRow->addStretch();
    issueTitleRow->addWidget(m_issueNewButton, 0, Qt::AlignTop);
    issueTitleRow->addWidget(m_issueCopyButton, 0, Qt::AlignTop);
    m_issueMeta = new QLabel; // Open/Closed status pill
    m_issueMeta->setObjectName("issueStatusPill");
    m_issueMeta->setTextFormat(Qt::PlainText);
    m_issueMeta->setAlignment(Qt::AlignCenter);
    m_issueMeta->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    m_issueReadonlyNote = new QLabel;
    m_issueReadonlyNote->setObjectName("statusLine");
    m_issueReadonlyNote->setWordWrap(true);
    m_issueReadonlyNote->hide();
    m_issueInlineNotice = new QLabel;
    m_issueInlineNotice->setObjectName("issueInlineNotice");
    m_issueInlineNotice->setWordWrap(true);
    m_issueInlineNotice->hide();

    m_issueThreadContainer = new QWidget;
    m_issueThreadLayout = new QVBoxLayout(m_issueThreadContainer);
    m_issueThreadLayout->setContentsMargins(0, 0, 0, 0);
    m_issueThreadLayout->setSpacing(10);
    m_issueThreadLayout->addStretch();
    m_issueThreadScroll = new QScrollArea;
    m_issueThreadScroll->setWidgetResizable(true);
    m_issueThreadScroll->setWidget(m_issueThreadContainer);
    m_issueThreadScroll->setObjectName("issuePageScroll");
    m_issueThreadScroll->setFrameShape(QFrame::NoFrame);

    auto *commentAvatar = new QLabel("FM");
    commentAvatar->setObjectName("issueAvatar");
    commentAvatar->setAlignment(Qt::AlignCenter);
    commentAvatar->setFixedSize(36, 36);
    commentAvatar->setScaledContents(true);
    m_issueComposerAvatar = commentAvatar;
    refreshIssueComposerAvatar();
    auto *commentTitle = new QLabel("Add a comment");
    commentTitle->setObjectName("issueCommentTitle");
    m_issueComposer = new MarkdownEditor;
    m_issueComposer->setObjectName("issueCommentEditor");
    m_issueComposer->setMinimumHeight(190);
    m_issueComposer->setPlaceholderText("Use Markdown to format your comment");
    m_issueAttachButton = new QPushButton("Paste, drop, or click to add files");
    m_issueCloseButton = new QPushButton("Close issue");
    m_issueCommentButton = new QPushButton("Comment");
    m_issueCommentButton->setObjectName("primaryButton");
    m_issueCommentButton->setCursor(Qt::PointingHandCursor);
    for (QPushButton *b : {m_issueAttachButton, m_issueCloseButton, m_issueVoteButton}) {
        b->setObjectName("ghostButton");
        b->setCursor(Qt::PointingHandCursor);
    }
    setOcticon(m_issueAttachButton, "paperclip", 16);
    auto *commentButtonRow = new QHBoxLayout;
    commentButtonRow->setContentsMargins(0, 0, 0, 0);
    commentButtonRow->addWidget(m_issueAttachButton, 0, Qt::AlignLeft);
    commentButtonRow->addStretch();
    commentButtonRow->addWidget(m_issueVoteButton);
    commentButtonRow->addWidget(m_issueCloseButton);
    commentButtonRow->addWidget(m_issueCommentButton);
    auto *commentColumn = new QVBoxLayout;
    commentColumn->setContentsMargins(0, 0, 0, 0);
    commentColumn->setSpacing(8);
    commentColumn->addWidget(commentTitle);
    commentColumn->addWidget(m_issueComposer);
    commentColumn->addLayout(commentButtonRow);
    auto *composerRow = new QHBoxLayout;
    composerRow->setContentsMargins(0, 0, 0, 0);
    composerRow->setSpacing(14);
    composerRow->addWidget(commentAvatar, 0, Qt::AlignTop);
    composerRow->addLayout(commentColumn, 1);

    auto *center = new QWidget;
    auto *centerLayout = new QVBoxLayout(center);
    centerLayout->setContentsMargins(24, 22, 22, 22);
    centerLayout->setSpacing(12);
    centerLayout->addLayout(issueTitleRow);
    centerLayout->addWidget(m_issueMeta);
    centerLayout->addWidget(m_issueReadonlyNote);
    centerLayout->addWidget(m_issueInlineNotice);
    auto *issueDivider = new QWidget;
    issueDivider->setObjectName("issueDivider");
    issueDivider->setFixedHeight(1);
    centerLayout->addWidget(issueDivider);
    centerLayout->addWidget(m_issueThreadScroll, 1);
    centerLayout->addLayout(composerRow);

    // Right: GitHub-style metadata sidebar.
    auto *meta = new QWidget;
    meta->setObjectName("issueSidebar");
    meta->setMinimumWidth(265);
    meta->setMaximumWidth(315);
    m_issueAssigneesValue = new QLabel("No one - <a href='#'>Assign yourself</a>");
    m_issueLabelsValue = new QLabel("No labels");
    m_issueMilestoneValue = new QLabel("No milestone");
    for (QLabel *v : {m_issueAssigneesValue, m_issueLabelsValue, m_issueMilestoneValue}) {
        v->setObjectName("statusLine");
        v->setWordWrap(true);
        v->setTextFormat(Qt::RichText);
        v->setOpenExternalLinks(false);
        v->setTextInteractionFlags(Qt::TextBrowserInteraction);
    }
    connect(m_issueAssigneesValue, &QLabel::linkActivated, this, [this] {
        editIssueAssignees();
        if (!m_issueAssigneesEdit)
            return;
        const QString who = m_userName.trimmed();
        if (who.isEmpty()) {
            setIssueInlineNotice("Set your profile name before assigning yourself.", true);
            return;
        }
        QStringList assignees = splitIssueFieldList(m_issueAssigneesEdit->text());
        if (!assignees.contains(who))
            assignees << who;
        m_issueAssigneesEdit->setText(assignees.join(", "));
    });
    m_issueLabelsButton = new QPushButton;
    m_issueMilestoneButton = new QPushButton;
    m_issueAssigneesButton = new QPushButton;
    m_issueDeleteButton = new QPushButton("Delete issue");
    for (QPushButton *b : {m_issueLabelsButton, m_issueMilestoneButton,
                           m_issueAssigneesButton}) {
        b->setObjectName("issueIconButton");
        b->setFixedSize(28, 28);
        b->setCursor(Qt::PointingHandCursor);
        setOcticon(b, "gear", 15);
    }
    m_issueDeleteButton->setObjectName("issueDangerLink");
    m_issueDeleteButton->setCursor(Qt::PointingHandCursor);
    setOcticon(m_issueDeleteButton, "trash", 15);
    auto makeValue = [](const QString &text) {
        auto *label = new QLabel(text);
        label->setObjectName("statusLine");
        label->setWordWrap(true);
        label->setTextFormat(Qt::RichText);
        return label;
    };
    auto makeGear = [&]() {
        auto *button = new QPushButton;
        button->setObjectName("issueIconButton");
        button->setFixedSize(28, 28);
        button->setEnabled(false);
        setOcticon(button, "gear", 15);
        return button;
    };
    auto makeAction = [&](const QString &text, const QString &icon = QString()) {
        auto *button = new QPushButton(text);
        button->setObjectName("issueSidebarAction");
        button->setCursor(Qt::PointingHandCursor);
        if (!icon.isEmpty())
            setOcticon(button, icon, 15);
        return button;
    };
    auto makeEditorButton = [&](const QString &text, const char *objectName) {
        auto *button = new QPushButton(text, meta);
        button->setObjectName(objectName);
        button->setProperty("buttonSize", "sm");
        button->setCursor(Qt::PointingHandCursor);
        return button;
    };
    auto makeInlineButtonRow = [&](QPushButton *save, QPushButton *cancel) {
        auto *rowWidget = new QWidget(meta);
        auto *row = new QHBoxLayout(rowWidget);
        row->setContentsMargins(0, 0, 0, 0);
        row->setSpacing(6);
        row->addStretch();
        row->addWidget(cancel);
        row->addWidget(save);
        return rowWidget;
    };

    m_issueAssigneesStack = new QStackedWidget(meta);
    m_issueAssigneesStack->addWidget(m_issueAssigneesValue);
    auto *assigneesEditBox = new QWidget(meta);
    auto *assigneesEditLayout = new QVBoxLayout(assigneesEditBox);
    assigneesEditLayout->setContentsMargins(0, 0, 0, 0);
    assigneesEditLayout->setSpacing(6);
    m_issueAssigneesEdit = new QLineEdit(meta);
    m_issueAssigneesEdit->setPlaceholderText("No one");
    auto *assignSelf = makeEditorButton("Assign yourself", "ghostButton");
    auto *assigneesSave = makeEditorButton("Save", "primaryButton");
    auto *assigneesCancel = makeEditorButton("Cancel", "ghostButton");
    assigneesEditLayout->addWidget(m_issueAssigneesEdit);
    assigneesEditLayout->addWidget(assignSelf, 0, Qt::AlignLeft);
    assigneesEditLayout->addWidget(makeInlineButtonRow(assigneesSave, assigneesCancel));
    m_issueAssigneesStack->addWidget(assigneesEditBox);
    connect(assignSelf, &QPushButton::clicked, this, [this] {
        if (!m_issueAssigneesEdit)
            return;
        const QString who = m_userName.trimmed();
        if (who.isEmpty()) {
            setIssueInlineNotice("Set your profile name before assigning yourself.", true);
            return;
        }
        QStringList assignees = splitIssueFieldList(m_issueAssigneesEdit->text());
        if (!assignees.contains(who))
            assignees << who;
        m_issueAssigneesEdit->setText(assignees.join(", "));
    });
    connect(assigneesSave, &QPushButton::clicked, this,
            &MainWindow::saveIssueAssigneesInline);
    connect(assigneesCancel, &QPushButton::clicked, this,
            &MainWindow::cancelIssueSidebarEditors);
    connect(m_issueAssigneesEdit, &QLineEdit::returnPressed, this,
            &MainWindow::saveIssueAssigneesInline);

    m_issueLabelsStack = new QStackedWidget(meta);
    m_issueLabelsStack->addWidget(m_issueLabelsValue);
    auto *labelsEditBox = new QWidget(meta);
    auto *labelsEditLayout = new QVBoxLayout(labelsEditBox);
    labelsEditLayout->setContentsMargins(0, 0, 0, 0);
    labelsEditLayout->setSpacing(6);
    m_issueLabelsEdit = new QLineEdit(meta);
    m_issueLabelsEdit->setPlaceholderText("No labels");
    auto *labelsSave = makeEditorButton("Save", "primaryButton");
    auto *labelsCancel = makeEditorButton("Cancel", "ghostButton");
    labelsEditLayout->addWidget(m_issueLabelsEdit);
    labelsEditLayout->addWidget(makeInlineButtonRow(labelsSave, labelsCancel));
    m_issueLabelsStack->addWidget(labelsEditBox);
    connect(labelsSave, &QPushButton::clicked, this,
            &MainWindow::saveIssueLabelsInline);
    connect(labelsCancel, &QPushButton::clicked, this,
            &MainWindow::cancelIssueSidebarEditors);
    connect(m_issueLabelsEdit, &QLineEdit::returnPressed, this,
            &MainWindow::saveIssueLabelsInline);

    m_issueMilestoneStack = new QStackedWidget(meta);
    m_issueMilestoneStack->addWidget(m_issueMilestoneValue);
    auto *milestoneEditBox = new QWidget(meta);
    auto *milestoneEditLayout = new QVBoxLayout(milestoneEditBox);
    milestoneEditLayout->setContentsMargins(0, 0, 0, 0);
    milestoneEditLayout->setSpacing(6);
    m_issueMilestoneEdit = new QComboBox(meta);
    auto *milestoneSave = makeEditorButton("Save", "primaryButton");
    auto *milestoneCancel = makeEditorButton("Cancel", "ghostButton");
    milestoneEditLayout->addWidget(m_issueMilestoneEdit);
    milestoneEditLayout->addWidget(makeInlineButtonRow(milestoneSave, milestoneCancel));
    m_issueMilestoneStack->addWidget(milestoneEditBox);
    connect(milestoneSave, &QPushButton::clicked, this,
            &MainWindow::saveIssueMilestoneInline);
    connect(milestoneCancel, &QPushButton::clicked, this,
            &MainWindow::cancelIssueSidebarEditors);

    auto *agentBox = new QWidget(meta);
    auto *agentLayout = new QVBoxLayout(agentBox);
    agentLayout->setContentsMargins(0, 0, 0, 0);
    agentLayout->setSpacing(6);
    m_issueAgentValue = new QLabel("No agent assigned", meta);
    m_issueAgentValue->setObjectName("statusLine");
    m_issueAgentValue->setWordWrap(true);
    m_issueAgentValue->setTextFormat(Qt::RichText);
    m_issueAssignCodexButton = makeEditorButton("Assign to Codex", "ghostButton");
    m_issueAssignClaudeButton =
        makeEditorButton("Assign to Claude Code", "ghostButton");
    m_issueAgentCreatePrCheck = new QCheckBox("Create a PR", meta);
    m_issueAgentCreatePrCheck->setToolTip(
        "If the agent produces a patch, create a ForkMesh pull request from it.");
    m_issueAgentViewButton = makeEditorButton("View session", "primaryButton");
    m_issueAgentViewButton->hide();
    setOcticon(m_issueAssignCodexButton, "terminal", 15);
    setOcticon(m_issueAssignClaudeButton, "code", 15);
    setOcticon(m_issueAgentViewButton, "chevron-right", 15);
    agentLayout->addWidget(m_issueAgentValue);
    agentLayout->addWidget(m_issueAssignCodexButton);
    agentLayout->addWidget(m_issueAssignClaudeButton);
    agentLayout->addWidget(m_issueAgentCreatePrCheck);
    agentLayout->addWidget(m_issueAgentViewButton, 0, Qt::AlignLeft);
    connect(m_issueAssignCodexButton, &QPushButton::clicked, this,
            [this] { assignIssueToAgent(QStringLiteral("codex")); });
    connect(m_issueAssignClaudeButton, &QPushButton::clicked, this,
            [this] { assignIssueToAgent(QStringLiteral("claude")); });
    connect(m_issueAgentViewButton, &QPushButton::clicked, this,
            &MainWindow::openAgentSessionFromIssue);

    auto *metaLayout = new QVBoxLayout(meta);
    metaLayout->setContentsMargins(22, 22, 10, 22);
    metaLayout->setSpacing(0);
    auto addDivider = [&] {
        auto *line = new QWidget(meta);
        line->setObjectName("issueSidebarDivider");
        line->setFixedHeight(1);
        metaLayout->addWidget(line);
    };
    auto addMetaSection = [&](const QString &label, QWidget *value,
                              QPushButton *btn = nullptr) {
        auto *header = new QHBoxLayout;
        header->setContentsMargins(0, 0, 0, 0);
        auto *l = new QLabel(label);
        l->setObjectName("issueSidebarHeading");
        header->addWidget(l);
        header->addStretch();
        if (btn)
            header->addWidget(btn);
        metaLayout->addLayout(header);
        metaLayout->addWidget(value);
        metaLayout->addSpacing(14);
        addDivider();
        metaLayout->addSpacing(14);
    };
    addMetaSection("Assignees", m_issueAssigneesStack, m_issueAssigneesButton);
    addMetaSection("Assign to agent", agentBox);
    addMetaSection("Labels", m_issueLabelsStack, m_issueLabelsButton);
    addMetaSection("Type", makeValue("No type"), makeGear());
    addMetaSection("Fields", makeValue("Priority <span style='float:right'>Choose an option</span>"));
    addMetaSection("Projects", makeValue("No projects"), makeGear());
    addMetaSection("Milestone", m_issueMilestoneStack, m_issueMilestoneButton);
    addMetaSection("Relationships", makeValue("None yet"), makeGear());
    addMetaSection("Development",
                   makeValue("Agent sessions and pull requests are linked here."));
    addMetaSection("Notifications", makeValue("You are receiving notifications because you're subscribed to this thread."));
    addMetaSection("Participants", makeValue("No participants"));
    auto *transferIssue = makeAction("Transfer issue", "arrow-left");
    auto *cloneIssue = makeAction("Clone issue", "copy");
    auto *lockIssue = makeAction("Lock conversation", "lock");
    auto *pinIssue = makeAction("Pin issue", "tag");
    auto *feedbackIssue = makeAction("Give feedback", "comment");
    for (QPushButton *action :
         {transferIssue, cloneIssue, lockIssue, pinIssue, m_issueDeleteButton,
          feedbackIssue})
        metaLayout->addWidget(action);
    metaLayout->addStretch();

    // Collapsible detail panel: the issue thread (center) + metadata sidebar,
    // sharing their own draggable divider.
    auto *issueDetailView = new QWidget;
    auto *detailSplit = new QSplitter(Qt::Horizontal);
    detailSplit->setChildrenCollapsible(false);
    detailSplit->addWidget(center);
    detailSplit->addWidget(meta);
    detailSplit->setStretchFactor(0, 1);
    detailSplit->setStretchFactor(1, 0);
    detailSplit->setSizes({520, 220});
    auto *detailLayout = new QVBoxLayout(issueDetailView);
    detailLayout->setContentsMargins(0, 0, 0, 0);
    detailLayout->addWidget(detailSplit);

    m_issueDetailStack = new QStackedWidget;
    m_issueDetailStack->addWidget(issueDetailView);
    m_issueDetail = m_issueDetailStack;

    // The table and the detail panel share a draggable divider; hiding the
    // detail lets the table use the full width.
    auto *splitter = new QSplitter(Qt::Horizontal);
    splitter->setObjectName("issuesSplitter");
    splitter->setChildrenCollapsible(false);
    splitter->addWidget(listPane);
    splitter->addWidget(m_issueDetail);
    splitter->setStretchFactor(0, 1);
    splitter->setStretchFactor(1, 1);
    splitter->setSizes({520, 560});

    auto *layout = new QHBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(splitter);

    connect(m_issueDetailToggle, &QPushButton::clicked, this, [this] {
        const bool show = !m_issueDetail->isVisible();
        m_issueDetail->setVisible(show);
        m_issueDetailToggle->setText(show ? "Hide detail" : "Show detail");
    });
    connect(m_issuesRepoCombo, &QComboBox::currentIndexChanged, this,
            [this](int) { reloadIssues(); });
    connect(m_issueStatusFilter, &QComboBox::currentIndexChanged, this,
            [this](int) { refreshIssueList(); });
    connect(m_issueLabelFilter, &QComboBox::currentIndexChanged, this,
            [this](int) { refreshIssueList(); });
    connect(m_issueMilestoneFilter, &QComboBox::currentIndexChanged, this,
            [this](int) { refreshIssueList(); });
    connect(m_issueSearch, &QLineEdit::textChanged, this,
            [this] { refreshIssueList(); });
    connect(m_issueTable, &QTableWidget::itemSelectionChanged, this, [this] {
        const QModelIndexList rows = m_issueTable->selectionModel()->selectedRows();
        if (rows.isEmpty())
            return;
        QTableWidgetItem *first = m_issueTable->item(rows.first().row(), 0);
        if (first)
            showIssue(first->data(Qt::UserRole).toInt());
    });
    connect(m_issueNewButton, &QPushButton::clicked, this, &MainWindow::promptNewIssue);
    connect(m_issueSyncButton, &QPushButton::clicked, this,
            &MainWindow::syncIssuesInbox);
    connect(issueBurnupButton, &QPushButton::clicked, this,
            &MainWindow::showIssueBurnupChart);
    connect(m_issueTitleEditButton, &QPushButton::clicked, this,
            &MainWindow::promptEditIssueTitle);
    connect(m_issueTitleSaveButton, &QPushButton::clicked, this,
            &MainWindow::saveIssueTitleEdit);
    connect(m_issueTitleCancelButton, &QPushButton::clicked, this,
            &MainWindow::cancelIssueTitleEdit);
    connect(m_issueTitleEditor, &QLineEdit::returnPressed, this,
            &MainWindow::saveIssueTitleEdit);
    connect(m_issueCopyButton, &QPushButton::clicked, this,
            &MainWindow::copyIssueToClipboard);
    connect(m_issueVoteButton, &QPushButton::clicked, this,
            &MainWindow::voteOnCurrentIssue);
    connect(m_issueCommentButton, &QPushButton::clicked, this,
            &MainWindow::addIssueComment);
    connect(m_issueAttachButton, &QPushButton::clicked, this,
            &MainWindow::attachIssueImage);
    connect(m_issueCloseButton, &QPushButton::clicked, this,
            &MainWindow::toggleIssueStatus);
    connect(m_issueDeleteButton, &QPushButton::clicked, this,
            &MainWindow::deleteCurrentIssue);
    connect(m_issueLabelsButton, &QPushButton::clicked, this,
            &MainWindow::editIssueLabels);
    connect(m_issueMilestoneButton, &QPushButton::clicked, this,
            &MainWindow::editIssueMilestone);
    connect(m_issueAssigneesButton, &QPushButton::clicked, this,
            &MainWindow::editIssueAssignees);
    return page;
}

// ---- Repo detail (files + issues tabs) -------------------------------------

QWidget *MainWindow::buildRepoDetailSection()
{
    auto *page = new QWidget;

    // --- GitHub-style header: title + Public badge, action buttons on the right.
    // The repo identity and public/private state now live in the top-bar repo
    // dropdown, so the old "owner/name  Public" header is omitted here. The label
    // is still created (hidden) because other code sets its text.
    m_repoHeaderTitle = new QLabel("Repository");
    m_repoHeaderTitle->setObjectName("repoHeaderTitle");
    m_repoHeaderTitle->setTextFormat(Qt::RichText);
    m_repoHeaderTitle->hide();

    auto *notifyButton = new QPushButton("Notify");
    notifyButton->setObjectName("repoAction");
    notifyButton->setToolTip("Notifications");
    setOcticon(notifyButton, "bell", 16);
    m_forkButton = new QPushButton("Fork 0");
    m_mirrorButton = new QPushButton("Mirror 1");
    m_sourceButton = new QPushButton("Source");
    m_starButton = new QPushButton("Star 0");
    // Open-in-browser link, mirroring the relay switcher's open button: takes
    // the active repo to its page on the mainnode website.
    m_repoOpenButton = new QPushButton("Open");
    for (QPushButton *b :
         {notifyButton, m_forkButton, m_mirrorButton, m_sourceButton,
          m_starButton, m_repoOpenButton}) {
        b->setObjectName("repoAction");
        b->setCursor(Qt::PointingHandCursor);
    }
    setOcticon(m_forkButton, "repo-forked", 16);
    setOcticon(m_mirrorButton, "sync", 16);
    setOcticon(m_sourceButton, "code", 16);
    setOcticon(m_starButton, "star", 16);
    setOcticon(m_repoOpenButton, "link", 16);
    m_repoOpenButton->setToolTip("Open this repository on the web");
    connect(m_repoOpenButton, &QPushButton::clicked, this,
            &MainWindow::openRepositoryWebsite);
    m_mirrorButton->setToolTip("Mirror status and actions");
    m_forkButton->setToolTip("Fork destination and working directory");
    m_sourceButton->setToolTip("Download or use this repository's local remote");
    m_mirrorMenu = new QMenu(m_mirrorButton);
    m_forkMenu = new QMenu(m_forkButton);
    m_sourceMenu = new QMenu(m_sourceButton);
    m_mirrorButton->setMenu(m_mirrorMenu);
    m_forkButton->setMenu(m_forkMenu);
    m_sourceButton->setMenu(m_sourceMenu);
    connect(m_mirrorMenu, &QMenu::aboutToShow, this,
            &MainWindow::updateRepoActionMenus);
    connect(m_forkMenu, &QMenu::aboutToShow, this,
            &MainWindow::updateRepoActionMenus);
    connect(m_sourceMenu, &QMenu::aboutToShow, this,
            &MainWindow::updateRepoActionMenus);

    auto *headerRow = new QHBoxLayout;
    headerRow->setContentsMargins(16, 12, 16, 4);
    headerRow->setSpacing(8);
    headerRow->addStretch();
    headerRow->addWidget(notifyButton);
    headerRow->addWidget(m_forkButton);
    headerRow->addWidget(m_mirrorButton);
    headerRow->addWidget(m_sourceButton);
    headerRow->addWidget(m_repoOpenButton);
    headerRow->addWidget(m_starButton);

    m_repoDetailNotice = new QLabel;
    m_repoDetailNotice->setObjectName("repoInlineNotice");
    m_repoDetailNotice->setWordWrap(true);
    m_repoDetailNotice->hide();

    auto *metaBand = new QWidget;
    metaBand->setObjectName("repoDetailMeta");
    m_repoDetailStatus = new QLabel;
    m_repoDetailStatus->setObjectName("statusLine");
    m_repoDetailStatus->setWordWrap(true);
    m_repoDetailStatus->setTextFormat(Qt::RichText);

    auto *metaLayout = new QVBoxLayout(metaBand);
    metaLayout->setContentsMargins(16, 4, 16, 8);
    metaLayout->setSpacing(6);
    metaLayout->addWidget(m_repoDetailStatus);
    // Served/clone counts and hosted-since/last-sync now live in the node
    // profile panel, so this band stays hidden in the repo view.
    metaBand->hide();

    // --- Tab bar (GitHub order; Commits gets its own tab).
    struct TabDef {
        const char *label;
        const char *icon;
    };
    // Note: existing pages are index-addressed in several places (idClicked,
    // switchTo*). Branches/Releases are appended after Insights so those indices
    // stay valid; Chat is added last and tracked via m_chatStackIndex.
    const QList<TabDef> tabs = {{"Code", "code"},
                                {"Commits", "git-branch"},
                                {"Issues", "issue-opened"},
                                {"Agents", "terminal"},
                                {"Pull requests", "git-pull-request"},
                                {"Actions", "workflow"},
                                {"Security and quality", "shield-check"},
                                {"Insights", "graph"},
                                {"Branches", "repo-forked"},
                                {"Releases", "tag"}};
    m_repoDetailTabs = new QButtonGroup(this);
    m_repoDetailTabs->setExclusive(true);
    auto *tabRow = new QHBoxLayout;
    tabRow->setContentsMargins(12, 0, 12, 0);
    tabRow->setSpacing(2);
    for (int i = 0; i < tabs.size(); ++i) {
        const TabDef tab = tabs.at(i);
        auto *b = new QPushButton(QString::fromLatin1(tab.label));
        b->setObjectName("repoTab");
        b->setCheckable(true);
        b->setCursor(Qt::PointingHandCursor);
        setOcticon(b, QString::fromLatin1(tab.icon), 16);
        if (i == 0)
            b->setChecked(true);
        if (i == 0)
            m_repoCodeTab = b;
        if (i == 1)
            m_repoCommitsTab = b;
        if (i == 2)
            m_repoIssuesTab = b; // keep a handle for the Issues (N) badge
        if (i == 3)
            m_repoAgentsTab = b; // handle for the Agents (N) badge
        if (i == 4)
            m_repoPullsTab = b;
        if (i == 5)
            m_repoActionsTab = b; // handle for the Actions (N) badge
        m_repoDetailTabs->addButton(b, i);
        tabRow->addWidget(b);
    }
    tabRow->addStretch();
    auto *tabBar = new QWidget;
    tabBar->setObjectName("repoTabBar");
    tabBar->setLayout(tabRow);

    // --- Inner stack: one page per tab.
    m_repoDetailStack = new QStackedWidget;
    m_repoDetailStack->addWidget(buildRepoFilesPanel());                 // 0 Code
    m_repoDetailStack->addWidget(buildRepoCommitsTab());                 // 1 Commits
    m_repoDetailStack->addWidget(buildIssuesSection());                  // 2 Issues
    m_repoDetailStack->addWidget(buildAgentsTab());                      // 3 Agents
    m_repoDetailStack->addWidget(buildPullsTab());                       // 4 Pull requests
    m_repoDetailStack->addWidget(buildRepoActionsTab());                 // 5 Actions
    m_repoDetailStack->addWidget(buildPlaceholderTab("Security and quality")); // 6
    m_repoDetailStack->addWidget(buildInsightsTab());                    // 7
    m_branchesTabIndex = m_repoDetailStack->count();
    m_repoDetailStack->addWidget(buildBranchesTab());                    // 8 Branches
    m_releasesTabIndex = m_repoDetailStack->count();
    m_repoDetailStack->addWidget(buildReleasesTab());                    // 9 Releases
    // Chat has no repo tab any more — it's reached from the top-bar Chat button.
    m_chatStackIndex = m_repoDetailStack->count();
    m_repoDetailStack->addWidget(buildChatSection());                    // 10 Chat
    connect(m_repoDetailTabs, &QButtonGroup::idClicked, this, [this](int id) {
        m_repoDetailStack->setCurrentIndex(id);
        if (id == 1)
            loadCommits();
        else if (id == 3)
            reloadAgents();
        else if (id == 4)
            reloadPulls();
        else if (id == 5)
            refreshRepoActions();
        else if (id == 7)
            loadRepoInsights();
        else if (id == m_branchesTabIndex)
            loadBranchesPanel();
        else if (id == m_releasesTabIndex)
            loadReleasesPanel();
    });

    // Before any repository is opened the Code/Commits/… tabs have nothing to
    // show, so land on the always-useful Chat view (no tab; reached via the
    // top-bar Chat button). Opening a repo switches to Code (see openRepoDetail).
    m_repoDetailStack->setCurrentIndex(m_chatStackIndex);

    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(6);
    layout->addLayout(headerRow);
    layout->addWidget(m_repoDetailNotice);
    layout->addWidget(metaBand);
    layout->addWidget(tabBar);
    layout->addWidget(m_repoDetailStack, 1);
    return page;
}

QWidget *MainWindow::buildRepoCommitsTab()
{
    m_commitsStack = new QStackedWidget;

    // --- Page 0: the commit list.
    auto *listPage = new QWidget;
    m_commitsTable = new QTableWidget(0, 6);
    m_commitsTable->setObjectName("commitsList");
    m_commitsTable->setHorizontalHeaderLabels(
        {"Author", "Date", "Files", "+adds", "-dels", "Summary"});
    m_commitsTable->verticalHeader()->setVisible(false);
    m_commitsTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_commitsTable->setSelectionMode(QAbstractItemView::SingleSelection);
    m_commitsTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_commitsTable->setShowGrid(false);
    m_commitsTable->setWordWrap(false);
    m_commitsTable->setSortingEnabled(true);
    m_commitsTable->setToolTip("Click a column header to sort");
    m_commitsTable->setTextElideMode(Qt::ElideRight);
    // Fixed default column widths instead of ResizeToContents: the latter
    // rescans every row on each resize, which makes dragging the splitter
    // beside a 300-row table choppy. Interactive sections stay smooth.
    QHeaderView *commitHeader = m_commitsTable->horizontalHeader();
    commitHeader->setHighlightSections(false);
    commitHeader->setSectionResizeMode(QHeaderView::Interactive);
    const int commitColWidths[kCommitSummaryCol] = {150, 72, 60, 66, 66};
    for (int i = 0; i < kCommitSummaryCol; ++i) {
        commitHeader->setSectionResizeMode(i, QHeaderView::Interactive);
        commitHeader->resizeSection(i, commitColWidths[i]);
    }
    commitHeader->setSectionResizeMode(kCommitSummaryCol, QHeaderView::Stretch);
    connect(m_commitsTable, &QTableWidget::cellClicked, this,
            [this](int row, int) {
                QTableWidgetItem *item = m_commitsTable->item(row, kCommitSummaryCol);
                if (item)
                    showCommit(item->data(Qt::UserRole).toString());
            });
    // Arrow-key navigation: when the current row changes (e.g. via Up/Down keys),
    // load and display the newly selected commit so the diff view stays in sync.
    connect(m_commitsTable, &QTableWidget::currentCellChanged, this,
            [this](int row, int, int prevRow, int) {
                if (row == prevRow || row < 0)
                    return;
                QTableWidgetItem *item = m_commitsTable->item(row, kCommitSummaryCol);
                if (item)
                    showCommit(item->data(Qt::UserRole).toString());
            });
    auto *listLayout = new QVBoxLayout(listPage);
    listLayout->setContentsMargins(16, 12, 16, 16);
    listLayout->addWidget(m_commitsTable);

    // --- Page 1: the GitHub-style commit diff view.
    auto *detailPage = new QWidget;

    auto *backButton = new QPushButton("Commits");
    backButton->setObjectName("ghostButton");
    backButton->setCursor(Qt::PointingHandCursor);
    setOcticon(backButton, "arrow-left", 16);
    connect(backButton, &QPushButton::clicked, this, &MainWindow::showCommitList);

    // Prev/Next walk the commit list (newest first): Prev = newer, Next = older.
    m_commitPrevButton = new QPushButton("Prev");
    m_commitNextButton = new QPushButton("Next");
    for (QPushButton *b : {m_commitPrevButton, m_commitNextButton}) {
        b->setObjectName("ghostButton");
        b->setCursor(Qt::PointingHandCursor);
    }
    setOcticon(m_commitPrevButton, "chevron-down", 16);
    setOcticon(m_commitNextButton, "chevron-right", 16);
    m_commitPrevButton->setToolTip("Show the previous (newer) commit");
    m_commitNextButton->setToolTip("Show the next (older) commit");
    auto goToCommitRow = [this](int row) {
        if (!m_commitsTable || row < 0 || row >= m_commitsTable->rowCount())
            return;
        QTableWidgetItem *it = m_commitsTable->item(row, kCommitSummaryCol);
        if (it)
            showCommit(it->data(Qt::UserRole).toString());
    };
    connect(m_commitPrevButton, &QPushButton::clicked, this,
            [this, goToCommitRow] { goToCommitRow(m_currentCommitRow - 1); });
    connect(m_commitNextButton, &QPushButton::clicked, this,
            [this, goToCommitRow] { goToCommitRow(m_currentCommitRow + 1); });

    m_commitTitle = new QLabel;
    m_commitTitle->setObjectName("repoHeaderTitle");
    m_commitTitle->setTextFormat(Qt::RichText);
    m_commitTitle->setTextInteractionFlags(Qt::TextSelectableByMouse);

    m_commitDownloadButton = new QPushButton("Download patch");
    m_commitDownloadButton->setObjectName("ghostButton");
    m_commitDownloadButton->setCursor(Qt::PointingHandCursor);
    m_commitDownloadButton->setToolTip(
        "Save this commit as a .patch file you can re-import as a pull request");
    setOcticon(m_commitDownloadButton, "download", 16);
    connect(m_commitDownloadButton, &QPushButton::clicked, this,
            &MainWindow::downloadCommitPatch);

    auto *navCol = new QVBoxLayout;
    navCol->setContentsMargins(0, 0, 0, 0);
    navCol->setSpacing(4);
    navCol->addWidget(backButton, 0, Qt::AlignRight);
    auto *prevNextRow = new QHBoxLayout;
    prevNextRow->setContentsMargins(0, 0, 0, 0);
    prevNextRow->setSpacing(4);
    prevNextRow->addStretch();
    prevNextRow->addWidget(m_commitDownloadButton);
    prevNextRow->addWidget(m_commitPrevButton);
    prevNextRow->addWidget(m_commitNextButton);
    navCol->addLayout(prevNextRow);

    auto *headerRow = new QHBoxLayout;
    headerRow->setContentsMargins(0, 0, 0, 0);
    headerRow->addWidget(m_commitTitle, 1, Qt::AlignTop);
    headerRow->addLayout(navCol);

    m_commitMessage = new QLabel;
    m_commitMessage->setObjectName("commitMessage");
    m_commitMessage->setWordWrap(true);
    m_commitMessage->setTextFormat(Qt::RichText);
    m_commitMessage->setTextInteractionFlags(Qt::TextSelectableByMouse);

    m_commitMeta = new QLabel;
    m_commitMeta->setObjectName("statusLine");
    m_commitMeta->setTextFormat(Qt::RichText);
    m_commitMeta->setTextInteractionFlags(Qt::TextSelectableByMouse);

    m_commitFilesSummary = new QLabel;
    m_commitFilesSummary->setObjectName("sectionLabel");
    m_commitFilesSummary->setTextFormat(Qt::RichText);

    // Left: changed-files list (click to scroll the diff to that file).
    auto *filesPane = new QWidget;
    filesPane->setMinimumWidth(200);
    filesPane->setMaximumWidth(300);
    m_commitFileList = new QListWidget;
    m_commitFileList->setObjectName("commitFileList");
    connect(m_commitFileList, &QListWidget::currentItemChanged, this,
            [this](QListWidgetItem *item, QListWidgetItem *) {
                if (item && m_commitDiffView)
                    m_commitDiffView->scrollToAnchor(
                        item->data(Qt::UserRole).toString());
            });
    auto *filesLayout = new QVBoxLayout(filesPane);
    filesLayout->setContentsMargins(0, 0, 8, 0);
    filesLayout->setSpacing(6);
    filesLayout->addWidget(m_commitFilesSummary);
    filesLayout->addWidget(m_commitFileList, 1);

    // Right: the unified diff for the whole commit.
    m_commitDiffView = new QTextBrowser;
    m_commitDiffView->setObjectName("commitDiffView");
    m_commitDiffView->setOpenExternalLinks(false);

    auto *split = new QSplitter(Qt::Horizontal);
    split->addWidget(filesPane);
    split->addWidget(m_commitDiffView);
    split->setStretchFactor(0, 0);
    split->setStretchFactor(1, 1);

    auto *detailLayout = new QVBoxLayout(detailPage);
    detailLayout->setContentsMargins(16, 12, 16, 16);
    detailLayout->setSpacing(8);
    detailLayout->addLayout(headerRow);
    detailLayout->addWidget(m_commitMessage);
    detailLayout->addWidget(m_commitMeta);
    detailLayout->addWidget(split, 1);

    // Right side: a placeholder until a commit is picked, then the diff view.
    // The commit list (listPage) stays visible in the left splitter pane the
    // whole time, so clicking a commit no longer hides it.
    auto *placeholder = new QLabel("Select a commit to view its diff.");
    placeholder->setObjectName("statusLine");
    placeholder->setAlignment(Qt::AlignCenter);
    m_commitsStack->addWidget(placeholder); // 0
    m_commitsStack->addWidget(detailPage);  // 1

    auto *outerSplit = new QSplitter(Qt::Horizontal);
    outerSplit->addWidget(listPage);
    outerSplit->addWidget(m_commitsStack);
    outerSplit->setStretchFactor(0, 0);
    outerSplit->setStretchFactor(1, 1);
    outerSplit->setSizes({360, 720});

    auto *page = new QWidget;
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(outerSplit);
    return page;
}

QWidget *MainWindow::buildInsightsTab()
{
    auto *page = new QWidget;
    page->setObjectName("mainContent");

    auto *scroll = new QScrollArea;
    scroll->setObjectName("mainContent");
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);

    auto *content = new QWidget;
    content->setObjectName("insightsPage");
    auto *layout = new QVBoxLayout(content);
    layout->setContentsMargins(18, 18, 18, 18);
    layout->setSpacing(14);

    auto *heading = new QLabel("Insights");
    heading->setObjectName("channelTitle");
    auto *subtitle = new QLabel(
        "Local repository and ForkMesh traffic metrics. No data leaves this node.");
    subtitle->setObjectName("statusLine");
    subtitle->setWordWrap(true);

    m_insightsRefreshButton = new QPushButton("Refresh");
    m_insightsRefreshButton->setObjectName("ghostButton");
    m_insightsRefreshButton->setProperty("buttonSize", "sm");
    m_insightsRefreshButton->setCursor(Qt::PointingHandCursor);
    setOcticon(m_insightsRefreshButton, "sync", 16);
    connect(m_insightsRefreshButton, &QPushButton::clicked, this,
            &MainWindow::loadRepoInsights);

    auto *headingCol = new QVBoxLayout;
    headingCol->setContentsMargins(0, 0, 0, 0);
    headingCol->setSpacing(3);
    headingCol->addWidget(heading);
    headingCol->addWidget(subtitle);

    auto *headerRow = new QHBoxLayout;
    headerRow->setContentsMargins(0, 0, 0, 0);
    headerRow->setSpacing(8);
    headerRow->addLayout(headingCol, 1);
    headerRow->addWidget(m_insightsRefreshButton, 0, Qt::AlignTop);
    layout->addLayout(headerRow);

    m_insightsSummary = new QLabel;
    m_insightsSummary->setObjectName("insightsCard");
    m_insightsSummary->setTextFormat(Qt::RichText);
    m_insightsSummary->setWordWrap(true);
    m_insightsSummary->setMinimumHeight(110);

    m_insightsTraffic = new QLabel;
    m_insightsTraffic->setObjectName("insightsCard");
    m_insightsTraffic->setTextFormat(Qt::RichText);
    m_insightsTraffic->setWordWrap(true);
    m_insightsTraffic->setMinimumHeight(110);

    auto *summaryRow = new QHBoxLayout;
    summaryRow->setContentsMargins(0, 0, 0, 0);
    summaryRow->setSpacing(10);
    summaryRow->addWidget(m_insightsSummary, 2);
    summaryRow->addWidget(m_insightsTraffic, 1);
    layout->addLayout(summaryRow);

    auto *languageLabel = new QLabel("LANGUAGES");
    languageLabel->setObjectName("sectionLabel");
    m_insightsLanguageBar = new QLabel;
    m_insightsLanguageBar->setObjectName("langBar");
    m_insightsLanguageBar->setFixedHeight(8);
    m_insightsLanguageBar->setTextFormat(Qt::RichText);
    m_insightsLanguageLegend = new QLabel;
    m_insightsLanguageLegend->setObjectName("statusLine");
    m_insightsLanguageLegend->setTextFormat(Qt::RichText);
    m_insightsLanguageLegend->setWordWrap(true);
    layout->addWidget(languageLabel);
    layout->addWidget(m_insightsLanguageBar);
    layout->addWidget(m_insightsLanguageLegend);

    auto configureTable = [](QTableWidget *table) {
        table->setObjectName("issueTable");
        table->verticalHeader()->setVisible(false);
        table->setSelectionBehavior(QAbstractItemView::SelectRows);
        table->setSelectionMode(QAbstractItemView::SingleSelection);
        table->setEditTriggers(QAbstractItemView::NoEditTriggers);
        table->setShowGrid(false);
        table->setWordWrap(false);
        table->setSortingEnabled(false);
        table->horizontalHeader()->setHighlightSections(false);
    };

    auto *contributorsLabel = new QLabel("CONTRIBUTORS");
    contributorsLabel->setObjectName("sectionLabel");
    m_insightsContributors = new QTableWidget(0, 3);
    m_insightsContributors->setHorizontalHeaderLabels({"Contributor", "Commits", "Share"});
    configureTable(m_insightsContributors);
    m_insightsContributors->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_insightsContributors->horizontalHeader()->setSectionResizeMode(
        1, QHeaderView::ResizeToContents);
    m_insightsContributors->horizontalHeader()->setSectionResizeMode(
        2, QHeaderView::ResizeToContents);
    m_insightsContributors->setMinimumHeight(220);

    auto *recentLabel = new QLabel("RECENT ACTIVITY");
    recentLabel->setObjectName("sectionLabel");
    m_insightsRecentCommits = new QTableWidget(0, 4);
    m_insightsRecentCommits->setHorizontalHeaderLabels(
        {"Commit", "Author", "When", "Message"});
    configureTable(m_insightsRecentCommits);
    m_insightsRecentCommits->horizontalHeader()->setSectionResizeMode(
        0, QHeaderView::ResizeToContents);
    m_insightsRecentCommits->horizontalHeader()->setSectionResizeMode(
        1, QHeaderView::ResizeToContents);
    m_insightsRecentCommits->horizontalHeader()->setSectionResizeMode(
        2, QHeaderView::ResizeToContents);
    m_insightsRecentCommits->horizontalHeader()->setSectionResizeMode(
        3, QHeaderView::Stretch);
    m_insightsRecentCommits->setMinimumHeight(240);

    layout->addWidget(contributorsLabel);
    layout->addWidget(m_insightsContributors);
    layout->addWidget(recentLabel);
    layout->addWidget(m_insightsRecentCommits);

    m_insightsActivity = new QLabel;
    m_insightsActivity->setObjectName("statusLine");
    m_insightsActivity->setTextFormat(Qt::RichText);
    m_insightsActivity->setWordWrap(true);
    layout->addWidget(m_insightsActivity);
    layout->addStretch();

    scroll->setWidget(content);
    auto *pageLayout = new QVBoxLayout(page);
    pageLayout->setContentsMargins(0, 0, 0, 0);
    pageLayout->addWidget(scroll);
    return page;
}

QWidget *MainWindow::buildPlaceholderTab(const QString &name)
{
    auto *page = new QWidget;
    auto *label = new QLabel(
        QStringLiteral("<b>%1</b><br><span style='color:#8b949e'>Not available in "
                       "ForkMesh yet.</span>")
            .arg(name));
    label->setObjectName("placeholderPanel");
    label->setAlignment(Qt::AlignCenter);
    label->setTextFormat(Qt::RichText);
    auto *layout = new QVBoxLayout(page);
    layout->addStretch();
    layout->addWidget(label, 0, Qt::AlignCenter);
    layout->addStretch();
    return page;
}

// ---- Pull requests ---------------------------------------------------------

QWidget *MainWindow::buildPullsTab()
{
    auto *page = new QWidget;

    // Left: toolbar + sortable PR table.
    auto *listPane = new QWidget;
    listPane->setMinimumWidth(360);
    auto *heading = new QLabel("Pull requests");
    heading->setObjectName("channelTitle");
    m_pullNewButton = new QPushButton("New pull request");
    m_pullChooseDirButton = new QPushButton("Choose directory");
    m_pullImportButton = new QPushButton("Import patch");
    m_pullSyncButton = new QPushButton("Sync inbox");
    for (QPushButton *b : {m_pullNewButton, m_pullChooseDirButton, m_pullImportButton,
                           m_pullSyncButton}) {
        b->setObjectName("ghostButton");
        b->setProperty("buttonSize", "sm");
        b->setCursor(Qt::PointingHandCursor);
    }
    setOcticon(m_pullNewButton, "plus", 16);
    setOcticon(m_pullChooseDirButton, "file-directory", 16);
    setOcticon(m_pullImportButton, "download", 16);
    setOcticon(m_pullSyncButton, "sync", 16);
    m_pullChooseDirButton->setToolTip("Create a pull request from another local checkout of this repository");
    m_pullImportButton->setToolTip("Open a .patch/.diff file (e.g. a downloaded commit) as a pull request");
    m_pullSyncButton->setToolTip("Pull PR submissions filed by other nodes and merge them");
    connect(m_pullImportButton, &QPushButton::clicked, this,
            &MainWindow::importPatchAsPull);
    auto *toolbar = new QHBoxLayout;
    toolbar->setContentsMargins(0, 0, 0, 0);
    toolbar->addWidget(m_pullNewButton);
    toolbar->addWidget(m_pullChooseDirButton);
    toolbar->addWidget(m_pullImportButton);
    toolbar->addWidget(m_pullSyncButton);
    toolbar->addStretch();

    m_pullSearch = new QLineEdit;
    m_pullSearch->setObjectName("issueSearch");
    m_pullSearch->setPlaceholderText("Search pull requests\xE2\x80\xA6");
    m_pullSearch->setClearButtonEnabled(true);

    m_pullTable = new QTableWidget(0, 6);
    m_pullTable->setObjectName("issueTable");
    m_pullTable->setHorizontalHeaderLabels(
        {"#", "Title", "Base \xE2\x86\x90 Head", "Status", "Files", "\xC2\xB1"});
    m_pullTable->verticalHeader()->setVisible(false);
    m_pullTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_pullTable->setSelectionMode(QAbstractItemView::SingleSelection);
    m_pullTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_pullTable->setShowGrid(false);
    m_pullTable->setWordWrap(false);
    m_pullTable->setSortingEnabled(true);
    QHeaderView *ph = m_pullTable->horizontalHeader();
    ph->setHighlightSections(false);
    ph->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    ph->setSectionResizeMode(1, QHeaderView::Stretch);
    for (int c = 2; c < 6; ++c)
        ph->setSectionResizeMode(c, QHeaderView::ResizeToContents);

    auto *listLayout = new QVBoxLayout(listPane);
    listLayout->setContentsMargins(18, 18, 12, 18);
    listLayout->setSpacing(8);
    listLayout->addWidget(heading);
    listLayout->addLayout(toolbar);
    listLayout->addWidget(m_pullSearch);
    listLayout->addWidget(m_pullTable, 1);

    // Right: PR detail — header + changed-files explorer + diff viewer.
    m_pullDetail = new QWidget;
    m_pullTitle = new QLabel("Select a pull request");
    m_pullTitle->setObjectName("channelTitle");
    m_pullTitle->setWordWrap(true);
    m_pullUpdateButton = new QPushButton("Update branch");
    m_pullMergeButton = new QPushButton("Merge");
    m_pullPushMainCheck = new QCheckBox("Push to main");
    m_pullCloseButton = new QPushButton("Close");
    m_pullDeleteButton = new QPushButton("Delete");
    for (QPushButton *b : {m_pullUpdateButton, m_pullMergeButton, m_pullCloseButton, m_pullDeleteButton}) {
        b->setObjectName("ghostButton");
        b->setProperty("buttonSize", "sm");
        b->setCursor(Qt::PointingHandCursor);
    }
    m_pullMergeButton->setObjectName("primaryButton");
    setOcticon(m_pullUpdateButton, "sync", 16);
    setOcticon(m_pullMergeButton, "check-circle", 16);
    setOcticon(m_pullCloseButton, "circle-slash", 16);
    setOcticon(m_pullDeleteButton, "trash", 16);
    m_pullDeleteButton->setToolTip("Permanently delete this pull request");
    m_pullUpdateButton->setToolTip("Merge the base branch into this pull request branch");
    m_pullPushMainCheck->setToolTip(
        "After merging, push the merged commit to the pull request's base branch in this repo's mirror.");
    auto *pullHeaderRow = new QHBoxLayout;
    pullHeaderRow->setContentsMargins(0, 0, 0, 0);
    pullHeaderRow->addWidget(m_pullTitle, 1);
    pullHeaderRow->addWidget(m_pullUpdateButton, 0, Qt::AlignTop);
    pullHeaderRow->addWidget(m_pullMergeButton, 0, Qt::AlignTop);
    pullHeaderRow->addWidget(m_pullPushMainCheck, 0, Qt::AlignTop);
    pullHeaderRow->addWidget(m_pullCloseButton, 0, Qt::AlignTop);
    pullHeaderRow->addWidget(m_pullDeleteButton, 0, Qt::AlignTop);
    m_pullMeta = new QLabel;
    m_pullMeta->setObjectName("statusLine");
    m_pullMeta->setTextFormat(Qt::RichText);
    m_pullMeta->setWordWrap(true);
    m_pullDesc = new QLabel;
    m_pullDesc->setObjectName("statusLine");
    m_pullDesc->setWordWrap(true);

    m_pullFiles = new QListWidget;
    m_pullFiles->setObjectName("overviewList");
    m_pullFiles->setMinimumWidth(200);
    connect(m_pullFiles, &QListWidget::currentItemChanged, this,
            [this](QListWidgetItem *item, QListWidgetItem *) {
                if (item)
                    renderPullDiff(item->data(Qt::UserRole).toString());
            });
    m_pullDiff = new QTextEdit;
    m_pullDiff->setObjectName("diffView");
    m_pullDiff->setReadOnly(true);
    m_pullDiff->setLineWrapMode(QTextEdit::NoWrap);

    auto *diffSplit = new QSplitter(Qt::Horizontal);
    diffSplit->setChildrenCollapsible(false);
    diffSplit->addWidget(m_pullFiles);
    diffSplit->addWidget(m_pullDiff);
    diffSplit->setStretchFactor(0, 0);
    diffSplit->setStretchFactor(1, 1);
    diffSplit->setSizes({240, 600});

    auto *detailLayout = new QVBoxLayout(m_pullDetail);
    detailLayout->setContentsMargins(18, 18, 18, 18);
    detailLayout->setSpacing(8);
    detailLayout->addLayout(pullHeaderRow);
    detailLayout->addWidget(m_pullMeta);
    detailLayout->addWidget(m_pullDesc);
    detailLayout->addWidget(diffSplit, 1);

    auto *splitter = new QSplitter(Qt::Horizontal);
    splitter->setChildrenCollapsible(false);
    splitter->addWidget(listPane);
    splitter->addWidget(m_pullDetail);
    splitter->setStretchFactor(0, 0);
    splitter->setStretchFactor(1, 1);
    splitter->setSizes({460, 620});

    auto *layout = new QHBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(splitter);

    connect(m_pullSearch, &QLineEdit::textChanged, this,
            [this] { refreshPullList(); });
    connect(m_pullTable, &QTableWidget::itemSelectionChanged, this, [this] {
        const QModelIndexList rows = m_pullTable->selectionModel()->selectedRows();
        if (rows.isEmpty())
            return;
        if (QTableWidgetItem *first = m_pullTable->item(rows.first().row(), 0))
            showPull(first->data(Qt::UserRole).toInt());
    });
    connect(m_pullNewButton, &QPushButton::clicked, this, &MainWindow::promptNewPull);
    connect(m_pullChooseDirButton, &QPushButton::clicked,
            this, &MainWindow::promptNewPullFromDirectory);
    connect(m_pullSyncButton, &QPushButton::clicked, this, &MainWindow::syncPullsInbox);
    connect(m_pullUpdateButton, &QPushButton::clicked,
            this, &MainWindow::updateCurrentPullBranch);
    connect(m_pullMergeButton, &QPushButton::clicked, this, &MainWindow::mergeCurrentPull);
    connect(m_pullCloseButton, &QPushButton::clicked, this, &MainWindow::closeCurrentPull);
    connect(m_pullDeleteButton, &QPushButton::clicked, this, &MainWindow::deleteCurrentPull);
    return page;
}

PullStore MainWindow::pullStoreForCurrentRepo() const
{
    if (m_repoDetailIndex < 0 || m_repoDetailIndex >= m_repositories.size())
        return PullStore(QString(), QString(), &m_profileIdentity, m_userName);
    const RepositoryRecord &repo = m_repositories.at(m_repoDetailIndex);
    return PullStore(repo.localPath, repo.mirrorPath, &m_profileIdentity, m_userName);
}

void MainWindow::reloadPulls()
{
    if (!m_pullTable)
        return;
    m_currentPulls = pullStoreForCurrentRepo().loadAll();
    updateRepoPullCount();
    refreshPullList();
    updatePullActionState();
}

void MainWindow::refreshPullList()
{
    if (!m_pullTable)
        return;
    const QString search = m_pullSearch ? m_pullSearch->text().trimmed() : QString();
    const int keep = m_currentPullNumber;
    m_pullTable->setSortingEnabled(false);
    m_pullTable->setRowCount(0);
    for (const PullRequest &pr : std::as_const(m_currentPulls)) {
        if (!search.isEmpty()) {
            const QString hay = QStringLiteral("#%1 %2 %3 %4 %5")
                                    .arg(pr.number)
                                    .arg(pr.title, pr.base, pr.head, pr.authorName);
            if (!hay.contains(search, Qt::CaseInsensitive))
                continue;
        }
        const int row = m_pullTable->rowCount();
        m_pullTable->insertRow(row);
        auto *num = new QTableWidgetItem;
        num->setData(Qt::DisplayRole, pr.number);
        num->setData(Qt::UserRole, pr.number);
        m_pullTable->setItem(row, 0, num);
        m_pullTable->setItem(row, 1, new QTableWidgetItem(pr.title));
        m_pullTable->setItem(row, 2,
                             new QTableWidgetItem(pr.base + QString::fromUtf8(" \xE2\x86\x90 ") +
                                                  pr.head));
        auto *st = new QTableWidgetItem(pr.status);
        st->setForeground(QColor(pr.status == "merged"  ? "#a371f7"
                                 : pr.status == "closed" ? "#f85149"
                                                         : "#3fb950"));
        m_pullTable->setItem(row, 3, st);
        auto *files = new QTableWidgetItem;
        files->setData(Qt::DisplayRole, pr.filesChanged);
        m_pullTable->setItem(row, 4, files);
        m_pullTable->setItem(row, 5,
                             new QTableWidgetItem(QStringLiteral("+%1 -%2")
                                                      .arg(pr.additions)
                                                      .arg(pr.deletions)));
    }
    m_pullTable->setSortingEnabled(true);
    int selRow = -1;
    for (int r = 0; r < m_pullTable->rowCount(); ++r)
        if (m_pullTable->item(r, 0)->data(Qt::UserRole).toInt() == keep) {
            selRow = r;
            break;
        }
    if (selRow < 0 && m_pullTable->rowCount() > 0)
        selRow = 0;
    if (selRow >= 0)
        m_pullTable->selectRow(selRow);
    else {
        m_currentPullNumber = -1;
        showPull(-1);
    }
}

void MainWindow::showPull(int number)
{
    const PullRequest *found = nullptr;
    for (const PullRequest &pr : m_currentPulls)
        if (pr.number == number)
            found = &pr;
    m_currentPullNumber = found ? number : -1;
    m_pullFiles->clear();
    m_pullFileDiffs.clear();

    if (!found) {
        m_pullTitle->setText("Select a pull request");
        m_pullMeta->clear();
        m_pullDesc->clear();
        m_pullDiff->clear();
        if (m_pullPushMainCheck)
            m_pullPushMainCheck->setText("Push to main");
        updatePullActionState();
        return;
    }
    if (m_pullPushMainCheck)
        m_pullPushMainCheck->setText(
            "Push to " + (found->base.isEmpty() ? QStringLiteral("main") : found->base));
    m_pullTitle->setText(QStringLiteral("#%1  %2").arg(found->number).arg(found->title));
    m_pullMeta->setText(
        QString::fromUtf8("<b>%1</b> \xE2\x86\x90 <b>%2</b> \xC2\xB7 %3 \xC2\xB7 %4 files "
                       "<span style='color:#3fb950'>+%5</span> "
                       "<span style='color:#f85149'>-%6</span> \xC2\xB7 by %7")
            .arg(found->base.toHtmlEscaped(), found->head.toHtmlEscaped(),
                 found->status)
            .arg(found->filesChanged)
            .arg(found->additions)
            .arg(found->deletions)
            .arg((found->authorName.isEmpty() ? found->author.left(10)
                                              : found->authorName)
                     .toHtmlEscaped()));
    m_pullDesc->setText(found->description.toHtmlEscaped());

    // Split the unified diff into per-file sections.
    QString currentFile;
    QStringList currentLines;
    const auto flush = [&] {
        if (!currentFile.isEmpty())
            m_pullFileDiffs.insert(currentFile, currentLines.join('\n'));
        currentLines.clear();
    };
    for (const QString &line : found->patch.split('\n')) {
        if (line.startsWith("diff --git ")) {
            flush();
            // "diff --git a/<path> b/<path>"
            currentFile = line.section(" b/", 1);
        }
        if (!currentFile.isEmpty())
            currentLines << line;
    }
    flush();

    for (auto it = m_pullFileDiffs.constBegin(); it != m_pullFileDiffs.constEnd(); ++it) {
        const QString name = it.key().section('/', -1);
        auto *item = new QListWidgetItem(iconForFile(name), it.key());
        item->setData(Qt::UserRole, it.key());
        m_pullFiles->addItem(item);
    }
    m_pullFiles->sortItems();
    if (m_pullFiles->count() > 0)
        m_pullFiles->setCurrentRow(0);
    else
        m_pullDiff->setPlainText("(no changes)");
    updatePullActionState();
}

void MainWindow::switchToPullTab(int pullNumber)
{
    if (m_repoDetailTabs && m_repoDetailTabs->button(4))
        m_repoDetailTabs->button(4)->setChecked(true);
    if (m_repoDetailStack)
        m_repoDetailStack->setCurrentIndex(4);
    m_currentPullNumber = pullNumber;
    reloadPulls();
    if (!m_pullTable)
        return;
    for (int row = 0; row < m_pullTable->rowCount(); ++row) {
        QTableWidgetItem *number = m_pullTable->item(row, 0);
        if (number && number->data(Qt::UserRole).toInt() == pullNumber) {
            m_pullTable->selectRow(row);
            showPull(pullNumber);
            return;
        }
    }
    showPull(pullNumber);
}

void MainWindow::renderPullDiff(const QString &filePath)
{
    const QString diff = m_pullFileDiffs.value(filePath);
    QString html =
        "<pre style='font-family:monospace; font-size:12px; margin:0; white-space:pre'>";
    for (const QString &line : diff.split('\n')) {
        QString color;
        if (line.startsWith("@@"))
            color = "#58a6ff";
        else if (line.startsWith("+++") || line.startsWith("---") ||
                 line.startsWith("diff ") || line.startsWith("index "))
            color = "#8b949e";
        else if (line.startsWith('+'))
            color = "#3fb950";
        else if (line.startsWith('-'))
            color = "#f85149";
        const QString escaped = line.toHtmlEscaped();
        if (color.isEmpty())
            html += escaped + "\n";
        else
            html += "<span style='color:" + color + "'>" + escaped + "</span>\n";
    }
    html += "</pre>";
    m_pullDiff->setHtml(html);
}

void MainWindow::updatePullActionState()
{
    const PullStore store = pullStoreForCurrentRepo();
    const bool writable = store.canWrite();
    const bool have = m_currentPullNumber >= 0;
    bool open = false;
    for (const PullRequest &pr : m_currentPulls)
        if (pr.number == m_currentPullNumber)
            open = pr.status == "open";
    bool behind = false;
    if (writable && have && open)
        store.isBranchBehindBase(m_currentPullNumber, &behind);
    if (m_pullNewButton)
        m_pullNewButton->setEnabled(m_repoDetailIndex >= 0);
    if (m_pullChooseDirButton)
        m_pullChooseDirButton->setEnabled(m_repoDetailIndex >= 0);
    if (m_pullImportButton)
        m_pullImportButton->setEnabled(writable);
    if (m_pullSyncButton)
        m_pullSyncButton->setEnabled(writable);
    if (m_pullUpdateButton) {
        m_pullUpdateButton->setVisible(writable && have && open && behind);
        m_pullUpdateButton->setEnabled(writable && have && open && behind);
    }
    if (m_pullMergeButton)
        m_pullMergeButton->setEnabled(writable && have && open);
    if (m_pullPushMainCheck)
        m_pullPushMainCheck->setEnabled(writable && have && open);
    if (m_pullCloseButton)
        m_pullCloseButton->setEnabled(writable && have && open);
    if (m_pullDeleteButton)
        m_pullDeleteButton->setEnabled(writable && have);
}

void MainWindow::promptNewPull()
{
    promptNewPullFromSource(QString());
}

void MainWindow::importPatchAsPull()
{
    if (m_repoDetailIndex < 0 || m_repoDetailIndex >= m_repositories.size())
        return;
    const QString dir = repoGitDir();
    PullStore store = pullStoreForCurrentRepo();
    if (dir.isEmpty() || !store.canWrite()) {
        setRepoDetailNotice(
            "Importing a patch needs a writable local checkout of this repo.", true);
        return;
    }
    const QString path = QFileDialog::getOpenFileName(
        this, "Import patch as pull request", QDir::homePath(),
        "Patch files (*.patch *.diff);;All files (*)");
    if (path.isEmpty())
        return;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        setRepoDetailNotice("Could not read the patch file.", true);
        return;
    }
    const QByteArray raw = file.readAll();
    file.close();
    const QString patch = QString::fromUtf8(raw);
    if (patch.trimmed().isEmpty()) {
        setRepoDetailNotice("That patch file is empty.", true);
        return;
    }

    // Derive a title: prefer the format-patch "Subject:" line (minus the
    // [PATCH] prefix), else fall back to the file name.
    QString title;
    for (const QString &line : patch.split(QLatin1Char('\n'))) {
        if (line.startsWith(QLatin1String("Subject:"))) {
            title = line.mid(8).trimmed();
            title.remove(QRegularExpression(QStringLiteral("^\\[PATCH[^\\]]*\\]\\s*")));
            break;
        }
        if (line.startsWith(QLatin1String("diff --git ")))
            break; // reached the diff with no Subject
    }
    if (title.isEmpty())
        title = QFileInfo(path).completeBaseName();

    const QStringList branches = repoBranches();
    const QString base = repoDefaultBranch(branches);

    // Sanity-check that the patch applies to the base before opening the PR.
    QString applyErr;
    QProcess check;
    check.setProgram("git");
    check.setArguments({"-C", dir, "apply", "--check", "--3way", path});
    check.start();
    check.waitForFinished(8000);
    if (check.exitStatus() != QProcess::NormalExit || check.exitCode() != 0) {
        const QString detail =
            QString::fromUtf8(check.readAllStandardError()).trimmed();
        if (QMessageBox::warning(
                this, "Import patch",
                QStringLiteral("This patch does not apply cleanly onto %1:\n\n%2\n\n"
                               "Open the pull request anyway?")
                    .arg(base, detail.isEmpty() ? "(no details)" : detail),
                QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes)
            return;
    }

    // Synthesize a head label from the title; the patch itself carries the change.
    QString head = QStringLiteral("imported/") +
                   title.toLower().replace(QRegularExpression(QStringLiteral("[^a-z0-9]+")),
                                           QStringLiteral("-"));
    head = head.left(60);
    if (head.endsWith(QLatin1Char('-')))
        head.chop(1);

    QString error;
    const int number = store.createPull(
        title, QStringLiteral("Imported from patch file `%1`.").arg(QFileInfo(path).fileName()),
        base, head, patch, &error);
    if (number < 0) {
        setRepoDetailNotice(error.isEmpty() ? "Could not create the pull request."
                                            : error,
                            true);
        return;
    }
    logSystem(QStringLiteral("Imported patch %1 as pull #%2.").arg(path).arg(number));
    setRepoDetailNotice(QStringLiteral("Imported patch as pull #%1.").arg(number));
    m_currentPullNumber = number;
    switchToPullTab(number);
}

void MainWindow::promptNewPullFromDirectory()
{
    if (m_repoDetailIndex < 0 || m_repoDetailIndex >= m_repositories.size())
        return;
    RepositoryRecord &currentRepo = m_repositories[m_repoDetailIndex];
    const QString startDir = currentRepo.localPath.isEmpty()
                                 ? QDir::homePath()
                                 : currentRepo.localPath;
    const QString chosen = QFileDialog::getExistingDirectory(
        this, "Choose a Git repository for this pull request", startDir);
    if (chosen.isEmpty())
        return;
    promptNewPullFromSource(chosen);
}

void MainWindow::promptNewPullFromSource(const QString &sourceDir,
                                         const QString &preferredBase,
                                         const QString &preferredHead)
{
    if (m_repoDetailIndex < 0 || m_repoDetailIndex >= m_repositories.size())
        return;
    RepositoryRecord &currentRepo = m_repositories[m_repoDetailIndex];
    const QString dir = sourceDir.trimmed().isEmpty() ? repoGitDir() : sourceDir.trimmed();
    if (dir.isEmpty()) {
        QMessageBox::warning(this, "New pull request",
                             "No local copy of this repository to diff.");
        return;
    }
    if (!sourceDir.trimmed().isEmpty()) {
        const bool looksLikeGit =
            QDir(dir).exists(".git") || QDir(dir).exists("HEAD");
        if (!looksLikeGit) {
            QMessageBox::warning(
                this, "New pull request",
                "That folder is not a Git repository. Choose a folder created by "
                "\"git init\" or \"git clone\".");
            return;
        }
        QString selectedName = repoNameFromUrl(dir);
        QByteArray origin;
        if (runGitCapture(dir, {"config", "--get", "remote.origin.url"}, &origin, nullptr) &&
            !origin.trimmed().isEmpty())
            selectedName = repoNameFromUrl(QString::fromUtf8(origin).trimmed());
        const QString currentName =
            repoSegment(currentRepo.name, QStringLiteral("repository"));
        if (selectedName != currentName) {
            QMessageBox::warning(
                this, "New pull request",
                QStringLiteral("That directory appears to be %1, but this page is for %2.")
                    .arg(selectedName, currentName));
            return;
        }
        if (currentRepo.localPath.isEmpty() || !QDir(currentRepo.localPath).exists()) {
            currentRepo.localPath = dir;
            saveRepositories();
            refreshRepositoryList();
        }
    }
    // Enumerate branches for the base/head pickers.
    QByteArray out;
    QStringList branches;
    if (runGitCapture(dir, {"branch", "--format=%(refname:short)"}, &out, nullptr))
        for (const QString &b : QString::fromUtf8(out).split('\n', Qt::SkipEmptyParts))
            branches << b.trimmed();
    if (branches.size() < 1) {
        QMessageBox::warning(this, "New pull request", "This repository has no branches.");
        return;
    }

    QDialog dialog(this);
    dialog.setWindowTitle("New pull request");
    auto *targetCombo = new QComboBox(&dialog);
    targetCombo->setEditable(true);
    targetCombo->addItem(currentRepo.owner);
    QSet<QString> targetOwners{currentRepo.owner};
    for (const RepositoryRecord &repo : std::as_const(m_repositories)) {
        if (repo.name == currentRepo.name && !targetOwners.contains(repo.owner)) {
            targetCombo->addItem(repo.owner);
            targetOwners.insert(repo.owner);
        }
    }
    targetCombo->setToolTip("Destination node that will receive this pull request");
    auto *baseCombo = new QComboBox(&dialog);
    auto *headCombo = new QComboBox(&dialog);
    baseCombo->addItems(branches);
    headCombo->addItems(branches);
    const int preferredBaseIndex = baseCombo->findText(preferredBase);
    if (preferredBaseIndex >= 0)
        baseCombo->setCurrentIndex(preferredBaseIndex);
    const int preferredHeadIndex = headCombo->findText(preferredHead);
    if (preferredHeadIndex >= 0) {
        headCombo->setCurrentIndex(preferredHeadIndex);
    } else {
        QByteArray currentBranchOut;
        if (runGitCapture(dir, {"rev-parse", "--abbrev-ref", "HEAD"},
                          &currentBranchOut, nullptr)) {
            const int currentIndex = headCombo->findText(
                QString::fromUtf8(currentBranchOut).trimmed());
            if (currentIndex >= 0)
                headCombo->setCurrentIndex(currentIndex);
            else if (branches.size() > 1)
                headCombo->setCurrentIndex(1);
        } else if (branches.size() > 1) {
            headCombo->setCurrentIndex(1);
        }
    }
    auto *titleEdit = new QLineEdit(&dialog);
    titleEdit->setPlaceholderText("Title");
    if (!preferredHead.isEmpty()) {
        QByteArray subject;
        if (runGitCapture(dir, {"log", "-1", "--format=%s", preferredHead},
                          &subject, nullptr))
            titleEdit->setText(QString::fromUtf8(subject).trimmed());
    }
    auto *bodyEdit = new QPlainTextEdit(&dialog);
    bodyEdit->setPlaceholderText("Describe the change\xE2\x80\xA6");
    auto *form = new QFormLayout;
    form->addRow("Target node", targetCombo);
    form->addRow("Base", baseCombo);
    form->addRow("Head", headCombo);
    form->addRow("Title", titleEdit);
    form->addRow("Description", bodyEdit);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel,
                                         &dialog);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    auto *dl = new QVBoxLayout(&dialog);
    dl->addLayout(form);
    dl->addWidget(buttons);
    dialog.resize(520, 420);
    if (dialog.exec() != QDialog::Accepted)
        return;

    const QString base = baseCombo->currentText();
    const QString head = headCombo->currentText();
    const QString targetText = targetCombo->currentText().trimmed();
    const QString targetOwner = repoSegment(targetText, QString());
    const QString title = titleEdit->text().trimmed();
    if (targetOwner.isEmpty()) {
        QMessageBox::warning(this, "New pull request", "A target node is required.");
        return;
    }
    if (title.isEmpty()) {
        QMessageBox::warning(this, "New pull request", "A title is required.");
        return;
    }
    if (base == head) {
        QMessageBox::warning(this, "New pull request", "Base and head must differ.");
        return;
    }
    QByteArray diff;
    QString diffError;
    const bool haveDiff = sourceDir.trimmed().isEmpty()
                              ? runGitCapture(dir, {"diff", "--binary", base + ".." + head},
                                              &diff, &diffError)
                              : buildWorkingTreeDiff(dir, base, &diff, &diffError);
    if (!haveDiff || diff.trimmed().isEmpty()) {
        QMessageBox::warning(this, "New pull request",
                             diffError.isEmpty()
                                 ? QStringLiteral("No differences between %1 and %2.")
                                       .arg(base, head)
                                 : QStringLiteral("Could not compare files: %1")
                                       .arg(diffError));
        return;
    }
    PullRequest pr;
    pr.title = title;
    pr.description = bodyEdit->toPlainText();
    pr.base = base;
    pr.head = head;
    pr.patch = QString::fromUtf8(diff);

    PullStore store = pullStoreForCurrentRepo();
    if (store.canWrite() && targetOwner == currentRepo.owner) {
        QString error;
        const int number = store.createPull(pr.title, pr.description, pr.base, pr.head,
                                             pr.patch, &error);
        if (number < 0) {
            QMessageBox::warning(this, "New pull request", error);
            return;
        }
        m_currentPullNumber = number;
        switchToPullTab(number);
    } else {
        RepositoryRecord targetRepo = currentRepo;
        targetRepo.owner = targetOwner;
        submitPullToInbox(store.makeSignedPull(pr), targetRepo);
    }
}

void MainWindow::updateCurrentPullBranch()
{
    if (m_currentPullNumber < 0)
        return;
    if (QMessageBox::question(this, "Update branch",
                              QStringLiteral("Merge the base branch into pull request #%1?")
                                  .arg(m_currentPullNumber)) != QMessageBox::Yes)
        return;
    PullStore store = pullStoreForCurrentRepo();
    QString error;
    if (!store.updateBranchFromBase(m_currentPullNumber, &error)) {
        QMessageBox::warning(this, "Update branch", error);
        return;
    }
    logSystem(QStringLiteral("Updated pull request #%1 from its base branch.")
                  .arg(m_currentPullNumber));
    reloadPulls();
}

void MainWindow::mergeCurrentPull()
{
    if (m_currentPullNumber < 0)
        return;
    PullRequest current;
    bool found = false;
    for (const PullRequest &pr : std::as_const(m_currentPulls)) {
        if (pr.number == m_currentPullNumber) {
            current = pr;
            found = true;
            break;
        }
    }
    if (!found)
        return;
    const bool pushAfterMerge =
        m_pullPushMainCheck && m_pullPushMainCheck->isChecked();
    const QString prompt =
        pushAfterMerge
            ? QStringLiteral("Apply and merge pull request #%1, then push to %2?")
                  .arg(m_currentPullNumber)
                  .arg(current.base.isEmpty() ? QStringLiteral("main") : current.base)
            : QStringLiteral("Apply and merge pull request #%1?")
                  .arg(m_currentPullNumber);
    if (QMessageBox::question(this, "Merge pull request",
                              prompt) != QMessageBox::Yes)
        return;
    PullStore store = pullStoreForCurrentRepo();
    QString error;
    if (!store.mergePull(m_currentPullNumber, &error)) {
        QMessageBox::warning(this, "Merge pull request", error);
        return;
    }
    logSystem(QStringLiteral("Merged pull request #%1.").arg(m_currentPullNumber));
    closeIssuesLinkedFromPull(current);
    if (pushAfterMerge && !pushCurrentPullToMirror(current, &error)) {
        QMessageBox::warning(this, "Push merged pull request", error);
        reloadPulls();
        return;
    }
    reloadPulls();
}

void MainWindow::closeIssuesLinkedFromPull(const PullRequest &pr)
{
    IssueStore store = issueStoreForCurrentRepo();
    if (!store.canWrite())
        return;

    static const QRegularExpression issueRefRe(
        QStringLiteral("\\bissue[-\\s]+#?(\\d+)\\b|"
                       "\\b(?:close[sd]?|fix(?:e[sd])?|resolve[sd]?)\\b\\s*:?\\s*#(\\d+)"),
        QRegularExpression::CaseInsensitiveOption);

    const QString haystack =
        QStringList{pr.title, pr.description, pr.head, pr.base}.join('\n');
    QSet<int> linked;
    auto it = issueRefRe.globalMatch(haystack);
    while (it.hasNext()) {
        const QRegularExpressionMatch match = it.next();
        const int number =
            (match.captured(1).isEmpty() ? match.captured(2) : match.captured(1)).toInt();
        if (number > 0)
            linked.insert(number);
    }
    if (linked.isEmpty())
        return;

    QHash<int, Issue> byNumber;
    for (const Issue &issue : store.loadAll())
        byNumber.insert(issue.number, issue);

    int closedCount = 0;
    QString lastClosed;
    for (const int number : std::as_const(linked)) {
        const Issue issue = byNumber.value(number);
        if (issue.number <= 0 || issue.status == QLatin1String("closed"))
            continue;

        QString err;
        const QString note =
            QStringLiteral("Closed by merged pull request #%1.").arg(pr.number);
        if (!store.addComment(number, note, {}, &err)) {
            logSystem(QStringLiteral("Issue #%1: could not link pull request #%2: %3")
                          .arg(number)
                          .arg(pr.number)
                          .arg(err));
            continue;
        }
        if (!store.setStatus(number, QStringLiteral("closed"), &err)) {
            logSystem(QStringLiteral("Issue #%1: could not close after pull request #%2: %3")
                          .arg(number)
                          .arg(pr.number)
                          .arg(err));
            continue;
        }
        logSystem(QStringLiteral("Closed issue #%1 via pull request #%2.")
                      .arg(number)
                      .arg(pr.number));
        ++closedCount;
        lastClosed = QStringLiteral("#%1").arg(number);
    }

    if (closedCount > 0) {
        reloadIssues();
        updateRepoIssueCount();
        flashMessage(closedCount == 1
                         ? QStringLiteral("Closed issue %1 from merged pull request.")
                               .arg(lastClosed)
                         : QStringLiteral("Closed %1 issues from merged pull request.")
                               .arg(closedCount));
    }
}

bool MainWindow::pushCurrentPullToMirror(const PullRequest &pr, QString *error)
{
    if (m_repoDetailIndex < 0 || m_repoDetailIndex >= m_repositories.size()) {
        if (error)
            *error = QStringLiteral("No repository is selected.");
        return false;
    }
    RepositoryRecord &repo = m_repositories[m_repoDetailIndex];
    if (repo.localPath.isEmpty() || !QDir(repo.localPath).exists(".git")) {
        if (error)
            *error = QStringLiteral("Pushing needs a local checkout.");
        return false;
    }
    if (repo.mirrorPath.isEmpty() || !QDir(repo.mirrorPath).exists()) {
        if (error)
            *error = QStringLiteral("Sync this repository first to create its mirror.");
        return false;
    }

    // Imported agent PRs may store the base *commit* in `base`, while native
    // PRs store a branch name. Never create refs/heads/<commit> or make that the
    // bare mirror's HEAD: it leaves ordinary clones looking like an empty repo.
    QString branch = pr.base.trimmed();
    if (branch.isEmpty() ||
        !runGitCapture(repo.localPath,
                       {"show-ref", "--verify", "--quiet",
                        "refs/heads/" + branch},
                       nullptr, nullptr)) {
        QByteArray current;
        if (runGitCapture(repo.localPath,
                          {"symbolic-ref", "--short", "HEAD"},
                          &current, nullptr))
            branch = QString::fromUtf8(current).trimmed();
    }
    if (branch.isEmpty())
        branch = QStringLiteral("main");
    ensurePushHook(repo);
    QString gitError;
    if (!runGitCapture(repo.localPath,
                       {"push", repo.mirrorPath,
                        "HEAD:refs/heads/" + branch},
                       nullptr, &gitError)) {
        if (error)
            *error = QStringLiteral("Could not push to %1: %2")
                         .arg(branch, gitError.left(500));
        return false;
    }

    runGitCapture(repo.mirrorPath,
                  {"symbolic-ref", "HEAD", "refs/heads/" + branch},
                  nullptr, nullptr);
    repo.lastSyncMs = QDateTime::currentMSecsSinceEpoch();
    saveRepositories();
    ensurePushHook(repo);
    refreshRepositoryList();
    refreshOpenRepoDetail();
    if (m_backend)
        m_backend->notifyMirrorUpdated(
            catalogOwner(repo) + "/" +
            repoSegment(repo.name, QStringLiteral("repository")));
    if (repo.publishToNetwork) {
        publishRepository(m_repoDetailIndex, false);
        startRepoHosts();
    }
    logSystem(QStringLiteral("Pushed merged pull request #%1 to %2.")
                  .arg(pr.number)
                  .arg(branch));
    return true;
}

void MainWindow::closeCurrentPull()
{
    if (m_currentPullNumber < 0)
        return;
    PullStore store = pullStoreForCurrentRepo();
    QString error;
    if (!store.setStatus(m_currentPullNumber, "closed", &error))
        QMessageBox::warning(this, "Close pull request", error);
    reloadPulls();
}

void MainWindow::deleteCurrentPull()
{
    if (m_currentPullNumber < 0)
        return;
    if (!m_pullDeleteConfirmPending) {
        m_pullDeleteConfirmPending = true;
        // Show a simple message box confirmation instead of inline notice
        // (pull detail panel has no equivalent inline notice widget).
        const int ret = QMessageBox::warning(
            this, "Delete pull request",
            QStringLiteral("Permanently delete pull request #%1? This cannot be undone.")
                .arg(m_currentPullNumber),
            QMessageBox::Ok | QMessageBox::Cancel);
        m_pullDeleteConfirmPending = false;
        if (ret != QMessageBox::Ok)
            return;
    }
    PullStore store = pullStoreForCurrentRepo();
    QString error;
    if (!store.deletePull(m_currentPullNumber, &error)) {
        QMessageBox::warning(this, "Delete pull request",
                             error.isEmpty() ? "Could not delete the pull request." : error);
        return;
    }
    m_currentPullNumber = -1;
    reloadPulls();
}

QUrl MainWindow::pullsApiUrl(const RepositoryRecord &repo) const
{
    QUrl url = catalogApiUrl();
    url.setPath("/api/repo/" + repoSegment(repo.owner, QStringLiteral("owner")) + "/" +
                repoSegment(repo.name, QStringLiteral("repository")) + "/pulls");
    return url;
}

void MainWindow::submitPullToInbox(const PullRequest &pr)
{
    if (m_repoDetailIndex < 0 || m_repoDetailIndex >= m_repositories.size())
        return;
    const RepositoryRecord &repo = m_repositories.at(m_repoDetailIndex);
    submitPullToInbox(pr, repo);
}

void MainWindow::submitPullToInbox(const PullRequest &pr,
                                   const RepositoryRecord &targetRepo)
{
    const QJsonObject payload{{"owner", targetRepo.owner},
                              {"repo", targetRepo.name},
                              {"pull", pr.toJson()}};
    QNetworkRequest request(pullsApiUrl(targetRepo));
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    QNetworkReply *reply = m_networkAccess->post(
        request, QJsonDocument(payload).toJson(QJsonDocument::Compact));
    connect(reply, &QNetworkReply::finished, this, [this, reply, targetRepo] {
        reply->deleteLater();
        if (reply->error() == QNetworkReply::NoError)
            QMessageBox::information(
                this, "Pull request sent",
                "Your signed pull request was delivered to " + targetRepo.owner +
                    "/" + targetRepo.name + ".");
        else
            QMessageBox::warning(this, "Pull request",
                                 "Could not send the pull request: " +
                                     reply->errorString());
    });
}

void MainWindow::syncPullsInbox()
{
    if (m_repoDetailIndex < 0 || m_repoDetailIndex >= m_repositories.size())
        return;
    const RepositoryRecord &repo = m_repositories.at(m_repoDetailIndex);
    if (!pullStoreForCurrentRepo().canWrite())
        return;

    const QString owner = repoSegment(repo.owner, QStringLiteral("owner"));
    const QString ts = QString::number(QDateTime::currentMSecsSinceEpoch());
    const QByteArray canonical =
        ("forkmesh-issues-pull-v1\n" + owner + "\n" + ts).toUtf8();
    const QString sig = m_profileIdentity.signData(canonical);
    QUrl url = pullsApiUrl(repo);
    QUrlQuery query;
    query.addQueryItem("owner", owner);
    query.addQueryItem("ts", ts);
    query.addQueryItem("sig", sig);
    url.setQuery(query);

    QNetworkReply *reply = m_networkAccess->get(QNetworkRequest(url));
    connect(reply, &QNetworkReply::finished, this, [this, reply, url] {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            QMessageBox::warning(this, "Sync inbox",
                                 "Could not reach the inbox: " + reply->errorString());
            return;
        }
        const QJsonArray pending =
            QJsonDocument::fromJson(reply->readAll()).object().value("pending").toArray();
        if (pending.isEmpty()) {
            QMessageBox::information(this, "Sync inbox", "No pending pull requests.");
            return;
        }
        PullStore store = pullStoreForCurrentRepo();
        int merged = 0;
        for (const QJsonValue &value : pending) {
            const PullRequest pr =
                PullRequest::fromJson(value.toObject().value("pull").toObject());
            if (store.applyRemotePull(pr))
                ++merged;
        }
        m_networkAccess->deleteResource(QNetworkRequest(url)); // ack/clear
        reloadPulls();
        QMessageBox::information(
            this, "Sync inbox",
            QStringLiteral("Merged %1 pull request(s) into pulls/.").arg(merged));
    });
}

// ---- Agents ---------------------------------------------------------------

QString MainWindow::agentProviderName(const QString &provider) const
{
    if (provider == QLatin1String("claude"))
        return QStringLiteral("Claude Code");
    return QStringLiteral("Codex");
}

namespace {

QString agentStatusText(const QString &status)
{
    if (status == AgentStatus::Queued) return QStringLiteral("Queued");
    if (status == AgentStatus::Running) return QStringLiteral("Running");
    if (status == AgentStatus::Waiting) return QStringLiteral("Waiting");
    if (status == AgentStatus::Success) return QStringLiteral("Success");
    if (status == AgentStatus::Failed) return QStringLiteral("Failed");
    if (status == AgentStatus::Stopped) return QStringLiteral("Stopped");
    if (status == AgentStatus::Cleared) return QStringLiteral("Cleared");
    return status;
}

QColor agentStatusColor(const QString &status)
{
    if (status == AgentStatus::Success) return QColor("#3fb950");
    if (status == AgentStatus::Failed) return QColor("#f85149");
    if (status == AgentStatus::Running) return QColor("#58a6ff");
    if (status == AgentStatus::Queued) return QColor("#d29922");
    if (status == AgentStatus::Waiting) return QColor("#d29922");
    if (status == AgentStatus::Stopped) return QColor("#8b949e");
    if (status == AgentStatus::Cleared) return QColor("#8b949e");
    return QColor("#8b949e");
}

QString openAiAuthHeader(const QString &apiKey)
{
    return QStringLiteral("Bearer ") + apiKey.trimmed();
}

QNetworkRequest openAiRequest(const QUrl &url, const QString &apiKey)
{
    QNetworkRequest request(url);
    request.setRawHeader("Authorization", openAiAuthHeader(apiKey).toUtf8());
    request.setRawHeader("Accept", "application/json");
    return request;
}

QString replyHeader(QNetworkReply *reply, const char *name)
{
    return QString::fromUtf8(reply->rawHeader(name)).trimmed();
}

QString apiErrorSummary(QNetworkReply *reply, const QByteArray &body)
{
    const int status =
        reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    QString message = reply->errorString();
    const QJsonDocument doc = QJsonDocument::fromJson(body);
    const QString apiMessage =
        doc.object().value("error").toObject().value("message").toString();
    if (!apiMessage.isEmpty())
        message = apiMessage;
    if (status > 0)
        return QStringLiteral("HTTP %1: %2").arg(status).arg(message);
    return message;
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
    auto *hint = new QLabel(
        "Issue-assigned local Codex and Claude Code runs. Usage is estimated "
        "from prompt and transcript size.");
    hint->setObjectName("statusLine");
    hint->setWordWrap(true);

    m_agentTable = new QTableWidget(0, 6);
    m_agentTable->setObjectName("issueTable");
    m_agentTable->setHorizontalHeaderLabels(
        {"#", "Issue", "Agent", "Status", "PR", "When"});
    m_agentTable->verticalHeader()->setVisible(false);
    m_agentTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_agentTable->setSelectionMode(QAbstractItemView::SingleSelection);
    m_agentTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_agentTable->setShowGrid(false);
    m_agentTable->setWordWrap(false);
    m_agentTable->setSortingEnabled(true);
    QHeaderView *agentHeader = m_agentTable->horizontalHeader();
    agentHeader->setHighlightSections(false);
    agentHeader->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    agentHeader->setSectionResizeMode(1, QHeaderView::Stretch);
    for (int c = 2; c < 6; ++c)
        agentHeader->setSectionResizeMode(c, QHeaderView::ResizeToContents);

    auto *listLayout = new QVBoxLayout(listPane);
    listLayout->setContentsMargins(18, 18, 12, 18);
    listLayout->setSpacing(8);
    listLayout->addWidget(heading);
    listLayout->addWidget(hint);
    m_agentOpenAiSpend = new QLabel("OpenAI spend this month: not yet refreshed");
    m_agentOpenAiSpend->setObjectName("channelTitle");
    m_agentOpenAiSpend->setWordWrap(true);
    m_agentOpenAiSpend->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_agentApiKeyStatus = new QLabel("OpenAI usage refreshes automatically after each Codex session.");
    m_agentApiKeyStatus->setObjectName("statusLine");
    m_agentApiKeyStatus->setWordWrap(true);
    m_agentApiKeyStatus->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_agentClaudeSpend = new QLabel("Claude spend this month: not yet refreshed");
    m_agentClaudeSpend->setObjectName("channelTitle");
    m_agentClaudeSpend->setWordWrap(true);
    m_agentClaudeSpend->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_agentClaudeStatus = new QLabel("Claude usage refreshes automatically after each Claude Code session.");
    m_agentClaudeStatus->setObjectName("statusLine");
    m_agentClaudeStatus->setWordWrap(true);
    m_agentClaudeStatus->setTextInteractionFlags(Qt::TextSelectableByMouse);
    auto *usageText = new QVBoxLayout;
    usageText->setContentsMargins(0, 0, 0, 0);
    usageText->setSpacing(4);
    usageText->addWidget(m_agentOpenAiSpend);
    usageText->addWidget(m_agentApiKeyStatus);
    usageText->addWidget(m_agentClaudeSpend);
    usageText->addWidget(m_agentClaudeStatus);
    m_agentLimitsLabel = new QLabel;
    m_agentLimitsLabel->setObjectName("statusLine");
    m_agentLimitsLabel->setWordWrap(true);
    m_agentLimitsLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    usageText->addWidget(m_agentLimitsLabel);
    // Issue #115: restore the last-known spend figures immediately so they are
    // visible on restart before any network refresh completes.
    applyCachedSpendLabels();
    refreshAgentLimitLabel();
    // Tick once a minute so the countdowns stay current while the tab is open.
    m_agentLimitsTimer = new QTimer(this);
    m_agentLimitsTimer->setInterval(60 * 1000);
    connect(m_agentLimitsTimer, &QTimer::timeout, this,
            &MainWindow::refreshAgentLimitLabel);
    m_agentLimitsTimer->start();
    listLayout->addLayout(usageText);
    listLayout->addWidget(m_agentTable, 1);

    auto *detailPane = new QWidget;
    m_agentTitle = new QLabel("Select a session");
    m_agentTitle->setObjectName("channelTitle");
    m_agentTitle->setWordWrap(true);
    m_agentMeta = new QLabel;
    m_agentMeta->setObjectName("statusLine");
    m_agentMeta->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_agentMeta->setWordWrap(true);
    m_agentUsage = new QLabel;
    m_agentUsage->setObjectName("statusLine");
    m_agentUsage->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_agentUsage->setWordWrap(true);

    m_agentStopButton = new QPushButton("Stop");
    m_agentStopButton->setObjectName("dangerButton");
    m_agentStopButton->setCursor(Qt::PointingHandCursor);
    setOcticon(m_agentStopButton, "circle-slash", 16);
    connect(m_agentStopButton, &QPushButton::clicked, this, [this] {
        if (AgentRunner *runner = runnerForSession(m_selectedAgentSessionId))
            runner->stop();
    });

    m_agentContinueButton = new QPushButton("Continue");
    m_agentContinueButton->setObjectName("primaryButton");
    m_agentContinueButton->setCursor(Qt::PointingHandCursor);
    m_agentContinueButton->setToolTip("Continue this session with the same agent");
    setOcticon(m_agentContinueButton, "terminal", 16);
    connect(m_agentContinueButton, &QPushButton::clicked, this,
            &MainWindow::continueSelectedAgentSession);

    m_agentDeleteButton = new QPushButton("Delete");
    m_agentDeleteButton->setObjectName("dangerButton");
    m_agentDeleteButton->setCursor(Qt::PointingHandCursor);
    setOcticon(m_agentDeleteButton, "trash", 16);
    connect(m_agentDeleteButton, &QPushButton::clicked, this,
            &MainWindow::deleteSelectedAgentSession);

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
    topRow->addWidget(m_agentViewPrButton, 0, Qt::AlignTop);
    topRow->addWidget(m_agentContinueButton, 0, Qt::AlignTop);
    topRow->addWidget(m_agentStopButton, 0, Qt::AlignTop);
    topRow->addWidget(m_agentDeleteButton, 0, Qt::AlignTop);

    m_agentLog = new QPlainTextEdit;
    m_agentLog->setReadOnly(true);
    m_agentLog->setObjectName("actionLog");
    applyLogFont(m_agentLog);
    new AgentLogHighlighter(m_agentLog->document());
    m_agentLog->setMaximumBlockCount(30000);

    m_agentPromptEdit = new QPlainTextEdit;
    m_agentPromptEdit->setPlaceholderText("Send an additional prompt to the running agent");
    m_agentPromptEdit->setMaximumHeight(92);
    m_agentSendPromptButton = new QPushButton("Send prompt");
    m_agentSendPromptButton->setObjectName("primaryButton");
    m_agentSendPromptButton->setCursor(Qt::PointingHandCursor);
    setOcticon(m_agentSendPromptButton, "comment", 16);
    connect(m_agentSendPromptButton, &QPushButton::clicked, this, [this] {
        if (!m_agentPromptEdit || m_selectedAgentSessionId < 0)
            return;
        const QString prompt = m_agentPromptEdit->toPlainText().trimmed();
        if (prompt.isEmpty())
            return;
        if (AgentRunner *runner = runnerForSession(m_selectedAgentSessionId)) {
            runner->steer(prompt);
        } else if (AgentSession *session = findAgentSession(m_selectedAgentSessionId)) {
            m_agentStore->appendLog(
                *session,
                QStringLiteral("\n==> User prompt saved while session was not running\n%1")
                    .arg(prompt));
            showAgentSession(session->id);
        }
        m_agentPromptEdit->clear();
    });

    auto *promptRow = new QHBoxLayout;
    promptRow->setContentsMargins(0, 0, 0, 0);
    promptRow->setSpacing(8);
    promptRow->addWidget(m_agentPromptEdit, 1);
    promptRow->addWidget(m_agentSendPromptButton, 0, Qt::AlignBottom);

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
    detailLayout->addWidget(m_agentUsage);
    detailLayout->addWidget(m_agentNetPanel);
    detailLayout->addWidget(m_agentLog, 1);
    detailLayout->addLayout(promptRow);

    auto *splitter = new QSplitter(Qt::Horizontal);
    splitter->setChildrenCollapsible(false);
    splitter->addWidget(listPane);
    splitter->addWidget(detailPane);
    splitter->setStretchFactor(0, 0);
    splitter->setStretchFactor(1, 1);
    splitter->setSizes({430, 680});

    auto *layout = new QHBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(splitter);

    connect(m_agentTable, &QTableWidget::itemSelectionChanged, this, [this] {
        const QModelIndexList rows = m_agentTable->selectionModel()->selectedRows();
        if (rows.isEmpty())
            return;
        QTableWidgetItem *first = m_agentTable->item(rows.first().row(), 0);
        if (first)
            showAgentSession(first->data(Qt::UserRole).toInt());
    });
    return page;
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

        const qint64 totalTokens = state->inputTokens + state->outputTokens;
        QStringList lines;
        if (state->usageOk) {
            lines << QStringLiteral(
                         "Last 24h OpenAI usage: %1 requests, %2 total tokens (%3 input, %4 cached input, %5 output).")
                         .arg(state->requests)
                         .arg(totalTokens)
                         .arg(state->inputTokens)
                         .arg(state->cachedTokens)
                         .arg(state->outputTokens);
        }
        lines << QStringLiteral("OpenAI key works. %1 models visible.")
                     .arg(state->modelCount);
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
}

void MainWindow::markAgentLimitWindow(const QString &provider)
{
    const bool claude = provider == QLatin1String("claude");
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
    // Compact "3h 12m" / "4d 6h" rendering of a remaining duration.
    auto humanize = [](qint64 ms) -> QString {
        const qint64 totalMin = (ms + 59999) / 60000; // round up to the minute
        const qint64 days = totalMin / (24 * 60);
        const qint64 hours = (totalMin % (24 * 60)) / 60;
        const qint64 mins = totalMin % 60;
        if (days > 0)
            return QStringLiteral("%1d %2h").arg(days).arg(hours);
        if (hours > 0)
            return QStringLiteral("%1h %2m").arg(hours).arg(mins);
        return QStringLiteral("%1m").arg(mins);
    };
    auto windowText = [&](const QString &key, qint64 windowMs) -> QString {
        const qint64 start = settings.value(key).toLongLong();
        if (start <= 0)
            return QStringLiteral("ready");
        const qint64 remaining = windowMs - (now - start);
        if (remaining <= 0)
            return QStringLiteral("ready");
        return QStringLiteral("resets in %1").arg(humanize(remaining));
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

void MainWindow::refreshClaudeSpend()
{
    const QString apiKey =
        QSettings().value(kClaudeApiKeySetting).toString().trimmed();
    if (apiKey.isEmpty()) {
        if (m_agentClaudeStatus)
            m_agentClaudeStatus->setText("No Claude API key saved in Settings.");
        return;
    }
    if (!m_networkAccess) {
        if (m_agentClaudeStatus)
            m_agentClaudeStatus->setText("Network client is not ready.");
        return;
    }

    if (m_agentClaudeStatus)
        m_agentClaudeStatus->setText("Refreshing Claude usage...");

    const QDateTime now = QDateTime::currentDateTimeUtc();
    const qint64 costsStart =
        QDate(now.date().year(), now.date().month(), 1)
            .startOfDay(QTimeZone(QTimeZone::UTC))
            .toSecsSinceEpoch();
    const qint64 costsEnd =
        now.date()
            .addDays(1)
            .startOfDay(QTimeZone(QTimeZone::UTC))
            .toSecsSinceEpoch();

    QUrl url(QStringLiteral("https://api.anthropic.com/v1/usage"));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("start_time"),
                       QString::number(costsStart));
    query.addQueryItem(QStringLiteral("end_time"),
                       QString::number(costsEnd));
    url.setQuery(query);

    QNetworkRequest request(url);
    request.setRawHeader("x-api-key", apiKey.toUtf8());
    request.setRawHeader("anthropic-version", "2023-06-01");
    request.setRawHeader("Accept", "application/json");

    QNetworkReply *reply = m_networkAccess->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        const QByteArray body = reply->readAll();
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            if (m_agentClaudeStatus)
                m_agentClaudeStatus->setText(
                    QStringLiteral("Claude usage unavailable: %1")
                        .arg(apiErrorSummary(reply, body)));
            if (m_agentClaudeSpend)
                m_agentClaudeSpend->setText("Claude spend this month: unavailable");
            return;
        }
        // Parse the usage response.
        const QJsonObject root = QJsonDocument::fromJson(body).object();
        double totalCost = 0.0;
        qint64 inputTokens = 0;
        qint64 outputTokens = 0;
        bool hasData = false;
        const QJsonArray data = root.value("data").toArray();
        for (const QJsonValue &entry : data) {
            const QJsonObject obj = entry.toObject();
            totalCost += obj.value("cost").toDouble();
            inputTokens += static_cast<qint64>(obj.value("input_tokens").toDouble());
            outputTokens += static_cast<qint64>(obj.value("output_tokens").toDouble());
            hasData = true;
        }
        if (!hasData) {
            // Flat top-level cost field in some API versions.
            if (root.contains("total_cost") || root.contains("cost")) {
                totalCost = root.value("total_cost").toDouble(
                    root.value("cost").toDouble());
                hasData = true;
            }
        }
        if (m_agentClaudeSpend) {
            const QString text =
                hasData
                    ? QStringLiteral("Claude spend, month to date: $%1 USD")
                          .arg(QString::number(totalCost, 'f', 4))
                    : QStringLiteral("Claude spend this month: $0.0000 USD");
            m_agentClaudeSpend->setText(text);
            cacheSpendLabel(kClaudeSpendTextSetting, kClaudeSpendTsSetting, text);
        }
        if (m_agentClaudeStatus) {
            if (inputTokens > 0 || outputTokens > 0)
                m_agentClaudeStatus->setText(
                    QStringLiteral("Claude usage this month: %1 input tokens, %2 output tokens.")
                        .arg(inputTokens)
                        .arg(outputTokens));
            else
                m_agentClaudeStatus->setText("Claude usage refreshed.");
        }
    });
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
            session.status = AgentStatus::Stopped;
            session.lastError = QStringLiteral("Interrupted by app shutdown.");
            session.finishedAtMs = QDateTime::currentMSecsSinceEpoch();
            m_agentStore->saveSession(session);
        } else if (session.status == AgentStatus::Queued) {
            m_agentQueue.append(session.id);
        }
    }
    m_agentSessions = m_agentStore->loadAllSessions();
    processAgentQueue();
}

void MainWindow::reloadAgents()
{
    if (!m_agentStore)
        return;
    m_agentSessions = m_agentStore->loadAllSessions();
    refreshAgentTable();
    if (m_selectedAgentSessionId > 0)
        showAgentSession(m_selectedAgentSessionId);
    updateAgentsTabIndicator();
}

void MainWindow::refreshAgentTable()
{
    if (!m_agentTable)
        return;
    QString owner, name;
    if (m_repoDetailIndex >= 0 && m_repoDetailIndex < m_repositories.size()) {
        owner = m_repositories.at(m_repoDetailIndex).owner;
        name = m_repositories.at(m_repoDetailIndex).name;
    }

    const int keep = m_selectedAgentSessionId;
    QSignalBlocker block(m_agentTable);
    m_agentTable->setSortingEnabled(false);
    m_agentTable->setRowCount(0);
    for (const AgentSession &session : std::as_const(m_agentSessions)) {
        if (session.owner != owner || session.name != name)
            continue;
        const int row = m_agentTable->rowCount();
        m_agentTable->insertRow(row);

        auto *idItem = new QTableWidgetItem;
        idItem->setData(Qt::DisplayRole, session.id);
        idItem->setData(Qt::UserRole, session.id);
        m_agentTable->setItem(row, 0, idItem);
        m_agentTable->setItem(row, 1,
                              new QTableWidgetItem(
                                  QStringLiteral("#%1 %2")
                                      .arg(session.issueNumber)
                                      .arg(session.issueTitle)));
        m_agentTable->setItem(row, 2,
                              new QTableWidgetItem(agentProviderName(session.provider)));
        auto *status = new QTableWidgetItem(agentStatusText(session.status));
        status->setForeground(agentStatusColor(session.status));
        m_agentTable->setItem(row, 3, status);
        m_agentTable->setItem(row, 4,
                              new QTableWidgetItem(
                                  session.prNumber > 0
                                      ? QStringLiteral("#%1").arg(session.prNumber)
                                      : (session.createPr ? QStringLiteral("Requested")
                                                          : QStringLiteral("-"))));
        m_agentTable->setItem(
            row, 5,
            new QTableWidgetItem(
                QDateTime::fromMSecsSinceEpoch(session.createdAtMs)
                    .toString(QStringLiteral("MMM d  hh:mm"))));
    }
    m_agentTable->setSortingEnabled(true);
    block.unblock();

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
}

AgentSession *MainWindow::findAgentSession(int sessionId)
{
    for (AgentSession &session : m_agentSessions)
        if (session.id == sessionId)
            return &session;
    return nullptr;
}

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
        if (m_agentUsage)
            m_agentUsage->clear();
        if (m_agentNetPanel)
            m_agentNetPanel->clear();
        if (m_agentViewPrButton)
            m_agentViewPrButton->hide();
        if (m_agentLog)
            m_agentLog->clear();
        updateAgentActionState();
        return;
    }

    if (m_agentTitle)
        m_agentTitle->setText(QStringLiteral("%1 on issue #%2")
                                  .arg(agentProviderName(session->provider))
                                  .arg(session->issueNumber));
    if (m_agentMeta) {
        QString meta = QStringLiteral("%1/%2 · %3 · %4")
                           .arg(session->owner, session->name,
                                agentStatusText(session->status),
                                session->branchName);
        if (session->prNumber > 0)
            meta += QStringLiteral(" · PR #%1").arg(session->prNumber);
        else if (session->createPr)
            meta += QStringLiteral(" · PR requested");
        if (session->startedAtMs > 0 && session->finishedAtMs > session->startedAtMs)
            meta += QStringLiteral(" · %1s")
                        .arg((session->finishedAtMs - session->startedAtMs) / 1000);
        m_agentMeta->setText(meta);
    }
    if (m_agentUsage) {
        const int window = session->contextWindow > 0 ? session->contextWindow : 32000;
        const int maxOutput =
            session->maxOutputTokens > 0
                ? session->maxOutputTokens
                : qMax(256, QSettings().value(kAgentMaxOutputSetting, 2000).toInt());
        const int pct = window > 0 ? qMin(100, session->contextTokens * 100 / window) : 0;
        m_agentUsage->setText(
            QStringLiteral("Session token usage: %1 total (%2 prompt estimate, %3 transcript estimate) · budget: context %4/%5 (%6%), max output %7 tokens · credits ~%8")
                .arg(session->totalTokens)
                .arg(session->promptTokens)
                .arg(session->completionTokens)
                .arg(session->contextTokens)
                .arg(window)
                .arg(pct)
                .arg(maxOutput)
                .arg(session->estimatedCredits));
    }
    // Connected / working status pill.
    if (m_agentStatusPill) {
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
            label = "Completed";
        else if (s == AgentStatus::Failed)
            label = "Failed";
        else
            label = agentStatusText(s);
        m_agentStatusPill->setText(
            QString::fromUtf8("<span style='color:%1'>\xE2\x97\x8F</span> "
                              "<span style='color:#8b949e'>%2</span>")
                .arg(dotColor, label.toHtmlEscaped()));
    }

    // View PR button appears once a pull request exists for this session.
    if (m_agentViewPrButton) {
        m_agentViewPrButton->setVisible(session->prNumber > 0);
        if (session->prNumber > 0)
            m_agentViewPrButton->setText(
                QStringLiteral("View PR #%1").arg(session->prNumber));
    }

    const QString log = m_agentStore ? m_agentStore->readLog(*session) : QString();
    updateAgentNetworkPanel(log, session->status);
    if (m_agentLog) {
        m_agentLog->setPlainText(log);
        m_agentLog->moveCursor(QTextCursor::End);
    }
    updateAgentActionState();
}

void MainWindow::updateAgentNetworkPanel(const QString &log, const QString &status)
{
    if (!m_agentNetPanel)
        return;
    int requests = 0, responses = 0, errors = 0;
    long long inTokens = 0, outTokens = 0;
    static const QRegularExpression tokenRe(
        QStringLiteral("in=(\\d+)\\s+out=(\\d+)"));
    const auto lines = QStringView(log).split(QLatin1Char('\n'));
    for (const auto &lineView : lines) {
        const QString line = lineView.toString();
        if (!line.contains(QLatin1String("[net]")))
            continue;
        if (line.contains(QLatin1String("request #")))
            ++requests;
        else if (line.contains(QLatin1String("response #"))) {
            ++responses;
            const auto m = tokenRe.match(line);
            if (m.hasMatch()) {
                inTokens += m.captured(1).toLongLong();
                outTokens += m.captured(2).toLongLong();
            }
        } else if (line.contains(QLatin1String("error #")))
            ++errors;
    }

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
    if (provider == QLatin1String("claude")) {
        config.command = claudeCommandSetting();
        config.apiKeyName = QStringLiteral("ANTHROPIC_API_KEY");
        config.apiKey = QSettings().value(kClaudeApiKeySetting).toString().trimmed();
    } else {
        config.command = codexCommandSetting();
        config.apiKeyName = QStringLiteral("CODEX_API_KEY");
        config.apiKey = QSettings().value(kCodexApiKeySetting).toString().trimmed();
        config.model = QSettings().value(kCodexModelSetting).toString().trimmed();
        if (!config.apiKey.isEmpty()) {
            config.preferApiKeyAuth = true;
            config.isolatedHome =
                QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) +
                QStringLiteral("/agents/codex-api-home");
        }
    }
    return config;
}

void MainWindow::assignIssueToAgent(const QString &provider)
{
    if (!m_agentStore || m_currentIssueNumber < 0)
        return;
    const int idx = issuesRepoIndex();
    if (idx < 0 || idx >= m_repositories.size())
        return;
    IssueStore issueStore = issueStoreForCurrentRepo();
    if (!issueStore.canWrite()) {
        setIssueInlineNotice("Only the host can assign coding agents.", true);
        return;
    }
    const Issue *issue = nullptr;
    for (const Issue &candidate : std::as_const(m_currentIssues))
        if (candidate.number == m_currentIssueNumber)
            issue = &candidate;
    if (!issue)
        return;

    const RepositoryRecord &repo = m_repositories.at(idx);
    AgentSession session;
    session.owner = repo.owner;
    session.name = repo.name;
    session.issueNumber = issue->number;
    session.issueTitle = issue->title;
    session.provider = provider;
    session.createPr = m_issueAgentCreatePrCheck && m_issueAgentCreatePrCheck->isChecked();
    session.contextWindow =
        qMax(1000, QSettings().value(kAgentContextSetting, 32000).toInt());
    session = m_agentStore->createSession(session);
    session.branchName = QStringLiteral("agent/issue-%1-%2-%3")
                             .arg(session.issueNumber)
                             .arg(provider)
                             .arg(session.id);
    m_agentStore->saveSession(session);
    m_agentStore->appendLog(
        session,
        QStringLiteral("==> Assigned from ForkMesh issue #%1.").arg(issue->number));

    QString error;
    if (!issueStore.assignAgent(issue->number, provider, session.id, session.createPr,
                                AgentStatus::Queued, &error)) {
        session.status = AgentStatus::Failed;
        session.lastError = error.isEmpty() ? QStringLiteral("Could not write issue event.")
                                            : error;
        m_agentStore->saveSession(session);
        setIssueInlineNotice(session.lastError, true);
        reloadAgents();
        return;
    }

    m_agentQueue.append(session.id);
    reloadAgents();
    reloadIssues();
    setIssueInlineNotice(
        QStringLiteral("Assigned %1 session #%2.")
            .arg(agentProviderName(provider))
            .arg(session.id));
    switchToAgentsTab(session.id);
    processAgentQueue();
}

void MainWindow::continueSelectedAgentSession()
{
    if (!m_agentStore || m_selectedAgentSessionId <= 0)
        return;
    AgentSession *session = findAgentSession(m_selectedAgentSessionId);
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
    m_agentStore->saveSession(*session);
    m_agentStore->appendLog(
        *session,
        QStringLiteral("\n==> Session continued from ForkMesh."));
    if (!m_agentQueue.contains(session->id))
        m_agentQueue.append(session->id);
    reloadAgents();
    showAgentSession(session->id);
    processAgentQueue();
}

void MainWindow::deleteSelectedAgentSession()
{
    if (!m_agentStore || m_selectedAgentSessionId <= 0)
        return;
    AgentSession *session = findAgentSession(m_selectedAgentSessionId);
    if (!session)
        return;

    const AgentSession snapshot = *session;
    if (AgentRunner *runner = runnerForSession(snapshot.id)) {
        runner->stop();
        if (runner->busy()) {
            flashMessage("Stopping agent session. Delete it again once it exits.");
            return;
        }
    }
    m_agentQueue.removeAll(snapshot.id);

    const int repoIndex = repoIndexFor(snapshot.owner, snapshot.name);
    if (repoIndex >= 0) {
        const RepositoryRecord repo = m_repositories.at(repoIndex);
        IssueStore issueStore(repo.localPath, repo.mirrorPath, &m_profileIdentity,
                              m_userName);
        if (!issueStore.canWrite()) {
            flashMessage("Only the host can delete an agent session from the issue.",
                         true);
            return;
        }
        QString error;
        if (!issueStore.assignAgent(snapshot.issueNumber, QString(), 0, false,
                                    AgentStatus::Cleared, &error)) {
            flashMessage(error.isEmpty()
                             ? QStringLiteral("Could not clear the issue agent.")
                             : error,
                         true);
            return;
        }
    }

    if (!m_agentStore->deleteSession(snapshot)) {
        flashMessage("Could not delete the agent session.", true);
        return;
    }

    m_selectedAgentSessionId = -1;
    reloadAgents();
    reloadIssues();
    refreshIssueList();
    updateIssueActionState();
    flashMessage("Agent session deleted.");
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
    const int repoIndex = repoIndexFor(session->owner, session->name);
    if (repoIndex >= 0 && repoIndex != m_repoDetailIndex)
        openRepoDetail(repoIndex);
    if (m_repoDetailTabs && m_repoDetailTabs->button(3))
        m_repoDetailTabs->button(3)->setChecked(true);
    if (m_repoDetailStack)
        m_repoDetailStack->setCurrentIndex(3);
    reloadAgents();
    showAgentSession(sessionId);
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
        if (repo.localPath.isEmpty()) {
            session->status = AgentStatus::Failed;
            session->lastError = QStringLiteral("No local checkout is configured.");
            m_agentStore->saveSession(*session);
            continue;
        }
        Issue issue;
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
        const AgentSession snapshot = *session;
        // A launched run consumes from this provider's rolling usage windows;
        // anchor them so the agent sessions screen can count down the time left.
        markAgentLimitWindow(snapshot.provider);
        acquireAgentRunner()->start(snapshot, issue, repo.localPath,
                                    agentConfigForProvider(session->provider));
    }
    reloadAgents();
}

void MainWindow::onAgentLog(int sessionId, const QString &text)
{
    if (sessionId != m_selectedAgentSessionId || !m_agentLog)
        return;
    m_agentLog->moveCursor(QTextCursor::End);
    m_agentLog->insertPlainText(text);
    if (!text.endsWith(QLatin1Char('\n')))
        m_agentLog->insertPlainText(QStringLiteral("\n"));
    m_agentLog->moveCursor(QTextCursor::End);
    // Refresh the live traffic graphic when a network marker streams in.
    if (text.contains(QLatin1String("[net]")) && m_agentStore) {
        if (AgentSession *session = findAgentSession(sessionId))
            updateAgentNetworkPanel(m_agentStore->readLog(*session), session->status);
    }
}

void MainWindow::onAgentStatusChanged(int sessionId, const QString &)
{
    reloadAgents();
    if (sessionId == m_selectedAgentSessionId)
        showAgentSession(sessionId);
    refreshIssueList();
}

void MainWindow::onAgentFinished(int sessionId, bool ok)
{
    reloadAgents();
    AgentSession *session = findAgentSession(sessionId);
    // Guard against opening a second PR for the same session: onAgentFinished can
    // be reached more than once (signal re-fire, requeue), and the session may
    // already carry a prNumber from a previous pass.
    if (ok && session && session->createPr && session->prNumber <= 0 && m_agentStore) {
        const QString patch = m_agentStore->readPatch(*session);
        if (!patch.trimmed().isEmpty()) {
            const int repoIndex = repoIndexFor(session->owner, session->name);
            if (repoIndex >= 0) {
                const RepositoryRecord repo = m_repositories.at(repoIndex);
                PullStore store(repo.localPath, repo.mirrorPath, &m_profileIdentity,
                                m_userName);
                QString error;
                const int pr = store.createPull(
                    QStringLiteral("Agent: issue #%1 %2")
                        .arg(session->issueNumber)
                        .arg(session->issueTitle),
                    QStringLiteral("Created from %1 session #%2 for issue #%3.")
                        .arg(agentProviderName(session->provider))
                        .arg(session->id)
                        .arg(session->issueNumber),
                    session->baseRef, session->branchName, patch, &error);
                if (pr > 0) {
                    session->prNumber = pr;
                    m_agentStore->saveSession(*session);
                    m_agentStore->appendLog(
                        *session,
                        QStringLiteral("==> Created pull request #%1.").arg(pr));
                    if (repoIndex == m_repoDetailIndex)
                        reloadPulls();
                } else {
                    m_agentStore->appendLog(
                        *session,
                        QStringLiteral("!! Could not create pull request: %1")
                            .arg(error));
                }
            }
        }
    }
    reloadAgents();
    if (sessionId == m_selectedAgentSessionId)
        showAgentSession(sessionId);
    refreshIssueList();
    // Auto-refresh usage/spend after a session completes.
    if (session) {
        if (session->provider == QLatin1String("claude"))
            refreshClaudeSpend();
        else
            testOpenAiAgentKey();
    }
    processAgentQueue();
}

void MainWindow::updateAgentActionState()
{
    const bool selected = m_selectedAgentSessionId > 0;
    const bool running = selected && runnerForSession(m_selectedAgentSessionId);
    if (m_agentStopButton)
        m_agentStopButton->setEnabled(running);
    AgentSession *session = selected ? findAgentSession(m_selectedAgentSessionId)
                                     : nullptr;
    // Sessions run in parallel, so Continue only depends on this session's own
    // state, not whether other sessions are busy.
    if (m_agentContinueButton)
        m_agentContinueButton->setEnabled(
            session && !running && session->status != AgentStatus::Queued);
    if (m_agentDeleteButton)
        m_agentDeleteButton->setEnabled(selected);
    if (m_agentSendPromptButton)
        m_agentSendPromptButton->setEnabled(selected);
    if (m_agentPromptEdit)
        m_agentPromptEdit->setEnabled(selected);
}

void MainWindow::updateIssueAgentUi(const Issue &issue)
{
    const AgentSession *session =
        issue.number > 0 ? latestAgentSessionForIssue(issue.number) : nullptr;
    if (m_issueAgentValue) {
        if (session) {
            m_issueAgentValue->setText(
                QStringLiteral("%1 session #%2<br>%3 · ~%4 credits")
                    .arg(agentProviderName(session->provider))
                    .arg(session->id)
                    .arg(agentStatusText(session->status))
                    .arg(session->estimatedCredits));
        } else {
            m_issueAgentValue->setText("No agent assigned");
        }
    }
    if (m_issueAgentViewButton)
        m_issueAgentViewButton->setVisible(session);
}

QWidget *MainWindow::buildRepoFilesPanel()
{
    // Two modes: a GitHub-style overview, and an explorer+editor view shown only
    // once a specific file is opened.
    m_filesStack = new QStackedWidget;
    m_filesStack->addWidget(buildRepoOverviewPage()); // 0 overview
    m_filesStack->addWidget(buildRepoEditorPage());   // 1 editor (explorer + tabs)
    return m_filesStack;
}

QWidget *MainWindow::buildRepoOverviewPage()
{
    auto *page = new QWidget;

    // Latest-commit bar with a history button, like GitHub's commit strip.
    auto *commitCard = new QWidget;
    commitCard->setObjectName("commitBar");
    m_commitBar = new QLabel;
    m_commitBar->setObjectName("commitBarText");
    m_commitBar->setTextFormat(Qt::RichText);
    m_commitBar->setWordWrap(true);
    m_historyButton = new QPushButton("Commits");
    m_historyButton->setObjectName("ghostButton");
    m_historyButton->setCursor(Qt::PointingHandCursor);
    m_historyButton->setToolTip("View the full commit history");
    setOcticon(m_historyButton, "git-branch", 16);
    connect(m_historyButton, &QPushButton::clicked, this, [this] {
        if (m_repoDetailTabs && m_repoDetailTabs->button(1))
            m_repoDetailTabs->button(1)->setChecked(true);
        if (m_repoDetailStack)
            m_repoDetailStack->setCurrentIndex(1);
        loadCommits();
    });
    auto *commitRow = new QHBoxLayout(commitCard);
    commitRow->setContentsMargins(12, 8, 8, 8);
    commitRow->addWidget(m_commitBar, 1);
    commitRow->addWidget(m_historyButton);

    m_overviewCrumb = new QLabel;
    m_overviewCrumb->setObjectName("statusLine");
    m_overviewCrumb->setTextFormat(Qt::RichText);
    m_overviewCrumb->setTextInteractionFlags(Qt::TextBrowserInteraction);
    connect(m_overviewCrumb, &QLabel::linkActivated, this,
            [this](const QString &href) {
                loadRepoOverview(href == "/" ? QString() : href);
            });

    m_overviewList = new QListWidget;
    m_overviewList->setObjectName("overviewList");
    connect(m_overviewList, &QListWidget::itemClicked, this,
            [this](QListWidgetItem *item) {
                const QString path = item->data(Qt::UserRole).toString();
                if (path.isEmpty())
                    return;
                if (item->data(Qt::UserRole + 1).toBool())
                    loadRepoOverview(path); // navigate into the directory
                else
                    openRepoFile(path); // open the file (switches to editor view)
            });

    m_readmeView = new QTextBrowser;
    m_readmeView->setObjectName("readmeView");
    m_readmeView->setOpenExternalLinks(true);

    // Toolbar: branch switcher + tags + "go to file" search.
    m_branchButton = new QPushButton("main");
    m_branchButton->setObjectName("ghostButton");
    m_branchButton->setCursor(Qt::PointingHandCursor);
    m_branchButton->setToolTip("Switch branch");
    setOcticon(m_branchButton, "git-branch", 16);
    m_branchesButton = new QPushButton("0 branches");
    m_branchesButton->setObjectName("ghostButton");
    m_branchesButton->setCursor(Qt::PointingHandCursor);
    m_branchesButton->setToolTip(
        "Open the Branches panel to view, compare, switch and delete branches");
    setOcticon(m_branchesButton, "git-branch", 16);
    connect(m_branchesButton, &QPushButton::clicked, this, [this] {
        if (m_branchesTabIndex >= 0 && m_repoDetailTabs &&
            m_repoDetailTabs->button(m_branchesTabIndex)) {
            m_repoDetailTabs->button(m_branchesTabIndex)->setChecked(true);
            m_repoDetailStack->setCurrentIndex(m_branchesTabIndex);
            loadBranchesPanel();
        }
    });
    m_tagsButton = new QPushButton("Tags");
    m_tagsButton->setObjectName("ghostButton");
    m_tagsButton->setCursor(Qt::PointingHandCursor);
    m_tagsButton->setToolTip("Open the Releases panel to create and manage tagged releases");
    setOcticon(m_tagsButton, "tag", 16);
    connect(m_tagsButton, &QPushButton::clicked, this, [this] {
        if (m_releasesTabIndex >= 0 && m_repoDetailTabs &&
            m_repoDetailTabs->button(m_releasesTabIndex)) {
            m_repoDetailTabs->button(m_releasesTabIndex)->setChecked(true);
            m_repoDetailStack->setCurrentIndex(m_releasesTabIndex);
            loadReleasesPanel();
        }
    });
    m_fileSearch = new QLineEdit;
    m_fileSearch->setPlaceholderText("Go to file\xE2\x80\xA6");
    m_fileSearch->setClearButtonEnabled(true);
    m_fileCompleter = new QCompleter(this);
    m_fileCompleter->setCaseSensitivity(Qt::CaseInsensitive);
    m_fileCompleter->setFilterMode(Qt::MatchContains);
    m_fileCompleter->setCompletionMode(QCompleter::PopupCompletion);
    m_fileSearch->setCompleter(m_fileCompleter);
    connect(m_fileCompleter, QOverload<const QString &>::of(&QCompleter::activated),
            this, [this](const QString &path) {
                if (!path.isEmpty())
                    openRepoFile(path);
                m_fileSearch->clear();
            });
    auto *toolbar = new QHBoxLayout;
    toolbar->setContentsMargins(0, 0, 0, 0);
    toolbar->setSpacing(8);
    toolbar->addWidget(m_branchButton);
    toolbar->addWidget(m_branchesButton);
    toolbar->addWidget(m_tagsButton);
    toolbar->addWidget(m_fileSearch, 1);

    // Left column: toolbar, latest commit, file list, README.
    auto *leftColumn = new QWidget;
    auto *leftLayout = new QVBoxLayout(leftColumn);
    leftLayout->setContentsMargins(0, 0, 0, 0);
    leftLayout->setSpacing(8);
    leftLayout->addLayout(toolbar);
    leftLayout->addWidget(commitCard);
    leftLayout->addWidget(m_overviewCrumb);
    leftLayout->addWidget(m_overviewList, 2);
    leftLayout->addWidget(m_readmeView, 3);

    auto *body = new QHBoxLayout;
    body->setContentsMargins(0, 0, 0, 0);
    body->setSpacing(16);
    body->addWidget(leftColumn, 1);
    body->addWidget(buildAboutSidebar());

    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(16, 10, 16, 16);
    layout->setSpacing(8);
    layout->addLayout(body);
    return page;
}

QWidget *MainWindow::buildAboutSidebar()
{
    auto *side = new QWidget;
    side->setObjectName("aboutSidebar");
    side->setFixedWidth(300);

    auto *aboutLabel = new QLabel("About");
    aboutLabel->setObjectName("aboutHeading");
    m_aboutText = new QLabel;
    m_aboutText->setObjectName("statusLine");
    m_aboutText->setWordWrap(true);
    m_aboutText->setTextFormat(Qt::RichText);
    m_aboutText->setOpenExternalLinks(true);
    m_aboutTopics = new QLabel;
    m_aboutTopics->setObjectName("statusLine");
    m_aboutTopics->setWordWrap(true);
    m_aboutTopics->setTextFormat(Qt::RichText);

    auto *langLabel = new QLabel("LANGUAGES");
    langLabel->setObjectName("sectionLabel");
    m_langBar = new QLabel;
    m_langBar->setObjectName("langBar");
    m_langBar->setFixedHeight(8);
    m_langBar->setTextFormat(Qt::RichText);
    m_langLegend = new QLabel;
    m_langLegend->setObjectName("statusLine");
    m_langLegend->setWordWrap(true);
    m_langLegend->setTextFormat(Qt::RichText);

    m_contributorsHeader = new QLabel("CONTRIBUTORS");
    m_contributorsHeader->setObjectName("sectionLabel");
    m_contributorsRow = new QLabel;
    m_contributorsRow->setObjectName("statusLine");
    m_contributorsRow->setWordWrap(true);
    m_contributorsRow->setTextFormat(Qt::RichText);

    auto *layout = new QVBoxLayout(side);
    layout->setContentsMargins(8, 0, 0, 0);
    layout->setSpacing(6);
    layout->addWidget(aboutLabel);
    layout->addWidget(m_aboutText);
    layout->addWidget(m_aboutTopics);
    layout->addSpacing(6);
    layout->addWidget(langLabel);
    layout->addWidget(m_langBar);
    layout->addWidget(m_langLegend);
    layout->addSpacing(6);
    layout->addWidget(m_contributorsHeader);
    layout->addWidget(m_contributorsRow);
    layout->addStretch();
    return side;
}

QWidget *MainWindow::buildRepoEditorPage()
{
    auto *page = new QWidget;

    auto *backButton = new QPushButton("Files");
    backButton->setObjectName("ghostButton");
    backButton->setCursor(Qt::PointingHandCursor);
    backButton->setToolTip("Back to the repository overview");
    setOcticon(backButton, "arrow-left", 16);
    connect(backButton, &QPushButton::clicked, this, &MainWindow::showRepoOverview);
    auto *backRow = new QHBoxLayout;
    backRow->setContentsMargins(8, 4, 8, 0);
    backRow->addWidget(backButton);
    backRow->addStretch();
    m_repoFileCommitButton = new QPushButton("Commit direct");
    m_repoFileCommitButton->setObjectName("ghostButton");
    m_repoFileCommitButton->setCursor(Qt::PointingHandCursor);
    m_repoFileCommitButton->setToolTip("Save this file and commit it directly to the default branch");
    setOcticon(m_repoFileCommitButton, "upload", 16);
    connect(m_repoFileCommitButton, &QPushButton::clicked, this,
            [this] { saveCurrentRepoFile(false); });
    m_repoFilePullButton = new QPushButton("Save as PR");
    m_repoFilePullButton->setObjectName("primaryButton");
    m_repoFilePullButton->setCursor(Qt::PointingHandCursor);
    m_repoFilePullButton->setToolTip("Save this file on a new branch and open a pull request");
    setOcticon(m_repoFilePullButton, "git-pull-request", 16);
    connect(m_repoFilePullButton, &QPushButton::clicked, this,
            [this] { saveCurrentRepoFile(true); });
    backRow->addWidget(m_repoFileCommitButton);
    backRow->addWidget(m_repoFilePullButton);

    m_repoFileTree = new QTreeWidget;
    m_repoFileTree->setObjectName("fileTree");
    m_repoFileTree->setHeaderHidden(true);
    m_repoFileTree->setMinimumWidth(180);
    m_repoFileTree->setIndentation(14);
    connect(m_repoFileTree, &QTreeWidget::itemClicked, this,
            [this](QTreeWidgetItem *item, int) {
                if (item && !item->data(0, Qt::UserRole + 1).toBool())
                    openRepoFile(item->data(0, Qt::UserRole).toString());
            });
    connect(m_repoFileTree, &QTreeWidget::itemExpanded, this,
            [this](QTreeWidgetItem *item) {
                if (item && item->data(0, Qt::UserRole + 1).toBool())
                    item->setIcon(0, iconForDir(true));
            });
    connect(m_repoFileTree, &QTreeWidget::itemCollapsed, this,
            [this](QTreeWidgetItem *item) {
                if (item && item->data(0, Qt::UserRole + 1).toBool())
                    item->setIcon(0, iconForDir(false));
            });

    m_repoFileTabs = new QTabWidget;
    m_repoFileTabs->setObjectName("fileTabs");
    m_repoFileTabs->setDocumentMode(true);
    m_repoFileTabs->setMovable(true);
    m_repoFileTabs->setTabsClosable(true);
    connect(m_repoFileTabs, &QTabWidget::tabCloseRequested, this, [this](int index) {
        QWidget *w = m_repoFileTabs->widget(index);
        m_openFileTabs.remove(m_openFileTabs.key(w));
        m_repoFileTabs->removeTab(index);
        w->deleteLater();
        // With no files left open, return to the overview.
        if (m_repoFileTabs->count() == 0)
            showRepoOverview();
        updateRepoFileSaveActions();
    });
    connect(m_repoFileTabs, &QTabWidget::currentChanged, this,
            [this] { updateRepoFileSaveActions(); });

    auto *splitter = new QSplitter(Qt::Horizontal);
    splitter->setObjectName("filesSplitter");
    splitter->setChildrenCollapsible(false);
    splitter->addWidget(m_repoFileTree);
    splitter->addWidget(m_repoFileTabs);
    splitter->setStretchFactor(0, 0);
    splitter->setStretchFactor(1, 1);
    splitter->setSizes({240, 700});

    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addLayout(backRow);
    layout->addWidget(splitter, 1);
    updateRepoFileSaveActions();
    return page;
}

QString MainWindow::iconsDir() const
{
    static bool resolved = false;
    static QString cached;
    if (resolved)
        return cached;
    resolved = true;
    const QString src = QStringLiteral(FORKMESH_SOURCE_DIR);
    if (!src.isEmpty()) {
        const QString candidate = QDir(src).absoluteFilePath("../icons");
        if (QDir(candidate).exists()) {
            cached = QDir(candidate).absolutePath();
            return cached;
        }
    }
    const QString beside = QCoreApplication::applicationDirPath() + "/icons";
    if (QDir(beside).exists())
        cached = beside;
    return cached;
}

QIcon MainWindow::iconForFile(const QString &fileName) const
{
    const QString dir = iconsDir();
    if (dir.isEmpty())
        return {};
    QString path = dir + "/" + fileTypeIconName(fileName.toLower()) + ".svg";
    if (!QFileInfo::exists(path))
        path = dir + "/default_file.svg";
    return QIcon(path);
}

QIcon MainWindow::iconForDir(bool opened) const
{
    const QString dir = iconsDir();
    if (dir.isEmpty())
        return {};
    return QIcon(dir + (opened ? "/default_folder_opened.svg" : "/default_folder.svg"));
}

QString MainWindow::repoGitDir() const
{
    if (m_repoDetailIndex < 0 || m_repoDetailIndex >= m_repositories.size())
        return {};
    const RepositoryRecord &repo = m_repositories.at(m_repoDetailIndex);
    if (!repo.localPath.isEmpty() && QDir(repo.localPath).exists())
        return repo.localPath;
    if (!repo.mirrorPath.isEmpty() && QDir(repo.mirrorPath).exists())
        return repo.mirrorPath;
    return {};
}

void MainWindow::setRepoDetailNotice(const QString &message, bool error)
{
    // Success/failure notices now surface as a compact, centered banner in the
    // top bar (between the breadcrumb and the bell) instead of an inline strip,
    // so the message is consistent everywhere in the app.
    if (m_repoDetailNotice) {
        m_repoDetailNotice->clear();
        m_repoDetailNotice->hide();
    }
    flashMessage(message, error);
}

void MainWindow::forkCurrentRepo()
{
    if (m_repoDetailIndex < 0 || m_repoDetailIndex >= m_repositories.size())
        return;
    const RepositoryRecord src = m_repositories.at(m_repoDetailIndex);

    // Default destination: your own account owns it, keeping the same name.
    const QString defaultOwner = accountOwner();

    // --- Ask where to fork to (destination owner + repository name).
    QDialog dialog(this);
    dialog.setWindowTitle("Fork repository");
    auto *form = new QFormLayout(&dialog);
    auto *info = new QLabel(
        QStringLiteral("Fork <b>%1/%2</b> into your own node. A new independent "
                       "mirror is created that you can push to.")
            .arg(src.owner.toHtmlEscaped(), src.name.toHtmlEscaped()));
    info->setWordWrap(true);
    info->setTextFormat(Qt::RichText);
    auto *ownerEdit = new QLineEdit(defaultOwner);
    auto *nameEdit = new QLineEdit(src.name);
    form->addRow(info);
    form->addRow("Owner (your node)", ownerEdit);
    form->addRow("Repository name", nameEdit);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok |
                                         QDialogButtonBox::Cancel);
    buttons->button(QDialogButtonBox::Ok)->setText("Fork");
    form->addRow(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if (dialog.exec() != QDialog::Accepted)
        return;

    const QString owner =
        repoSegment(ownerEdit->text().trimmed(), QStringLiteral("owner"));
    const QString name =
        repoSegment(nameEdit->text().trimmed(), QStringLiteral("repository"));
    if (owner.isEmpty() || name.isEmpty())
        return;
    for (const RepositoryRecord &r : std::as_const(m_repositories))
        if (!r.previewOnly && r.owner == owner && r.name == name) {
            QMessageBox::information(
                this, "Fork repository",
                QStringLiteral("You already have %1/%2.").arg(owner, name));
            return;
        }

    // Clone the fork from the existing local mirror when available (fast,
    // offline); otherwise from the repo's configured source.
    const QString source =
        (!src.mirrorPath.isEmpty() && QDir(src.mirrorPath).exists())
            ? src.mirrorPath
            : repositorySource(src);
    if (source.isEmpty()) {
        QMessageBox::warning(
            this, "Fork repository",
            "There is no local mirror or source to fork from yet. Sync the "
            "repository first, then fork.");
        return;
    }

    RepositoryRecord fork;
    fork.owner = owner;
    fork.name = name;
    fork.description = src.description;
    fork.solanaAddress = savedSolanaAddress();
    fork.publishToNetwork = true;
    fork.actionsEnabled = src.actionsEnabled;
    fork.hostedSinceMs = QDateTime::currentMSecsSinceEpoch();
    fork.mirrorPath = repositoryMirrorRoot() + "/" +
                      repoSegment(owner, QStringLiteral("owner")) + "-" +
                      repoSegment(name, QStringLiteral("repository")) + ".git";
    // No cloneUrl/localPath: a fork is independent and must not auto-sync from
    // upstream (that would prune the branches you push to it).

    m_repositories.append(fork);
    saveRepositories();
    refreshRepositoryList();
    logSystem(QStringLiteral("Forking %1/%2 to %3/%4 from %5")
                  .arg(src.owner, src.name, owner, name, source));

    QDir().mkpath(QFileInfo(fork.mirrorPath).absolutePath());
    auto *process = new QProcess(this);
    connect(process, &QProcess::finished, this,
            [this, process, owner, name](int code, QProcess::ExitStatus) {
                const QString err =
                    QString::fromUtf8(process->readAllStandardError()).trimmed();
                process->deleteLater();
                const int idx = repoIndexFor(owner, name);
                if (idx < 0)
                    return;
                if (code != 0) {
                    logSystem("Fork failed: " + err.right(300));
                    QMessageBox::warning(this, "Fork repository",
                                         "Could not clone the fork" +
                                             (err.isEmpty() ? QString()
                                                            : ":\n" + err.right(400)));
                    m_repositories.removeAt(idx);
                    saveRepositories();
                    refreshRepositoryList();
                    return;
                }
                RepositoryRecord &f = m_repositories[idx];
                // Detach from upstream so the fork is fully independent.
                QProcess::execute(QStringLiteral("git"),
                                  {QStringLiteral("-C"), f.mirrorPath,
                                   QStringLiteral("remote"), QStringLiteral("remove"),
                                   QStringLiteral("origin")});
                f.lastSyncMs = QDateTime::currentMSecsSinceEpoch();
                saveRepositories();
                ensurePushHook(f);
                if (m_backend)
                    m_backend->addChannel(repositoryChannel(f));
                publishRepository(idx, false);
                startRepoHosts();
                refreshRepositoryList();
                logSystem(QStringLiteral("Forked into %1/%2.").arg(owner, name));
                openRepoDetail(idx);
            });
    process->start(QStringLiteral("git"),
                   {QStringLiteral("clone"), QStringLiteral("--mirror"), source,
                    fork.mirrorPath});
}

void MainWindow::downloadCurrentRepoZip()
{
    if (m_repoDetailIndex < 0 || m_repoDetailIndex >= m_repositories.size())
        return;
    const RepositoryRecord repo = m_repositories.at(m_repoDetailIndex);
    const QString dir = repoGitDir();
    if (dir.isEmpty()) {
        setRepoDetailNotice(
            "Sync this repository first; there is no local mirror to archive yet.",
            true);
        return;
    }

    const QString downloads =
        QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
    const QString outDir = downloads.isEmpty() ? QDir::homePath() : downloads;
    QDir().mkpath(outDir);

    const QString safeOwner = repoSegment(repo.owner, QStringLiteral("owner"));
    const QString safeName = repoSegment(repo.name, QStringLiteral("repository"));
    const QString safeRef = repoSegment(currentRef(), QStringLiteral("head"));
    const QString base = safeOwner + "-" + safeName + "-" + safeRef;
    QString target = QDir(outDir).filePath(base + ".zip");
    for (int i = 2; QFileInfo::exists(target); ++i)
        target = QDir(outDir).filePath(base + "-" + QString::number(i) + ".zip");

    if (m_sourceButton) {
        m_sourceButton->setEnabled(false);
        m_sourceButton->setText("Zipping...");
    }
    setRepoDetailNotice("Creating ZIP archive in Downloads...");

    auto *process = new QProcess(this);
    auto handled = std::make_shared<bool>(false);
    auto finishButton = [this] {
        if (m_sourceButton) {
            m_sourceButton->setEnabled(true);
            m_sourceButton->setText("Source");
            setOcticon(m_sourceButton, "code", 16);
        }
    };
    connect(process, &QProcess::errorOccurred, this,
            [this, process, handled, finishButton](QProcess::ProcessError) {
                if (*handled)
                    return;
                *handled = true;
                finishButton();
                setRepoDetailNotice("Could not start git archive.", true);
                process->deleteLater();
            });
    connect(process, &QProcess::finished, this,
            [this, process, handled, finishButton, target](int code,
                                                           QProcess::ExitStatus status) {
                if (*handled)
                    return;
                *handled = true;
                const QString err =
                    QString::fromUtf8(process->readAllStandardError()).trimmed();
                process->deleteLater();
                finishButton();
                if (status != QProcess::NormalExit || code != 0) {
                    setRepoDetailNotice(
                        "Could not create ZIP archive" +
                            (err.isEmpty() ? QString() : ": " + err.left(240)),
                        true);
                    return;
                }
                setRepoDetailNotice("Saved ZIP archive to " +
                                    QFileInfo(target).absoluteFilePath());
                logSystem("Saved repository ZIP to " + target);
            });
    process->start(
        QStringLiteral("git"),
        {QStringLiteral("-C"), dir, QStringLiteral("archive"),
         QStringLiteral("--format=zip"), QStringLiteral("-o"), target,
         QStringLiteral("--prefix=") + safeOwner + "-" + safeName + "/",
         currentRef()});
}

void MainWindow::openRepoDetail(int repoIndex)
{
    if (repoIndex < 0 || repoIndex >= m_repositories.size())
        return;
    m_repoDetailIndex = repoIndex;
    const RepositoryRecord &repo = m_repositories.at(repoIndex);
    if (!repo.previewOnly)
        QSettings().setValue(kLastRepositorySetting, repo.owner + "/" + repo.name);
    if (m_repoHeaderTitle)
        m_repoHeaderTitle->setText(
            QStringLiteral("%1 / <b>%2</b>")
                .arg(repo.owner.toHtmlEscaped(), repo.name.toHtmlEscaped()));
    setRepoDetailNotice(QString());

    // Per-repo metadata (info.json) + the branch we view; both feed the loaders.
    m_repoInfo = RepoInfo();
    m_repoBranch.clear();
    loadRepoInfo();
    loadBranchesAndTags();
    if (m_forkButton)
        m_forkButton->setText(QStringLiteral("Fork %1").arg(m_repoInfo.forks));
    if (m_mirrorButton) {
        if (repo.previewOnly) {
            m_mirrorButton->setText(QStringLiteral("Mirror it"));
            m_mirrorButton->setToolTip(
                "Clone this preview into your local mirrors and host it");
        } else {
            m_mirrorButton->setText(
                QStringLiteral("Mirror %1").arg(qMax(1, m_repoInfo.mirrors)));
            m_mirrorButton->setToolTip("Sync this repository's mirror now");
        }
    }
    if (m_starButton)
        m_starButton->setText(QStringLiteral("Star %1").arg(m_repoInfo.stars));
    updateRepoDetailStatus();
    updateRepoActionMenus();
    updateRepoCodeSize();

    // Point the embedded issues UI at this repo (its combo is hidden).
    refreshIssuesRepoCombo();
    if (m_issuesRepoCombo) {
        const int combo = m_issuesRepoCombo->findData(repoIndex);
        if (combo >= 0)
            m_issuesRepoCombo->setCurrentIndex(combo);
    }
    reloadIssues();
    reloadAgents();
    updateRepoIssueCount();
    m_currentPulls = pullStoreForCurrentRepo().loadAll();
    updateRepoPullCount();

    // Default to the Code tab; reset the editor tabs/tree for the new repo.
    if (m_repoDetailTabs && m_repoDetailTabs->button(0))
        m_repoDetailTabs->button(0)->setChecked(true);
    if (m_repoDetailStack)
        m_repoDetailStack->setCurrentIndex(0);
    if (m_repoFileTabs) {
        m_repoFileTabs->clear();
        m_openFileTabs.clear();
    }
    if (m_repoFileTree)
        m_repoFileTree->clear();
    m_treeLoadedForIndex = -1;

    loadFileSearchIndex();
    loadAboutSidebar();
    loadCommits();
    // Insights (contributor stats, git shortlog) are computed lazily when the
    // Insights tab is opened — see the tab-switch handler — so opening a repo
    // doesn't pay for them up front.
    // Land on the GitHub-style overview (no explorer until a file is opened).
    loadRepoOverview(QString());
    if (m_filesStack)
        m_filesStack->setCurrentIndex(0);
    // The detail panel lives inside Home next to the columns now, so just make
    // sure Home is the active section and refresh the breadcrumb.
    showSection(0);
    updateBreadcrumb();
    updateActionsTabIndicator(); // reflect any in-flight runs for this repo
}

void MainWindow::updateRepoCodeSize()
{
    if (!m_repoCodeTab)
        return;
    if (m_repoDetailIndex < 0 || m_repoDetailIndex >= m_repositories.size()) {
        m_repoCodeTab->setText(QStringLiteral("Code (0 B)"));
        return;
    }
    const RepositoryRecord &repo = m_repositories.at(m_repoDetailIndex);
    if (repo.mirrorPath.isEmpty() || !QDir(repo.mirrorPath).exists()) {
        m_repoCodeTab->setText(QStringLiteral("Code (0 B)"));
        return;
    }

    QByteArray out;
    qint64 sizeKiB = 0;
    if (runGitCapture(repo.mirrorPath, {"count-objects", "-v"}, &out, nullptr)) {
        const QString text = QString::fromUtf8(out);
        for (const QString &line : text.split(QLatin1Char('\n'))) {
            const int colon = line.indexOf(QLatin1Char(':'));
            if (colon < 0)
                continue;
            const QString key = line.left(colon).trimmed();
            if (key == QLatin1String("size") || key == QLatin1String("size-pack"))
                sizeKiB += line.mid(colon + 1).trimmed().toLongLong();
        }
    }
    m_repoCodeTab->setText(QStringLiteral("Code (%1)").arg(formatByteSize(sizeKiB * 1024)));
}

void MainWindow::updateRepoCommitCount()
{
    if (!m_repoCommitsTab)
        return;
    const QString dir = repoGitDir();
    QByteArray out;
    int count = 0;
    if (!dir.isEmpty() &&
        runGitCapture(dir, {"rev-list", "--count", currentRef()}, &out, nullptr))
        count = QString::fromUtf8(out).trimmed().toInt();
    m_repoCommitsTab->setText(QStringLiteral("Commits (%1)").arg(count));
}

void MainWindow::updateRepoIssueCount()
{
    if (m_repoIssuesTab) {
        int openCount = 0;
        for (const Issue &issue : std::as_const(m_currentIssues)) {
            if (issue.status != "closed")
                ++openCount;
        }
        m_repoIssuesTab->setText(
            QStringLiteral("Issues (%1)").arg(openCount));
    }
}

void MainWindow::updateRepoPullCount()
{
    if (m_repoPullsTab)
        m_repoPullsTab->setText(
            QStringLiteral("Pull requests (%1)").arg(m_currentPulls.size()));
}

void MainWindow::loadRepoFileTree()
{
    if (!m_repoFileTree)
        return;
    m_repoFileTree->clear();

    const QString dir = repoGitDir();
    if (dir.isEmpty()) {
        new QTreeWidgetItem(m_repoFileTree,
                            {"No local copy of this repository to browse."});
        return;
    }
    QByteArray out;
    QString err;
    // One recursive listing of every tracked path; we build the hierarchy below.
    if (!runGitCapture(dir, {"ls-tree", "-r", "--name-only", "-z", currentRef()}, &out,
                       &err)) {
        new QTreeWidgetItem(m_repoFileTree,
                            {err.isEmpty() ? "This repository has no commits yet."
                                           : "Could not read files: " + err.left(120)});
        return;
    }

    QStringList paths;
    for (const QByteArray &record : out.split('\0'))
        if (!record.isEmpty())
            paths << QString::fromUtf8(record);
    paths.sort(Qt::CaseInsensitive);

    QHash<QString, QTreeWidgetItem *> dirs; // accumulated path -> directory node
    for (const QString &path : std::as_const(paths)) {
        const QStringList parts = path.split('/', Qt::SkipEmptyParts);
        QString acc;
        QTreeWidgetItem *parent = nullptr;
        for (int i = 0; i < parts.size(); ++i) {
            acc = acc.isEmpty() ? parts.at(i) : acc + "/" + parts.at(i);
            const bool isLast = (i == parts.size() - 1);
            if (isLast) {
                auto *item = parent ? new QTreeWidgetItem(parent)
                                    : new QTreeWidgetItem(m_repoFileTree);
                item->setText(0, parts.at(i));
                item->setIcon(0, iconForFile(parts.at(i)));
                item->setData(0, Qt::UserRole, acc);
                item->setData(0, Qt::UserRole + 1, false);
            } else {
                QTreeWidgetItem *node = dirs.value(acc);
                if (!node) {
                    node = parent ? new QTreeWidgetItem(parent)
                                  : new QTreeWidgetItem(m_repoFileTree);
                    node->setText(0, parts.at(i));
                    node->setIcon(0, iconForDir(false));
                    node->setData(0, Qt::UserRole, acc);
                    node->setData(0, Qt::UserRole + 1, true);
                    dirs.insert(acc, node);
                }
                parent = node;
            }
        }
    }

    // Folders first, then files, alphabetically — at every level.
    std::function<void(QTreeWidgetItem *)> sortChildren = [&](QTreeWidgetItem *parent) {
        const auto kids = parent->takeChildren();
        QList<QTreeWidgetItem *> sorted = kids;
        std::sort(sorted.begin(), sorted.end(),
                  [](QTreeWidgetItem *a, QTreeWidgetItem *b) {
                      const bool ad = a->data(0, Qt::UserRole + 1).toBool();
                      const bool bd = b->data(0, Qt::UserRole + 1).toBool();
                      if (ad != bd)
                          return ad;
                      return a->text(0).toLower() < b->text(0).toLower();
                  });
        for (QTreeWidgetItem *child : std::as_const(sorted)) {
            parent->addChild(child);
            sortChildren(child);
        }
    };
    sortChildren(m_repoFileTree->invisibleRootItem());

    if (paths.isEmpty())
        new QTreeWidgetItem(m_repoFileTree, {"(empty repository)"});
}

void MainWindow::openRepoFile(const QString &path)
{
    if (path.isEmpty() || !m_repoFileTabs)
        return;
    // Opening a file reveals the explorer + editor view; build the tree lazily.
    if (m_treeLoadedForIndex != m_repoDetailIndex) {
        loadRepoFileTree();
        m_treeLoadedForIndex = m_repoDetailIndex;
    }
    if (m_filesStack)
        m_filesStack->setCurrentIndex(1);

    // Focus an already-open tab for this file.
    if (m_openFileTabs.contains(path)) {
        m_repoFileTabs->setCurrentWidget(m_openFileTabs.value(path));
        return;
    }
    const QString dir = repoGitDir();
    QByteArray out;
    QString err;
    QString content;
    bool editable = false;
    if (dir.isEmpty() || !runGitCapture(dir, {"show", currentRef() + ":" + path}, &out, &err))
        content = "Could not read file: " + err.left(200);
    else if (out.size() > 1024 * 1024)
        content = QStringLiteral("File is too large to preview (%1 KB).")
                      .arg(out.size() / 1024);
    else if (out.contains('\0'))
        content = QString::fromUtf8("Binary file (%1 bytes) \xE2\x80\x94 not shown.")
                      .arg(out.size());
    else {
        content = QString::fromUtf8(out);
        editable = repoHasWorkingTree();
        if (path.endsWith(QStringLiteral(".json"), Qt::CaseInsensitive) ||
            path.endsWith(QStringLiteral(".jsonc"), Qt::CaseInsensitive)) {
            const QJsonDocument doc = QJsonDocument::fromJson(out);
            if (!doc.isNull())
                content = QString::fromUtf8(doc.toJson(QJsonDocument::Indented));
        }
    }

    auto *editor = new CodePreviewEditor(path);
    editor->setReadOnly(!editable);
    editor->setPlainText(content);
    editor->document()->setModified(false);
    new CodePreviewHighlighter(editor->document(), path);

    const QString name = path.section('/', -1);
    const int index = m_repoFileTabs->addTab(editor, iconForFile(name), name);
    m_repoFileTabs->setTabToolTip(index, path);
    m_repoFileTabs->setCurrentIndex(index);
    m_openFileTabs.insert(path, editor);
    updateRepoFileSaveActions();
}

void MainWindow::updateRepoFileSaveActions()
{
    const QWidget *w = m_repoFileTabs ? m_repoFileTabs->currentWidget() : nullptr;
    const auto *editor = qobject_cast<const QPlainTextEdit *>(w);
    const bool canSave = editor && !editor->isReadOnly() &&
                         !w->property("previewPath").toString().isEmpty() &&
                         repoHasWorkingTree();
    if (m_repoFileCommitButton)
        m_repoFileCommitButton->setEnabled(canSave);
    if (m_repoFilePullButton)
        m_repoFilePullButton->setEnabled(canSave);
}

void MainWindow::saveCurrentRepoFile(bool createPull)
{
    QWidget *w = m_repoFileTabs ? m_repoFileTabs->currentWidget() : nullptr;
    auto *editor = qobject_cast<QPlainTextEdit *>(w);
    if (!editor || editor->isReadOnly())
        return;
    const QString path = w->property("previewPath").toString();
    if (path.isEmpty())
        return;
    if (saveRepoFileEdit(path, editor->toPlainText(), createPull))
        editor->document()->setModified(false);
    updateRepoFileSaveActions();
}

bool MainWindow::saveRepoFileEdit(const QString &path, const QString &content,
                                  bool createPull)
{
    if (m_repoDetailIndex < 0 || m_repoDetailIndex >= m_repositories.size())
        return false;
    if (!repoHasWorkingTree()) {
        setRepoDetailNotice("This is a read-only mirror; files can't be edited here.",
                            true);
        return false;
    }
    const QString cleanPath = QDir::cleanPath(path);
    if (cleanPath.isEmpty() || cleanPath.startsWith("../") ||
        cleanPath.contains("/../") || QDir::isAbsolutePath(cleanPath)) {
        setRepoDetailNotice("Refusing to write outside the repository.", true);
        return false;
    }

    const QString dir = repoGitDir();
    QByteArray status;
    QString err;
    if (!runGitCapture(dir, {"status", "--porcelain"}, &status, &err) ||
        !status.trimmed().isEmpty()) {
        setRepoDetailNotice(
            err.isEmpty() ? "Commit or stash local changes before saving a file."
                          : err.left(240),
            true);
        return false;
    }

    const QStringList branches = repoBranches();
    const QString base = repoDefaultBranch(branches);
    if (base.isEmpty()) {
        setRepoDetailNotice("This repository has no branch to commit onto.", true);
        return false;
    }

    QString title = QStringLiteral("Edit %1").arg(cleanPath);
    QString description;
    QString branch;
    if (createPull) {
        QDialog dialog(this);
        dialog.setWindowTitle("Save as pull request");
        auto *branchEdit = new QLineEdit(&dialog);
        branch = QStringLiteral("edit/%1-%2")
                     .arg(repoSegment(cleanPath, QStringLiteral("file")),
                          QString::number(QDateTime::currentSecsSinceEpoch()));
        branchEdit->setText(branch.left(80));
        auto *titleEdit = new QLineEdit(title, &dialog);
        auto *bodyEdit = new QPlainTextEdit(&dialog);
        bodyEdit->setPlaceholderText("Describe the change...");
        auto *form = new QFormLayout;
        form->addRow("Branch", branchEdit);
        form->addRow("Title", titleEdit);
        form->addRow("Description", bodyEdit);
        auto *buttons =
            new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel,
                                 &dialog);
        buttons->button(QDialogButtonBox::Ok)->setText("Create pull request");
        connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
        connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
        auto *layout = new QVBoxLayout(&dialog);
        layout->addLayout(form);
        layout->addWidget(buttons);
        dialog.resize(520, 320);
        if (dialog.exec() != QDialog::Accepted)
            return false;
        branch = branchEdit->text().trimmed();
        title = titleEdit->text().trimmed();
        description = bodyEdit->toPlainText();
        if (branch.isEmpty() || title.isEmpty()) {
            setRepoDetailNotice("A pull request needs a branch and title.", true);
            return false;
        }
    } else {
        bool ok = false;
        title = QInputDialog::getText(this, "Commit file edit", "Commit message:",
                                      QLineEdit::Normal, title, &ok)
                    .trimmed();
        if (!ok)
            return false;
        if (title.isEmpty()) {
            setRepoDetailNotice("A commit message is required.", true);
            return false;
        }
    }

    auto writeEditedFile = [&]() -> bool {
        const QString fullPath = QDir(dir).filePath(cleanPath);
        QDir().mkpath(QFileInfo(fullPath).absolutePath());
        QFile file(fullPath);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
            err = QStringLiteral("Could not write %1").arg(cleanPath);
            return false;
        }
        if (file.write(content.toUtf8()) < 0) {
            err = QStringLiteral("Could not write %1").arg(cleanPath);
            return false;
        }
        return true;
    };

    if (!runGitCapture(dir, {"checkout", base}, nullptr, &err)) {
        setRepoDetailNotice(QStringLiteral("Could not check out %1: %2")
                                .arg(base, err.left(200)),
                            true);
        return false;
    }

    QString commitRef = base;
    if (createPull) {
        if (!runGitCapture(dir, {"check-ref-format", "--branch", branch}, nullptr,
                           &err) ||
            !runGitCapture(dir, {"checkout", "-b", branch, base}, nullptr, &err)) {
            setRepoDetailNotice(QStringLiteral("Could not create branch %1: %2")
                                    .arg(branch, err.left(200)),
                                true);
            runGitCapture(dir, {"checkout", base}, nullptr, nullptr);
            return false;
        }
        commitRef = branch;
    }

    if (!writeEditedFile() ||
        !runGitCapture(dir, {"add", "--", cleanPath}, nullptr, &err)) {
        setRepoDetailNotice(err.isEmpty() ? "Could not stage the edited file."
                                          : err.left(240),
                            true);
        runGitCapture(dir, {"checkout", base}, nullptr, nullptr);
        return false;
    }
    if (runGitCapture(dir, {"diff", "--cached", "--quiet"}, nullptr, nullptr)) {
        setRepoDetailNotice("No changes to save.");
        runGitCapture(dir, {"checkout", base}, nullptr, nullptr);
        return false;
    }
    if (!runGitCapture(dir, {"commit", "-m", title, "--", cleanPath}, nullptr, &err)) {
        setRepoDetailNotice(QStringLiteral("git commit failed: %1").arg(err.left(240)),
                            true);
        runGitCapture(dir, {"checkout", base}, nullptr, nullptr);
        return false;
    }

    if (createPull) {
        QByteArray diff;
        if (!runGitCapture(dir, {"diff", "--binary", base + ".." + branch}, &diff,
                           &err)) {
            setRepoDetailNotice(QStringLiteral("Could not create pull request diff: %1")
                                    .arg(err.left(240)),
                                true);
            runGitCapture(dir, {"checkout", base}, nullptr, nullptr);
            return false;
        }
        runGitCapture(dir, {"checkout", base}, nullptr, nullptr);
        PullStore store = pullStoreForCurrentRepo();
        QString error;
        const int number = store.createPull(title, description, base, branch,
                                            QString::fromUtf8(diff), &error);
        if (number < 0) {
            setRepoDetailNotice(error.isEmpty() ? "Could not create the pull request."
                                                : error,
                                true);
            return false;
        }
        logSystem(QStringLiteral("Saved %1 on %2 and opened pull #%3.")
                      .arg(cleanPath, branch)
                      .arg(number));
        setRepoDetailNotice(QStringLiteral("Opened pull request #%1 from %2.")
                                .arg(number)
                                .arg(branch));
        m_currentPullNumber = number;
        switchToPullTab(number);
    } else {
        logSystem(QStringLiteral("Committed %1 to %2.").arg(cleanPath, commitRef));
        setRepoDetailNotice(QStringLiteral("Committed %1 to %2.")
                                .arg(cleanPath, commitRef));
    }

    setRepoBranch(base);
    // A direct commit shouldn't yank the user out of the file editor back to the
    // overview. Remember which files-panel page they were on and restore it after
    // the refresh (the PR path intentionally navigates to the new pull instead).
    const int filesPage = m_filesStack ? m_filesStack->currentIndex() : 0;
    refreshOpenRepoDetail();
    if (!createPull && m_filesStack)
        m_filesStack->setCurrentIndex(filesPage);
    return true;
}

void MainWindow::showRepoOverview()
{
    if (m_filesStack)
        m_filesStack->setCurrentIndex(0);
}

void MainWindow::loadRepoOverview(const QString &path)
{
    if (!m_overviewList)
        return;
    m_overviewPath = path;
    if (m_filesStack)
        m_filesStack->setCurrentIndex(0);
    m_overviewList->clear();
    if (m_readmeView)
        m_readmeView->clear();

    const QString dir = repoGitDir();

    // Latest commit strip: "<subject> · <author> committed <relative time>".
    if (m_commitBar) {
        QByteArray logOut;
        QStringList logArgs{"log", "-1", "--format=%H%x1f%an%x1f%ar%x1f%s",
                            currentRef()};
        if (!path.isEmpty())
            logArgs << "--" << path;
        if (!dir.isEmpty() && runGitCapture(dir, logArgs, &logOut, nullptr) &&
            !logOut.trimmed().isEmpty()) {
            const QStringList f = QString::fromUtf8(logOut).trimmed().split('\x1f');
            const QString fullHash = f.value(0);
            const QString author = f.value(1);
            const QString when = f.value(2);
            const QString subject = f.value(3);
            // Latest commit: subject, author and "x ago", plus the action/check
            // status glyph for this (the first/most-recent) commit.
            m_commitBar->setText(
                QStringLiteral("%1<b>%2</b> &nbsp; <span style='color:#8b949e'>%3 "
                               "committed %4</span>")
                    .arg(commitStatusGlyph(fullHash),
                         subject.toHtmlEscaped(), author.toHtmlEscaped(),
                         when.toHtmlEscaped()));
        } else {
            m_commitBar->setText("<span style='color:#8b949e'>No commits yet</span>");
        }
    }
    if (m_historyButton) {
        QByteArray countOut;
        QString count;
        if (!dir.isEmpty() &&
            runGitCapture(dir, {"rev-list", "--count", currentRef()}, &countOut, nullptr))
            count = QString::fromUtf8(countOut).trimmed();
        m_historyButton->setText(count.isEmpty()
                                     ? QStringLiteral("Commits")
                                     : QStringLiteral("%1 Commits").arg(count));
    }

    // Breadcrumb for directory navigation.
    if (m_overviewCrumb) {
        QString crumb = QStringLiteral("<a href=\"/\">root</a>");
        QString acc;
        for (const QString &part : path.split('/', Qt::SkipEmptyParts)) {
            acc = acc.isEmpty() ? part : acc + "/" + part;
            crumb += " / <a href=\"" + acc.toHtmlEscaped() + "\">" +
                     part.toHtmlEscaped() + "</a>";
        }
        m_overviewCrumb->setText(crumb);
    }

    if (dir.isEmpty()) {
        new QListWidgetItem("No local copy of this repository to browse.",
                            m_overviewList);
        return;
    }

    const QString treeish = path.isEmpty() ? currentRef() : currentRef() + ":" + path;
    QByteArray out;
    QString err;
    if (!runGitCapture(dir, {"ls-tree", "-z", treeish}, &out, &err)) {
        new QListWidgetItem(err.isEmpty() ? "This repository has no commits yet."
                                          : "Could not read files: " + err.left(120),
                            m_overviewList);
        return;
    }

    struct Entry {
        QString name;
        QString path;
        bool isDir;
    };
    QList<Entry> entries;
    QString readmePath;
    for (const QByteArray &record : out.split('\0')) {
        if (record.isEmpty())
            continue;
        const int tab = record.indexOf('\t');
        if (tab < 0)
            continue;
        const QStringList meta =
            QString::fromUtf8(record.left(tab)).split(' ', Qt::SkipEmptyParts);
        if (meta.size() < 2)
            continue;
        Entry e;
        e.name = QString::fromUtf8(record.mid(tab + 1));
        e.isDir = meta.at(1) == "tree";
        e.path = path.isEmpty() ? e.name : path + "/" + e.name;
        entries.append(e);
        if (!e.isDir && e.name.compare("README.md", Qt::CaseInsensitive) == 0)
            readmePath = e.path;
    }
    std::sort(entries.begin(), entries.end(), [](const Entry &a, const Entry &b) {
        if (a.isDir != b.isDir)
            return a.isDir;
        return a.name.toLower() < b.name.toLower();
    });
    if (!path.isEmpty()) {
        auto *up = new QListWidgetItem(iconForDir(false), "..", m_overviewList);
        const int cut = path.lastIndexOf('/');
        up->setData(Qt::UserRole, cut < 0 ? QString() : path.left(cut));
        up->setData(Qt::UserRole + 1, true);
    }
    for (const Entry &e : std::as_const(entries)) {
        auto *item = new QListWidgetItem(
            e.isDir ? iconForDir(false) : iconForFile(e.name), e.name, m_overviewList);
        item->setData(Qt::UserRole, e.path);
        item->setData(Qt::UserRole + 1, e.isDir);
    }

    // Render the directory's README beneath the file list (GitHub-style).
    if (m_readmeView && !readmePath.isEmpty()) {
        QByteArray readme;
        if (runGitCapture(dir, {"show", currentRef() + ":" + readmePath}, &readme,
                          nullptr) &&
            !readme.contains('\0'))
            m_readmeView->setMarkdown(QString::fromUtf8(readme));
    }
}

QString MainWindow::currentRef() const
{
    return m_repoBranch.isEmpty() ? QStringLiteral("HEAD") : m_repoBranch;
}

void MainWindow::loadCommits()
{
    if (!m_commitsTable)
        return;
    QSignalBlocker block(m_commitsTable);
    m_commitsTable->setSortingEnabled(false);
    m_commitsTable->setRowCount(0);
    showCommitList(); // always land on the list when (re)loading
    updateRepoCommitCount();
    const QString dir = repoGitDir();
    if (dir.isEmpty()) {
        m_commitsTable->setSortingEnabled(true);
        return;
    }
    QByteArray out;
    if (!runGitCapture(dir,
                       {"log", "--numstat",
                        "--format=%x1e%H%x1f%h%x1f%an%x1f%ar%x1f%ct%x1f%s",
                        "-n", "300", currentRef()},
                       &out, nullptr)) {
        m_commitsTable->setSortingEnabled(true);
        return;
    }
    for (const QByteArray &record : out.split('\x1e')) {
        if (record.trimmed().isEmpty())
            continue;
        const QStringList lines =
            QString::fromUtf8(record).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
        if (lines.isEmpty())
            continue;
        const QStringList f = lines.first().split(QLatin1Char('\x1f'));
        if (f.size() < 6)
            continue;
        int files = 0;
        int adds = 0;
        int dels = 0;
        for (int i = 1; i < lines.size(); ++i) {
            const QStringList stats = lines.at(i).split(QLatin1Char('\t'));
            if (stats.size() < 3)
                continue;
            ++files;
            bool ok = false;
            const int addCount = stats.at(0).toInt(&ok);
            if (ok)
                adds += addCount;
            const int delCount = stats.at(1).toInt(&ok);
            if (ok)
                dels += delCount;
        }

        const int row = m_commitsTable->rowCount();
        m_commitsTable->insertRow(row);
        auto *summary = new SortTableWidgetItem(f.at(5));
        summary->setData(Qt::UserRole, f.at(0));
        summary->setData(kTableSortRole, f.at(5).toLower());
        // Action/check status badge for this commit (green check / red x /
        // spinning-blue dot), shown as a leading icon when a workflow ran for it.
        switch (commitStatusCode(f.at(0))) {
        case 1:
            summary->setIcon(themedOcticon("check-circle", QColor("#3fb950"), 14));
            summary->setToolTip(QStringLiteral("Checks passed \xC2\xB7 %1").arg(f.at(1)));
            break;
        case 2:
            summary->setIcon(themedOcticon("x", QColor("#f85149"), 14));
            summary->setToolTip(QStringLiteral("Checks failed \xC2\xB7 %1").arg(f.at(1)));
            break;
        case 3:
            summary->setIcon(themedOcticon("sync", QColor("#58a6ff"), 14));
            summary->setToolTip(QStringLiteral("Checks running \xC2\xB7 %1").arg(f.at(1)));
            break;
        default:
            summary->setToolTip(
                QStringLiteral("Click to view the diff for %1").arg(f.at(1)));
            break;
        }
        // Summary (with the commit hash on UserRole) sits last; the metadata
        // columns are to its left.
        m_commitsTable->setItem(row, kCommitSummaryCol, summary);

        auto *author = new SortTableWidgetItem(f.at(2));
        author->setData(kTableSortRole, f.at(2).toLower());
        m_commitsTable->setItem(row, 0, author);
        const qint64 commitTs = f.at(4).toLongLong();
        auto *date = new SortTableWidgetItem(formatShortRelativeTime(commitTs));
        date->setData(kTableSortRole, commitTs);
        date->setToolTip(f.at(3)); // full "x ago" form on hover
        m_commitsTable->setItem(row, 1, date);
        auto *fileItem = new SortTableWidgetItem(QString::number(files));
        fileItem->setData(kTableSortRole, files);
        m_commitsTable->setItem(row, 2, fileItem);
        auto *addsItem = new SortTableWidgetItem(QStringLiteral("+%1").arg(adds));
        addsItem->setForeground(QColor("#2ea043"));
        addsItem->setData(kTableSortRole, adds);
        m_commitsTable->setItem(row, 3, addsItem);
        auto *delsItem =
            new SortTableWidgetItem(QString::fromUtf8("\xE2\x88\x92%1").arg(dels));
        delsItem->setForeground(QColor("#f85149"));
        delsItem->setData(kTableSortRole, dels);
        m_commitsTable->setItem(row, 4, delsItem);
    }
    m_commitsTable->setSortingEnabled(true);

    // Honour "closes #N" / "fixes #N" / "resolves #N" in commit messages by
    // closing and annotating the referenced issues (idempotent).
    applyCommitIssueClosures();
}

void MainWindow::showCommitList()
{
    if (m_commitsStack)
        m_commitsStack->setCurrentIndex(0);
}

void MainWindow::downloadCommitPatch()
{
    const QString dir = repoGitDir();
    if (dir.isEmpty() || m_currentCommitHash.isEmpty())
        return;
    // format-patch produces a self-describing patch (author, message, diff) that
    // is still apply-able via git apply, so it round-trips through Import as PR.
    QByteArray patch;
    QString err;
    if (!runGitCapture(dir,
                       {"format-patch", "-1", "--stdout", m_currentCommitHash},
                       &patch, &err) ||
        patch.trimmed().isEmpty()) {
        setRepoDetailNotice(err.isEmpty() ? "Could not generate the patch." : err, true);
        return;
    }
    const QString shortHash = m_currentCommitHash.left(8);
    const QString suggested =
        QDir::homePath() + "/" + shortHash + QStringLiteral(".patch");
    const QString path = QFileDialog::getSaveFileName(
        this, "Download patch", suggested, "Patch files (*.patch *.diff)");
    if (path.isEmpty())
        return;
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(patch) < 0) {
        setRepoDetailNotice("Could not write the patch file.", true);
        return;
    }
    file.close();
    setRepoDetailNotice(QStringLiteral("Saved patch to %1.").arg(path));
    logSystem(QStringLiteral("Saved commit %1 as patch %2.").arg(shortHash, path));
}

void MainWindow::applyCommitIssueClosures()
{
    // Closing an issue authors signed events into the repo's issues/ folder, so
    // it requires a real working tree on this node. Mirror-only repos are
    // read-only here and are skipped.
    if (m_repoDetailIndex < 0 || m_repoDetailIndex >= m_repositories.size())
        return;
    IssueStore store = issueStoreForCurrentRepo();
    if (!store.canWrite())
        return;
    const QString dir = repoGitDir();
    if (dir.isEmpty())
        return;

    // Full commit messages so we catch closing keywords in the body, not just
    // the subject. Records separated by RS (0x1e); fields by US (0x1f).
    QByteArray out;
    if (!runGitCapture(dir,
                       {"log", "--format=%h%x1f%s%x1f%B%x1e", "-n", "500",
                        currentRef()},
                       &out, nullptr))
        return;

    QList<Issue> issues = store.loadAll();
    QHash<int, const Issue *> byNumber;
    for (const Issue &issue : issues)
        byNumber.insert(issue.number, &issue);

    // GitHub-style closing keywords followed by #<number>.
    static const QRegularExpression closeRe(
        QStringLiteral("\\b(?:close[sd]?|fix(?:e[sd])?|resolve[sd]?)\\b\\s*:?\\s*"
                       "#(\\d+)"),
        QRegularExpression::CaseInsensitiveOption);

    int closedCount = 0;
    QString lastClosed;
    for (const QByteArray &record : out.split('\x1e')) {
        if (record.trimmed().isEmpty())
            continue;
        const QStringList f = QString::fromUtf8(record).split('\x1f');
        if (f.size() < 3)
            continue;
        const QString shortHash = f.at(0).trimmed();
        const QString subject = f.at(1).trimmed();
        const QString message = f.at(2);

        auto it = closeRe.globalMatch(message);
        QSet<int> seenInThisCommit; // a commit may name the same issue twice
        while (it.hasNext()) {
            const int number = it.next().captured(1).toInt();
            if (number <= 0 || seenInThisCommit.contains(number))
                continue;
            seenInThisCommit.insert(number);

            const Issue *issue = byNumber.value(number, nullptr);
            if (!issue || issue->status == QLatin1String("closed"))
                continue;

            // Idempotency: skip if a comment already records this commit closing
            // the issue (so reloading the commits tab doesn't re-comment).
            bool alreadyLinked = false;
            for (const IssueEvent &ev : issue->events) {
                if (ev.type == QLatin1String("comment") &&
                    ev.body.contains(shortHash)) {
                    alreadyLinked = true;
                    break;
                }
            }
            if (alreadyLinked)
                continue;

            QString err;
            const QString note =
                QStringLiteral("Closed by commit `%1` \xE2\x80\x94 %2")
                    .arg(shortHash, subject);
            if (!store.addComment(number, note, {}, &err)) {
                logSystem(QStringLiteral("Issue #%1: could not link commit %2: %3")
                              .arg(number)
                              .arg(shortHash, err));
                continue;
            }
            if (!store.setStatus(number, QStringLiteral("closed"), &err)) {
                logSystem(QStringLiteral("Issue #%1: could not close: %2")
                              .arg(number)
                              .arg(err));
                continue;
            }
            logSystem(QStringLiteral("Closed issue #%1 via commit %2.")
                          .arg(number)
                          .arg(shortHash));
            ++closedCount;
            lastClosed = QStringLiteral("#%1").arg(number);
            // The store mutated on disk; refresh our snapshot so a later commit
            // in the same pass sees the updated status.
            issues = store.loadAll();
            byNumber.clear();
            for (const Issue &i : issues)
                byNumber.insert(i.number, &i);
        }
    }

    if (closedCount > 0) {
        flashMessage(closedCount == 1
                         ? QStringLiteral("Closed issue %1 from commit message.")
                               .arg(lastClosed)
                         : QStringLiteral("Closed %1 issues from commit messages.")
                               .arg(closedCount));
        reloadIssues();
        updateRepoIssueCount();
    }
}

namespace {

struct DiffFileEntry {
    QString path;
    QString anchor;
    int adds = 0;
    int dels = 0;
    QString status = QStringLiteral("modified"); // added/deleted/modified/renamed
};

QString diffImageMimeForPath(const QString &path)
{
    const QString lower = path.toLower();
    if (lower.endsWith(QStringLiteral(".png")))
        return QStringLiteral("image/png");
    if (lower.endsWith(QStringLiteral(".jpg")) || lower.endsWith(QStringLiteral(".jpeg")))
        return QStringLiteral("image/jpeg");
    if (lower.endsWith(QStringLiteral(".gif")))
        return QStringLiteral("image/gif");
    if (lower.endsWith(QStringLiteral(".webp")))
        return QStringLiteral("image/webp");
    if (lower.endsWith(QStringLiteral(".bmp")))
        return QStringLiteral("image/bmp");
    if (lower.endsWith(QStringLiteral(".ico")))
        return QStringLiteral("image/x-icon");
    return QString();
}

QString diffImageCellHtml(const QString &label, const QString &path,
                          const QString &mime, const QByteArray &bytes)
{
    const QString caption =
        QStringLiteral("<div class='imgcaption'>%1</div>").arg(label);
    if (bytes.isEmpty())
        return QStringLiteral("<td class='imgcell'><div class='imgempty'>Not "
                              "present</div>%1</td>")
            .arg(caption);
    return QStringLiteral("<td class='imgcell'><img alt=\"%1: %2\" src=\"data:%3;"
                          "base64,%4\">%5</td>")
        .arg(label.toHtmlEscaped(), path.toHtmlEscaped(), mime,
             QString::fromLatin1(bytes.toBase64()), caption);
}

QString diffImagePreviewHtml(const QString &dir, const QString &base,
                             const QString &head, const QString &path)
{
    constexpr int kMaxInlineImageBytes = 512 * 1024;
    const QString mime = diffImageMimeForPath(path);
    if (mime.isEmpty())
        return QString();

    QByteArray oldBytes;
    if (!runGitCapture(dir, {"show", base + ":" + path}, &oldBytes, nullptr) ||
        oldBytes.size() > kMaxInlineImageBytes)
        oldBytes.clear();
    QByteArray newBytes;
    if (!runGitCapture(dir, {"show", head + ":" + path}, &newBytes, nullptr) ||
        newBytes.size() > kMaxInlineImageBytes)
        newBytes.clear();
    if (oldBytes.isEmpty() && newBytes.isEmpty())
        return QString();

    return QStringLiteral("<table class='imagetable' width='100%' cellspacing='0' "
                          "cellpadding='0'><tr>%1%2</tr></table>")
        .arg(diffImageCellHtml(QStringLiteral("Before"), path, mime, oldBytes),
             diffImageCellHtml(QStringLiteral("After"), path, mime, newBytes));
}

// Render a unified diff into an HTML table with an old/new line-number gutter
// and +/- coloring (classes styled by the document stylesheet), one block per
// file with a named anchor so the file list can scroll to it.
QString renderUnifiedDiffHtml(const QString &patch, QList<DiffFileEntry> &files,
                              const QString &dir, const QString &base,
                              const QString &head)
{
    static const QRegularExpression hunkRe(
        QStringLiteral("@@ -(\\d+)(?:,\\d+)? \\+(\\d+)(?:,\\d+)? @@"));
    QString html;
    html.reserve(patch.size() * 3); // avoid repeated reallocation on big diffs
    QString fileBody;
    const QStringList lines = patch.split(QLatin1Char('\n'));
    int oldNo = 0, newNo = 0, fileIdx = -1;
    bool inFile = false;

    // Per-file header is emitted lazily: we buffer the rows so the header can
    // report final +/- counts (read from the hunks), then prepend the styled
    // header block before the table.
    auto emitFileHeader = [&](int idx) {
        const DiffFileEntry &f = files[idx];
        QString badgeClass = QStringLiteral("st-mod");
        QString badgeText = QStringLiteral("MODIFIED");
        if (f.status == QLatin1String("added")) {
            badgeClass = QStringLiteral("st-add");
            badgeText = QStringLiteral("ADDED");
        } else if (f.status == QLatin1String("deleted")) {
            badgeClass = QStringLiteral("st-del");
            badgeText = QStringLiteral("DELETED");
        } else if (f.status == QLatin1String("renamed")) {
            badgeClass = QStringLiteral("st-ren");
            badgeText = QStringLiteral("RENAMED");
        }
        const QString imagePreview = diffImagePreviewHtml(dir, base, head, f.path);
        html += QStringLiteral(
                    "<a name=\"%1\"></a><div class='fileblock'>"
                    "<div class='fileheader'>"
                    "<span class='stbadge %2'>%3</span>"
                    "<span class='fpath'>%4</span>"
                    "<span class='fstat'><span class='sadd'>+%5</span> "
                    "<span class='sdel'>\xE2\x88\x92%6</span></span></div>"
                    "%7<table class='difftable' cellspacing='0' cellpadding='0'>")
                    .arg(f.anchor, badgeClass, badgeText, f.path.toHtmlEscaped(),
                         QString::number(f.adds), QString::number(f.dels),
                         imagePreview);
    };
    auto closeFile = [&] {
        if (inFile) {
            emitFileHeader(fileIdx);
            html += fileBody;
            html += QStringLiteral("</table></div>");
            fileBody.clear();
            inFile = false;
        }
    };

    for (const QString &line : lines) {
        if (line.startsWith(QLatin1String("diff --git "))) {
            closeFile();
            QString path = line;
            const int bpos = line.indexOf(QLatin1String(" b/"));
            if (bpos >= 0)
                path = line.mid(bpos + 3);
            DiffFileEntry f;
            f.path = path;
            f.anchor = QStringLiteral("file-%1").arg(files.size());
            files.append(f);
            fileIdx = files.size() - 1;
            inFile = true;
            continue;
        }
        if (!inFile)
            continue;
        // Status detection (header lines come before the first hunk).
        if (fileIdx >= 0) {
            if (line.startsWith(QLatin1String("new file")))
                files[fileIdx].status = QStringLiteral("added");
            else if (line.startsWith(QLatin1String("deleted file")))
                files[fileIdx].status = QStringLiteral("deleted");
            else if (line.startsWith(QLatin1String("rename ")) ||
                     line.startsWith(QLatin1String("similarity ")))
                files[fileIdx].status = QStringLiteral("renamed");
        }
        if (line.startsWith(QLatin1String("index ")) ||
            line.startsWith(QLatin1String("--- ")) ||
            line.startsWith(QLatin1String("+++ ")) ||
            line.startsWith(QLatin1String("new file")) ||
            line.startsWith(QLatin1String("deleted file")) ||
            line.startsWith(QLatin1String("similarity ")) ||
            line.startsWith(QLatin1String("rename ")) ||
            line.startsWith(QLatin1String("old mode")) ||
            line.startsWith(QLatin1String("new mode")))
            continue;
        if (line.startsWith(QLatin1String("@@"))) {
            const QRegularExpressionMatch m = hunkRe.match(line);
            if (m.hasMatch()) {
                oldNo = m.captured(1).toInt();
                newNo = m.captured(2).toInt();
            }
            fileBody += QStringLiteral(
                            "<tr><td class='ln hunk'></td><td class='ln hunk'></td>"
                            "<td class='code hunk'>%1</td></tr>")
                            .arg(line.toHtmlEscaped());
            continue;
        }

        const QChar c0 = line.isEmpty() ? QLatin1Char(' ') : line.at(0);
        QString text = line.isEmpty() ? QString() : line.mid(1);
        QString cls, oldCell, newCell;
        if (c0 == QLatin1Char('+')) {
            cls = QStringLiteral("add");
            newCell = QString::number(newNo++);
            if (fileIdx >= 0)
                ++files[fileIdx].adds;
        } else if (c0 == QLatin1Char('-')) {
            cls = QStringLiteral("del");
            oldCell = QString::number(oldNo++);
            if (fileIdx >= 0)
                ++files[fileIdx].dels;
        } else if (c0 == QLatin1Char('\\')) { // "\ No newline at end of file"
            cls = QStringLiteral("ctx");
            text = line;
        } else {
            cls = QStringLiteral("ctx");
            oldCell = QString::number(oldNo++);
            newCell = QString::number(newNo++);
        }
        fileBody += QStringLiteral("<tr><td class='ln %1'>%2</td>"
                                   "<td class='ln %1'>%3</td>"
                                   "<td class='code %1'>%4</td></tr>")
                        .arg(cls, oldCell, newCell,
                             text.isEmpty() ? QStringLiteral("&nbsp;")
                                            : text.toHtmlEscaped());
    }
    closeFile();
    return html;
}

} // namespace

void MainWindow::showCommit(const QString &hash)
{
    const QString dir = repoGitDir();
    if (dir.isEmpty() || hash.isEmpty() || !m_commitsStack)
        return;

    // Track this commit's position so Prev/Next can walk the list, and reflect
    // the available directions on the buttons.
    m_currentCommitRow = -1;
    if (m_commitsTable)
        for (int i = 0; i < m_commitsTable->rowCount(); ++i)
            if (m_commitsTable->item(i, kCommitSummaryCol)
                    ->data(Qt::UserRole)
                    .toString() == hash) {
                m_currentCommitRow = i;
                break;
            }
    if (m_commitPrevButton)
        m_commitPrevButton->setEnabled(m_currentCommitRow > 0);
    if (m_commitNextButton)
        m_commitNextButton->setEnabled(m_commitsTable &&
                                       m_currentCommitRow >= 0 &&
                                       m_currentCommitRow < m_commitsTable->rowCount() - 1);
    // Keep the table highlight in sync so the selected row follows Prev/Next.
    if (m_commitsTable && m_currentCommitRow >= 0) {
        QSignalBlocker blk(m_commitsTable);
        m_commitsTable->selectRow(m_currentCommitRow);
    }

    // --- Metadata (full hash, author, date, parents, subject, body).
    QByteArray meta;
    runGitCapture(dir,
                  {"show", "-s", "--date=format:%b %e, %Y",
                   "--format=%H%x1f%an%x1f%ad%x1f%P%x1f%s%x1f%b", hash},
                  &meta, nullptr);
    const QStringList mf = QString::fromUtf8(meta).split(QLatin1Char('\x1f'));
    const QString full = mf.value(0).trimmed();
    const QString author = mf.value(1).trimmed();
    const QString date = mf.value(2).trimmed();
    const QStringList parents =
        mf.value(3).trimmed().split(QLatin1Char(' '), Qt::SkipEmptyParts);
    const QString subject = mf.value(4).trimmed();
    const QString body = mf.value(5).trimmed();
    const QString shortHash = full.isEmpty() ? hash : full.left(7);

    // Diff against the first parent (or the empty tree for a root commit), which
    // matches how a commit page presents merges and initial commits.
    const QString emptyTree =
        QStringLiteral("4b825dc642cb6eb9a060e54bf8d69288fbee4904");
    const QString base = parents.isEmpty() ? emptyTree : parents.first();
    m_currentCommitHash = full.isEmpty() ? hash : full;
    if (m_commitDownloadButton)
        m_commitDownloadButton->setEnabled(true);
    QByteArray patchRaw;
    runGitCapture(dir, {"diff", "-M", base, full.isEmpty() ? hash : full},
                  &patchRaw, nullptr);

    // --- Header labels.
    if (m_commitTitle)
        m_commitTitle->setText(
            QStringLiteral("Commit <code>%1</code>").arg(shortHash.toHtmlEscaped()));
    if (m_commitMessage) {
        QString msg = QStringLiteral("<b>%1</b>").arg(subject.toHtmlEscaped());
        if (!body.isEmpty())
            msg += QStringLiteral(
                       "<br><span style='color:#8b949e; white-space:pre-wrap'>%1</span>")
                       .arg(body.toHtmlEscaped());
        m_commitMessage->setText(msg);
    }

    // --- Render the diff and collect per-file stats.
    QList<DiffFileEntry> files;
    const QString diffHtml =
        renderUnifiedDiffHtml(QString::fromUtf8(patchRaw), files, dir, base,
                              full.isEmpty() ? hash : full);
    int totalAdds = 0, totalDels = 0;
    for (const DiffFileEntry &f : files) {
        totalAdds += f.adds;
        totalDels += f.dels;
    }

    if (m_commitMeta)
        m_commitMeta->setText(
            QString::fromUtf8("%1 committed on %2 \xC2\xB7 %3 parent%4 \xC2\xB7 "
                           "<b>%5</b> file%6 changed "
                           "<span style='color:#3fb950'>+%7</span> "
                           "<span style='color:#f85149'>\xE2\x88\x92%8</span>")
                .arg(author.toHtmlEscaped(), date.toHtmlEscaped(),
                     QString::number(qMax(1, parents.size())),
                     parents.size() == 1 ? "" : "s", QString::number(files.size()),
                     files.size() == 1 ? "" : "s", QString::number(totalAdds),
                     QString::number(totalDels)));

    if (m_commitFilesSummary)
        m_commitFilesSummary->setText(
            QStringLiteral("%1 file%2 changed")
                .arg(files.size())
                .arg(files.size() == 1 ? "" : "s"));

    // --- Left file list (click scrolls the diff to that file).
    if (m_commitFileList) {
        QSignalBlocker block(m_commitFileList);
        m_commitFileList->clear();
        for (const DiffFileEntry &f : files) {
            // Show the basename prominently with the +/- counts; full path on
            // hover. A status-coloured octicon leads each row.
            const QString name = f.path.section(QLatin1Char('/'), -1);
            auto *item = new QListWidgetItem(
                QString::fromUtf8("%1   +%2 \xE2\x88\x92%3")
                    .arg(name, QString::number(f.adds), QString::number(f.dels)));
            QString icon = "file-diff";
            QColor tint("#d29922"); // modified
            if (f.status == QLatin1String("added")) {
                icon = "diff";
                tint = QColor("#3fb950");
            } else if (f.status == QLatin1String("deleted")) {
                icon = "trash";
                tint = QColor("#f85149");
            } else if (f.status == QLatin1String("renamed")) {
                icon = "file-diff";
                tint = QColor("#58a6ff");
            }
            item->setIcon(themedOcticon(icon, tint, 14));
            item->setData(Qt::UserRole, f.anchor);
            item->setToolTip(QStringLiteral("%1 \xC2\xB7 %2").arg(f.status, f.path));
            m_commitFileList->addItem(item);
        }
    }

    // --- Theme-aware diff styling, then the rendered HTML.
    if (m_commitDiffView) {
        const bool dark =
            qApp->palette().color(QPalette::Base).lightness() < 128;
        const QString addBg = dark ? "#12261c" : "#e6ffec";
        const QString delBg = dark ? "#2d1416" : "#ffebe9";
        const QString hunkBg = dark ? "#0d1d33" : "#ddf4ff";
        const QString hunkFg = dark ? "#58a6ff" : "#0969da";
        const QString lnFg = "#8b949e";
        const QString headBg = dark ? "#161b22" : "#f6f8fa";
        const QString border = dark ? "#30363d" : "#d0d7de";
        const QString gutterBg = dark ? "#0d1117" : "#f6f8fa";
        const QString fg = dark ? "#e6edf3" : "#1f2328";
        const QString css =
            QStringLiteral(
                ".fileblock { margin-bottom:18px; }"
                ".fileheader { background:%1; padding:8px 12px; font-family:"
                "monospace; border:1px solid %7; }"
                // Status word + path + counts on the header line.
                ".stbadge { font-weight:700; font-size:10px; margin-right:10px; }"
                ".st-add { color:#3fb950; } .st-del { color:#f85149; }"
                ".st-mod { color:#d29922; } .st-ren { color:#58a6ff; }"
                ".fpath { font-weight:600; color:%8; }"
                ".fstat { color:%2; font-size:11px; }"
                ".sadd { color:#3fb950; font-weight:700; }"
                ".sdel { color:#f85149; font-weight:700; }"
                ".difftable { font-family:monospace; font-size:12px; width:100%; }"
                ".imagetable { border-left:1px solid %7; border-right:1px solid %7; }"
                ".imgcell { width:50%; padding:10px; text-align:center; }"
                ".imgcell img { max-width:100%; max-height:360px; }"
                ".imgempty { color:%2; padding:60px 0; border:1px solid %7; }"
                ".imgcaption { color:%2; font-size:12px; margin-top:6px; }"
                // Two equal-width line-number gutters, right-aligned, with a
                // separator rule so old/new numbers line up evenly on each side.
                "td.ln { color:%2; text-align:right; padding:0 10px; width:1%; "
                "white-space:nowrap; background:%9; border-right:1px solid %7; }"
                "td.code { white-space:pre; padding:0 10px; color:%8; }"
                ".add { background:%3; } .del { background:%4; }"
                ".hunk { color:%5; background:%6; }"
                "td.ln.hunk { background:%6; border-right:1px solid %7; }")
                .arg(headBg, lnFg, addBg, delBg, hunkFg, hunkBg, border, fg, gutterBg);
        m_commitDiffView->document()->setDefaultStyleSheet(css);
        m_commitDiffView->setHtml(diffHtml.isEmpty()
                                      ? QStringLiteral("<p style='color:#8b949e'>"
                                                       "No changes in this commit.</p>")
                                      : diffHtml);
    }

    m_commitsStack->setCurrentIndex(1);
}

void MainWindow::loadRepoInsights()
{
    if (!m_insightsSummary)
        return;

    if (m_insightsContributors)
        m_insightsContributors->setRowCount(0);
    if (m_insightsRecentCommits)
        m_insightsRecentCommits->setRowCount(0);
    if (m_insightsLanguageBar)
        m_insightsLanguageBar->clear();
    if (m_insightsLanguageLegend)
        m_insightsLanguageLegend->clear();
    if (m_insightsActivity)
        m_insightsActivity->clear();

    auto setNoRepo = [this] {
        m_insightsSummary->setText(
            "<b>Repository summary</b><br><span style='color:#8b949e'>Select a "
            "repository with a local checkout or mirror to see insights.</span>");
        if (m_insightsTraffic)
            m_insightsTraffic->setText(
                "<b>ForkMesh traffic</b><br><span style='color:#8b949e'>No "
                "repository selected.</span>");
        if (m_insightsLanguageLegend)
            m_insightsLanguageLegend->setText(
                "<span style='color:#8b949e'>No language data available.</span>");
    };

    if (m_repoDetailIndex < 0 || m_repoDetailIndex >= m_repositories.size()) {
        setNoRepo();
        return;
    }

    const RepositoryRecord &repo = m_repositories.at(m_repoDetailIndex);
    const QString repoKey = repo.owner + "/" + repo.name;
    const QString dir = repoGitDir();
    const QString ref = currentRef();
    QStringList notes;

    QString sourceKind = QStringLiteral("unavailable");
    if (!repo.localPath.isEmpty() && QDir(repo.localPath).exists())
        sourceKind = QStringLiteral("local worktree");
    else if (repo.previewOnly && !repo.mirrorPath.isEmpty() &&
             QDir(repo.mirrorPath).exists())
        sourceKind = QStringLiteral("preview cache");
    else if (!repo.mirrorPath.isEmpty() && QDir(repo.mirrorPath).exists())
        sourceKind = QStringLiteral("bare mirror");

    notes << QStringLiteral("Data source: %1. Selected ref: %2.")
                 .arg(sourceKind, ref);
    if (!repositorySource(repo).isEmpty())
        notes << QStringLiteral("Source: %1.").arg(repositorySource(repo));
    if (repo.lastSyncMs > 0)
        notes << QStringLiteral("Last sync: %1.").arg(formatRepoDate(repo.lastSyncMs));
    if (repo.publishedAtMs > 0)
        notes << QStringLiteral("Published: %1.").arg(formatRepoDate(repo.publishedAtMs));

    QString commitCountText = QStringLiteral("0");
    qint64 fileCount = 0;
    qint64 totalBytes = 0;
    QHash<QString, qint64> bytesByLanguage;
    qint64 recognizedBytes = 0;

    if (dir.isEmpty()) {
        notes << QStringLiteral("No local checkout or mirror is available for Git history.");
    } else {
        QByteArray countOut;
        QString countErr;
        if (runGitCapture(dir, {"rev-list", "--count", ref}, &countOut, &countErr)) {
            commitCountText = QString::fromUtf8(countOut).trimmed();
            if (commitCountText.isEmpty())
                commitCountText = QStringLiteral("0");
        } else {
            notes << QStringLiteral("Commit count unavailable: %1.")
                         .arg(countErr.isEmpty() ? QStringLiteral("git failed") : countErr.left(160));
        }

        QByteArray treeOut;
        QString treeErr;
        if (runGitCapture(dir, {"ls-tree", "-r", "-l", ref}, &treeOut, &treeErr)) {
            for (const QByteArray &record : treeOut.split('\n')) {
                if (record.trimmed().isEmpty())
                    continue;
                const int tab = record.indexOf('\t');
                if (tab < 0)
                    continue;
                const QList<QByteArray> meta = record.left(tab).simplified().split(' ');
                if (meta.size() < 4 || meta.at(1) != "blob")
                    continue;
                ++fileCount;
                bool ok = false;
                const qint64 size = QString::fromUtf8(meta.at(3)).toLongLong(&ok);
                if (!ok)
                    continue;
                totalBytes += qMax<qint64>(0, size);
                const QString name = QString::fromUtf8(record.mid(tab + 1));
                const QString language = languageForFile(name);
                if (!language.isEmpty() && size > 0) {
                    bytesByLanguage[language] += size;
                    recognizedBytes += size;
                }
            }
        } else {
            notes << QStringLiteral("File composition unavailable: %1.")
                         .arg(treeErr.isEmpty() ? QStringLiteral("git failed") : treeErr.left(160));
        }
    }

    int openIssues = 0;
    int closedIssues = 0;
    for (const Issue &issue : std::as_const(m_currentIssues)) {
        if (issue.isDeleted())
            continue;
        if (issue.status == QStringLiteral("closed"))
            ++closedIssues;
        else
            ++openIssues;
    }

    QString pullError;
    const QList<PullRequest> pulls = pullStoreForCurrentRepo().loadAll(&pullError);
    int openPulls = 0;
    int mergedPulls = 0;
    int closedPulls = 0;
    for (const PullRequest &pr : pulls) {
        if (pr.status == QStringLiteral("merged"))
            ++mergedPulls;
        else if (pr.status == QStringLiteral("closed"))
            ++closedPulls;
        else
            ++openPulls;
    }
    if (!pullError.isEmpty())
        notes << QStringLiteral("Pull request data unavailable: %1.").arg(pullError.left(160));

    const int totalIssues = openIssues + closedIssues;
    const int totalPulls = openPulls + mergedPulls + closedPulls;
    m_insightsSummary->setText(
        "<b>Repository summary</b>" +
        insightMetricsTable({
            insightMetricCell("Commits", commitCountText, ref),
            insightMetricCell("Contributors", QStringLiteral("0"), "all branches"),
            insightMetricCell("Files", QString::number(fileCount), "tracked blobs"),
            insightMetricCell("Code size", formatInsightBytes(totalBytes), "tracked bytes"),
            insightMetricCell("Issues", QString::number(totalIssues),
                              QStringLiteral("%1 open / %2 closed")
                                  .arg(openIssues)
                                  .arg(closedIssues)),
            insightMetricCell("Pull requests", QString::number(totalPulls),
                              QStringLiteral("%1 open / %2 merged / %3 closed")
                                  .arg(openPulls)
                                  .arg(mergedPulls)
                                  .arg(closedPulls)),
        }));

    const QPair<int, int> stats = m_repoStats.value(repoKey);
    if (m_insightsTraffic) {
        m_insightsTraffic->setText(
            QStringLiteral(
                "<b>ForkMesh traffic</b><br><br>"
                "<span style='font-size:21px; font-weight:800'>%1</span><br>"
                "<span style='color:#8b949e'>served requests</span><br><br>"
                "<span style='font-size:21px; font-weight:800'>%2</span><br>"
                "<span style='color:#8b949e'>clone requests</span><br><br>"
                "<span style='color:#8b949e'>Local-only counters since this app "
                "started tracking.</span>")
                .arg(stats.first)
                .arg(stats.second));
    }

    QList<QPair<QString, qint64>> languages;
    for (auto it = bytesByLanguage.constBegin(); it != bytesByLanguage.constEnd(); ++it)
        languages.append({it.key(), it.value()});
    std::sort(languages.begin(), languages.end(),
              [](const auto &a, const auto &b) { return a.second > b.second; });
    QString languageBar;
    QString languageLegend;
    const int shownLanguages = qMin(5, int(languages.size()));
    for (int i = 0; i < shownLanguages && recognizedBytes > 0; ++i) {
        const double pct = 100.0 * languages.at(i).second / recognizedBytes;
        const QString color = languageColor(languages.at(i).first);
        languageBar += QStringLiteral("<span style='background:%1;'>%2</span>")
                           .arg(color, QString(qMax(1, int(pct / 2)), QChar(0x2588)));
        languageLegend += QString::fromUtf8(
                              "<span style='color:%1'>\xE2\x97\x8F</span> %2 %3% "
                              "<span style='color:#8b949e'>(%4)</span>&nbsp;&nbsp;")
                              .arg(color,
                                   languages.at(i).first.toHtmlEscaped(),
                                   QString::number(pct, 'f', 1),
                                   formatInsightBytes(languages.at(i).second));
    }
    if (m_insightsLanguageBar)
        m_insightsLanguageBar->setText(
            languageBar.isEmpty()
                ? QString()
                : QStringLiteral("<span style='font-size:8px'>%1</span>").arg(languageBar));
    if (m_insightsLanguageLegend)
        m_insightsLanguageLegend->setText(
            languageLegend.isEmpty()
                ? "<span style='color:#8b949e'>No recognized code files yet.</span>"
                : languageLegend);

    struct Contributor {
        QString name;
        int commits = 0;
    };
    QList<Contributor> contributors;
    int contributorCommitTotal = 0;
    if (!dir.isEmpty()) {
        QByteArray shortlogOut;
        QString shortlogErr;
        if (runGitCapture(dir, {"shortlog", "-sn", "--all", "--no-merges"},
                          &shortlogOut, &shortlogErr)) {
            const QRegularExpression lineRe(QStringLiteral("^\\s*(\\d+)\\s+(.+)$"));
            for (const QString &line : QString::fromUtf8(shortlogOut).split('\n')) {
                const QRegularExpressionMatch match = lineRe.match(line);
                if (!match.hasMatch())
                    continue;
                const int commits = match.captured(1).toInt();
                const QString name = match.captured(2).trimmed();
                if (name.isEmpty())
                    continue;
                contributors.append({name, commits});
                contributorCommitTotal += commits;
            }
        } else {
            notes << QStringLiteral("Contributor data unavailable: %1.")
                         .arg(shortlogErr.isEmpty() ? QStringLiteral("git failed")
                                                    : shortlogErr.left(160));
        }
    }
    if (m_insightsContributors) {
        const int shown = qMin(20, int(contributors.size()));
        for (int i = 0; i < shown; ++i) {
            const Contributor &contributor = contributors.at(i);
            const int row = m_insightsContributors->rowCount();
            m_insightsContributors->insertRow(row);
            m_insightsContributors->setItem(row, 0, new QTableWidgetItem(contributor.name));
            auto *countItem = new QTableWidgetItem;
            countItem->setData(Qt::DisplayRole, contributor.commits);
            m_insightsContributors->setItem(row, 1, countItem);
            const double pct =
                contributorCommitTotal > 0
                    ? 100.0 * contributor.commits / contributorCommitTotal
                    : 0.0;
            m_insightsContributors->setItem(
                row, 2, new QTableWidgetItem(QStringLiteral("%1%").arg(pct, 0, 'f', 1)));
        }
    }

    // Refresh the contributor count now that the shortlog has been parsed.
    m_insightsSummary->setText(
        "<b>Repository summary</b>" +
        insightMetricsTable({
            insightMetricCell("Commits", commitCountText, ref),
            insightMetricCell("Contributors", QString::number(contributors.size()),
                              "all branches"),
            insightMetricCell("Files", QString::number(fileCount), "tracked blobs"),
            insightMetricCell("Code size", formatInsightBytes(totalBytes), "tracked bytes"),
            insightMetricCell("Issues", QString::number(totalIssues),
                              QStringLiteral("%1 open / %2 closed")
                                  .arg(openIssues)
                                  .arg(closedIssues)),
            insightMetricCell("Pull requests", QString::number(totalPulls),
                              QStringLiteral("%1 open / %2 merged / %3 closed")
                                  .arg(openPulls)
                                  .arg(mergedPulls)
                                  .arg(closedPulls)),
        }));

    if (!dir.isEmpty()) {
        QByteArray logOut;
        QString logErr;
        if (runGitCapture(dir, {"log", "--format=%h%x1f%an%x1f%ar%x1f%s", "-n", "10", ref},
                          &logOut, &logErr)) {
            for (const QByteArray &record : logOut.split('\n')) {
                if (record.trimmed().isEmpty())
                    continue;
                const QStringList fields = QString::fromUtf8(record).split('\x1f');
                if (fields.size() < 4)
                    continue;
                const int row = m_insightsRecentCommits->rowCount();
                m_insightsRecentCommits->insertRow(row);
                m_insightsRecentCommits->setItem(row, 0, new QTableWidgetItem(fields.at(0)));
                m_insightsRecentCommits->setItem(row, 1, new QTableWidgetItem(fields.at(1)));
                m_insightsRecentCommits->setItem(row, 2, new QTableWidgetItem(fields.at(2)));
                m_insightsRecentCommits->setItem(row, 3, new QTableWidgetItem(fields.at(3)));
            }
            if (m_insightsRecentCommits->rowCount() == 0)
                notes << QStringLiteral("No recent commits on the selected ref.");
        } else {
            notes << QStringLiteral("Recent activity unavailable: %1.")
                         .arg(logErr.isEmpty() ? QStringLiteral("git failed") : logErr.left(160));
        }
    }

    if (m_insightsActivity) {
        QStringList escapedNotes;
        for (const QString &note : std::as_const(notes))
            escapedNotes << note.toHtmlEscaped();
        m_insightsActivity->setText(escapedNotes.join("<br>"));
    }
}

void MainWindow::loadRepoInfo()
{
    m_repoInfo = RepoInfo();
    if (m_repoDetailIndex < 0 || m_repoDetailIndex >= m_repositories.size())
        return;
    const RepositoryRecord &repo = m_repositories.at(m_repoDetailIndex);

    QByteArray raw;
    const QString local = repo.localPath + "/info.json";
    if (!repo.localPath.isEmpty() && QFileInfo::exists(local)) {
        QFile file(local);
        if (file.open(QIODevice::ReadOnly))
            raw = file.readAll();
    } else {
        const QString dir = repoGitDir();
        if (!dir.isEmpty())
            runGitCapture(dir, {"show", currentRef() + ":info.json"}, &raw, nullptr);
    }
    if (raw.isEmpty())
        return;

    const QJsonObject obj = QJsonDocument::fromJson(raw).object();
    m_repoInfo.about = obj.value("about").toString();
    m_repoInfo.website = obj.value("website").toString();
    m_repoInfo.language = obj.value("language").toString();
    m_repoInfo.defaultBranch = obj.value("defaultBranch").toString();
    m_repoInfo.forks = obj.value("forks").toInt(0);
    m_repoInfo.stars = obj.value("stars").toInt(0);
    m_repoInfo.mirrors = obj.value("mirrors").toInt(1);
    for (const QJsonValue &v : obj.value("topics").toArray())
        m_repoInfo.topics << v.toString();
    for (const QJsonValue &v : obj.value("contributors").toArray()) {
        const QJsonObject c = v.toObject();
        if (!c.value("name").toString().isEmpty())
            m_repoInfo.contributorAvatars.insert(c.value("name").toString(),
                                                 c.value("avatar").toString());
    }
}

void MainWindow::setRepoBranch(const QString &branch)
{
    m_repoBranch = branch;
    if (m_branchButton)
        m_branchButton->setText(branch);
    loadRepoOverview(QString());
    loadAboutSidebar();
    loadCommits();
    if (m_insightsSummary)
        loadRepoInsights();
}

QStringList MainWindow::repoBranches() const
{
    QStringList branches;
    const QString dir = repoGitDir();
    QByteArray out;
    if (!dir.isEmpty() &&
        runGitCapture(dir,
                      {"branch", "--sort=-committerdate",
                       "--format=%(refname:short)"},
                      &out, nullptr)) {
        for (const QString &line : QString::fromUtf8(out).split('\n')) {
            const QString branch = line.trimmed();
            if (!branch.isEmpty() && !branches.contains(branch))
                branches.append(branch);
        }
    }
    return branches;
}

QString MainWindow::repoDefaultBranch(const QStringList &branches) const
{
    const QString configured = m_repoInfo.defaultBranch.trimmed();
    if (!configured.isEmpty() && branches.contains(configured))
        return configured;

    QByteArray head;
    const QString dir = repoGitDir();
    if (!dir.isEmpty() &&
        runGitCapture(dir, {"symbolic-ref", "--short", "HEAD"}, &head,
                      nullptr)) {
        const QString branch = QString::fromUtf8(head).trimmed();
        if (branches.contains(branch))
            return branch;
    }
    if (branches.contains(QStringLiteral("main")))
        return QStringLiteral("main");
    if (branches.contains(QStringLiteral("master")))
        return QStringLiteral("master");
    if (!m_repoBranch.isEmpty() && branches.contains(m_repoBranch))
        return m_repoBranch;
    return branches.isEmpty() ? QString() : branches.first();
}

void MainWindow::loadBranchesAndTags()
{
    const QString dir = repoGitDir();

    // Current branch / default ref.
    QString branch = m_repoInfo.defaultBranch;
    if (branch.isEmpty() && !dir.isEmpty()) {
        QByteArray head;
        if (runGitCapture(dir, {"rev-parse", "--abbrev-ref", "HEAD"}, &head, nullptr))
            branch = QString::fromUtf8(head).trimmed();
    }
    if (branch.isEmpty() || branch == "HEAD")
        branch = QStringLiteral("HEAD");
    m_repoBranch = branch == "HEAD" ? QString() : branch;
    if (m_branchButton)
        m_branchButton->setText(m_repoBranch.isEmpty() ? "HEAD" : m_repoBranch);

    const QStringList branches = repoBranches();
    if (m_branchesButton) {
        m_branchesButton->setText(
            QStringLiteral("%1 %2")
                .arg(branches.size())
                .arg(branches.size() == 1 ? QStringLiteral("branch")
                                          : QStringLiteral("branches")));
        m_branchesButton->setEnabled(!branches.isEmpty());
    }

    // Branch menu.
    if (m_branchButton) {
        auto *menu = new QMenu(m_branchButton);
        for (const QString &branchName : branches)
            menu->addAction(branchName, this,
                            [this, branchName] { setRepoBranch(branchName); });
        if (menu->isEmpty())
            menu->addAction("No branches")->setEnabled(false);
        m_branchButton->setMenu(menu);
    }

    // Tags count (clicking the button opens the Releases panel, not a menu).
    if (m_tagsButton) {
        QByteArray out;
        int count = 0;
        if (!dir.isEmpty() && runGitCapture(dir, {"tag", "--sort=-creatordate"}, &out,
                                            nullptr)) {
            for (const QString &line : QString::fromUtf8(out).split('\n'))
                if (!line.trimmed().isEmpty())
                    ++count;
        }
        m_tagsButton->setText(QStringLiteral("Tags %1").arg(count));
    }

    // Only refresh the Branches / Releases panels if one is actually on screen.
    // They run a git command per branch/tag, so eagerly refreshing them on every
    // ref change (including at startup) would needlessly slow things down — the
    // tab-switch handler refreshes them when the user opens them.
    if (m_repoDetailStack) {
        const int current = m_repoDetailStack->currentIndex();
        if (current == m_branchesTabIndex)
            loadBranchesPanel();
        else if (current == m_releasesTabIndex)
            loadReleasesPanel();
    }
}

bool MainWindow::repoHasWorkingTree() const
{
    if (m_repoDetailIndex < 0 || m_repoDetailIndex >= m_repositories.size())
        return false;
    const RepositoryRecord &repo = m_repositories.at(m_repoDetailIndex);
    return !repo.previewOnly && !repo.localPath.isEmpty() &&
           QDir(repo.localPath).exists(".git");
}

// ---- Branches panel --------------------------------------------------------

QWidget *MainWindow::buildBranchesTab()
{
    auto *page = new QWidget;
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(16, 14, 16, 16);
    layout->setSpacing(10);

    auto *headerRow = new QHBoxLayout;
    headerRow->setContentsMargins(0, 0, 0, 0);
    auto *heading = new QLabel("Branches");
    heading->setObjectName("channelTitle");
    m_branchesSummary = new QLabel;
    m_branchesSummary->setObjectName("statusLine");
    auto *newBranchButton = new QPushButton("New branch");
    newBranchButton->setObjectName("primaryButton");
    newBranchButton->setCursor(Qt::PointingHandCursor);
    setOcticon(newBranchButton, "git-branch", 16);
    connect(newBranchButton, &QPushButton::clicked, this, &MainWindow::promptNewBranch);
    auto *refreshButton = new QPushButton("Refresh");
    refreshButton->setObjectName("ghostButton");
    refreshButton->setCursor(Qt::PointingHandCursor);
    setOcticon(refreshButton, "sync", 16);
    connect(refreshButton, &QPushButton::clicked, this, &MainWindow::loadBranchesPanel);
    headerRow->addWidget(heading);
    headerRow->addWidget(m_branchesSummary);
    headerRow->addStretch();
    headerRow->addWidget(refreshButton);
    headerRow->addWidget(newBranchButton);
    layout->addLayout(headerRow);

    m_branchesTable = new QTableWidget(0, 4);
    m_branchesTable->setObjectName("issueTable");
    m_branchesTable->setHorizontalHeaderLabels(
        {"Branch", "Status", "Updated", ""});
    m_branchesTable->verticalHeader()->setVisible(false);
    m_branchesTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_branchesTable->setSelectionMode(QAbstractItemView::SingleSelection);
    m_branchesTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_branchesTable->setShowGrid(false);
    m_branchesTable->setWordWrap(false);
    QHeaderView *bh = m_branchesTable->horizontalHeader();
    bh->setHighlightSections(false);
    bh->setSectionResizeMode(0, QHeaderView::Stretch);
    bh->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    bh->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    bh->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    connect(m_branchesTable, &QTableWidget::cellDoubleClicked, this,
            [this](int row, int) {
                QTableWidgetItem *it = m_branchesTable->item(row, 0);
                if (it)
                    setRepoBranch(it->text());
            });
    layout->addWidget(m_branchesTable, 1);
    return page;
}

void MainWindow::loadBranchesPanel()
{
    if (!m_branchesTable)
        return;
    m_branchesTable->setRowCount(0);
    const QString dir = repoGitDir();
    const QStringList branches = repoBranches();
    const QString base = repoDefaultBranch(branches);
    const QString selected = m_repoBranch.isEmpty() ? base : m_repoBranch;
    const bool writable = repoHasWorkingTree();

    if (m_branchesSummary)
        m_branchesSummary->setText(
            QStringLiteral("\xC2\xB7 %1 total \xC2\xB7 default: %2")
                .arg(branches.size())
                .arg(base.isEmpty() ? "none" : base));

    for (const QString &branch : branches) {
        const int row = m_branchesTable->rowCount();
        m_branchesTable->insertRow(row);

        auto *name = new QTableWidgetItem(branch);
        if (branch == selected)
            name->setIcon(themedOcticon("check-circle", QColor("#3fb950"), 14));
        else
            name->setIcon(themedOcticon("git-branch", QColor("#8b949e"), 14));
        m_branchesTable->setItem(row, 0, name);

        // Ahead/behind vs the default branch.
        QString status = branch == base ? QStringLiteral("Default branch") : QString();
        qint64 ts = 0;
        if (!dir.isEmpty()) {
            if (branch != base) {
                QByteArray counts;
                if (runGitCapture(dir,
                                  {"rev-list", "--left-right", "--count",
                                   base + "..." + branch},
                                  &counts, nullptr)) {
                    const QStringList parts =
                        QString::fromUtf8(counts).trimmed().split(
                            QRegularExpression(QStringLiteral("\\s+")));
                    if (parts.size() >= 2)
                        status = QStringLiteral("%1 behind \xC2\xB7 %2 ahead")
                                     .arg(parts.at(0), parts.at(1));
                }
            }
            QByteArray when;
            if (runGitCapture(dir,
                              {"log", "-1", "--format=%ct", branch}, &when, nullptr))
                ts = QString::fromUtf8(when).trimmed().toLongLong();
        }
        m_branchesTable->setItem(row, 1, new QTableWidgetItem(status));
        auto *updated = new QTableWidgetItem(formatShortRelativeTime(ts));
        m_branchesTable->setItem(row, 2, updated);

        // Delete button (disabled for the default/checked-out branch).
        auto *del = new QPushButton;
        del->setObjectName("issueIconButton");
        del->setFlat(true);
        del->setCursor(Qt::PointingHandCursor);
        del->setIcon(themedOcticon("trash", QColor("#f85149"), 15));
        del->setIconSize(QSize(15, 15));
        del->setToolTip(QStringLiteral("Delete branch %1").arg(branch));
        const bool canDelete = writable && branch != base && branch != selected;
        del->setEnabled(canDelete);
        if (!canDelete)
            del->setToolTip(writable
                                ? "Can't delete the default or current branch"
                                : "Read-only mirror — no working tree to delete from");
        connect(del, &QPushButton::clicked, this,
                [this, branch] { deleteBranch(branch); });
        m_branchesTable->setCellWidget(row, 3, del);
    }
    if (branches.isEmpty()) {
        m_branchesTable->insertRow(0);
        auto *empty = new QTableWidgetItem("No branches in this repository.");
        empty->setForeground(QColor("#8b949e"));
        m_branchesTable->setItem(0, 0, empty);
    }
}

void MainWindow::promptNewBranch()
{
    if (!repoHasWorkingTree()) {
        setRepoDetailNotice("This is a read-only mirror; branches can't be created here.",
                            true);
        return;
    }
    const QString dir = repoGitDir();
    const QStringList branches = repoBranches();
    const QString base = m_repoBranch.isEmpty() ? repoDefaultBranch(branches)
                                                : m_repoBranch;
    bool ok = false;
    const QString name =
        QInputDialog::getText(this, "New branch",
                              QStringLiteral("Create a branch from %1:").arg(base),
                              QLineEdit::Normal, QString(), &ok)
            .trimmed();
    if (!ok || name.isEmpty())
        return;
    QString err;
    if (!runGitCapture(dir, {"branch", name, base}, nullptr, &err)) {
        setRepoDetailNotice(err.isEmpty() ? "Could not create the branch." : err, true);
        return;
    }
    logSystem(QStringLiteral("Git: created branch %1 from %2.").arg(name, base));
    setRepoDetailNotice(QStringLiteral("Created branch %1.").arg(name));
    loadBranchesAndTags();
    setRepoBranch(name);
}

void MainWindow::deleteBranch(const QString &branch)
{
    if (branch.isEmpty() || !repoHasWorkingTree())
        return;
    const QString dir = repoGitDir();
    if (QMessageBox::question(
            this, "Delete branch",
            QStringLiteral("Delete branch \"%1\"? This cannot be undone.").arg(branch),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes)
        return;
    QString err;
    // -D force-deletes even if not merged; the user explicitly confirmed.
    if (!runGitCapture(dir, {"branch", "-D", branch}, nullptr, &err)) {
        setRepoDetailNotice(err.isEmpty() ? "Could not delete the branch." : err, true);
        return;
    }
    logSystem(QStringLiteral("Git: deleted branch %1.").arg(branch));
    setRepoDetailNotice(QStringLiteral("Deleted branch %1.").arg(branch));
    loadBranchesAndTags();
}

// ---- Releases panel --------------------------------------------------------

QWidget *MainWindow::buildReleasesTab()
{
    auto *page = new QWidget;
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(16, 14, 16, 16);
    layout->setSpacing(10);

    auto *headerRow = new QHBoxLayout;
    headerRow->setContentsMargins(0, 0, 0, 0);
    auto *heading = new QLabel("Releases");
    heading->setObjectName("channelTitle");
    m_releasesSummary = new QLabel;
    m_releasesSummary->setObjectName("statusLine");
    auto *newButton = new QPushButton("Draft a release");
    newButton->setObjectName("primaryButton");
    newButton->setCursor(Qt::PointingHandCursor);
    setOcticon(newButton, "tag", 16);
    connect(newButton, &QPushButton::clicked, this, &MainWindow::promptNewRelease);
    auto *refreshButton = new QPushButton("Refresh");
    refreshButton->setObjectName("ghostButton");
    refreshButton->setCursor(Qt::PointingHandCursor);
    setOcticon(refreshButton, "sync", 16);
    connect(refreshButton, &QPushButton::clicked, this, &MainWindow::loadReleasesPanel);
    headerRow->addWidget(heading);
    headerRow->addWidget(m_releasesSummary);
    headerRow->addStretch();
    headerRow->addWidget(refreshButton);
    headerRow->addWidget(newButton);
    layout->addLayout(headerRow);

    m_releasesTable = new QTableWidget(0, 4);
    m_releasesTable->setObjectName("issueTable");
    m_releasesTable->setHorizontalHeaderLabels({"Tag", "Date", "Release notes", ""});
    m_releasesTable->verticalHeader()->setVisible(false);
    m_releasesTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_releasesTable->setSelectionMode(QAbstractItemView::SingleSelection);
    m_releasesTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_releasesTable->setShowGrid(false);
    m_releasesTable->setWordWrap(false);
    QHeaderView *rh = m_releasesTable->horizontalHeader();
    rh->setHighlightSections(false);
    rh->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    rh->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    rh->setSectionResizeMode(2, QHeaderView::Stretch);
    rh->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    connect(m_releasesTable, &QTableWidget::cellDoubleClicked, this,
            [this](int row, int) {
                QTableWidgetItem *it = m_releasesTable->item(row, 0);
                if (it)
                    setRepoBranch(it->text()); // browse the repo at the tag
            });
    layout->addWidget(m_releasesTable, 1);
    return page;
}

void MainWindow::loadReleasesPanel()
{
    if (!m_releasesTable)
        return;
    m_releasesTable->setRowCount(0);
    const QString dir = repoGitDir();
    const bool writable = repoHasWorkingTree();

    int count = 0;
    QByteArray out;
    if (!dir.isEmpty() &&
        runGitCapture(dir,
                      {"for-each-ref", "--sort=-creatordate", "refs/tags",
                       "--format=%(refname:short)%x1f%(creatordate:short)%x1f"
                       "%(contents:subject)"},
                      &out, nullptr)) {
        for (const QByteArray &line : out.split('\n')) {
            const QString text = QString::fromUtf8(line);
            if (text.trimmed().isEmpty())
                continue;
            const QStringList f = text.split(QLatin1Char('\x1f'));
            if (f.isEmpty())
                continue;
            const QString tag = f.value(0).trimmed();
            if (tag.isEmpty())
                continue;
            const int row = m_releasesTable->rowCount();
            m_releasesTable->insertRow(row);
            auto *tagItem = new QTableWidgetItem(tag);
            tagItem->setIcon(themedOcticon("tag", QColor("#a371f7"), 14));
            m_releasesTable->setItem(row, 0, tagItem);
            m_releasesTable->setItem(row, 1, new QTableWidgetItem(f.value(1).trimmed()));
            m_releasesTable->setItem(row, 2, new QTableWidgetItem(f.value(2).trimmed()));

            auto *del = new QPushButton;
            del->setObjectName("issueIconButton");
            del->setFlat(true);
            del->setCursor(Qt::PointingHandCursor);
            del->setIcon(themedOcticon("trash", QColor("#f85149"), 15));
            del->setIconSize(QSize(15, 15));
            del->setToolTip(QStringLiteral("Delete tag %1").arg(tag));
            del->setEnabled(writable);
            connect(del, &QPushButton::clicked, this, [this, tag] { deleteTag(tag); });
            m_releasesTable->setCellWidget(row, 3, del);
            ++count;
        }
    }
    if (m_releasesSummary)
        m_releasesSummary->setText(
            QStringLiteral("\xC2\xB7 %1 release%2")
                .arg(count)
                .arg(count == 1 ? "" : "s"));
    if (count == 0) {
        m_releasesTable->insertRow(0);
        auto *empty = new QTableWidgetItem(
            "No releases yet. Draft one to tag a commit in the repository.");
        empty->setForeground(QColor("#8b949e"));
        m_releasesTable->setItem(0, 0, empty);
    }
}

void MainWindow::promptNewRelease()
{
    if (!repoHasWorkingTree()) {
        setRepoDetailNotice("This is a read-only mirror; releases can't be created here.",
                            true);
        return;
    }
    const QString dir = repoGitDir();
    const QStringList branches = repoBranches();
    const QString target = m_repoBranch.isEmpty() ? repoDefaultBranch(branches)
                                                  : m_repoBranch;

    // GitHub-style "draft a release": tag name, target ref, and release notes.
    QDialog dialog(this);
    dialog.setWindowTitle("Draft a new release");
    auto *form = new QFormLayout(&dialog);
    auto *tagEdit = new QLineEdit;
    tagEdit->setPlaceholderText("v1.0.0");
    auto *targetEdit = new QComboBox;
    targetEdit->addItems(branches);
    const int targetIdx = targetEdit->findText(target);
    if (targetIdx >= 0)
        targetEdit->setCurrentIndex(targetIdx);
    auto *titleEdit = new QLineEdit;
    titleEdit->setPlaceholderText("Release title (optional)");
    auto *notesEdit = new QPlainTextEdit;
    notesEdit->setPlaceholderText("Describe this release...");
    notesEdit->setMinimumHeight(120);
    form->addRow("Tag", tagEdit);
    form->addRow("Target", targetEdit);
    form->addRow("Title", titleEdit);
    form->addRow("Notes", notesEdit);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    buttons->button(QDialogButtonBox::Ok)->setText("Publish release");
    form->addRow(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if (dialog.exec() != QDialog::Accepted)
        return;

    const QString tag = tagEdit->text().trimmed();
    const QString targetRef = targetEdit->currentText().trimmed();
    if (tag.isEmpty()) {
        setRepoDetailNotice("A release needs a tag name.", true);
        return;
    }
    QString message = titleEdit->text().trimmed();
    const QString notes = notesEdit->toPlainText().trimmed();
    if (message.isEmpty())
        message = tag;
    if (!notes.isEmpty())
        message += "\n\n" + notes;

    // Annotated tag so the release notes live in the repo's git history.
    QString err;
    if (!runGitCapture(dir, {"tag", "-a", tag, targetRef, "-m", message}, nullptr,
                       &err)) {
        setRepoDetailNotice(err.isEmpty() ? "Could not create the release tag." : err,
                            true);
        return;
    }
    logSystem(QStringLiteral("Git: tagged release %1 at %2.").arg(tag, targetRef));
    setRepoDetailNotice(QStringLiteral("Published release %1.").arg(tag));
    loadBranchesAndTags();
}

void MainWindow::deleteTag(const QString &tag)
{
    if (tag.isEmpty() || !repoHasWorkingTree())
        return;
    const QString dir = repoGitDir();
    if (QMessageBox::question(
            this, "Delete release",
            QStringLiteral("Delete release tag \"%1\"? This cannot be undone.").arg(tag),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes)
        return;
    QString err;
    if (!runGitCapture(dir, {"tag", "-d", tag}, nullptr, &err)) {
        setRepoDetailNotice(err.isEmpty() ? "Could not delete the tag." : err, true);
        return;
    }
    logSystem(QStringLiteral("Git: deleted tag %1.").arg(tag));
    setRepoDetailNotice(QStringLiteral("Deleted release %1.").arg(tag));
    loadBranchesAndTags();
}

void MainWindow::loadFileSearchIndex()
{
    if (!m_fileCompleter)
        return;
    QStringList paths;
    const QString dir = repoGitDir();
    QByteArray out;
    if (!dir.isEmpty() &&
        runGitCapture(dir, {"ls-tree", "-r", "--name-only", "-z", currentRef()}, &out,
                      nullptr)) {
        for (const QByteArray &record : out.split('\0'))
            if (!record.isEmpty())
                paths << QString::fromUtf8(record);
    }
    m_fileCompleter->setModel(new QStringListModel(paths, m_fileCompleter));
}

void MainWindow::loadAboutSidebar()
{
    const QString dir = repoGitDir();
    const RepositoryRecord *repo =
        (m_repoDetailIndex >= 0 && m_repoDetailIndex < m_repositories.size())
            ? &m_repositories.at(m_repoDetailIndex)
            : nullptr;

    // About text + website.
    if (m_aboutText) {
        QString text = m_repoInfo.about.isEmpty()
                           ? (repo ? repo->description : QString())
                           : m_repoInfo.about;
        if (text.isEmpty())
            text = "<span style='color:#8b949e'>No description.</span>";
        else
            text = text.toHtmlEscaped();
        if (!m_repoInfo.website.isEmpty())
            text += QStringLiteral("<br><a href=\"%1\">%1</a>")
                        .arg(m_repoInfo.website.toHtmlEscaped());
        m_aboutText->setText(text);
    }
    // Topics as chips.
    if (m_aboutTopics) {
        QStringList chips;
        for (const QString &t : m_repoInfo.topics)
            chips << "<span style='background:#1f6feb33; color:#58a6ff; "
                     "border-radius:9px; padding:1px 8px;'>" +
                         t.toHtmlEscaped() + "</span>";
        m_aboutTopics->setText(chips.join(" "));
        m_aboutTopics->setVisible(!chips.isEmpty());
    }

    // Languages: aggregate blob sizes per language.
    if (m_langBar && m_langLegend) {
        QHash<QString, qint64> bytesByLang;
        qint64 total = 0;
        QByteArray out;
        if (!dir.isEmpty() &&
            runGitCapture(dir, {"ls-tree", "-r", "-l", currentRef()}, &out, nullptr)) {
            for (const QByteArray &record : out.split('\n')) {
                const int tab = record.indexOf('\t');
                if (tab < 0)
                    continue;
                const QList<QByteArray> meta = record.left(tab).simplified().split(' ');
                if (meta.size() < 4)
                    continue;
                bool ok = false;
                const qint64 size = QString::fromUtf8(meta.at(3)).toLongLong(&ok);
                if (!ok || size <= 0)
                    continue;
                const QString name = QString::fromUtf8(record.mid(tab + 1));
                const QString lang = languageForFile(name);
                if (lang.isEmpty())
                    continue;
                bytesByLang[lang] += size;
                total += size;
            }
        }
        QList<QPair<QString, qint64>> langs;
        for (auto it = bytesByLang.constBegin(); it != bytesByLang.constEnd(); ++it)
            langs.append({it.key(), it.value()});
        std::sort(langs.begin(), langs.end(),
                  [](const auto &a, const auto &b) { return a.second > b.second; });

        QString bar, legend;
        const int shown = qMin(5, int(langs.size()));
        for (int i = 0; i < shown && total > 0; ++i) {
            const double pct = 100.0 * langs.at(i).second / total;
            const QString color = languageColor(langs.at(i).first);
            bar += QStringLiteral("<span style='background:%1;'>%2</span>")
                       .arg(color, QString(qMax(1, int(pct / 2)), QChar(0x2588)));
            legend += QStringLiteral(
                          "<span style='color:%1'>\xE2\x97\x8F</span> %2 %3%&nbsp; ")
                          .arg(color, langs.at(i).first.toHtmlEscaped(),
                               QString::number(pct, 'f', 1));
        }
        m_langBar->setText(bar.isEmpty()
                               ? QString()
                               : QStringLiteral("<span style='font-size:8px'>%1</span>")
                                     .arg(bar));
        m_langLegend->setText(legend.isEmpty()
                                  ? "<span style='color:#8b949e'>No code yet.</span>"
                                  : legend);
    }

    // Contributors from git shortlog, with generated avatars.
    if (m_contributorsRow && m_contributorsHeader) {
        struct Contrib {
            QString name;
            int count;
        };
        QList<Contrib> contribs;
        QByteArray out;
        if (!dir.isEmpty() &&
            runGitCapture(dir, {"shortlog", "-sn", "--all", "--no-merges"}, &out,
                          nullptr)) {
            for (const QString &line : QString::fromUtf8(out).split('\n')) {
                const QString t = line.trimmed();
                if (t.isEmpty())
                    continue;
                const int tab = t.indexOf('\t');
                if (tab < 0)
                    continue;
                contribs.append({t.mid(tab + 1).trimmed(), t.left(tab).toInt()});
            }
        }
        m_contributorsHeader->setText(
            QStringLiteral("CONTRIBUTORS %1").arg(contribs.size()));
        QString html;
        const int shown = qMin(12, int(contribs.size()));
        for (int i = 0; i < shown; ++i)
            html += QString::fromUtf8("<span style='color:%1' title='%2'>\xE2\x97\x8F</span> ")
                        .arg(senderColor(contribs.at(i).name),
                             (contribs.at(i).name + " \xC2\xB7 " +
                              QString::number(contribs.at(i).count) + " commits")
                                 .toHtmlEscaped());
        if (contribs.size() > shown)
            html += QStringLiteral("<span style='color:#8b949e'>+%1</span>")
                        .arg(contribs.size() - shown);
        m_contributorsRow->setText(html.isEmpty()
                                       ? "<span style='color:#8b949e'>None yet.</span>"
                                       : html);
    }
}

void MainWindow::startRefreshSpin()
{
    if (!m_refreshButton)
        return;
    if (!m_refreshSpinTimer) {
        m_refreshSpinTimer = new QTimer(this);
        connect(m_refreshSpinTimer, &QTimer::timeout, this, [this] {
            m_refreshAngle = (m_refreshAngle + 30) % 360;
            m_refreshButton->setIcon(
                QIcon(refreshPixmap(QColor(Theme::kTextTertiary), m_refreshAngle, 22)));
        });
    }
    m_refreshSpinTimer->start(60);
}

void MainWindow::stopRefreshSpin()
{
    if (m_refreshSpinTimer)
        m_refreshSpinTimer->stop();
    if (m_refreshButton)
        m_refreshButton->setIcon(
            QIcon(refreshPixmap(QColor(Theme::kTextTertiary), 0, 22)));
}

int MainWindow::issuesRepoIndex() const
{
    if (!m_issuesRepoCombo || m_issuesRepoCombo->currentIndex() < 0)
        return -1;
    bool ok = false;
    const int idx = m_issuesRepoCombo->currentData().toInt(&ok);
    if (!ok || idx < 0 || idx >= m_repositories.size())
        return -1;
    return idx;
}

IssueStore MainWindow::issueStoreForCurrentRepo() const
{
    const int idx = issuesRepoIndex();
    if (idx < 0)
        return IssueStore(QString(), QString(), &m_profileIdentity, m_userName);
    const RepositoryRecord &repo = m_repositories.at(idx);
    return IssueStore(repo.localPath, repo.mirrorPath, &m_profileIdentity, m_userName);
}

void MainWindow::refreshIssuesRepoCombo()
{
    if (!m_issuesRepoCombo)
        return;
    const QVariant previous =
        m_issuesRepoCombo->count() ? m_issuesRepoCombo->currentData() : QVariant();
    QSignalBlocker blocker(m_issuesRepoCombo);
    m_issuesRepoCombo->clear();
    for (int i = 0; i < m_repositories.size(); ++i) {
        const RepositoryRecord &repo = m_repositories.at(i);
        m_issuesRepoCombo->addItem(repo.owner + "/" + repo.name, i);
    }
    if (previous.isValid()) {
        const int restore = m_issuesRepoCombo->findData(previous);
        if (restore >= 0)
            m_issuesRepoCombo->setCurrentIndex(restore);
    }
    blocker.unblock();
    reloadIssues();
}

void MainWindow::reloadIssues()
{
    if (!m_issueTable)
        return;
    if (issuesRepoIndex() < 0) {
        m_currentIssues.clear();
        m_currentLabels.clear();
        m_currentMilestones.clear();
        m_issueTable->setRowCount(0);
        m_currentIssueNumber = -1;
        renderIssueThread(Issue());
        updateIssueActionState();
        updateRepoIssueCount();
        return;
    }
    const IssueStore store = issueStoreForCurrentRepo();
    m_currentIssues = store.loadAll();
    m_currentLabels = store.loadLabels();
    m_currentMilestones = store.loadMilestones();

    QSignalBlocker labelBlock(m_issueLabelFilter);
    m_issueLabelFilter->clear();
    m_issueLabelFilter->addItem("All labels", QString());
    for (const IssueLabel &label : m_currentLabels)
        m_issueLabelFilter->addItem(label.name, label.name);
    labelBlock.unblock();

    QSignalBlocker msBlock(m_issueMilestoneFilter);
    m_issueMilestoneFilter->clear();
    m_issueMilestoneFilter->addItem("All milestones", QString());
    for (const IssueMilestone &ms : m_currentMilestones)
        m_issueMilestoneFilter->addItem(ms.title, ms.title);
    msBlock.unblock();

    refreshIssueList();
    updateIssueActionState();
    updateRepoIssueCount();
}

QWidget *MainWindow::makeIssueRow(const Issue &issue,
                                  const QHash<QString, QString> &labelColors) const
{
    auto *row = new QWidget;
    auto *col = new QVBoxLayout(row);
    col->setContentsMargins(8, 5, 8, 5);
    col->setSpacing(4);

    QString titleText =
        QStringLiteral("#%1  %2").arg(issue.number).arg(issue.title.toHtmlEscaped());
    if (issue.status == "closed")
        titleText += "  (closed)";
    auto *title = new QLabel(titleText);
    title->setObjectName("issueRowTitle");
    title->setWordWrap(true);
    col->addWidget(title);

    if (!issue.labels.isEmpty() || !issue.milestone.isEmpty()) {
        auto *pills = new QHBoxLayout;
        pills->setContentsMargins(0, 0, 0, 0);
        pills->setSpacing(4);
        // Each label shown as a colored, pill-shaped chip. The id selector keeps
        // the dynamic background from being overridden by the sidebar's
        // "#sidebar QLabel { background: transparent }" rule.
        for (const QString &name : issue.labels) {
            const QString bg = labelColors.value(name, QStringLiteral("#94a3b8"));
            auto *pill = new QLabel(name);
            pill->setObjectName("issuePill");
            pill->setStyleSheet(
                QStringLiteral("QLabel#issuePill { background:%1; color:%2; "
                               "border-radius:9px; padding:1px 8px; "
                               "font-size:11px; font-weight:600; }")
                    .arg(bg, pillTextColor(bg)));
            pills->addWidget(pill, 0, Qt::AlignLeft);
        }
        // The milestone (if any) as a subtle outlined pill.
        if (!issue.milestone.isEmpty()) {
            auto *ms = new QLabel(
                QStringLiteral("Milestone %1").arg(issue.milestone));
            ms->setObjectName("issueMilestonePill");
            ms->setStyleSheet(
                "QLabel#issueMilestonePill { border:1px solid #8b949e; "
                "color:#8b949e; border-radius:9px; padding:1px 8px; "
                "font-size:11px; }");
            pills->addWidget(ms, 0, Qt::AlignLeft);
        }
        pills->addStretch();
        col->addLayout(pills);
    }
    return row;
}

void MainWindow::refreshIssueList()
{
    if (!m_issueTable)
        return;
    const QString statusFilter = m_issueStatusFilter->currentText();
    const QString labelFilter = m_issueLabelFilter->currentData().toString();
    const QString msFilter = m_issueMilestoneFilter->currentData().toString();
    const QString search =
        m_issueSearch ? m_issueSearch->text().trimmed() : QString();
    const int keep = m_currentIssueNumber;

    // Disable sorting while inserting so rows aren't reordered mid-build.
    m_issueTable->setSortingEnabled(false);
    m_issueTable->setRowCount(0);
    for (const Issue &issue : m_currentIssues) {
        if (statusFilter == "Open" && issue.status != "open")
            continue;
        if (statusFilter == "Closed" && issue.status != "closed")
            continue;
        if (!labelFilter.isEmpty() && !issue.labels.contains(labelFilter))
            continue;
        if (!msFilter.isEmpty() && issue.milestone != msFilter)
            continue;
        // Free-text search over number, title, labels and milestone.
        if (!search.isEmpty()) {
            const QString hay = QStringLiteral("#%1 %2 %3 %4")
                                    .arg(issue.number)
                                    .arg(issue.title, issue.labels.join(" "),
                                         issue.milestone);
            if (!hay.contains(search, Qt::CaseInsensitive))
                continue;
        }

        const int row = m_issueTable->rowCount();
        m_issueTable->insertRow(row);

        auto *numItem = new QTableWidgetItem;
        // An int in DisplayRole both renders the number and sorts numerically.
        numItem->setData(Qt::DisplayRole, issue.number);
        numItem->setData(Qt::UserRole, issue.number); // lookup key
        m_issueTable->setItem(row, 0, numItem);
        m_issueTable->setItem(row, 1, new QTableWidgetItem(issue.title));
        auto *status = new QTableWidgetItem(issue.status == "closed" ? "Closed"
                                                                     : "Open");
        status->setForeground(QColor(issue.status == "closed" ? "#f85149"
                                                              : "#3fb950"));
        m_issueTable->setItem(row, 2, status);
        auto *votes = new QTableWidgetItem;
        votes->setData(Qt::DisplayRole, issue.votes); // numeric sort
        votes->setTextAlignment(Qt::AlignCenter);
        m_issueTable->setItem(row, 3, votes);
        m_issueTable->setItem(row, 4, new QTableWidgetItem(issue.labels.join(", ")));
        m_issueTable->setItem(row, 5, new QTableWidgetItem(issue.milestone));
        // Created date: ISO yyyy-MM-dd sorts chronologically as plain text; the
        // tooltip carries the friendly "x ago" form.
        auto *created = new QTableWidgetItem(
            issue.createdAt > 0
                ? QDateTime::fromMSecsSinceEpoch(issue.createdAt).toString("yyyy-MM-dd")
                : QString());
        created->setToolTip(formatIssueRelativeTime(issue.createdAt));
        m_issueTable->setItem(row, 6, created);
        if (const AgentSession *session = latestAgentSessionForIssue(issue.number)) {
            // Brand icon for the agent that worked the issue: Claude uses the
            // "code" octicon (clay), Codex/OpenAI the "terminal" octicon (green).
            const bool isClaude = session->provider == QLatin1String("claude");
            const QString iconName = isClaude ? "code" : "terminal";
            const QColor iconColor(isClaude ? "#d97757" : "#10a37f");
            auto *button = new QPushButton;
            button->setObjectName("issueIconButton");
            button->setFlat(true);
            button->setCursor(Qt::PointingHandCursor);
            button->setIcon(themedOcticon(iconName, iconColor, 16));
            button->setIconSize(QSize(16, 16));
            button->setFocusPolicy(Qt::NoFocus); // don't steal the row selection
            button->setToolTip(agentProviderName(session->provider) +
                               QStringLiteral(" session #%1 \xC2\xB7 click to view")
                                   .arg(session->id));
            const int sessionId = session->id;
            // Clicking the icon opens the session; because the button consumes
            // the click it doesn't reselect the row or refresh the issue pane.
            connect(button, &QPushButton::clicked, this,
                    [this, sessionId] { switchToAgentsTab(sessionId); });
            m_issueTable->setCellWidget(row, 7, button);
            auto *agentItem = new QTableWidgetItem(agentProviderName(session->provider));
            agentItem->setData(Qt::UserRole, session->id);
            m_issueTable->setItem(row, 7, agentItem);
        } else {
            m_issueTable->setItem(row, 7, new QTableWidgetItem(QString()));
        }
    }
    m_issueTable->setSortingEnabled(true);

    // Re-select the kept issue (row order may differ after sorting).
    int selRow = -1;
    for (int r = 0; r < m_issueTable->rowCount(); ++r) {
        if (m_issueTable->item(r, 0)->data(Qt::UserRole).toInt() == keep) {
            selRow = r;
            break;
        }
    }
    if (selRow < 0 && m_issueTable->rowCount() > 0)
        selRow = 0;
    if (selRow >= 0) {
        m_issueTable->selectRow(selRow); // fires itemSelectionChanged -> showIssue
    } else {
        m_currentIssueNumber = -1;
        renderIssueThread(Issue());
        updateIssueActionState();
    }
}

void MainWindow::showIssue(int number)
{
    for (const Issue &issue : m_currentIssues) {
        if (issue.number == number) {
            removeIssueComposePage();
            m_issueDeleteConfirmPending = false;
            m_currentIssueNumber = number;
            renderIssueThread(issue);
            updateIssueActionState();
            return;
        }
    }
}

void MainWindow::renderIssueThread(const Issue &issue)
{
    // Clear all cards (keep the trailing stretch rebuilt at the end).
    while (QLayoutItem *item = m_issueThreadLayout->takeAt(0)) {
        if (QWidget *w = item->widget())
            w->deleteLater();
        delete item;
    }

    if (issue.number == 0) {
        m_issueTitle->setText("Select an issue");
        cancelIssueTitleEdit();
        m_issueMeta->clear();
        m_issueMeta->hide();
        if (m_issueAssigneesValue)
            m_issueAssigneesValue->setText("No one - <a href='#'>Assign yourself</a>");
        if (m_issueLabelsValue)
            m_issueLabelsValue->setText("No labels");
        if (m_issueMilestoneValue)
            m_issueMilestoneValue->setText("No milestone");
        updateIssueAgentUi(Issue());
        cancelIssueSidebarEditors();
        m_issueThreadLayout->addStretch();
        return;
    }

    m_issueTitle->setText(
        QStringLiteral("%1 <span style='color:#656d76;font-weight:400'>#%2</span>")
            .arg(issue.title.toHtmlEscaped())
            .arg(issue.number));
    if (m_issueTitleEditor)
        m_issueTitleEditor->setText(issue.title);

    // Status pill: rounded corners come from QSS (QLabel rich text can't render
    // border-radius), switched by the dynamic "status" property.
    const bool issueClosed = issue.status == "closed";
    m_issueMeta->show();
    m_issueMeta->setText(issueClosed ? "Closed" : "Open");
    m_issueMeta->setProperty("status", issueClosed ? "closed" : "open");
    m_issueMeta->style()->unpolish(m_issueMeta);
    m_issueMeta->style()->polish(m_issueMeta);

    auto colorFor = [this](const QString &name) -> QString {
        for (const IssueLabel &l : m_currentLabels)
            if (l.name == name && !l.color.isEmpty())
                return l.color;
        return QStringLiteral("#94a3b8");
    };
    if (issue.assignees.isEmpty()) {
        m_issueAssigneesValue->setText("No one - <a href='#'>Assign yourself</a>");
    } else {
        QStringList shown;
        for (const QString &a : issue.assignees)
            shown << a.left(16).toHtmlEscaped();
        m_issueAssigneesValue->setText(shown.join("<br>"));
    }
    if (issue.labels.isEmpty()) {
        m_issueLabelsValue->setText("No labels");
    } else {
        QStringList chips;
        for (const QString &name : issue.labels)
            chips << QString::fromUtf8("<span style='color:%1'>\xE2\x97\x8F %2</span>")
                         .arg(colorFor(name), name.toHtmlEscaped());
        m_issueLabelsValue->setText(chips.join("<br>"));
    }
    m_issueMilestoneValue->setText(
        issue.milestone.isEmpty()
            ? QStringLiteral("No milestone")
            : QStringLiteral("<b>%1</b>").arg(issue.milestone.toHtmlEscaped()));
    updateIssueAgentUi(issue);
    if (m_issueAssigneesEdit)
        m_issueAssigneesEdit->setText(issue.assignees.join(", "));
    if (m_issueLabelsEdit)
        m_issueLabelsEdit->setText(issue.labels.join(", "));
    if (m_issueMilestoneEdit) {
        QSignalBlocker blocker(m_issueMilestoneEdit);
        m_issueMilestoneEdit->clear();
        m_issueMilestoneEdit->addItem("No milestone", QString());
        for (const IssueMilestone &ms : m_currentMilestones)
            m_issueMilestoneEdit->addItem(ms.title, ms.title);
        const int selected = m_issueMilestoneEdit->findData(issue.milestone);
        if (selected >= 0)
            m_issueMilestoneEdit->setCurrentIndex(selected);
    }
    cancelIssueSidebarEditors();

    // Pre-compute edits (target -> latest edit) and deletions.
    QHash<QString, IssueEvent> edits;
    QSet<QString> deleted;
    for (const IssueEvent &ev : issue.events) {
        if (ev.type == "edit" && !ev.target.isEmpty())
            edits.insert(ev.target, ev); // later edits overwrite
        else if (ev.type == "delete" && !ev.target.isEmpty() && ev.target != "self")
            deleted.insert(ev.target);
    }

    const int idx = issuesRepoIndex();
    const QString imageBase =
        idx >= 0 ? m_repositories.at(idx).localPath + "/issues/" +
                       QString::number(issue.number) + "/"
                 : QString();
    const bool haveLocalFiles = !imageBase.isEmpty() &&
                                QFileInfo::exists(imageBase + "issue.md");
    const bool writable = issueStoreForCurrentRepo().canWrite();

    auto addCard = [&](const IssueEvent &ev, bool isOpen) {
        IssueEvent shown = ev;
        if (edits.contains(ev.id)) {
            shown.body = edits.value(ev.id).body;
            shown.attachments = edits.value(ev.id).attachments;
        }
        const int num = issue.number;
        const QString eid = ev.id;
        const QString eventBody = shown.body;
        const QStringList eventAttachments = shown.attachments;
        const QString who = ev.authorName.isEmpty() ? ev.author.left(10) : ev.authorName;
        const QString when = formatIssueRelativeTime(ev.ts);

        auto *row = new QWidget;
        row->setObjectName("issueTimelineRow");
        auto *rowLayout = new QHBoxLayout(row);
        rowLayout->setContentsMargins(0, 0, 0, 0);
        rowLayout->setSpacing(14);
        auto *avatar = new QLabel(who.left(2).toUpper());
        avatar->setObjectName("issueAvatar");
        avatar->setAlignment(Qt::AlignCenter);
        avatar->setFixedSize(36, 36);
        rowLayout->addWidget(avatar, 0, Qt::AlignTop);

        auto *card = new QWidget;
        card->setObjectName("issueTimelineCard");
        auto *cardLayout = new QVBoxLayout(card);
        cardLayout->setContentsMargins(0, 0, 0, 0);
        cardLayout->setSpacing(0);
        auto *headerBox = new QWidget(card);
        headerBox->setObjectName("issueTimelineHeader");
        auto *headerRow = new QHBoxLayout(headerBox);
        headerRow->setContentsMargins(16, 8, 10, 8);
        headerRow->setSpacing(8);
        auto *header = new QLabel(
            QStringLiteral("<b>%1</b> <span>%2 %3</span>")
                .arg(who.toHtmlEscaped(), isOpen ? "opened" : "commented", when));
        header->setTextFormat(Qt::RichText);
        headerRow->addWidget(header);
        headerRow->addStretch();

        auto *bodyContainer = new ClickableIssueBody(card);
        auto *bodyLayout = new QVBoxLayout(bodyContainer);
        bodyLayout->setContentsMargins(16, 16, 16, 16);
        bodyLayout->setSpacing(10);

        auto clearBody = [bodyLayout]() {
            while (QLayoutItem *item = bodyLayout->takeAt(0)) {
                if (QWidget *w = item->widget()) {
                    w->hide();
                    w->deleteLater();
                }
                delete item;
            }
        };
        auto renderBody = [=]() {
            clearBody();
            auto *body = new QLabel;
            body->setTextFormat(Qt::MarkdownText);
            body->setText(eventBody);
            body->setWordWrap(true);
            body->setTextInteractionFlags(Qt::TextBrowserInteraction);
            body->setOpenExternalLinks(true);
            const bool emptyEditableDescription =
                writable && isOpen && eventBody.trimmed().isEmpty();
            if (emptyEditableDescription) {
                body->setText("Click to add a description.");
                body->setObjectName("statusLine");
                body->setAttribute(Qt::WA_TransparentForMouseEvents, true);
                bodyContainer->setCursor(Qt::PointingHandCursor);
                bodyContainer->setMinimumHeight(52);
            } else {
                bodyContainer->unsetCursor();
                bodyContainer->setMinimumHeight(0);
            }
            bodyLayout->addWidget(body);
            for (const QString &rel : eventAttachments) {
                if (haveLocalFiles) {
                    QPixmap pix(imageBase + rel);
                    if (!pix.isNull()) {
                        auto *img = new QLabel;
                        img->setObjectName("issueAttachmentPreview");
                        img->setPixmap(pix.width() > 640
                                           ? pix.scaledToWidth(640, Qt::SmoothTransformation)
                                           : pix);
                        bodyLayout->addWidget(img);
                        continue;
                    }
                }
                auto *placeholder =
                    new QLabel(QStringLiteral("Image: %1").arg(rel));
                placeholder->setObjectName("statusLine");
                bodyLayout->addWidget(placeholder);
            }
        };
        auto showBodyEditor = [=]() {
            clearBody();
            bodyContainer->onClicked = nullptr;
            bodyContainer->unsetCursor();
            bodyContainer->setMinimumHeight(0);
            auto *editor = new MarkdownEditor(bodyContainer);
            editor->setMarkdown(eventBody);
            editor->setMinimumHeight(250);
            editor->setPlaceholderText(isOpen ? "Type your description here..."
                                              : "Type your comment here...");
            const int repoIdx = issuesRepoIndex();
            if (repoIdx >= 0)
                editor->setPreviewBasePath(m_repositories.at(repoIdx).localPath +
                                           "/issues/" + QString::number(num));
            bodyLayout->addWidget(editor);
            auto *attach = new QPushButton("Paste, drop, or click to add files");
            attach->setObjectName("ghostButton");
            attach->setCursor(Qt::PointingHandCursor);
            setOcticon(attach, "paperclip", 16);
            connect(attach, &QPushButton::clicked, this, [editor]() {
                const QStringList files = QFileDialog::getOpenFileNames(
                    editor, "Attach images", QString(),
                    "Images (*.png *.jpg *.jpeg *.gif *.webp *.bmp *.svg);;All files (*)");
                for (const QString &file : files)
                    editor->addImageFile(file);
            });
            bodyLayout->addWidget(attach, 0, Qt::AlignLeft);
            auto *buttonRow = new QHBoxLayout;
            buttonRow->setContentsMargins(0, 0, 0, 0);
            auto *cancel = new QPushButton("Cancel");
            cancel->setObjectName("ghostButton");
            cancel->setCursor(Qt::PointingHandCursor);
            auto *save = new QPushButton("Save");
            save->setObjectName("primaryButton");
            save->setCursor(Qt::PointingHandCursor);
            buttonRow->addStretch();
            buttonRow->addWidget(cancel);
            buttonRow->addWidget(save);
            bodyLayout->addLayout(buttonRow);
            connect(cancel, &QPushButton::clicked, this, [this, num]() { showIssue(num); });
            connect(save, &QPushButton::clicked, this,
                    [this, num, eid, eventAttachments, editor]() {
                        IssueStore store = issueStoreForCurrentRepo();
                        QString error;
                        if (!store.editEvent(num, eid, editor->markdown(),
                                             eventAttachments,
                                             editor->pendingAttachments(), &error)) {
                            setIssueInlineNotice(
                                error.isEmpty() ? "Could not update the issue body."
                                                : error,
                                true);
                            return;
                        }
                        setIssueInlineNotice("Issue body updated.");
                        reloadIssues();
                    });
            editor->focusEditor();
        };
        // Only intercept clicks when the body is an empty, editable description
        // (click-to-add-a-description). For real comment text, leave onClicked
        // unset so clicks reach the label and the text stays selectable —
        // including word (double-click) and paragraph (triple-click) selection.
        if (writable && isOpen && eventBody.trimmed().isEmpty())
            bodyContainer->onClicked = [=]() { showBodyEditor(); };

        QMenu *menu = new QMenu(card);
        menu->setAttribute(Qt::WA_TranslucentBackground, false);
        menu->setAutoFillBackground(true);
        menu->setWindowOpacity(1.0);
        const bool darkMenu = currentThemeIsDark();
        menu->setStyleSheet(
            QStringLiteral(
                "QMenu { background-color:%1; color:%2; border:1px solid %3; "
                "border-radius:8px; padding:6px; }"
                "QMenu::item { background-color:%1; padding:7px 26px 7px 22px; "
                "border-radius:6px; }"
                "QMenu::item:selected { background-color:%4; }"
                "QMenu::separator { height:1px; background:%3; margin:6px 0; }")
                .arg(darkMenu ? "#161b22" : "#ffffff",
                     darkMenu ? "#e6edf3" : "#1f2328",
                     darkMenu ? "#30363d" : "#d0d7de",
                     darkMenu ? "#21262d" : "#f6f8fa"));
        QAction *copyLink = menu->addAction("Copy link");
        QAction *copyMarkdown = menu->addAction("Copy Markdown");
        QAction *quoteReply = menu->addAction("Quote reply");
        connect(copyLink, &QAction::triggered, this, [this, num, eid]() {
            QString owner = "repo";
            QString repo = "issue";
            const int repoIdx = issuesRepoIndex();
            if (repoIdx >= 0) {
                owner = m_repositories.at(repoIdx).owner;
                repo = m_repositories.at(repoIdx).name;
            }
            QApplication::clipboard()->setText(
                QStringLiteral("forkmesh://issue/%1/%2/%3#%4")
                    .arg(owner, repo)
                    .arg(num)
                    .arg(eid));
            setIssueInlineNotice("Issue link copied.");
        });
        connect(copyMarkdown, &QAction::triggered, this, [this, eventBody]() {
            QApplication::clipboard()->setText(eventBody);
            setIssueInlineNotice("Markdown copied.");
        });
        connect(quoteReply, &QAction::triggered, this, [this, eventBody]() {
            if (!m_issueComposer)
                return;
            QStringList quoted;
            for (const QString &line : eventBody.split('\n'))
                quoted << QStringLiteral("> %1").arg(line);
            QString text = m_issueComposer->markdown();
            if (!text.isEmpty() && !text.endsWith('\n'))
                text += '\n';
            text += quoted.join('\n') + "\n\n";
            m_issueComposer->setMarkdown(text);
            m_issueComposer->focusEditor();
            setIssueInlineNotice("Quoted into the comment box.");
        });
        if (writable) {
            auto *editButton = new QPushButton(headerBox);
            editButton->setObjectName("issueActionButton");
            editButton->setFixedSize(30, 30);
            editButton->setCursor(Qt::PointingHandCursor);
            editButton->setToolTip(isOpen ? "Edit description" : "Edit comment");
            setOcticon(editButton, "pencil", 15);
            connect(editButton, &QPushButton::clicked, this, showBodyEditor);
            headerRow->addWidget(editButton);
        }
        auto *actionsButton = new QToolButton(headerBox);
        actionsButton->setObjectName("issueActionButton");
        actionsButton->setText("...");
        actionsButton->setCursor(Qt::PointingHandCursor);
        actionsButton->setPopupMode(QToolButton::InstantPopup);
        actionsButton->setMenu(menu);
        headerRow->addWidget(actionsButton);

        cardLayout->addWidget(headerBox);
        renderBody();
        cardLayout->addWidget(bodyContainer);
        rowLayout->addWidget(card, 1);
        m_issueThreadLayout->addWidget(row);
    };

    auto addActivity = [&](const QString &text, qint64 ts, const QString &who) {
        const QString when = QDateTime::fromMSecsSinceEpoch(ts).toString("HH:mm");
        auto *line = new QLabel(QString::fromUtf8("\xC2\xB7 %1 %2 (%3)")
                                    .arg(who.toHtmlEscaped(), text, when));
        line->setObjectName("statusLine");
        line->setWordWrap(true);
        m_issueThreadLayout->addWidget(line);
    };

    for (const IssueEvent &ev : issue.events) {
        const QString who = ev.authorName.isEmpty() ? ev.author.left(10) : ev.authorName;
        if (ev.type == "open")
            addCard(ev, true);
        else if (ev.type == "comment") {
            if (!deleted.contains(ev.id))
                addCard(ev, false);
        } else if (ev.type == "status")
            addActivity(ev.status == "closed" ? "closed this" : "reopened this", ev.ts, who);
        else if (ev.type == "labels")
            addActivity("set labels: " + ev.labels.join(", "), ev.ts, who);
        else if (ev.type == "milestone")
            addActivity(ev.milestone.isEmpty() ? "cleared the milestone"
                                               : "set milestone: " + ev.milestone,
                        ev.ts, who);
        else if (ev.type == "assignees")
            addActivity("set assignees: " + ev.assignees.join(", "), ev.ts, who);
        else if (ev.type == "agent") {
            QString text;
            if (ev.agentSessionId <= 0 || ev.agentStatus == AgentStatus::Cleared) {
                text = QStringLiteral("cleared the agent assignment");
            } else {
                text = QStringLiteral("assigned %1 session #%2")
                           .arg(agentProviderName(ev.agentProvider))
                           .arg(ev.agentSessionId);
                if (ev.agentCreatePr)
                    text += QStringLiteral(" with PR creation requested");
                if (!ev.agentStatus.isEmpty())
                    text += QStringLiteral(" (%1)").arg(agentStatusText(ev.agentStatus));
            }
            addActivity(text, ev.ts, who);
        }
    }
    m_issueThreadLayout->addStretch();
}

void MainWindow::showIssueBurnupChart()
{
    if (!m_issueDetailStack)
        return;

    auto *page = new QWidget;
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(28, 24, 28, 24);
    layout->setSpacing(14);

    auto *title = new QLabel("Issue burn-up");
    title->setObjectName("issuePageTitle");
    auto *back = new QPushButton("Back to issue");
    back->setObjectName("ghostButton");
    back->setCursor(Qt::PointingHandCursor);
    setOcticon(back, "arrow-left", 16);
    auto *titleRow = new QHBoxLayout;
    titleRow->setContentsMargins(0, 0, 0, 0);
    titleRow->addWidget(title);
    titleRow->addStretch();
    titleRow->addWidget(back);
    layout->addLayout(titleRow);

    auto *description = new QLabel(
        "Open and closed totals reconstructed from issue creation and status "
        "events. List filters do not change the chart.");
    description->setObjectName("statusLine");
    description->setWordWrap(true);
    layout->addWidget(description);

    auto *rangeGroup = new QButtonGroup(page);
    rangeGroup->setExclusive(true);
    auto *rangeRow = new QHBoxLayout;
    rangeRow->setContentsMargins(0, 0, 0, 0);
    rangeRow->setSpacing(4);
    const QStringList rangeLabels{
        QStringLiteral("Day"), QStringLiteral("Week"),
        QStringLiteral("2 weeks"), QStringLiteral("Month"),
        QStringLiteral("All time")};
    for (int i = 0; i < rangeLabels.size(); ++i) {
        auto *button = new QPushButton(rangeLabels.at(i));
        button->setObjectName("repoTab");
        button->setCheckable(true);
        button->setCursor(Qt::PointingHandCursor);
        rangeGroup->addButton(button, i);
        rangeRow->addWidget(button);
    }
    rangeRow->addStretch();
    layout->addLayout(rangeRow);

    auto *summary = new QLabel;
    summary->setTextFormat(Qt::RichText);
    summary->setWordWrap(true);
    layout->addWidget(summary);

    auto *legend = new QLabel(
        "<span style='color:#58a6ff;font-weight:700'>\xE2\x97\x8F Open</span>"
        "&nbsp;&nbsp;&nbsp;"
        "<span style='color:#3fb950;font-weight:700'>\xE2\x97\x8F Closed</span>");
    legend->setTextFormat(Qt::RichText);
    layout->addWidget(legend);

    auto *chart = new IssueBurnupChart(page);
    layout->addWidget(chart, 1);

    auto refreshChart = [this, chart, summary](int range) {
        const qint64 now = QDateTime::currentMSecsSinceEpoch();
        qint64 start = now - 24LL * 60 * 60 * 1000;
        int intervals = 24;
        QString rangeName = QStringLiteral("past day");
        if (range == 1) {
            start = now - 7LL * 24 * 60 * 60 * 1000;
            intervals = 28;
            rangeName = QStringLiteral("past week");
        } else if (range == 2) {
            start = now - 14LL * 24 * 60 * 60 * 1000;
            intervals = 28;
            rangeName = QStringLiteral("past 2 weeks");
        } else if (range == 3) {
            start = now - 30LL * 24 * 60 * 60 * 1000;
            intervals = 30;
            rangeName = QStringLiteral("past month");
        } else if (range == 4) {
            start = firstIssueHistoryTimestamp(
                m_currentIssues, now - 24LL * 60 * 60 * 1000);
            if (start >= now)
                start = now - 24LL * 60 * 60 * 1000;
            intervals = 60;
            rangeName = QStringLiteral("all time");
        }

        if (m_currentIssues.isEmpty()) {
            chart->setSeries({});
            summary->setText(
                QStringLiteral("No issues are available for the %1 range.")
                    .arg(rangeName));
            return;
        }

        const QList<IssueBurnupPoint> series =
            buildIssueBurnupSeries(m_currentIssues, start, now, intervals);
        chart->setSeries(series);
        const IssueBurnupPoint &first = series.first();
        const IssueBurnupPoint &last = series.last();
        const int firstTotal = first.openCount + first.closedCount;
        const int lastTotal = last.openCount + last.closedCount;
        auto signedNumber = [](int value) {
            return value > 0 ? QStringLiteral("+%1").arg(value)
                             : QString::number(value);
        };
        summary->setText(
            QStringLiteral(
                "<span style='font-size:22px;font-weight:800'>%1</span> open"
                "&nbsp;&nbsp;&nbsp;"
                "<span style='font-size:22px;font-weight:800'>%2</span> closed"
                "&nbsp;&nbsp;&nbsp;"
                "<span style='color:#8b949e'>%3 total &middot; %4 total and %5 "
                "net closed over the %6</span>")
                .arg(last.openCount)
                .arg(last.closedCount)
                .arg(lastTotal)
                .arg(signedNumber(lastTotal - firstTotal))
                .arg(signedNumber(last.closedCount - first.closedCount),
                     rangeName));
    };

    connect(rangeGroup, &QButtonGroup::idClicked, this, refreshChart);
    connect(back, &QPushButton::clicked, this,
            &MainWindow::removeIssueComposePage);
    rangeGroup->button(1)->setChecked(true);
    refreshChart(1);
    showIssueComposePage(page);
}

void MainWindow::showIssueComposePage(QWidget *page)
{
    if (!m_issueDetailStack || !page)
        return;
    removeIssueComposePage();
    m_issueComposePage = page;
    m_issueDetailStack->addWidget(page);
    m_issueDetailStack->setCurrentWidget(page);
    if (m_issueDetail)
        m_issueDetail->setVisible(true);
    if (m_issueDetailToggle)
        m_issueDetailToggle->setText("Hide detail");
}

void MainWindow::removeIssueComposePage()
{
    if (!m_issueDetailStack)
        return;
    if (m_issueComposePage) {
        QWidget *old = m_issueComposePage;
        m_issueComposePage = nullptr;
        m_issueDetailStack->setCurrentIndex(0);
        m_issueDetailStack->removeWidget(old);
        old->deleteLater();
    } else {
        m_issueDetailStack->setCurrentIndex(0);
    }
}

void MainWindow::setIssueInlineNotice(const QString &message, bool error)
{
    // #96: issue-created and related notices now surface in the top notification
    // toast instead of an in-page banner. The inline label stays hidden.
    if (m_issueInlineNotice)
        m_issueInlineNotice->hide();
    if (!message.trimmed().isEmpty())
        flashMessage(message, error);
}

void MainWindow::promptEditIssueTitle()
{
    if (m_currentIssueNumber < 0)
        return;
    QString currentTitle;
    for (const Issue &issue : std::as_const(m_currentIssues)) {
        if (issue.number == m_currentIssueNumber) {
            currentTitle = issue.title;
            break;
        }
    }
    if (currentTitle.isEmpty() || !m_issueTitleEditor)
        return;
    m_issueDeleteConfirmPending = false;
    setIssueInlineNotice(QString());
    m_issueTitleEditor->setText(currentTitle);
    m_issueTitle->hide();
    m_issueTitleEditButton->hide();
    m_issueTitleEditor->show();
    m_issueTitleSaveButton->show();
    m_issueTitleCancelButton->show();
    m_issueTitleEditor->setFocus();
    m_issueTitleEditor->selectAll();
}

void MainWindow::saveIssueTitleEdit()
{
    if (m_currentIssueNumber < 0 || !m_issueTitleEditor)
        return;
    const QString trimmed = m_issueTitleEditor->text().trimmed();
    if (trimmed.isEmpty()) {
        setIssueInlineNotice("A title is required.", true);
        return;
    }
    QString currentTitle;
    for (const Issue &issue : std::as_const(m_currentIssues))
        if (issue.number == m_currentIssueNumber)
            currentTitle = issue.title;
    if (trimmed == currentTitle) {
        cancelIssueTitleEdit();
        return;
    }

    IssueStore store = issueStoreForCurrentRepo();
    QString error;
    if (!store.setTitle(m_currentIssueNumber, trimmed, &error)) {
        setIssueInlineNotice(error.isEmpty() ? "Could not update the title." : error,
                             true);
        return;
    }
    cancelIssueTitleEdit();
    setIssueInlineNotice("Title updated.");
    reloadIssues();
}

void MainWindow::cancelIssueTitleEdit()
{
    if (m_issueTitle)
        m_issueTitle->show();
    if (m_issueTitleEditButton)
        m_issueTitleEditButton->show();
    if (m_issueTitleEditor)
        m_issueTitleEditor->hide();
    if (m_issueTitleSaveButton)
        m_issueTitleSaveButton->hide();
    if (m_issueTitleCancelButton)
        m_issueTitleCancelButton->hide();
}

void MainWindow::updateIssueActionState()
{
    const IssueStore store = issueStoreForCurrentRepo();
    const bool writable = store.canWrite();
    const bool haveIssue = m_currentIssueNumber >= 0;

    if (m_issueNewButton)
        m_issueNewButton->setEnabled(writable);
    if (m_issueSyncButton)
        m_issueSyncButton->setEnabled(writable);
    if (m_issueTitleEditButton)
        m_issueTitleEditButton->setEnabled(writable && haveIssue);
    if (m_issueTitleEditor)
        m_issueTitleEditor->setEnabled(writable && haveIssue);
    if (m_issueTitleSaveButton)
        m_issueTitleSaveButton->setEnabled(writable && haveIssue);
    if (m_issueTitleCancelButton)
        m_issueTitleCancelButton->setEnabled(haveIssue);
    if (!haveIssue || !writable)
        m_issueDeleteConfirmPending = false;
    // Owner-only structural edits.
    for (QPushButton *b : {m_issueCloseButton, m_issueLabelsButton,
                           m_issueMilestoneButton, m_issueAssigneesButton,
                           m_issueDeleteButton, m_issueAttachButton,
                           m_issueAssignCodexButton, m_issueAssignClaudeButton}) {
        if (b)
            b->setEnabled(writable && haveIssue);
    }
    if (m_issueAgentCreatePrCheck)
        m_issueAgentCreatePrCheck->setEnabled(writable && haveIssue);
    if (m_issueAgentViewButton)
        m_issueAgentViewButton->setEnabled(haveIssue &&
                                           latestAgentSessionForIssue(m_currentIssueNumber));
    // Comments work for everyone with an issue selected: owners write locally,
    // others submit a signed comment to the relay inbox.
    if (m_issueCommentButton)
        m_issueCommentButton->setEnabled(haveIssue);
    if (m_issueCopyButton)
        m_issueCopyButton->setEnabled(haveIssue);
    if (m_issueComposer)
        m_issueComposer->setEnabled(haveIssue);
    updateVoteUi();

    // Reflect current status on the close/reopen button.
    if (m_issueCloseButton && haveIssue) {
        for (const Issue &issue : m_currentIssues) {
            if (issue.number == m_currentIssueNumber) {
                m_issueCloseButton->setText(issue.status == "closed" ? "Reopen"
                                                                     : "Close issue");
                break;
            }
        }
    }
    if (m_issueReadonlyNote) {
        m_issueReadonlyNote->setVisible(!writable && issuesRepoIndex() >= 0);
        m_issueReadonlyNote->setText(
            "You don't host this repository \xE2\x80\x94 comments are sent to the "
            "maintainer's inbox (text only). New issues and edits are owner-only.");
    }
    if (m_issueCommentButton)
        m_issueCommentButton->setText(writable ? "Comment" : "Send to maintainer");
}

void MainWindow::promptNewIssue()
{
    const IssueStore probe = issueStoreForCurrentRepo();
    if (!probe.canWrite())
        return;

    auto *page = new QWidget;

    auto *titleLabel = new QLabel("Add a title <span style='color:#cf222e'>*</span>",
                                  page);
    titleLabel->setTextFormat(Qt::RichText);
    titleLabel->setObjectName("sectionLabel");
    auto *titleEdit = new QLineEdit(page);
    titleEdit->setPlaceholderText("Title");
    auto *bodyEdit = new MarkdownEditor(page);
    bodyEdit->setMinimumHeight(430);
    bodyEdit->setPlaceholderText("Type your description here...");

    auto *left = new QWidget(page);
    auto *leftLayout = new QVBoxLayout(left);
    leftLayout->setContentsMargins(0, 0, 0, 0);
    leftLayout->setSpacing(8);
    leftLayout->addWidget(titleLabel);
    leftLayout->addWidget(titleEdit);
    auto *descriptionLabel = new QLabel("Add a description", page);
    descriptionLabel->setObjectName("sectionLabel");
    leftLayout->addSpacing(8);
    leftLayout->addWidget(descriptionLabel);
    leftLayout->addWidget(bodyEdit, 1);
    auto *attachHint = new QLabel("Paste, drop, or click Image to add files", page);
    attachHint->setObjectName("statusLine");
    leftLayout->addWidget(attachHint);

    auto *sidebar = new QWidget(page);
    sidebar->setObjectName("issueComposeSidebar");
    sidebar->setFixedWidth(285);
    auto *sideLayout = new QVBoxLayout(sidebar);
    sideLayout->setContentsMargins(18, 2, 0, 0);
    sideLayout->setSpacing(8);

    auto addDivider = [&]() {
        auto *line = new QWidget(sidebar);
        line->setFixedHeight(1);
        line->setStyleSheet(QStringLiteral("background:%1;")
                                .arg(currentThemeIsDark() ? "#30363d" : "#d0d7de"));
        sideLayout->addWidget(line);
    };
    auto addSection = [&](const QString &label, QWidget *field) {
        auto *header = new QHBoxLayout;
        header->setContentsMargins(0, 0, 0, 0);
        auto *title = new QLabel(label, sidebar);
        title->setObjectName("sectionLabel");
        auto *gear = new QPushButton(sidebar);
        gear->setObjectName("ghostButton");
        gear->setProperty("buttonSize", "sm");
        gear->setFixedSize(28, 28);
        gear->setEnabled(false);
        setOcticon(gear, "gear", 14);
        header->addWidget(title);
        header->addStretch();
        header->addWidget(gear);
        sideLayout->addLayout(header);
        sideLayout->addWidget(field);
        sideLayout->addSpacing(6);
        addDivider();
        sideLayout->addSpacing(6);
    };

    auto *assigneeBox = new QWidget(sidebar);
    auto *assigneeLayout = new QVBoxLayout(assigneeBox);
    assigneeLayout->setContentsMargins(0, 0, 0, 0);
    assigneeLayout->setSpacing(6);
    auto *assigneesEdit = new QLineEdit(sidebar);
    assigneesEdit->setPlaceholderText("No one");
    auto *assignSelf = new QPushButton("Assign yourself", sidebar);
    assignSelf->setObjectName("ghostButton");
    assignSelf->setProperty("buttonSize", "sm");
    assignSelf->setCursor(Qt::PointingHandCursor);
    connect(assignSelf, &QPushButton::clicked, this, [this, assigneesEdit] {
        const QString who = m_userName.trimmed();
        if (who.isEmpty())
            return;
        QStringList assignees = splitIssueFieldList(assigneesEdit->text());
        if (!assignees.contains(who))
            assignees << who;
        assigneesEdit->setText(assignees.join(", "));
    });
    assigneeLayout->addWidget(assigneesEdit);
    assigneeLayout->addWidget(assignSelf, 0, Qt::AlignLeft);
    addSection("Assignees", assigneeBox);

    auto *labelsEdit = new QLineEdit(sidebar);
    labelsEdit->setPlaceholderText("No labels");
    addSection("Labels", labelsEdit);

    auto *typeValue = new QLabel("No type", sidebar);
    typeValue->setObjectName("statusLine");
    addSection("Type", typeValue);

    auto *fieldsBox = new QWidget(sidebar);
    auto *fieldsLayout = new QHBoxLayout(fieldsBox);
    fieldsLayout->setContentsMargins(0, 0, 0, 0);
    auto *priority = new QLabel("Priority", fieldsBox);
    priority->setObjectName("statusLine");
    auto *priorityValue = new QLabel("Choose an option", fieldsBox);
    priorityValue->setObjectName("statusLine");
    fieldsLayout->addWidget(priority);
    fieldsLayout->addStretch();
    fieldsLayout->addWidget(priorityValue);
    addSection("Fields", fieldsBox);

    auto *projectsValue = new QLabel("No projects", sidebar);
    projectsValue->setObjectName("statusLine");
    addSection("Projects", projectsValue);

    auto *milestoneCombo = new QComboBox(sidebar);
    milestoneCombo->addItem("No milestone", QString());
    for (const IssueMilestone &ms : m_currentMilestones)
        milestoneCombo->addItem(ms.title, ms.title);
    addSection("Milestone", milestoneCombo);
    sideLayout->addStretch();

    auto *content = new QHBoxLayout;
    content->setContentsMargins(0, 0, 0, 0);
    content->setSpacing(22);
    content->addWidget(left, 1);
    content->addWidget(sidebar);

    auto *createMore = new QCheckBox("Create more", page);
    auto *cancelButton = new QPushButton("Cancel", page);
    cancelButton->setObjectName("ghostButton");
    cancelButton->setCursor(Qt::PointingHandCursor);
    auto *createButton = new QPushButton("Create", page);
    createButton->setObjectName("primaryButton");
    createButton->setCursor(Qt::PointingHandCursor);
    setOcticon(createButton, "issue-opened", 16);
    auto *pageNotice = new QLabel(page);
    pageNotice->setObjectName("issueInlineNotice");
    pageNotice->setWordWrap(true);
    pageNotice->hide();
    auto setPageNotice = [pageNotice](const QString &message, bool error = false) {
        if (message.trimmed().isEmpty()) {
            pageNotice->clear();
            pageNotice->hide();
            return;
        }
        const bool dark = currentThemeIsDark();
        const QString bg = error ? (dark ? "#3d1f21" : "#ffebe9")
                                 : (dark ? "#11251a" : "#dafbe1");
        const QString border = error ? (dark ? "#f85149" : "#cf222e")
                                     : (dark ? "#2ea043" : "#1f883d");
        const QString fg = dark ? "#e6edf3" : "#1f2328";
        pageNotice->setStyleSheet(
            QStringLiteral("QLabel#issueInlineNotice { background-color:%1; color:%2; "
                           "border:1px solid %3; border-radius:6px; padding:8px 10px; }")
                .arg(bg, fg, border));
        pageNotice->setText(message.toHtmlEscaped());
        pageNotice->show();
    };
    auto *buttonRow = new QHBoxLayout;
    buttonRow->setContentsMargins(0, 0, 0, 0);
    buttonRow->addStretch();
    buttonRow->addWidget(createMore);
    buttonRow->addSpacing(18);
    buttonRow->addWidget(cancelButton);
    buttonRow->addWidget(createButton);

    auto *pageLayout = new QVBoxLayout(page);
    pageLayout->setContentsMargins(22, 18, 16, 14);
    pageLayout->setSpacing(14);
    pageLayout->addLayout(content, 1);
    pageLayout->addWidget(pageNotice);
    pageLayout->addLayout(buttonRow);
    connect(cancelButton, &QPushButton::clicked, this,
            &MainWindow::removeIssueComposePage);
    connect(createButton, &QPushButton::clicked, this, [=] {
        if (titleEdit->text().trimmed().isEmpty()) {
            setPageNotice("A title is required.", true);
            return;
        }

        const QString title = titleEdit->text().trimmed();
        const QStringList labels = splitIssueFieldList(labelsEdit->text());
        const QStringList assignees = splitIssueFieldList(assigneesEdit->text());

        IssueStore store = issueStoreForCurrentRepo();
        QString error;
        const int number = store.createIssue(title, bodyEdit->markdown(), labels,
                                             milestoneCombo->currentData().toString(),
                                             assignees,
                                             bodyEdit->pendingAttachments(),
                                             &error);
        if (number < 0) {
            setPageNotice(error.isEmpty() ? "Could not create the issue." : error,
                          true);
            return;
        }
        m_currentIssueNumber = number;
        const bool more = createMore->isChecked();
        removeIssueComposePage();
        reloadIssues();
        setIssueInlineNotice("Issue created.");
        if (more)
            promptNewIssue();
    });

    showIssueComposePage(page);
    titleEdit->setFocus();
}

void MainWindow::quickAddIssue()
{
    if (!m_issueQuickAdd)
        return;
    const QString title = m_issueQuickAdd->text().trimmed();
    if (title.isEmpty())
        return;
    IssueStore store = issueStoreForCurrentRepo();
    if (!store.canWrite()) {
        setIssueInlineNotice(issuesRepoIndex() < 0
                                 ? "Pick a repository you host to add issues."
                                 : "You don't host this repository, so new issues are owner-only.",
                             true);
        return;
    }
    QString error;
    const int number = store.createIssue(title, QString(), {}, QString(), {}, {},
                                         &error);
    if (number < 0) {
        setIssueInlineNotice(error.isEmpty() ? "Could not create the issue." : error,
                             true);
        return;
    }
    m_issueQuickAdd->clear();
    m_currentIssueNumber = number;
    reloadIssues();
    setIssueInlineNotice("Issue created.");
    // If requested, hand the freshly-created issue straight to a coding agent.
    if (m_quickAddAssignAgent && m_quickAddAssignAgent->isChecked()) {
        const QString provider =
            m_quickAddAgentProvider
                ? m_quickAddAgentProvider->currentData().toString()
                : QStringLiteral("codex");
        const bool oldCreatePr =
            m_issueAgentCreatePrCheck && m_issueAgentCreatePrCheck->isChecked();
        if (m_issueAgentCreatePrCheck) {
            const QSignalBlocker block(m_issueAgentCreatePrCheck);
            m_issueAgentCreatePrCheck->setChecked(m_quickAddCreatePr &&
                                                  m_quickAddCreatePr->isChecked());
            assignIssueToAgent(provider);
            m_issueAgentCreatePrCheck->setChecked(oldCreatePr);
        } else {
            assignIssueToAgent(provider);
        }
    }
}

void MainWindow::copyIssueToClipboard()
{
    if (m_currentIssueNumber < 0)
        return;
    const Issue *issue = nullptr;
    for (const Issue &candidate : m_currentIssues)
        if (candidate.number == m_currentIssueNumber)
            issue = &candidate;
    if (!issue)
        return;

    // Pre-compute edits (target -> latest body) and deletions, mirroring
    // renderIssueThread, so the copied text matches what's on screen.
    QHash<QString, QString> edits;
    QSet<QString> deleted;
    for (const IssueEvent &ev : issue->events) {
        if (ev.type == "edit" && !ev.target.isEmpty())
            edits.insert(ev.target, ev.body);
        else if (ev.type == "delete" && !ev.target.isEmpty() && ev.target != "self")
            deleted.insert(ev.target);
    }

    QStringList lines;
    lines << QStringLiteral("#%1 %2").arg(issue->number).arg(issue->title);
    lines << QStringLiteral("Status: %1").arg(issue->status);
    if (!issue->labels.isEmpty())
        lines << "Labels: " + issue->labels.join(", ");
    if (!issue->milestone.isEmpty())
        lines << "Milestone: " + issue->milestone;
    if (!issue->assignees.isEmpty())
        lines << "Assignees: " + issue->assignees.join(", ");

    for (const IssueEvent &ev : issue->events) {
        if (ev.type != "open" && ev.type != "comment")
            continue;
        if (deleted.contains(ev.id))
            continue;
        const QString who = ev.authorName.isEmpty() ? ev.author.left(10) : ev.authorName;
        const QString when =
            QDateTime::fromMSecsSinceEpoch(ev.ts).toString("yyyy-MM-dd HH:mm");
        const QString body = edits.contains(ev.id) ? edits.value(ev.id) : ev.body;
        lines << QString() << QStringLiteral("--- %1 (%2) ---").arg(who, when) << body;
    }

    QApplication::clipboard()->setText(lines.join('\n'));
    setIssueInlineNotice("Issue copied.");
}

void MainWindow::addIssueComment()
{
    if (m_currentIssueNumber < 0)
        return;
    const QString body = m_issueComposer ? m_issueComposer->markdown() : QString();
    const QStringList attachments =
        m_issueComposer ? m_issueComposer->pendingAttachments() : m_pendingIssueAttachments;
    if (body.trimmed().isEmpty() && attachments.isEmpty())
        return;
    IssueStore store = issueStoreForCurrentRepo();
    if (!store.canWrite()) {
        // Not the host: send a signed comment to the maintainer's relay inbox.
        submitIssueCommentToInbox(body);
        return;
    }
    QString error;
    if (!store.addComment(m_currentIssueNumber, body, attachments, &error)) {
        setIssueInlineNotice(error.isEmpty() ? "Could not add the comment." : error,
                             true);
        return;
    }
    if (m_issueComposer) {
        m_issueComposer->setMarkdown(QString());
        m_issueComposer->clearPendingAttachments();
    }
    m_pendingIssueAttachments.clear();
    if (m_issueAttachButton)
        m_issueAttachButton->setText("Paste, drop, or click to add files");
    reloadIssues();
    setIssueInlineNotice("Comment added.");
}

void MainWindow::attachIssueImage()
{
    const QStringList files = QFileDialog::getOpenFileNames(
        this, "Attach images", QString(),
        "Images (*.png *.jpg *.jpeg *.gif *.webp);;All files (*)");
    if (files.isEmpty())
        return;
    for (const QString &f : files)
        queueIssueAttachment(f);
}

void MainWindow::queueIssueAttachment(const QString &path)
{
    if (path.isEmpty() || m_pendingIssueAttachments.contains(path))
        return;
    m_pendingIssueAttachments += path;
    if (m_issueComposer)
        m_issueComposer->addImageFile(path);
    if (m_issueAttachButton)
        m_issueAttachButton->setText(
            QStringLiteral("Attached: %1").arg(m_pendingIssueAttachments.size()));
}

bool MainWindow::eventFilter(QObject *obj, QEvent *event)
{
    return QMainWindow::eventFilter(obj, event);
}

void MainWindow::toggleIssueStatus()
{
    if (m_currentIssueNumber < 0)
        return;
    QString status = "open";
    for (const Issue &issue : m_currentIssues)
        if (issue.number == m_currentIssueNumber)
            status = issue.status;
    IssueStore store = issueStoreForCurrentRepo();
    QString error;
    if (!store.setStatus(m_currentIssueNumber, status == "open" ? "closed" : "open",
                         &error)) {
        setIssueInlineNotice(error.isEmpty() ? "Could not update issue status." : error,
                             true);
        return;
    }
    reloadIssues();
    setIssueInlineNotice(status == "open" ? "Issue closed." : "Issue reopened.");
}

void MainWindow::deleteCurrentIssue()
{
    if (m_currentIssueNumber < 0)
        return;
    if (!m_issueDeleteConfirmPending) {
        m_issueDeleteConfirmPending = true;
        setIssueInlineNotice(
            QStringLiteral("Delete issue #%1? Click Delete issue again to confirm.")
                .arg(m_currentIssueNumber),
            true);
        return;
    }
    IssueStore store = issueStoreForCurrentRepo();
    QString error;
    if (!store.deleteIssue(m_currentIssueNumber, &error)) {
        setIssueInlineNotice(error.isEmpty() ? "Could not delete the issue." : error,
                             true);
        return;
    }
    m_issueDeleteConfirmPending = false;
    m_currentIssueNumber = -1;
    reloadIssues();
    setIssueInlineNotice("Issue deleted.");
}

void MainWindow::editIssueLabels()
{
    if (m_currentIssueNumber < 0)
        return;
    m_issueDeleteConfirmPending = false;
    setIssueInlineNotice(QString());
    if (m_issueLabelsStack)
        m_issueLabelsStack->setCurrentIndex(1);
    if (m_issueLabelsEdit) {
        m_issueLabelsEdit->setFocus();
        m_issueLabelsEdit->selectAll();
    }
}

void MainWindow::saveIssueLabelsInline()
{
    if (m_currentIssueNumber < 0 || !m_issueLabelsEdit)
        return;
    IssueStore store = issueStoreForCurrentRepo();
    QString error;
    const QStringList labels = splitIssueFieldList(m_issueLabelsEdit->text());
    if (!store.setLabels(m_currentIssueNumber, labels, &error)) {
        setIssueInlineNotice(error.isEmpty() ? "Could not update labels." : error,
                             true);
        return;
    }
    setIssueInlineNotice("Labels updated.");
    reloadIssues();
}

void MainWindow::editIssueMilestone()
{
    if (m_currentIssueNumber < 0)
        return;
    m_issueDeleteConfirmPending = false;
    setIssueInlineNotice(QString());
    if (m_issueMilestoneStack)
        m_issueMilestoneStack->setCurrentIndex(1);
    if (m_issueMilestoneEdit)
        m_issueMilestoneEdit->setFocus();
}

void MainWindow::saveIssueMilestoneInline()
{
    if (m_currentIssueNumber < 0 || !m_issueMilestoneEdit)
        return;
    IssueStore store = issueStoreForCurrentRepo();
    QString error;
    const QString milestone = m_issueMilestoneEdit->currentData().toString();
    if (!store.setMilestone(m_currentIssueNumber, milestone, &error)) {
        setIssueInlineNotice(error.isEmpty() ? "Could not update milestone." : error,
                             true);
        return;
    }
    setIssueInlineNotice("Milestone updated.");
    reloadIssues();
}

void MainWindow::editIssueAssignees()
{
    if (m_currentIssueNumber < 0)
        return;
    m_issueDeleteConfirmPending = false;
    setIssueInlineNotice(QString());
    if (m_issueAssigneesStack)
        m_issueAssigneesStack->setCurrentIndex(1);
    if (m_issueAssigneesEdit) {
        m_issueAssigneesEdit->setFocus();
        m_issueAssigneesEdit->selectAll();
    }
}

void MainWindow::saveIssueAssigneesInline()
{
    if (m_currentIssueNumber < 0 || !m_issueAssigneesEdit)
        return;
    IssueStore store = issueStoreForCurrentRepo();
    QString error;
    const QStringList assignees = splitIssueFieldList(m_issueAssigneesEdit->text());
    if (!store.setAssignees(m_currentIssueNumber, assignees, &error)) {
        setIssueInlineNotice(error.isEmpty() ? "Could not update assignees." : error,
                             true);
        return;
    }
    setIssueInlineNotice("Assignees updated.");
    reloadIssues();
}

void MainWindow::cancelIssueSidebarEditors()
{
    if (m_issueAssigneesStack)
        m_issueAssigneesStack->setCurrentIndex(0);
    if (m_issueLabelsStack)
        m_issueLabelsStack->setCurrentIndex(0);
    if (m_issueMilestoneStack)
        m_issueMilestoneStack->setCurrentIndex(0);
}

QUrl MainWindow::issuesApiUrl(const RepositoryRecord &repo) const
{
    QUrl url = catalogApiUrl();
    url.setPath("/api/repo/" + repoSegment(repo.owner, QStringLiteral("owner")) +
                "/" + repoSegment(repo.name, QStringLiteral("repository")) +
                "/issues");
    return url;
}

void MainWindow::submitIssueCommentToInbox(const QString &body)
{
    const int idx = issuesRepoIndex();
    if (idx < 0)
        return;
    const RepositoryRecord &repo = m_repositories.at(idx);

    QString text = body;
    while (text.endsWith('\n') || text.endsWith('\r'))
        text.chop(1);

    IssueStore store = issueStoreForCurrentRepo();
    IssueEvent ev;
    ev.type = "comment";
    ev.body = text;
    ev = store.makeSignedEvent(m_currentIssueNumber, ev);
    // bodyFile isn't part of the signature; name it after the (now-assigned) id
    // so the maintainer's node stores it predictably.
    ev.bodyFile = "comments/" + ev.id + ".md";

    QJsonObject eventJson = ev.toJson();
    eventJson.insert("body", ev.body); // worker needs the text to verify the sig
    const QJsonObject payload{{"owner", repo.owner},
                              {"repo", repo.name},
                              {"number", m_currentIssueNumber},
                              {"event", eventJson}};

    QNetworkRequest request(issuesApiUrl(repo));
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    QNetworkReply *reply = m_networkAccess->post(
        request, QJsonDocument(payload).toJson(QJsonDocument::Compact));
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        reply->deleteLater();
        if (reply->error() == QNetworkReply::NoError) {
            if (m_issueComposer) {
                m_issueComposer->setMarkdown(QString());
                m_issueComposer->clearPendingAttachments();
            }
            setIssueInlineNotice(
                "Your signed comment was delivered to the maintainer's inbox.");
        } else {
            setIssueInlineNotice("Could not send the comment: " + reply->errorString(),
                                 true);
        }
    });
}

int MainWindow::availableCredits() const
{
    const qint64 live =
        m_connectedAtMs > 0 ? QDateTime::currentMSecsSinceEpoch() - m_connectedAtMs : 0;
    const int earned = int((m_totalConnectionMs + live) / 3600000); // 1 per hour
    const int spent = QSettings().value(kVotesSpentSetting).toInt();
    return std::max(0, earned - spent);
}

void MainWindow::submitIssueVoteToInbox()
{
    const int idx = issuesRepoIndex();
    if (idx < 0)
        return;
    const RepositoryRecord &repo = m_repositories.at(idx);
    IssueStore store = issueStoreForCurrentRepo();
    IssueEvent ev;
    ev.type = "vote";
    ev = store.makeSignedEvent(m_currentIssueNumber, ev);
    const QJsonObject payload{{"owner", repo.owner},
                              {"repo", repo.name},
                              {"number", m_currentIssueNumber},
                              {"event", ev.toJson()}};
    QNetworkRequest request(issuesApiUrl(repo));
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    QNetworkReply *reply = m_networkAccess->post(
        request, QJsonDocument(payload).toJson(QJsonDocument::Compact));
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError)
            setIssueInlineNotice("Could not send your vote: " + reply->errorString(),
                                 true);
    });
}

void MainWindow::voteOnCurrentIssue()
{
    const int idx = issuesRepoIndex();
    if (idx < 0 || m_currentIssueNumber < 0)
        return;
    const RepositoryRecord &repo = m_repositories.at(idx);
    const QString key = repo.owner + "/" + repo.name + "#" +
                        QString::number(m_currentIssueNumber);
    QStringList voted = QSettings().value(kVotedSetting).toStringList();
    // Repeat voting is allowed now; you may keep voting as long as you have
    // credits (each vote spends one).
    if (availableCredits() <= 0) {
        setIssueInlineNotice(
            "No voting credits yet. You earn 1 credit for every hour online.",
            true);
        return;
    }

    IssueStore store = issueStoreForCurrentRepo();
    if (store.canWrite()) {
        QString error;
        if (!store.addVote(m_currentIssueNumber, &error)) {
            setIssueInlineNotice(error.isEmpty() ? "Could not record your vote." : error,
                                 true);
            return;
        }
    } else {
        // Not the host: submit a signed vote to the maintainer's inbox.
        submitIssueVoteToInbox();
        setIssueInlineNotice("Your signed vote was sent to the maintainer's inbox.");
    }

    // Spend a credit; credits are the only limit on voting now. We still note
    // which issues you've voted on (deduped) for reference, but it no longer
    // blocks further votes.
    QSettings s;
    s.setValue(kVotesSpentSetting, s.value(kVotesSpentSetting).toInt() + 1);
    if (!voted.contains(key)) {
        voted << key;
        s.setValue(kVotedSetting, voted);
    }
    reloadIssues();
    setIssueInlineNotice("Vote recorded.");
    updateVoteUi();
}

void MainWindow::updateVoteUi()
{
    if (m_issueCreditsLabel)
        m_issueCreditsLabel->setText(
            QStringLiteral("Credits: %1").arg(availableCredits()));
    if (!m_issueVoteButton)
        return;
    const bool haveIssue = m_currentIssueNumber >= 0;
    int votes = 0;
    if (haveIssue) {
        for (const Issue &issue : m_currentIssues)
            if (issue.number == m_currentIssueNumber)
                votes = issue.votes;
    }
    const int credits = availableCredits();
    m_issueVoteButton->setText(
        QStringLiteral("Vote (%1)").arg(votes));
    // You can vote repeatedly as long as you have credits; each vote spends one.
    m_issueVoteButton->setEnabled(haveIssue && credits > 0);
    m_issueVoteButton->setToolTip(
        credits > 0
            ? QStringLiteral("Upvote this issue (spends 1 of %1 voting credits)")
                  .arg(credits)
            : QStringLiteral("No voting credits yet \xE2\x80\x94 you earn 1 per "
                             "hour online"));
}

void MainWindow::syncIssuesInbox()
{
    const int idx = issuesRepoIndex();
    if (idx < 0)
        return;
    const RepositoryRecord &repo = m_repositories.at(idx);
    if (!issueStoreForCurrentRepo().canWrite())
        return;

    const QString owner = repoSegment(repo.owner, QStringLiteral("owner"));
    const QString ts = QString::number(QDateTime::currentMSecsSinceEpoch());
    const QByteArray canonical =
        ("forkmesh-issues-pull-v1\n" + owner + "\n" + ts).toUtf8();
    const QString sig = m_profileIdentity.signData(canonical);

    QUrl url = issuesApiUrl(repo);
    QUrlQuery query;
    query.addQueryItem("owner", owner);
    query.addQueryItem("ts", ts);
    query.addQueryItem("sig", sig);
    url.setQuery(query);

    QNetworkReply *reply = m_networkAccess->get(QNetworkRequest(url));
    connect(reply, &QNetworkReply::finished, this, [this, reply, repo, url] {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            setIssueInlineNotice("Could not reach the inbox: " + reply->errorString(),
                                 true);
            return;
        }
        const QJsonObject root =
            QJsonDocument::fromJson(reply->readAll()).object();
        const QJsonArray pending = root.value("pending").toArray();
        if (pending.isEmpty()) {
            setIssueInlineNotice("No pending submissions.");
            return;
        }
        IssueStore store = issueStoreForCurrentRepo();
        int merged = 0;
        for (const QJsonValue &value : pending) {
            const QJsonObject item = value.toObject();
            const int number = item.value("number").toInt();
            const QJsonObject eventObj = item.value("event").toObject();
            IssueEvent ev = IssueEvent::fromJson(eventObj);
            ev.body = eventObj.value("body").toString();
            if (store.applyRemoteEvent(number, ev,
                                       item.value("titleIfNew").toString()))
                ++merged;
        }
        // Acknowledge so the inbox clears the merged submissions.
        m_networkAccess->deleteResource(QNetworkRequest(url));
        reloadIssues();
        setIssueInlineNotice(
            QStringLiteral("Merged %1 submission(s) into issues/.").arg(merged));
    });
}

QWidget *MainWindow::buildChatSection()
{
    auto *page = new QWidget;

    // Sidebar
    auto *sidebar = new QWidget;
    sidebar->setObjectName("sidebar");
    sidebar->setFixedWidth(280);

    auto *workspace = new QLabel("<span style='color:#22c55e'>Fork</span>Mesh");
    workspace->setObjectName("workspaceName");
    m_statusLine = new QLabel;
    m_statusLine->setObjectName("statusLine");
    m_statusLine->setWordWrap(true);

    auto *channelsLabel = new QLabel("REPOSITORY CHATS");
    channelsLabel->setObjectName("sectionLabel");
    m_channelList = new QListWidget;
    auto *addChannelButton = new QPushButton("+ Add chat");
    addChannelButton->setObjectName("ghostButton");
    addChannelButton->setCursor(Qt::PointingHandCursor);

    auto *dmsLabel = new QLabel("DIRECT MESSAGES");
    dmsLabel->setObjectName("sectionLabel");
    m_dmList = new QListWidget;
    // Members list removed: nodes are the members. Use the Node dropdown and the
    // node profile's "Message" button to start a direct chat.

    auto *badgeRow = new QHBoxLayout;
    badgeRow->setContentsMargins(0, 0, 0, 0);
    badgeRow->addWidget(workspace);
    badgeRow->addStretch();

    auto *sidebarLayout = new QVBoxLayout(sidebar);
    sidebarLayout->setContentsMargins(14, 16, 14, 12);
    sidebarLayout->setSpacing(6);
    sidebarLayout->addLayout(badgeRow);
    sidebarLayout->addWidget(m_statusLine);
    sidebarLayout->addWidget(channelsLabel);
    sidebarLayout->addWidget(m_channelList, 2);
    sidebarLayout->addWidget(addChannelButton);
    sidebarLayout->addWidget(dmsLabel);
    sidebarLayout->addWidget(m_dmList, 1);
    sidebarLayout->addStretch();

    // Main column
    auto *header = new QWidget;
    header->setObjectName("chatHeader");
    m_channelTitle = new QLabel("#general");
    m_channelTitle->setObjectName("channelTitle");
    m_encryptionLabel = new QLabel;
    m_encryptionLabel->setObjectName("encryptionLabel");
    auto *settingsButton = new QPushButton(QString());
    settingsButton->setObjectName("iconButton");
    settingsButton->setCursor(Qt::PointingHandCursor);
    settingsButton->setToolTip("Settings & network log");
    setOcticon(settingsButton, "gear", 18);
    connect(settingsButton, &QPushButton::clicked, this, [this] { showSection(1); }); // Settings
    auto *headerLayout = new QHBoxLayout(header);
    headerLayout->setContentsMargins(18, 12, 18, 12);
    headerLayout->addWidget(m_channelTitle);
    headerLayout->addStretch();
    headerLayout->addWidget(m_encryptionLabel);
    headerLayout->addSpacing(8);
    headerLayout->addWidget(settingsButton);

    // Firewall banner: hidden until the backend reports the host firewall is
    // blocking ForkMesh, then offers a one-click "Allow through firewall".
    m_firewallBanner = new QWidget;
    m_firewallBanner->setObjectName("firewallBanner");
    m_firewallBannerLabel = new QLabel;
    m_firewallBannerLabel->setObjectName("firewallBannerLabel");
    m_firewallBannerLabel->setWordWrap(true);
    m_firewallAllowButton = new QPushButton("Allow through firewall");
    m_firewallAllowButton->setObjectName("primaryButton");
    m_firewallAllowButton->setCursor(Qt::PointingHandCursor);
    auto *firewallDismiss = new QPushButton(QString());
    firewallDismiss->setObjectName("ghostButton");
    firewallDismiss->setCursor(Qt::PointingHandCursor);
    firewallDismiss->setToolTip("Dismiss");
    setOcticon(firewallDismiss, "x", 16);
    auto *firewallLayout = new QHBoxLayout(m_firewallBanner);
    firewallLayout->setContentsMargins(16, 10, 12, 10);
    firewallLayout->setSpacing(10);
    firewallLayout->addWidget(m_firewallBannerLabel, 1);
    firewallLayout->addWidget(m_firewallAllowButton);
    firewallLayout->addWidget(firewallDismiss);
    m_firewallBanner->hide();
    connect(m_firewallAllowButton, &QPushButton::clicked, this,
            &MainWindow::allowFirewall);
    connect(firewallDismiss, &QPushButton::clicked, m_firewallBanner,
            &QWidget::hide);

    // Scrollable column of message-row widgets (supports avatars, inline
    // images, animated GIFs, file chips, and reaction bars).
    m_messageScroll = new QScrollArea;
    m_messageScroll->setObjectName("messageView");
    m_messageScroll->setWidgetResizable(true);
    m_messageScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_messageContainer = new QWidget;
    m_messageContainer->setObjectName("messageContainer");
    m_messageLayout = new QVBoxLayout(m_messageContainer);
    m_messageLayout->setContentsMargins(4, 8, 4, 8);
    m_messageLayout->setSpacing(0);
    m_messageLayout->addStretch();
    m_messageScroll->setWidget(m_messageContainer);

    // Keep the newest message visible. A row added to the layout grows the
    // scroll range asynchronously, so we can't reliably scroll the instant we
    // insert it; instead, whenever the range grows while we're pinned to the
    // bottom, jump to the new maximum. The user scrolling up clears the pin so
    // we don't drag them back down while they read history.
    QScrollBar *vbar = m_messageScroll->verticalScrollBar();
    connect(vbar, &QScrollBar::rangeChanged, this, [this](int, int max) {
        if (m_stickToBottom)
            m_messageScroll->verticalScrollBar()->setValue(max);
    });
    connect(vbar, &QScrollBar::valueChanged, this, [this](int value) {
        QScrollBar *bar = m_messageScroll->verticalScrollBar();
        m_stickToBottom = value >= bar->maximum() - 4;
    });

    auto *composer = new QWidget;
    composer->setObjectName("composerBar");
    auto *attachButton = new QPushButton(QString());
    attachButton->setObjectName("iconButton");
    attachButton->setCursor(Qt::PointingHandCursor);
    attachButton->setToolTip("Share a file (any type, including GIFs)");
    setOcticon(attachButton, "paperclip", 18);
    connect(attachButton, &QPushButton::clicked, this, &MainWindow::attachFile);
    m_messageInput = new QLineEdit;
    m_messageInput->setObjectName("messageInput");
    m_messageInput->setPlaceholderText("Message #general");
    m_messageInput->setMaxLength(16000);
    auto *sendButton = new QPushButton("Send");
    sendButton->setObjectName("primaryButton");
    auto *composerLayout = new QHBoxLayout(composer);
    composerLayout->setContentsMargins(14, 10, 14, 12);
    composerLayout->setSpacing(8);
    composerLayout->addWidget(attachButton);
    composerLayout->addWidget(m_messageInput);
    composerLayout->addWidget(sendButton);

    m_typingLabel = new QLabel;
    m_typingLabel->setObjectName("typingLabel");
    m_typingLabel->setFixedHeight(20);
    m_typingLabel->setText(QString());

    auto *mainColumn = new QVBoxLayout;
    mainColumn->setContentsMargins(0, 0, 0, 0);
    mainColumn->setSpacing(0);
    mainColumn->addWidget(header);
    mainColumn->addWidget(m_firewallBanner);
    mainColumn->addWidget(m_messageScroll, 1);
    mainColumn->addWidget(m_typingLabel);
    mainColumn->addWidget(composer);

    auto *layout = new QHBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(sidebar);
    layout->addLayout(mainColumn, 1);

    connect(m_channelList, &QListWidget::currentItemChanged, this,
            [this](QListWidgetItem *item, QListWidgetItem *) {
                if (item)
                    switchConversation(item->data(Qt::UserRole).toString());
            });
    connect(m_dmList, &QListWidget::currentItemChanged, this,
            [this](QListWidgetItem *item, QListWidgetItem *) {
                if (item)
                    switchConversation(item->data(Qt::UserRole).toString());
            });
    connect(addChannelButton, &QPushButton::clicked, this, &MainWindow::promptAddChannel);
    connect(m_messageInput, &QLineEdit::textEdited, this, &MainWindow::onComposerEdited);
    connect(m_messageInput, &QLineEdit::returnPressed, this, &MainWindow::sendCurrentMessage);
    connect(sendButton, &QPushButton::clicked, this, &MainWindow::sendCurrentMessage);

    return page;
}

void MainWindow::updateHomeStats()
{
    // The quest board is gone; this now just persists accumulated uptime. The
    // per-node stats live inline in the repositories panel (see selfNodeStats).
    if (m_connectedAtMs > 0) {
        const qint64 now = QDateTime::currentMSecsSinceEpoch();
        const qint64 totalMs = m_totalConnectionMs + (now - m_connectedAtMs);
        QSettings().setValue(kConnectionTotalSetting, totalMs);
    }
}

QString MainWindow::selfNodeStats() const
{
    int mirrored = 0;
    int online = 0;
    for (const RepositoryRecord &repo : m_repositories) {
        if (repo.previewOnly)
            continue;
        if (repo.lastSyncMs > 0 ||
            (!repo.mirrorPath.isEmpty() && QDir(repo.mirrorPath).exists()))
            ++mirrored;
        if (repo.publishedAtMs > 0 || repo.publishToNetwork)
            ++online;
    }
    const qint64 sessionMs =
        m_connectedAtMs > 0 ? QDateTime::currentMSecsSinceEpoch() - m_connectedAtMs : 0;
    const qint64 totalMs = m_totalConnectionMs + sessionMs;
    const QString key = m_profileIdentity.shortPublicKey();
    const int permanentRepoCount =
        int(std::count_if(m_repositories.cbegin(), m_repositories.cend(),
                          [](const RepositoryRecord &repo) {
                              return !repo.previewOnly;
                          }));
    return QStringLiteral(
               "%1 repos \xC2\xB7 %2 mirrored \xC2\xB7 %3 online \xC2\xB7 %4 chats")
               .arg(permanentRepoCount)
               .arg(mirrored)
               .arg(online)
               .arg(m_channels.size()) +
           "\nuptime " + formatDuration(sessionMs) + " \xC2\xB7 total " +
           formatDuration(totalMs) +
           (key.isEmpty() ? QString() : " \xC2\xB7 key " + key);
}

// ----------------------------------------------------------------- settings

QWidget *MainWindow::buildSettingsSection()
{
    auto *page = new QWidget;

    auto *title = new QLabel("Settings");
    title->setObjectName("settingsTitle");

    auto *profileLabel = new QLabel("PROFILE");
    profileLabel->setObjectName("sectionLabel");

    m_settingsNameEdit = new QLineEdit;
    m_settingsNameEdit->setMaxLength(32);
    m_settingsNameEdit->setPlaceholderText("Name");
    connect(m_settingsNameEdit, &QLineEdit::editingFinished, this,
            [this] { onProfileNameChanged(m_settingsNameEdit->text()); });

    m_settingsAvatarPreview = new QLabel("No\navatar");
    m_settingsAvatarPreview->setObjectName("avatarPreview");
    m_settingsAvatarPreview->setFixedSize(64, 64);
    m_settingsAvatarPreview->setAlignment(Qt::AlignCenter);
    auto *uploadButton = new QPushButton("Upload…");
    uploadButton->setObjectName("ghostButton");
    uploadButton->setCursor(Qt::PointingHandCursor);
    connect(uploadButton, &QPushButton::clicked, this, &MainWindow::chooseAvatar);
    auto *generateButton = new QPushButton("Generate");
    generateButton->setObjectName("ghostButton");
    generateButton->setCursor(Qt::PointingHandCursor);
    generateButton->setToolTip("Generate a fresh ForkMesh mesh-identicon avatar");
    connect(generateButton, &QPushButton::clicked, this, [this] {
        const QByteArray png = forkMeshAvatarPng(
            QString::number(QRandomGenerator::global()->generate64()));
        setSettingsAvatar(png);
        onAvatarChosen(png);
    });
    auto *avatarRow = new QHBoxLayout;
    avatarRow->setSpacing(12);
    avatarRow->addWidget(m_settingsAvatarPreview);
    avatarRow->addWidget(uploadButton);
    avatarRow->addWidget(generateButton);
    avatarRow->addStretch();

    // #66: let the node's Solana donation/payout address be set right here in
    // Settings, not only during setup or from the profile panel.
    m_settingsSolanaEdit = new QLineEdit;
    m_settingsSolanaEdit->setMaxLength(64);
    m_settingsSolanaEdit->setPlaceholderText(
        "Solana address (for donations / payouts, optional)");
    m_settingsSolanaEdit->setText(savedSolanaAddress());
    connect(m_settingsSolanaEdit, &QLineEdit::editingFinished, this, [this] {
        const QString addr = m_settingsSolanaEdit->text().trimmed();
        m_settingsSolanaEdit->setText(addr);
        saveSolanaAddress(addr);
        if (m_solanaEdit && m_solanaEdit->text().trimmed() != addr)
            m_solanaEdit->setText(addr);
        // Share with peers on the next connect; refresh the sponsor banner now.
        updateSolanaNotice();
        updateHomeStats();
    });

    auto *form = new QFormLayout;
    form->setLabelAlignment(Qt::AlignLeft);
    form->setSpacing(8);
    form->addRow("Name", m_settingsNameEdit);
    form->addRow("Solana", m_settingsSolanaEdit);
    form->addRow("Avatar", avatarRow);

    auto *startupLabel = new QLabel("STARTUP");
    startupLabel->setObjectName("sectionLabel");
    m_autostartCheck = new QCheckBox("Launch ForkMesh at login");
    m_autostartCheck->setChecked(isAutostartEnabled());
    m_autostartCheck->setToolTip(
        "Start ForkMesh automatically when you log in to this computer.");
    connect(m_autostartCheck, &QCheckBox::toggled, this, [](bool enabled) {
        setAutostartEnabled(enabled);
    });

    auto *notifyLabel = new QLabel("NOTIFICATIONS");
    notifyLabel->setObjectName("sectionLabel");
    auto *pushAlertCheck =
        new QCheckBox("Show a system alert when a push reaches a mirror");
    pushAlertCheck->setChecked(
        QSettings().value(kPushAlertSetting, true).toBool());
    pushAlertCheck->setToolTip(
        "Pop up a desktop notification with the repo, branch and commit "
        "whenever someone pushes to one of this node's mirrors.");
    connect(pushAlertCheck, &QCheckBox::toggled, this, [](bool enabled) {
        QSettings().setValue(kPushAlertSetting, enabled);
    });
    auto *actionAlertCheck =
        new QCheckBox("Show a system alert when an action runs");
    actionAlertCheck->setChecked(
        QSettings().value(kActionAlertSetting, true).toBool());
    actionAlertCheck->setToolTip(
        "Pop up a desktop notification when a .forkmesh/ workflow starts and "
        "when it finishes.");
    connect(actionAlertCheck, &QCheckBox::toggled, this, [](bool enabled) {
        QSettings().setValue(kActionAlertSetting, enabled);
    });
    auto *nodeConnectAlertCheck =
        new QCheckBox("Show a system alert when a node connects");
    nodeConnectAlertCheck->setChecked(
        QSettings().value(kNodeConnectAlertSetting, true).toBool());
    nodeConnectAlertCheck->setToolTip(
        "Pop up a desktop notification when another node comes online on this "
        "network.");
    connect(nodeConnectAlertCheck, &QCheckBox::toggled, this, [](bool enabled) {
        QSettings().setValue(kNodeConnectAlertSetting, enabled);
    });
    auto *disbursementAlertCheck =
        new QCheckBox("Show a system alert when this node receives a disbursement");
    disbursementAlertCheck->setChecked(
        QSettings().value(kDisbursementAlertSetting, true).toBool());
    disbursementAlertCheck->setToolTip(
        "Pop up a desktop notification when this node's Solana wallet balance "
        "increases after a refresh.");
    connect(disbursementAlertCheck, &QCheckBox::toggled, this, [](bool enabled) {
        QSettings().setValue(kDisbursementAlertSetting, enabled);
    });

    auto *appearanceLabel = new QLabel("APPEARANCE");
    appearanceLabel->setObjectName("sectionLabel");
    m_themeCombo = new QComboBox;
    m_themeCombo->addItem("Follow system", "system");
    m_themeCombo->addItem("Dark", "dark");
    m_themeCombo->addItem("Light", "light");
    m_themeCombo->setToolTip("Choose the color theme, or follow the OS setting.");
    {
        const QString pref = QSettings().value(kThemeSetting, "system").toString();
        const int idx = m_themeCombo->findData(pref);
        m_themeCombo->setCurrentIndex(idx < 0 ? 0 : idx);
    }
    connect(m_themeCombo, &QComboBox::currentIndexChanged, this, [this](int) {
        QSettings().setValue(kThemeSetting, m_themeCombo->currentData().toString());
        applyTheme();
    });

    auto *agentsLabel = new QLabel("AGENTS");
    agentsLabel->setObjectName("sectionLabel");
    auto *agentsHint = new QLabel(
        "Stored locally. Command templates run in a temporary worktree with "
        "{promptFile}, {modelArg}, {contextWindow}, and {maxOutputTokens} available.");
    agentsHint->setObjectName("statusLine");
    agentsHint->setWordWrap(true);

    m_codexApiKeyEdit = new QLineEdit;
    m_codexApiKeyEdit->setEchoMode(QLineEdit::Password);
    m_codexApiKeyEdit->setPlaceholderText("OPENAI_API_KEY");
    m_codexApiKeyEdit->setText(QSettings().value(kCodexApiKeySetting).toString().trimmed());
    connect(m_codexApiKeyEdit, &QLineEdit::editingFinished, this, [this] {
        const QString key = m_codexApiKeyEdit->text().trimmed();
        m_codexApiKeyEdit->setText(key);
        QSettings().setValue(kCodexApiKeySetting, key);
    });

    m_openAiAdminKeyEdit = new QLineEdit;
    m_openAiAdminKeyEdit->setEchoMode(QLineEdit::Password);
    m_openAiAdminKeyEdit->setPlaceholderText("Optional Admin API key for usage and costs");
    m_openAiAdminKeyEdit->setText(
        QSettings().value(kOpenAiAdminKeySetting).toString().trimmed());
    connect(m_openAiAdminKeyEdit, &QLineEdit::editingFinished, this, [this] {
        const QString key = m_openAiAdminKeyEdit->text().trimmed();
        m_openAiAdminKeyEdit->setText(key);
        QSettings().setValue(kOpenAiAdminKeySetting, key);
    });

    m_codexModelEdit = new QLineEdit;
    m_codexModelEdit->setPlaceholderText("Optional, e.g. gpt-5.1-codex");
    m_codexModelEdit->setText(QSettings().value(kCodexModelSetting).toString().trimmed());
    connect(m_codexModelEdit, &QLineEdit::editingFinished, this, [this] {
        const QString model = m_codexModelEdit->text().trimmed();
        m_codexModelEdit->setText(model);
        QSettings().setValue(kCodexModelSetting, model);
    });

    m_claudeApiKeyEdit = new QLineEdit;
    m_claudeApiKeyEdit->setEchoMode(QLineEdit::Password);
    m_claudeApiKeyEdit->setPlaceholderText("ANTHROPIC_API_KEY");
    m_claudeApiKeyEdit->setText(QSettings().value(kClaudeApiKeySetting).toString().trimmed());
    connect(m_claudeApiKeyEdit, &QLineEdit::editingFinished, this, [this] {
        const QString key = m_claudeApiKeyEdit->text().trimmed();
        m_claudeApiKeyEdit->setText(key);
        QSettings().setValue(kClaudeApiKeySetting, key);
    });

    m_codexCommandEdit = new QLineEdit;
    m_codexCommandEdit->setText(codexCommandSetting());
    connect(m_codexCommandEdit, &QLineEdit::editingFinished, this, [this] {
        QSettings().setValue(kCodexCommandSetting, m_codexCommandEdit->text());
    });

    m_claudeCommandEdit = new QLineEdit;
    m_claudeCommandEdit->setText(claudeCommandSetting());
    connect(m_claudeCommandEdit, &QLineEdit::editingFinished, this, [this] {
        QSettings().setValue(kClaudeCommandSetting, m_claudeCommandEdit->text());
    });

    m_agentContextEdit = new QLineEdit;
    m_agentContextEdit->setPlaceholderText("32000");
    m_agentContextEdit->setText(
        QSettings().value(kAgentContextSetting, 32000).toString());
    connect(m_agentContextEdit, &QLineEdit::editingFinished, this, [this] {
        QSettings().setValue(kAgentContextSetting,
                             qMax(1000, m_agentContextEdit->text().toInt()));
    });

    m_agentMaxOutputEdit = new QLineEdit;
    m_agentMaxOutputEdit->setPlaceholderText("2000");
    m_agentMaxOutputEdit->setText(
        QSettings().value(kAgentMaxOutputSetting, 2000).toString());
    connect(m_agentMaxOutputEdit, &QLineEdit::editingFinished, this, [this] {
        QSettings().setValue(kAgentMaxOutputSetting,
                             qMax(256, m_agentMaxOutputEdit->text().toInt()));
    });

    auto *agentForm = new QFormLayout;
    agentForm->setLabelAlignment(Qt::AlignLeft);
    agentForm->setSpacing(8);
    agentForm->addRow("OpenAI API key", m_codexApiKeyEdit);
    agentForm->addRow("OpenAI Admin key", m_openAiAdminKeyEdit);
    agentForm->addRow("Codex model", m_codexModelEdit);
    agentForm->addRow("Claude API key", m_claudeApiKeyEdit);
    agentForm->addRow("Codex command", m_codexCommandEdit);
    agentForm->addRow("Claude command", m_claudeCommandEdit);
    agentForm->addRow("Context window", m_agentContextEdit);
    agentForm->addRow("Max output", m_agentMaxOutputEdit);

    // Mirror storage location: where bare mirrors of repos are kept. Mirrors act
    // as the local "remote" a fork pushes to (see issue: fork from the client).
    auto *storageLabel = new QLabel("MIRROR STORAGE");
    storageLabel->setObjectName("sectionLabel");
    m_mirrorRootEdit = new QLineEdit(repositoryMirrorRoot());
    m_mirrorRootEdit->setReadOnly(true);
    m_mirrorRootEdit->setToolTip(
        "Folder where mirrored repositories are stored. New mirrors are created "
        "here; a local fork pushes into its mirror.");
    auto *mirrorChangeButton = new QPushButton("Change\xE2\x80\xA6");
    mirrorChangeButton->setObjectName("ghostButton");
    mirrorChangeButton->setCursor(Qt::PointingHandCursor);
    connect(mirrorChangeButton, &QPushButton::clicked, this,
            &MainWindow::changeMirrorLocation);
    auto *mirrorRow = new QHBoxLayout;
    mirrorRow->setContentsMargins(0, 0, 0, 0);
    mirrorRow->addWidget(m_mirrorRootEdit, 1);
    mirrorRow->addWidget(mirrorChangeButton);

    auto *previewCacheLabel = new QLabel("PREVIEW CACHE");
    previewCacheLabel->setObjectName("sectionLabel");
    m_previewCacheRootEdit = new QLineEdit(repositoryPreviewRoot());
    m_previewCacheRootEdit->setReadOnly(true);
    m_previewCacheRootEdit->setToolTip(
        "Folder where temporary browse-only mirrors are stored before you "
        "choose to mirror or fork a repository.");
    auto *previewCacheChangeButton = new QPushButton("Change\xE2\x80\xA6");
    previewCacheChangeButton->setObjectName("ghostButton");
    previewCacheChangeButton->setCursor(Qt::PointingHandCursor);
    connect(previewCacheChangeButton, &QPushButton::clicked, this,
            &MainWindow::changePreviewCacheLocation);
    auto *previewCacheRow = new QHBoxLayout;
    previewCacheRow->setContentsMargins(0, 0, 0, 0);
    previewCacheRow->addWidget(m_previewCacheRootEdit, 1);
    previewCacheRow->addWidget(previewCacheChangeButton);

    // Variables / secrets shared by all action workflows on this node. Values
    // are injected into each run's environment (e.g. CLOUDFLARE_API_TOKEN) and
    // redacted from run logs.
    auto *varsLabel = new QLabel("VARIABLES / SECRETS");
    varsLabel->setObjectName("sectionLabel");
    auto *varsHint = new QLabel(
        "Injected into every action run's environment and redacted from logs. "
        "Add CLOUDFLARE_API_TOKEN here to let the deploy workflow authenticate.");
    varsHint->setObjectName("statusLine");
    varsHint->setWordWrap(true);

    m_varsTable = new QTableWidget(0, 2);
    m_varsTable->setHorizontalHeaderLabels({"Name", "Value"});
    m_varsTable->horizontalHeader()->setStretchLastSection(true);
    m_varsTable->verticalHeader()->setVisible(false);
    m_varsTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_varsTable->setSelectionMode(QAbstractItemView::SingleSelection);
    m_varsTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_varsTable->setMaximumHeight(160);

    auto *varAddButton = new QPushButton("Add\xE2\x80\xA6");
    auto *varEditButton = new QPushButton("Edit\xE2\x80\xA6");
    auto *varDeleteButton = new QPushButton("Delete");
    for (QPushButton *b : {varAddButton, varEditButton, varDeleteButton}) {
        b->setObjectName("ghostButton");
        b->setCursor(Qt::PointingHandCursor);
    }
    connect(varAddButton, &QPushButton::clicked, this,
            &MainWindow::addOrEditVariable);
    connect(varEditButton, &QPushButton::clicked, this,
            &MainWindow::addOrEditVariable);
    connect(varDeleteButton, &QPushButton::clicked, this,
            &MainWindow::deleteSelectedVariable);
    auto *varButtonRow = new QHBoxLayout;
    varButtonRow->setContentsMargins(0, 0, 0, 0);
    varButtonRow->addWidget(varAddButton);
    varButtonRow->addWidget(varEditButton);
    varButtonRow->addWidget(varDeleteButton);
    varButtonRow->addStretch();

    auto *leaveButton = new QPushButton("Leave node");
    leaveButton->setObjectName("dangerButton");
    leaveButton->setCursor(Qt::PointingHandCursor);
    setOcticon(leaveButton, "sign-out", 16);
    connect(leaveButton, &QPushButton::clicked, this, [this] { leaveSession(); });

    // Rebuild & restart now lives here, next to Leave node, rather than in the
    // server rail.
    m_rebuildButton = new QPushButton("Rebuild & restart");
    m_rebuildButton->setObjectName("ghostButton");
    m_rebuildButton->setCursor(Qt::PointingHandCursor);
    m_rebuildButton->setToolTip("Pull the latest version, rebuild, and relaunch");
    setOcticon(m_rebuildButton, "sync", 16);
    connect(m_rebuildButton, &QPushButton::clicked, this,
            [this] { quickRebuildRestart(); });

    // Log out clears the signed-in account so you can log back in (as the same
    // or a different account).
    auto *logoutButton = new QPushButton("Log out");
    logoutButton->setObjectName("ghostButton");
    logoutButton->setCursor(Qt::PointingHandCursor);
    setOcticon(logoutButton, "sign-out", 16);
    connect(logoutButton, &QPushButton::clicked, this, [this] { logout(); });

    m_rebuildStatus = new QLabel;
    m_rebuildStatus->setObjectName("modeHint");
    m_rebuildStatus->setWordWrap(true);
    m_rebuildStatus->hide();

    auto *footerRow = new QHBoxLayout;
    footerRow->setContentsMargins(0, 0, 0, 0);
    footerRow->addWidget(leaveButton);
    footerRow->addWidget(m_rebuildButton);
    footerRow->addWidget(logoutButton);
    footerRow->addStretch();

    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(24, 22, 24, 22);
    layout->setSpacing(10);
    layout->addWidget(title);
    layout->addWidget(profileLabel);
    layout->addLayout(form);
    layout->addSpacing(6);
    layout->addWidget(storageLabel);
    layout->addLayout(mirrorRow);
    layout->addSpacing(6);
    layout->addWidget(previewCacheLabel);
    layout->addLayout(previewCacheRow);
    layout->addSpacing(6);
    layout->addWidget(startupLabel);
    layout->addWidget(m_autostartCheck);
    layout->addSpacing(6);
    layout->addWidget(notifyLabel);
    layout->addWidget(pushAlertCheck);
    layout->addWidget(actionAlertCheck);
    layout->addWidget(nodeConnectAlertCheck);
    layout->addWidget(disbursementAlertCheck);
    layout->addSpacing(6);
    layout->addWidget(appearanceLabel);
    layout->addWidget(m_themeCombo, 0, Qt::AlignLeft);
    layout->addSpacing(6);
    layout->addWidget(agentsLabel);
    layout->addWidget(agentsHint);
    layout->addLayout(agentForm);
    layout->addSpacing(6);
    layout->addWidget(varsLabel);
    layout->addWidget(varsHint);
    layout->addWidget(m_varsTable);
    layout->addLayout(varButtonRow);
    layout->addStretch();
    layout->addWidget(m_rebuildStatus);
    layout->addLayout(footerRow);
    reloadVariablesTable();
    setSettingsAvatar(QByteArray()); // show the current/generated avatar
    return page;
}

QByteArray MainWindow::effectiveAvatar()
{
    if (!m_userAvatar.isEmpty())
        return m_userAvatar;
    QString seed = !m_accountName.isEmpty()
                       ? m_accountName
                       : (!m_userName.isEmpty() ? m_userName
                                                : m_profileIdentity.publicKey());
    if (seed.isEmpty())
        seed = QStringLiteral("forkmesh");
    return forkMeshAvatarPng(seed);
}

void MainWindow::updateAvatarButton()
{
    if (!m_avatarNavButton)
        return;
    const QPixmap pm = roundedAvatar(effectiveAvatar(), 34);
    if (!pm.isNull())
        m_avatarNavButton->setIcon(QIcon(pm));
    refreshIssueComposerAvatar();
}

void MainWindow::refreshIssueComposerAvatar()
{
    if (!m_issueComposerAvatar)
        return;
    // Show this node's avatar next to the comment composer so it's clear who is
    // about to post.
    const QPixmap pm = roundedAvatar(effectiveAvatar(), 36);
    if (pm.isNull()) {
        m_issueComposerAvatar->setPixmap(QPixmap());
        m_issueComposerAvatar->setText("FM");
    } else {
        m_issueComposerAvatar->setText(QString());
        m_issueComposerAvatar->setPixmap(pm);
    }
}

void MainWindow::setSettingsAvatar(const QByteArray &pngData)
{
    if (!m_settingsAvatarPreview)
        return;
    // Fall back to the deterministic generated avatar when none is set.
    const QByteArray data = pngData.isEmpty() ? effectiveAvatar() : pngData;
    QPixmap pixmap;
    if (!pixmap.loadFromData(data))
        return;
    constexpr int side = 64;
    QPixmap rounded(side, side);
    rounded.fill(Qt::transparent);
    QPainter painter(&rounded);
    painter.setRenderHint(QPainter::Antialiasing);
    QPainterPath clip;
    clip.addRoundedRect(0, 0, side, side, 14, 14);
    painter.setClipPath(clip);
    painter.drawPixmap(0, 0,
                       pixmap.scaled(side, side, Qt::KeepAspectRatioByExpanding,
                                     Qt::SmoothTransformation));
    m_settingsAvatarPreview->setPixmap(rounded);
}

void MainWindow::chooseAvatar()
{
    const QString path = QFileDialog::getOpenFileName(
        this, "Choose avatar image", QString(),
        "Images (*.png *.jpg *.jpeg *.webp *.bmp *.gif)");
    if (path.isEmpty())
        return;
    QImage image(path);
    if (image.isNull())
        return;
    // Center-crop to a square, scale down, and re-encode as a small PNG.
    const int squareSide = qMin(image.width(), image.height());
    image = image.copy((image.width() - squareSide) / 2,
                       (image.height() - squareSide) / 2, squareSide, squareSide)
                .scaled(128, 128, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    QByteArray png;
    QBuffer buffer(&png);
    buffer.open(QIODevice::WriteOnly);
    image.save(&buffer, "PNG");
    setSettingsAvatar(png);
    onAvatarChosen(png);
}

void MainWindow::quickRebuildRestart()
{
    // Incremental rebuild + relaunch (no cache wipe) for fast iteration. Reuses
    // the Settings rebuild button/status as the progress target.
    m_buildButton = m_rebuildButton;
    m_buildStatusLabel = m_rebuildStatus;
    const QString clientDir = updateClientDir();
    if (!QDir(clientDir).exists("CMakeLists.txt")) {
        stopRefreshSpin();
        QMessageBox::information(
            this, "Rebuild & restart",
            "No local source checkout to rebuild from. Use Quick update on the "
            "start screen instead.");
        return;
    }
    if (m_rebuildButton)
        m_rebuildButton->setEnabled(false);
    buildAndRelaunch(clientDir);
}

void MainWindow::rebuildAndRelaunch()
{
    if (m_settingsNameEdit)
        saveProfileName(m_settingsNameEdit->text());
    m_buildButton = m_rebuildButton;
    m_buildStatusLabel = m_rebuildStatus;
    m_rebuildButton->setEnabled(false);

    const QString clientDir = updateClientDir();
    if (!QDir(clientDir).exists("CMakeLists.txt")) {
        setUpdateStatus("No local source checkout to rebuild from. Use Quick "
                        "update on the start screen instead.",
                        true);
        m_rebuildButton->setEnabled(true);
        return;
    }
    // Clear the build cache for a clean from-scratch rebuild, then relaunch.
    setUpdateStatus("Clearing build cache...");
    QDir(clientDir + "/build").removeRecursively();
    buildAndRelaunch(clientDir);
}

void MainWindow::attachBackend(ChatBackend *backend)
{
    if (m_backend)
        leaveSession();
    m_backend = backend;

    connect(backend, &ChatBackend::messageArrived, this, &MainWindow::onMessage);
    connect(backend, &ChatBackend::reactionChanged, this, &MainWindow::onReaction);
    connect(backend, &ChatBackend::messageEdited, this, &MainWindow::onMessageEdited);
    connect(backend, &ChatBackend::messageDeleted, this, &MainWindow::onMessageDeleted);
    connect(backend, &ChatBackend::avatarChanged, this, &MainWindow::onAvatar);
    connect(backend, &ChatBackend::typingChanged, this, &MainWindow::onTypingChanged);
    connect(backend, &ChatBackend::systemMessage, this, &MainWindow::logSystem);
    connect(backend, &ChatBackend::channelsChanged, this, &MainWindow::setChannels);
    connect(backend, &ChatBackend::rosterChanged, this, &MainWindow::setRoster);
    connect(backend, &ChatBackend::mirrorUpdated, this, &MainWindow::onPeerMirrorUpdated);
    connect(backend, &ChatBackend::statusChanged, this, [this](const QString &status) {
        const QString summary = status.section(" · ", 0, 0);
        m_statusLine->setText(summary);
        logSystem("Status: " + status);
        updateConnectionStatus();
    });
    connect(backend, &ChatBackend::firewallBlocking, this, &MainWindow::showFirewallBanner);
    connect(backend, &ChatBackend::firewallHealthy, this, &MainWindow::hideFirewallBanner);
    connect(backend, &ChatBackend::fatalError, this, [this](const QString &message) {
        leaveSession();
        m_setupError->setText(message);
        m_setupError->show();
    });
}

void MainWindow::leaveSession(const QString &)
{
    if (m_connectedAtMs > 0) {
        m_totalConnectionMs += QDateTime::currentMSecsSinceEpoch() - m_connectedAtMs;
        m_connectedAtMs = 0;
        QSettings().setValue(kConnectionTotalSetting, m_totalConnectionMs);
    }
    stopRepoHosts();
    if (m_backend) {
        m_backend->disconnect(this);
        m_backend->disconnect(m_statusLine);
        m_backend->shutdown();
        m_backend->deleteLater();
        m_backend = nullptr;
    }
    updateConnectionStatus();
    m_stack->setCurrentIndex(0);
    m_userName.clear();

    m_homeRoster.clear();
    refreshRepositoryList(); // clears node online status from the repos panel
    updateHomeStats();
}

// ------------------------------------------------------------------ firewall

void MainWindow::showFirewallBanner(const QString &displayCommand,
                                    const QString &privilegedCommand)
{
    m_firewallPrivilegedCommand = privilegedCommand;
    m_firewallBannerLabel->setText(
        "A firewall on this computer may be blocking peers from "
        "connecting. Click to open ForkMesh's ports (asks for your password).");
    m_firewallBannerLabel->setToolTip(displayCommand);
    m_firewallAllowButton->setEnabled(true);
    m_firewallAllowButton->setText("Allow through firewall");
    m_firewallBanner->show();
}

void MainWindow::hideFirewallBanner()
{
    if (m_firewallBanner)
        m_firewallBanner->hide();
    if (m_firewallAllowButton)
        m_firewallAllowButton->show();
}

void MainWindow::allowFirewall()
{
    if (m_firewallPrivilegedCommand.isEmpty())
        return;
    m_firewallAllowButton->setEnabled(false);
    m_firewallAllowButton->setText("Allowing…");

    auto *process = new QProcess(this);
    connect(process, &QProcess::finished, this,
            [this, process](int exitCode, QProcess::ExitStatus) {
                const QString errors =
                    QString::fromUtf8(process->readAllStandardError()).trimmed();
                process->deleteLater();
                if (exitCode == 0) {
                    // Ports are open immediately; existing sockets start
                    // receiving, so discovery recovers within a few seconds.
                    m_firewallBannerLabel->setText(
                        "Firewall opened. Peers should connect "
                        "within a few seconds.");
                    m_firewallAllowButton->hide();
                    logSystem("Firewall opened for ForkMesh's ports.");
                } else {
                    m_firewallBannerLabel->setText(
                        "Could not open the firewall" +
                        (errors.isEmpty() ? QString() : ": " + errors.right(200)) +
                        ". Run the command shown in the chat manually.");
                    m_firewallAllowButton->setEnabled(true);
                    m_firewallAllowButton->setText("Try again");
                }
            });
    connect(process, &QProcess::errorOccurred, this, [this, process] {
        process->deleteLater();
        m_firewallBannerLabel->setText(
            "Could not launch the privilege helper (pkexec). Run the command "
            "shown in the chat manually in a terminal.");
        m_firewallAllowButton->setEnabled(true);
        m_firewallAllowButton->setText("Try again");
    });
    // pkexec shows a graphical password prompt and runs the command as root.
    process->start("pkexec", {"sh", "-c", m_firewallPrivilegedCommand});
}

// -------------------------------------------------------------- diagnostics

void MainWindow::logSystem(const QString &text)
{
    const QString time = QDateTime::currentDateTime().toString("hh:mm:ss");
    const QString line = time + "  " + text;
    m_networkLog.append(line);
    while (m_networkLog.size() > kNetworkLogLimit)
        m_networkLog.removeFirst();
    if (m_settingsLog)
        m_settingsLog->appendPlainText(line);
}

void MainWindow::flashMessage(const QString &text, bool error)
{
    // Always keep a copy in the network log for history.
    logSystem(text);
    if (!m_topMessage)
        return;

    const QString trimmed = text.simplified();
    if (trimmed.isEmpty()) {
        m_topMessage->hide();
        return;
    }
    // Green for success, red for failure; compact pill in the centre of the bar.
    const QString fg = error ? "#f85149" : "#3fb950";
    const QString glyph = error ? QString::fromUtf8("\xE2\x9C\x95")  // ✕
                                : QString::fromUtf8("\xE2\x9C\x93"); // ✓
    m_topMessage->setText(
        QStringLiteral("<span style='color:%1'>%2 %3</span>")
            .arg(fg, glyph, trimmed.toHtmlEscaped()));
    m_topMessage->show();

    if (!m_topMessageTimer) {
        m_topMessageTimer = new QTimer(this);
        m_topMessageTimer->setSingleShot(true);
        connect(m_topMessageTimer, &QTimer::timeout, this, [this] {
            if (m_topMessage)
                m_topMessage->hide();
        });
    }
    m_topMessageTimer->start(error ? 6000 : 4000);
}

void MainWindow::notifyIfInactive(const QString &title, const QString &body)
{
    if (isActiveWindow())
        return;

    QApplication::alert(this, 0);
    QString cleanBody = body.simplified();
    if (cleanBody.size() > 180)
        cleanBody = cleanBody.left(177) + "...";
    postNotification(title, cleanBody);
}

// ------------------------------------------------------------------ messages

QString MainWindow::senderColor(const QString &sender) const
{
    const uint hash = qHash(sender);
    return Theme::kSenderPalette[hash % Theme::kSenderPaletteSize];
}

MessageRow *MainWindow::addMessageRow(const ChatMessage &message)
{
    auto *row = new MessageRow(message, senderColor(message.senderName));
    if (m_avatars.contains(message.senderId))
        row->setAvatar(m_avatars.value(message.senderId));
    if (m_reactions.contains(message.id))
        row->setReactions(m_reactions.value(message.id));
    connect(row, &MessageRow::reactionToggled, this,
            [this](const QString &messageId, const QString &emoji) {
                if (m_backend)
                    m_backend->sendReaction(m_currentConversation, messageId, emoji);
            });
    connect(row, &MessageRow::editRequested, this, &MainWindow::promptEditMessage);
    connect(row, &MessageRow::deleteRequested, this, &MainWindow::confirmDeleteMessage);
    connect(row, &MessageRow::saveFileRequested, this,
            &MainWindow::saveIncomingFile);
    connect(row, &MessageRow::senderClicked, this, &MainWindow::showNodeProfile);
    // Insert before the trailing stretch.
    m_messageLayout->insertWidget(m_messageLayout->count() - 1, row);
    m_visibleRows.insert(message.id, row);
    return row;
}

void MainWindow::rebuildConversationView()
{
    // Drop existing rows (keep the trailing stretch at the end).
    m_visibleRows.clear();
    while (m_messageLayout->count() > 1) {
        QLayoutItem *item = m_messageLayout->takeAt(0);
        if (item->widget())
            item->widget()->deleteLater();
        delete item;
    }
    for (const ChatMessage &message : m_history.value(m_currentConversation))
        addMessageRow(message);
    // Switching into a conversation always lands at the newest message.
    scrollToBottom();
}

void MainWindow::scrollToBottom()
{
    m_stickToBottom = true;
    // The rangeChanged handler scrolls once the new rows expand the range, but
    // when the range is unchanged (e.g. it already fit) no signal fires, so
    // pin to the current maximum after layout settles too.
    QTimer::singleShot(0, this, [this] {
        QScrollBar *bar = m_messageScroll->verticalScrollBar();
        bar->setValue(bar->maximum());
    });
}

void MainWindow::onMessage(const ChatMessage &message)
{
    const QString conversation = message.conversation;
    if (conversation.isEmpty())
        return;

    // Skip messages we already have (e.g. loaded from disk then replayed by a
    // peer on reconnect) so history isn't duplicated.
    if (!message.id.isEmpty()) {
        if (m_historyIds.contains(message.id))
            return;
        m_historyIds.insert(message.id);
    }

    // Open a DM tab on first contact. For an incoming DM the author *is* the
    // other party (senderId == peerId), so that names the conversation; our
    // own echoed messages (senderId != peerId) must not rename it.
    if (isDirectConversation(conversation)) {
        const QString peerId = dmPeerId(conversation);
        if (message.senderId == peerId && !message.senderName.isEmpty())
            m_dmNames.insert(peerId, message.senderName);
        if (!m_openDms.contains(peerId)) {
            m_openDms.append(peerId);
            refreshDmList();
        }
    }

    m_history[conversation].append(message);

    if (conversation == m_currentConversation) {
        const bool wasAtBottom = m_stickToBottom;
        addMessageRow(message);
        // Follow new arrivals only when already reading the latest; the
        // rangeChanged handler does the actual scrolling once the row lays out.
        if (wasAtBottom)
            scrollToBottom();
    } else {
        m_unread.insert(conversation);
        refreshChannelList();
        refreshDmList();
    }

    if (!message.self) {
        const QString where = isDirectConversation(conversation)
                                  ? "sent you a message"
                                  : "in " + conversation;
        const QString preview =
            message.hasFile() ? "File: " + message.fileName : message.text;
        if (textMentionsNodeName(message.text, m_userName)) {
            QApplication::alert(this, 0);
            QString cleanPreview = preview.simplified();
            if (cleanPreview.size() > 180)
                cleanPreview = cleanPreview.left(177) + "...";
            postNotification(message.senderName + " mentioned you", cleanPreview);
        } else {
            notifyIfInactive(message.senderName + " " + where, preview);
        }
    }
    scheduleChatSave();
}

void MainWindow::onReaction(const QString &conversation, const QString &messageId,
                            const QString &emoji, const QString &reactorName,
                            bool added)
{
    Q_UNUSED(conversation);
    QStringList &reactors = m_reactions[messageId][emoji];
    if (added) {
        if (!reactors.contains(reactorName))
            reactors.append(reactorName);
    } else {
        reactors.removeAll(reactorName);
        if (reactors.isEmpty())
            m_reactions[messageId].remove(emoji);
    }
    if (auto *row = m_visibleRows.value(messageId))
        row->setReactions(m_reactions.value(messageId));
}

void MainWindow::onMessageEdited(const QString &conversation, const QString &messageId,
                                 const QString &newText)
{
    QList<ChatMessage> &messages = m_history[conversation];
    for (ChatMessage &message : messages) {
        if (message.id == messageId) {
            message.text = newText.left(16000);
            message.edited = true;
            break;
        }
    }
    if (conversation == m_currentConversation)
        rebuildConversationView();
    scheduleChatSave();
}

void MainWindow::onMessageDeleted(const QString &conversation, const QString &messageId)
{
    QList<ChatMessage> &messages = m_history[conversation];
    for (ChatMessage &message : messages) {
        if (message.id == messageId) {
            message.text.clear();
            message.fileName.clear();
            message.fileMime.clear();
            message.fileData.clear();
            message.deleted = true;
            break;
        }
    }
    m_reactions.remove(messageId);
    if (conversation == m_currentConversation)
        rebuildConversationView();
    scheduleChatSave();
}

void MainWindow::promptEditMessage(const QString &messageId, const QString &currentText)
{
    if (!m_backend || messageId.isEmpty())
        return;
    bool ok = false;
    const QString text = QInputDialog::getMultiLineText(
        this, "Edit message", "Message:", currentText, &ok);
    const QString trimmed = text.trimmed();
    if (!ok || trimmed.isEmpty() || trimmed == currentText)
        return;
    m_backend->editMessage(m_currentConversation, messageId, trimmed);
}

void MainWindow::confirmDeleteMessage(const QString &messageId)
{
    if (!m_backend || messageId.isEmpty())
        return;
    const int result = QMessageBox::question(
        this, "Delete message", "Delete this message for everyone?");
    if (result == QMessageBox::Yes)
        m_backend->deleteMessage(m_currentConversation, messageId);
}

void MainWindow::onAvatar(const QString &peerId, const QByteArray &pngData)
{
    QPixmap pixmap;
    if (!pixmap.loadFromData(pngData))
        return;
    m_avatars.insert(peerId, pixmap);
    // Update any visible rows authored by this peer.
    for (auto it = m_visibleRows.constBegin(); it != m_visibleRows.constEnd(); ++it) {
        if (it.value()->senderId() == peerId)
            it.value()->setAvatar(pixmap);
    }
}

void MainWindow::onTypingChanged(const QString &conversation, const QString &peerId,
                                 const QString &peerName, bool active)
{
    if (conversation.isEmpty() || peerId.isEmpty())
        return;
    if (active)
        m_typing[conversation].insert(peerId, peerName);
    else if (m_typing.contains(conversation))
        m_typing[conversation].remove(peerId);
    refreshTypingLabel();
}

void MainWindow::setChannels(const QStringList &channels)
{
    m_channels = channels;
    if (m_currentConversation.startsWith('#') &&
        !m_channels.contains(m_currentConversation))
        m_currentConversation.clear();
    refreshChannelList();
    updateHomeStats();
    if (m_currentConversation.isEmpty() && !m_channels.isEmpty())
        m_channelList->setCurrentRow(0); // triggers switchConversation
}

void MainWindow::setRoster(const QList<MemberInfo> &members)
{
    QList<MemberInfo> visibleMembers;
    visibleMembers.reserve(members.size());
    for (const MemberInfo &member : members) {
        if (member.self || member.online)
            visibleMembers.append(member);
    }

    // #33: optionally pop a desktop notification when another node comes online.
    // Capture who was online before this update (m_homeRoster still holds the
    // previous roster), and skip the very first fill so we don't alert for every
    // node that was already online when we connected.
    const bool firstRoster = m_homeRoster.isEmpty();
    QSet<QString> previouslyOnline;
    for (const MemberInfo &m : std::as_const(m_homeRoster))
        if (m.online && !m.id.isEmpty())
            previouslyOnline.insert(m.id);
    if (!firstRoster &&
        QSettings().value(kNodeConnectAlertSetting, true).toBool()) {
        for (const MemberInfo &m : visibleMembers) {
            if (m.self || m.id.isEmpty() || !m.online)
                continue;
            if (!previouslyOnline.contains(m.id))
                postNotification(QStringLiteral("Node connected"),
                                 m.name + QStringLiteral(" is online"));
        }
    }

    m_homeRoster = visibleMembers;
    // The members list is gone (nodes are the members); keep DM tab titles in
    // sync with renamed/rediscovered nodes.
    for (const MemberInfo &member : visibleMembers) {
        if (m_dmNames.contains(member.id) && m_dmNames.value(member.id) != member.name) {
            m_dmNames.insert(member.id, member.name);
            refreshDmList();
            if (m_currentConversation == dmKey(member.id))
                m_channelTitle->setText(kDmPrefix + member.name);
        }
    }

    // Nodes live in the top-bar Node dropdown; refresh it (and the repo list)
    // to reflect live connection status.
    refreshRepositoryList();
    updateHomeStats();
    updateConnectionStatus();
}

void MainWindow::removeChatMember(const QString &id, const QString &name)
{
    if (id.isEmpty())
        return;

    if (m_backend)
        m_backend->forgetMember(id);

    // Drop any open direct chat with them and leave that conversation.
    const QString conversation = dmKey(id);
    m_openDms.removeAll(id);
    m_dmNames.remove(id);
    m_avatars.remove(id);
    m_unread.remove(conversation);
    const QList<ChatMessage> removedMessages = m_history.take(conversation);
    for (const ChatMessage &message : removedMessages) {
        m_historyIds.remove(message.id);
        m_reactions.remove(message.id);
    }
    m_typing.remove(conversation);
    for (const QString &key : m_typing.keys()) {
        m_typing[key].remove(id);
        if (m_typing.value(key).isEmpty())
            m_typing.remove(key);
    }
    if (m_typingConversation == conversation)
        sendTypingState(false);
    if (m_currentConversation == conversation) {
        if (m_channels.isEmpty()) {
            m_currentConversation.clear();
            m_channelTitle->setText(QStringLiteral("No conversation"));
            m_messageInput->setPlaceholderText(QStringLiteral("Message"));
            rebuildConversationView();
            refreshTypingLabel();
        } else {
            switchConversation(m_channels.first());
        }
    }

    // Remove the stale roster entry now; a fresh roster update re-adds them if
    // they join the network again.
    QList<MemberInfo> remaining;
    for (const MemberInfo &m : std::as_const(m_homeRoster))
        if (m.id != id)
            remaining.append(m);
    refreshDmList();
    setRoster(remaining);
    saveChatHistory();
    logSystem("Removed stale member " + (name.isEmpty() ? id.left(8) : name) + ".");
}

void MainWindow::refreshChannelList()
{
    QSignalBlocker blocker(m_channelList);
    m_channelList->clear();
    for (const QString &channel : std::as_const(m_channels)) {
        auto *item = new QListWidgetItem(
            (m_unread.contains(channel) ? "\xE2\x97\x8F " : "") + channel);
        item->setData(Qt::UserRole, channel);
        m_channelList->addItem(item);
        if (channel == m_currentConversation)
            m_channelList->setCurrentItem(item);
    }
    updateChatButton();
}

void MainWindow::refreshDmList()
{
    QSignalBlocker blocker(m_dmList);
    m_dmList->clear();
    for (const QString &peerId : std::as_const(m_openDms)) {
        const QString key = dmKey(peerId);
        auto *item = new QListWidgetItem(
            (m_unread.contains(key) ? "\xE2\x97\x8F " : "") + kDmPrefix +
            m_dmNames.value(peerId, QStringLiteral("unknown")));
        item->setData(Qt::UserRole, key);
        m_dmList->addItem(item);
        if (key == m_currentConversation)
            m_dmList->setCurrentItem(item);
    }
    updateHomeStats();
    updateChatButton();
}

void MainWindow::switchConversation(const QString &conversation)
{
    if (conversation.isEmpty() || conversation == m_currentConversation)
        return;
    sendTypingState(false);
    m_currentConversation = conversation;
    m_unread.remove(conversation);

    QString title = conversation;
    if (isDirectConversation(conversation))
        title = kDmPrefix + m_dmNames.value(dmPeerId(conversation),
                                            QStringLiteral("unknown"));
    m_channelTitle->setText(title);
    m_messageInput->setPlaceholderText("Message " + title);
    rebuildConversationView();
    refreshTypingLabel();

    // Selection lives in exactly one sidebar list at a time.
    if (isDirectConversation(conversation)) {
        QSignalBlocker blocker(m_channelList);
        m_channelList->clearSelection();
        m_channelList->setCurrentItem(nullptr);
    } else {
        QSignalBlocker blocker(m_dmList);
        m_dmList->clearSelection();
        m_dmList->setCurrentItem(nullptr);
    }
    refreshChannelList();
    refreshDmList();
}

void MainWindow::openDirectChat(const QString &peerId, const QString &peerName)
{
    m_dmNames.insert(peerId, peerName);
    if (!m_openDms.contains(peerId)) {
        m_openDms.append(peerId);
        refreshDmList();
    }
    showChatView(); // chat has no tab now — surface the chat view explicitly
    switchConversation(dmKey(peerId));
    m_messageInput->setFocus();
}

void MainWindow::promptAddChannel()
{
    if (!m_backend)
        return;
    bool ok = false;
    const QString name = QInputDialog::getText(
        this, "Add channel", "Channel name:", QLineEdit::Normal, "#", &ok);
    if (ok && !name.trimmed().isEmpty() && name.trimmed() != "#")
        m_backend->addChannel(name);
}

void MainWindow::sendCurrentMessage()
{
    const QString text = m_messageInput->text().trimmed();
    if (text.isEmpty() || !m_backend || m_currentConversation.isEmpty())
        return;
    sendTypingState(false);
    if (isDirectConversation(m_currentConversation))
        m_backend->sendDirect(dmPeerId(m_currentConversation), text);
    else
        m_backend->sendChat(m_currentConversation, text);
    m_messageInput->clear();
}

void MainWindow::onComposerEdited(const QString &text)
{
    if (!m_backend || m_currentConversation.isEmpty())
        return;
    if (text.trimmed().isEmpty()) {
        sendTypingState(false);
        return;
    }
    sendTypingState(true);
    m_typingStopTimer->start(2500);
}

void MainWindow::sendTypingState(bool active)
{
    if (!m_backend)
        return;
    if (active) {
        if (m_currentConversation.isEmpty())
            return;
        if (m_typingConversation == m_currentConversation &&
            m_typingStopTimer->isActive())
            return;
        m_typingConversation = m_currentConversation;
        m_backend->sendTyping(m_currentConversation, true);
        return;
    }
    if (!m_typingConversation.isEmpty()) {
        m_backend->sendTyping(m_typingConversation, false);
        m_typingConversation.clear();
    }
    m_typingStopTimer->stop();
}

void MainWindow::refreshTypingLabel()
{
    QStringList names;
    const auto active = m_typing.value(m_currentConversation);
    for (const QString &name : active)
        names.append(name);
    names.removeDuplicates();

    if (names.isEmpty()) {
        m_typingLabel->clear();
    } else if (names.size() == 1) {
        m_typingLabel->setText(names.first() + " is typing...");
    } else if (names.size() == 2) {
        m_typingLabel->setText(names.at(0) + " and " + names.at(1) +
                               " are typing...");
    } else {
        m_typingLabel->setText(QString::number(names.size()) + " people are typing...");
    }
}

// ------------------------------------------------------------------- files

void MainWindow::attachFile()
{
    if (!m_backend || m_currentConversation.isEmpty())
        return;
    const QString path =
        QFileDialog::getOpenFileName(this, "Share a file", QString(), "All files (*)");
    if (path.isEmpty())
        return;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        QMessageBox::warning(this, "Share a file", "Could not read " + path);
        return;
    }
    // Keep LAN transfers reasonable; the wire frame caps at ~96 MB.
    const qint64 maxBytes = 64ll * 1024 * 1024;
    if (file.size() > maxBytes) {
        QMessageBox::warning(this, "Share a file",
                             "That file is larger than 64 MB. Please share a "
                             "smaller file.");
        return;
    }
    const QByteArray data = file.readAll();
    const QFileInfo info(path);
    const QString mime = QMimeDatabase().mimeTypeForFileNameAndData(path, data).name();
    m_backend->sendFile(m_currentConversation, info.fileName(), mime, data);
}

void MainWindow::saveIncomingFile(const QString &fileName, const QByteArray &data)
{
    const QString safeName = QFileInfo(fileName).fileName().left(180);
    const QString dir =
        QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
    const QString suggested =
        (dir.isEmpty() ? QDir::homePath() : dir) + "/" +
        (safeName.isEmpty() ? QStringLiteral("file") : safeName);
    const QString path =
        QFileDialog::getSaveFileName(this, "Save file", suggested);
    if (path.isEmpty())
        return;
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(data) != data.size()) {
        QMessageBox::warning(this, "Save file", "Could not save to " + path);
        return;
    }
}

// ------------------------------------------------------------- repositories

QString MainWindow::repositoryMirrorRoot() const
{
    const QString configured =
        QSettings().value(kMirrorRootSetting).toString().trimmed();
    if (!configured.isEmpty())
        return configured;
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) +
           "/mirrors";
}

QString MainWindow::repositoryPreviewRoot() const
{
    const QString configured =
        QSettings().value(kPreviewCacheRootSetting).toString().trimmed();
    if (!configured.isEmpty())
        return configured;
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) +
           "/repo-preview-cache";
}

QString MainWindow::repositoryPreviewPath(const QString &owner,
                                          const QString &name) const
{
    return repositoryPreviewRoot() + "/" +
           repoSegment(owner, QStringLiteral("owner")) + "-" +
           repoSegment(name, QStringLiteral("repository")) + ".git";
}

QString MainWindow::repositoryNetworkCloneUrl(const QString &owner,
                                              const QString &name) const
{
    QUrl url = catalogApiUrl();
    url.setPath("/" + repoSegment(owner, QStringLiteral("owner")) + "/" +
                repoSegment(name, QStringLiteral("repository")));
    url.setQuery(QString());
    url.setFragment(QString());
    return url.toString();
}

void MainWindow::changeMirrorLocation()
{
    const QString chosen = QFileDialog::getExistingDirectory(
        this, "Choose where to store mirrored repositories",
        repositoryMirrorRoot());
    if (chosen.isEmpty())
        return;
    QSettings().setValue(kMirrorRootSetting, chosen);
    if (m_mirrorRootEdit)
        m_mirrorRootEdit->setText(chosen);
    logSystem("Mirror storage folder set to " + chosen +
              " (applies to newly added repositories).");
    QMessageBox::information(
        this, "Mirror storage",
        "New mirrors will be stored in:\n" + chosen +
            "\n\nExisting mirrors stay where they are. A local fork will push "
            "into its repository's mirror here.");
}

void MainWindow::changePreviewCacheLocation()
{
    const QString chosen = QFileDialog::getExistingDirectory(
        this, "Choose where to cache repository previews",
        repositoryPreviewRoot());
    if (chosen.isEmpty())
        return;
    QSettings().setValue(kPreviewCacheRootSetting, chosen);
    if (m_previewCacheRootEdit)
        m_previewCacheRootEdit->setText(chosen);
    logSystem("Preview cache folder set to " + chosen + ".");
    QMessageBox::information(
        this, "Preview cache",
        "Temporary repository previews will be stored in:\n" + chosen +
            "\n\nExisting preview caches stay where they are.");
}

QString MainWindow::repositoryChannel(const RepositoryRecord &repo) const
{
    return "#" + repoSegment(repo.owner, QStringLiteral("owner")) + "-" +
           repoSegment(repo.name, QStringLiteral("repository"));
}

QString MainWindow::repositorySource(const RepositoryRecord &repo) const
{
    return repo.localPath.trimmed().isEmpty() ? repo.cloneUrl.trimmed()
                                             : repo.localPath.trimmed();
}

void MainWindow::loadRepositories()
{
    m_repositories.clear();
    QSettings settings;
    const int count = settings.beginReadArray(kRepositoriesArray);
    for (int i = 0; i < count; ++i) {
        settings.setArrayIndex(i);
        RepositoryRecord repo;
        repo.owner = settings.value("owner").toString();
        repo.name = settings.value("name").toString();
        repo.description = settings.value("description").toString();
        repo.cloneUrl = settings.value("cloneUrl").toString();
        repo.localPath = settings.value("localPath").toString();
        repo.solanaAddress = settings.value("solanaAddress").toString();
        repo.mirrorPath = settings.value("mirrorPath").toString();
        repo.publishToNetwork = settings.value("publishToNetwork").toBool();
        repo.actionsEnabled = settings.value("actionsEnabled", true).toBool();
        repo.hostedSinceMs = settings.value("hostedSinceMs").toLongLong();
        repo.lastSyncMs = settings.value("lastSyncMs").toLongLong();
        repo.publishedAtMs = settings.value("publishedAtMs").toLongLong();
        if (!repo.name.isEmpty() && !repositorySource(repo).isEmpty())
            m_repositories.append(repo);
    }
    settings.endArray();
    loadRepoStats();
}

void MainWindow::saveRepositories() const
{
    QSettings settings;
    const int permanentCount =
        int(std::count_if(m_repositories.cbegin(), m_repositories.cend(),
                          [](const RepositoryRecord &repo) {
                              return !repo.previewOnly;
                          }));
    settings.beginWriteArray(kRepositoriesArray, permanentCount);
    int saved = 0;
    for (int i = 0; i < m_repositories.size(); ++i) {
        const RepositoryRecord &repo = m_repositories.at(i);
        if (repo.previewOnly)
            continue;
        settings.setArrayIndex(saved++);
        settings.setValue("owner", repo.owner);
        settings.setValue("name", repo.name);
        settings.setValue("description", repo.description);
        settings.setValue("cloneUrl", repo.cloneUrl);
        settings.setValue("localPath", repo.localPath);
        settings.setValue("solanaAddress", repo.solanaAddress);
        settings.setValue("mirrorPath", repo.mirrorPath);
        settings.setValue("publishToNetwork", repo.publishToNetwork);
        settings.setValue("actionsEnabled", repo.actionsEnabled);
        settings.setValue("hostedSinceMs", repo.hostedSinceMs);
        settings.setValue("lastSyncMs", repo.lastSyncMs);
        settings.setValue("publishedAtMs", repo.publishedAtMs);
    }
    settings.endArray();
}

void MainWindow::refreshRepositoryList()
{
    m_repoMenuEntries.clear();
    m_nodeMenuEntries.clear();

    // Repos grouped by node (owner).
    QHash<QString, QList<int>> reposByNode;
    for (int i = 0; i < m_repositories.size(); ++i)
        reposByNode[m_repositories.at(i).owner].append(i);

    // Node order: connected nodes first (the old leaderboard ranking — you, then
    // online, then by name), then any repo owners that aren't connected. Each
    // node is shown with its repos nested underneath.
    struct NodeInfo {
        bool inRoster = false;
        bool online = false;
        bool self = false;
        QString solana;
        QString balance;
        QString platform;
        QStringList mirrors;
    };
    QHash<QString, NodeInfo> nodes;
    QStringList nodeOrder;
    QList<MemberInfo> ranked = m_homeRoster;
    std::sort(ranked.begin(), ranked.end(),
              [](const MemberInfo &a, const MemberInfo &b) {
                  if (a.self != b.self)
                      return a.self;
                  if (a.online != b.online)
                      return a.online;
                  return a.name.localeAwareCompare(b.name) < 0;
              });
    for (const MemberInfo &m : std::as_const(ranked)) {
        if (m.name.isEmpty())
            continue;
        if (!nodes.contains(m.name)) {
            NodeInfo ni;
            ni.inRoster = true;
            ni.online = m.online;
            ni.self = m.self;
            ni.solana = m.solanaAddress.trimmed();
            if (ni.self && ni.solana.isEmpty())
                ni.solana = savedSolanaAddress();
            ni.balance = m.solanaBalance.trimmed();
            ni.platform = m.platform;
            ni.mirrors = m.mirrors;
            nodes.insert(m.name, ni);
            nodeOrder.append(m.name);
        } else {
            NodeInfo &ni = nodes[m.name];
            if (m.online)
                ni.online = true;
            if (ni.platform.isEmpty())
                ni.platform = m.platform;
            if (!m.mirrors.isEmpty())
                ni.mirrors = m.mirrors;
        }
    }
    for (int i = 0; i < m_repositories.size(); ++i) {
        const RepositoryRecord &repo = m_repositories.at(i);
        const QString owner = repo.owner;
        if (!nodes.contains(owner)) {
            if (repo.previewOnly)
                continue;
            nodes.insert(owner, NodeInfo());
            nodeOrder.append(owner);
        }
    }

    // Repos already shown locally by "owner/name", so advertised mirrors are
    // not duplicated.
    QSet<QString> shownLocalRepoKeys;
    for (const RepositoryRecord &repo : std::as_const(m_repositories)) {
        const QString key = repo.owner + "/" + repo.name;
        shownLocalRepoKeys.insert(key);
    }
    QSet<QString> shownAdvertised; // dedupe a repo advertised by several nodes

    // --- Nodes dropdown: one entry per node for the top-bar node switcher. The
    // OS badge (Linux/Windows/mac) signals online (tinted) vs offline (grey).
    bool selectedStillExists = false;
    for (const QString &node : std::as_const(nodeOrder)) {
        const NodeInfo info = nodes.value(node);
        NodeMenuEntry entry;
        entry.name = node;
        entry.platform = info.platform;
        entry.online = info.inRoster && info.online;
        entry.self = info.self;
        entry.repoCount = reposByNode.value(node).size();
        m_nodeMenuEntries.append(entry);
        if (node == m_selectedNode)
            selectedStillExists = true;
    }

    // Default the selection to the first node (the ranking puts you first) when
    // nothing is selected yet or the previously-selected node went away.
    if (!selectedStillExists)
        m_selectedNode = nodeOrder.isEmpty() ? QString() : nodeOrder.first();
    updateNodeSwitcher();

    // --- Repositories dropdown: the repos owned by the selected node, plus any
    // repos that node advertises mirroring that we don't already have.
    const NodeInfo selInfo = nodes.value(m_selectedNode);
    for (int i : reposByNode.value(m_selectedNode)) {
        const RepositoryRecord &repo = m_repositories.at(i);
        const bool online = repo.publishedAtMs > 0;
        QString label = repo.name;
        if (repo.previewOnly)
            label += "  \xC2\xB7 preview";
        else
            label += repo.publishToNetwork ? "  \xC2\xB7 public"
                                           : "  \xC2\xB7 private";
        if (m_syncingRepos.contains(i))
            label += repo.previewOnly ? "  \xC2\xB7 caching" : "  \xC2\xB7 syncing";
        RepoMenuEntry entry;
        entry.label = label;
        entry.index = i;
        // A repo glyph: green when published+online on the web, grey otherwise.
        entry.icon = themedOcticon(
            repo.previewOnly ? QStringLiteral("cloud") : QStringLiteral("repo"),
            repo.previewOnly ? QColor("#58a6ff")
                             : (repo.publishToNetwork && online ? QColor("#2ea043")
                                                                : QColor("#6e7681")),
            14);
        m_repoMenuEntries.append(entry);
    }
    for (const QString &ownerName : selInfo.mirrors) {
        if (shownLocalRepoKeys.contains(ownerName) ||
            shownAdvertised.contains(ownerName))
            continue;
        shownAdvertised.insert(ownerName);
        RepoMenuEntry entry;
        entry.label = QString::fromUtf8("\xE2\x86\x93 ") + ownerName +
                      QString::fromUtf8("   \xC2\xB7 browse");
        entry.index = -2; // advertised mirror marker
        entry.advertised = ownerName;
        entry.icon = themedOcticon("cloud", QColor("#58a6ff"), 14);
        m_repoMenuEntries.append(entry);
    }
    updateRepoSwitcher();
    updateRepoPushButton();
    updateRepoDetailStatus();
    updateRepoActionMenus();
    updateHomeStats();

    // Advertise our own mirrors so other nodes can see and mirror them too.
    // Advertise under the SAME owner/name the live host tunnel and catalog
    // register with (catalogOwner + canonical name), not the raw repo.owner.
    // Peers turn the advertised string straight into a clone URL, which the
    // worker routes to the DO keyed host:<owner>/<name>. If we advertised
    // repo.owner while the host socket connected as catalogOwner, the peer hit
    // a DO with no host attached and got a 503 — surfaced as "Sync deferred,
    // host temporarily unavailable" even though we were online and serving.
    if (m_backend) {
        QStringList ours;
        for (const RepositoryRecord &repo : std::as_const(m_repositories))
            if (!repo.previewOnly)
                ours << catalogOwner(repo) + "/" +
                            repoSegment(repo.name, QStringLiteral("repository"));
        m_backend->setMirroredRepos(ours);
    }
}

void MainWindow::mirrorAdvertisedRepo(const QString &ownerName)
{
    const int slash = ownerName.indexOf('/');
    if (slash <= 0)
        return;
    const QString owner = ownerName.left(slash);
    const QString name = ownerName.mid(slash + 1);
    int previewIndex = -1;
    for (int i = 0; i < m_repositories.size(); ++i) {
        const RepositoryRecord &r = m_repositories.at(i);
        if (r.owner != owner || r.name != name)
            continue;
        if (r.previewOnly) {
            previewIndex = i;
            continue;
        }
        QMessageBox::information(this, "Mirror",
                                 "You already mirror this repository.");
        return;
    }
    if (previewIndex >= 0) {
        mirrorPreviewRepository(previewIndex);
        return;
    }
    const QString cloneUrl = repositoryNetworkCloneUrl(owner, name);
    if (cloneUrl.isEmpty())
        return;

    if (QMessageBox::question(
            this, "Mirror it too",
            QStringLiteral("Mirror %1 into your local mirrors?\n\nIt will be cloned "
                           "from %2.")
                .arg(ownerName, cloneUrl)) != QMessageBox::Yes)
        return;

    RepositoryRecord repo;
    repo.owner = owner;
    repo.name = name;
    repo.cloneUrl = cloneUrl;
    repo.publishToNetwork = true;
    repo.hostedSinceMs = QDateTime::currentMSecsSinceEpoch();
    repo.mirrorPath = repositoryMirrorRoot() + "/" +
                      repoSegment(owner, QStringLiteral("owner")) + "-" +
                      repoSegment(name, QStringLiteral("repository")) + ".git";
    m_repositories.append(repo);
    saveRepositories();
    if (m_backend)
        m_backend->addChannel(repositoryChannel(repo));
    refreshRepositoryList();
    logSystem("Mirroring " + ownerName + " from " + cloneUrl);
    syncRepository(m_repositories.size() - 1); // clone from the network mirror
}

void MainWindow::previewAdvertisedRepo(const QString &ownerName)
{
    const int slash = ownerName.indexOf('/');
    if (slash <= 0)
        return;
    const QString owner = ownerName.left(slash);
    const QString name = ownerName.mid(slash + 1);

    for (int i = 0; i < m_repositories.size(); ++i) {
        const RepositoryRecord &repo = m_repositories.at(i);
        if (repo.owner == owner && repo.name == name) {
            openRepoDetail(i);
            if (repo.previewOnly && !m_syncingRepos.contains(i) &&
                !QDir(repo.mirrorPath).exists())
                syncRepository(i);
            return;
        }
    }

    RepositoryRecord repo;
    repo.owner = owner;
    repo.name = name;
    repo.cloneUrl = repositoryNetworkCloneUrl(owner, name);
    repo.previewOnly = true;
    repo.hostedSinceMs = QDateTime::currentMSecsSinceEpoch();
    repo.mirrorPath = repositoryPreviewPath(owner, name);
    m_repositories.append(repo);
    const int index = m_repositories.size() - 1;
    refreshRepositoryList();
    logSystem("Preview: caching " + ownerName + " from " + repo.cloneUrl);
    openRepoDetail(index);
    syncRepository(index);
}

void MainWindow::mirrorPreviewRepository(int index)
{
    if (index < 0 || index >= m_repositories.size())
        return;
    const RepositoryRecord preview = m_repositories.at(index);
    if (!preview.previewOnly)
        return;

    for (int i = 0; i < m_repositories.size(); ++i) {
        if (i == index)
            continue;
        const RepositoryRecord &repo = m_repositories.at(i);
        if (!repo.previewOnly && repo.owner == preview.owner &&
            repo.name == preview.name) {
            QMessageBox::information(
                this, "Mirror repository",
                QStringLiteral("You already mirror %1/%2.")
                    .arg(preview.owner, preview.name));
            return;
        }
    }

    const QString permanentPath = repositoryMirrorRoot() + "/" +
                                  repoSegment(preview.owner, QStringLiteral("owner")) +
                                  "-" +
                                  repoSegment(preview.name,
                                              QStringLiteral("repository")) +
                                  ".git";
    const QString source =
        (!preview.mirrorPath.isEmpty() && QDir(preview.mirrorPath).exists())
            ? preview.mirrorPath
            : preview.cloneUrl;
    if (source.isEmpty()) {
        setRepoDetailNotice("Preview is not cached yet; try again after it loads.",
                            true);
        return;
    }

    if (!QDir().mkpath(QFileInfo(permanentPath).absolutePath())) {
        QMessageBox::warning(this, "Mirror repository",
                             "Could not create " +
                                 QFileInfo(permanentPath).absolutePath());
        return;
    }

    if (QDir(permanentPath).exists()) {
        RepositoryRecord &repo = m_repositories[index];
        repo.previewOnly = false;
        repo.publishToNetwork = true;
        repo.hostedSinceMs = QDateTime::currentMSecsSinceEpoch();
        repo.mirrorPath = permanentPath;
        if (repo.lastSyncMs <= 0)
            repo.lastSyncMs = QDateTime::currentMSecsSinceEpoch();
        saveRepositories();
        ensurePushHook(repo);
        if (m_backend)
            m_backend->addChannel(repositoryChannel(repo));
        publishRepository(index, false);
        startRepoHosts();
        refreshRepositoryList();
        openRepoDetail(index);
        setRepoDetailNotice("Mirroring " + repo.owner + "/" + repo.name + ".");
        return;
    }

    m_syncingRepos.insert(index);
    refreshRepositoryList();
    setRepoDetailNotice("Creating permanent mirror...");
    logSystem(QStringLiteral("Mirror: promoting preview %1/%2 from %3 to %4.")
                  .arg(preview.owner, preview.name, source, permanentPath));

    auto *process = new QProcess(this);
    connect(process, &QProcess::finished, this,
            [this, process, index, permanentPath](int exitCode,
                                                  QProcess::ExitStatus status) {
                const QString errors =
                    QString::fromUtf8(process->readAllStandardError()).trimmed();
                process->deleteLater();
                m_syncingRepos.remove(index);
                if (index < 0 || index >= m_repositories.size()) {
                    refreshRepositoryList();
                    return;
                }
                RepositoryRecord &repo = m_repositories[index];
                if (status == QProcess::NormalExit && exitCode == 0) {
                    repo.previewOnly = false;
                    repo.publishToNetwork = true;
                    repo.hostedSinceMs = QDateTime::currentMSecsSinceEpoch();
                    repo.lastSyncMs = QDateTime::currentMSecsSinceEpoch();
                    repo.mirrorPath = permanentPath;
                    saveRepositories();
                    ensurePushHook(repo);
                    if (m_backend)
                        m_backend->addChannel(repositoryChannel(repo));
                    publishRepository(index, false);
                    startRepoHosts();
                    refreshRepositoryList();
                    openRepoDetail(index);
                    logSystem("Mirror: added " + repo.owner + "/" + repo.name +
                              " from preview cache.");
                    setRepoDetailNotice("Mirroring " + repo.owner + "/" +
                                        repo.name + ".");
                } else {
                    refreshRepositoryList();
                    logSystem("Mirror: could not promote preview: " +
                              errors.right(300));
                    setRepoDetailNotice(
                        "Could not create mirror" +
                            (errors.isEmpty() ? QString() :
                                                ": " + errors.right(160)),
                        true);
                }
            });
    connect(process, &QProcess::errorOccurred, this,
            [this, process, index](QProcess::ProcessError) {
                process->deleteLater();
                m_syncingRepos.remove(index);
                refreshRepositoryList();
                setRepoDetailNotice("Could not run git. Install Git and try again.",
                                    true);
            });
    process->start(QStringLiteral("git"),
                   {QStringLiteral("clone"), QStringLiteral("--mirror"), source,
                    permanentPath});
}

void MainWindow::promptAddRepository()
{
    // Simple flow: pick a local Git repository. Everything else is derived.
    // The folder is mirrored locally and only signed metadata is published to
    // the website; the .git data never leaves this machine.
    const QString path = QFileDialog::getExistingDirectory(
        this, "Choose a local Git repository to mirror and publish");
    if (path.isEmpty())
        return;

    const bool looksLikeGit =
        QDir(path).exists(".git") || QDir(path).exists("HEAD");
    if (!looksLikeGit) {
        QMessageBox::warning(
            this, "Add repository",
            "That folder is not a Git repository. Choose a folder created by "
            "\"git init\" or \"git clone\".");
        return;
    }

    RepositoryRecord repo;
    repo.localPath = path;
    repo.name = repoNameFromUrl(path);
    // Repos are namespaced under the single account name.
    repo.owner = accountOwner();
    repo.solanaAddress = savedSolanaAddress();
    // Selecting a local repo publishes it to the website so it shows up online
    // and others can discover and mirror it. No public clone URL is sent.
    repo.publishToNetwork = true;
    repo.hostedSinceMs = QDateTime::currentMSecsSinceEpoch();
    repo.mirrorPath = repositoryMirrorRoot() + "/" +
                      repoSegment(repo.owner, QStringLiteral("owner")) + "-" +
                      repoSegment(repo.name, QStringLiteral("repository")) + ".git";

    m_repositories.append(repo);
    saveRepositories();
    refreshRepositoryList();
    if (m_backend)
        m_backend->addChannel(repositoryChannel(repo));

    const QJsonObject metadata{{"owner", repo.owner},
                               {"name", repo.name},
                               {"channel", repositoryChannel(repo)},
                               {"mirrorPath", repo.mirrorPath},
                               {"hostedSince", QString::number(repo.hostedSinceMs)},
                               {"maintainer", m_profileIdentity.publicKey()}};
    logSystem("Repository: signed mirror metadata for " + repo.owner + "/" +
              repo.name + " with signature " +
              m_profileIdentity.signJson(metadata).left(16) + "...");
    publishRepository(m_repositories.size() - 1, false);
    syncRepository(m_repositories.size() - 1);
}

QString MainWindow::repositoryWebUrl(const RepositoryRecord &repo) const
{
    // Clean repository route on the public website, derived from the same host
    // that serves the catalog API. Static Assets routes this to the catalog SPA.
    // Key it by catalogOwner — the owner the catalog entry and live host tunnel
    // register under — so the page resolves to a host actually serving the repo.
    QUrl url = catalogApiUrl();
    url.setPath("/" + catalogOwner(repo) +
                "/" + repoSegment(repo.name, QStringLiteral("repository")));
    url.setFragment(QString());
    return url.toString();
}

void MainWindow::updateRepoActionMenus()
{
    if (!m_mirrorMenu || !m_forkMenu || !m_sourceMenu)
        return;
    m_mirrorMenu->clear();
    m_forkMenu->clear();
    m_sourceMenu->clear();

    if (m_repoDetailIndex < 0 || m_repoDetailIndex >= m_repositories.size()) {
        for (QMenu *menu : {m_mirrorMenu, m_forkMenu, m_sourceMenu}) {
            QAction *empty = menu->addAction("No repository selected");
            empty->setEnabled(false);
        }
        return;
    }

    const RepositoryRecord &repo = m_repositories.at(m_repoDetailIndex);
    const bool hasMirror = !repo.mirrorPath.isEmpty() && QDir(repo.mirrorPath).exists();
    const bool online = !repo.previewOnly && repo.publishedAtMs > 0;
    const QString webUrl = repositoryWebUrl(repo);

    // Mirror owns network availability, public browse URL, local storage and
    // destructive removal. This keeps those details next to the action they
    // describe instead of permanently expanding the repository header.
    if (repo.previewOnly) {
        m_mirrorMenu->addSection(hasMirror ? "PREVIEW CACHED" : "PREVIEW PENDING");
        QAction *keep = m_mirrorMenu->addAction("Keep as a local mirror");
        connect(keep, &QAction::triggered, this, [this] {
            if (m_repoDetailIndex >= 0)
                mirrorPreviewRepository(m_repoDetailIndex);
        });
    } else {
        m_mirrorMenu->addSection("MIRROR STATUS");
        if (online) {
            QAction *browse = m_mirrorMenu->addAction(
                QStringLiteral("Online ") + QChar(0x00B7) +
                " browsable at " + webUrl);
            browse->setToolTip(webUrl);
            connect(browse, &QAction::triggered, this,
                    [webUrl] { QDesktopServices::openUrl(QUrl(webUrl)); });
        } else {
            QAction *status = m_mirrorMenu->addAction(
                repo.publishToNetwork ? "Publishing to ForkMesh..." : "Local only");
            status->setEnabled(false);
        }
        m_mirrorMenu->addSection("MIRROR LOCATION");
        QAction *location = m_mirrorMenu->addAction(
            hasMirror ? repo.mirrorPath : "Mirror has not been created yet");
        location->setEnabled(false);
        QAction *sync = m_mirrorMenu->addAction(hasMirror ? "Sync mirror now"
                                                           : "Create mirror now");
        connect(sync, &QAction::triggered, this, [this] {
            if (m_repoDetailIndex >= 0)
                syncRepository(m_repoDetailIndex);
        });
        if (hasMirror) {
            QAction *copy = m_mirrorMenu->addAction("Copy mirror location");
            connect(copy, &QAction::triggered, this, [this] {
                if (m_repoDetailIndex < 0 ||
                    m_repoDetailIndex >= m_repositories.size())
                    return;
                QApplication::clipboard()->setText(
                    m_repositories.at(m_repoDetailIndex).mirrorPath);
                setRepoDetailNotice("Copied mirror location.");
            });
        }
        m_mirrorMenu->addSeparator();
        QAction *remove = m_mirrorMenu->addAction("Delete mirror...");
        connect(remove, &QAction::triggered, this, &MainWindow::deleteCurrentMirror);
    }

    // Fork shows the destination identity and its editable checkout. A bare
    // fork may intentionally have no working directory attached yet.
    m_forkMenu->addSection("FORKED TO");
    QAction *forkTarget = m_forkMenu->addAction(repo.owner + "/" + repo.name);
    forkTarget->setEnabled(false);
    m_forkMenu->addSection("WORKING DIRECTORY");
    const bool hasWorktree = !repo.localPath.isEmpty() && QDir(repo.localPath).exists();
    QAction *worktree = m_forkMenu->addAction(
        hasWorktree ? repo.localPath : "No working directory attached");
    worktree->setEnabled(false);
    if (hasWorktree) {
        QAction *open = m_forkMenu->addAction("Open working directory");
        connect(open, &QAction::triggered, this, [path = repo.localPath] {
            QDesktopServices::openUrl(QUrl::fromLocalFile(path));
        });
    }
    m_forkMenu->addSeparator();
    QAction *fork = m_forkMenu->addAction("Create another fork...");
    connect(fork, &QAction::triggered, this, &MainWindow::forkCurrentRepo);

    // Source holds distribution and Git-remote details.
    m_sourceMenu->addSection("LOCAL REMOTE");
    QAction *remote = m_sourceMenu->addAction(
        hasMirror ? repo.mirrorPath : "Sync first to create a local remote");
    remote->setEnabled(false);
    if (hasMirror) {
        QAction *copyRemote = m_sourceMenu->addAction("Copy local remote");
        connect(copyRemote, &QAction::triggered, this, [this] {
            if (m_repoDetailIndex < 0 ||
                m_repoDetailIndex >= m_repositories.size())
                return;
            const QString path = m_repositories.at(m_repoDetailIndex).mirrorPath;
            QApplication::clipboard()->setText(path);
            setRepoDetailNotice("Copied local remote path.");
            logSystem("Copied local remote path to clipboard: " + path);
        });
        QAction *copyCommand = m_sourceMenu->addAction(
            "Copy git remote add command");
        connect(copyCommand, &QAction::triggered, this, [this] {
            if (m_repoDetailIndex < 0 ||
                m_repoDetailIndex >= m_repositories.size())
                return;
            const QString path = m_repositories.at(m_repoDetailIndex).mirrorPath;
            QApplication::clipboard()->setText(
                QStringLiteral("git remote add forkmesh \"") + path + "\"");
            setRepoDetailNotice("Copied git remote add command.");
        });
    }
    m_sourceMenu->addSeparator();
    QAction *zip = m_sourceMenu->addAction("Download ZIP");
    zip->setEnabled(hasMirror || hasWorktree);
    connect(zip, &QAction::triggered, this, &MainWindow::downloadCurrentRepoZip);
}

void MainWindow::deleteCurrentMirror()
{
    if (m_repoDetailIndex < 0 || m_repoDetailIndex >= m_repositories.size())
        return;
    const int index = m_repoDetailIndex;
    const RepositoryRecord repo = m_repositories.at(index);
    const QString rawPath = repo.mirrorPath.trimmed();
    const QString rawWorktree = repo.localPath.trimmed();
    const QString path = rawPath.isEmpty() ? QString() : QDir::cleanPath(rawPath);
    const QString worktree = rawWorktree.isEmpty()
                                 ? QString()
                                 : QDir::cleanPath(rawWorktree);

    QString message = QStringLiteral(
        "Delete the local mirror for %1/%2?\n\nMirror: %3")
                          .arg(repo.owner, repo.name,
                               path.isEmpty() ? QStringLiteral("not created") : path);
    if (!repo.localPath.isEmpty())
        message += QStringLiteral("\n\nYour working directory will be kept:\n%1")
                       .arg(repo.localPath);
    if (!repo.previewOnly && repo.publishToNetwork)
        message += QStringLiteral("\n\nThe repository will also be removed from "
                                  "the public ForkMesh catalog.");
    if (QMessageBox::warning(this, "Delete mirror", message,
                             QMessageBox::Yes | QMessageBox::Cancel,
                             QMessageBox::Cancel) != QMessageBox::Yes)
        return;

    if (!path.isEmpty() && path == worktree) {
        QMessageBox::warning(
            this, "Delete mirror",
            "The mirror path matches the working directory, so nothing was deleted.");
        return;
    }

    stopRepoHosts();
    if (!path.isEmpty() && QDir(path).exists() && !QDir(path).removeRecursively()) {
        startRepoHosts();
        QMessageBox::warning(this, "Delete mirror",
                             "Could not delete the mirror at:\n" + path);
        return;
    }
    if (!repo.previewOnly && repo.publishToNetwork)
        deleteCatalogRepository(catalogOwner(repo), repo.name);
    const bool canKeepLocalRecord = !repo.localPath.trimmed().isEmpty() ||
                                    !repo.cloneUrl.trimmed().isEmpty();
    if (!repo.previewOnly && canKeepLocalRecord) {
        // Delete the mirror, not the source checkout. Keep the repository in
        // the app as local-only so Mirror > Create mirror can publish it again.
        RepositoryRecord &local = m_repositories[index];
        local.publishToNetwork = false;
        local.publishedAtMs = 0;
        local.lastSyncMs = 0;
    } else {
        // A preview or independent bare-only fork has no separate source left
        // after its mirror is deleted, so its transient record goes too.
        m_repositories.removeAt(index);
    }
    saveRepositories();
    startRepoHosts();
    refreshRepositoryList();
    if (!repo.previewOnly && canKeepLocalRecord) {
        m_repoDetailIndex = index;
        updateRepoDetailStatus();
        updateRepoActionMenus();
    } else {
        m_repoDetailIndex = -1;
        if (!m_repositories.isEmpty())
            openRepoDetail(qMin(index, m_repositories.size() - 1));
        else if (m_repoDetailStack && m_chatStackIndex >= 0)
            m_repoDetailStack->setCurrentIndex(m_chatStackIndex);
    }
    logSystem("Deleted local mirror for " + repo.owner + "/" + repo.name + ".");
    setRepoDetailNotice(canKeepLocalRecord
                            ? "Deleted mirror. The working directory was kept."
                            : "Deleted mirror.");
}

void MainWindow::updateRepoDetailStatus()
{
    if (!m_repoDetailStatus)
        return;
    if (m_repoDetailIndex < 0 || m_repoDetailIndex >= m_repositories.size()) {
        m_repoDetailStatus->clear();
        return;
    }
    const RepositoryRecord &repo = m_repositories.at(m_repoDetailIndex);
    const QString repoKey = repo.owner + "/" + repo.name;
    const QPair<int, int> stats = m_repoStats.value(repoKey);
    if (repo.previewOnly) {
        const bool cached =
            !repo.mirrorPath.isEmpty() && QDir(repo.mirrorPath).exists();
        QStringList bits;
        bits << (cached ? QStringLiteral("<b>Preview cached</b>")
                        : QStringLiteral("<b>Preview cache pending</b>"));
        bits << QStringLiteral("<b>Temporary</b>");
        bits << QStringLiteral("<b>%1</b> served").arg(stats.first);
        bits << QStringLiteral("<b>%1</b> clone%2")
                    .arg(stats.second)
                    .arg(stats.second == 1 ? QString()
                                           : QStringLiteral("s"));
        QString details = bits.join(QStringLiteral(" \xC2\xB7 "));
        details += QStringLiteral("<br><span style='color:#8b949e'>Cache %1 "
                                  "\xC2\xB7 Last refresh %2</span>")
                       .arg(repo.mirrorPath.toHtmlEscaped(),
                            formatRepoDate(repo.lastSyncMs));
        m_repoDetailStatus->setText(details);
        return;
    }
    QStringList bits;
    bits << QStringLiteral("<b>%1</b> served").arg(stats.first);
    bits << QStringLiteral("<b>%1</b> clone%2")
                .arg(stats.second)
                .arg(stats.second == 1 ? QString() : QStringLiteral("s"));

    QString details = bits.join(QStringLiteral(" \xC2\xB7 "));
    details += QStringLiteral("<br><span style='color:#8b949e'>Hosted since %1 "
                              "\xC2\xB7 Last sync %2</span>")
                   .arg(formatRepoDate(repo.hostedSinceMs),
                        formatRepoDate(repo.lastSyncMs));
    m_repoDetailStatus->setText(details);
}

QUrl MainWindow::catalogApiUrl() const
{
    QUrl url(m_serverUrlEdit ? m_serverUrlEdit->text().trimmed() : kDefaultServerUrl);
    if (!url.isValid() || url.host().isEmpty())
        url = QUrl(kDefaultServerUrl);
    if (url.scheme() == "ws")
        url.setScheme(QStringLiteral("http"));
    else if (url.scheme() == "wss")
        url.setScheme(QStringLiteral("https"));
    url.setPath(QStringLiteral("/api/repositories"));
    url.setQuery(QString());
    url.setFragment(QString());
    return url;
}

void MainWindow::deleteCatalogRepository(const QString &owner, const QString &name)
{
    const QString safeOwner = repoSegment(owner, QStringLiteral("owner"));
    const QString safeName = repoSegment(name, QStringLiteral("repository"));
    if (safeOwner.isEmpty() || safeName.isEmpty())
        return;
    if (!m_profileIdentity.isValid() && !m_profileIdentity.load()) {
        logSystem("Catalog: could not load identity to remove old website entry.");
        return;
    }

    const QString ts = QString::number(QDateTime::currentMSecsSinceEpoch());
    const QByteArray canonical =
        ("forkmesh-catalog-delete-v1\n" + safeOwner + "\n" + safeName + "\n" + ts)
            .toUtf8();
    QUrl url = catalogApiUrl();
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("owner"), safeOwner);
    query.addQueryItem(QStringLiteral("name"), safeName);
    query.addQueryItem(QStringLiteral("ts"), ts);
    query.addQueryItem(QStringLiteral("sig"), m_profileIdentity.signData(canonical));
    url.setQuery(query);

    QNetworkReply *reply = m_networkAccess->deleteResource(QNetworkRequest(url));
    connect(reply, &QNetworkReply::finished, this,
            [this, reply, safeOwner, safeName] {
                reply->deleteLater();
                if (reply->error() == QNetworkReply::NoError) {
                    logSystem("Catalog: removed old website entry " + safeOwner +
                              "/" + safeName + ".");
                } else {
                    logSystem("Catalog: could not remove old website entry " +
                              safeOwner + "/" + safeName + ": " +
                              reply->errorString());
                }
            });
}

void MainWindow::migrateReposForProfileName(const QString &oldOwner,
                                            const QString &newOwner)
{
    const QString oldName = repoSegment(oldOwner, QStringLiteral("owner"));
    const QString newName = repoSegment(newOwner, QStringLiteral("owner"));
    if (oldName.isEmpty() || newName.isEmpty() || oldName == newName)
        return;

    QList<int> toRepublish;
    bool changed = false;
    for (int i = 0; i < m_repositories.size(); ++i) {
        RepositoryRecord &repo = m_repositories[i];
        if (repo.previewOnly)
            continue;
        const QString repoName = repoSegment(repo.name, QStringLiteral("repository"));
        const bool wasPublished = repo.publishToNetwork || repo.publishedAtMs > 0;
        if (wasPublished)
            deleteCatalogRepository(oldName, repoName);
        if (repo.owner == oldName || wasPublished) {
            repo.owner = newName;
            changed = true;
            if (repo.publishToNetwork)
                toRepublish.append(i);
        }
    }

    if (!changed)
        return;
    saveRepositories();
    installAllPushHooks();
    refreshRepositoryList();
    startRepoHosts();
    for (int index : std::as_const(toRepublish))
        publishRepository(index, false);
    logSystem("Renamed local published repositories from " + oldName + " to " +
              newName + ".");
}

QUrl MainWindow::hostWsUrl(const RepositoryRecord &repo) const
{
    QUrl url(m_serverUrlEdit ? m_serverUrlEdit->text().trimmed() : kDefaultServerUrl);
    if (!url.isValid() || url.host().isEmpty())
        url = QUrl(kDefaultServerUrl);
    if (url.scheme() == "http")
        url.setScheme(QStringLiteral("ws"));
    else if (url.scheme() == "https")
        url.setScheme(QStringLiteral("wss"));
    url.setPath("/api/repo/" + catalogOwner(repo) +
                "/" + repoSegment(repo.name, QStringLiteral("repository")) +
                "/host");
    url.setQuery(QString());
    url.setFragment(QString());
    return url;
}

void MainWindow::stopRepoHosts()
{
    for (RepoHost *host : std::as_const(m_repoHosts)) {
        host->stop();
        host->deleteLater();
    }
    m_repoHosts.clear();
}

void MainWindow::startRepoHosts()
{
    // One live host per published repository that already has a local mirror.
    // Rebuilt from scratch so adding/removing repos stays simple.
    stopRepoHosts();
    for (const RepositoryRecord &repo : std::as_const(m_repositories)) {
        if (repo.previewOnly || !repo.publishToNetwork || repo.mirrorPath.isEmpty() ||
            !QDir(repo.mirrorPath).exists())
            continue;
        auto *host = new RepoHost(catalogOwner(repo), repo.name, repo.mirrorPath,
                                  hostWsUrl(repo), this);
        connect(host, &RepoHost::log, this, &MainWindow::logSystem);
        connect(host, &RepoHost::requestServed, this, &MainWindow::onRequestServed);
        host->start();
        m_repoHosts.append(host);
    }
}

void MainWindow::onRequestServed(const QString &owner, const QString &name, bool clone)
{
    QPair<int, int> &stats = m_repoStats[owner + "/" + name];
    stats.first += 1; // served through the mainnode
    if (clone)
        stats.second += 1; // git clone
    saveRepoStats();
    refreshRepositoryList();
    if (m_repoDetailIndex >= 0 && m_repoDetailIndex < m_repositories.size()) {
        const RepositoryRecord &repo = m_repositories.at(m_repoDetailIndex);
        if (repo.owner == owner && repo.name == name)
            updateRepoDetailStatus();
    }
    // Hosting stats now live in the node profile; keep them current while it is open.
    if (m_nodeProfilePanel && m_nodeProfilePanel->isVisible())
        refreshProfileHostingStats();
}

void MainWindow::loadRepoStats()
{
    m_repoStats.clear();
    const QJsonObject obj =
        QJsonDocument::fromJson(
            QSettings().value(QStringLiteral("repositories/stats")).toByteArray())
            .object();
    for (auto it = obj.constBegin(); it != obj.constEnd(); ++it) {
        const QJsonObject entry = it.value().toObject();
        m_repoStats.insert(it.key(),
                           {entry.value("served").toInt(),
                            entry.value("clones").toInt()});
    }
}

void MainWindow::saveRepoStats() const
{
    QJsonObject obj;
    for (auto it = m_repoStats.constBegin(); it != m_repoStats.constEnd(); ++it) {
        obj.insert(it.key(), QJsonObject{{"served", it.value().first},
                                         {"clones", it.value().second}});
    }
    QSettings().setValue(QStringLiteral("repositories/stats"),
                         QJsonDocument(obj).toJson(QJsonDocument::Compact));
}

void MainWindow::publishRepository(int index, bool showDialogOnError)
{
    if (index < 0 || index >= m_repositories.size())
        return;
    if (m_repositories.at(index).previewOnly)
        return;
    if (!m_profileIdentity.isValid() && !m_profileIdentity.load()) {
        logSystem("Catalog: could not load identity for repository publishing.");
        return;
    }

    RepositoryRecord &repo = m_repositories[index];
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    // Always publish under the registered account name so the catalog dedups by
    // account/name (one entry per fork) and the server can verify ownership.
    // catalogOwner() is shared with the live host tunnel so the website browses
    // the same owner the host registers under.
    const QString owner = catalogOwner(repo);
    const QString name = repoSegment(repo.name, QStringLiteral("repository"));
    const QString updatedAt = QString::number(now);
    QJsonObject metadata{{"owner", owner},
                         {"name", name},
                         {"description", repo.description},
                         {"cloneUrl", repo.cloneUrl},
                         {"solana", repo.solanaAddress},
                         {"channel", repositoryChannel(repo)},
                         {"hostedSince", QString::number(repo.hostedSinceMs)},
                         {"lastSync", QString::number(repo.lastSyncMs)},
                         {"updatedAt", updatedAt},
                         {"source", repo.localPath.trimmed().isEmpty()
                                        ? QStringLiteral("remote-clone")
                                        : QStringLiteral("local-node")},
                         {"maintainer", m_profileIdentity.publicKey()}};
    metadata.insert("signature", m_profileIdentity.signJson(metadata));
    // The server verifies this against the account's registered pubkey: only the
    // account key holder can write its namespace (prevents impersonation/dups).
    const QByteArray catalogCanonical =
        ("forkmesh-catalog-v1\n" + owner + "\n" + name + "\n" + updatedAt).toUtf8();
    metadata.insert("catalogSig", m_profileIdentity.signData(catalogCanonical));

    QNetworkRequest request(catalogApiUrl());
    request.setHeader(QNetworkRequest::ContentTypeHeader,
                      QStringLiteral("application/json"));
    request.setRawHeader("Accept", "application/json");
    QNetworkReply *reply =
        m_networkAccess->post(request, QJsonDocument(metadata).toJson(QJsonDocument::Compact));
    logSystem("Catalog: publishing " + repo.owner + "/" + repo.name + " to " +
              request.url().toString() + ".");

    connect(reply, &QNetworkReply::finished, this,
            [this, reply, index, showDialogOnError] {
                const QByteArray body = reply->readAll();
                const int status =
                    reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
                const QNetworkReply::NetworkError error = reply->error();
                reply->deleteLater();

                if (index < 0 || index >= m_repositories.size())
                    return;

                RepositoryRecord &repo = m_repositories[index];
                if (error == QNetworkReply::NoError && status >= 200 && status < 300) {
                    repo.publishToNetwork = true;
                    repo.publishedAtMs = QDateTime::currentMSecsSinceEpoch();
                    saveRepositories();
                    refreshRepositoryList();
                    logSystem("Catalog: published " + repo.owner + "/" +
                              repo.name + " to forkmesh.com.");
                    if (showDialogOnError)
                        flashMessage("Published " + repo.owner + "/" + repo.name +
                                     " to forkmesh.com.");
                    return;
                }

                const QString detail =
                    QString::fromUtf8(body).trimmed().left(500);
                const QString message =
                    "Catalog publish failed for " + repo.owner + "/" + repo.name +
                    (status > 0 ? " (HTTP " + QString::number(status) + ")" :
                                  QString()) +
                    (detail.isEmpty() ? QString() : ": " + detail);
                logSystem(message);
                if (showDialogOnError)
                    flashMessage(message, /*error=*/true);
            });
}

namespace {

// A cheap digest of all refs in a bare mirror, so an automatic fetch can tell
// whether the owner's repo actually changed before announcing/republishing.
QString mirrorRefsDigest(const QString &mirrorPath)
{
    if (!QDir(mirrorPath).exists())
        return QString();
    QProcess p;
    p.start("git", {"-C", mirrorPath, "for-each-ref",
                    "--format=%(objectname) %(refname)"});
    if (!p.waitForFinished(5000))
        return QString();
    return QString::fromUtf8(p.readAllStandardOutput());
}

QString mirrorHeadBranch(const QString &mirrorPath)
{
    if (!QDir(mirrorPath).exists())
        return QString();
    QProcess p;
    p.start("git", {"-C", mirrorPath, "symbolic-ref", "--short", "HEAD"});
    if (!p.waitForFinished(5000) || p.exitCode() != 0)
        return QString();
    return QString::fromUtf8(p.readAllStandardOutput()).trimmed();
}

QString mirrorBranchCommit(const QString &mirrorPath, const QString &branch)
{
    if (!QDir(mirrorPath).exists() || branch.isEmpty())
        return QString();
    QProcess p;
    p.start("git", {"-C", mirrorPath, "rev-parse", "--verify",
                    "refs/heads/" + branch});
    if (!p.waitForFinished(5000) || p.exitCode() != 0)
        return QString();
    return QString::fromUtf8(p.readAllStandardOutput()).trimmed();
}

void repairMirrorHead(const QString &mirrorPath, const QString &sourcePath)
{
    QString preferred;
    if (QDir(sourcePath).exists(QStringLiteral(".git"))) {
        QProcess source;
        source.start("git", {"-C", sourcePath, "symbolic-ref", "--short", "HEAD"});
        if (source.waitForFinished(5000) && source.exitCode() == 0)
            preferred = QString::fromUtf8(source.readAllStandardOutput()).trimmed();
    }

    const QString current = mirrorHeadBranch(mirrorPath);
    QStringList candidates{preferred, QStringLiteral("main"),
                           QStringLiteral("master"), current};
    QString branch;
    for (const QString &candidate : std::as_const(candidates)) {
        if (!candidate.isEmpty() &&
            !mirrorBranchCommit(mirrorPath, candidate).isEmpty()) {
            branch = candidate;
            break;
        }
    }
    if (branch.isEmpty()) {
        QProcess refs;
        refs.start("git", {"-C", mirrorPath, "for-each-ref",
                           "--format=%(refname:short)", "--sort=-committerdate",
                           "--count=1", "refs/heads/"});
        if (refs.waitForFinished(5000) && refs.exitCode() == 0)
            branch = QString::fromUtf8(refs.readAllStandardOutput()).trimmed();
    }
    if (branch.isEmpty() || branch == current)
        return;

    QProcess setHead;
    setHead.start("git", {"-C", mirrorPath, "symbolic-ref", "HEAD",
                          "refs/heads/" + branch});
    setHead.waitForFinished(5000);
}

} // namespace

void MainWindow::autoSyncMirrors()
{
    // Quietly refresh every repo's mirror so it tracks the owner's repo.
    for (int i = 0; i < m_repositories.size(); ++i) {
        if (!m_syncingRepos.contains(i) &&
            !m_repositories.at(i).previewOnly &&
            !repositorySource(m_repositories.at(i)).isEmpty())
            syncRepository(i, /*quiet=*/true);
    }
}

void MainWindow::onPeerMirrorUpdated(const QString &ownerName,
                                     const QString &peerName)
{
    // Only surface it if we keep a real mirror of this repo (browse-only
    // previews don't count) — otherwise the peer's update isn't relevant here.
    bool relevant = false;
    for (const RepositoryRecord &repo : std::as_const(m_repositories)) {
        if (!repo.previewOnly && (repo.owner + "/" + repo.name) == ownerName) {
            relevant = true;
            break;
        }
    }
    if (!relevant)
        return;

    const QString who =
        peerName.trimmed().isEmpty() ? QStringLiteral("A peer") : peerName.trimmed();
    const QString msg = who + " updated the mirror of " + ownerName +
                        " from its source.";
    logSystem(msg);
    flashMessage(msg);
    if (m_trayIcon && QSystemTrayIcon::supportsMessages())
        m_trayIcon->showMessage("ForkMesh — mirror updated", msg,
                                QSystemTrayIcon::Information, 6000);
}

void MainWindow::syncRepository(int index, bool quiet)
{
    if (index < 0 || index >= m_repositories.size() ||
        m_syncingRepos.contains(index))
        return;

    RepositoryRecord &repo = m_repositories[index];
    const bool preview = repo.previewOnly;
    if (!QDir().mkpath(QFileInfo(repo.mirrorPath).absolutePath())) {
        if (!quiet)
            QMessageBox::warning(this, "Sync repository",
                                 "Could not create " +
                                     QFileInfo(repo.mirrorPath).absolutePath());
        return;
    }

    const bool hasMirror = QDir(repo.mirrorPath).exists();
    const QString source = repositorySource(repo);

    // A repository we publish and host ourselves, with no separate upstream
    // working copy, IS the source of truth. Re-fetching it would loop back
    // through the relay to our own host tunnel and fail (HTTP 5xx), so there is
    // nothing to sync.
    if (!preview && hasMirror && repo.publishToNetwork &&
        repo.localPath.trimmed().isEmpty() && repo.owner == accountOwner()) {
        if (!quiet)
            flashMessage(QStringLiteral("Nothing to sync for %1/%2 — this node "
                                        "hosts it directly.")
                             .arg(repo.owner, repo.name));
        return;
    }

    const QString beforeDigest = mirrorRefsDigest(repo.mirrorPath);
    const QString beforeHeadBranch = mirrorHeadBranch(repo.mirrorPath);
    const QString beforeHeadCommit =
        mirrorBranchCommit(repo.mirrorPath, beforeHeadBranch);
    const QStringList args = hasMirror
                                 ? QStringList{"-C", repo.mirrorPath,
                                               "fetch", "--prune"}
                                 : QStringList{"clone", "--mirror",
                                               source, repo.mirrorPath};

    // Track the live source: an owned repo with a local working copy should
    // fetch from that copy, not from a stale relay URL baked into origin at
    // clone time (which can return HTTP 5xx through the host tunnel).
    if (hasMirror && !source.isEmpty())
        runGitCapture(repo.mirrorPath,
                      {QStringLiteral("remote"), QStringLiteral("set-url"),
                       QStringLiteral("origin"), source},
                      nullptr, nullptr);

    m_syncingRepos.insert(index);
    refreshRepositoryList();
    if (!quiet) {
        const QString prefix =
            preview ? QStringLiteral("Preview cache: ")
                    : QStringLiteral("Mirror: ");
        logSystem(prefix +
                  (hasMirror ? QStringLiteral("fetching ") : QStringLiteral("cloning ")) +
                  repo.owner + "/" + repo.name + " from " + source + ".");
    }

    auto *process = new QProcess(this);
    connect(process, &QProcess::finished, this,
            [this, process, index, quiet, beforeDigest, beforeHeadCommit,
             hasMirror](
                int exitCode, QProcess::ExitStatus) {
                const QString errors =
                    QString::fromUtf8(process->readAllStandardError()).trimmed();
                process->deleteLater();
                m_syncingRepos.remove(index);

                if (index < 0 || index >= m_repositories.size()) {
                    refreshRepositoryList();
                    return;
                }

                RepositoryRecord &repo = m_repositories[index];
                if (exitCode == 0) {
                    // Fetch does not repair a bare repo's symbolic HEAD. Keep it
                    // on the source/default branch so smart-HTTP clones check
                    // out real content even after agent PR activity.
                    repairMirrorHead(repo.mirrorPath, repo.localPath);
                    const bool stillPreview = repo.previewOnly;
                    // Did the owner's repo actually change?
                    const bool changed =
                        !hasMirror ||
                        mirrorRefsDigest(repo.mirrorPath) != beforeDigest;
                    repo.lastSyncMs = QDateTime::currentMSecsSinceEpoch();
                    if (!stillPreview)
                        saveRepositories();
                    refreshRepositoryList();
                    // Now that the bare mirror exists, (re)install the push hook
                    // so local pushes are detected (for actions + live refresh).
                    if (!stillPreview)
                        ensurePushHook(repo);
                    if (changed && hasMirror && !stillPreview && m_actionStore) {
                        const QString branch = mirrorHeadBranch(repo.mirrorPath);
                        const QString commit =
                            mirrorBranchCommit(repo.mirrorPath, branch);
                        if (!branch.isEmpty() && !commit.isEmpty() &&
                            commit != beforeHeadCommit) {
                            enqueuePushEvent(repo.owner, repo.name, commit,
                                             "refs/heads/" + branch);
                        }
                    }
                    // If this repo's detail is open, reflect the new commits.
                    if (changed && index == m_repoDetailIndex)
                        refreshOpenRepoDetail();
                    // Tell connected peers that also mirror this repo that it
                    // advanced from its source of truth. Only for real mirrors
                    // that already existed (an actual update, not a first clone).
                    if (changed && hasMirror && !stillPreview && m_backend)
                        m_backend->notifyMirrorUpdated(
                            catalogOwner(repo) + "/" +
                            repoSegment(repo.name, QStringLiteral("repository")));
                    // Quiet auto-syncs only speak up when something changed.
                    if (!quiet || changed) {
                        logSystem((stillPreview ? QStringLiteral("Preview cache: cached ")
                                                : QStringLiteral("Mirror: synced ")) +
                                  repo.owner + "/" + repo.name + " into " +
                                  repo.mirrorPath + ".");
                    }
                    if (!quiet) {
                        flashMessage(stillPreview
                                         ? "Cached preview for " + repo.owner + "/" +
                                               repo.name +
                                               (changed ? QString()
                                                        : " (already up to date)")
                                         : "Synced " + repo.owner + "/" + repo.name +
                                               (changed ? QString()
                                                        : " (already up to date)"));
                    }
                    if (!stillPreview && repo.publishToNetwork &&
                        (changed || !quiet)) {
                        publishRepository(index, false);
                        // Serve this repo's files live to the web now that a
                        // mirror exists (pure live tunnel, nothing uploaded).
                        startRepoHosts();
                    }
                } else {
                    refreshRepositoryList();
                    // Relay/host hiccups (HTTP 5xx, RPC failed, connection
                    // resets) are transient: the host serving this repo is
                    // momentarily unavailable and the next sync will retry. Log
                    // them quietly rather than raising a persistent red error.
                    const bool transient =
                        errors.contains(QStringLiteral("HTTP 50")) ||
                        errors.contains(QStringLiteral("RPC failed")) ||
                        errors.contains(QStringLiteral("curl 22")) ||
                        errors.contains(QStringLiteral("502")) ||
                        errors.contains(QStringLiteral("503")) ||
                        errors.contains(QStringLiteral("504")) ||
                        errors.contains(QStringLiteral("Could not resolve"),
                                        Qt::CaseInsensitive) ||
                        errors.contains(QStringLiteral("Couldn't connect"),
                                        Qt::CaseInsensitive) ||
                        errors.contains(QStringLiteral("Connection reset"),
                                        Qt::CaseInsensitive);
                    logSystem((repo.previewOnly ? QStringLiteral("Preview cache: sync failed for ")
                                                : QStringLiteral("Mirror: sync failed for ")) +
                              repo.owner + "/" +
                              repo.name +
                              (transient ? QStringLiteral(" (host temporarily "
                                                          "unavailable): ")
                                         : QStringLiteral(": ")) +
                              errors.right(300));
                    if (!quiet && transient) {
                        flashMessage(QStringLiteral("Sync deferred for %1/%2 — host "
                                                    "temporarily unavailable.")
                                         .arg(repo.owner, repo.name),
                                     /*error=*/false);
                    } else if (!quiet) {
                        flashMessage(
                            (repo.previewOnly ? QStringLiteral("Preview failed for ")
                                              : QStringLiteral("Sync failed for ")) +
                                repo.owner + "/" + repo.name +
                                (errors.isEmpty() ? QString() :
                                                    ": " + errors.right(160)),
                            /*error=*/true);
                    }
                }
            });
    connect(process, &QProcess::errorOccurred, this,
            [this, process, index, quiet] {
                process->deleteLater();
                m_syncingRepos.remove(index);
                refreshRepositoryList();
                if (!quiet)
                    flashMessage(
                        "Could not run git. Install Git and try again.",
                        /*error=*/true);
            });
    process->start("git", args);
}

// ------------------------------------------------------------------ settings

void MainWindow::onProfileNameChanged(const QString &name)
{
    const QString trimmed = accountNameFromInput(name, QString());
    if (trimmed.isEmpty()) {
        if (m_settingsNameEdit)
            m_settingsNameEdit->setText(m_userName);
        return;
    }
    if (m_settingsNameEdit && m_settingsNameEdit->text() != trimmed)
        m_settingsNameEdit->setText(trimmed);
    if (m_nameEdit && m_nameEdit->text() != trimmed)
        m_nameEdit->setText(trimmed);
    if (trimmed == m_userName)
        return;
    const QString oldOwner =
        accountNameFromInput(m_accountName.isEmpty() ? m_userName : m_accountName,
                             QStringLiteral("owner"));
    m_userName = trimmed;
    m_accountName = trimmed;
    saveProfileName(trimmed);
    migrateReposForProfileName(oldOwner, trimmed);
    if (m_backend)
        m_backend->setUserName(trimmed);
    refreshRepositoryList();
    logSystem("Name changed to " + trimmed + ".");
}

void MainWindow::onAvatarChosen(const QByteArray &pngData)
{
    m_userAvatar = pngData;
    QSettings().setValue(kAvatarSetting, pngData);
    if (m_backend)
        m_backend->setAvatar(pngData);
    updateAvatarButton();
}

void MainWindow::logout()
{
    // Drop the signed-in account (admin/heartbeat state) so the user can log
    // back in, then tear the session down to the setup screen.
    if (m_heartbeatTimer)
        m_heartbeatTimer->stop();
    if (m_adminPollTimer)
        m_adminPollTimer->stop();
    m_accountAuthenticated = false;
    m_accountTier = QStringLiteral("free");
    m_accountSolanaVerified = false;
    m_isAdmin = false;
    m_seenPendingUsers.clear();
    m_accountName.clear();
    QSettings().remove(kAccountNameSetting);
    leaveSession();
}

// ---- Actions (CI on push to the mirror) -----------------------------------

namespace {

QString actionStatusText(const QString &status)
{
    if (status == ActionStatus::AwaitingApproval) return QStringLiteral("Awaiting approval");
    if (status == ActionStatus::Queued) return QStringLiteral("Queued");
    if (status == ActionStatus::Running) return QStringLiteral("Running");
    if (status == ActionStatus::Success) return QStringLiteral("Success");
    if (status == ActionStatus::Failed) return QStringLiteral("Failed");
    if (status == ActionStatus::Rejected) return QStringLiteral("Rejected");
    return status;
}

QColor actionStatusColor(const QString &status)
{
    if (status == ActionStatus::Success) return QColor("#3fb950");
    if (status == ActionStatus::Failed) return QColor("#f85149");
    if (status == ActionStatus::Running) return QColor("#58a6ff");
    if (status == ActionStatus::AwaitingApproval) return QColor("#d29922");
    if (status == ActionStatus::Rejected) return QColor("#8b949e");
    return QColor("#8b949e");
}

} // namespace

void MainWindow::initActions()
{
    const QString root =
        QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) +
        QStringLiteral("/actions");
    m_actionStore = new ActionStore(root);
    m_actionRunner = new ActionRunner(m_actionStore, this);
    connect(m_actionRunner, &ActionRunner::logLine, this, &MainWindow::onRunLog);
    connect(m_actionRunner, &ActionRunner::statusChanged, this,
            &MainWindow::onRunStatusChanged);
    connect(m_actionRunner, &ActionRunner::finished, this,
            &MainWindow::onRunFinished);

    m_actionRuns = m_actionStore->loadAllRuns();
    // A run still marked Running was interrupted by a previous shutdown; it can't
    // resume, so record it as failed. Re-queue anything that was only queued.
    for (int i = 0; i < m_actionRuns.size(); ++i) {
        ActionRun &run = m_actionRuns[i];
        if (run.status == ActionStatus::Running) {
            run.status = ActionStatus::Failed;
            m_actionStore->saveRun(run);
        } else if (run.status == ActionStatus::Queued) {
            m_actionQueue.append(run.id);
        }
    }

    installAllPushHooks();

    m_actionSpoolWatcher = new QFileSystemWatcher(this);
    m_actionSpoolWatcher->addPath(m_actionStore->spoolDir());
    connect(m_actionSpoolWatcher, &QFileSystemWatcher::directoryChanged, this,
            [this](const QString &) { scanActionSpool(); });

    // Fallback poll: QFileSystemWatcher can miss rapid create+rename events, so
    // also sweep the spool on a short interval. The watcher keeps it snappy; the
    // poll guarantees a push is never silently dropped.
    auto *poll = new QTimer(this);
    poll->setInterval(4000);
    connect(poll, &QTimer::timeout, this, &MainWindow::scanActionSpool);
    poll->start();

    // Catch pushes that landed while we were closed, then drain the queue.
    scanActionSpool();
    processActionQueue();
    refreshActionsTable();
    updateNotificationButton();
}

void MainWindow::ensurePushHook(const RepositoryRecord &repo) const
{
    if (repo.previewOnly)
        return;
    if (!m_actionStore || repo.mirrorPath.isEmpty())
        return;
    if (!QDir(repo.mirrorPath).exists())
        return; // mirror not cloned yet; installed on the next sync
    const QString hooksDir = repo.mirrorPath + QStringLiteral("/hooks");
    QDir().mkpath(hooksDir);

    // A small POSIX-sh post-receive hook: it appends one event file per push to
    // the spool dir (atomically via a .tmp rename) for the app to pick up.
    const QString spool = m_actionStore->spoolDir();
    const QString script = QStringLiteral(
        "#!/bin/sh\n"
        "spool='%1'\n"
        "mkdir -p \"$spool\"\n"
        "f=\"$spool/$(date +%s)-$$.push\"\n"
        "{\n"
        "  echo 'owner %2'\n"
        "  echo 'name %3'\n"
        "  echo 'mirror %4'\n"
        "  while read old new ref; do echo \"ref $old $new $ref\"; done\n"
        "} > \"$f.tmp\" && mv \"$f.tmp\" \"$f\"\n")
        .arg(spool, repo.owner, repo.name, repo.mirrorPath);

    QFile f(hooksDir + QStringLiteral("/post-receive"));
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return;
    f.write(script.toUtf8());
    f.close();
    f.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner |
                     QFileDevice::ExeOwner | QFileDevice::ReadGroup |
                     QFileDevice::ExeGroup | QFileDevice::ReadOther |
                     QFileDevice::ExeOther);
}

void MainWindow::removePushHook(const RepositoryRecord &repo) const
{
    if (repo.mirrorPath.isEmpty())
        return;
    QFile::remove(repo.mirrorPath + QStringLiteral("/hooks/post-receive"));
}

void MainWindow::installAllPushHooks() const
{
    // Install the hook on every mirror, not just actions-enabled ones: it only
    // writes a spool event, which we also use to refresh the open Code view in
    // real time. Workflow execution is still gated on actionsEnabled.
    for (const RepositoryRecord &repo : m_repositories)
        if (!repo.previewOnly)
            ensurePushHook(repo);
}

int MainWindow::repoIndexFor(const QString &owner, const QString &name) const
{
    for (int i = 0; i < m_repositories.size(); ++i)
        if (!m_repositories.at(i).previewOnly &&
            m_repositories.at(i).owner == owner &&
            m_repositories.at(i).name == name)
            return i;
    return -1;
}

ActionRun *MainWindow::findRun(int runId)
{
    for (ActionRun &run : m_actionRuns)
        if (run.id == runId)
            return &run;
    return nullptr;
}

void MainWindow::scanActionSpool()
{
    if (!m_actionStore)
        return;
    QDir dir(m_actionStore->spoolDir());
    const QStringList files =
        dir.entryList({QStringLiteral("*.push")}, QDir::Files, QDir::Name);
    for (const QString &file : files) {
        const QString full = dir.filePath(file);
        QFile f(full);
        if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
            continue;
        const QString text = QString::fromUtf8(f.readAll());
        f.close();
        QFile::remove(full);

        QString owner, name, ref, commit;
        const QStringList lines = text.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
        for (const QString &line : lines) {
            if (line.startsWith(QLatin1String("owner ")))
                owner = line.mid(6).trimmed();
            else if (line.startsWith(QLatin1String("name ")))
                name = line.mid(5).trimmed();
            else if (line.startsWith(QLatin1String("ref "))) {
                const QStringList p =
                    line.mid(4).split(QLatin1Char(' '), Qt::SkipEmptyParts);
                if (p.size() >= 3 &&
                    p.at(2).startsWith(QLatin1String("refs/heads/"))) {
                    commit = p.at(1);
                    ref = p.at(2);
                }
            }
        }
        // Skip events with no branch update or a branch deletion (all-zero SHA).
        if (owner.isEmpty() || name.isEmpty() || commit.isEmpty())
            continue;
        if (commit.count(QLatin1Char('0')) == commit.size())
            continue;
        enqueuePushEvent(owner, name, commit, ref);
    }
    processActionQueue();
}

void MainWindow::enqueuePushEvent(const QString &owner, const QString &name,
                                  const QString &commit, const QString &ref)
{
    const int repoIndex = repoIndexFor(owner, name);
    if (repoIndex < 0)
        return;
    const RepositoryRecord repo = m_repositories.at(repoIndex);

    // Short branch name + the pushed commit's subject, for the log and alert.
    const QString branch = ref.startsWith(QLatin1String("refs/heads/"))
                               ? ref.mid(11)
                               : ref;
    QString subject;
    {
        QProcess s;
        s.start(QStringLiteral("git"),
                {QStringLiteral("-C"), repo.mirrorPath, QStringLiteral("show"),
                 QStringLiteral("-s"), QStringLiteral("--format=%s"), commit});
        s.waitForFinished(5000);
        subject = QString::fromUtf8(s.readAllStandardOutput()).trimmed();
    }

    // Always note the push in the network log.
    logSystem(QString::fromUtf8("Push to %1/%2 on %3 \xE2\x86\x92 %4%5")
                  .arg(owner, name, branch, commit.left(8),
                       subject.isEmpty()
                           ? QString()
                           : QString::fromUtf8(" \xE2\x80\x94 ") + subject));

    // Optional desktop alert with the push details (on by default).
    if (QSettings().value(kPushAlertSetting, true).toBool()) {
        const QString body =
            QString::fromUtf8("%1/%2 \xC2\xB7 %3 \xC2\xB7 %4%5")
                .arg(owner, name, branch, commit.left(8),
                     subject.isEmpty() ? QString()
                                       : QStringLiteral("\n") + subject);
        postNotification(QStringLiteral("Push received"), body,
                         false, QStringLiteral("emblem-synchronizing"));
    }

    // Live refresh: if this repo's detail view is open, reflect the new commit
    // immediately (works for every mirror, whether or not actions are enabled).
    if (repoIndex == m_repoDetailIndex)
        refreshOpenRepoDetail();

    if (!repo.actionsEnabled)
        return; // push detection only; no workflow execution for this repo

    // List .forkmesh/*.yml|*.yaml at the pushed commit without checking it out.
    QProcess ls;
    ls.start(QStringLiteral("git"),
             {QStringLiteral("-C"), repo.mirrorPath, QStringLiteral("ls-tree"),
              QStringLiteral("-r"), QStringLiteral("--name-only"), commit,
              QStringLiteral("--"), QStringLiteral(".forkmesh")});
    ls.waitForFinished(10000);
    const QStringList paths = QString::fromUtf8(ls.readAllStandardOutput())
                                  .split(QLatin1Char('\n'), Qt::SkipEmptyParts);

    bool added = false;
    for (const QString &path : paths) {
        if (!(path.endsWith(QLatin1String(".yml")) ||
              path.endsWith(QLatin1String(".yaml"))))
            continue;
        QProcess show;
        show.start(QStringLiteral("git"),
                   {QStringLiteral("-C"), repo.mirrorPath, QStringLiteral("show"),
                    commit + QLatin1Char(':') + path});
        show.waitForFinished(10000);
        if (show.exitCode() != 0)
            continue;
        const QString content = QString::fromUtf8(show.readAllStandardOutput());
        const ActionWorkflow wf = ActionFile::parse(path, content);
        if (!wf.valid || !wf.triggersOnPush())
            continue;

        ActionRun run;
        run.owner = owner;
        run.name = name;
        run.workflowPath = path;
        run.workflowName = wf.name;
        run.workflowContent = content;
        run.commit = commit;
        run.ref = ref;
        const bool approved =
            ActionStore::isApproved(run.repoKey(), path, content);
        run.status =
            approved ? ActionStatus::Queued : ActionStatus::AwaitingApproval;

        const ActionRun created = m_actionStore->createRun(run);
        m_actionRuns.prepend(created);
        if (approved)
            m_actionQueue.append(created.id);
        else
            addNotification(QStringLiteral("Action waiting for approval"),
                            QString::fromUtf8("%1 \xC2\xB7 %2/%3 at %4")
                                .arg(wf.name, owner, name, commit.left(8)),
                            false, created.id);
        added = true;
        logSystem(QStringLiteral("Actions: %1 \"%2\" for %3/%4 @ %5")
                      .arg(approved ? QStringLiteral("queued")
                                    : QStringLiteral("awaiting approval of"),
                           wf.name, owner, name, commit.left(8)));
    }
    if (added) {
        // Make the new run(s) visible immediately if this repo's Actions tab is
        // the one on screen.
        refreshActionsTable();
        if (m_actionWorkflowList && repoIndex == m_repoDetailIndex)
            refreshRepoActions();
        updateNotificationButton();
    } else {
        logSystem(QStringLiteral(
                      "Actions: no .forkmesh/ workflow with 'on: push' at %1 for "
                      "%2/%3 \xE2\x80\x94 nothing to run.")
                      .arg(commit.left(8), owner, name));
    }
}

void MainWindow::refreshOpenRepoDetail()
{
    if (m_repoDetailIndex < 0 || m_repoDetailIndex >= m_repositories.size())
        return;
    // Re-read the branch tip, commit list, About sidebar and the current file
    // view so a freshly pushed commit shows without reopening the repo.
    loadBranchesAndTags();
    loadCommits();
    reloadAgents();
    loadAboutSidebar();
    if (m_insightsSummary)
        loadRepoInsights();
    updateRepoDetailStatus();
    updateRepoActionMenus();
    updateRepoCodeSize();
    updateRepoPushButton();
    m_treeLoadedForIndex = -1; // force the explorer tree to rebuild on next use
    loadRepoOverview(m_overviewPath);
}

void MainWindow::processActionQueue()
{
    if (!m_actionRunner || m_actionRunner->busy())
        return;
    while (!m_actionQueue.isEmpty()) {
        const int runId = m_actionQueue.takeFirst();
        ActionRun *run = findRun(runId);
        if (!run || run->status != ActionStatus::Queued)
            continue;
        const int repoIndex = repoIndexFor(run->owner, run->name);
        if (repoIndex < 0) {
            run->status = ActionStatus::Failed;
            m_actionStore->saveRun(*run);
            continue;
        }
        const QString mirror = m_repositories.at(repoIndex).mirrorPath;
        const ActionWorkflow wf =
            ActionFile::parse(run->workflowPath, run->workflowContent);
        if (!wf.valid) {
            run->status = ActionStatus::Failed;
            m_actionStore->saveRun(*run);
            continue;
        }
        // start() emits statusChanged synchronously (which reloads m_actionRuns),
        // so copy the run out first and don't touch the pointer afterwards.
        const ActionRun snapshot = *run;
        m_actionRunner->start(snapshot, wf, mirror, ActionStore::variables());
        return; // one run at a time; finished() drives the next
    }
}

void MainWindow::onRunLog(int runId, const QString &text)
{
    if (runId != m_selectedRunId || !m_actionLog)
        return;
    m_actionLog->moveCursor(QTextCursor::End);
    m_actionLog->insertPlainText(text);
    m_actionLog->moveCursor(QTextCursor::End);
}

void MainWindow::onRunStatusChanged(int runId, const QString &status)
{
    m_actionRuns = m_actionStore->loadAllRuns();
    refreshActionsTable();
    refreshCommitStatusGlyphs();
    if (runId == m_selectedRunId && m_actionRunMeta) {
        if (const ActionRun *run = findRun(runId)) {
            m_actionRunMeta->setText(
                QString::fromUtf8("%1/%2 \xC2\xB7 %3 \xC2\xB7 %4")
                    .arg(run->owner, run->name, run->commit.left(8),
                         actionStatusText(status)));
        }
    }
    updateNotificationButton();
    if (status == ActionStatus::Running) {
        if (const ActionRun *run = findRun(runId))
            notifyActionEvent(QStringLiteral("Action started"),
                              QString::fromUtf8("%1 \xC2\xB7 %2/%3")
                                  .arg(run->workflowName, run->owner, run->name),
                              false);
    }
}

void MainWindow::onRunFinished(int runId, bool ok)
{
    m_actionRuns = m_actionStore->loadAllRuns();
    refreshActionsTable();
    refreshCommitStatusGlyphs();
    updateNotificationButton();
    if (const ActionRun *run = findRun(runId))
        notifyActionEvent(ok ? QStringLiteral("Action succeeded")
                             : QStringLiteral("Action failed"),
                          QString::fromUtf8("%1 \xC2\xB7 %2/%3")
                              .arg(run->workflowName, run->owner, run->name),
                          !ok);
    if (runId == m_selectedRunId)
        showRun(runId); // finished: reload the complete log from disk
    processActionQueue();
}

void MainWindow::notifyActionEvent(const QString &title, const QString &body,
                                   bool warning)
{
    addNotification(title, body, warning);
    if (!QSettings().value(kActionAlertSetting, true).toBool())
        return;
    const QString icon = warning ? QStringLiteral("dialog-error")
                         : title.contains("started")
                             ? QStringLiteral("system-run")
                             : QStringLiteral("emblem-default");
    postNotification(title, body, warning, icon);
}

void MainWindow::addNotification(const QString &title, const QString &body,
                                 bool warning, int runId)
{
    AppNotification item;
    item.title = title;
    item.body = body;
    item.warning = warning;
    item.runId = runId;
    item.timestampMs = QDateTime::currentMSecsSinceEpoch();
    m_notifications.prepend(item);
    while (m_notifications.size() > 100)
        m_notifications.removeLast();
    updateNotificationButton();
}

int MainWindow::pendingActionCount() const
{
    int count = 0;
    for (const ActionRun &run : m_actionRuns)
        if (run.status == ActionStatus::AwaitingApproval)
            ++count;
    return count;
}

void MainWindow::updateNotificationButton()
{
    if (!m_notificationButton)
        return;
    const int pending = pendingActionCount();
    m_notificationButton->setText(pending > 0
                                      ? QStringLiteral("Notifications •")
                                      : QStringLiteral("Notifications"));
    m_notificationButton->setToolTip(
        pending > 0
            ? QStringLiteral("%1 action(s) waiting for approval").arg(pending)
            : QStringLiteral("Notifications"));
    m_notificationButton->setObjectName(pending > 0
                                            ? QStringLiteral("notificationButtonAlert")
                                            : QStringLiteral("notificationButton"));
    m_notificationButton->style()->unpolish(m_notificationButton);
    m_notificationButton->style()->polish(m_notificationButton);
}

void MainWindow::openActionRunFromNotification(int runId)
{
    const ActionRun *run = findRun(runId);
    if (!run)
        return;
    const int index = repoIndexFor(run->owner, run->name);
    if (index < 0)
        return;
    openRepoDetail(index);
    if (m_repoDetailTabs && m_repoDetailTabs->button(5))
        m_repoDetailTabs->button(5)->setChecked(true);
    if (m_repoDetailStack)
        m_repoDetailStack->setCurrentIndex(5);
    refreshRepoActions();
    if (m_actionsTable) {
        for (int row = 0; row < m_actionsTable->rowCount(); ++row) {
            QTableWidgetItem *item = m_actionsTable->item(row, 0);
            if (item && item->data(Qt::UserRole).toInt() == runId) {
                m_actionsTable->selectRow(row);
                break;
            }
        }
    }
    showRun(runId);
}

void MainWindow::showNotifications()
{
    QDialog dialog(this);
    dialog.setWindowTitle("Notifications");
    dialog.resize(520, 420);
    auto *layout = new QVBoxLayout(&dialog);
    auto *list = new QListWidget;
    list->setSelectionMode(QAbstractItemView::SingleSelection);
    layout->addWidget(list, 1);

    bool hasRows = false;
    for (const ActionRun &run : std::as_const(m_actionRuns)) {
        if (run.status != ActionStatus::AwaitingApproval)
            continue;
        auto *item = new QListWidgetItem(
            QStringLiteral("Action waiting: %1\n%2/%3 at %4")
                .arg(run.workflowName, run.owner, run.name, run.commit.left(8)));
        item->setData(Qt::UserRole, run.id);
        list->addItem(item);
        hasRows = true;
    }
    for (const AppNotification &notice : std::as_const(m_notifications)) {
        const QString when = formatRepoDate(notice.timestampMs);
        auto *item = new QListWidgetItem(
            notice.title + QStringLiteral("\n") + notice.body +
            QStringLiteral("\n") + when);
        item->setData(Qt::UserRole, notice.runId);
        if (notice.warning)
            item->setForeground(QColor("#f85149"));
        list->addItem(item);
        hasRows = true;
    }
    if (!hasRows) {
        auto *empty = new QListWidgetItem("No notifications yet.");
        empty->setFlags(Qt::NoItemFlags);
        list->addItem(empty);
    }

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close);
    QPushButton *openButton = buttons->addButton("Open", QDialogButtonBox::ActionRole);
    openButton->setEnabled(false);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    connect(list, &QListWidget::currentItemChanged, this,
            [openButton](QListWidgetItem *item, QListWidgetItem *) {
                openButton->setEnabled(item && item->data(Qt::UserRole).toInt() > 0);
            });
    auto openSelected = [this, list, &dialog] {
        QListWidgetItem *item = list->currentItem();
        if (!item)
            return;
        const int runId = item->data(Qt::UserRole).toInt();
        if (runId <= 0)
            return;
        dialog.accept();
        openActionRunFromNotification(runId);
    };
    connect(openButton, &QPushButton::clicked, this, openSelected);
    connect(list, &QListWidget::itemDoubleClicked, this,
            [openSelected](QListWidgetItem *) { openSelected(); });
    dialog.exec();
}

void MainWindow::postNotification(const QString &title, const QString &body,
                                  bool warning, const QString &icon)
{
    const QString iconName =
        !icon.isEmpty() ? icon
                        : (warning ? QStringLiteral("dialog-error")
                                   : QStringLiteral("dialog-information"));
#if defined(Q_OS_LINUX)
    // Prefer notify-send: many Linux desktops don't render the body of a
    // QSystemTrayIcon message (they fall back to just the app name), but the
    // libnotify daemon shows the summary, body and icon reliably.
    static const QString notifySend =
        QStandardPaths::findExecutable(QStringLiteral("notify-send"));
    if (!notifySend.isEmpty()) {
        const QStringList args = {
            QStringLiteral("-a"), QStringLiteral("ForkMesh"),
            QStringLiteral("-i"), iconName,
            QStringLiteral("-u"),
            warning ? QStringLiteral("critical") : QStringLiteral("normal"),
            title, body};
        if (QProcess::startDetached(notifySend, args))
            return;
    }
#endif
    if (m_trayIcon && QSystemTrayIcon::isSystemTrayAvailable())
        m_trayIcon->showMessage(
            title, body,
            warning ? QSystemTrayIcon::Warning : QSystemTrayIcon::Information,
            6000);
}

void MainWindow::refreshActionsTable()
{
    if (!m_actionsTable)
        return;
    // The table lives inside one repo's Actions tab, so only show that repo's
    // runs, optionally narrowed to the workflow selected in the left column.
    QString owner, name;
    if (m_repoDetailIndex >= 0 && m_repoDetailIndex < m_repositories.size()) {
        owner = m_repositories.at(m_repoDetailIndex).owner;
        name = m_repositories.at(m_repoDetailIndex).name;
    }

    QSignalBlocker block(m_actionsTable);
    m_actionsTable->setRowCount(0);
    for (const ActionRun &run : m_actionRuns) {
        if (run.owner != owner || run.name != name)
            continue;
        if (!m_selectedWorkflowFilter.isEmpty() &&
            run.workflowPath != m_selectedWorkflowFilter)
            continue;
        const int row = m_actionsTable->rowCount();
        m_actionsTable->insertRow(row);

        auto *wfItem = new QTableWidgetItem(run.workflowName);
        wfItem->setData(Qt::UserRole, run.id);
        auto *statusItem = new QTableWidgetItem(actionStatusText(run.status));
        statusItem->setForeground(actionStatusColor(run.status));
        const QString when =
            run.createdAtMs > 0
                ? QDateTime::fromMSecsSinceEpoch(run.createdAtMs)
                      .toString(QStringLiteral("MMM d  hh:mm"))
                : QString();
        auto *whenItem = new QTableWidgetItem(when);

        m_actionsTable->setItem(row, 0, wfItem);
        m_actionsTable->setItem(row, 1, statusItem);
        m_actionsTable->setItem(row, 2, whenItem);
        if (run.id == m_selectedRunId)
            m_actionsTable->selectRow(row);
    }
    updateActionsTabIndicator();
}

void MainWindow::showLatestVisibleActionRun()
{
    if (!m_actionsTable || m_actionsTable->rowCount() == 0) {
        showRun(-1);
        return;
    }

    int targetRow = 0;
    int targetRunId = -1;
    for (int row = 0; row < m_actionsTable->rowCount(); ++row) {
        QTableWidgetItem *item = m_actionsTable->item(row, 0);
        if (!item)
            continue;
        const int runId = item->data(Qt::UserRole).toInt();
        if (targetRunId < 0) {
            targetRow = row;
            targetRunId = runId;
        }
        const ActionRun *run = findRun(runId);
        if (run && run->status == ActionStatus::Running) {
            targetRow = row;
            targetRunId = runId;
            break;
        }
    }

    if (targetRunId < 0) {
        showRun(-1);
        return;
    }

    m_actionsTable->selectRow(targetRow);
    showRun(targetRunId);
}

int MainWindow::commitStatusCode(const QString &sha) const
{
    if (sha.isEmpty() || m_repoDetailIndex < 0 ||
        m_repoDetailIndex >= m_repositories.size())
        return 0;
    const QString owner = m_repositories.at(m_repoDetailIndex).owner;
    const QString name = m_repositories.at(m_repoDetailIndex).name;

    // Aggregate every run for this repo whose commit matches `sha` (one is a
    // prefix of the other, since the log uses short hashes and runs store full
    // SHAs). Running/queued wins, then any failure, then success.
    bool running = false, failed = false, success = false;
    for (const ActionRun &run : m_actionRuns) {
        if (run.owner != owner || run.name != name || run.commit.isEmpty())
            continue;
        if (!(run.commit.startsWith(sha) || sha.startsWith(run.commit)))
            continue;
        if (run.status == ActionStatus::Running ||
            run.status == ActionStatus::Queued ||
            run.status == ActionStatus::AwaitingApproval)
            running = true;
        else if (run.status == ActionStatus::Failed ||
                 run.status == ActionStatus::Rejected)
            failed = true;
        else if (run.status == ActionStatus::Success)
            success = true;
    }
    if (running)
        return 3;
    if (failed)
        return 2;
    if (success)
        return 1;
    return 0;
}

QString MainWindow::commitStatusGlyph(const QString &sha) const
{
    switch (commitStatusCode(sha)) {
    case 3:
        return QStringLiteral(" <span style='color:#58a6ff' "
                              "title='Checks running'>\xE2\x97\x90</span>"); // ◐
    case 2:
        return QStringLiteral(" <span style='color:#f85149' "
                              "title='Checks failed'>\xE2\x9C\x95</span>"); // ✕
    case 1:
        return QStringLiteral(" <span style='color:#3fb950' "
                              "title='Checks passed'>\xE2\x9C\x93</span>"); // ✓
    default:
        return QString();
    }
}

void MainWindow::refreshCommitStatusGlyphs()
{
    if (!m_repoDetailStack)
        return;
    switch (m_repoDetailStack->currentIndex()) {
    case 0: // Code overview: refresh the latest-commit strip
        loadRepoOverview(m_overviewPath);
        break;
    case 1: // Commits list
        loadCommits();
        break;
    default:
        break;
    }
}

void MainWindow::updateActionsTabIndicator()
{
    QAbstractButton *tab = m_repoDetailTabs ? m_repoDetailTabs->button(5) : nullptr;
    if (!tab)
        return;

    // Is any run for the currently-open repo still in flight?
    bool active = false;
    if (m_repoDetailIndex >= 0 && m_repoDetailIndex < m_repositories.size()) {
        const QString owner = m_repositories.at(m_repoDetailIndex).owner;
        const QString name = m_repositories.at(m_repoDetailIndex).name;
        for (const ActionRun &run : m_actionRuns)
            if (run.owner == owner && run.name == name &&
                (run.status == ActionStatus::Running ||
                 run.status == ActionStatus::Queued)) {
                active = true;
                break;
            }
    }

    if (!active) {
        if (m_actionsSpinTimer)
            m_actionsSpinTimer->stop();
        const int workflows = m_actionWorkflowList
                                  ? qMax(0, m_actionWorkflowList->count() - 1)
                                  : 0;
        tab->setText(QStringLiteral("Actions (%1)").arg(workflows));
        return;
    }

    if (!m_actionsSpinTimer) {
        m_actionsSpinTimer = new QTimer(this);
        connect(m_actionsSpinTimer, &QTimer::timeout, this, [this] {
            QAbstractButton *t = m_repoDetailTabs ? m_repoDetailTabs->button(5) : nullptr;
            if (!t)
                return;
            static const char *frames[] = {"\xE2\xA0\x8B", "\xE2\xA0\x99",
                                           "\xE2\xA0\xB9", "\xE2\xA0\xB8",
                                           "\xE2\xA0\xBC", "\xE2\xA0\xB4",
                                           "\xE2\xA0\xA6", "\xE2\xA0\xA7",
                                           "\xE2\xA0\x87", "\xE2\xA0\x8F"};
            m_actionsSpinFrame = (m_actionsSpinFrame + 1) % 10;
            const int wfCount = m_actionWorkflowList
                                    ? qMax(0, m_actionWorkflowList->count() - 1)
                                    : 0;
            t->setText(QStringLiteral("Actions (%1) ").arg(wfCount) +
                       QString::fromUtf8(frames[m_actionsSpinFrame]));
        });
    }
    if (!m_actionsSpinTimer->isActive())
        m_actionsSpinTimer->start(110);
}

void MainWindow::updateAgentsTabIndicator()
{
    if (!m_repoAgentsTab)
        return;

    // Count agent sessions for the currently-open repo.
    QString owner, name;
    if (m_repoDetailIndex >= 0 && m_repoDetailIndex < m_repositories.size()) {
        owner = m_repositories.at(m_repoDetailIndex).owner;
        name  = m_repositories.at(m_repoDetailIndex).name;
    }
    int n = 0;
    for (const AgentSession &s : std::as_const(m_agentSessions))
        if (s.owner == owner && s.name == name)
            ++n;

    // Is any agent running right now?
    bool active = anyAgentRunning();

    if (!active) {
        if (m_agentsSpinTimer)
            m_agentsSpinTimer->stop();
        m_repoAgentsTab->setText(QStringLiteral("Agents (%1)").arg(n));
        return;
    }

    if (!m_agentsSpinTimer) {
        m_agentsSpinTimer = new QTimer(this);
        connect(m_agentsSpinTimer, &QTimer::timeout, this, [this] {
            if (!m_repoAgentsTab)
                return;
            static const char *frames[] = {"\xE2\xA0\x8B", "\xE2\xA0\x99",
                                           "\xE2\xA0\xB9", "\xE2\xA0\xB8",
                                           "\xE2\xA0\xBC", "\xE2\xA0\xB4",
                                           "\xE2\xA0\xA6", "\xE2\xA0\xA7",
                                           "\xE2\xA0\x87", "\xE2\xA0\x8F"};
            m_agentsSpinFrame = (m_agentsSpinFrame + 1) % 10;
            QString owner2, name2;
            if (m_repoDetailIndex >= 0 && m_repoDetailIndex < m_repositories.size()) {
                owner2 = m_repositories.at(m_repoDetailIndex).owner;
                name2  = m_repositories.at(m_repoDetailIndex).name;
            }
            int cnt = 0;
            for (const AgentSession &s : std::as_const(m_agentSessions))
                if (s.owner == owner2 && s.name == name2)
                    ++cnt;
            m_repoAgentsTab->setText(QStringLiteral("Agents (%1) ").arg(cnt) +
                                     QString::fromUtf8(frames[m_agentsSpinFrame]));
        });
    }
    if (!m_agentsSpinTimer->isActive())
        m_agentsSpinTimer->start(110);
}

QList<ActionWorkflow>
MainWindow::availableWorkflowsForRepo(const RepositoryRecord &repo) const
{
    QList<ActionWorkflow> out;
    // Prefer reading the bare mirror's default branch (HEAD); fall back to a
    // local working tree if one is configured.
    if (!repo.mirrorPath.isEmpty() && QDir(repo.mirrorPath).exists()) {
        QProcess ls;
        ls.start(QStringLiteral("git"),
                 {QStringLiteral("-C"), repo.mirrorPath, QStringLiteral("ls-tree"),
                  QStringLiteral("-r"), QStringLiteral("--name-only"),
                  QStringLiteral("HEAD"), QStringLiteral("--"),
                  QStringLiteral(".forkmesh")});
        ls.waitForFinished(8000);
        const QStringList paths = QString::fromUtf8(ls.readAllStandardOutput())
                                      .split(QLatin1Char('\n'), Qt::SkipEmptyParts);
        for (const QString &path : paths) {
            if (!(path.endsWith(QLatin1String(".yml")) ||
                  path.endsWith(QLatin1String(".yaml"))))
                continue;
            QProcess show;
            show.start(QStringLiteral("git"),
                       {QStringLiteral("-C"), repo.mirrorPath,
                        QStringLiteral("show"), QStringLiteral("HEAD:") + path});
            show.waitForFinished(8000);
            if (show.exitCode() != 0)
                continue;
            out.append(ActionFile::parse(
                path, QString::fromUtf8(show.readAllStandardOutput())));
        }
    }
    if (out.isEmpty() && !repo.localPath.isEmpty())
        out = ActionFile::parseWorkflowsInDir(repo.localPath);
    return out;
}

void MainWindow::refreshRepoActions()
{
    if (!m_actionWorkflowList)
        return;
    QSignalBlocker block(m_actionWorkflowList);
    m_actionWorkflowList->clear();

    if (m_repoDetailIndex < 0 || m_repoDetailIndex >= m_repositories.size()) {
        if (m_repoActionsTab)
            m_repoActionsTab->setText(QStringLiteral("Actions (0)"));
        refreshActionsTable();
        return;
    }
    const RepositoryRecord &repo = m_repositories.at(m_repoDetailIndex);

    if (m_actionsEnabledCheck) {
        QSignalBlocker block(m_actionsEnabledCheck);
        m_actionsEnabledCheck->setChecked(repo.actionsEnabled);
    }

    auto *all = new QListWidgetItem(QStringLiteral("All workflows"));
    all->setData(Qt::UserRole, QString());
    m_actionWorkflowList->addItem(all);
    all->setSelected(true);

    const QList<ActionWorkflow> wfs = availableWorkflowsForRepo(repo);
    for (const ActionWorkflow &wf : wfs) {
        auto *item =
            new QListWidgetItem(wf.name);
        item->setData(Qt::UserRole, wf.path);
        item->setToolTip(wf.valid ? wf.path +
                                        (wf.triggersOnPush()
                                             ? QStringLiteral("  (on: push)")
                                             : QString())
                                  : wf.path + QStringLiteral("  — ") + wf.error);
        m_actionWorkflowList->addItem(item);
    }
    if (wfs.isEmpty()) {
        auto *none = new QListWidgetItem(
            repo.actionsEnabled
                ? QStringLiteral("No workflows in .forkmesh/")
                : QStringLiteral("No workflows in .forkmesh/ (actions disabled)"));
        none->setFlags(Qt::NoItemFlags);
        m_actionWorkflowList->addItem(none);
    }

    m_selectedWorkflowFilter.clear();
    refreshActionsTable();
    showLatestVisibleActionRun();
    if (m_repoActionsTab)
        m_repoActionsTab->setText(QStringLiteral("Actions (%1)")
                                      .arg(qMax(0, m_actionWorkflowList->count() - 1)));
}

void MainWindow::showRun(int runId)
{
    m_selectedRunId = runId;
    const ActionRun *run = findRun(runId);
    if (!run) {
        if (m_actionRunTitle)
            m_actionRunTitle->setText(QStringLiteral("Select a run"));
        if (m_actionRunMeta)
            m_actionRunMeta->clear();
        if (m_actionLog)
            m_actionLog->clear();
        if (m_actionApprovalBar)
            m_actionApprovalBar->hide();
        if (m_actionApprovalBanner)
            m_actionApprovalBanner->hide();
        if (m_actionDiff)
            m_actionDiff->hide();
        return;
    }

    if (m_actionRunTitle)
        m_actionRunTitle->setText(run->workflowName);
    if (m_actionRunMeta) {
        QString meta = QString::fromUtf8("%1/%2 \xC2\xB7 %3 \xC2\xB7 %4")
                           .arg(run->owner, run->name, run->commit.left(8),
                                actionStatusText(run->status));
        if (run->startedAtMs > 0 && run->finishedAtMs > run->startedAtMs)
            meta += QString::fromUtf8(" \xC2\xB7 %1s")
                        .arg((run->finishedAtMs - run->startedAtMs) / 1000);
        m_actionRunMeta->setText(meta);
    }

    const bool pending = run->status == ActionStatus::AwaitingApproval;
    if (m_actionApprovalBanner)
        m_actionApprovalBanner->setVisible(pending);
    if (m_actionApprovalBar)
        m_actionApprovalBar->setVisible(pending);
    if (m_actionDiff)
        m_actionDiff->setVisible(pending);
    if (m_actionLog)
        m_actionLog->setVisible(!pending);

    if (pending && m_actionDiff) {
        const QString prior =
            ActionStore::lastApprovedContent(run->repoKey(), run->workflowPath);
        QString body;
        body += QStringLiteral("# Previously approved (%1)\n").arg(run->workflowPath);
        body += prior.isEmpty()
                    ? QStringLiteral("(none — this workflow has never been approved)\n")
                    : prior;
        body += QStringLiteral("\n\n# Incoming from this push (%1)\n")
                    .arg(run->commit.left(8));
        body += run->workflowContent;
        m_actionDiff->setPlainText(body);
    } else if (m_actionLog) {
        m_actionLog->setPlainText(m_actionStore->readLog(*run));
        m_actionLog->moveCursor(QTextCursor::End);
    }
}

void MainWindow::approveSelectedRun()
{
    ActionRun *run = findRun(m_selectedRunId);
    if (!run || run->status != ActionStatus::AwaitingApproval)
        return;
    ActionStore::approve(run->repoKey(), run->workflowPath, run->workflowContent);
    run->status = ActionStatus::Queued;
    m_actionStore->saveRun(*run);
    m_actionQueue.append(run->id);
    logSystem(QStringLiteral("Actions: approved \"%1\" for %2/%3.")
                  .arg(run->workflowName, run->owner, run->name));
    m_actionRuns = m_actionStore->loadAllRuns();
    refreshActionsTable();
    showRun(m_selectedRunId);
    updateNotificationButton();
    processActionQueue();
}

void MainWindow::rejectSelectedRun()
{
    ActionRun *run = findRun(m_selectedRunId);
    if (!run || run->status != ActionStatus::AwaitingApproval)
        return;
    run->status = ActionStatus::Rejected;
    run->finishedAtMs = QDateTime::currentMSecsSinceEpoch();
    m_actionStore->saveRun(*run);
    logSystem(QStringLiteral("Actions: rejected \"%1\" for %2/%3.")
                  .arg(run->workflowName, run->owner, run->name));
    m_actionRuns = m_actionStore->loadAllRuns();
    refreshActionsTable();
    showRun(m_selectedRunId);
    updateNotificationButton();
}

QWidget *MainWindow::buildRepoActionsTab()
{
    auto *page = new QWidget;

    // Far left: the actions available in this repo (.forkmesh/ workflows).
    auto *wfPane = new QWidget;
    wfPane->setMinimumWidth(180);
    wfPane->setMaximumWidth(260);
    auto *wfHeading = new QLabel("Workflows");
    wfHeading->setObjectName("sectionLabel");
    auto *wfHint = new QLabel(
        "Actions defined in .forkmesh/. They run when a fork pushes to this "
        "repo's mirror.");
    wfHint->setObjectName("statusLine");
    wfHint->setWordWrap(true);
    m_actionWorkflowList = new QListWidget;
    m_actionWorkflowList->setObjectName("actionWorkflowList");
    connect(m_actionWorkflowList, &QListWidget::currentItemChanged, this,
            [this](QListWidgetItem *item, QListWidgetItem *) {
                m_selectedWorkflowFilter =
                    item ? item->data(Qt::UserRole).toString() : QString();
                refreshActionsTable();
                showLatestVisibleActionRun();
            });

    // Enable/disable actions for this repo, right here on the Actions tab.
    m_actionsEnabledCheck = new QCheckBox("Run actions on push");
    m_actionsEnabledCheck->setToolTip(
        "When a fork pushes to this repo's local mirror, run its .forkmesh/ "
        "workflows. Changed workflows still require approval below before they "
        "run.");
    connect(m_actionsEnabledCheck, &QCheckBox::toggled, this, [this](bool on) {
        if (m_repoDetailIndex < 0 || m_repoDetailIndex >= m_repositories.size())
            return;
        if (m_repositories[m_repoDetailIndex].actionsEnabled == on)
            return;
        m_repositories[m_repoDetailIndex].actionsEnabled = on;
        saveRepositories();
        // The hook stays installed regardless (it powers the live Code refresh);
        // just make sure it exists when enabling.
        ensurePushHook(m_repositories.at(m_repoDetailIndex));
        logSystem(QStringLiteral("Actions %1 for %2/%3.")
                      .arg(on ? "enabled" : "disabled",
                           m_repositories.at(m_repoDetailIndex).owner,
                           m_repositories.at(m_repoDetailIndex).name));
    });

    auto *wfLayout = new QVBoxLayout(wfPane);
    wfLayout->setContentsMargins(16, 22, 8, 22);
    wfLayout->setSpacing(8);
    wfLayout->addWidget(wfHeading);
    wfLayout->addWidget(wfHint);
    wfLayout->addWidget(m_actionsEnabledCheck);
    wfLayout->addWidget(m_actionWorkflowList, 1);

    // Middle: the run list for the selected workflow (or all).
    auto *listPane = new QWidget;
    listPane->setMinimumWidth(300);
    auto *heading = new QLabel("Runs");
    heading->setObjectName("channelTitle");
    auto *subtitle = new QLabel(
        "Changed workflows wait for your approval before they run.");
    subtitle->setObjectName("statusLine");
    subtitle->setWordWrap(true);

    m_actionsTable = new QTableWidget(0, 3);
    m_actionsTable->setHorizontalHeaderLabels({"Workflow", "Status", "When"});
    m_actionsTable->horizontalHeader()->setStretchLastSection(true);
    m_actionsTable->verticalHeader()->setVisible(false);
    m_actionsTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_actionsTable->setSelectionMode(QAbstractItemView::SingleSelection);
    m_actionsTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    connect(m_actionsTable, &QTableWidget::itemSelectionChanged, this, [this] {
        const QModelIndexList rows =
            m_actionsTable->selectionModel()->selectedRows();
        if (rows.isEmpty())
            return;
        QTableWidgetItem *first = m_actionsTable->item(rows.first().row(), 0);
        if (first)
            showRun(first->data(Qt::UserRole).toInt());
    });

    auto *listLayout = new QVBoxLayout(listPane);
    listLayout->setContentsMargins(12, 22, 12, 22);
    listLayout->setSpacing(8);
    listLayout->addWidget(heading);
    listLayout->addWidget(subtitle);
    listLayout->addWidget(m_actionsTable, 1);

    // Right: run detail (header, optional approval, log).
    auto *detailPane = new QWidget;
    m_actionRunTitle = new QLabel("Select a run");
    m_actionRunTitle->setObjectName("channelTitle");
    m_actionRunMeta = new QLabel;
    m_actionRunMeta->setObjectName("statusLine");
    m_actionRunMeta->setTextInteractionFlags(Qt::TextSelectableByMouse);

    m_actionApprovalBanner = new QLabel(
        "This workflow is new or changed. Review the difference "
        "below, then Approve to run it (secrets are only exposed after approval).");
    m_actionApprovalBanner->setWordWrap(true);
    m_actionApprovalBanner->setStyleSheet(
        "color:#d29922; background:#1c1908; border:1px solid #3a3416; "
        "border-radius:6px; padding:8px;");
    m_actionApprovalBanner->hide();

    m_actionDiff = new QTextEdit;
    m_actionDiff->setReadOnly(true);
    m_actionDiff->setLineWrapMode(QTextEdit::NoWrap);
    m_actionDiff->setFontFamily(QStringLiteral("monospace"));
    m_actionDiff->hide();

    m_actionApproveButton = new QPushButton("Approve & run");
    m_actionApproveButton->setObjectName("primaryButton");
    m_actionApproveButton->setCursor(Qt::PointingHandCursor);
    m_actionRejectButton = new QPushButton("Reject");
    m_actionRejectButton->setObjectName("dangerButton");
    m_actionRejectButton->setCursor(Qt::PointingHandCursor);
    connect(m_actionApproveButton, &QPushButton::clicked, this,
            &MainWindow::approveSelectedRun);
    connect(m_actionRejectButton, &QPushButton::clicked, this,
            &MainWindow::rejectSelectedRun);
    m_actionApprovalBar = new QWidget;
    auto *approvalRow = new QHBoxLayout(m_actionApprovalBar);
    approvalRow->setContentsMargins(0, 0, 0, 0);
    approvalRow->addWidget(m_actionApproveButton);
    approvalRow->addWidget(m_actionRejectButton);
    approvalRow->addStretch();
    m_actionApprovalBar->hide();

    m_actionLog = new QPlainTextEdit;
    m_actionLog->setReadOnly(true);
    m_actionLog->setObjectName("actionLog");
    applyLogFont(m_actionLog);
    new AgentLogHighlighter(m_actionLog->document());
    m_actionLog->setMaximumBlockCount(20000);

    auto *detailLayout = new QVBoxLayout(detailPane);
    detailLayout->setContentsMargins(12, 22, 24, 22);
    detailLayout->setSpacing(8);
    detailLayout->addWidget(m_actionRunTitle);
    detailLayout->addWidget(m_actionRunMeta);
    detailLayout->addWidget(m_actionApprovalBanner);
    detailLayout->addWidget(m_actionApprovalBar);
    detailLayout->addWidget(m_actionDiff, 1);
    detailLayout->addWidget(m_actionLog, 2);

    auto *splitter = new QSplitter(Qt::Horizontal);
    splitter->addWidget(wfPane);
    splitter->addWidget(listPane);
    splitter->addWidget(detailPane);
    splitter->setStretchFactor(0, 0);
    splitter->setStretchFactor(1, 0);
    splitter->setStretchFactor(2, 1);

    auto *layout = new QHBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(splitter);
    return page;
}

// ---- Settings: variables / secrets ----------------------------------------

void MainWindow::reloadVariablesTable()
{
    if (!m_varsTable)
        return;
    const QMap<QString, QString> vars = ActionStore::variables();
    QSignalBlocker block(m_varsTable);
    m_varsTable->setRowCount(0);
    for (auto it = vars.constBegin(); it != vars.constEnd(); ++it) {
        const int row = m_varsTable->rowCount();
        m_varsTable->insertRow(row);
        m_varsTable->setItem(row, 0, new QTableWidgetItem(it.key()));
        // Mask the value; the real text is kept in UserRole for editing.
        auto *valueItem = new QTableWidgetItem(
            QString(qMin(it.value().size(), 24), QChar(0x2022)));
        valueItem->setData(Qt::UserRole, it.value());
        m_varsTable->setItem(row, 1, valueItem);
    }
}

void MainWindow::addOrEditVariable()
{
    QString name, value;
    const QList<QTableWidgetItem *> selected =
        m_varsTable ? m_varsTable->selectedItems() : QList<QTableWidgetItem *>();
    const bool editing = !selected.isEmpty();
    if (editing) {
        const int row = selected.first()->row();
        name = m_varsTable->item(row, 0)->text();
        value = m_varsTable->item(row, 1)->data(Qt::UserRole).toString();
    }

    bool ok = false;
    const QString newName = QInputDialog::getText(
        this, editing ? "Edit variable" : "Add variable",
        "Name (e.g. CLOUDFLARE_API_TOKEN):", QLineEdit::Normal, name, &ok);
    if (!ok || newName.trimmed().isEmpty())
        return;
    const QString newValue = QInputDialog::getText(
        this, editing ? "Edit variable" : "Add variable", "Value:",
        QLineEdit::Password, value, &ok);
    if (!ok)
        return;

    QMap<QString, QString> vars = ActionStore::variables();
    if (editing && newName.trimmed() != name)
        vars.remove(name);
    vars.insert(newName.trimmed(), newValue);
    ActionStore::setVariables(vars);
    reloadVariablesTable();
}

void MainWindow::deleteSelectedVariable()
{
    if (!m_varsTable)
        return;
    const QList<QTableWidgetItem *> selected = m_varsTable->selectedItems();
    if (selected.isEmpty())
        return;
    const QString name = m_varsTable->item(selected.first()->row(), 0)->text();
    QMap<QString, QString> vars = ActionStore::variables();
    vars.remove(name);
    ActionStore::setVariables(vars);
    reloadVariablesTable();
}

void MainWindow::persistVariablesFromTable()
{
    // Variables are written directly in add/edit/delete; nothing to flush here.
}
