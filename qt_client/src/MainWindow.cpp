#include "MainWindow.h"

#include "MessageRow.h"
#include "RepoHost.h"
#include "ServerNode.h"
#include "Theme.h"

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
#include <QFileInfo>
#include <QFormLayout>
#include <QGridLayout>
#include <QStandardPaths>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QMimeDatabase>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QImage>
#include <QPainter>
#include <QPainterPath>
#include <QPlainTextEdit>
#include <QProcess>
#include <QPushButton>
#include <QRegularExpression>
#include <QScrollArea>
#include <QScrollBar>
#include <QSettings>
#include <QSignalBlocker>
#include <QSplitter>
#include <QStackedWidget>
#include <QStringListModel>
#include <QStyle>
#include <QStyleHints>
#include <QHeaderView>
#include <QTableWidget>
#include <QTabWidget>
#include <QTextBrowser>
#include <QTextEdit>
#include <QTreeWidget>
#include <QSystemTrayIcon>
#include <QThread>
#include <QTimer>
#include <QUrl>
#include <QUrlQuery>
#include <QUuid>
#include <QVBoxLayout>

#include <algorithm>

#ifndef FORKMESH_VERSION
#define FORKMESH_VERSION "dev"
#endif
#ifndef FORKMESH_SOURCE_DIR
#define FORKMESH_SOURCE_DIR ""
#endif

namespace {

const QString kRepoUrl = QStringLiteral("https://github.com/forkmesh/forkmesh.git");
const QString kDisplayNameSetting = QStringLiteral("profile/displayName");
const QString kHandleSetting = QStringLiteral("profile/handle");
const QString kBchSetting = QStringLiteral("profile/bch");
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
const QString kConnectionTotalSetting = QStringLiteral("stats/connectionTotalMs");
const QString kThemeSetting = QStringLiteral("app/theme"); // system | dark | light
const QString kWindowGeometrySetting = QStringLiteral("ui/windowGeometry");
const QString kVotesSpentSetting = QStringLiteral("votes/spent");
const QString kVotedSetting = QStringLiteral("votes/voted");
constexpr int kNetworkLogLimit = 2000;

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

QString formatRepoDate(qint64 timestampMs)
{
    if (timestampMs <= 0)
        return QStringLiteral("never");
    return QDateTime::fromMSecsSinceEpoch(timestampMs).toString("yyyy-MM-dd hh:mm");
}

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

// A small platform emoji for a node's operating system.
QString platformEmoji(const QString &platform)
{
    if (platform == "linux")
        return QString::fromUtf8("\xF0\x9F\x90\xA7"); // penguin
    if (platform == "macos")
        return QString::fromUtf8("\xF0\x9F\x8D\x8E"); // apple
    if (platform == "windows")
        return QString::fromUtf8("\xF0\x9F\xAA\x9F"); // window
    if (platform == "android")
        return QString::fromUtf8("\xF0\x9F\xA4\x96"); // robot
    if (platform == "ios")
        return QString::fromUtf8("\xF0\x9F\x93\xB1"); // phone
    if (platform == "web")
        return QString::fromUtf8("\xF0\x9F\x8C\x90"); // globe
    return QString();
}

// Crisp vector icons for the server-rail footer (glyph fonts render these
// inconsistently across platforms, so we draw them).
QPixmap gearPixmap(const QColor &color, int size)
{
    QPixmap pm(size, size);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    p.translate(size / 2.0, size / 2.0);
    p.setPen(Qt::NoPen);
    p.setBrush(color);
    const double rBody = size * 0.28;
    const double toothW = size * 0.12;
    const double toothH = size * 0.16;
    for (int i = 0; i < 8; ++i) {
        p.save();
        p.rotate(i * 45.0);
        p.drawRoundedRect(QRectF(-toothW / 2, -rBody - toothH * 0.55, toothW, toothH),
                          1.0, 1.0);
        p.restore();
    }
    QPainterPath body;
    body.addEllipse(QPointF(0, 0), rBody, rBody);
    QPainterPath hole;
    hole.addEllipse(QPointF(0, 0), size * 0.11, size * 0.11);
    p.drawPath(body.subtracted(hole));
    return pm;
}

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

QString defaultDisplayName(const ForkMeshIdentity &identity)
{
    const QString suffix = identity.shortPublicKey().left(8);
    return suffix.isEmpty() ? QStringLiteral("forkmesh-node")
                            : QStringLiteral("node-") + suffix;
}

void saveDisplayName(const QString &name)
{
    const QString trimmed = name.trimmed();
    if (!trimmed.isEmpty())
        QSettings().setValue(kDisplayNameSetting, trimmed);
}

QIcon statusDotIcon(bool online)
{
    QPixmap pixmap(12, 12);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(Qt::NoPen);
    painter.setBrush(online ? QColor("#22c55e") : QColor("#6b7280"));
    painter.drawEllipse(1, 1, 10, 10);
    return QIcon(pixmap);
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
    painter.drawEllipse(0, 0, side, side);
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
    loadActiveServerIntoEdits();
    refreshServerRail();
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
    const QString pref = QSettings().value(kThemeSetting, "system").toString();
    bool dark = true;
    if (pref == "light")
        dark = false;
    else if (pref == "dark")
        dark = true;
    else {
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
        dark = QGuiApplication::styleHints()->colorScheme() != Qt::ColorScheme::Light;
#endif
    }
    qApp->setStyleSheet(Theme::styleSheetForDark(dark));
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
    if (!current.isEmpty() && m_history.contains(current))
        switchConversation(current);
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
    m_nameEdit->setPlaceholderText("Display name");
    m_nameEdit->setMaxLength(32);
    m_nameEdit->setText(QSettings().value(kDisplayNameSetting).toString());
    m_handleEdit = new QLineEdit;
    m_handleEdit->setPlaceholderText("Handle (alice or node.example:alice)");
    m_handleEdit->setMaxLength(80);
    m_handleEdit->setText(QSettings().value(kHandleSetting).toString());
    m_bchEdit = new QLineEdit;
    m_bchEdit->setPlaceholderText("Bitcoin Cash address for donations (optional)");
    m_bchEdit->setMaxLength(160);
    m_bchEdit->setText(QSettings().value(kBchSetting).toString());
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

    m_updateButton = new QPushButton("\xE2\x9F\xB3 Quick update");
    m_updateButton->setObjectName("ghostButton");
    m_updateButton->setCursor(Qt::PointingHandCursor);
    m_updateButton->setToolTip("Pull the latest version, rebuild, and relaunch");
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
    cardLayout->addWidget(m_handleEdit);
    cardLayout->addWidget(m_bchEdit);
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
    cardLayout->addWidget(m_updateButton, 0, Qt::AlignHCenter);
    cardLayout->addWidget(m_updateStatus);

    auto *layout = new QVBoxLayout(page);
    layout->addStretch();
    layout->addWidget(card, 0, Qt::AlignHCenter);
    layout->addStretch();

    connect(startButton, &QPushButton::clicked, this, &MainWindow::startSession);
    connect(m_nameEdit, &QLineEdit::returnPressed, this, &MainWindow::startSession);
    connect(m_nameEdit, &QLineEdit::textEdited, this, [](const QString &name) {
        saveDisplayName(name);
    });
    connect(m_handleEdit, &QLineEdit::textEdited, this, [](const QString &handle) {
        QSettings().setValue(kHandleSetting, handle.trimmed());
    });
    connect(m_bchEdit, &QLineEdit::textEdited, this, [this](const QString &address) {
        QSettings().setValue(kBchSetting, address.trimmed());
        updateBchNotice();
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
    QString name = m_nameEdit->text().trimmed();
    if (name.isEmpty()) {
        name = defaultDisplayName(m_profileIdentity);
        m_nameEdit->setText(name);
        saveDisplayName(name);
    }
    if (!m_profileIdentity.isValid() && !m_profileIdentity.load()) {
        m_setupError->setText(m_profileIdentity.errorString());
        m_setupError->show();
        return;
    }
    if (m_serverUrlEdit->text().trimmed().isEmpty())
        m_serverUrlEdit->setText(kDefaultServerUrl);
    if (m_roomNameEdit->text().trimmed().isEmpty())
        m_roomNameEdit->setText(kDefaultRoomName);
    if (m_passphraseEdit->text().isEmpty())
        m_passphraseEdit->setText(kDefaultPassphrase);
    m_setupError->hide();
    m_userName = name;
    persistProfile();
    m_userAvatar = QSettings().value(kAvatarSetting).toByteArray();

    // Seed the Settings section's profile controls.
    if (m_settingsNameEdit)
        m_settingsNameEdit->setText(m_userName);
    setSettingsAvatar(m_userAvatar);

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
    m_memberList->clear();
    rebuildConversationView();

    QSettings().setValue(kServerUrlSetting, m_serverUrlEdit->text().trimmed());
    QSettings().setValue(kRoomNameSetting, m_roomNameEdit->text().trimmed());
    QSettings().setValue(kPassphraseSetting, m_passphraseEdit->text());
    persistEditsToActiveServer();
    const QUrl url(m_serverUrlEdit->text().trimmed());
    auto *server = new ServerNode(name, url, m_roomNameEdit->text().trimmed(),
                                  m_passphraseEdit->text(),
                                  m_bchEdit->text().trimmed(), this);
    attachBackend(server);
    if (!server->start())
        return;

    if (m_backend) {
        if (m_connectedAtMs <= 0)
            m_connectedAtMs = QDateTime::currentMSecsSinceEpoch();
        if (!m_userAvatar.isEmpty())
            m_backend->setAvatar(m_userAvatar);
        for (const RepositoryRecord &repo : std::as_const(m_repositories))
            m_backend->addChannel(repositoryChannel(repo));
        m_encryptionLabel->setText("\xF0\x9F\x94\x92 Mainnode encrypted");
        logSystem("Encryption: client-side AES-256-GCM mainnode room encryption.");
        const QJsonObject signedProfile =
            m_profileIdentity.signedProfile(m_nameEdit->text(),
                                            m_handleEdit->text(),
                                            m_bchEdit->text());
        const QString profileBytes = QString::fromUtf8(
            QJsonDocument(signedProfile).toJson(QJsonDocument::Compact));
        logSystem("Identity: signed profile for " +
                  m_profileIdentity.shortPublicKey() + " (" +
                  QString::number(profileBytes.toUtf8().size()) + " bytes).");
        m_stack->setCurrentIndex(1);
        showSection(0); // land on the Home overview after connecting
        refreshServerRail();
        updateBchNotice();
        // Restore locally-saved chat history for this server/room so past
        // conversations are visible right away (deduped against any replay).
        loadChatHistory();
        // Serve already-mirrored repos live to the web for this session.
        startRepoHosts();
    }
}

void MainWindow::persistProfile()
{
    QString handle = m_handleEdit->text().trimmed();
    if (handle.isEmpty())
        handle = repoSegment(m_nameEdit->text(), QStringLiteral("node"));
    m_handleEdit->setText(handle);

    saveDisplayName(m_nameEdit->text());
    QSettings settings;
    settings.setValue(kHandleSetting, handle);
    settings.setValue(kBchSetting, m_bchEdit->text().trimmed());
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
    saveDisplayName(m_nameEdit->text());
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

void MainWindow::buildAndRelaunch(const QString &clientDir)
{
    const QString buildDir = clientDir + "/build";
    setUpdateStatus("Configuring...");
    runUpdateStep("cmake", cmakeConfigureArgs(clientDir, buildDir),
                  clientDir, [this, buildDir] {
        setUpdateStatus("Rebuilding...");
        runUpdateStep("cmake",
                      {"--build", buildDir, "-j",
                       QString::number(QThread::idealThreadCount())},
                      buildDir, [this, buildDir] {
            // When the running binary lives elsewhere (e.g. ~/.local/bin),
            // install the fresh build over it; the running inode stays valid.
            const QString built = builtExecutablePath(buildDir);
            const QString appPath = QCoreApplication::applicationFilePath();
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
        });
    });
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
        return m_faviconCache.value(host);
    return letterFavicon(host);
}

QWidget *MainWindow::buildServerRail()
{
    m_serverRail = new QWidget;
    m_serverRail->setObjectName("serverRail");
    m_serverRail->setFixedWidth(56);

    m_serverGroup = new QButtonGroup(this);
    m_serverGroup->setExclusive(true);

    auto *layout = new QVBoxLayout(m_serverRail);
    layout->setContentsMargins(8, 14, 8, 14);
    layout->setSpacing(8);
    layout->setAlignment(Qt::AlignTop);
    refreshServerRail();
    return m_serverRail;
}

void MainWindow::refreshServerRail()
{
    if (!m_serverRail)
        return;
    auto *layout = qobject_cast<QVBoxLayout *>(m_serverRail->layout());
    if (!layout)
        return;

    // Clear existing buttons.
    for (QAbstractButton *button : m_serverGroup->buttons())
        m_serverGroup->removeButton(button);
    while (QLayoutItem *item = layout->takeAt(0)) {
        if (QWidget *w = item->widget())
            w->deleteLater();
        delete item;
    }

    for (int i = 0; i < m_servers.size(); ++i) {
        const ServerConfig &server = m_servers.at(i);
        auto *button = new QPushButton;
        button->setObjectName("serverButton");
        button->setCheckable(true);
        button->setCursor(Qt::PointingHandCursor);
        button->setFixedSize(40, 40);
        button->setIconSize(QSize(28, 28));
        button->setIcon(QIcon(faviconFor(server)));
        button->setToolTip(serverHost(server.url));
        button->setContextMenuPolicy(Qt::CustomContextMenu);
        if (i == m_activeServer)
            button->setChecked(true);
        m_serverGroup->addButton(button, i);
        connect(button, &QWidget::customContextMenuRequested, this,
                [this, i](const QPoint &) { removeServer(i); });
        layout->addWidget(button, 0, Qt::AlignHCenter);
    }

    auto *addButton = new QPushButton("+");
    addButton->setObjectName("serverAddButton");
    addButton->setCursor(Qt::PointingHandCursor);
    addButton->setFixedSize(40, 40);
    addButton->setToolTip("Add a mainnode server");
    connect(addButton, &QPushButton::clicked, this, &MainWindow::promptAddServer);
    layout->addWidget(addButton, 0, Qt::AlignHCenter);
    layout->addStretch();

    // Footer pinned to the bottom: rebuild/restart and Settings (the nav bar is
    // gone, so Settings lives here as an icon).
    const QColor footerColor(Theme::kTextTertiary);
    auto *rebuildBtn = new QPushButton;
    rebuildBtn->setObjectName("serverFooterButton");
    rebuildBtn->setCursor(Qt::PointingHandCursor);
    rebuildBtn->setFixedSize(40, 32);
    rebuildBtn->setIconSize(QSize(22, 22));
    rebuildBtn->setIcon(QIcon(refreshPixmap(footerColor, 0, 22)));
    rebuildBtn->setToolTip("Rebuild and restart ForkMesh");
    m_refreshButton = rebuildBtn;
    connect(rebuildBtn, &QPushButton::clicked, this, [this] {
        startRefreshSpin();
        quickRebuildRestart();
    });
    layout->addWidget(rebuildBtn, 0, Qt::AlignHCenter);

    auto *settingsBtn = new QPushButton;
    settingsBtn->setObjectName("serverFooterButton");
    settingsBtn->setCursor(Qt::PointingHandCursor);
    settingsBtn->setFixedSize(40, 40);
    settingsBtn->setIconSize(QSize(22, 22));
    settingsBtn->setIcon(QIcon(gearPixmap(footerColor, 22)));
    settingsBtn->setToolTip("Settings");
    connect(settingsBtn, &QPushButton::clicked, this, [this] { showSection(2); });
    layout->addWidget(settingsBtn, 0, Qt::AlignHCenter);

    connect(m_serverGroup, &QButtonGroup::idClicked, this,
            &MainWindow::switchToServer, Qt::UniqueConnection);
    updateBreadcrumb();
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
    refreshServerRail();
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
    refreshServerRail();
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
    refreshServerRail();
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
        refreshServerRail();
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
    m_sectionStack->addWidget(buildHomeSection());       // 0 Home (repos + chat)
    m_sectionStack->addWidget(buildRepoDetailSection()); // 1 Repo detail
    m_sectionStack->addWidget(buildSettingsSection());   // 2 Settings

    // The server rail is the only left strip now; the section fills the rest.
    auto *content = new QWidget;
    auto *contentLayout = new QHBoxLayout(content);
    contentLayout->setContentsMargins(0, 0, 0, 0);
    contentLayout->setSpacing(0);
    contentLayout->addWidget(buildServerRail());
    contentLayout->addWidget(m_sectionStack, 1);

    // Global donation nudge: shown across the whole app until this node sets a
    // Bitcoin Cash address, so the network stays open to donations.
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(buildBreadcrumb());
    layout->addWidget(buildBchNotice());
    layout->addWidget(content, 1);
    layout->addWidget(buildNetworkLogDock());
    return page;
}

QWidget *MainWindow::buildNetworkLogDock()
{
    auto *dock = new QWidget;
    dock->setObjectName("logDock");

    auto *label = new QLabel("NETWORK LOG");
    label->setObjectName("sectionLabel");
    auto *toggle = new QPushButton(QString::fromUtf8("\xE2\x96\xBE")); // down triangle
    toggle->setObjectName("iconButton");
    toggle->setCursor(Qt::PointingHandCursor);
    toggle->setFixedWidth(26);
    toggle->setToolTip("Show/hide the network log");

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
    layout->addLayout(headerRow);
    layout->addWidget(m_settingsLog);

    connect(toggle, &QPushButton::clicked, this, [this, toggle] {
        const bool show = !m_settingsLog->isVisible();
        m_settingsLog->setVisible(show);
        toggle->setText(QString::fromUtf8(show ? "\xE2\x96\xBE" : "\xE2\x96\xB8"));
    });
    return dock;
}

QWidget *MainWindow::buildBreadcrumb()
{
    auto *bar = new QWidget;
    bar->setObjectName("breadcrumbBar");
    m_breadcrumbServerIcon = new QLabel;
    m_breadcrumbServerIcon->setFixedSize(18, 18);
    m_breadcrumbServerIcon->setScaledContents(true);
    m_breadcrumb = new QLabel;
    m_breadcrumb->setObjectName("breadcrumb");
    m_breadcrumb->setTextFormat(Qt::RichText);
    m_breadcrumb->setTextInteractionFlags(Qt::TextBrowserInteraction);
    connect(m_breadcrumb, &QLabel::linkActivated, this, [this](const QString &href) {
        if (href == "repos")
            showSection(0);
    });
    auto *layout = new QHBoxLayout(bar);
    layout->setContentsMargins(14, 6, 14, 6);
    layout->setSpacing(8);
    layout->addWidget(m_breadcrumbServerIcon);
    layout->addWidget(m_breadcrumb);
    layout->addStretch();
    updateBreadcrumb();
    return bar;
}

void MainWindow::updateBreadcrumb()
{
    if (!m_breadcrumb)
        return;
    static const char *kSections[] = {"Home", "Repository", "Settings"};
    QString host;
    if (m_activeServer >= 0 && m_activeServer < m_servers.size())
        host = serverHost(m_servers.at(m_activeServer).url);
    // Active server favicon, shown next to the breadcrumb at the top of the app.
    if (m_breadcrumbServerIcon) {
        const QPixmap fav = m_faviconCache.value(host);
        m_breadcrumbServerIcon->setPixmap(fav.isNull() ? letterFavicon(host) : fav);
    }
    if (host.isEmpty())
        host = "ForkMesh";
    const int section = m_sectionStack ? m_sectionStack->currentIndex() : 0;
    const QString sep =
        QString::fromUtf8("<span style='color:#8b949e'>  \xE2\x80\xBA  </span>");
    QString trail = (section >= 0 && section < 3) ? kSections[section] : "Home";
    // On the repo detail view, fold the "Repositories › owner/name" path into the
    // single top breadcrumb (Repositories is a link back to the repo list).
    if (section == 1 && m_repoDetailIndex >= 0 &&
        m_repoDetailIndex < m_repositories.size()) {
        const RepositoryRecord &repo = m_repositories.at(m_repoDetailIndex);
        trail = QStringLiteral("<a href=\"repos\">Repositories</a>%1%2")
                    .arg(sep, (repo.owner + "/" + repo.name).toHtmlEscaped());
    }
    // No hardcoded text colors here: the section/separator inherit the
    // #breadcrumb stylesheet color so it stays readable in light and dark.
    m_breadcrumb->setText(
        QString::fromUtf8("\xF0\x9F\x9F\xA2 <b>%1</b>%2%3").arg(host.toHtmlEscaped(), sep, trail));
}

QWidget *MainWindow::buildBchNotice()
{
    m_bchBanner = new QWidget;
    m_bchBanner->setObjectName("bchBanner");
    m_bchBannerLabel = new QLabel(
        "\xF0\x9F\x92\x9A Add a Bitcoin Cash address so others can sponsor this "
        "node \xE2\x80\x94 it keeps the network open to donations and more "
        "sustainable.");
    m_bchBannerLabel->setObjectName("bchBannerLabel");
    m_bchBannerLabel->setWordWrap(true);

    auto *addButton = new QPushButton("Add BCH address");
    addButton->setObjectName("primaryButton");
    addButton->setCursor(Qt::PointingHandCursor);
    connect(addButton, &QPushButton::clicked, this, &MainWindow::promptSetBchAddress);

    auto *dismissButton = new QPushButton("\xE2\x9C\x95");
    dismissButton->setObjectName("ghostButton");
    dismissButton->setCursor(Qt::PointingHandCursor);
    dismissButton->setToolTip("Hide for now");
    connect(dismissButton, &QPushButton::clicked, m_bchBanner, &QWidget::hide);

    auto *layout = new QHBoxLayout(m_bchBanner);
    layout->setContentsMargins(16, 10, 12, 10);
    layout->setSpacing(10);
    layout->addWidget(m_bchBannerLabel, 1);
    layout->addWidget(addButton);
    layout->addWidget(dismissButton);
    m_bchBanner->hide();
    return m_bchBanner;
}

void MainWindow::updateBchNotice()
{
    if (!m_bchBanner)
        return;
    const bool hasAddress =
        !QSettings().value(kBchSetting).toString().trimmed().isEmpty();
    m_bchBanner->setVisible(!hasAddress);
}

void MainWindow::promptSetBchAddress()
{
    bool ok = false;
    const QString current = QSettings().value(kBchSetting).toString().trimmed();
    const QString address = QInputDialog::getText(
        this, "Bitcoin Cash address",
        "Enter a Bitcoin Cash address to receive donations:", QLineEdit::Normal,
        current, &ok);
    if (!ok)
        return;
    const QString trimmed = address.trimmed();
    QSettings().setValue(kBchSetting, trimmed);
    if (m_bchEdit)
        m_bchEdit->setText(trimmed);
    // The address is shared with peers on the next connect; the sponsor button
    // and donation notice pick it up immediately.
    updateBchNotice();
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

    // Everything on one page: the nodes & repositories panel on the left (each
    // node now carries its own stats inline), chat on the right.
    auto *splitter = new QSplitter(Qt::Horizontal);
    splitter->setObjectName("homeSplitter");
    splitter->setChildrenCollapsible(false);
    splitter->addWidget(buildReposPanel());
    splitter->addWidget(buildChatSection());
    splitter->setStretchFactor(0, 0);
    splitter->setStretchFactor(1, 1);
    splitter->setSizes({360, 680});

    auto *layout = new QHBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(splitter);
    return page;
}

QWidget *MainWindow::buildReposPanel()
{
    auto *page = new QWidget;
    page->setObjectName("sidebar"); // reuse list/label styling
    page->setMinimumWidth(240);

    auto *heading = new QLabel("Repositories");
    heading->setObjectName("channelTitle");
    auto *subtitle = new QLabel(
        "Repos this node mirrors, grouped by node. Only signed metadata is "
        "shared \xE2\x80\x94 the .git data stays on this machine.");
    subtitle->setObjectName("statusLine");
    subtitle->setWordWrap(true);

    auto *reposLabel = new QLabel("NODES & REPOSITORIES");
    reposLabel->setObjectName("sectionLabel");
    m_repoList = new QListWidget;
    m_repoList->setToolTip(
        "Repositories this node is preserving locally, grouped by node");

    // The bare mirror doubles as a local git remote: a fork can push here to
    // publish into the mirror. Show its path so the user can wire it up.
    auto *remoteLabel = new QLabel("LOCAL REMOTE \xC2\xB7 PUSH YOUR FORK HERE");
    remoteLabel->setObjectName("sectionLabel");
    m_repoRemoteEdit = new QLineEdit;
    m_repoRemoteEdit->setReadOnly(true);
    m_repoRemoteEdit->setPlaceholderText("Select a repository");
    m_repoRemoteEdit->setToolTip(
        "Add this as a remote in your fork, then push to publish into the mirror.");
    auto *copyRemoteButton = new QPushButton("Copy");
    copyRemoteButton->setObjectName("ghostButton");
    copyRemoteButton->setCursor(Qt::PointingHandCursor);
    auto *remoteRow = new QHBoxLayout;
    remoteRow->setContentsMargins(0, 0, 0, 0);
    remoteRow->addWidget(m_repoRemoteEdit, 1);
    remoteRow->addWidget(copyRemoteButton);
    m_repoRemoteHint = new QLabel;
    m_repoRemoteHint->setObjectName("statusLine");
    m_repoRemoteHint->setWordWrap(true);
    m_repoRemoteHint->setTextFormat(Qt::RichText);
    m_repoRemoteHint->setTextInteractionFlags(Qt::TextSelectableByMouse);

    // Live web status for the selected repository: a green "online" indicator
    // and a clickable link to browse it on the website once it is published.
    m_repoWebLink = new QLabel("Select a repository to see its web status.");
    m_repoWebLink->setObjectName("statusLine");
    m_repoWebLink->setWordWrap(true);
    m_repoWebLink->setOpenExternalLinks(true);
    m_repoWebLink->setTextInteractionFlags(Qt::TextBrowserInteraction);

    auto *addRepoButton = new QPushButton("+ Add");
    addRepoButton->setObjectName("ghostButton");
    addRepoButton->setCursor(Qt::PointingHandCursor);
    m_syncRepoButton = new QPushButton("Sync");
    m_syncRepoButton->setObjectName("ghostButton");
    m_syncRepoButton->setCursor(Qt::PointingHandCursor);
    m_publishRepoButton = new QPushButton("Publish");
    m_publishRepoButton->setObjectName("ghostButton");
    m_publishRepoButton->setCursor(Qt::PointingHandCursor);
    auto *repoButtonRow = new QHBoxLayout;
    repoButtonRow->setContentsMargins(0, 0, 0, 0);
    repoButtonRow->addWidget(addRepoButton);
    repoButtonRow->addWidget(m_syncRepoButton);
    repoButtonRow->addWidget(m_publishRepoButton);
    repoButtonRow->addStretch();

    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(24, 22, 24, 22);
    layout->setSpacing(8);
    layout->addWidget(heading);
    layout->addWidget(subtitle);
    layout->addSpacing(8);
    layout->addWidget(reposLabel);
    layout->addWidget(m_repoList, 1);
    layout->addWidget(remoteLabel);
    layout->addLayout(remoteRow);
    layout->addWidget(m_repoRemoteHint);
    layout->addWidget(m_repoWebLink);
    layout->addLayout(repoButtonRow);

    connect(m_repoList, &QListWidget::currentRowChanged, this, [this](int) {
        updateRepoWebLink();
        updateRepoRemoteInfo();
    });
    connect(copyRemoteButton, &QPushButton::clicked, this, [this] {
        const QString path = m_repoRemoteEdit->text();
        if (!path.isEmpty()) {
            QApplication::clipboard()->setText(path);
            logSystem("Copied local remote path to clipboard: " + path);
        }
    });
    connect(m_repoList, &QListWidget::itemClicked, this,
            [this](QListWidgetItem *item) {
                if (!item)
                    return;
                const int index = item->data(Qt::UserRole).toInt();
                if (index >= 0 && index < m_repositories.size())
                    openRepoDetail(index); // files + issues for this repo
                else if (index == -2) // advertised mirror: "mirror it too"
                    mirrorAdvertisedRepo(item->data(Qt::UserRole + 1).toString());
            });
    connect(addRepoButton, &QPushButton::clicked, this,
            &MainWindow::promptAddRepository);
    connect(m_syncRepoButton, &QPushButton::clicked, this,
            &MainWindow::syncSelectedRepository);
    connect(m_publishRepoButton, &QPushButton::clicked, this,
            &MainWindow::publishSelectedRepository);
    return page;
}

// ---- Issues section --------------------------------------------------------

QWidget *MainWindow::buildIssuesSection()
{
    auto *page = new QWidget;

    // --- Left: a sortable issue table with filters above and quick-add below.
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
    m_issueStatusFilter->addItems({"Open", "Closed", "All"});
    m_issueLabelFilter = new QComboBox;
    m_issueMilestoneFilter = new QComboBox;
    auto *filterRow = new QHBoxLayout;
    filterRow->setContentsMargins(0, 0, 0, 0);
    filterRow->addWidget(m_issueSearch, 1);
    filterRow->addWidget(m_issueStatusFilter);
    filterRow->addWidget(m_issueLabelFilter);
    filterRow->addWidget(m_issueMilestoneFilter);

    m_issueNewButton = new QPushButton("+ New issue");
    m_issueNewButton->setObjectName("ghostButton");
    m_issueNewButton->setCursor(Qt::PointingHandCursor);
    m_issueSyncButton = new QPushButton("Sync inbox");
    m_issueSyncButton->setObjectName("ghostButton");
    m_issueSyncButton->setCursor(Qt::PointingHandCursor);
    m_issueSyncButton->setToolTip(
        "Pull issue/comment submissions filed by other nodes and merge them");
    m_issueDetailToggle = new QPushButton("Hide detail");
    m_issueDetailToggle->setObjectName("ghostButton");
    m_issueDetailToggle->setCursor(Qt::PointingHandCursor);
    m_issueDetailToggle->setToolTip("Show/hide the issue detail panel");
    m_issueCreditsLabel = new QLabel;
    m_issueCreditsLabel->setObjectName("statusLine");
    m_issueCreditsLabel->setToolTip("Voting credits — earn 1 per hour online");
    auto *actionRow = new QHBoxLayout;
    actionRow->setContentsMargins(0, 0, 0, 0);
    actionRow->addWidget(m_issueNewButton);
    actionRow->addWidget(m_issueSyncButton);
    actionRow->addStretch();
    actionRow->addWidget(m_issueCreditsLabel);
    actionRow->addWidget(m_issueDetailToggle);

    m_issueTable = new QTableWidget(0, 6);
    m_issueTable->setObjectName("issueTable");
    m_issueTable->setHorizontalHeaderLabels(
        {"#", "Title", "Status", "Votes", "Labels", "Milestone"});
    m_issueTable->verticalHeader()->setVisible(false);
    m_issueTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_issueTable->setSelectionMode(QAbstractItemView::SingleSelection);
    m_issueTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_issueTable->setShowGrid(false);
    m_issueTable->setWordWrap(false);
    m_issueTable->setSortingEnabled(true);
    m_issueTable->sortByColumn(0, Qt::AscendingOrder);
    m_issueTable->setToolTip("Click a column header to sort");
    QHeaderView *header = m_issueTable->horizontalHeader();
    header->setHighlightSections(false);
    header->setSectionResizeMode(0, QHeaderView::ResizeToContents); // #
    header->setSectionResizeMode(1, QHeaderView::Stretch);          // Title
    header->setSectionResizeMode(2, QHeaderView::ResizeToContents); // Status
    header->setSectionResizeMode(3, QHeaderView::ResizeToContents); // Votes
    header->setSectionResizeMode(4, QHeaderView::ResizeToContents); // Labels
    header->setSectionResizeMode(5, QHeaderView::ResizeToContents); // Milestone

    // Quick-add: a single title field at the bottom, for filing an issue
    // without opening the full dialog.
    m_issueQuickAdd = new QLineEdit;
    m_issueQuickAdd->setObjectName("issueQuickAdd");
    m_issueQuickAdd->setPlaceholderText("+ Quick issue title\xE2\x80\xA6 (Enter)");
    m_issueQuickAdd->setMaxLength(160);

    auto *listLayout = new QVBoxLayout(listPane);
    listLayout->setContentsMargins(18, 18, 12, 18);
    listLayout->setSpacing(8);
    listLayout->addWidget(heading);
    listLayout->addWidget(m_issuesRepoCombo);
    listLayout->addLayout(filterRow);
    listLayout->addLayout(actionRow);
    listLayout->addWidget(m_issueTable, 1);
    listLayout->addWidget(m_issueQuickAdd);

    // Center: the selected issue's title, status badge, thread and composer.
    m_issueTitle = new QLabel("Select an issue");
    m_issueTitle->setObjectName("channelTitle");
    m_issueTitle->setWordWrap(true);
    m_issueCopyButton = new QPushButton("\xF0\x9F\x93\x8B Copy");
    m_issueCopyButton->setObjectName("ghostButton");
    m_issueCopyButton->setCursor(Qt::PointingHandCursor);
    m_issueCopyButton->setToolTip("Copy this issue (title and thread) to the clipboard");
    m_issueVoteButton = new QPushButton(QString::fromUtf8("\xE2\x96\xB2 Vote"));
    m_issueVoteButton->setObjectName("ghostButton");
    m_issueVoteButton->setCursor(Qt::PointingHandCursor);
    m_issueVoteButton->setToolTip("Upvote this issue (spends 1 voting credit)");
    auto *issueTitleRow = new QHBoxLayout;
    issueTitleRow->setContentsMargins(0, 0, 0, 0);
    issueTitleRow->addWidget(m_issueTitle, 1);
    issueTitleRow->addWidget(m_issueVoteButton, 0, Qt::AlignTop);
    issueTitleRow->addWidget(m_issueCopyButton, 0, Qt::AlignTop);
    m_issueMeta = new QLabel; // status badge
    m_issueMeta->setObjectName("statusLine");
    m_issueMeta->setWordWrap(true);
    m_issueMeta->setTextFormat(Qt::RichText);
    m_issueReadonlyNote = new QLabel;
    m_issueReadonlyNote->setObjectName("statusLine");
    m_issueReadonlyNote->setWordWrap(true);
    m_issueReadonlyNote->hide();

    m_issueThreadContainer = new QWidget;
    m_issueThreadLayout = new QVBoxLayout(m_issueThreadContainer);
    m_issueThreadLayout->setContentsMargins(0, 0, 0, 0);
    m_issueThreadLayout->setSpacing(10);
    m_issueThreadLayout->addStretch();
    m_issueThreadScroll = new QScrollArea;
    m_issueThreadScroll->setWidgetResizable(true);
    m_issueThreadScroll->setWidget(m_issueThreadContainer);
    m_issueThreadScroll->setObjectName("messageScroll");

    m_issueComposer = new QPlainTextEdit;
    m_issueComposer->setPlaceholderText("Write a comment\xE2\x80\xA6");
    m_issueComposer->setFixedHeight(80);
    m_issueAttachButton = new QPushButton("Attach image");
    m_issueCloseButton = new QPushButton("Close issue");
    m_issueCommentButton = new QPushButton("Comment");
    m_issueCommentButton->setObjectName("primaryButton");
    m_issueCommentButton->setCursor(Qt::PointingHandCursor);
    for (QPushButton *b : {m_issueAttachButton, m_issueCloseButton}) {
        b->setObjectName("ghostButton");
        b->setCursor(Qt::PointingHandCursor);
    }
    auto *composerButtons = new QVBoxLayout;
    composerButtons->addWidget(m_issueAttachButton);
    composerButtons->addWidget(m_issueCommentButton);
    composerButtons->addWidget(m_issueCloseButton);
    auto *composerRow = new QHBoxLayout;
    composerRow->setContentsMargins(0, 0, 0, 0);
    composerRow->addWidget(m_issueComposer, 1);
    composerRow->addLayout(composerButtons);

    auto *center = new QWidget;
    auto *centerLayout = new QVBoxLayout(center);
    centerLayout->setContentsMargins(22, 18, 16, 18);
    centerLayout->setSpacing(8);
    centerLayout->addLayout(issueTitleRow);
    centerLayout->addWidget(m_issueMeta);
    centerLayout->addWidget(m_issueReadonlyNote);
    centerLayout->addWidget(m_issueThreadScroll, 1);
    centerLayout->addLayout(composerRow);

    // Right: GitHub-style metadata sidebar (assignees, labels, milestone).
    auto *meta = new QWidget;
    meta->setObjectName("sidebar");
    meta->setMinimumWidth(180);
    m_issueAssigneesValue = new QLabel("No one assigned");
    m_issueLabelsValue = new QLabel("None yet");
    m_issueMilestoneValue = new QLabel("No milestone");
    for (QLabel *v : {m_issueAssigneesValue, m_issueLabelsValue, m_issueMilestoneValue}) {
        v->setObjectName("statusLine");
        v->setWordWrap(true);
        v->setTextFormat(Qt::RichText);
    }
    m_issueLabelsButton = new QPushButton("Edit");
    m_issueMilestoneButton = new QPushButton("Edit");
    m_issueAssigneesButton = new QPushButton("Edit");
    m_issueDeleteButton = new QPushButton("Delete issue");
    for (QPushButton *b : {m_issueLabelsButton, m_issueMilestoneButton,
                           m_issueAssigneesButton, m_issueDeleteButton}) {
        b->setObjectName("ghostButton");
        b->setCursor(Qt::PointingHandCursor);
    }
    auto *metaLayout = new QVBoxLayout(meta);
    metaLayout->setContentsMargins(16, 18, 16, 18);
    metaLayout->setSpacing(6);
    auto addMetaSection = [&](const QString &label, QLabel *value, QPushButton *btn) {
        auto *header = new QHBoxLayout;
        header->setContentsMargins(0, 0, 0, 0);
        auto *l = new QLabel(label);
        l->setObjectName("sectionLabel");
        header->addWidget(l);
        header->addStretch();
        btn->setMaximumWidth(60);
        header->addWidget(btn);
        metaLayout->addLayout(header);
        metaLayout->addWidget(value);
        metaLayout->addSpacing(10);
    };
    addMetaSection("ASSIGNEES", m_issueAssigneesValue, m_issueAssigneesButton);
    addMetaSection("LABELS", m_issueLabelsValue, m_issueLabelsButton);
    addMetaSection("MILESTONE", m_issueMilestoneValue, m_issueMilestoneButton);
    metaLayout->addStretch();
    metaLayout->addWidget(m_issueDeleteButton);

    // Collapsible detail panel: the issue thread (center) + metadata sidebar,
    // sharing their own draggable divider.
    m_issueDetail = new QWidget;
    auto *detailSplit = new QSplitter(Qt::Horizontal);
    detailSplit->setChildrenCollapsible(false);
    detailSplit->addWidget(center);
    detailSplit->addWidget(meta);
    detailSplit->setStretchFactor(0, 1);
    detailSplit->setStretchFactor(1, 0);
    detailSplit->setSizes({520, 220});
    auto *detailLayout = new QVBoxLayout(m_issueDetail);
    detailLayout->setContentsMargins(0, 0, 0, 0);
    detailLayout->addWidget(detailSplit);

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
    connect(m_issueQuickAdd, &QLineEdit::returnPressed, this,
            &MainWindow::quickAddIssue);
    connect(m_issueSyncButton, &QPushButton::clicked, this,
            &MainWindow::syncIssuesInbox);
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
    m_repoHeaderTitle = new QLabel("Repository");
    m_repoHeaderTitle->setObjectName("repoHeaderTitle");
    m_repoHeaderTitle->setTextFormat(Qt::RichText);
    auto *publicBadge = new QLabel("Public");
    publicBadge->setObjectName("publicBadge");

    auto *notifyButton = new QPushButton(QString::fromUtf8("\xF0\x9F\x94\x94"));
    notifyButton->setObjectName("repoAction");
    notifyButton->setToolTip("Notifications");
    m_forkButton = new QPushButton("\xE2\x9A\x82 Fork 0");
    m_mirrorButton = new QPushButton("\xE2\x87\x86 Mirror 1");
    m_starButton = new QPushButton("\xE2\x98\x86 Star 0");
    for (QPushButton *b : {notifyButton, m_forkButton, m_mirrorButton, m_starButton}) {
        b->setObjectName("repoAction");
        b->setCursor(Qt::PointingHandCursor);
    }
    m_mirrorButton->setToolTip("Sync this repository's mirror now");
    connect(m_mirrorButton, &QPushButton::clicked, this, [this] {
        if (m_repoDetailIndex >= 0)
            syncRepository(m_repoDetailIndex);
    });

    auto *headerRow = new QHBoxLayout;
    headerRow->setContentsMargins(16, 12, 16, 4);
    headerRow->setSpacing(8);
    headerRow->addWidget(m_repoHeaderTitle);
    headerRow->addWidget(publicBadge);
    headerRow->addStretch();
    headerRow->addWidget(notifyButton);
    headerRow->addWidget(m_forkButton);
    headerRow->addWidget(m_mirrorButton);
    headerRow->addWidget(m_starButton);

    // --- Tab bar (GitHub order; Commits gets its own tab).
    struct TabDef {
        const char *label;
    };
    const QList<QString> tabs = {"\xF0\x9F\x92\xBB Code",
                                 "\xF0\x9F\x95\x98 Commits",
                                 "\xF0\x9F\x93\x8B Issues",
                                 "\xF0\x9F\x94\x80 Pull requests",
                                 "\xE2\x96\xB6 Actions",
                                 "\xF0\x9F\x93\x96 Wiki",
                                 "\xF0\x9F\x9B\xA1 Security and quality",
                                 "\xF0\x9F\x93\x8A Insights"};
    m_repoDetailTabs = new QButtonGroup(this);
    m_repoDetailTabs->setExclusive(true);
    auto *tabRow = new QHBoxLayout;
    tabRow->setContentsMargins(12, 0, 12, 0);
    tabRow->setSpacing(2);
    for (int i = 0; i < tabs.size(); ++i) {
        auto *b = new QPushButton(tabs.at(i));
        b->setObjectName("repoTab");
        b->setCheckable(true);
        b->setCursor(Qt::PointingHandCursor);
        if (i == 0)
            b->setChecked(true);
        if (i == 2)
            m_repoIssuesTab = b; // keep a handle for the Issues (N) badge
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
    m_repoDetailStack->addWidget(buildPullsTab());                       // 3 Pull requests
    m_repoDetailStack->addWidget(buildPlaceholderTab("Actions"));        // 4
    m_repoDetailStack->addWidget(buildPlaceholderTab("Wiki"));           // 5
    m_repoDetailStack->addWidget(buildPlaceholderTab("Security and quality")); // 6
    m_repoDetailStack->addWidget(buildPlaceholderTab("Insights"));       // 7
    connect(m_repoDetailTabs, &QButtonGroup::idClicked, this, [this](int id) {
        m_repoDetailStack->setCurrentIndex(id);
        if (id == 1)
            loadCommits();
        else if (id == 3)
            reloadPulls();
    });

    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(6);
    layout->addLayout(headerRow);
    layout->addWidget(tabBar);
    layout->addWidget(m_repoDetailStack, 1);
    return page;
}

QWidget *MainWindow::buildRepoCommitsTab()
{
    auto *page = new QWidget;
    m_commitsList = new QListWidget;
    m_commitsList->setObjectName("commitsList");
    m_commitsList->setWordWrap(true);
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(16, 12, 16, 16);
    layout->addWidget(m_commitsList);
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
    m_pullNewButton = new QPushButton("+ New pull request");
    m_pullSyncButton = new QPushButton("Sync inbox");
    for (QPushButton *b : {m_pullNewButton, m_pullSyncButton}) {
        b->setObjectName("ghostButton");
        b->setCursor(Qt::PointingHandCursor);
    }
    m_pullSyncButton->setToolTip("Pull PR submissions filed by other nodes and merge them");
    auto *toolbar = new QHBoxLayout;
    toolbar->setContentsMargins(0, 0, 0, 0);
    toolbar->addWidget(m_pullNewButton);
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
    m_pullMergeButton = new QPushButton("Merge");
    m_pullCloseButton = new QPushButton("Close");
    for (QPushButton *b : {m_pullMergeButton, m_pullCloseButton}) {
        b->setObjectName("ghostButton");
        b->setCursor(Qt::PointingHandCursor);
    }
    m_pullMergeButton->setObjectName("primaryButton");
    auto *pullHeaderRow = new QHBoxLayout;
    pullHeaderRow->setContentsMargins(0, 0, 0, 0);
    pullHeaderRow->addWidget(m_pullTitle, 1);
    pullHeaderRow->addWidget(m_pullMergeButton, 0, Qt::AlignTop);
    pullHeaderRow->addWidget(m_pullCloseButton, 0, Qt::AlignTop);
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
    connect(m_pullSyncButton, &QPushButton::clicked, this, &MainWindow::syncPullsInbox);
    connect(m_pullMergeButton, &QPushButton::clicked, this, &MainWindow::mergeCurrentPull);
    connect(m_pullCloseButton, &QPushButton::clicked, this, &MainWindow::closeCurrentPull);
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
        updatePullActionState();
        return;
    }
    m_pullTitle->setText(QStringLiteral("#%1  %2").arg(found->number).arg(found->title));
    m_pullMeta->setText(
        QStringLiteral("<b>%1</b> \xE2\x86\x90 <b>%2</b> \xC2\xB7 %3 \xC2\xB7 %4 files "
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
    if (m_pullNewButton)
        m_pullNewButton->setEnabled(m_repoDetailIndex >= 0);
    if (m_pullSyncButton)
        m_pullSyncButton->setEnabled(writable);
    if (m_pullMergeButton)
        m_pullMergeButton->setEnabled(writable && have && open);
    if (m_pullCloseButton)
        m_pullCloseButton->setEnabled(writable && have && open);
}

void MainWindow::promptNewPull()
{
    if (m_repoDetailIndex < 0 || m_repoDetailIndex >= m_repositories.size())
        return;
    const QString dir = repoGitDir();
    if (dir.isEmpty()) {
        QMessageBox::warning(this, "New pull request",
                             "No local copy of this repository to diff.");
        return;
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
    auto *baseCombo = new QComboBox(&dialog);
    auto *headCombo = new QComboBox(&dialog);
    baseCombo->addItems(branches);
    headCombo->addItems(branches);
    if (branches.size() > 1)
        headCombo->setCurrentIndex(1);
    auto *titleEdit = new QLineEdit(&dialog);
    titleEdit->setPlaceholderText("Title");
    auto *bodyEdit = new QPlainTextEdit(&dialog);
    bodyEdit->setPlaceholderText("Describe the change\xE2\x80\xA6");
    auto *form = new QFormLayout;
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
    const QString title = titleEdit->text().trimmed();
    if (title.isEmpty()) {
        QMessageBox::warning(this, "New pull request", "A title is required.");
        return;
    }
    if (base == head) {
        QMessageBox::warning(this, "New pull request", "Base and head must differ.");
        return;
    }
    QByteArray diff;
    if (!runGitCapture(dir, {"diff", base + ".." + head}, &diff, nullptr) ||
        diff.trimmed().isEmpty()) {
        QMessageBox::warning(this, "New pull request",
                             "No differences between " + base + " and " + head + ".");
        return;
    }
    PullRequest pr;
    pr.title = title;
    pr.description = bodyEdit->toPlainText();
    pr.base = base;
    pr.head = head;
    pr.patch = QString::fromUtf8(diff);

    PullStore store = pullStoreForCurrentRepo();
    if (store.canWrite()) {
        QString error;
        const int number = store.createPull(pr.title, pr.description, pr.base, pr.head,
                                             pr.patch, &error);
        if (number < 0) {
            QMessageBox::warning(this, "New pull request", error);
            return;
        }
        m_currentPullNumber = number;
        reloadPulls();
    } else {
        submitPullToInbox(store.makeSignedPull(pr));
    }
}

void MainWindow::mergeCurrentPull()
{
    if (m_currentPullNumber < 0)
        return;
    if (QMessageBox::question(this, "Merge pull request",
                              QStringLiteral("Apply and merge pull request #%1?")
                                  .arg(m_currentPullNumber)) != QMessageBox::Yes)
        return;
    PullStore store = pullStoreForCurrentRepo();
    QString error;
    if (!store.mergePull(m_currentPullNumber, &error)) {
        QMessageBox::warning(this, "Merge pull request", error);
        return;
    }
    logSystem(QStringLiteral("Merged pull request #%1.").arg(m_currentPullNumber));
    reloadPulls();
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
    const QJsonObject payload{{"owner", repo.owner},
                              {"repo", repo.name},
                              {"pull", pr.toJson()}};
    QNetworkRequest request(pullsApiUrl(repo));
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    QNetworkReply *reply = m_networkAccess->post(
        request, QJsonDocument(payload).toJson(QJsonDocument::Compact));
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        reply->deleteLater();
        if (reply->error() == QNetworkReply::NoError)
            QMessageBox::information(
                this, "Pull request sent",
                "Your signed pull request was delivered to the maintainer's inbox.");
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
    m_historyButton = new QPushButton("\xF0\x9F\x95\x98 Commits");
    m_historyButton->setObjectName("ghostButton");
    m_historyButton->setCursor(Qt::PointingHandCursor);
    m_historyButton->setToolTip("View the full commit history");
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
    m_branchButton = new QPushButton("\xF0\x9F\x8C\xBF main");
    m_branchButton->setObjectName("ghostButton");
    m_branchButton->setCursor(Qt::PointingHandCursor);
    m_branchButton->setToolTip("Switch branch");
    m_tagsButton = new QPushButton("\xF0\x9F\x8F\xB7 Tags");
    m_tagsButton->setObjectName("ghostButton");
    m_tagsButton->setCursor(Qt::PointingHandCursor);
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

    auto *backButton = new QPushButton("\xE2\x86\x90 Files");
    backButton->setObjectName("ghostButton");
    backButton->setCursor(Qt::PointingHandCursor);
    backButton->setToolTip("Back to the repository overview");
    connect(backButton, &QPushButton::clicked, this, &MainWindow::showRepoOverview);
    auto *backRow = new QHBoxLayout;
    backRow->setContentsMargins(8, 4, 8, 0);
    backRow->addWidget(backButton);
    backRow->addStretch();

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
    });

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

void MainWindow::openRepoDetail(int repoIndex)
{
    if (repoIndex < 0 || repoIndex >= m_repositories.size())
        return;
    m_repoDetailIndex = repoIndex;
    const RepositoryRecord &repo = m_repositories.at(repoIndex);
    if (m_repoHeaderTitle)
        m_repoHeaderTitle->setText(
            QStringLiteral("%1 / <b>%2</b>")
                .arg(repo.owner.toHtmlEscaped(), repo.name.toHtmlEscaped()));

    // Per-repo metadata (info.json) + the branch we view; both feed the loaders.
    m_repoInfo = RepoInfo();
    m_repoBranch.clear();
    loadRepoInfo();
    loadBranchesAndTags();
    if (m_forkButton)
        m_forkButton->setText(QString::fromUtf8("\xE2\x9A\x82 Fork %1").arg(m_repoInfo.forks));
    if (m_mirrorButton)
        m_mirrorButton->setText(
            QString::fromUtf8("\xE2\x87\x86 Mirror %1").arg(qMax(1, m_repoInfo.mirrors)));
    if (m_starButton)
        m_starButton->setText(QString::fromUtf8("\xE2\x98\x86 Star %1").arg(m_repoInfo.stars));

    // Point the embedded issues UI at this repo (its combo is hidden).
    refreshIssuesRepoCombo();
    if (m_issuesRepoCombo) {
        const int combo = m_issuesRepoCombo->findData(repoIndex);
        if (combo >= 0)
            m_issuesRepoCombo->setCurrentIndex(combo);
    }
    reloadIssues();
    updateRepoIssueCount();

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
    // Land on the GitHub-style overview (no explorer until a file is opened).
    loadRepoOverview(QString());
    if (m_filesStack)
        m_filesStack->setCurrentIndex(0);
    showSection(1);
}

void MainWindow::updateRepoIssueCount()
{
    if (m_repoIssuesTab)
        m_repoIssuesTab->setText(
            QString::fromUtf8("\xF0\x9F\x93\x8B Issues (%1)").arg(m_currentIssues.size()));
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
    if (dir.isEmpty() || !runGitCapture(dir, {"show", currentRef() + ":" + path}, &out, &err))
        content = "Could not read file: " + err.left(200);
    else if (out.size() > 1024 * 1024)
        content = QStringLiteral("File is too large to preview (%1 KB).")
                      .arg(out.size() / 1024);
    else if (out.contains('\0'))
        content = QString::fromUtf8("Binary file (%1 bytes) \xE2\x80\x94 not shown.")
                      .arg(out.size());
    else
        content = QString::fromUtf8(out);

    auto *editor = new QPlainTextEdit;
    editor->setReadOnly(true);
    editor->setObjectName("codeEditor");
    editor->setLineWrapMode(QPlainTextEdit::NoWrap);
    editor->setPlainText(content);

    const QString name = path.section('/', -1);
    const int index = m_repoFileTabs->addTab(editor, iconForFile(name), name);
    m_repoFileTabs->setTabToolTip(index, path);
    m_repoFileTabs->setCurrentIndex(index);
    m_openFileTabs.insert(path, editor);
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
        QStringList logArgs{"log", "-1", "--format=%an%x1f%ar%x1f%s", currentRef()};
        if (!path.isEmpty())
            logArgs << "--" << path;
        if (!dir.isEmpty() && runGitCapture(dir, logArgs, &logOut, nullptr) &&
            !logOut.trimmed().isEmpty()) {
            const QStringList f = QString::fromUtf8(logOut).trimmed().split('\x1f');
            const QString author = f.value(0);
            const QString when = f.value(1);
            const QString subject = f.value(2);
            m_commitBar->setText(
                QStringLiteral("<b>%1</b> &nbsp; <span style='color:#8b949e'>%2 "
                               "committed %3</span>")
                    .arg(author.toHtmlEscaped(), subject.toHtmlEscaped(),
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
                                     ? QString::fromUtf8("\xF0\x9F\x95\x98 Commits")
                                     : QString::fromUtf8("\xF0\x9F\x95\x98 %1 Commits").arg(count));
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
    if (!m_commitsList)
        return;
    m_commitsList->clear();
    const QString dir = repoGitDir();
    if (dir.isEmpty())
        return;
    QByteArray out;
    if (!runGitCapture(dir, {"log", "--format=%h%x1f%an%x1f%ar%x1f%s", "-n", "300",
                             currentRef()},
                       &out, nullptr))
        return;
    for (const QByteArray &record : out.split('\n')) {
        if (record.trimmed().isEmpty())
            continue;
        const QStringList f = QString::fromUtf8(record).split('\x1f');
        if (f.size() < 4)
            continue;
        auto *item = new QListWidgetItem(
            QString::fromUtf8("%1\n%2 \xC2\xB7 %3 \xC2\xB7 %4")
                .arg(f.at(3), f.at(1), f.at(2), f.at(0)),
            m_commitsList);
        item->setToolTip(f.at(0));
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
        m_branchButton->setText(QString::fromUtf8("\xF0\x9F\x8C\xBF ") + branch);
    loadRepoOverview(QString());
    loadAboutSidebar();
    loadCommits();
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
        m_branchButton->setText(QString::fromUtf8("\xF0\x9F\x8C\xBF ") +
                                (m_repoBranch.isEmpty() ? "HEAD" : m_repoBranch));

    // Branch menu.
    if (m_branchButton) {
        auto *menu = new QMenu(m_branchButton);
        QByteArray out;
        if (!dir.isEmpty() &&
            runGitCapture(dir, {"branch", "--format=%(refname:short)"}, &out, nullptr)) {
            for (const QString &line : QString::fromUtf8(out).split('\n')) {
                const QString b = line.trimmed();
                if (b.isEmpty())
                    continue;
                menu->addAction(b, this, [this, b] { setRepoBranch(b); });
            }
        }
        if (menu->isEmpty())
            menu->addAction("No branches")->setEnabled(false);
        m_branchButton->setMenu(menu);
    }

    // Tags menu.
    if (m_tagsButton) {
        auto *menu = new QMenu(m_tagsButton);
        QByteArray out;
        int count = 0;
        if (!dir.isEmpty() && runGitCapture(dir, {"tag", "--sort=-creatordate"}, &out,
                                            nullptr)) {
            for (const QString &line : QString::fromUtf8(out).split('\n')) {
                const QString t = line.trimmed();
                if (t.isEmpty())
                    continue;
                ++count;
                menu->addAction(t, this, [this, t] { setRepoBranch(t); });
            }
        }
        if (menu->isEmpty())
            menu->addAction("No tags")->setEnabled(false);
        m_tagsButton->setMenu(menu);
        m_tagsButton->setText(QString::fromUtf8("\xF0\x9F\x8F\xB7 Tags %1").arg(count));
    }
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
        titleText += "  \xE2\x9C\x94";
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
                QString::fromUtf8("\xF0\x9F\x8F\x81 %1").arg(issue.milestone));
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
        m_issueMeta->clear();
        if (m_issueAssigneesValue)
            m_issueAssigneesValue->setText("No one assigned");
        if (m_issueLabelsValue)
            m_issueLabelsValue->setText("None yet");
        if (m_issueMilestoneValue)
            m_issueMilestoneValue->setText("No milestone");
        m_issueThreadLayout->addStretch();
        return;
    }

    m_issueTitle->setText(QStringLiteral("#%1  %2").arg(issue.number).arg(issue.title));

    // Status badge stays next to the title; labels/milestone/assignees live in
    // the GitHub-style right sidebar.
    m_issueMeta->setText(issue.status == "closed"
                             ? QString::fromUtf8("<b style='color:#ef4444'>\xE2\x97\x8F closed</b>")
                             : QString::fromUtf8("<b style='color:#22c55e'>\xE2\x97\x8F open</b>"));

    auto colorFor = [this](const QString &name) -> QString {
        for (const IssueLabel &l : m_currentLabels)
            if (l.name == name && !l.color.isEmpty())
                return l.color;
        return QStringLiteral("#94a3b8");
    };
    if (issue.assignees.isEmpty()) {
        m_issueAssigneesValue->setText("No one assigned");
    } else {
        QStringList shown;
        for (const QString &a : issue.assignees)
            shown << a.left(16).toHtmlEscaped();
        m_issueAssigneesValue->setText(shown.join("<br>"));
    }
    if (issue.labels.isEmpty()) {
        m_issueLabelsValue->setText("None yet");
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

    auto addCard = [&](const IssueEvent &ev, bool isOpen) {
        IssueEvent shown = ev;
        if (edits.contains(ev.id)) {
            shown.body = edits.value(ev.id).body;
            shown.attachments = edits.value(ev.id).attachments;
        }
        auto *card = new QWidget;
        card->setObjectName("homeCard");
        auto *cardLayout = new QVBoxLayout(card);
        cardLayout->setContentsMargins(14, 10, 14, 12);
        cardLayout->setSpacing(6);
        const QString who = ev.authorName.isEmpty() ? ev.author.left(10) : ev.authorName;
        const QString when =
            QDateTime::fromMSecsSinceEpoch(ev.ts).toString("yyyy-MM-dd HH:mm");
        auto *header = new QLabel(
            QStringLiteral("<b style='color:%1'>%2</b> <span style='color:#94a3b8'>%3%4</span>")
                .arg(senderColor(who), who.toHtmlEscaped(), when,
                     isOpen ? QString::fromUtf8(" \xC2\xB7 opened") : QString()));
        header->setTextFormat(Qt::RichText);
        cardLayout->addWidget(header);
        auto *body = new QLabel;
        body->setTextFormat(Qt::MarkdownText);
        body->setText(shown.body);
        body->setWordWrap(true);
        body->setTextInteractionFlags(Qt::TextBrowserInteraction);
        body->setOpenExternalLinks(true);
        cardLayout->addWidget(body);
        for (const QString &rel : shown.attachments) {
            if (haveLocalFiles) {
                QPixmap pix(imageBase + rel);
                if (!pix.isNull()) {
                    auto *img = new QLabel;
                    img->setPixmap(pix.width() > 420
                                       ? pix.scaledToWidth(420, Qt::SmoothTransformation)
                                       : pix);
                    cardLayout->addWidget(img);
                    continue;
                }
            }
            auto *placeholder = new QLabel(QString::fromUtf8("\xF0\x9F\x96\xBC %1").arg(rel));
            placeholder->setObjectName("statusLine");
            cardLayout->addWidget(placeholder);
        }
        m_issueThreadLayout->addWidget(card);
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
    }
    m_issueThreadLayout->addStretch();
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
    // Owner-only structural edits.
    for (QPushButton *b : {m_issueCloseButton, m_issueLabelsButton,
                           m_issueMilestoneButton, m_issueAssigneesButton,
                           m_issueDeleteButton, m_issueAttachButton}) {
        if (b)
            b->setEnabled(writable && haveIssue);
    }
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

    QDialog dialog(this);
    dialog.setWindowTitle("New issue");
    auto *titleEdit = new QLineEdit(&dialog);
    titleEdit->setPlaceholderText("Title");
    auto *bodyEdit = new QPlainTextEdit(&dialog);
    bodyEdit->setPlaceholderText("Describe the issue (markdown supported)\xE2\x80\xA6");
    auto *labelsEdit = new QLineEdit(&dialog);
    labelsEdit->setPlaceholderText("labels (comma separated)");
    auto *milestoneCombo = new QComboBox(&dialog);
    milestoneCombo->addItem("(no milestone)", QString());
    for (const IssueMilestone &ms : m_currentMilestones)
        milestoneCombo->addItem(ms.title, ms.title);

    auto *form = new QFormLayout;
    form->addRow("Title", titleEdit);
    form->addRow("Body", bodyEdit);
    form->addRow("Labels", labelsEdit);
    form->addRow("Milestone", milestoneCombo);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel,
                                         &dialog);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    auto *dialogLayout = new QVBoxLayout(&dialog);
    dialogLayout->addLayout(form);
    dialogLayout->addWidget(buttons);
    dialog.resize(520, 420);
    if (dialog.exec() != QDialog::Accepted)
        return;

    const QString title = titleEdit->text().trimmed();
    if (title.isEmpty()) {
        QMessageBox::warning(this, "New issue", "A title is required.");
        return;
    }
    QStringList labels;
    for (const QString &part : labelsEdit->text().split(',', Qt::SkipEmptyParts))
        labels << part.trimmed();

    IssueStore store = issueStoreForCurrentRepo();
    QString error;
    const int number = store.createIssue(title, bodyEdit->toPlainText(), labels,
                                         milestoneCombo->currentData().toString(),
                                         {}, {}, &error);
    if (number < 0) {
        QMessageBox::warning(this, "New issue", error);
        return;
    }
    m_currentIssueNumber = number;
    reloadIssues();
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
        QMessageBox::warning(this, "Quick issue",
                             issuesRepoIndex() < 0
                                 ? "Pick a repository you host to add issues."
                                 : "You don't host this repository, so new issues "
                                   "are owner-only.");
        return;
    }
    QString error;
    const int number = store.createIssue(title, QString(), {}, QString(), {}, {},
                                         &error);
    if (number < 0) {
        QMessageBox::warning(this, "Quick issue", error);
        return;
    }
    m_issueQuickAdd->clear();
    m_currentIssueNumber = number;
    reloadIssues();
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
    if (m_issueCopyButton) {
        m_issueCopyButton->setText("\xE2\x9C\x94 Copied");
        QTimer::singleShot(1500, m_issueCopyButton,
                           [this] { m_issueCopyButton->setText("\xF0\x9F\x93\x8B Copy"); });
    }
}

void MainWindow::addIssueComment()
{
    if (m_currentIssueNumber < 0)
        return;
    const QString body = m_issueComposer ? m_issueComposer->toPlainText() : QString();
    if (body.trimmed().isEmpty() && m_pendingIssueAttachments.isEmpty())
        return;
    IssueStore store = issueStoreForCurrentRepo();
    if (!store.canWrite()) {
        // Not the host: send a signed comment to the maintainer's relay inbox.
        submitIssueCommentToInbox(body);
        return;
    }
    QString error;
    if (!store.addComment(m_currentIssueNumber, body, m_pendingIssueAttachments, &error)) {
        QMessageBox::warning(this, "Comment", error);
        return;
    }
    m_issueComposer->clear();
    m_pendingIssueAttachments.clear();
    if (m_issueAttachButton)
        m_issueAttachButton->setText("Attach image");
    reloadIssues();
}

void MainWindow::attachIssueImage()
{
    const QStringList files = QFileDialog::getOpenFileNames(
        this, "Attach images", QString(),
        "Images (*.png *.jpg *.jpeg *.gif *.webp);;All files (*)");
    if (files.isEmpty())
        return;
    m_pendingIssueAttachments += files;
    if (m_issueAttachButton)
        m_issueAttachButton->setText(
            QStringLiteral("Attached: %1").arg(m_pendingIssueAttachments.size()));
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
        QMessageBox::warning(this, "Issue", error);
        return;
    }
    reloadIssues();
}

void MainWindow::deleteCurrentIssue()
{
    if (m_currentIssueNumber < 0)
        return;
    if (QMessageBox::question(
            this, "Delete issue",
            QStringLiteral("Delete issue #%1? This removes its folder and commits.")
                .arg(m_currentIssueNumber)) != QMessageBox::Yes)
        return;
    IssueStore store = issueStoreForCurrentRepo();
    QString error;
    if (!store.deleteIssue(m_currentIssueNumber, &error)) {
        QMessageBox::warning(this, "Delete issue", error);
        return;
    }
    m_currentIssueNumber = -1;
    reloadIssues();
}

void MainWindow::editIssueLabels()
{
    if (m_currentIssueNumber < 0)
        return;
    QStringList current;
    for (const Issue &issue : m_currentIssues)
        if (issue.number == m_currentIssueNumber)
            current = issue.labels;

    QDialog dialog(this);
    dialog.setWindowTitle("Labels");
    auto *list = new QListWidget(&dialog);
    for (const IssueLabel &label : m_currentLabels) {
        auto *item = new QListWidgetItem(label.name, list);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(current.contains(label.name) ? Qt::Checked : Qt::Unchecked);
    }
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel,
                                         &dialog);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    auto *dialogLayout = new QVBoxLayout(&dialog);
    dialogLayout->addWidget(new QLabel("Select labels for this issue:"));
    dialogLayout->addWidget(list);
    dialogLayout->addWidget(buttons);
    if (dialog.exec() != QDialog::Accepted)
        return;

    QStringList chosen;
    for (int i = 0; i < list->count(); ++i)
        if (list->item(i)->checkState() == Qt::Checked)
            chosen << list->item(i)->text();
    IssueStore store = issueStoreForCurrentRepo();
    QString error;
    if (!store.setLabels(m_currentIssueNumber, chosen, &error))
        QMessageBox::warning(this, "Labels", error);
    reloadIssues();
}

void MainWindow::editIssueMilestone()
{
    if (m_currentIssueNumber < 0)
        return;
    QStringList options{"(no milestone)"};
    for (const IssueMilestone &ms : m_currentMilestones)
        options << ms.title;
    QString current;
    for (const Issue &issue : m_currentIssues)
        if (issue.number == m_currentIssueNumber)
            current = issue.milestone;
    int currentIndex = current.isEmpty() ? 0 : options.indexOf(current);
    if (currentIndex < 0)
        currentIndex = 0;
    bool ok = false;
    const QString choice = QInputDialog::getItem(this, "Milestone", "Milestone:",
                                                 options, currentIndex, false, &ok);
    if (!ok)
        return;
    IssueStore store = issueStoreForCurrentRepo();
    QString error;
    if (!store.setMilestone(m_currentIssueNumber,
                            choice == "(no milestone)" ? QString() : choice, &error))
        QMessageBox::warning(this, "Milestone", error);
    reloadIssues();
}

void MainWindow::editIssueAssignees()
{
    if (m_currentIssueNumber < 0)
        return;
    QString current;
    for (const Issue &issue : m_currentIssues)
        if (issue.number == m_currentIssueNumber)
            current = issue.assignees.join(", ");
    bool ok = false;
    const QString text = QInputDialog::getText(
        this, "Assignees", "Assignees (comma separated names or pubkeys):",
        QLineEdit::Normal, current, &ok);
    if (!ok)
        return;
    QStringList assignees;
    for (const QString &part : text.split(',', Qt::SkipEmptyParts))
        assignees << part.trimmed();
    IssueStore store = issueStoreForCurrentRepo();
    QString error;
    if (!store.setAssignees(m_currentIssueNumber, assignees, &error))
        QMessageBox::warning(this, "Assignees", error);
    reloadIssues();
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
            if (m_issueComposer)
                m_issueComposer->clear();
            QMessageBox::information(
                this, "Comment sent",
                "Your signed comment was delivered to the maintainer's inbox.");
        } else {
            QMessageBox::warning(this, "Comment",
                                 "Could not send the comment: " + reply->errorString());
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
            QMessageBox::warning(this, "Vote",
                                 "Could not send your vote: " + reply->errorString());
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
    if (voted.contains(key)) {
        QMessageBox::information(this, "Vote", "You have already voted on this issue.");
        return;
    }
    if (availableCredits() <= 0) {
        QMessageBox::information(
            this, "Vote",
            "No voting credits yet — you earn 1 credit for every hour online.");
        return;
    }

    IssueStore store = issueStoreForCurrentRepo();
    if (store.canWrite()) {
        QString error;
        if (!store.addVote(m_currentIssueNumber, &error)) {
            QMessageBox::warning(this, "Vote", error);
            return;
        }
    } else {
        // Not the host: submit a signed vote to the maintainer's inbox.
        submitIssueVoteToInbox();
    }

    // Spend a credit and record the vote locally (blocks double-voting).
    QSettings s;
    s.setValue(kVotesSpentSetting, s.value(kVotesSpentSetting).toInt() + 1);
    voted << key;
    s.setValue(kVotedSetting, voted);
    reloadIssues();
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
    bool alreadyVoted = false;
    if (haveIssue) {
        for (const Issue &issue : m_currentIssues)
            if (issue.number == m_currentIssueNumber)
                votes = issue.votes;
        const int idx = issuesRepoIndex();
        if (idx >= 0) {
            const RepositoryRecord &repo = m_repositories.at(idx);
            const QString key = repo.owner + "/" + repo.name + "#" +
                                QString::number(m_currentIssueNumber);
            alreadyVoted = QSettings().value(kVotedSetting).toStringList().contains(key);
        }
    }
    m_issueVoteButton->setText(
        QString::fromUtf8("\xE2\x96\xB2 Vote (%1)").arg(votes));
    m_issueVoteButton->setEnabled(haveIssue && !alreadyVoted &&
                                  availableCredits() > 0);
    m_issueVoteButton->setToolTip(
        alreadyVoted ? "You already voted on this issue"
                     : "Upvote this issue (spends 1 voting credit)");
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
            QMessageBox::warning(this, "Sync inbox",
                                 "Could not reach the inbox: " + reply->errorString());
            return;
        }
        const QJsonObject root =
            QJsonDocument::fromJson(reply->readAll()).object();
        const QJsonArray pending = root.value("pending").toArray();
        if (pending.isEmpty()) {
            QMessageBox::information(this, "Sync inbox", "No pending submissions.");
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
        QMessageBox::information(
            this, "Sync inbox",
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

    auto *membersLabel = new QLabel("MEMBERS");
    membersLabel->setObjectName("sectionLabel");
    membersLabel->setToolTip("Click a member to start a direct chat");
    m_memberList = new QListWidget;
    m_memberList->setSelectionMode(QAbstractItemView::NoSelection);
    m_memberList->setFocusPolicy(Qt::NoFocus);
    m_memberList->setCursor(Qt::PointingHandCursor);
    m_memberList->setToolTip("Click a member to start a direct chat");

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
    sidebarLayout->addWidget(membersLabel);
    sidebarLayout->addWidget(m_memberList, 2);
    sidebarLayout->addStretch();

    // Main column
    auto *header = new QWidget;
    header->setObjectName("chatHeader");
    m_channelTitle = new QLabel("#general");
    m_channelTitle->setObjectName("channelTitle");
    m_encryptionLabel = new QLabel;
    m_encryptionLabel->setObjectName("encryptionLabel");
    auto *settingsButton = new QPushButton("\xE2\x9A\x99");
    settingsButton->setObjectName("iconButton");
    settingsButton->setCursor(Qt::PointingHandCursor);
    settingsButton->setToolTip("Settings & network log");
    connect(settingsButton, &QPushButton::clicked, this, [this] { showSection(2); });
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
    auto *firewallDismiss = new QPushButton("\xE2\x9C\x95");
    firewallDismiss->setObjectName("ghostButton");
    firewallDismiss->setCursor(Qt::PointingHandCursor);
    firewallDismiss->setToolTip("Dismiss");
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
    auto *attachButton = new QPushButton("\xF0\x9F\x93\x8E");
    attachButton->setObjectName("iconButton");
    attachButton->setCursor(Qt::PointingHandCursor);
    attachButton->setToolTip("Share a file (any type, including GIFs)");
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
    connect(m_memberList, &QListWidget::itemClicked, this,
            [this](QListWidgetItem *item) {
                const QString id = item->data(Qt::UserRole).toString();
                const bool self = item->data(Qt::UserRole + 2).toBool();
                if (!id.isEmpty() && !self)
                    openDirectChat(id, item->data(Qt::UserRole + 1).toString());
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
    return QStringLiteral(
               "%1 repos \xC2\xB7 %2 mirrored \xC2\xB7 %3 online \xC2\xB7 %4 chats")
               .arg(m_repositories.size())
               .arg(mirrored)
               .arg(online)
               .arg(m_channels.size()) +
           "\n       uptime " + formatDuration(sessionMs) + " \xC2\xB7 total " +
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
    m_settingsNameEdit->setPlaceholderText("Display name");
    connect(m_settingsNameEdit, &QLineEdit::editingFinished, this,
            [this] { onDisplayNameChanged(m_settingsNameEdit->text()); });

    m_settingsAvatarPreview = new QLabel("No\navatar");
    m_settingsAvatarPreview->setObjectName("avatarPreview");
    m_settingsAvatarPreview->setFixedSize(64, 64);
    m_settingsAvatarPreview->setAlignment(Qt::AlignCenter);
    auto *uploadButton = new QPushButton("Upload avatar…");
    uploadButton->setObjectName("ghostButton");
    uploadButton->setCursor(Qt::PointingHandCursor);
    connect(uploadButton, &QPushButton::clicked, this, &MainWindow::chooseAvatar);
    auto *avatarRow = new QHBoxLayout;
    avatarRow->setSpacing(12);
    avatarRow->addWidget(m_settingsAvatarPreview);
    avatarRow->addWidget(uploadButton);
    avatarRow->addStretch();

    auto *form = new QFormLayout;
    form->setLabelAlignment(Qt::AlignLeft);
    form->setSpacing(8);
    form->addRow("Display name", m_settingsNameEdit);
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

    auto *leaveButton = new QPushButton("\xE2\x86\x90 Leave node");
    leaveButton->setObjectName("dangerButton");
    leaveButton->setCursor(Qt::PointingHandCursor);
    connect(leaveButton, &QPushButton::clicked, this, [this] { leaveSession(); });
    auto *footerRow = new QHBoxLayout;
    footerRow->setContentsMargins(0, 0, 0, 0);
    footerRow->addWidget(leaveButton);
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
    layout->addWidget(startupLabel);
    layout->addWidget(m_autostartCheck);
    layout->addSpacing(6);
    layout->addWidget(appearanceLabel);
    layout->addWidget(m_themeCombo, 0, Qt::AlignLeft);
    layout->addStretch();
    layout->addLayout(footerRow);
    return page;
}

void MainWindow::setSettingsAvatar(const QByteArray &pngData)
{
    if (!m_settingsAvatarPreview || pngData.isEmpty())
        return;
    QPixmap pixmap;
    if (!pixmap.loadFromData(pngData))
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
        saveDisplayName(m_settingsNameEdit->text());
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
    connect(backend, &ChatBackend::statusChanged, this, [this](const QString &status) {
        const QString summary = status.section(" · ", 0, 0);
        m_statusLine->setText(summary);
        logSystem("Status: " + status);
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
        "\xE2\x9A\xA0 A firewall on this computer may be blocking peers from "
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
                        "\xE2\x9C\x94 Firewall opened. Peers should connect "
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

void MainWindow::notifyIfInactive(const QString &title, const QString &body)
{
    if (isActiveWindow())
        return;

    QApplication::alert(this, 0);
    if (!m_trayIcon || !QSystemTrayIcon::isSystemTrayAvailable())
        return;

    QString cleanBody = body.simplified();
    if (cleanBody.size() > 180)
        cleanBody = cleanBody.left(177) + "...";
    m_trayIcon->showMessage(title, cleanBody, QSystemTrayIcon::Information, 6000);
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
            message.hasFile() ? "\xF0\x9F\x93\x8E " + message.fileName : message.text;
        notifyIfInactive(message.senderName + " " + where, preview);
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
    m_homeRoster = members;
    m_memberList->clear();
    QHash<QString, int> nameCounts;
    for (const MemberInfo &member : members)
        ++nameCounts[member.name];
    for (const MemberInfo &member : members) {
        QString label = member.name;
        if (nameCounts.value(member.name) > 1 && !member.id.isEmpty())
            label += " [" + member.id.left(6) + "]";
        if (!member.note.isEmpty())
            label += " " + member.note;
        if (member.self)
            label += " (you)";
        auto *item = new QListWidgetItem(label);
        item->setIcon(statusDotIcon(member.online));
        item->setData(Qt::UserRole, member.id);
        item->setData(Qt::UserRole + 1, member.name);
        item->setData(Qt::UserRole + 2, member.self);
        m_memberList->addItem(item);
        // Keep DM tab titles in sync with renamed/rediscovered members.
        if (m_dmNames.contains(member.id) && m_dmNames.value(member.id) != member.name) {
            m_dmNames.insert(member.id, member.name);
            refreshDmList();
            if (m_currentConversation == dmKey(member.id))
                m_channelTitle->setText(kDmPrefix + member.name);
        }
    }

    // Nodes now live in the repositories panel (each node with its repos under
    // it), so refresh that to reflect live connection status.
    refreshRepositoryList();
    updateHomeStats();
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
        repo.bchAddress = settings.value("bchAddress").toString();
        repo.mirrorPath = settings.value("mirrorPath").toString();
        repo.publishToNetwork = settings.value("publishToNetwork").toBool();
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
    settings.beginWriteArray(kRepositoriesArray);
    for (int i = 0; i < m_repositories.size(); ++i) {
        settings.setArrayIndex(i);
        const RepositoryRecord &repo = m_repositories.at(i);
        settings.setValue("owner", repo.owner);
        settings.setValue("name", repo.name);
        settings.setValue("description", repo.description);
        settings.setValue("cloneUrl", repo.cloneUrl);
        settings.setValue("localPath", repo.localPath);
        settings.setValue("bchAddress", repo.bchAddress);
        settings.setValue("mirrorPath", repo.mirrorPath);
        settings.setValue("publishToNetwork", repo.publishToNetwork);
        settings.setValue("hostedSinceMs", repo.hostedSinceMs);
        settings.setValue("lastSyncMs", repo.lastSyncMs);
        settings.setValue("publishedAtMs", repo.publishedAtMs);
    }
    settings.endArray();
}

void MainWindow::refreshRepositoryList()
{
    if (!m_repoList)
        return;

    QSignalBlocker blocker(m_repoList);
    // Preserve the selected repository across the rebuild (rows shift because of
    // the node header rows).
    int selectedRepo = -1;
    if (QListWidgetItem *current = m_repoList->currentItem()) {
        const int idx = current->data(Qt::UserRole).toInt();
        if (idx >= 0)
            selectedRepo = idx;
    }
    m_repoList->clear();

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
        QString bch;
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
            ni.bch = m.bchAddress.trimmed();
            if (ni.self && ni.bch.isEmpty())
                ni.bch = QSettings().value(kBchSetting).toString().trimmed();
            ni.balance = m.bchBalance.trimmed();
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
        const QString owner = m_repositories.at(i).owner;
        if (!nodes.contains(owner)) {
            nodes.insert(owner, NodeInfo());
            nodeOrder.append(owner);
        }
    }

    // Our own repos by "owner/name", so advertised mirrors we already have are
    // not offered again.
    QSet<QString> ourRepoKeys;
    for (const RepositoryRecord &repo : std::as_const(m_repositories))
        ourRepoKeys.insert(repo.owner + "/" + repo.name);
    QSet<QString> shownAdvertised; // dedupe a repo advertised by several nodes

    QListWidgetItem *itemToSelect = nullptr;
    for (const QString &node : std::as_const(nodeOrder)) {
        const NodeInfo info = nodes.value(node);
        // Node header row: bold, not selectable, with a status dot + platform.
        QString headerText = node;
        if (info.self)
            headerText += " (you)";
        const QString emoji = platformEmoji(info.platform);
        if (!emoji.isEmpty())
            headerText += "  " + emoji;
        auto *header = new QListWidgetItem(headerText);
        header->setData(Qt::UserRole, -1);
        header->setFlags(Qt::ItemIsEnabled);
        if (info.inRoster)
            header->setIcon(statusDotIcon(info.online));
        else
            header->setText(QString::fromUtf8("\xF0\x9F\x96\xA5 ") + headerText);
        if (!info.platform.isEmpty())
            header->setToolTip("Platform: " + info.platform);
        QFont headerFont = header->font();
        headerFont.setBold(true);
        header->setFont(headerFont);
        m_repoList->addItem(header);

        // Inline node stats (this is the info the quest board used to show).
        QString infoText;
        if (info.self) {
            infoText = "       " + selfNodeStats();
        } else if (info.inRoster) {
            infoText = QString::fromUtf8("       %1")
                           .arg(info.online ? "online" : "offline");
            if (!info.bch.isEmpty())
                infoText += " \xC2\xB7 BCH " + compactAddress(info.bch);
            if (!info.balance.isEmpty())
                infoText += " \xC2\xB7 " + info.balance;
        }
        if (!infoText.isEmpty()) {
            auto *infoItem = new QListWidgetItem(infoText);
            infoItem->setData(Qt::UserRole, -1);
            infoItem->setFlags(Qt::ItemIsEnabled);
            infoItem->setForeground(QColor(Theme::kTextTertiary));
            m_repoList->addItem(infoItem);
        }

        for (int i : reposByNode.value(node)) {
            const RepositoryRecord &repo = m_repositories.at(i);
            const bool online = repo.publishedAtMs > 0;
            QString label = "    " + repo.name; // indent under the node
            if (m_syncingRepos.contains(i))
                label += "  \xC2\xB7 syncing";
            else if (repo.lastSyncMs > 0)
                label += "  \xC2\xB7 mirrored";
            if (online)
                label += "  \xC2\xB7 online";
            else if (repo.publishToNetwork)
                label += "  \xC2\xB7 publishing\xE2\x80\xA6";
            const QPair<int, int> stats =
                m_repoStats.value(repo.owner + "/" + repo.name);
            if (stats.first > 0)
                label += "\n       served " + QString::number(stats.first) +
                         "\xC3\x97 through the mainnode \xC2\xB7 " +
                         QString::number(stats.second) + " clone" +
                         (stats.second == 1 ? "" : "s");
            auto *item = new QListWidgetItem(label);
            item->setData(Qt::UserRole, i);
            // Green dot = published and browsable on the web; grey = local/pending.
            if (repo.publishToNetwork)
                item->setIcon(statusDotIcon(online));
            item->setToolTip(
                "Source: " + repositorySource(repo) +
                "\nMirror path: " + repo.mirrorPath +
                "\nHosted since: " + formatRepoDate(repo.hostedSinceMs) +
                "\nLast sync: " + formatRepoDate(repo.lastSyncMs) +
                "\nWeb: " +
                (online ? "online at " + repositoryWebUrl(repo)
                        : (repo.publishToNetwork ? "publishing\xE2\x80\xA6"
                                                 : "local only")) +
                (repo.bchAddress.isEmpty() ? QString() :
                                           "\nDonations: " + repo.bchAddress));
            m_repoList->addItem(item);
            if (i == selectedRepo)
                itemToSelect = item;
        }

        // Repos this node advertises mirroring that we don't have yet — offer to
        // "mirror it too".
        for (const QString &ownerName : info.mirrors) {
            if (ourRepoKeys.contains(ownerName) || shownAdvertised.contains(ownerName))
                continue;
            shownAdvertised.insert(ownerName);
            auto *item = new QListWidgetItem(
                QString::fromUtf8("    \xE2\x86\x93 ") + ownerName +
                QString::fromUtf8("   \xC2\xB7 mirror it too"));
            item->setData(Qt::UserRole, -2); // advertised mirror marker
            item->setData(Qt::UserRole + 1, ownerName);
            item->setForeground(QColor("#58a6ff"));
            item->setToolTip("Click to mirror this repository into your own mirror");
            m_repoList->addItem(item);
        }
    }
    if (itemToSelect)
        m_repoList->setCurrentItem(itemToSelect);
    updateRepoWebLink();
    updateRepoRemoteInfo();
    updateHomeStats();

    // Advertise our own mirrors so other nodes can see and mirror them too.
    if (m_backend) {
        QStringList ours;
        for (const RepositoryRecord &repo : std::as_const(m_repositories))
            ours << repo.owner + "/" + repo.name;
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
    for (const RepositoryRecord &r : std::as_const(m_repositories))
        if (r.owner == owner && r.name == name) {
            QMessageBox::information(this, "Mirror",
                                     "You already mirror this repository.");
            return;
        }
    if (m_activeServer < 0 || m_activeServer >= m_servers.size())
        return;
    const QUrl serverUrl(m_servers.at(m_activeServer).url);
    const QString host = serverHost(m_servers.at(m_activeServer).url);
    if (host.isEmpty())
        return;
    const QString scheme =
        serverUrl.scheme() == "ws" ? QStringLiteral("http") : QStringLiteral("https");
    const QString cloneUrl = scheme + "://" + host + "/" + owner + "/" + name;

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
    const QString ownerSetting = QSettings().value(kHandleSetting).toString().trimmed();
    repo.owner = ownerSetting.isEmpty()
                     ? repoSegment(m_userName, QStringLiteral("owner"))
                     : repoSegment(ownerSetting, QStringLiteral("owner"));
    repo.bchAddress = QSettings().value(kBchSetting).toString().trimmed();
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
    // Deep link to the repository's card on the public website, derived from
    // the same host that serves the catalog API.
    QUrl url = catalogApiUrl();
    url.setPath(QStringLiteral("/"));
    url.setFragment("repo/" + repoSegment(repo.owner, QStringLiteral("owner")) +
                    "/" + repoSegment(repo.name, QStringLiteral("repository")));
    return url.toString();
}

void MainWindow::updateRepoWebLink()
{
    if (!m_repoWebLink)
        return;
    QListWidgetItem *item = m_repoList ? m_repoList->currentItem() : nullptr;
    if (!item) {
        m_repoWebLink->setText("Select a repository to see its web status.");
        return;
    }
    const int index = item->data(Qt::UserRole).toInt();
    if (index < 0 || index >= m_repositories.size())
        return;
    const RepositoryRecord &repo = m_repositories.at(index);
    if (repo.publishedAtMs > 0) {
        const QString url = repositoryWebUrl(repo);
        m_repoWebLink->setText(
            "\xF0\x9F\x9F\xA2 <b>Online</b> \xC2\xB7 browsable at "
            "<a style='color:#4ade80' href=\"" + url + "\">" + url + "</a>");
    } else if (repo.publishToNetwork) {
        m_repoWebLink->setText(
            "\xF0\x9F\x95\x92 Publishing to the network\xE2\x80\xA6");
    } else {
        m_repoWebLink->setText("Local only \xC2\xB7 not published.");
    }
}

void MainWindow::updateRepoRemoteInfo()
{
    if (!m_repoRemoteEdit)
        return;
    QListWidgetItem *item = m_repoList ? m_repoList->currentItem() : nullptr;
    if (!item) {
        m_repoRemoteEdit->clear();
        if (m_repoRemoteHint)
            m_repoRemoteHint->clear();
        return;
    }
    const int index = item->data(Qt::UserRole).toInt();
    if (index < 0 || index >= m_repositories.size())
        return;
    const QString path = m_repositories.at(index).mirrorPath;
    m_repoRemoteEdit->setText(path);
    if (!m_repoRemoteHint)
        return;
    if (path.isEmpty()) {
        m_repoRemoteHint->setText("No mirror configured for this repository yet.");
        return;
    }
    const QString mono = "<span style='font-family:monospace; color:#cbd5e1'>";
    QString hint = "From your fork: " + mono +
                   "git remote add forkmesh \"" + path.toHtmlEscaped() +
                   "\"</span> then " + mono + "git push forkmesh &lt;branch&gt;</span>.";
    if (!QDir(path).exists())
        hint += " <i>(Sync this repository first to create the mirror.)</i>";
    m_repoRemoteHint->setText(hint);
}

void MainWindow::syncSelectedRepository()
{
    if (!m_repoList)
        return;
    QListWidgetItem *item = m_repoList->currentItem();
    if (!item) {
        QMessageBox::information(this, "Sync repository",
                                 "Select a mirrored repository first.");
        return;
    }
    syncRepository(item->data(Qt::UserRole).toInt());
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

QUrl MainWindow::filesApiUrl(const RepositoryRecord &repo) const
{
    QUrl url = catalogApiUrl();
    url.setPath("/api/repo/" + repoSegment(repo.owner, QStringLiteral("owner")) +
                "/" + repoSegment(repo.name, QStringLiteral("repository")) +
                "/files");
    return url;
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
    url.setPath("/api/repo/" + repoSegment(repo.owner, QStringLiteral("owner")) +
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
        if (!repo.publishToNetwork || repo.mirrorPath.isEmpty() ||
            !QDir(repo.mirrorPath).exists())
            continue;
        auto *host = new RepoHost(repo.owner, repo.name, repo.mirrorPath,
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

void MainWindow::publishRepositoryFiles(int index)
{
    if (index < 0 || index >= m_repositories.size())
        return;
    const RepositoryRecord repo = m_repositories.at(index);
    if (!repo.publishToNetwork || repo.mirrorPath.isEmpty())
        return;
    if (!m_profileIdentity.isValid() && !m_profileIdentity.load())
        return;

    // List the files in the mirror's default branch (paths + sizes only). The
    // file contents never leave this machine; only the listing is published.
    auto *process = new QProcess(this);
    connect(process, &QProcess::finished, this,
            [this, process, index](int exitCode, QProcess::ExitStatus) {
                const QByteArray out = process->readAllStandardOutput();
                process->deleteLater();
                if (exitCode != 0 || index < 0 || index >= m_repositories.size())
                    return;

                const RepositoryRecord &repo = m_repositories.at(index);
                const QString owner = repoSegment(repo.owner, QStringLiteral("owner"));
                const QString name = repoSegment(repo.name, QStringLiteral("repository"));
                QJsonArray files;
                const QList<QByteArray> lines = out.split('\n');
                for (const QByteArray &line : lines) {
                    // "<mode> <type> <oid> <size>\t<path>"
                    const int tab = line.indexOf('\t');
                    if (tab < 0)
                        continue;
                    const QList<QByteArray> meta =
                        line.left(tab).simplified().split(' ');
                    if (meta.size() < 4 || meta.at(1) != "blob")
                        continue;
                    bool ok = false;
                    const qlonglong size = meta.at(3).toLongLong(&ok);
                    files.append(QJsonObject{
                        {"path", QString::fromUtf8(line.mid(tab + 1))},
                        {"size", double(ok ? size : 0)}});
                    if (files.size() >= 5000)
                        break;
                }

                const QString updatedAt =
                    QString::number(QDateTime::currentMSecsSinceEpoch());
                const QJsonObject signedMeta{
                    {"owner", owner},
                    {"name", name},
                    {"updatedAt", updatedAt},
                    {"count", files.size()},
                    {"maintainer", m_profileIdentity.publicKey()}};
                QJsonObject payload{
                    {"owner", owner},
                    {"name", name},
                    {"updatedAt", updatedAt},
                    {"maintainer", m_profileIdentity.publicKey()},
                    {"signature", m_profileIdentity.signJson(signedMeta)},
                    {"files", files}};

                QNetworkRequest request(filesApiUrl(repo));
                request.setHeader(QNetworkRequest::ContentTypeHeader,
                                  QStringLiteral("application/json"));
                QNetworkReply *reply = m_networkAccess->post(
                    request, QJsonDocument(payload).toJson(QJsonDocument::Compact));
                logSystem("Files: publishing " + QString::number(files.size()) +
                          " file paths for " + repo.owner + "/" + repo.name + ".");
                connect(reply, &QNetworkReply::finished, this, [this, reply, repo] {
                    const int status =
                        reply->attribute(QNetworkRequest::HttpStatusCodeAttribute)
                            .toInt();
                    const QNetworkReply::NetworkError error = reply->error();
                    reply->deleteLater();
                    if (error == QNetworkReply::NoError && status >= 200 &&
                        status < 300)
                        logSystem("Files: published file list for " + repo.owner +
                                  "/" + repo.name + ".");
                    else
                        logSystem("Files: could not publish file list for " +
                                  repo.owner + "/" + repo.name + " (HTTP " +
                                  QString::number(status) + ").");
                });
            });
    connect(process, &QProcess::errorOccurred, this, [process] { process->deleteLater(); });
    process->start("git", {"-C", repo.mirrorPath, "ls-tree", "-r", "-l",
                           "--full-tree", "HEAD"});
}

void MainWindow::publishSelectedRepository()
{
    if (!m_repoList)
        return;
    QListWidgetItem *item = m_repoList->currentItem();
    if (!item) {
        QMessageBox::information(this, "Publish repository",
                                 "Select a mirrored repository first.");
        return;
    }
    const int index = item->data(Qt::UserRole).toInt();
    if (index >= 0 && index < m_repositories.size()) {
        m_repositories[index].publishToNetwork = true;
        saveRepositories();
        refreshRepositoryList();
    }
    publishRepository(index);
}

void MainWindow::publishRepository(int index, bool showDialogOnError)
{
    if (index < 0 || index >= m_repositories.size())
        return;
    if (!m_profileIdentity.isValid() && !m_profileIdentity.load()) {
        logSystem("Catalog: could not load identity for repository publishing.");
        return;
    }

    RepositoryRecord &repo = m_repositories[index];
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    const QString owner = repoSegment(repo.owner, QStringLiteral("owner"));
    const QString name = repoSegment(repo.name, QStringLiteral("repository"));
    QJsonObject metadata{{"owner", owner},
                         {"name", name},
                         {"description", repo.description},
                         {"cloneUrl", repo.cloneUrl},
                         {"bch", repo.bchAddress},
                         {"channel", repositoryChannel(repo)},
                         {"hostedSince", QString::number(repo.hostedSinceMs)},
                         {"lastSync", QString::number(repo.lastSyncMs)},
                         {"updatedAt", QString::number(now)},
                         {"source", repo.localPath.trimmed().isEmpty()
                                        ? QStringLiteral("remote-clone")
                                        : QStringLiteral("local-node")},
                         {"maintainer", m_profileIdentity.publicKey()}};
    metadata.insert("signature", m_profileIdentity.signJson(metadata));

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
                    QMessageBox::warning(this, "Publish repository", message);
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

} // namespace

void MainWindow::autoSyncMirrors()
{
    // Quietly refresh every repo's mirror so it tracks the owner's repo.
    for (int i = 0; i < m_repositories.size(); ++i) {
        if (!m_syncingRepos.contains(i) &&
            !repositorySource(m_repositories.at(i)).isEmpty())
            syncRepository(i, /*quiet=*/true);
    }
}

void MainWindow::syncRepository(int index, bool quiet)
{
    if (index < 0 || index >= m_repositories.size() ||
        m_syncingRepos.contains(index))
        return;

    RepositoryRecord &repo = m_repositories[index];
    if (!QDir().mkpath(QFileInfo(repo.mirrorPath).absolutePath())) {
        if (!quiet)
            QMessageBox::warning(this, "Sync repository",
                                 "Could not create " +
                                     QFileInfo(repo.mirrorPath).absolutePath());
        return;
    }

    const bool hasMirror = QDir(repo.mirrorPath).exists();
    const QString source = repositorySource(repo);
    const QString beforeDigest = mirrorRefsDigest(repo.mirrorPath);
    const QStringList args = hasMirror
                                 ? QStringList{"-C", repo.mirrorPath,
                                               "fetch", "--prune"}
                                 : QStringList{"clone", "--mirror",
                                               source, repo.mirrorPath};

    m_syncingRepos.insert(index);
    refreshRepositoryList();
    if (!quiet)
        logSystem(QStringLiteral("Mirror: ") +
                  (hasMirror ? QStringLiteral("fetching ") : QStringLiteral("cloning ")) +
                  repo.owner + "/" + repo.name + " from " + source + ".");

    auto *process = new QProcess(this);
    connect(process, &QProcess::finished, this,
            [this, process, index, quiet, beforeDigest, hasMirror](
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
                    // Did the owner's repo actually change?
                    const bool changed =
                        !hasMirror ||
                        mirrorRefsDigest(repo.mirrorPath) != beforeDigest;
                    repo.lastSyncMs = QDateTime::currentMSecsSinceEpoch();
                    saveRepositories();
                    refreshRepositoryList();
                    // Quiet auto-syncs only speak up when something changed.
                    if (!quiet || changed)
                        logSystem("Mirror: synced " + repo.owner + "/" +
                                  repo.name + " into " + repo.mirrorPath + ".");
                    if (changed && m_backend) {
                        m_backend->sendChat(
                            repositoryChannel(repo),
                            "Mirror synced by " + m_userName + " at " +
                                formatRepoDate(repo.lastSyncMs));
                    }
                    if (repo.publishToNetwork && (changed || !quiet)) {
                        publishRepository(index, false);
                        // Serve this repo's files live to the web now that a
                        // mirror exists (pure live tunnel, nothing uploaded).
                        startRepoHosts();
                    }
                } else {
                    refreshRepositoryList();
                    logSystem("Mirror: sync failed for " + repo.owner + "/" +
                              repo.name + ": " + errors.right(300));
                    if (!quiet)
                        QMessageBox::warning(
                            this, "Sync repository",
                            "Git mirror sync failed" +
                                (errors.isEmpty() ? QString() :
                                                    ": " + errors.right(500)));
                }
            });
    connect(process, &QProcess::errorOccurred, this,
            [this, process, index, quiet] {
                process->deleteLater();
                m_syncingRepos.remove(index);
                refreshRepositoryList();
                if (!quiet)
                    QMessageBox::warning(
                        this, "Sync repository",
                        "Could not run git. Install Git and try again.");
            });
    process->start("git", args);
}

// ------------------------------------------------------------------ settings

void MainWindow::onDisplayNameChanged(const QString &name)
{
    const QString trimmed = name.trimmed();
    if (trimmed.isEmpty() || trimmed == m_userName)
        return;
    m_userName = trimmed;
    saveDisplayName(trimmed);
    // The display name is announced with each peer link; it takes effect for
    // new messages immediately and for peers on their next reconnect.
    logSystem("Display name changed to " + trimmed +
              " (applies to new messages).");
}

void MainWindow::onAvatarChosen(const QByteArray &pngData)
{
    m_userAvatar = pngData;
    QSettings().setValue(kAvatarSetting, pngData);
    if (m_backend)
        m_backend->setAvatar(pngData);
}
