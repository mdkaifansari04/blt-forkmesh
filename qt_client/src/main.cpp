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









    if (!forkmesh::acquireSingleInstance(localSetupLink)) {
        qInfo().noquote()
            << "ForkMesh is already running; focusing the existing window.";
        return 0;
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
            if (!headless)
                QMessageBox::critical(nullptr, QStringLiteral("ForkMesh"),
                                      message);
            return 2;
        }
    }







    QFontDatabase::addApplicationFont(QStringLiteral(":/fonts/NotoColorEmoji.ttf"));
    {
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
            QMessageBox::critical(
                nullptr, "ForkMesh",
                "ForkMesh must not be run as root. It runs git and workflow "
                "commands with your privileges, so running as root is unsafe.\n\n"
                "Start it again as your normal user.");
            return 1;
        }
    }
#endif



    MainWindow::applyTheme();
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
    QObject::connect(app.styleHints(), &QStyleHints::colorSchemeChanged, &app,
                     [](Qt::ColorScheme) { MainWindow::applyTheme(); });
#endif


    QElapsedTimer startup;
    startup.start();
    qInfo().noquote() << QStringLiteral("[startup +%1ms] constructing MainWindow")
                             .arg(startup.elapsed(), 5);
    auto *window = new MainWindow;
    const auto openLocalSetup = [window](const QString &target) {
        if (isCloudflareSetupLink(target))
            window->openCloudflareSetupFromSystemLink(target);
    };
    app.setLocalLinkHandler(openLocalSetup);



    window->setHeadlessMode(headless);
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




    window->show();
    openLocalSetup(localSetupLink);
    qInfo().noquote() << QStringLiteral("[startup +%1ms] window shown; entering event loop")
                             .arg(startup.elapsed(), 5);



    HeadlessConsole *console = nullptr;
    if (headless)
        console = new HeadlessConsole(window, &app, &app);
    Q_UNUSED(console);

    const int exitCode = app.exec();
    if (headless) {




        std::fflush(stdout);
        std::fflush(stderr);
        std::_Exit(exitCode);
    }
    delete window;
    return exitCode;
}
