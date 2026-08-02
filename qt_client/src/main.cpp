#include "CrashHandler.h"
#include "ControlNode.h"
#include "DirectorySizeScan.h"
#include "ForkMeshIdentity.h"
#include "HeadlessConsole.h"
#include "MainWindow.h"
#include "MirrorActionsConfiguration.h"
#include "PlatformLogFilter.h"
#include "PublicMirrorRuntime.h"
#include "ServerNode.h"
#include "SingleInstance.h"
#include "StartupSplash.h"
#include "SystemStats.h"
#include "Theme.h"

#if __has_include("ForkMeshVersion.h")
#include "ForkMeshVersion.h"
#endif
#ifndef FORKMESH_VERSION
#define FORKMESH_VERSION "dev"
#endif
#ifndef FORKMESH_BUILD_COMMIT
#define FORKMESH_BUILD_COMMIT "unknown"
#endif

#include <QApplication>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDebug>
#include <QDir>
#include <QEvent>
#include <QFile>
#include <QFileInfo>
#include <QFileOpenEvent>
#include <QMetaObject>
#include <QElapsedTimer>
#include <QFont>
#include <QFontDatabase>
#include <QGuiApplication>
#include <QIcon>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QMessageBox>
#include <QSettings>
#include <QRegularExpression>
#include <QSet>
#include <QStandardPaths>
#include <QStringList>
#include <QStyleFactory>
#include <QStyleHints>
#include <QTimer>

#include <cstdio>
#include <cstdlib>
#include <utility>

#if defined(Q_OS_UNIX) && !defined(Q_OS_MACOS)
#include <unistd.h>
#endif

namespace {

QJsonObject readBoundedHelperRequest(const char *label, bool *ok)
{
    if (ok)
        *ok = false;
    QByteArray input;
    char buffer[4096];
    while (!std::feof(stdin) && input.size() <= 128 * 1024) {
        const std::size_t read =
            std::fread(buffer, 1, sizeof(buffer), stdin);
        if (read > 0)
            input.append(buffer, int(read));
        if (std::ferror(stdin))
            break;
    }
    if (input.isEmpty() || input.size() > 128 * 1024) {
        std::fprintf(stderr, "%s: invalid request size\n", label);
        return {};
    }
    QJsonParseError parseError;
    const QJsonDocument document =
        QJsonDocument::fromJson(input, &parseError);
    if (parseError.error != QJsonParseError::NoError ||
        !document.isObject()) {
        std::fprintf(stderr, "%s: invalid JSON request\n", label);
        return {};
    }
    if (ok)
        *ok = true;
    return document.object();
}

QByteArray strictBase64Url(const QString &value, qsizetype expected = -1)
{
    static const QRegularExpression pattern(
        QStringLiteral("^[A-Za-z0-9_-]+$"));
    if (!pattern.match(value).hasMatch())
        return {};
    const QByteArray decoded = QByteArray::fromBase64(
        value.toLatin1(), QByteArray::Base64UrlEncoding |
                                QByteArray::AbortOnBase64DecodingErrors);
    if ((expected >= 0 && decoded.size() != expected) ||
        QString::fromLatin1(
            decoded.toBase64(QByteArray::Base64UrlEncoding |
                             QByteArray::OmitTrailingEquals)) != value) {
        return {};
    }
    return decoded;
}

bool validExternalIdentityRequest(
    const QJsonObject &request, const QString &expectedType,
    QByteArray *message, QString *publicKey = nullptr)
{
    static const QSet<QString> commonFields{
        QStringLiteral("schemaVersion"), QStringLiteral("type"),
        QStringLiteral("algorithm"), QStringLiteral("encoding"),
        QStringLiteral("publicKey"), QStringLiteral("messageBase64"),
        QStringLiteral("messageSha256")};
    QSet<QString> actual;
    for (auto it = request.constBegin(); it != request.constEnd(); ++it)
        actual.insert(it.key());
    const bool verifier =
        expectedType ==
        QLatin1String("forkmesh.request-capability-verification");
    QSet<QString> expected = commonFields;
    if (verifier)
        expected.insert(QStringLiteral("signature"));
    const QString key =
        request.value(QStringLiteral("publicKey")).toString();
    const QByteArray payload = strictBase64Url(
        request.value(QStringLiteral("messageBase64")).toString());
    const QString digest =
        QString::fromLatin1(
            QCryptographicHash::hash(payload, QCryptographicHash::Sha256)
                .toHex());
    if (actual != expected ||
        request.value(QStringLiteral("schemaVersion")).toInt() != 1 ||
        request.value(QStringLiteral("type")).toString() != expectedType ||
        request.value(QStringLiteral("algorithm")).toString() !=
            QLatin1String("Ed25519") ||
        request.value(QStringLiteral("encoding")).toString() !=
            QLatin1String("base64url-no-padding") ||
        strictBase64Url(key, 32).size() != 32 ||
        payload.isEmpty() || payload.size() > 16 * 1024 ||
        request.value(QStringLiteral("messageSha256")).toString() !=
            digest) {
        return false;
    }
    if (message)
        *message = payload;
    if (publicKey)
        *publicKey = key;
    return true;
}









int runSizeMapScan(const QString &requestPath)
{
    QFile request(requestPath);
    if (!request.open(QIODevice::ReadOnly)) {
        std::fprintf(stderr, "size-map-scan: cannot read request\n");
        return 2;
    }
    const QByteArray payload = request.read(16 * 1024 * 1024);
    QString path;
    forkmesh::DirectorySizeScanOptions options;
    if (!forkmesh::decodeScanRequest(payload, &path, &options)) {
        std::fprintf(stderr, "size-map-scan: invalid request\n");
        return 2;
    }
    if (!QFileInfo(path).isDir()) {
        std::fprintf(stderr, "size-map-scan: not a directory\n");
        return 2;
    }



    const auto progress = [](const QString &current, qint64 bytes, int files) {
        const QByteArray line =
            forkmesh::encodeScanProgress(current, bytes, files);
        std::fwrite(line.constData(), 1, std::size_t(line.size()), stderr);
        std::fflush(stderr);
    };
    const QByteArray result = forkmesh::encodeScanResult(
        forkmesh::scanDirectorySizes(path, options, progress));
    std::fwrite(result.constData(), 1, std::size_t(result.size()), stdout);
    std::fflush(stdout);
    return 0;
}

void writeHelperResponse(const QJsonObject &response)
{
    const QByteArray bytes =
        QJsonDocument(response).toJson(QJsonDocument::Compact);
    std::fwrite(bytes.constData(), 1, std::size_t(bytes.size()), stdout);
    std::fputc('\n', stdout);
    std::fflush(stdout);
}

int runMirrorCapabilityVerifier(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("ForkMesh"));
    app.setOrganizationName(QStringLiteral("ForkMesh"));
    bool parsed = false;
    const QJsonObject request =
        readBoundedHelperRequest("capability verifier", &parsed);
    QByteArray payload;
    QString publicKey;
    const QString signature =
        request.value(QStringLiteral("signature")).toString();
    if (!parsed ||
        !validExternalIdentityRequest(
            request,
            QStringLiteral(
                "forkmesh.request-capability-verification"),
            &payload, &publicKey) ||
        strictBase64Url(signature, 64).size() != 64) {
        std::fputs("capability verifier: invalid request\n", stderr);
        return 2;
    }
    writeHelperResponse(
        {{QStringLiteral("valid"),
          ForkMeshIdentity::verifySignature(publicKey, signature,
                                            payload)},
         {QStringLiteral("publicKey"), publicKey}});
    return 0;
}

bool allowedHealthPayload(const QByteArray &payload)
{
    const QStringList lines =
        QString::fromUtf8(payload).split(QLatin1Char('\n'),
                                         Qt::KeepEmptyParts);
    static const QRegularExpression node(
        QStringLiteral("^[a-z](?:[a-z0-9-]{0,61}[a-z0-9])?$"));
    static const QRegularExpression nonce(
        QStringLiteral("^[A-Za-z0-9_-]{16,128}$"));
    static const QRegularExpression number(
        QStringLiteral("^[1-9][0-9]{0,18}$"));
    static const QRegularExpression repository(
        QStringLiteral("^[A-Za-z0-9._-]{1,100}$"));
    static const QRegularExpression digest(
        QStringLiteral("^[0-9a-f]{64}$"));
    if (lines.size() == 4 &&
        lines.at(0) == QLatin1String("forkmesh-https-health-v1")) {
        return node.match(lines.at(1)).hasMatch() &&
               nonce.match(lines.at(2)).hasMatch() &&
               number.match(lines.at(3)).hasMatch();
    }
    if (lines.size() != 10 ||
        lines.at(0) !=
            QLatin1String("forkmesh-https-health-repository-v1") ||
        !node.match(lines.at(1)).hasMatch() ||
        !nonce.match(lines.at(2)).hasMatch() ||
        !number.match(lines.at(3)).hasMatch() ||
        !node.match(lines.at(4)).hasMatch() ||
        !repository.match(lines.at(5)).hasMatch() ||
        (lines.at(6) != QLatin1String("0") &&
         lines.at(6) != QLatin1String("1")) ||
        (lines.at(7) != QLatin1String("ok") &&
         lines.at(7) != QLatin1String("unavailable")) ||
        (lines.at(6) == QLatin1String("1") &&
         lines.at(7) != QLatin1String("ok")) ||
        (lines.at(6) == QLatin1String("0") &&
         lines.at(7) != QLatin1String("unavailable")) ||
        !digest.match(lines.at(8)).hasMatch() ||
        !digest.match(lines.at(9)).hasMatch()) {
        return false;
    }
    return true;
}

int runMirrorHealthSigner(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("ForkMesh"));
    app.setOrganizationName(QStringLiteral("ForkMesh"));
    bool parsed = false;
    const QJsonObject request =
        readBoundedHelperRequest("health signer", &parsed);
    QByteArray payload;
    QString expectedPublicKey;
    ForkMeshIdentity identity;
    if (!parsed ||
        !validExternalIdentityRequest(
            request,
            QStringLiteral("forkmesh.health-challenge-signing"),
            &payload, &expectedPublicKey) ||
        !allowedHealthPayload(payload) || !identity.load() ||
        !identity.isValid() ||
        identity.publicKey() != expectedPublicKey) {
        std::fputs("health signer: invalid request or local identity\n",
                   stderr);
        return 2;
    }
    const QString signature = identity.signData(payload);
    if (signature.isEmpty()) {
        std::fputs("health signer: signing failed\n", stderr);
        return 3;
    }
    writeHelperResponse(
        {{QStringLiteral("publicKey"), identity.publicKey()},
         {QStringLiteral("signature"), signature}});
    return 0;
}

int runPublicMirrorMaterializer(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("ForkMesh"));
    app.setOrganizationName(QStringLiteral("ForkMesh"));
    bool parsed = false;
    const QJsonObject request =
        readBoundedHelperRequest("public mirror materializer", &parsed);
    ForkMeshIdentity identity;
    if (!parsed || !identity.load() || !identity.isValid()) {
        std::fputs(
            "public mirror materializer: local identity unavailable\n",
            stderr);
        return 2;
    }
    const QByteArray canonical =
        QByteArrayLiteral("forkmesh-public-age-vault-unlock-v1\n") +
        identity.publicKey().toUtf8();
    QByteArray vaultSecret = QByteArray::fromBase64(
        identity.signData(canonical).toLatin1(),
        QByteArray::Base64UrlEncoding);
    const QString dataRoot =
        QStandardPaths::writableLocation(
            QStandardPaths::AppDataLocation);
    QString error;
    const QJsonObject response =
        PublicMirrorRuntime::materializeGatewayRequest(
            request,
            QDir(dataRoot).filePath(
                QStringLiteral("public-mirror-archives")),
            QDir(dataRoot).filePath(
                QStringLiteral("identity/public-age-vault.json")),
            vaultSecret, PublicMirrorRuntime::Tools(), &error);
    vaultSecret.fill('\0');
    vaultSecret.clear();
    if (response.isEmpty()) {
        std::fputs(
            "public mirror materializer: request rejected\n", stderr);
        return 3;
    }
    writeHelperResponse(response);
    return 0;
}







bool detectHeadless(const QStringList &args)
{
    if (args.contains(QStringLiteral("--headless")) ||
        args.contains(QStringLiteral("--cli")))
        return true;
#if defined(Q_OS_UNIX) && !defined(Q_OS_MACOS)

    if (qEnvironmentVariableIsSet("QT_QPA_PLATFORM"))
        return qgetenv("QT_QPA_PLATFORM").startsWith("offscreen") ||
               qgetenv("QT_QPA_PLATFORM").startsWith("minimal");
    if (!qEnvironmentVariableIsSet("DISPLAY") &&
        !qEnvironmentVariableIsSet("WAYLAND_DISPLAY"))
        return true;
#endif
    return false;
}

bool isCloudflareSetupLink(const QString &value)
{
    const QUrl url(value);
    if (!url.isValid() || url.scheme() != QLatin1String("forkmesh") ||
        url.host() != QLatin1String("control") ||
        url.path() != QLatin1String("/cloudflare") ||
        !url.userInfo().isEmpty() || !url.fragment().isEmpty())
        return false;
    const QUrlQuery query(url);
    static const QRegularExpression prohibited(
        QStringLiteral(
            "(?:token|secret|password|private|credential|api[_-]?key)"),
        QRegularExpression::CaseInsensitiveOption);
    for (const auto &item : query.queryItems(QUrl::FullyDecoded)) {
        if (prohibited.match(item.first).hasMatch())
            return false;
    }
    return value.size() <= 4096;
}

QString earlyMainLogPath()
{
#if defined(Q_OS_UNIX) && !defined(Q_OS_MACOS)
    const QByteArray xdgDataHome = qgetenv("XDG_DATA_HOME");
    const QString base = xdgDataHome.isEmpty()
                             ? QDir::homePath() + QStringLiteral("/.local/share")
                             : QString::fromLocal8Bit(xdgDataHome);
    return QDir(base).filePath(QStringLiteral("ForkMesh/ForkMesh/network_log.txt"));
#else
    return QString();
#endif
}








class ForkMeshApplication : public QApplication
{
public:
    using QApplication::QApplication;

    void setLocalLinkHandler(std::function<void(const QString &)> handler)
    {
        m_localLinkHandler = std::move(handler);
        if (!m_pendingLocalLink.isEmpty() && m_localLinkHandler) {
            const QString pending = std::exchange(m_pendingLocalLink, QString());
            m_localLinkHandler(pending);
        }
    }

    bool event(QEvent *event) override
    {
        if (event && event->type() == QEvent::FileOpen) {
            const auto *open = static_cast<QFileOpenEvent *>(event);
            const QString link = open->url().toString(QUrl::FullyEncoded);
            if (isCloudflareSetupLink(link)) {
                if (m_localLinkHandler)
                    m_localLinkHandler(link);
                else
                    m_pendingLocalLink = link;
                return true;
            }
        }
        return QApplication::event(event);
    }

    bool notify(QObject *receiver, QEvent *event) override
    {
        try {
            return QApplication::notify(receiver, event);
        } catch (const std::exception &e) {
            forkmesh::logCaughtFault(describe(receiver, event),
                                     QString::fromUtf8(e.what()));
        } catch (...) {
            forkmesh::logCaughtFault(describe(receiver, event),
                                     QStringLiteral("(non-std exception)"));
        }


        return false;
    }

private:
    std::function<void(const QString &)> m_localLinkHandler;
    QString m_pendingLocalLink;

    static QString describe(QObject *receiver, QEvent *event)
    {
        const QString cls = receiver && receiver->metaObject()
                                ? QString::fromLatin1(receiver->metaObject()->className())
                                : QStringLiteral("(null)");
        const QString name = receiver ? receiver->objectName() : QString();
        const int type = event ? int(event->type()) : -1;
        return QStringLiteral("event delivery to %1%2 (event type %3)")
            .arg(cls,
                 name.isEmpty() ? QString() : QStringLiteral(" \"%1\"").arg(name))
            .arg(type);
    }
};






int runMirrorManifestSigner(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("ForkMesh"));
    app.setOrganizationName(QStringLiteral("ForkMesh"));

    QByteArray input;
    char buffer[4096];
    while (!std::feof(stdin) && input.size() <= 128 * 1024) {
        const std::size_t read = std::fread(buffer, 1, sizeof(buffer), stdin);
        if (read > 0)
            input.append(buffer, int(read));
        if (std::ferror(stdin))
            break;
    }
    if (input.isEmpty() || input.size() > 128 * 1024) {
        std::fputs("manifest signer: invalid request size\n", stderr);
        return 2;
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(input, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        std::fputs("manifest signer: invalid JSON request\n", stderr);
        return 2;
    }

    ForkMeshIdentity identity;
    if (!identity.load() || !identity.isValid()) {
        std::fputs("manifest signer: local identity is unavailable\n", stderr);
        return 3;
    }
    QString validationError;
    const QByteArray payload = forkmesh::control::mirrorManifestSigningPayload(
        document.object(), identity.publicKey(), &validationError);
    if (payload.isEmpty()) {
        const QByteArray safe =
            QStringLiteral("manifest signer: %1\n")
                .arg(validationError)
                .toUtf8();
        std::fwrite(safe.constData(), 1, std::size_t(safe.size()), stderr);
        return 2;
    }
    const QString signature = identity.signData(payload);
    if (signature.isEmpty()) {
        std::fputs("manifest signer: signing failed\n", stderr);
        return 3;
    }
    const QByteArray response =
        QJsonDocument(QJsonObject{
                          {QStringLiteral("publicKey"), identity.publicKey()},
                          {QStringLiteral("signature"), signature},
                      })
            .toJson(QJsonDocument::Compact);
    std::fwrite(response.constData(), 1, std::size_t(response.size()), stdout);
    std::fputc('\n', stdout);
    std::fflush(stdout);
    return 0;
}

}

int main(int argc, char *argv[])
{



    for (int i = 1; i + 1 < argc; ++i) {
        if (qstrcmp(argv[i], "--size-map-scan") == 0)
            return runSizeMapScan(QString::fromLocal8Bit(argv[i + 1]));
    }

    // Descriptors before anything can open one: most distributions still ship a
    // 1024 soft cap against a hard cap in the hundreds of thousands, and a node
    // that holds relay sockets, git child pipes, watches and a wakeup pipe per
    // thread can reach it. Hitting it is not a graceful failure — glib's
    // g_wakeup_new() calls g_error() when the pipe fails, so the next thread
    // start aborts the whole app with SIGTRAP inside
    // g_main_context_new_with_flags (the v0.7.9 crash report). Raising the soft
    // cap to the hard one removes that cliff.
    SystemStats::raiseOpenFileLimit();

    // First thing, before anything can fault: install the crash handlers so an
    // unexpected exit/crash leaves a record in the network log. A compact signal
    // breadcrumb and the full backtrace go to network_log.txt directly so the
    // crash is visible in the log view on next startup without needing a
    // separate file. Cheap; opens a couple fds plus fixed buffers.
    forkmesh::installCrashHandler(
        QString(),
        earlyMainLogPath());



    QStringList rawArgs;
    rawArgs.reserve(argc);
    for (int i = 0; i < argc; ++i)
        rawArgs << QString::fromLocal8Bit(argv[i]);
    QString localSetupLink;
    for (const QString &argument : std::as_const(rawArgs)) {
        if (isCloudflareSetupLink(argument)) {
            localSetupLink = argument;
            break;
        }
    }





    if (rawArgs.contains(QStringLiteral("--version"))) {
        printf("ForkMesh %s\n", FORKMESH_VERSION);
        return 0;
    }




    if (rawArgs.contains(QStringLiteral("--build-commit"))) {
        printf("%s\n", FORKMESH_BUILD_COMMIT);
        static const QRegularExpression exactCommit(
            QStringLiteral("^(?:[0-9a-f]{40}|[0-9a-f]{64})$"));
        return exactCommit.match(QStringLiteral(FORKMESH_BUILD_COMMIT)).hasMatch()
                   ? 0
                   : 1;
    }
    if (rawArgs.contains(QStringLiteral("--sign-mirror-manifest")))
        return runMirrorManifestSigner(argc, argv);
    if (rawArgs.contains(QStringLiteral("--verify-mirror-capability")))
        return runMirrorCapabilityVerifier(argc, argv);
    if (rawArgs.contains(QStringLiteral("--sign-mirror-health")))
        return runMirrorHealthSigner(argc, argv);
    if (rawArgs.contains(QStringLiteral("--materialize-public-mirror")))
        return runPublicMirrorMaterializer(argc, argv);
    if (rawArgs.contains(
            QStringLiteral("--configure-mirror-actions-stdin")))
        return forkmesh::mirror_actions::runConfigurationStdin(argc, argv);

    const bool headless = detectHeadless(rawArgs);
    const bool allowRoot = rawArgs.contains(QStringLiteral("--allow-root")) ||
                           qEnvironmentVariableIsSet("FORKMESH_ALLOW_ROOT");





    if (headless && !qEnvironmentVariableIsSet("QT_QPA_PLATFORM"))
        qputenv("QT_QPA_PLATFORM", "offscreen");








    forkmesh::installPlatformLogFilter();







    ForkMeshApplication app(argc, argv);
    app.setApplicationName("ForkMesh");
    app.setOrganizationName("ForkMesh");

    app.setWindowIcon(QIcon(QStringLiteral(":/app/forkmesh.png")));





    QGuiApplication::setDesktopFileName(QStringLiteral("forkmesh"));
    app.setStyle(QStyleFactory::create("Fusion"));

    // From here to the main window's first frame the GUI thread is busy and
    // nothing is on screen, so put up the launch splash: a centred card that
    // narrates every startup step as it happens (adhoc #39). It is a no-op
    // headless, under FORKMESH_NO_SPLASH, and when ui/startupSplash is off.
    // MainWindow::paintEvent hands over to the real window and fades it out.
    forkmesh::ui::showStartupSplash(headless, QStringLiteral(FORKMESH_VERSION),
                                    QStringLiteral(FORKMESH_BUILD_COMMIT));
    forkmesh::ui::startupStep(
        QStringLiteral("Checking for an already-running ForkMesh"));

    // Refuse to run a second instance for this user. This is the actual fix for
    // "a new ForkMesh window opens seemingly at random": every trigger for that
    // (a desktop session restore, a login-autostart entry racing a manually
    // opened window, a double-click landing while a slow cold start is still
    // loading) used to hand back a brand new, fully independent process — its
    // own mesh backend and repo-hosting server reading/writing the same
    // ~/.forkmesh data out from under the first one. Now a duplicate launch
    // just raises the existing window and exits.
    if (!forkmesh::acquireSingleInstance(localSetupLink)) {
        // The other instance is being raised instead; this process is about to
        // exit, so take the splash off the screen rather than flashing it.
        forkmesh::ui::dismissStartupSplash();
        qInfo().noquote()
            << "ForkMesh is already running; focusing the existing window.";
        return 0;
    }
    forkmesh::ui::startupStep(
        QStringLiteral("Recovering interrupted mirror configuration"));

    // Headless nodes intentionally place TMPDIR on persistent disk because a
    // repository materialization can be larger than their RAM-backed /tmp.
    // An abrupt reboot cannot run the Qt/Python temporary-directory destructors,
    // so sweep exact ForkMesh mirror artifacts only after proving this is the
    // sole application instance. This keeps crash debris from growing without
    // bound across gateway and encrypted-mirror restarts.
    {
        QString cleanupError;
        const int removed =
            PublicMirrorRuntime::cleanupStaleTemporaryDirectories(
                QDir::tempPath(), &cleanupError);
        if (removed < 0) {
            qWarning().noquote()
                << "Mirror temporary cleanup skipped:" << cleanupError;
        } else if (removed > 0) {
            qInfo().noquote()
                << "Mirror temporary cleanup removed" << removed
                << "stale director" << (removed == 1 ? "y." : "ies.");
        }
    }






    {
        QSettings recoverySettings;
        QString recoveryError;
        if (!forkmesh::mirror_actions::recoverPendingConfiguration(
                recoverySettings, {}, &recoveryError)) {
            const QString message =
                QStringLiteral(
                    "ForkMesh could not recover an interrupted mirror Actions "
                    "configuration (%1). No Actions services were started.")
                    .arg(recoveryError.isEmpty()
                             ? QStringLiteral("unknown recovery error")
                             : recoveryError);
            qCritical().noquote() << message;
            forkmesh::ui::startupStepFailed(
                QStringLiteral("Mirror Actions recovery failed — %1")
                    .arg(recoveryError));
            forkmesh::ui::dismissStartupSplash();
            if (!headless)
                QMessageBox::critical(nullptr, QStringLiteral("ForkMesh"),
                                      message);
            return 2;
        }
    }

    // Bundle a colour-emoji font so 🎉/🙊/✅ paint in full colour in chat
    // messages (and everywhere else) even on systems that ship no colour-emoji
    // font of their own (common on Linux). Registering it and appending it to
    // the application font's fallback family list makes Qt render colour glyphs
    // for any emoji codepoint the primary UI family is missing, rather than the
    // flat black-and-white boxes seen without it.
    forkmesh::ui::startupStep(QStringLiteral("Loading the colour-emoji font"));
    QFontDatabase::addApplicationFont(QStringLiteral(":/fonts/NotoColorEmoji.ttf"));
    {
        forkmesh::ui::startupStep(
            QStringLiteral("Choosing the interface font"));
        QFont base = app.font();









        const QStringList installed = QFontDatabase::families();
        for (const QString &preferred : {QStringLiteral("Inter"),
                                         QStringLiteral("SF Pro Text"),
                                         QStringLiteral("Segoe UI"),
                                         QStringLiteral("Roboto"),
                                         QStringLiteral("Noto Sans"),
                                         QStringLiteral("Ubuntu"),
                                         QStringLiteral("Cantarell")}) {
            if (installed.contains(preferred)) {
                base.setFamily(preferred);
                break;
            }
        }



        QStringList families{base.family()};
        for (const QString &emoji : {QStringLiteral("Noto Color Emoji"),
                                     QStringLiteral("Apple Color Emoji"),
                                     QStringLiteral("Segoe UI Emoji")}) {
            if (!families.contains(emoji))
                families << emoji;
        }
        base.setFamilies(families);
        app.setFont(base);
        forkmesh::ui::startupDetail(
            QStringLiteral("UI family: %1").arg(base.family()));
    }






    if (headless) {
        QSettings settings;
        for (const QString &key : {TelemetrySettings::kReportCpu,
                                   TelemetrySettings::kReportMemory,
                                   TelemetrySettings::kReportDisk}) {
            if (!settings.contains(key))
                settings.setValue(key, true);
        }




        if (!settings.contains(QStringLiteral("update/autoUpdate")))
            settings.setValue(QStringLiteral("update/autoUpdate"), true);
    }







#if defined(Q_OS_UNIX) && !defined(Q_OS_MACOS)
    if (geteuid() == 0 || getuid() == 0) {
        const char *refusal =
            "ForkMesh must not be run as root. It runs git and workflow commands "
            "with your privileges, so running as root is unsafe.\n"
            "Start it again as your normal user.";
        if (headless) {
            if (!allowRoot) {
                qCritical().noquote()
                    << refusal
                    << "\n(Pass --allow-root or set FORKMESH_ALLOW_ROOT=1 to "
                       "override on a dedicated VM.)";
                return 1;
            }
            qWarning().noquote()
                << "ForkMesh: running as root (--allow-root). git and workflow "
                   "steps will run with root privileges.";
        } else {
            forkmesh::ui::dismissStartupSplash();
            QMessageBox::critical(
                nullptr, "ForkMesh",
                "ForkMesh must not be run as root. It runs git and workflow "
                "commands with your privileges, so running as root is unsafe.\n\n"
                "Start it again as your normal user.");
            return 1;
        }
    }
#endif

    // Apply the saved theme (system/dark/light); when set to "system", follow
    // the OS color scheme and switch live as it changes.
    forkmesh::ui::startupStep(QStringLiteral("Applying the theme"));
    MainWindow::applyTheme();
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
    QObject::connect(app.styleHints(), &QStyleHints::colorSchemeChanged, &app,
                     [](Qt::ColorScheme) { MainWindow::applyTheme(); });
#endif


    QElapsedTimer startup;
    startup.start();
    qInfo().noquote() << QStringLiteral("[startup +%1ms] constructing MainWindow")
                             .arg(startup.elapsed(), 5);
    // Everything from here until show() runs with no event loop; the splash
    // repaints synchronously off the startupStep() calls inside the ctor.
    forkmesh::ui::startupStep(QStringLiteral("Starting the application window"));
    auto *window = new MainWindow;
    const auto openLocalSetup = [window](const QString &target) {
        if (isCloudflareSetupLink(target))
            window->openCloudflareSetupFromSystemLink(target);
    };
    app.setLocalLinkHandler(openLocalSetup);



    window->setHeadlessMode(headless);
    // The splash has been a free-standing always-on-top window because until
    // this line there was no window for it to be part of. Now there is: hand it
    // into the main window, where it becomes a child widget hovering over the
    // app's own content. Always-on-top is only ever a request, and several
    // compositors granted the newly-mapped main window the top spot instead —
    // which put the splash behind the app it was narrating. A child widget is
    // simply part of the window and cannot be stacked behind it.
    forkmesh::ui::attachStartupSplashTo(window);
    qInfo().noquote() << QStringLiteral("[startup +%1ms] MainWindow constructed")
                             .arg(startup.elapsed(), 5);







    forkmesh::onSingleInstanceActivation([window, headless, openLocalSetup](
                                             const QString &target) {
        if (headless)
            return;
        openLocalSetup(target);
        window->setWindowState((window->windowState() & ~Qt::WindowMinimized) |
                              Qt::WindowActive);
        window->show();
        window->raise();
        window->activateWindow();
    });
    // show() works under the offscreen platform too (rendering to an offscreen
    // surface) and drives the same deferred-startup path — including the
    // headless/offscreen safety net in MainWindow::showEvent — so auto-restore and
    // auto-connect behave identically headless and on the desktop.
    forkmesh::ui::startupStep(QStringLiteral("Showing the main window"));
    window->show();
    openLocalSetup(localSetupLink);
    qInfo().noquote() << QStringLiteral("[startup +%1ms] window shown; entering event loop")
                             .arg(startup.elapsed(), 5);
    // From here on there is a Log view to put messages in, so everything the app
    // and Qt log — catalog publishes, mirror syncs, rebuild/restart phases, Qt's
    // own "QProcess: Destroyed while process ("git") is still running." warnings
    // — goes there instead of scrolling past in the terminal the desktop was
    // launched from. The startup lines above deliberately stay on the console:
    // they time the window that has to exist before any of this can be read.
    // Headless keeps the console copy (see setAppLogSink): its operator reads
    // the service journal, and headlessUpdateRestart() in particular relies on
    // logRestart()'s terminal echo to narrate a rebuild nobody can watch on
    // screen.
    forkmesh::setAppLogSink(
        [window](QtMsgType type, const QString &message) {
            // Emitted from worker threads too (public-mirror sync, git helpers),
            // so hop to the GUI thread. A queued call whose receiver is deleted
            // first is discarded by ~QObject, and clearAppLogSink() below runs
            // before that delete.
            QMetaObject::invokeMethod(
                window, [window, type, message] {
                    window->logCapturedMessage(type, message);
                },
                Qt::QueuedConnection);
        },
        headless);
    // The splash normally hands over from MainWindow's first paintEvent, which
    // is the moment there is actually something behind it. This is the backstop
    // for platforms/compositors that never deliver that paint (finish() is
    // idempotent, so the common case ignores it).
    QTimer::singleShot(4000, &app, [] {
        forkmesh::ui::finishStartupSplash(QStringLiteral("Window ready"));
    });



    HeadlessConsole *console = nullptr;
    if (headless)
        console = new HeadlessConsole(window, &app, &app);
    Q_UNUSED(console);

    const int exitCode = app.exec();
    // Past this point a queued call into the window would never be delivered
    // (no event loop left to deliver it) and the window is about to go away, so
    // shutdown logs to the console again. Blocks until any in-flight sink call
    // has returned, which is what makes the delete below safe.
    forkmesh::clearAppLogSink();
    if (headless) {




        std::fflush(stdout);
        std::fflush(stderr);
        std::_Exit(exitCode);
    }
    delete window;
    return exitCode;
}
