#include "CrashHandler.h"
#include "ControlNode.h"
#include "ForkMeshIdentity.h"
#include "HeadlessConsole.h"
#include "MainWindow.h"
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

#include <QApplication>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDebug>
#include <QDir>
#include <QEvent>
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

// Headless = no GUI available / wanted. Triggered explicitly with --headless or
// --cli, or auto-detected on Linux when there's no display server to connect to
// (so installing on a server / VM "just works" instead of aborting on the xcb
// plugin). We force the always-present "offscreen" Qt platform so QApplication
// still constructs — the whole backend (mesh, repo serving, mirror sync) runs in
// the event loop regardless of a display, and a stdin REPL drives it.
bool detectHeadless(const QStringList &args)
{
    if (args.contains(QStringLiteral("--headless")) ||
        args.contains(QStringLiteral("--cli")))
        return true;
#if defined(Q_OS_UNIX) && !defined(Q_OS_MACOS)
    // Honour an explicit platform choice (e.g. QT_QPA_PLATFORM=offscreen in CI).
    if (qEnvironmentVariableIsSet("QT_QPA_PLATFORM"))
        return qgetenv("QT_QPA_PLATFORM").startsWith("offscreen") ||
               qgetenv("QT_QPA_PLATFORM").startsWith("minimal");
    if (!qEnvironmentVariableIsSet("DISPLAY") &&
        !qEnvironmentVariableIsSet("WAYLAND_DISPLAY"))
        return true;
#endif
    return false;
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

// QApplication whose notify() wraps every event delivery in a try/catch. A C++
// exception thrown out of a slot invoked by the event loop — e.g. a handler for
// a tab click / currentChanged — is undefined behaviour in Qt6 and typically
// takes the whole app down with nothing logged ("it crashes when I click a
// tab"). Catching it here turns that silent crash into a logged fault (main log
// + durable crash log via forkmesh::logCaughtFault) and keeps the app running
// instead of dying. Signals (SIGSEGV etc.) still go through the CrashHandler.
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
            if (link == QLatin1String("forkmesh://control/cloudflare")) {
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
        // Swallow the fault: returning to the event loop keeps the window alive
        // rather than letting the exception unwind through Qt's C event loop.
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

// External signer used by tools/cloudflare_bootstrap.py. The bootstrapper sends
// a public mirror-manifest request on stdin; this short-lived mode validates it,
// signs the exact canonical payload with the existing local node identity, and
// writes only the public key + detached signature to stdout. The private key
// never crosses the process boundary, enters argv, or reaches Cloudflare.
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

} // namespace

int main(int argc, char *argv[])
{
    // First thing, before anything can fault: install the crash handlers so an
    // unexpected exit/crash leaves a record in the network log. A compact signal
    // breadcrumb and the full backtrace go to network_log.txt directly so the
    // crash is visible in the log view on next startup without needing a
    // separate file. Cheap; opens a couple fds plus fixed buffers.
    forkmesh::installCrashHandler(
        QString(),
        earlyMainLogPath());

    // Collect args before QApplication so headless/root flags are visible while we
    // still control the Qt platform plugin selection.
    QStringList rawArgs;
    rawArgs.reserve(argc);
    for (int i = 0; i < argc; ++i)
        rawArgs << QString::fromLocal8Bit(argv[i]);
    const QString localSetupLink =
        rawArgs.contains(QStringLiteral("forkmesh://control/cloudflare"))
            ? QStringLiteral("forkmesh://control/cloudflare")
            : QString();

    // `forkmesh --version` prints and exits before the Qt platform, root-gate
    // and single-instance setup. The auto-updater runs a candidate binary with
    // this flag as a smoke test, so it must succeed on a bare VPS with no
    // display, as root, and while the old instance still holds the lock.
    if (rawArgs.contains(QStringLiteral("--version"))) {
        printf("ForkMesh %s\n", FORKMESH_VERSION);
        return 0;
    }
    if (rawArgs.contains(QStringLiteral("--sign-mirror-manifest")))
        return runMirrorManifestSigner(argc, argv);
    if (rawArgs.contains(QStringLiteral("--verify-mirror-capability")))
        return runMirrorCapabilityVerifier(argc, argv);
    if (rawArgs.contains(QStringLiteral("--sign-mirror-health")))
        return runMirrorHealthSigner(argc, argv);
    if (rawArgs.contains(QStringLiteral("--materialize-public-mirror")))
        return runPublicMirrorMaterializer(argc, argv);

    const bool headless = detectHeadless(rawArgs);
    const bool allowRoot = rawArgs.contains(QStringLiteral("--allow-root")) ||
                           qEnvironmentVariableIsSet("FORKMESH_ALLOW_ROOT");

    // Under headless, run on the offscreen platform so QApplication initialises
    // without a display (unless the user already pinned a platform). This is what
    // turns the "could not connect to display / xcb plugin" abort into a working
    // CLI node.
    if (headless && !qEnvironmentVariableIsSet("QT_QPA_PLATFORM"))
        qputenv("QT_QPA_PLATFORM", "offscreen");

    // Headless always runs on offscreen/minimal, whose propagateSizeHints()
    // warning would otherwise spam the `forkmesh>` console. Install the filter
    // only here so the desktop GUI keeps Qt's untouched default logging.
    if (headless)
        forkmesh::installPlatformLogFilter();

    // The embedded terminal (TerminalWidget) renders itself from a forkpty PTY —
    // no xterm, no X11 reparenting — so it works the same on X11 and Wayland and
    // we no longer force the xcb backend. Forcing xcb pushed Wayland sessions onto
    // XWayland, which maps a black frame before the first paint (a jarring black
    // screen on launch). Letting Qt pick the native platform shows the themed
    // window immediately.
    ForkMeshApplication app(argc, argv);
    app.setApplicationName("ForkMesh");
    app.setOrganizationName("ForkMesh");
    // App icon: the cube cropped out of the ForkMesh logo.
    app.setWindowIcon(QIcon(QStringLiteral(":/app/forkmesh.png")));
    // On Wayland (the GNOME default) the dock/taskbar icon is NOT taken from
    // setWindowIcon — the compositor matches the window's app-id to an installed
    // .desktop file. This name must equal the basename of the desktop entry that
    // install.sh writes (forkmesh.desktop) for GNOME to show our logo and let it
    // be pinned. Harmless on X11/macOS/Windows.
    QGuiApplication::setDesktopFileName(QStringLiteral("forkmesh"));
    app.setStyle(QStyleFactory::create("Fusion"));

    // Refuse to run a second instance for this user. This is the actual fix for
    // "a new ForkMesh window opens seemingly at random": every trigger for that
    // (a desktop session restore, a login-autostart entry racing a manually
    // opened window, a double-click landing while a slow cold start is still
    // loading) used to hand back a brand new, fully independent process — its
    // own mesh backend and repo-hosting server reading/writing the same
    // ~/.forkmesh data out from under the first one. Now a duplicate launch
    // just raises the existing window and exits.
    if (!forkmesh::acquireSingleInstance(localSetupLink)) {
        qInfo().noquote()
            << "ForkMesh is already running; focusing the existing window.";
        return 0;
    }

    // Bundle a colour-emoji font so 🎉/🙊/✅ paint in full colour in chat
    // messages (and everywhere else) even on systems that ship no colour-emoji
    // font of their own (common on Linux). Registering it and appending it to
    // the application font's fallback family list makes Qt render colour glyphs
    // for any emoji codepoint the primary UI family is missing, rather than the
    // flat black-and-white boxes seen without it.
    QFontDatabase::addApplicationFont(QStringLiteral(":/fonts/NotoColorEmoji.ttf"));
    {
        QFont base = app.font();

        // Prefer a modern, consistent UI family over the raw platform default
        // (which on many Linux boxes is a dated bitmap-ish sans). We don't bundle
        // a font — that would bloat the binary — so we just pick the first family
        // from a prioritised list that the system actually has installed, and fall
        // back to whatever Qt chose otherwise. This is a no-op on systems that ship
        // none of them, so nothing regresses; where one is present (Segoe UI on
        // Windows, SF Pro on macOS, Inter/Roboto/Noto on Linux) the whole app gets
        // a cleaner, more uniform look.
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

        // Keep the chosen UI family first, then append colour-emoji fallbacks so
        // any codepoint the primary family is missing still paints in full colour.
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

    // Host-stats reporting (CPU/RAM/disk in the Mirror nodes view) is off by
    // default on the desktop but on for headless installs done from the Hosts
    // tab, so an operator can monitor the servers they provisioned. Seed the
    // toggles on the first headless launch; once set, the value persists and a
    // later operator edit is never overwritten (adhoc #23).
    if (headless) {
        QSettings settings;
        for (const QString &key : {TelemetrySettings::kReportCpu,
                                   TelemetrySettings::kReportMemory,
                                   TelemetrySettings::kReportDisk}) {
            if (!settings.contains(key))
                settings.setValue(key, true);
        }
        // Auto-update (kAutoUpdateSetting, MainWindowInternal.h) is likewise on by
        // default for a headless install — no one is around to click "Update,
        // rebuild & restart" on an operator-run VM — but off by default on
        // desktop; seed once, same as the telemetry toggles above.
        if (!settings.contains(QStringLiteral("update/autoUpdate")))
            settings.setValue(QStringLiteral("update/autoUpdate"), true);
    }

    // Refuse to run as root: ForkMesh runs git, action workflows and shell
    // steps, so running privileged is dangerous and unnecessary. This is a hard
    // refusal with no opt-out — including under sudo, where the real uid is 0
    // even though the effective uid may have been dropped. Headless can opt out
    // with --allow-root / FORKMESH_ALLOW_ROOT (e.g. a single-purpose VM), and
    // reports to stderr rather than a dialog nobody would see.
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

    // Apply the saved theme (system/dark/light); when set to "system", follow
    // the OS color scheme and switch live as it changes.
    MainWindow::applyTheme();
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
    QObject::connect(app.styleHints(), &QStyleHints::colorSchemeChanged, &app,
                     [](Qt::ColorScheme) { MainWindow::applyTheme(); });
#endif

    // Detailed startup timing to the terminal so a slow launch is diagnosable.
    QElapsedTimer startup;
    startup.start();
    qInfo().noquote() << QStringLiteral("[startup +%1ms] constructing MainWindow")
                             .arg(startup.elapsed(), 5);
    auto *window = new MainWindow;
    const auto openLocalSetup = [window](const QString &target) {
        if (target == QLatin1String("forkmesh://control/cloudflare"))
            window->openCloudflareSetupFromSystemLink();
    };
    app.setLocalLinkHandler(openLocalSetup);
    // Tell the window it's running without a GUI so its auto-start path can
    // register a fresh mirror's account non-interactively (the desktop pops a
    // "Join ForkMesh" dialog for that, which a headless VM cannot click).
    window->setHeadlessMode(headless);
    qInfo().noquote() << QStringLiteral("[startup +%1ms] MainWindow constructed")
                             .arg(startup.elapsed(), 5);
    // A later launch attempt bounces off acquireSingleInstance() above and
    // pings us instead; raise and focus our window in response. Under headless
    // (offscreen platform) there is no window to focus — raise()/activateWindow()
    // are no-ops there anyway, but the offscreen plugin logs "This plugin does
    // not support raise()" on every call, which makes a successful re-run of a
    // headless install (bouncing off an already-running node) look like an
    // error in the SSH install log. Skip the no-op calls entirely headless.
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
    window->show();
    openLocalSetup(localSetupLink);
    qInfo().noquote() << QStringLiteral("[startup +%1ms] window shown; entering event loop")
                             .arg(startup.elapsed(), 5);

    // In headless mode, the node runs the full backend but there's no GUI to
    // interact with, so attach a stdin REPL to observe and drive it.
    HeadlessConsole *console = nullptr;
    if (headless)
        console = new HeadlessConsole(window, &app, &app);
    Q_UNUSED(console);

    const int exitCode = app.exec();
    if (headless) {
        // Offscreen Qt has crashed in widget teardown on SIGTERM while deleting
        // the hidden text-edit-heavy UI tree. The process is exiting anyway, so
        // let the OS reclaim those widgets after closeEvent/aboutToQuit have
        // flushed logs and settings.
        std::fflush(stdout);
        std::fflush(stderr);
        std::_Exit(exitCode);
    }
    delete window;
    return exitCode;
}
