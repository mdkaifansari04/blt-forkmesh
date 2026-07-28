#include "ControlNode.h"
#include "MirrorActionsConfiguration.h"
#include "MirrorActionsSummary.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMap>
#include <QMutex>
#include <QProcess>
#include <QSettings>
#include <QSet>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QThread>
#include <QWaitCondition>

#include <atomic>
#include <cstdio>
#include <thread>

namespace {

int failures = 0;

void check(bool condition, const char *message)
{
    if (condition)
        return;
    std::fprintf(stderr, "FAIL: %s\n", message);
    ++failures;
}

QString base64Url(const QByteArray &value)
{
    return QString::fromLatin1(
        value.toBase64(QByteArray::Base64UrlEncoding |
                       QByteArray::OmitTrailingEquals));
}

bool runProcess(const QString &program, const QStringList &arguments,
                const QString &workingDirectory = {})
{
    QProcess process;
    if (!workingDirectory.isEmpty())
        process.setWorkingDirectory(workingDirectory);
    process.setProcessChannelMode(QProcess::MergedChannels);
    process.start(program, arguments);
    return process.waitForStarted(3000) &&
           process.waitForFinished(15000) &&
           process.exitStatus() == QProcess::NormalExit &&
           process.exitCode() == 0;
}

QMap<QString, QVariant> settingsValues(QSettings &settings)
{
    QMap<QString, QVariant> values;
    settings.sync();
    for (const QString &key : settings.allKeys())
        values.insert(key, settings.value(key));
    return values;
}

} // namespace

int main(int argc, char **argv)
{
    QStandardPaths::setTestModeEnabled(true);
    QCoreApplication app(argc, argv);
    app.setOrganizationName(QStringLiteral("ForkMeshTests"));
    app.setApplicationName(QStringLiteral("control-node-security"));

    forkmesh::control::CloudflareBootstrapRequest request;
    request.hostname = QStringLiteral("mirror.example.com");
    request.zoneName = QStringLiteral("example.com");
    request.accountId = QStringLiteral("account_123");
    request.nodeName = QStringLiteral("alice-mirror");
    request.relayLabel = QStringLiteral("Alice mirror");
    request.mainRelayUrl = QStringLiteral("https://forkmesh.com");
    check(forkmesh::control::validateCloudflareBootstrapRequest(request).isEmpty(),
          "valid public Cloudflare fields are accepted");

    auto invalid = request;
    invalid.hostname = QStringLiteral("https://mirror.example.com/private");
    check(!forkmesh::control::validateCloudflareBootstrapRequest(invalid).isEmpty(),
          "URL-shaped hostname is rejected");
    invalid = request;
    invalid.hostname = QStringLiteral("mirror.other.example");
    check(!forkmesh::control::validateCloudflareBootstrapRequest(invalid).isEmpty(),
          "hostname outside the zone is rejected");
    invalid = request;
    invalid.mainRelayUrl =
        QStringLiteral("https://user:pass@forkmesh.com/private?token=x");
    check(!forkmesh::control::validateCloudflareBootstrapRequest(invalid).isEmpty(),
          "credentialed/private upstream URL is rejected");
    forkmesh::control::CloudflareBootstrapRequest automaticRequest;
    automaticRequest.mainRelayUrl = QStringLiteral("https://forkmesh.com");
    check(forkmesh::control::validateCloudflareBootstrapRequest(
              automaticRequest, true).isEmpty(),
          "empty topology is accepted only for fail-closed token discovery");
    check(!forkmesh::control::validateCloudflareBootstrapRequest(
               automaticRequest).isEmpty(),
          "empty topology is rejected outside automatic discovery");

    const QString token =
        QStringLiteral("cloudflare-token-that-must-never-enter-argv");
    const auto command =
        forkmesh::control::buildCloudflareBootstrapCommand(
            request, token, QStringLiteral("/repo/tools/cloudflare_bootstrap.py"),
            QStringLiteral("/usr/bin/python3"),
            QStringLiteral("/opt/Fork Mesh/forkmesh"),
            base64Url(QByteArray(32, 'P')));
    check(command.program == QStringLiteral("/usr/bin/python3"),
          "python program is preserved");
    check(!command.arguments.join(QChar(u'\0')).contains(token),
          "Cloudflare token is absent from argv");
    check(command.environment.value(QStringLiteral("CLOUDFLARE_API_TOKEN")) ==
              token,
          "Cloudflare token is passed through the child environment");
    check(!command.arguments.contains(QStringLiteral("--secret-env")),
          "desktop launcher never uploads a Worker secret");
    check(command.arguments.contains(QStringLiteral("--json-stdout")),
          "desktop requests a bounded machine-readable deployment result");
    check(command.arguments.contains(QStringLiteral("--mirror-public-key")),
          "desktop identity public key is passed to manifest bootstrap");
    check(command.arguments.contains(
              QStringLiteral("--manifest-signer-command")),
          "local external manifest signer is configured");
    check(!command.arguments.contains(QStringLiteral("--skip-mirror-manifest")),
          "valid local identity does not skip the signed mirror manifest");
    const int signerIndex =
        command.arguments.indexOf(QStringLiteral("--manifest-signer-command"));
    check(signerIndex >= 0 &&
              command.arguments.value(signerIndex + 1).contains(
                  QStringLiteral("--sign-mirror-manifest")),
          "signer command invokes the dedicated local signer mode");

    const auto automaticCommand =
        forkmesh::control::buildCloudflareBootstrapCommand(
            automaticRequest, token,
            QStringLiteral("/repo/tools/cloudflare_bootstrap.py"),
            QStringLiteral("/usr/bin/python3"), QString(), QString());
    check(automaticCommand.arguments.contains(
              QStringLiteral("--auto-configure")) &&
              !automaticCommand.arguments.contains(
                  QStringLiteral("--hostname")) &&
              !automaticCommand.arguments.contains(
                  QStringLiteral("--zone")),
          "token-only command discovers topology without empty hostname arguments");
    check(!automaticCommand.arguments.join(QChar(u'\0')).contains(token),
          "token-only discovery also keeps the API token out of argv");

    const QJsonObject bootstrapResult{
        {QStringLiteral("ok"), true},
        {QStringLiteral("hostname"),
         QStringLiteral("forkmesh.example.com")},
        {QStringLiteral("zoneName"), QStringLiteral("example.com")},
        {QStringLiteral("accountId"), QStringLiteral("account-1")},
        {QStringLiteral("nodeName"), QStringLiteral("forkmesh-node")},
        {QStringLiteral("directMirrorHostname"),
         QStringLiteral("mirror.example.com")},
    };
    const QByteArray encodedResult =
        QJsonDocument(bootstrapResult)
            .toJson(QJsonDocument::Compact)
            .toBase64(QByteArray::Base64UrlEncoding |
                      QByteArray::OmitTrailingEquals);
    QString resultError;
    const QJsonObject parsedResult =
        forkmesh::control::parseCloudflareBootstrapResult(
            QByteArrayLiteral("human progress\nFORKMESH_BOOTSTRAP_RESULT=") +
                encodedResult + QByteArrayLiteral("\n"),
            &resultError);
    check(parsedResult.value(QStringLiteral("hostname")).toString() ==
              QStringLiteral("forkmesh.example.com") &&
              resultError.isEmpty(),
          "desktop decodes one bounded successful bootstrap result");
    check(forkmesh::control::parseCloudflareBootstrapResult(
              QByteArrayLiteral("FORKMESH_BOOTSTRAP_RESULT=bad\n"
                                "FORKMESH_BOOTSTRAP_RESULT=bad\n"),
              &resultError).isEmpty(),
          "duplicate machine results fail closed");

    const auto tunnelCommand =
        forkmesh::control::buildCloudflareTunnelBootstrapCommand(
            request, QStringLiteral("direct.example.com"), token,
            QStringLiteral("/repo/tools/cloudflare_tunnel_bootstrap.py"),
            QStringLiteral("/usr/bin/python3"),
            QStringLiteral("/opt/Fork Mesh/forkmesh"),
            base64Url(QByteArray(32, 'P')),
            QStringLiteral("/state/gateway/config.json"),
            QStringLiteral("/state/gateway/forkmesh-mirror.json"),
            QStringLiteral("/state/gateway/connector.token"));
    check(!tunnelCommand.arguments.join(QChar(u'\0')).contains(token) &&
              tunnelCommand.environment.value(
                  QStringLiteral("CLOUDFLARE_API_TOKEN")) == token,
          "Tunnel provisioning keeps the Cloudflare API token out of argv");
    check(tunnelCommand.arguments.contains(
              QStringLiteral("--tunnel-token-file")) &&
              tunnelCommand.arguments.contains(
                  QStringLiteral("--manifest-output")) &&
              tunnelCommand.arguments.contains(
                  QStringLiteral("--gateway-config")),
          "Tunnel command provisions restartable local connector state and a signed manifest");
    check(tunnelCommand.arguments.contains(
              QStringLiteral("direct.example.com")),
          "Tunnel command uses a distinct direct mirror hostname");

    const QString redacted = forkmesh::control::redactProcessOutput(
        QStringLiteral("token=%1\nAuthorization: Bearer abcdefghijk\n")
            .arg(token),
        {token});
    check(!redacted.contains(token), "exact API token is redacted from output");
    check(!redacted.contains(QStringLiteral("abcdefghijk")),
          "Bearer credential is redacted from output");
    check(redacted.contains(QStringLiteral("<redacted>")),
          "redaction marker is visible");

    check(forkmesh::control::isValidSolanaPublicAddress(
              QStringLiteral("11111111111111111111111111111111")),
          "32-byte base58 Solana public address is accepted");
    check(forkmesh::control::isValidSolanaPublicAddress(
              QStringLiteral("So11111111111111111111111111111111111111112")),
          "wrapped-SOL public address is accepted");
    check(!forkmesh::control::isValidSolanaPublicAddress(
              QStringLiteral("seed phrase words are not an address")),
          "seed phrase cannot be accepted as a public address");
    check(!forkmesh::control::isValidSolanaPublicAddress(
              QStringLiteral("O0Il")),
          "non-base58 private-looking input is rejected");

    const QUrl world = forkmesh::control::worldUrlForRelay(
        QStringLiteral("wss://relay.example.com/api/room?secret=no"));
    check(world.toString() == QStringLiteral("https://relay.example.com/world/"),
          "websocket relay becomes the HTTPS World portal");

    check(forkmesh::control::worldDevServerUrl(QString()).toString() ==
              QStringLiteral("http://127.0.0.1:8788/world/"),
          "empty World dev setting yields the default local dev URL");
    check(forkmesh::control::worldDevServerUrl(
              QStringLiteral("localhost:9099")).toString() ==
              QStringLiteral("http://localhost:9099/world/"),
          "bare host:port World dev setting is normalized to /world/");
    check(!forkmesh::control::worldDevServerUrl(QStringLiteral("OFF")).isValid(),
          "\"off\" disables the World dev probe");
    check(!forkmesh::control::worldDevServerUrl(
               QStringLiteral("http://evil.example.com/world/")).isValid(),
          "non-loopback World dev URL fails closed");
    check(!forkmesh::control::worldDevServerUrl(
               QStringLiteral("https://127.0.0.1:8788/")).isValid(),
          "the local World dev probe stays plain http");

    const QStringList mirrorOwners =
        forkmesh::control::directMirrorRepositoryOwners(
            QStringLiteral("forkmesh"), QStringLiteral("jett"));
    check(mirrorOwners ==
              QStringList{QStringLiteral("forkmesh"),
                          QStringLiteral("jett")},
          "direct mirror exposes canonical and catalog owner aliases");
    check(forkmesh::control::directMirrorRepositoryOwners(
              QStringLiteral("ForkMesh"), QStringLiteral("forkmesh")) ==
              QStringList{QStringLiteral("forkmesh")},
          "identical normalized direct mirror aliases are emitted once");
    check(forkmesh::control::directMirrorRepositoryOwners(
              QStringLiteral("../private"), QStringLiteral("jett")) ==
              QStringList{QStringLiteral("jett")},
          "invalid direct mirror owner aliases fail closed");

    const QString publicKey = base64Url(QByteArray(32, 'K'));
    const QByteArray payload =
        QJsonDocument(QJsonObject{
                          {QStringLiteral("type"),
                           QStringLiteral("forkmesh.mirror-endpoint")},
                          {QStringLiteral("schemaVersion"), 1},
                      })
            .toJson(QJsonDocument::Compact);
    const QString digest = QString::fromLatin1(
        QCryptographicHash::hash(payload, QCryptographicHash::Sha256).toHex());
    QJsonObject signingRequest{
        {QStringLiteral("schemaVersion"), 1},
        {QStringLiteral("type"),
         QStringLiteral("forkmesh.mirror-endpoint-signing-request")},
        {QStringLiteral("algorithm"), QStringLiteral("Ed25519")},
        {QStringLiteral("encoding"), QStringLiteral("base64url-no-padding")},
        {QStringLiteral("canonicalization"),
         QStringLiteral("forkmesh-json-sort-v1")},
        {QStringLiteral("publicKey"), publicKey},
        {QStringLiteral("payloadBase64"), base64Url(payload)},
        {QStringLiteral("payloadSha256"), digest},
    };
    QString signingError;
    check(forkmesh::control::mirrorManifestSigningPayload(
              signingRequest, publicKey, &signingError) == payload,
          "valid manifest signing request yields exact canonical payload");
    signingRequest.insert(QStringLiteral("payloadSha256"),
                          QString(64, QLatin1Char('0')));
    check(forkmesh::control::mirrorManifestSigningPayload(
              signingRequest, publicKey, &signingError).isEmpty(),
          "manifest checksum mismatch is rejected");
    signingRequest.insert(QStringLiteral("payloadSha256"), digest);
    check(forkmesh::control::mirrorManifestSigningPayload(
              signingRequest, base64Url(QByteArray(32, 'Z')),
              &signingError).isEmpty(),
          "manifest request for another identity is rejected");

    const QJsonObject canonicalVector{
        {QStringLiteral("z"),
         QString::fromUtf8("snowman \xE2\x98\x83 and \xF0\x9F\x98\x80")},
        {QStringLiteral("a"),
         QJsonArray{0, true, QStringLiteral("line\n")}},
    };
    const QByteArray expectedCanonical =
        QByteArrayLiteral(
            "{\"a\":[0,true,\"line\\n\"],\"z\":\"snowman "
            "\\u2603 and \\ud83d\\ude00\"}");
    check(forkmesh::control::pythonCanonicalJson(
              canonicalVector, &signingError) == expectedCanonical,
          "catalog canonical JSON matches Python sorted ensure-ascii encoding");
    check(forkmesh::control::catalogV2SigningPayload(
              canonicalVector, &signingError) ==
              QByteArrayLiteral(
                  "forkmesh-catalog-v2\n"
                  "2a5d6a869bf7835724b35f3a16660dfc8f683adcc60ca2b367cd669d7fc03e97"),
          "catalog-v2 signing payload matches the cross-language hash vector");

    const QJsonObject boundedTelemetry =
        forkmesh::control::normalizedCatalogHostTelemetry(
            QJsonObject{
                {QStringLiteral("cpuPercent"), 149},
                {QStringLiteral("memUsedBytes"), 900},
                {QStringLiteral("memTotalBytes"), 800},
                {QStringLiteral("diskUsedBytes"), 300},
                {QStringLiteral("diskTotalBytes"), 1000},
            });
    check(boundedTelemetry.value(QStringLiteral("cpuPercent")).toInt() == 100 &&
              boundedTelemetry.value(QStringLiteral("memUsedBytes")).toInt() == 800 &&
              boundedTelemetry.value(QStringLiteral("memTotalBytes")).toInt() == 800 &&
              boundedTelemetry.value(QStringLiteral("diskUsedBytes")).toInt() == 300 &&
              boundedTelemetry.value(QStringLiteral("diskTotalBytes")).toInt() == 1000,
          "catalog telemetry is bounded before canonical signing");
    const QJsonObject unknownTelemetry =
        forkmesh::control::normalizedCatalogHostTelemetry(
            QJsonObject{
                {QStringLiteral("cpuPercent"), -1},
                {QStringLiteral("memUsedBytes"), 100},
                {QStringLiteral("diskUsedBytes"), 10},
                {QStringLiteral("diskTotalBytes"), 0},
            });
    check(unknownTelemetry.value(QStringLiteral("cpuPercent")).isNull() &&
              unknownTelemetry.value(QStringLiteral("memUsedBytes")).isNull() &&
              unknownTelemetry.value(QStringLiteral("memTotalBytes")).isNull() &&
              unknownTelemetry.value(QStringLiteral("diskUsedBytes")).isNull() &&
              unknownTelemetry.value(QStringLiteral("diskTotalBytes")).isNull(),
          "unshared or partial catalog telemetry stays null");

    const QByteArray routePayload =
        forkmesh::control::privateReplicaRouteSigningPayload(
            QStringLiteral("alice"), QStringLiteral("private-project"),
            QStringLiteral("alice-mirror"), QString(64, QLatin1Char('a')),
            QString(64, QLatin1Char('b')), 3, true, 1784800000000LL,
            &signingError);
    check(routePayload ==
              QByteArrayLiteral(
                  "forkmesh-private-route-v1\nalice\nprivate-project\n"
                  "alice-mirror\n"
                  "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\n"
                  "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb\n"
                  "3\n1\n1784800000000"),
          "private-replica route signing canonical matches Worker contract");
    check(forkmesh::control::privateReplicaRouteSigningPayload(
              QStringLiteral("alice\nmallory"),
              QStringLiteral("private-project"),
              QStringLiteral("alice-mirror"), QString(64, QLatin1Char('a')),
              QString(64, QLatin1Char('b')), 3, true, 1784800000000LL,
              &signingError).isEmpty(),
          "private route canonical rejects delimiter injection");

    check(forkmesh::control::httpsMirrorRegistrationSigningPayload(
              QStringLiteral("alice-mirror"),
              QStringLiteral("https://direct.example.com"),
              publicKey, 1784800000000LL, &signingError) ==
              QByteArrayLiteral(
                  "forkmesh-https-endpoint-v1\nalice-mirror\n"
                  "https://direct.example.com\n") +
                  publicKey.toUtf8() +
                  QByteArrayLiteral("\n1784800000000"),
          "direct endpoint registration canonical matches Worker contract");
    check(forkmesh::control::httpsMirrorRegistrationSigningPayload(
              QStringLiteral("alice-mirror"),
              QStringLiteral("http://direct.example.com/private"),
              publicKey, 1784800000000LL, &signingError).isEmpty(),
          "direct endpoint registration rejects non-HTTPS/private origins");

    QTemporaryDir root;
    check(root.isValid(), "temporary source tree exists");
    const QString client = root.filePath(QStringLiteral("qt_client"));
    const QString tools = root.filePath(QStringLiteral("tools"));
    const QString worker =
        root.filePath(QStringLiteral("cloudflare_worker"));
    check(QDir().mkpath(client) && QDir().mkpath(tools) &&
              QDir().mkpath(worker + QStringLiteral("/tools")) &&
              QDir().mkpath(worker + QStringLiteral("/src")) &&
              QDir().mkpath(worker + QStringLiteral("/public")) &&
              QDir().mkpath(worker + QStringLiteral("/migrations")),
          "temporary source directories created");
    const auto writeFixture = [](const QString &path,
                                 const QByteArray &contents) {
        QFile file(path);
        const bool opened =
            file.open(QIODevice::WriteOnly | QIODevice::Truncate);
        check(opened, "temporary deployment fixture created");
        if (opened)
            file.write(contents);
        file.close();
    };
    writeFixture(worker + QStringLiteral("/wrangler.toml"), "[vars]\n");
    writeFixture(worker + QStringLiteral("/pywrangler.sh"), "# pinned\n");
    writeFixture(worker + QStringLiteral("/pyproject.toml"), "[project]\n");
    writeFixture(worker + QStringLiteral("/tools/build_dashboard_assets.py"),
                 "# build\n");
    writeFixture(worker + QStringLiteral("/src/entry.py"), "# worker\n");
    QFile script(root.filePath(
        QStringLiteral("tools/cloudflare_bootstrap.py")));
    check(script.open(QIODevice::WriteOnly | QIODevice::Truncate),
          "temporary bootstrap script created");
    script.write("#!/usr/bin/env python3\n");
    script.close();
    check(forkmesh::control::findCloudflareBootstrapScript(client, QString()) ==
              QFileInfo(script).canonicalFilePath(),
          "repository-pinned bootstrap script resolves from Qt source dir");
    check(forkmesh::control::findCloudflareWorkerDirectory(client, QString()) ==
              QFileInfo(worker).canonicalFilePath(),
          "repository-pinned Worker directory resolves with the bootstrapper");
    QFile tunnelScript(root.filePath(
        QStringLiteral("tools/cloudflare_tunnel_bootstrap.py")));
    QFile gatewayScript(root.filePath(
        QStringLiteral("tools/mirror_gateway.py")));
    check(tunnelScript.open(QIODevice::WriteOnly | QIODevice::Truncate) &&
              gatewayScript.open(QIODevice::WriteOnly |
                                 QIODevice::Truncate),
          "direct mirror tools created");
    tunnelScript.write("#!/usr/bin/env python3\n");
    gatewayScript.write("#!/usr/bin/env python3\n");
    tunnelScript.close();
    gatewayScript.close();
    check(forkmesh::control::findCloudflareTunnelBootstrapScript(
              client, QString()) ==
              QFileInfo(tunnelScript).canonicalFilePath() &&
              forkmesh::control::findMirrorGatewayScript(
                  client, QString()) ==
              QFileInfo(gatewayScript).canonicalFilePath(),
          "repository-pinned Tunnel and gateway tools resolve independently");
    QFile installerScript(root.filePath(
        QStringLiteral("tools/cloudflared_install.py")));
    check(installerScript.open(
              QIODevice::WriteOnly | QIODevice::Truncate),
          "managed cloudflared installer fixture created");
    installerScript.write("#!/usr/bin/env python3\n");
    installerScript.close();
    check(forkmesh::control::findCloudflaredInstallerScript(
              client, QString()) ==
              QFileInfo(installerScript).canonicalFilePath(),
          "repository-pinned managed cloudflared installer resolves");

    QTemporaryDir installedRoot;
    check(installedRoot.isValid(), "temporary installed layout exists");
    const QString installedBin =
        installedRoot.filePath(QStringLiteral("bin"));
    const QString installedResources =
        installedRoot.filePath(QStringLiteral("share/forkmesh"));
    const QString installedTools =
        installedResources + QStringLiteral("/tools");
    const QString installedWorker =
        installedResources + QStringLiteral("/cloudflare_worker");
    check(QDir().mkpath(installedBin) &&
              QDir().mkpath(installedTools) &&
              QDir().mkpath(installedWorker + QStringLiteral("/tools")) &&
              QDir().mkpath(installedWorker + QStringLiteral("/src")) &&
              QDir().mkpath(installedWorker + QStringLiteral("/public")) &&
              QDir().mkpath(installedWorker + QStringLiteral("/migrations")),
          "CMake-style installed resource directories created");
    writeFixture(installedTools + QStringLiteral("/cloudflare_bootstrap.py"),
                 "#!/usr/bin/env python3\n");
    writeFixture(
        installedTools + QStringLiteral("/cloudflare_tunnel_bootstrap.py"),
        "#!/usr/bin/env python3\n");
    writeFixture(installedTools + QStringLiteral("/mirror_gateway.py"),
                 "#!/usr/bin/env python3\n");
    writeFixture(installedTools + QStringLiteral("/cloudflared_install.py"),
                 "#!/usr/bin/env python3\n");
    writeFixture(installedWorker + QStringLiteral("/wrangler.toml"),
                 "[vars]\n");
    writeFixture(installedWorker + QStringLiteral("/pywrangler.sh"),
                 "# pinned\n");
    writeFixture(installedWorker + QStringLiteral("/pyproject.toml"),
                 "[project]\n");
    writeFixture(
        installedWorker + QStringLiteral("/tools/build_dashboard_assets.py"),
        "# build\n");
    writeFixture(installedWorker + QStringLiteral("/src/entry.py"),
                 "# worker\n");
    const QString installedBootstrap =
        forkmesh::control::findCloudflareBootstrapScript(
            QString(), installedBin);
    check(installedBootstrap ==
              QFileInfo(installedTools +
                        QStringLiteral("/cloudflare_bootstrap.py"))
                  .canonicalFilePath(),
          "bootstrap resolves from the Linux CMake installed layout without a source checkout");
    check(forkmesh::control::findCloudflareWorkerDirectory(
              QString(), installedBin) ==
              QFileInfo(installedWorker).canonicalFilePath(),
          "installed Worker directory resolves with the bootstrapper");
    check(forkmesh::control::findCloudflareTunnelBootstrapScript(
              QString(), installedBin) ==
                  QFileInfo(
                      installedTools +
                      QStringLiteral("/cloudflare_tunnel_bootstrap.py"))
                      .canonicalFilePath() &&
              forkmesh::control::findMirrorGatewayScript(
                  QString(), installedBin) ==
                  QFileInfo(installedTools +
                            QStringLiteral("/mirror_gateway.py"))
                      .canonicalFilePath() &&
              forkmesh::control::findCloudflaredInstallerScript(
                  QString(), installedBin) ==
                  QFileInfo(installedTools +
                            QStringLiteral("/cloudflared_install.py"))
                      .canonicalFilePath(),
          "all direct-mirror tools resolve from installed resources");

    QFile::remove(
        installedWorker + QStringLiteral("/src/entry.py"));
    check(forkmesh::control::findCloudflareBootstrapScript(
              QString(), installedBin).isEmpty(),
          "incomplete installed Worker bundle fails closed");
    check(forkmesh::control::findCloudflareWorkerDirectory(
              QString(), installedBin).isEmpty(),
          "incomplete installed Worker directory fails closed");

    const QString tailToken =
        QStringLiteral("cf-tail-token-must-never-enter-argv");
    const auto tailCommand =
        forkmesh::control::buildCloudflareTailCommand(
            tailToken, QStringLiteral("account_123"),
            QStringLiteral("/usr/bin/npx"));
    check(tailCommand.program == QStringLiteral("/usr/bin/npx") &&
              tailCommand.arguments ==
                  QStringList({QStringLiteral("--yes"),
                               QStringLiteral("wrangler@4.42.1"),
                               QStringLiteral("tail"),
                               QStringLiteral("--format"),
                               QStringLiteral("pretty")}),
          "Cloudflare tail uses a direct pinned Wrangler invocation");
    check(!tailCommand.arguments.join(QChar(u'\0')).contains(tailToken) &&
              tailCommand.environment.value(
                  QStringLiteral("CLOUDFLARE_API_TOKEN")) == tailToken &&
              tailCommand.environment.value(
                  QStringLiteral("CLOUDFLARE_ACCOUNT_ID")) ==
                  QStringLiteral("account_123"),
          "Cloudflare tail keeps credentials only in the child environment");
    check(forkmesh::control::buildCloudflareTailCommand(
              tailToken, QStringLiteral("invalid account!"),
              QStringLiteral("/usr/bin/npx")).program.isEmpty(),
          "Cloudflare tail rejects malformed account IDs");

    const QMap<QString, QString> storedVariables = {
        {QStringLiteral("cloudflare_api_token"), QStringLiteral("  cf-stored  ")},
        {QStringLiteral("CF_TOKEN"), QStringLiteral("cf-secondary")},
        {QStringLiteral("CLOUDFLARE_ACCOUNT_ID"),
         QStringLiteral("account_from_secrets")},
        {QStringLiteral("PUBLIC_MODE"), QStringLiteral("production")},
    };
    check(forkmesh::control::cloudflareApiTokenFromVariables(
              storedVariables) == QStringLiteral("cf-stored") &&
              forkmesh::control::cloudflareAccountIdFromVariables(
                  storedVariables) ==
                  QStringLiteral("account_from_secrets"),
          "stored action secrets supply the Cloudflare token and account");
    check(forkmesh::control::cloudflareApiTokenFromVariables(
              {{QStringLiteral("CLOUDFLARE_API_TOKEN"),
                QStringLiteral("   ")},
               {QStringLiteral("CF_API_TOKEN"),
                QStringLiteral("cf-fallback")}}) ==
              QStringLiteral("cf-fallback"),
          "blank stored tokens fall through to the next known secret name");
    check(forkmesh::control::cloudflareApiTokenFromVariables(
              {{QStringLiteral("CLOUDFLARE_API_TOKEN"),
                QStringLiteral("cf-line\nCLOUDFLARE_EMAIL=x")}})
              .isEmpty() &&
              forkmesh::control::cloudflareApiTokenFromVariables({}).isEmpty(),
          "multi-line and missing stored tokens are ignored");

    forkmesh::control::MirrorActionsConfigurationRequest actionsRequest;
    actionsRequest.requestId =
        QStringLiteral("0123456789abcdef0123456789abcdef");
    actionsRequest.host = QStringLiteral("mirror2.example.com");
    actionsRequest.sshUser = QStringLiteral("forkmesh");
    actionsRequest.nodeName = QStringLiteral("mirror2");
    actionsRequest.actionsEnabled = true;
    actionsRequest.replaceVariables = true;
    actionsRequest.variables = {
        {QStringLiteral("DEPLOY_TOKEN"),
         QStringLiteral("stdin-only-action-secret")},
        {QStringLiteral("PUBLIC_MODE"), QStringLiteral("production")},
    };
    check(forkmesh::control::validateMirrorActionsConfigurationRequest(
              actionsRequest).isEmpty(),
          "bounded mirror Actions configuration is accepted");
    QString actionsError;
    const QByteArray actionsPayload =
        forkmesh::control::buildMirrorActionsConfigurationPayload(
            actionsRequest, &actionsError);
    const QJsonObject actionsJson =
        QJsonDocument::fromJson(actionsPayload).object();
    check(actionsError.isEmpty() &&
              actionsPayload.size() <= 64 * 1024 &&
              actionsJson.value(QStringLiteral("type")).toString() ==
                  QStringLiteral("forkmesh.mirror-actions-configuration") &&
              actionsJson.value(QStringLiteral("actionsEnabled")).toBool() &&
              actionsJson.value(QStringLiteral("variables"))
                      .toObject()
                      .value(QStringLiteral("values"))
                      .toObject()
                      .value(QStringLiteral("DEPLOY_TOKEN"))
                      .toString() ==
                  QStringLiteral("stdin-only-action-secret"),
          "mirror Actions request serializes only into a bounded stdin document");
    auto stateOnlyActions = actionsRequest;
    stateOnlyActions.replaceVariables = false;
    stateOnlyActions.variables.clear();
    const QJsonObject stateOnlyJson =
        QJsonDocument::fromJson(
            forkmesh::control::buildMirrorActionsConfigurationPayload(
                stateOnlyActions, &actionsError))
            .object();
    check(!stateOnlyJson.contains(QStringLiteral("variables")),
          "state-only Actions request cannot clear or disclose remote variables");

    const QString sshPassword =
        QStringLiteral("ssh-password-that-must-never-enter-argv");
    const auto actionsPasswordCommand =
        forkmesh::control::buildMirrorActionsSshCommand(
            actionsRequest, sshPassword, &actionsError);
    const QString actionsArgv =
        actionsPasswordCommand.arguments.join(QLatin1Char(' '));
    check(actionsPasswordCommand.program == QStringLiteral("sshpass") &&
              actionsPasswordCommand.environment.value(
                  QStringLiteral("SSHPASS")) == sshPassword &&
              actionsPasswordCommand.standardInput == actionsPayload,
          "password-authenticated Actions transport uses environment plus stdin");
    check(!actionsArgv.contains(sshPassword) &&
              !actionsArgv.contains(
                  QStringLiteral("stdin-only-action-secret")) &&
              !actionsArgv.contains(QStringLiteral("DEPLOY_TOKEN")) &&
              !actionsArgv.contains(QStringLiteral("production")),
          "Actions password, variable names, and values are absent from argv");
    check(actionsArgv.contains(
              QStringLiteral("--configure-mirror-actions-stdin")) &&
              actionsArgv.contains(
                  QStringLiteral("StrictHostKeyChecking=accept-new")) &&
              actionsArgv.contains(
                  QStringLiteral("PreferredAuthentications=publickey,password")) &&
              actionsArgv.contains(QStringLiteral("UserKnownHostsFile=")) &&
              !actionsPasswordCommand.arguments.contains(
                  QStringLiteral("UserKnownHostsFile=/dev/null")) &&
              !actionsArgv.contains(
                  QStringLiteral("PubkeyAuthentication=no")),
          "Actions transport invokes only the fixed helper, persists TOFU keys, and prefers public-key auth");

    const auto actionsKeyCommand =
        forkmesh::control::buildMirrorActionsSshCommand(
            actionsRequest, QString(), &actionsError);
    check(actionsKeyCommand.program == QStringLiteral("ssh") &&
              actionsKeyCommand.arguments.contains(
                  QStringLiteral("BatchMode=yes")) &&
              actionsKeyCommand.arguments.contains(
                  QStringLiteral("PreferredAuthentications=publickey")) &&
              actionsKeyCommand.arguments.contains(
                  QStringLiteral("HashKnownHosts=yes")) &&
              actionsKeyCommand.arguments.contains(
                  QStringLiteral("UpdateHostKeys=yes")) &&
              !actionsKeyCommand.arguments.contains(
                  QStringLiteral("IdentitiesOnly=yes")) &&
              !actionsKeyCommand.environment.contains(
                  QStringLiteral("SSHPASS")),
          "empty password selects non-interactive SSH key authentication");

    const auto genericKeyCommand =
        forkmesh::control::buildHostSshCommand(
            QStringLiteral("mirror.example.test"),
            QStringLiteral("forkmesh"), QString(),
            QStringLiteral("exec tail -f node.log"), &actionsError);
    const QString genericKeyArgv =
        genericKeyCommand.arguments.join(QChar(u'\0'));
    check(actionsError.isEmpty() &&
              genericKeyCommand.program == QStringLiteral("ssh") &&
              genericKeyArgv.contains(
                  QStringLiteral("StrictHostKeyChecking=accept-new")) &&
              genericKeyArgv.contains(QStringLiteral("UserKnownHostsFile=")) &&
              !genericKeyArgv.contains(QStringLiteral("/dev/null")) &&
              !genericKeyArgv.contains(
                  QStringLiteral("StrictHostKeyChecking=no")),
          "all controller host operations use a persistent fail-closed TOFU trust store");
    check(forkmesh::control::buildHostSshCommand(
              QStringLiteral("-oProxyCommand=bad"),
              QStringLiteral("forkmesh"), QString(),
              QStringLiteral("true"), &actionsError)
              .program.isEmpty(),
          "generic controller SSH rejects option-shaped hosts");

    // Managed identity keys (adhoc #315): a recorded key file pins
    // authentication to exactly that key; a missing file fails closed instead
    // of silently falling back to the user's default keys.
    QTemporaryDir identityDir;
    const QString identityPath =
        identityDir.filePath(QStringLiteral("vultr_mirror_ed25519"));
    {
        QFile identity(identityPath);
        check(identity.open(QIODevice::WriteOnly) &&
                  identity.write("managed-key-material") > 0,
              "managed identity fixture is writable");
    }
    const auto identityCommand = forkmesh::control::buildHostSshCommand(
        QStringLiteral("203.0.113.10"), QStringLiteral("root"), QString(),
        QStringLiteral("true"), &actionsError, identityPath);
    check(actionsError.isEmpty() &&
              identityCommand.program == QStringLiteral("ssh") &&
              identityCommand.arguments.contains(QStringLiteral("-i")) &&
              identityCommand.arguments.contains(identityPath) &&
              identityCommand.arguments.contains(
                  QStringLiteral("IdentitiesOnly=yes")),
          "a managed identity file is pinned with -i and IdentitiesOnly");
    check(forkmesh::control::buildHostSshCommand(
              QStringLiteral("203.0.113.10"), QStringLiteral("root"),
              QString(), QStringLiteral("true"), &actionsError,
              identityDir.filePath(QStringLiteral("missing_key")))
              .program.isEmpty(),
          "a missing managed identity file fails closed");

    // Provider sign-in terminal (adhoc #422): the agent-CLI installer copies no
    // tokens, so ForkMesh opens a real interactive shell on the mirror right
    // after it finishes. That needs a forced remote TTY and a shell-safe
    // command line, and the remote command itself must stay credential-free.
    QString loginError;
    const auto loginCommand =
        forkmesh::control::buildHostInteractiveSshCommand(
            QStringLiteral("203.0.113.10"), QStringLiteral("root"), QString(),
            forkmesh::control::buildHostAgentLoginRemoteCommand(), &loginError,
            identityPath);
    check(loginError.isEmpty() &&
              loginCommand.program == QStringLiteral("ssh") &&
              loginCommand.arguments.contains(QStringLiteral("-tt")) &&
              loginCommand.arguments.indexOf(QStringLiteral("-tt")) ==
                  loginCommand.arguments.size() - 3 &&
              loginCommand.arguments.at(loginCommand.arguments.size() - 2) ==
                  QStringLiteral("root@203.0.113.10"),
          "the sign-in shell forces a remote TTY before user@host");
    const QString loginRemote =
        forkmesh::control::buildHostAgentLoginRemoteCommand();
    check(loginRemote.contains(QStringLiteral("$HOME/.local/bin")) &&
              loginRemote.contains(QStringLiteral("codex login")) &&
              loginRemote.contains(QStringLiteral("exec \"${SHELL:-/bin/sh}\"")),
          "the sign-in shell puts the CLI prefixes on PATH and execs a login shell");
    const QString loginLine =
        forkmesh::control::hostSshCommandLine(loginCommand);
    check(loginLine.startsWith(QStringLiteral("'ssh' ")) &&
              loginLine.contains(QStringLiteral("'-tt'")) &&
              loginLine.contains(QStringLiteral("'\\''")),
          "the flattened sign-in command line quotes every word and escapes the "
          "remote command's own quotes");
    check(forkmesh::control::hostSshCommandLine(
              forkmesh::control::buildHostInteractiveSshCommand(
                  QStringLiteral("-oProxyCommand=bad"),
                  QStringLiteral("root"), QString(),
                  QStringLiteral("true"), &loginError))
              .isEmpty(),
          "a rejected sign-in host flattens to no command line at all");

    // Host size map (adhoc #390): a read-only `du` browser over the same
    // authenticated SSH channel.
    check(forkmesh::control::normalizeRemoteDiskPath(
              QStringLiteral("/var//lib/./forkmesh/")) ==
                  QStringLiteral("/var/lib/forkmesh") &&
              forkmesh::control::normalizeRemoteDiskPath(
                  QStringLiteral("/var/lib/..")) == QStringLiteral("/var") &&
              forkmesh::control::normalizeRemoteDiskPath(
                  QStringLiteral("/../..")) == QStringLiteral("/") &&
              forkmesh::control::normalizeRemoteDiskPath(QString()) ==
                  QStringLiteral("/"),
          "remote size-map paths collapse to canonical absolute paths");
    check(forkmesh::control::normalizeRemoteDiskPath(
              QStringLiteral("var/lib")).isEmpty() &&
              forkmesh::control::normalizeRemoteDiskPath(
                  QStringLiteral("/var\nrm -rf /")).isEmpty(),
          "relative and newline-bearing size-map paths are rejected");

    QString diskError;
    const QString diskCommand = forkmesh::control::buildHostDiskUsageCommand(
        QStringLiteral("/srv/it's here"), &diskError);
    check(diskError.isEmpty() &&
              diskCommand.startsWith(QStringLiteral("sh -lc '")) &&
              diskCommand.contains(QStringLiteral("du -x -k -a -d 1")) &&
              diskCommand.contains(QStringLiteral("'\\''")) &&
              !diskCommand.contains(QStringLiteral("rm ")) &&
              !diskCommand.contains(QStringLiteral("chmod")),
          "the size-map command is a single quoted read-only du level");
    check(forkmesh::control::buildHostDiskUsageCommand(
              QStringLiteral("relative/path"), &diskError).isEmpty() &&
              !diskError.isEmpty(),
          "the size-map command refuses a non-absolute path");

    const QByteArray diskOutput =
        QByteArray("Welcome to Ubuntu (banner noise)\n") +
        "FORKMESH-DU1 d 2048 " + QByteArray("/var/log").toBase64() + "\n" +
        "FORKMESH-DU1 f 4 " + QByteArray("/var/notes 'x'.txt").toBase64() + "\n" +
        "FORKMESH-DU1 d 8192 " + QByteArray("/var/lib").toBase64() + "\n" +
        "FORKMESH-DU1 T 10244 " + QByteArray("/var").toBase64() + "\n" +
        "FORKMESH-DU1-END\n";
    const auto usage =
        forkmesh::control::parseHostDiskUsage(diskOutput, QStringLiteral("/var"));
    check(usage.complete && usage.error.isEmpty() &&
              usage.totalBytes == 10244LL * 1024 &&
              usage.entries.size() == 3 &&
              usage.entries.at(0).path == QStringLiteral("/var/lib") &&
              usage.entries.at(0).name == QStringLiteral("lib") &&
              usage.entries.at(0).directory &&
              usage.entries.at(0).bytes == 8192LL * 1024 &&
              usage.entries.at(2).name == QStringLiteral("notes 'x'.txt") &&
              !usage.entries.at(2).directory,
          "the size map parses banner-wrapped du sentinels largest first");
    const auto diskFailure = forkmesh::control::parseHostDiskUsage(
        QByteArray("FORKMESH-DU1-ERROR ") + QByteArray("nope").toBase64() + "\n",
        QStringLiteral("/root"));
    check(diskFailure.complete && diskFailure.error == QStringLiteral("nope") &&
              diskFailure.entries.isEmpty(),
          "a host-side size-map refusal surfaces as an error, not an empty tree");
    check(forkmesh::control::parseHostDiskUsage(
              QByteArray("ssh: connect to host port 22: Connection refused\n"),
              QStringLiteral("/"))
              .complete == false,
          "a transport failure never looks like a complete size map");
    check(forkmesh::control::formatDiskSize(0) == QStringLiteral("0 B") &&
              forkmesh::control::formatDiskSize(1536) ==
                  QStringLiteral("1.5 KB") &&
              forkmesh::control::formatDiskSize(3LL * 1024 * 1024 * 1024) ==
                  QStringLiteral("3.0 GB"),
          "size-map byte formatting is human readable");

    // One-click Vultr provisioning helpers (adhoc #315).
    check(forkmesh::control::validateVultrMirrorRequest(
              QStringLiteral("ABCDEFGHIJKLMNOPQRSTUVWXYZ234567"),
              QStringLiteral("vultr-mirror-1"))
              .isEmpty(),
          "a plausible Vultr key and node name validate");
    check(!forkmesh::control::validateVultrMirrorRequest(
               QStringLiteral("short"), QStringLiteral("vultr-mirror-1"))
               .isEmpty() &&
              !forkmesh::control::validateVultrMirrorRequest(
                   QStringLiteral("ABCDEFGHIJKLMNOPQRSTUVWXYZ234567"),
                   QStringLiteral("Bad Name"))
                   .isEmpty(),
          "Vultr validation rejects malformed keys and node names");

    const QJsonArray vultrPlans{
        QJsonObject{{QStringLiteral("id"), QStringLiteral("vc2-2c-4gb")},
                    {QStringLiteral("monthly_cost"), 20},
                    {QStringLiteral("ram"), 4096},
                    {QStringLiteral("locations"),
                     QJsonArray{QStringLiteral("ewr")}}},
        QJsonObject{{QStringLiteral("id"), QStringLiteral("vc2-1c-1gb")},
                    {QStringLiteral("monthly_cost"), 5},
                    {QStringLiteral("ram"), 1024},
                    {QStringLiteral("locations"),
                     QJsonArray{QStringLiteral("fra"),
                                QStringLiteral("ams")}}},
        // Cheaper but sold out everywhere: not deployable, must be skipped.
        QJsonObject{{QStringLiteral("id"), QStringLiteral("vc2-old")},
                    {QStringLiteral("monthly_cost"), 3},
                    {QStringLiteral("ram"), 512},
                    {QStringLiteral("locations"), QJsonArray{}}},
        // Same price, more RAM: preferred deterministically.
        QJsonObject{{QStringLiteral("id"), QStringLiteral("vhp-1c-2gb")},
                    {QStringLiteral("monthly_cost"), 5},
                    {QStringLiteral("ram"), 2048},
                    {QStringLiteral("locations"),
                     QJsonArray{QStringLiteral("syd")}}},
        // Cheapest of all, but IPv6-only: unreachable for the mesh (adhoc #344).
        QJsonObject{{QStringLiteral("id"), QStringLiteral("vc2-1c-0.5gb-v6")},
                    {QStringLiteral("monthly_cost"), 2.5},
                    {QStringLiteral("ram"), 512},
                    {QStringLiteral("locations"),
                     QJsonArray{QStringLiteral("ewr")}}},
    };
    const QJsonObject cheapest =
        forkmesh::control::cheapestVultrPlan(vultrPlans);
    check(cheapest.value(QStringLiteral("id")).toString() ==
              QStringLiteral("vhp-1c-2gb"),
          "cheapest Vultr plan skips undeployable plans and breaks ties on RAM");
    check(forkmesh::control::vultrPlanHasIpv4(cheapest) &&
              !forkmesh::control::vultrPlanHasIpv4(
                  vultrPlans.at(4).toObject()) &&
              !forkmesh::control::vultrPlanHasIpv4(QJsonObject()),
          "IPv6-only Vultr plans are never eligible, however cheap");
    check(forkmesh::control::vultrPlanRegion(cheapest) ==
              QStringLiteral("syd") &&
              forkmesh::control::vultrPlanRegion(
                  vultrPlans.at(1).toObject()) == QStringLiteral("ams"),
          "Vultr region selection is the plan's first sorted location");
    check(forkmesh::control::cheapestVultrPlan(QJsonArray()).isEmpty(),
          "an empty Vultr plan list yields no selection");

    const QJsonArray vultrOs{
        QJsonObject{{QStringLiteral("id"), 401},
                    {QStringLiteral("name"), QStringLiteral("Debian 11 x64")},
                    {QStringLiteral("arch"), QStringLiteral("x64")},
                    {QStringLiteral("family"), QStringLiteral("debian")}},
        QJsonObject{{QStringLiteral("id"), 477},
                    {QStringLiteral("name"),
                     QStringLiteral("Debian 12 x64 (bookworm)")},
                    {QStringLiteral("arch"), QStringLiteral("x64")},
                    {QStringLiteral("family"), QStringLiteral("debian")}},
        QJsonObject{{QStringLiteral("id"), 999},
                    {QStringLiteral("name"),
                     QStringLiteral("Ubuntu 24.04 LTS x64")},
                    {QStringLiteral("arch"), QStringLiteral("x64")},
                    {QStringLiteral("family"), QStringLiteral("ubuntu")}},
        QJsonObject{{QStringLiteral("id"), 478},
                    {QStringLiteral("name"),
                     QStringLiteral("Debian 12 i386")},
                    {QStringLiteral("arch"), QStringLiteral("i386")},
                    {QStringLiteral("family"), QStringLiteral("debian")}},
    };
    check(forkmesh::control::latestVultrDebianOs(vultrOs)
                  .value(QStringLiteral("id"))
                  .toInt() == 477,
          "latest Vultr Debian selection picks the newest x64 Debian only");

    // A freshly created mirror has nothing mirroring it yet, so the provisioner
    // must direct-upload this app's binary rather than let the installer look
    // for an online mirror — but only when that binary can actually run there.
    check(forkmesh::control::localBinaryRunsOnVultrMirror(
              QStringLiteral("linux"), QStringLiteral("x86_64")),
          "linux/x86_64 desktop uploads its own binary to a new Vultr mirror");
    check(forkmesh::control::localBinaryRunsOnVultrMirror(
              QStringLiteral("Linux"), QStringLiteral("amd64")),
          "amd64/x64 spellings count as x86_64 for the upload decision");
    check(!forkmesh::control::localBinaryRunsOnVultrMirror(
              QStringLiteral("darwin"), QStringLiteral("x86_64")),
          "macOS desktop does not upload a binary the Debian mirror can't run");
    check(!forkmesh::control::localBinaryRunsOnVultrMirror(
              QStringLiteral("linux"), QStringLiteral("arm64")),
          "arm64 desktop does not upload a binary the x64 mirror can't run");

    const QJsonObject instancePayload =
        forkmesh::control::vultrInstanceCreatePayload(
            QStringLiteral("vultr-mirror-1"), QStringLiteral("vhp-1c-2gb"),
            QStringLiteral("syd"), 477, QStringLiteral("key-id-1"));
    check(instancePayload.value(QStringLiteral("plan")).toString() ==
                  QStringLiteral("vhp-1c-2gb") &&
              instancePayload.value(QStringLiteral("region")).toString() ==
                  QStringLiteral("syd") &&
              instancePayload.value(QStringLiteral("os_id")).toInt() == 477 &&
              instancePayload.value(QStringLiteral("sshkey_id")).toArray() ==
                  QJsonArray{QStringLiteral("key-id-1")} &&
              instancePayload.value(QStringLiteral("backups")).toString() ==
                  QStringLiteral("disabled") &&
              instancePayload.value(QStringLiteral("activation_email"))
                      .toBool() == false &&
              instancePayload.value(QStringLiteral("label")).toString() ==
                  QStringLiteral("vultr-mirror-1"),
          "the Vultr instance payload pins plan, region, OS, key and no extras");

    const QJsonObject bootingInstance{
        {QStringLiteral("status"), QStringLiteral("pending")},
        {QStringLiteral("power_status"), QStringLiteral("running")},
        {QStringLiteral("main_ip"), QStringLiteral("0.0.0.0")},
    };
    const QJsonObject readyInstance{
        {QStringLiteral("status"), QStringLiteral("active")},
        {QStringLiteral("power_status"), QStringLiteral("running")},
        {QStringLiteral("main_ip"), QStringLiteral("203.0.113.99")},
    };
    QJsonObject stoppedInstance = readyInstance;
    stoppedInstance.insert(QStringLiteral("power_status"),
                           QStringLiteral("stopped"));
    check(forkmesh::control::vultrInstanceReadyIp(bootingInstance).isEmpty() &&
              forkmesh::control::vultrInstanceReadyIp(stoppedInstance)
                  .isEmpty() &&
              forkmesh::control::vultrInstanceReadyIp(readyInstance) ==
                  QStringLiteral("203.0.113.99"),
          "instance readiness requires active+running and a real IPv4");
    check(instancePayload.value(QStringLiteral("enable_ipv6")).toBool(true) ==
              false,
          "the Vultr instance payload never opts into an IPv6-only address");

    QJsonObject ipv6OnlyInstance = readyInstance;
    ipv6OnlyInstance.insert(QStringLiteral("main_ip"),
                            QStringLiteral("0.0.0.0"));
    ipv6OnlyInstance.insert(QStringLiteral("v6_main_ip"),
                            QStringLiteral("2001:db8::1"));
    QJsonObject dualStackInstance = readyInstance;
    dualStackInstance.insert(QStringLiteral("v6_main_ip"),
                             QStringLiteral("2001:db8::1"));
    check(forkmesh::control::vultrInstanceIsIpv6Only(ipv6OnlyInstance) &&
              !forkmesh::control::vultrInstanceIsIpv6Only(dualStackInstance) &&
              !forkmesh::control::vultrInstanceIsIpv6Only(bootingInstance),
          "an IPv6-only instance is detected instead of polled to a timeout");

    check(forkmesh::control::nextMirrorNodeName(
              {QStringLiteral("mirror1"), QStringLiteral("Mirror4"),
               QStringLiteral("laptop")}) == QStringLiteral("mirror5") &&
              forkmesh::control::nextMirrorNodeName({}) ==
                  QStringLiteral("mirror1") &&
              forkmesh::control::nextMirrorNodeName(
                  {QStringLiteral("mirror2"), QStringLiteral(" mirror3 ")}) ==
                  QStringLiteral("mirror4"),
          "the default mirror name continues the fleet's own numbering");

    QMap<QString, QString> vultrVariables;
    vultrVariables.insert(QStringLiteral("vultr_api_key"),
                          QStringLiteral("STOREDVULTRKEY01234567890"));
    vultrVariables.insert(QStringLiteral("OTHER"),
                          QStringLiteral("unrelated"));
    check(forkmesh::control::vultrApiKeyFromVariables(vultrVariables) ==
                  QStringLiteral("STOREDVULTRKEY01234567890") &&
              forkmesh::control::vultrApiKeyFromVariables({}).isEmpty(),
          "the stored VULTR_API_KEY device variable is resolved case-insensitively");

    // --- Installing a fresh mirror without a published release (adhoc #408) -
    check(forkmesh::control::localBinaryRunsOnVultrMirror(
              QStringLiteral("linux"), QStringLiteral("x86_64")) &&
              forkmesh::control::localBinaryRunsOnVultrMirror(
                  QStringLiteral("Linux"), QStringLiteral("amd64")) &&
              !forkmesh::control::localBinaryRunsOnVultrMirror(
                  QStringLiteral("linux"), QStringLiteral("arm64")) &&
              !forkmesh::control::localBinaryRunsOnVultrMirror(
                  QStringLiteral("darwin"), QStringLiteral("x86_64")) &&
              !forkmesh::control::localBinaryRunsOnVultrMirror(
                  QStringLiteral("winnt"), QStringLiteral("x86_64")),
          "only a linux/x86_64 controller uploads its own binary to the "
          "Debian x64 instance the flow deploys");

    check(forkmesh::control::vultrInstallNeedsLocalBinary(QStringLiteral(
              "Error: No online ForkMesh node is currently mirroring "
              "'forkmesh'.")) &&
              forkmesh::control::vultrInstallNeedsLocalBinary(QStringLiteral(
                  "Error: No prebuilt ForkMesh binary is published for "
                  "linux/x86_64, and falling back to a source build is "
                  "disabled (FORKMESH_NO_SOURCE_FALLBACK=1, the default on "
                  "headless Linux).")) &&
              forkmesh::control::vultrInstallNeedsLocalBinary(QStringLiteral(
                  "Error: No prebuilt ForkMesh release passed independent "
                  "manifest authentication and SHA-256 verification.")) &&
              !forkmesh::control::vultrInstallNeedsLocalBinary(QStringLiteral(
                  "ssh: connect to host 203.0.113.10 port 22: Connection "
                  "refused")),
          "an install that found nothing to download escalates to the "
          "direct upload, an unreachable host does not");

    // --- Agent CLIs on a fresh mirror (adhoc #418) -------------------------
    forkmesh::control::AgentCliCredentials agentCredentials;
    check(forkmesh::control::agentCliCredentialsAreEmpty(agentCredentials) &&
              forkmesh::control::describeAgentCliCredentials(agentCredentials)
                  .isEmpty(),
          "a device with no agent login has nothing to copy to a new mirror");

    agentCredentials.claudeCredentials =
        QByteArrayLiteral("{\"claudeAiOauth\":{\"accessToken\":\"tok-claude\"}}");
    agentCredentials.env.insert(QStringLiteral("OPENAI_API_KEY"),
                                QStringLiteral("sk-it's-quoted"));
    agentCredentials.env.insert(QStringLiteral("bad name"),
                                QStringLiteral("dropped"));
    agentCredentials.env.insert(QStringLiteral("CONTROL_CHARS"),
                                QStringLiteral("line\nbreak"));
    check(!forkmesh::control::agentCliCredentialsAreEmpty(agentCredentials) &&
              forkmesh::control::describeAgentCliCredentials(
                  agentCredentials) ==
                  QStringLiteral("Claude Code login, OPENAI_API_KEY"),
          "the copy summary names the login and the usable keys only");

    const QByteArray envFile =
        forkmesh::control::agentCliEnvFileContents(agentCredentials.env);
    check(envFile ==
              QByteArrayLiteral("export OPENAI_API_KEY='sk-it'\\''s-quoted'\n"),
          "the sourced env file single-quotes values and drops unusable names");

    QString agentPayloadError;
    const QByteArray agentPayload =
        forkmesh::control::buildAgentCliBootstrapPayload(agentCredentials,
                                                         &agentPayloadError);
    check(agentPayloadError.isEmpty() && !agentPayload.isEmpty() &&
              !agentPayload.contains(QByteArrayLiteral("tok-claude")) &&
              !agentPayload.contains(QByteArrayLiteral("sk-it")) &&
              agentPayload.startsWith(QByteArrayLiteral("claude ")) &&
              agentPayload.contains(QByteArrayLiteral("\nenv ")) &&
              !agentPayload.contains(QByteArrayLiteral("codex ")),
          "the stdin payload carries one base64 line per present section");
    QByteArray decodedClaude;
    QByteArray decodedEnv;
    for (const QByteArray &line : agentPayload.split('\n')) {
        const int space = line.indexOf(' ');
        if (space <= 0)
            continue;
        const QByteArray decoded =
            QByteArray::fromBase64(line.mid(space + 1));
        if (line.startsWith(QByteArrayLiteral("claude ")))
            decodedClaude = decoded;
        else if (line.startsWith(QByteArrayLiteral("env ")))
            decodedEnv = decoded;
    }
    check(decodedClaude == agentCredentials.claudeCredentials.trimmed() &&
              decodedEnv == envFile,
          "each payload section round-trips to the exact file the mirror writes");

    forkmesh::control::AgentCliCredentials malformedCredentials;
    malformedCredentials.codexAuth = QByteArrayLiteral("not json");
    QString malformedError;
    check(forkmesh::control::buildAgentCliBootstrapPayload(
              malformedCredentials, &malformedError).isEmpty() &&
              !malformedError.isEmpty(),
          "a login file that is not a credential document is never sent");
    QString emptyError;
    check(forkmesh::control::buildAgentCliBootstrapPayload(
              forkmesh::control::AgentCliCredentials{}, &emptyError)
                  .isEmpty() &&
              !emptyError.isEmpty(),
          "an empty bundle is refused rather than sent as a blank payload");

    const QString agentInstallOnly =
        forkmesh::control::agentCliBootstrapRemoteCommand(false);
    const QString agentWithLogins =
        forkmesh::control::agentCliBootstrapRemoteCommand(true);
    check(agentInstallOnly.contains(
              QStringLiteral("https://claude.ai/install.sh")) &&
              agentInstallOnly.contains(
                  QStringLiteral("https://chatgpt.com/codex/install.sh")) &&
              !agentInstallOnly.contains(QStringLiteral("credentials.json")) &&
              !agentInstallOnly.contains(QStringLiteral("cat >")),
          "the install-only command reads no stdin and writes no login file");
    check(agentWithLogins.contains(QStringLiteral("cat > \"$tmp\"")) &&
              agentWithLogins.contains(
                  QStringLiteral("$home/.claude/.credentials.json")) &&
              agentWithLogins.contains(
                  QStringLiteral("$home/.codex/auth.json")) &&
              agentWithLogins.contains(
                  QStringLiteral("$home/.forkmesh/agent-env")) &&
              agentWithLogins.contains(QStringLiteral("chmod 600")) &&
              agentWithLogins.contains(QStringLiteral("umask 077")),
          "the credential command writes each login file with private modes");
    check(agentWithLogins.startsWith(QStringLiteral("sh -lc '")) &&
              agentWithLogins.endsWith(QLatin1Char('\'')) &&
              agentWithLogins.count(QLatin1Char('\'')) == 2,
          "the remote command is one shell word, so no section can escape it");

    // --- Cloudflare DNS for a fresh Vultr mirror (adhoc #331) --------------
    QMap<QString, QString> zoneVariables;
    zoneVariables.insert(QStringLiteral("cloudflare_zone"),
                         QStringLiteral("Example.Com"));
    check(forkmesh::control::cloudflareZoneNameFromVariables(zoneVariables) ==
                  QStringLiteral("Example.Com") &&
              forkmesh::control::cloudflareZoneNameFromVariables({}).isEmpty(),
          "the stored CLOUDFLARE_ZONE device variable is resolved");

    check(forkmesh::control::vultrMirrorDnsHostname(
              QStringLiteral("vultr-mirror-1"),
              QStringLiteral(" Example.COM. ")) ==
                  QStringLiteral("vultr-mirror-1.example.com") &&
              forkmesh::control::vultrMirrorDnsHostname(
                  QStringLiteral("vultr-mirror-1"), QStringLiteral("example"))
                  .isEmpty() &&
              forkmesh::control::vultrMirrorDnsHostname(
                  QStringLiteral("bad node"), QStringLiteral("example.com"))
                  .isEmpty() &&
              forkmesh::control::vultrMirrorDnsHostname(
                  QString(), QStringLiteral("example.com")).isEmpty(),
          "the mirror DNS hostname is <node>.<zone> and rejects bad halves");

    const QJsonObject dnsPayload =
        forkmesh::control::vultrMirrorDnsRecordPayload(
            QStringLiteral("vultr-mirror-1.example.com"),
            QStringLiteral("203.0.113.99"));
    check(dnsPayload.value(QStringLiteral("type")).toString() ==
                  QStringLiteral("A") &&
              dnsPayload.value(QStringLiteral("name")).toString() ==
                  QStringLiteral("vultr-mirror-1.example.com") &&
              dnsPayload.value(QStringLiteral("content")).toString() ==
                  QStringLiteral("203.0.113.99") &&
              dnsPayload.value(QStringLiteral("proxied")).toBool() == false &&
              dnsPayload.value(QStringLiteral("ttl")).toInt() == 1,
          "the mirror DNS record is a DNS-only A answer at automatic TTL");
    check(forkmesh::control::vultrMirrorDnsRecordPayload(
              QStringLiteral("vultr-mirror-1.example.com"),
              QStringLiteral("0.0.0.0")).isEmpty() &&
              forkmesh::control::vultrMirrorDnsRecordPayload(
                  QStringLiteral("vultr-mirror-1.example.com"),
                  QStringLiteral("203.0.113.999")).isEmpty() &&
              forkmesh::control::vultrMirrorDnsRecordPayload(
                  QStringLiteral("vultr-mirror-1"),
                  QStringLiteral("203.0.113.99")).isEmpty(),
          "malformed addresses or single-label names never reach Cloudflare");

    const QJsonArray zones{
        QJsonObject{{QStringLiteral("id"), QStringLiteral("zone-other")},
                    {QStringLiteral("name"), QStringLiteral("other.test")}},
        QJsonObject{{QStringLiteral("id"), QStringLiteral("zone-example")},
                    {QStringLiteral("name"), QStringLiteral("example.com")}},
    };
    QJsonArray ambiguousZones = zones;
    ambiguousZones.append(
        QJsonObject{{QStringLiteral("id"), QStringLiteral("zone-duplicate")},
                    {QStringLiteral("name"), QStringLiteral("example.com")}});
    check(forkmesh::control::cloudflareZoneId(
              zones, QStringLiteral("Example.com.")) ==
                  QStringLiteral("zone-example") &&
              forkmesh::control::cloudflareZoneId(
                  zones, QStringLiteral("missing.test")).isEmpty() &&
              forkmesh::control::cloudflareZoneId(
                  ambiguousZones, QStringLiteral("example.com")).isEmpty(),
          "the zone id resolves only on an unambiguous exact name match");

    const QJsonArray dnsRecords{
        QJsonObject{
            {QStringLiteral("id"), QStringLiteral("record-cname")},
            {QStringLiteral("name"),
             QStringLiteral("vultr-mirror-1.example.com")},
            {QStringLiteral("type"), QStringLiteral("CNAME")}},
        QJsonObject{
            {QStringLiteral("id"), QStringLiteral("record-a")},
            {QStringLiteral("name"),
             QStringLiteral("vultr-mirror-1.example.com")},
            {QStringLiteral("type"), QStringLiteral("A")}},
    };
    check(forkmesh::control::cloudflareDnsRecordId(
              dnsRecords, QStringLiteral("vultr-mirror-1.example.com"),
              QStringLiteral("A")) == QStringLiteral("record-a") &&
              forkmesh::control::cloudflareDnsRecordId(
                  dnsRecords, QStringLiteral("vultr-mirror-2.example.com"),
                  QStringLiteral("A")).isEmpty(),
          "an existing A record is reused so repeat deploys update in place");

    QTemporaryDir hostSettingsDir;
    const QString hostSettingsPath =
        hostSettingsDir.filePath(QStringLiteral("controller.ini"));
    QSettings hostSettings(hostSettingsPath, QSettings::IniFormat);
    const QString legacyPassword =
        QStringLiteral("legacy-admin-password-must-leave-settings");
    const QJsonArray legacyHosts{
        QJsonObject{
            {QStringLiteral("name"), QStringLiteral("mirror2")},
            {QStringLiteral("ip"), QStringLiteral("mirror2.example.test")},
            {QStringLiteral("user"), QStringLiteral("forkmesh")},
            {QStringLiteral("pass"), legacyPassword},
            {QStringLiteral("status"), QStringLiteral("installed")},
        }};
    hostSettings.setValue(
        QStringLiteral("hosts/list"),
        QString::fromUtf8(
            QJsonDocument(legacyHosts).toJson(QJsonDocument::Compact)));
    hostSettings.sync();
    QHash<QString, QString> sessionHostPasswords;
    QJsonArray migratedHosts = forkmesh::control::loadSavedHosts(
        hostSettings, QStringLiteral("hosts/list"),
        &sessionHostPasswords);
    hostSettings.sync();
    QFile exportedSettings(hostSettingsPath);
    check(exportedSettings.open(QIODevice::ReadOnly),
          "migrated host settings file is readable");
    const QByteArray persistedAfterMigration = exportedSettings.readAll();
    exportedSettings.close();
    const QJsonObject migratedHost = migratedHosts.at(0).toObject();
    const QString migratedCredentialKey =
        forkmesh::control::savedHostCredentialKey(
            QStringLiteral("mirror2"),
            QStringLiteral("mirror2.example.test"),
            QStringLiteral("forkmesh"));
    check(!migratedHost.contains(QStringLiteral("pass")) &&
              sessionHostPasswords.value(migratedCredentialKey) ==
                  legacyPassword &&
              !persistedAfterMigration.contains(
                  legacyPassword.toUtf8()) &&
              !persistedAfterMigration.contains("\"pass\""),
          "legacy plaintext host passwords migrate to memory and are deleted from QSettings");

    // --- SSH connection-failure classification (adhoc #335) ----------------
    check(forkmesh::control::sshConnectionFailureHint(
              255, QStringLiteral("ssh: connect to host 1.2.3.4 port 22: "
                                   "Connection timed out"))
                  .contains(QStringLiteral("firewall"), Qt::CaseInsensitive) &&
              forkmesh::control::sshConnectionFailureHint(
                  255, QStringLiteral("ssh: connect to host 1.2.3.4 port 22: "
                                       "Connection refused"))
                      .contains(QStringLiteral("firewall"),
                                Qt::CaseInsensitive) &&
              forkmesh::control::sshConnectionFailureHint(
                  255,
                  QStringLiteral(
                      "Host key verification failed."))
                      .contains(QStringLiteral("key"), Qt::CaseInsensitive),
          "a 255 ssh exit with a known connection-failure signature yields an "
          "actionable hint");
    // --- Non-routable target diagnosis + attempt summaries (adhoc #342) ----
    const QString cgnatHint = forkmesh::control::sshConnectionFailureHint(
        255,
        QStringLiteral("ssh: connect to host 100.68.82.54 port 22: "
                       "Connection timed out"),
        QStringLiteral("100.68.82.54"));
    check(cgnatHint.contains(QStringLiteral("100.64.0.0/10")) &&
              !cgnatHint.contains(QStringLiteral("security group")) &&
              forkmesh::control::sshConnectionFailureHint(
                  255,
                  QStringLiteral("ssh: connect to host 10.0.0.9 port 22: "
                                 "No route to host"),
                  QStringLiteral("10.0.0.9"))
                  .contains(QStringLiteral("10.0.0.0/8")) &&
              forkmesh::control::sshConnectionFailureHint(
                  255,
                  QStringLiteral("ssh: connect to host 1.2.3.4 port 22: "
                                 "Connection timed out"),
                  QStringLiteral("1.2.3.4"))
                  .contains(QStringLiteral("firewall"), Qt::CaseInsensitive),
          "a timeout against a non-routable address is diagnosed as the "
          "address, not as a firewall");
    check(forkmesh::control::nonRoutableAddressNote(
              QStringLiteral("192.168.1.10")).contains(
              QStringLiteral("192.168.0.0/16")) &&
              forkmesh::control::nonRoutableAddressNote(
                  QStringLiteral("172.16.4.1")).contains(
                  QStringLiteral("172.16.0.0/12")) &&
              forkmesh::control::nonRoutableAddressNote(
                  QStringLiteral("172.32.4.1")).isEmpty() &&
              forkmesh::control::nonRoutableAddressNote(
                  QStringLiteral("100.128.0.1")).isEmpty() &&
              forkmesh::control::nonRoutableAddressNote(
                  QStringLiteral("45.32.1.9")).isEmpty() &&
              forkmesh::control::nonRoutableAddressNote(
                  QStringLiteral("mirror5.example.test")).isEmpty(),
          "only genuinely non-routable IPv4 literals are flagged");
    check(forkmesh::control::sshFailureSummary(
              255,
              QStringLiteral("Warming up\nssh: connect to host 1.2.3.4 port "
                             "22: Connection timed out\n"))
                  .contains(QStringLiteral("Connection timed out")) &&
              forkmesh::control::sshFailureSummary(255, QStringLiteral(""))
                      == QStringLiteral("exit 255") &&
              forkmesh::control::sshFailureSummary(
                  1, QStringLiteral("x").repeated(400)).size() < 200,
          "each attempt gets a bounded one-line failure summary");

    check(forkmesh::control::sshConnectionFailureHint(
              1, QStringLiteral("ssh: connect to host 1.2.3.4 port 22: "
                                 "Connection timed out"))
                  .isEmpty() &&
              forkmesh::control::sshConnectionFailureHint(
                  255, QStringLiteral("some unrelated remote error"))
                      .isEmpty(),
          "a non-255 exit or unrecognized output yields no ssh hint");

    QJsonObject accidentallySecretHost = migratedHost;
    accidentallySecretHost.insert(QStringLiteral("sshPassword"),
                                  legacyPassword);
    forkmesh::control::saveSavedHosts(
        hostSettings, QStringLiteral("hosts/list"),
        QJsonArray{accidentallySecretHost});
    hostSettings.sync();
    exportedSettings.setFileName(hostSettingsPath);
    check(exportedSettings.open(QIODevice::ReadOnly),
          "defensively saved host settings file is readable");
    const QByteArray persistedAfterSave = exportedSettings.readAll();
    check(!persistedAfterSave.contains(legacyPassword.toUtf8()) &&
              !persistedAfterSave.contains("sshPassword"),
          "host settings writer strips password-like fields defensively");

    auto invalidActions = actionsRequest;
    invalidActions.replaceVariables = false;
    check(!forkmesh::control::validateMirrorActionsConfigurationRequest(
               invalidActions).isEmpty(),
          "secret values cannot be silently ignored without replace approval");
    invalidActions = actionsRequest;
    invalidActions.variables.clear();
    invalidActions.variables.insert(QStringLiteral("BAD-NAME"),
                                    QStringLiteral("secret"));
    check(!forkmesh::control::validateMirrorActionsConfigurationRequest(
               invalidActions).isEmpty(),
          "unsafe Actions variable names fail closed");
    invalidActions = actionsRequest;
    invalidActions.host = QStringLiteral("-oProxyCommand=bad");
    check(forkmesh::control::buildMirrorActionsSshCommand(
              invalidActions, sshPassword, &actionsError).program.isEmpty(),
          "option-shaped SSH host input fails closed");
    invalidActions = actionsRequest;
    invalidActions.variables.insert(
        QStringLiteral("TOO_LARGE"), QString(16 * 1024 + 1, QLatin1Char('x')));
    check(forkmesh::control::buildMirrorActionsConfigurationPayload(
              invalidActions, &actionsError).isEmpty(),
          "oversize Actions variable value fails closed before SSH");

    const QJsonObject actionsResult{
        {QStringLiteral("type"),
         QStringLiteral("forkmesh.mirror-actions-configuration-result")},
        {QStringLiteral("schemaVersion"), 1},
        {QStringLiteral("ok"), true},
        {QStringLiteral("requestId"), actionsRequest.requestId},
        {QStringLiteral("node"), actionsRequest.nodeName},
        {QStringLiteral("actionsEnabled"), true},
        {QStringLiteral("variablesReplaced"), true},
        {QStringLiteral("variableCount"), 2},
    };
    const QByteArray encodedActionsResult =
        QJsonDocument(actionsResult)
            .toJson(QJsonDocument::Compact)
            .toBase64(QByteArray::Base64UrlEncoding |
                      QByteArray::OmitTrailingEquals);
    const QByteArray resultOutput =
        QByteArrayLiteral("bounded progress\nFORKMESH_ACTIONS_RESULT=") +
        encodedActionsResult + QByteArrayLiteral("\n");
    check(forkmesh::control::parseMirrorActionsConfigurationResult(
              resultOutput, actionsRequest.requestId,
              actionsRequest.nodeName, &actionsError)
                  .value(QStringLiteral("ok"))
                  .toBool(),
          "one matching bounded Actions helper result confirms the request");
    check(forkmesh::control::parseMirrorActionsConfigurationResult(
              resultOutput + resultOutput, actionsRequest.requestId,
              actionsRequest.nodeName, &actionsError).isEmpty(),
          "duplicate Actions helper results fail closed");
    check(forkmesh::control::parseMirrorActionsConfigurationResult(
              resultOutput, QString(32, QLatin1Char('f')),
              actionsRequest.nodeName, &actionsError).isEmpty(),
          "Actions helper result for another request fails closed");
    QJsonObject leakingActionsResult = actionsResult;
    leakingActionsResult.insert(
        QStringLiteral("variables"),
        QJsonObject{{QStringLiteral("DEPLOY_TOKEN"),
                     QStringLiteral("must-not-be-returned")}});
    const QByteArray leakingResultOutput =
        QByteArrayLiteral("FORKMESH_ACTIONS_RESULT=") +
        QJsonDocument(leakingActionsResult)
            .toJson(QJsonDocument::Compact)
            .toBase64(QByteArray::Base64UrlEncoding |
                      QByteArray::OmitTrailingEquals) +
        QByteArrayLiteral("\n");
    check(forkmesh::control::parseMirrorActionsConfigurationResult(
              leakingResultOutput, actionsRequest.requestId,
              actionsRequest.nodeName, &actionsError).isEmpty(),
          "Actions helper result cannot return variable names or values");
    check(forkmesh::control::parseMirrorActionsConfigurationResult(
              QByteArray(64 * 1024 + 1, 'x'), actionsRequest.requestId,
              actionsRequest.nodeName, &actionsError).isEmpty(),
          "oversize Actions helper output fails closed");

    QTemporaryDir mirrorActionsRoot;
    const QString sourceCheckout =
        mirrorActionsRoot.filePath(QStringLiteral("source"));
    const QString sourceBare =
        mirrorActionsRoot.filePath(QStringLiteral("source.git"));
    check(mirrorActionsRoot.isValid() &&
              QDir().mkpath(sourceCheckout) &&
              runProcess(QStringLiteral("git"),
                         {QStringLiteral("init"),
                          QStringLiteral("--initial-branch=main")},
                         sourceCheckout),
          "mirror Actions helper fixture repository initializes");
    QFile fixtureReadme(
        QDir(sourceCheckout).filePath(QStringLiteral("README.md")));
    check(fixtureReadme.open(QIODevice::WriteOnly) &&
              fixtureReadme.write("safe action fixture\n") > 0,
          "mirror Actions helper fixture writes one source file");
    fixtureReadme.close();
    check(runProcess(
              QStringLiteral("git"),
              {QStringLiteral("-c"), QStringLiteral("user.name=ForkMesh Test"),
               QStringLiteral("-c"),
               QStringLiteral("user.email=forkmesh@example.test"),
               QStringLiteral("add"), QStringLiteral("README.md")},
              sourceCheckout) &&
              runProcess(
                  QStringLiteral("git"),
                  {QStringLiteral("-c"),
                   QStringLiteral("user.name=ForkMesh Test"),
                   QStringLiteral("-c"),
                   QStringLiteral("user.email=forkmesh@example.test"),
                   QStringLiteral("commit"), QStringLiteral("-m"),
                   QStringLiteral("fixture")},
                  sourceCheckout) &&
              runProcess(QStringLiteral("git"),
                         {QStringLiteral("clone"), QStringLiteral("--bare"),
                          sourceCheckout, sourceBare}),
          "mirror Actions helper fixture creates a serving bare repository");
    QDir().mkpath(QDir(sourceBare).filePath(QStringLiteral("hooks")));
    const QString servingHook =
        QDir(sourceBare).filePath(QStringLiteral("hooks/post-receive"));
    QFile hook(servingHook);
    check(hook.open(QIODevice::WriteOnly) &&
              hook.write("#!/bin/sh\n# serving refresh hook\n") > 0,
          "mirror Actions fixture installs a serving refresh hook");
    hook.close();

    const QString gatewayConfig =
        mirrorActionsRoot.filePath(QStringLiteral("mirror-refresh.json"));
    QFile gateway(gatewayConfig);
    const QJsonObject gatewayObject{
        {QStringLiteral("schemaVersion"), 1},
        {QStringLiteral("type"),
         QStringLiteral("forkmesh.headless-mirror-refresh")},
        {QStringLiteral("sourceRepository"), sourceBare},
        {QStringLiteral("nodeOwner"), QStringLiteral("mirror2")},
        {QStringLiteral("repositoryName"), QStringLiteral("forkmesh")},
        {QStringLiteral("ownerAliases"),
         QJsonArray{QStringLiteral("mirror2"),
                    QStringLiteral("forkmesh")}},
        {QStringLiteral("catalog"),
         QJsonObject{{QStringLiteral("branch"),
                      QStringLiteral("main")}}},
    };
    check(gateway.open(QIODevice::WriteOnly) &&
              gateway.write(
                  QJsonDocument(gatewayObject)
                      .toJson(QJsonDocument::Compact)) > 0,
          "mirror Actions fixture writes a secret-free gateway configuration");
    gateway.close();
    QFile::setPermissions(gatewayConfig,
                          QFileDevice::ReadOwner |
                              QFileDevice::WriteOwner);
    qputenv("FORKMESH_MIRROR_REFRESH_CONFIG",
            gatewayConfig.toUtf8());

    const QString actionSettingsPath =
        mirrorActionsRoot.filePath(QStringLiteral("settings.ini"));
    QSettings actionSettings(actionSettingsPath, QSettings::IniFormat);
    actionSettings.setValue(QStringLiteral("node/machineName"),
                            QStringLiteral("mirror2"));
    const QJsonObject nodeActionsRequest{
        {QStringLiteral("type"),
         QStringLiteral("forkmesh.mirror-actions-configuration")},
        {QStringLiteral("schemaVersion"), 1},
        {QStringLiteral("requestId"),
         QStringLiteral("abcdef0123456789abcdef0123456789")},
        {QStringLiteral("node"), QStringLiteral("mirror2")},
        {QStringLiteral("actionsEnabled"), true},
        {QStringLiteral("variables"),
         QJsonObject{
             {QStringLiteral("mode"), QStringLiteral("replace")},
             {QStringLiteral("values"),
              QJsonObject{
                  {QStringLiteral("DEPLOY_TOKEN"),
                   QStringLiteral("node-local-only-value")},
                  {QStringLiteral("DEPLOY_ENV"),
                   QStringLiteral("production")},
              }},
         }},
    };
    bool catalogPublisherCalled = false;
    bool catalogPublishedBeforeLocalCommit = false;
    QString publishedRefreshConfiguration;
    bool publishedActionsEnabled = false;
    const QJsonObject applied =
        forkmesh::mirror_actions::applyConfiguration(
            nodeActionsRequest, actionSettings,
            [&](const QString &path, bool enabled) {
                catalogPublisherCalled = true;
                publishedRefreshConfiguration = path;
                publishedActionsEnabled = enabled;
                catalogPublishedBeforeLocalCommit =
                    !actionSettings.contains(QString::fromLatin1(
                        forkmesh::mirror_actions::kGenerationSetting)) &&
                    !actionSettings.contains(
                        QStringLiteral("actions/variables"));
                return true;
            });
    check(applied.value(QStringLiteral("ok")).toBool() &&
              applied.value(QStringLiteral("actionsEnabled")).toBool() &&
              applied.value(QStringLiteral("variablesReplaced")).toBool() &&
              applied.value(QStringLiteral("variableCount")).toInt() == 2 &&
              catalogPublisherCalled &&
              catalogPublishedBeforeLocalCommit &&
              publishedRefreshConfiguration == gatewayConfig &&
              publishedActionsEnabled &&
              !QJsonDocument(applied)
                   .toJson(QJsonDocument::Compact)
                   .contains("DEPLOY_TOKEN") &&
              !QJsonDocument(applied)
                   .toJson(QJsonDocument::Compact)
                   .contains("node-local-only-value"),
          "node helper publishes the catalog before atomically exposing staged local settings and returns only bounded metadata");
    const QMap<QString, QVariant> confirmedSettings =
        settingsValues(actionSettings);
    QJsonObject unconfirmedRequest = nodeActionsRequest;
    unconfirmedRequest.insert(
        QStringLiteral("requestId"),
        QStringLiteral("fedcba9876543210fedcba9876543210"));
    unconfirmedRequest.insert(QStringLiteral("actionsEnabled"), false);
    unconfirmedRequest.insert(
        QStringLiteral("variables"),
        QJsonObject{
            {QStringLiteral("mode"), QStringLiteral("replace")},
            {QStringLiteral("values"),
             QJsonObject{
                 {QStringLiteral("ROLLBACK_SECRET"),
                  QStringLiteral("must-never-be-committed")},
             }},
        });
    bool unpublishedSettingsStayedCommitted = false;
    int failedPublicationCalls = 0;
    const QJsonObject unconfirmed =
        forkmesh::mirror_actions::applyConfiguration(
            unconfirmedRequest, actionSettings,
            [&](const QString &, bool enabled) {
                ++failedPublicationCalls;
                if (!enabled) {
                    unpublishedSettingsStayedCommitted =
                        actionSettings
                                .value(QString::fromLatin1(
                                    forkmesh::mirror_actions::
                                        kGenerationSetting))
                                .toString() ==
                            nodeActionsRequest
                                .value(QStringLiteral("requestId"))
                                .toString() &&
                        actionSettings
                            .value(QString::fromLatin1(
                                forkmesh::mirror_actions::kEnabledSetting))
                            .toBool() &&
                        !actionSettings
                             .value(QStringLiteral("actions/variables"))
                             .toByteArray()
                             .contains("must-never-be-committed");
                    return false;
                }
                return true; // explicit rollback to the prior catalog state
            });
    check(!unconfirmed.value(QStringLiteral("ok")).toBool() &&
              unconfirmed.value(QStringLiteral("errorCode")).toString() ==
                  QStringLiteral("catalog_update_failed") &&
              failedPublicationCalls == 2 &&
              unpublishedSettingsStayedCommitted &&
              settingsValues(actionSettings) == confirmedSettings &&
              !QJsonDocument(unconfirmed)
                   .toJson(QJsonDocument::Compact)
                   .contains("must-never-be-committed"),
          "failed catalog publication leaves every prior setting and secret unchanged");
    const QJsonObject savedVariables =
        QJsonDocument::fromJson(
            actionSettings
                .value(QStringLiteral("actions/variables"))
                .toByteArray())
            .object();
    check(savedVariables.value(QStringLiteral("DEPLOY_TOKEN")).toString() ==
                  QStringLiteral("node-local-only-value") &&
              actionSettings
                      .value(QString::fromLatin1(
                          forkmesh::mirror_actions::kEnabledSetting))
                      .toBool() &&
              actionSettings
                      .value(QString::fromLatin1(
                          forkmesh::mirror_actions::kSummaryPathSetting))
                      .toString() ==
                  QDir(QFileInfo(gatewayConfig).absolutePath())
                      .filePath(QStringLiteral("actions-summary.json")),
          "node helper stores device-only variables and the fixed adjacent summary path");
    const int repositoryCount = actionSettings.beginReadArray(
        QStringLiteral("repositories/items"));
    actionSettings.setArrayIndex(0);
    const QString isolatedMirror =
        actionSettings.value(QStringLiteral("mirrorPath")).toString();
    check(repositoryCount == 1 &&
              actionSettings
                  .value(QStringLiteral("externallyManagedActions"))
                  .toBool() &&
              actionSettings
                      .value(QStringLiteral("externalActionsSource"))
                      .toString() == sourceBare &&
              isolatedMirror != sourceBare &&
              QFileInfo(QDir(isolatedMirror)
                            .filePath(QStringLiteral("HEAD")))
                  .isFile() &&
              !QFileInfo(QDir(isolatedMirror)
                             .filePath(
                                 QStringLiteral("objects/info/alternates")))
                   .exists(),
          "node helper creates an independent Actions mirror and marks its hook external");
    actionSettings.endArray();
    QFile unchangedHook(servingHook);
    check(unchangedHook.open(QIODevice::ReadOnly) &&
              unchangedHook.readAll().contains("serving refresh hook"),
          "node helper preserves the serving repository post-receive hook");
    unchangedHook.close();
    const QFileInfo settingsInfo(actionSettingsPath);
    check(!(settingsInfo.permissions() &
            (QFileDevice::ReadGroup | QFileDevice::WriteGroup |
             QFileDevice::ReadOther | QFileDevice::WriteOther)),
          "node helper restricts its device-local settings file");

    const QString recoveryJournal =
        forkmesh::mirror_actions::configurationRecoveryJournalPath(
            actionSettings);
    auto restoreJournal = [&](const QByteArray &encoded) {
        QFile file(recoveryJournal);
        const bool written =
            file.open(QIODevice::WriteOnly | QIODevice::Truncate) &&
            file.write(encoded) == encoded.size();
        file.close();
        return written &&
               QFile::setPermissions(
                   recoveryJournal,
                   QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    };
    QJsonObject crashRequest = nodeActionsRequest;
    crashRequest.insert(
        QStringLiteral("requestId"),
        QStringLiteral("11111111111111111111111111111111"));
    crashRequest.insert(QStringLiteral("actionsEnabled"), false);
    crashRequest.insert(
        QStringLiteral("variables"),
        QJsonObject{
            {QStringLiteral("mode"), QStringLiteral("replace")},
            {QStringLiteral("values"),
             QJsonObject{
                 {QStringLiteral("CRASH_SECRET"),
                  QStringLiteral("replacement-not-yet-live")},
             }},
        });
    QByteArray interruptedJournal;
    bool crashPublicationSawOldSettings = false;
    const QJsonObject crashApplied =
        forkmesh::mirror_actions::applyConfiguration(
            crashRequest, actionSettings,
            [&](const QString &, bool enabled) {
                QFile journal(recoveryJournal);
                if (journal.open(QIODevice::ReadOnly))
                    interruptedJournal = journal.readAll();
                crashPublicationSawOldSettings =
                    !enabled &&
                    settingsValues(actionSettings) == confirmedSettings &&
                    !interruptedJournal.contains(
                        "replacement-not-yet-live");
                return true;
            });
    check(crashApplied.value(QStringLiteral("ok")).toBool() &&
              !interruptedJournal.isEmpty() &&
              crashPublicationSawOldSettings &&
              !QFileInfo::exists(recoveryJournal),
          "durable recovery journal exists before publication, excludes replacement secrets, and is removed only after commit");

    // Reinstall the exact journal captured while publication was in flight.
    // The local settings are now fully new, which is a stronger recovery case
    // than a process dying midway through QSettings synchronization.
    check(restoreJournal(interruptedJournal),
          "interrupted transaction journal can be restored for crash simulation");
    int crashRollbackCalls = 0;
    QString crashRecoveryError;
    const bool crashRecovered =
        forkmesh::mirror_actions::recoverPendingConfiguration(
            actionSettings,
            [&](const QString &path, bool enabled) {
                ++crashRollbackCalls;
                return path == gatewayConfig && enabled;
            },
            &crashRecoveryError);
    check(crashRecovered && crashRecoveryError.isEmpty() &&
              crashRollbackCalls == 1 &&
              settingsValues(actionSettings) == confirmedSettings &&
              !QFileInfo::exists(recoveryJournal),
          "startup recovery durably restores old settings before rolling back the published catalog");

    // Capture a second interrupted transaction so local and external rollback
    // failures can be exercised independently and retried from one durable
    // journal.
    QJsonObject failureRequest = crashRequest;
    failureRequest.insert(
        QStringLiteral("requestId"),
        QStringLiteral("22222222222222222222222222222222"));
    QByteArray failureJournal;
    const QJsonObject failureApplied =
        forkmesh::mirror_actions::applyConfiguration(
            failureRequest, actionSettings,
            [&](const QString &, bool) {
                QFile journal(recoveryJournal);
                if (journal.open(QIODevice::ReadOnly))
                    failureJournal = journal.readAll();
                return true;
            });
    actionSettings.sync();
    QFile currentSettingsFile(actionSettingsPath);
    QByteArray currentSettingsBytes;
    if (currentSettingsFile.open(QIODevice::ReadOnly))
        currentSettingsBytes = currentSettingsFile.readAll();
    currentSettingsFile.close();
    check(failureApplied.value(QStringLiteral("ok")).toBool() &&
              !failureJournal.isEmpty() &&
              !currentSettingsBytes.isEmpty() &&
              restoreJournal(failureJournal),
          "second interrupted transaction fixture is durable");

    // Make the settings filename itself a directory. This fails even when the
    // test runs as root and proves catalog rollback is not attempted when the
    // prior local values cannot be durably restored.
    actionSettings.sync();
    check(QFile::remove(actionSettingsPath) &&
              QDir().mkpath(actionSettingsPath),
          "local rollback failure fixture blocks the settings file");
    QSettings brokenSettings(actionSettingsPath, QSettings::IniFormat);
    bool catalogTouchedBeforeLocalRestore = false;
    QString localRollbackError;
    const bool localRollbackRecovered =
        forkmesh::mirror_actions::recoverPendingConfiguration(
            brokenSettings,
            [&](const QString &, bool) {
                catalogTouchedBeforeLocalRestore = true;
                return true;
            },
            &localRollbackError);
    check(!localRollbackRecovered &&
              localRollbackError ==
                  QStringLiteral("configuration_local_rollback_failed") &&
              !catalogTouchedBeforeLocalRestore &&
              QFileInfo::exists(recoveryJournal),
          "failed local durability verification leaves the journal and never reports or attempts catalog rollback");

    QDir blockedSettings(actionSettingsPath);
    check(blockedSettings.removeRecursively(),
          "blocked settings fixture is removed");
    QFile repairedSettings(actionSettingsPath);
    check(repairedSettings.open(
              QIODevice::WriteOnly | QIODevice::Truncate) &&
              repairedSettings.write(currentSettingsBytes) ==
                  currentSettingsBytes.size(),
          "settings file is repaired for rollback retry");
    repairedSettings.close();
    QFile::setPermissions(actionSettingsPath,
                          QFileDevice::ReadOwner |
                              QFileDevice::WriteOwner);
    {
        QSettings restoredUntouchedSettings(actionSettingsPath,
                                            QSettings::IniFormat);
        restoredUntouchedSettings.setValue(
            QStringLiteral("node/machineName"),
            QStringLiteral("mirror2"));
        restoredUntouchedSettings.sync();
    }

    QSettings rollbackSettings(actionSettingsPath, QSettings::IniFormat);
    int failedCatalogRollbackCalls = 0;
    QString catalogRollbackError;
    const bool catalogRollbackRecovered =
        forkmesh::mirror_actions::recoverPendingConfiguration(
            rollbackSettings,
            [&](const QString &, bool enabled) {
                ++failedCatalogRollbackCalls;
                return !enabled; // previous state is enabled, so fail
            },
            &catalogRollbackError);
    check(!catalogRollbackRecovered &&
              catalogRollbackError ==
                  QStringLiteral("configuration_catalog_rollback_failed") &&
              failedCatalogRollbackCalls == 1 &&
              settingsValues(rollbackSettings) == confirmedSettings &&
              QFileInfo::exists(recoveryJournal),
          "failed catalog rollback keeps the verified old local state and journal for retry");
    QString retryError;
    const bool retryRecovered =
        forkmesh::mirror_actions::recoverPendingConfiguration(
            rollbackSettings,
            [&](const QString &path, bool enabled) {
                return path == gatewayConfig && enabled;
            },
            &retryError);
    check(retryRecovered && retryError.isEmpty() &&
              settingsValues(rollbackSettings) == confirmedSettings &&
              !QFileInfo::exists(recoveryJournal),
          "catalog rollback retries idempotently and removes the journal only after success");

    // Two helpers targeting the same QSettings file must serialize the whole
    // snapshot/publish/write/finalize interval, not merely their writes.
    QJsonObject concurrentFirst = nodeActionsRequest;
    concurrentFirst.remove(QStringLiteral("variables"));
    concurrentFirst.insert(
        QStringLiteral("requestId"),
        QStringLiteral("33333333333333333333333333333333"));
    concurrentFirst.insert(QStringLiteral("actionsEnabled"), true);
    QJsonObject concurrentSecond = concurrentFirst;
    concurrentSecond.insert(
        QStringLiteral("requestId"),
        QStringLiteral("44444444444444444444444444444444"));
    concurrentSecond.insert(QStringLiteral("actionsEnabled"), false);
    QMutex concurrencyMutex;
    QWaitCondition concurrencyCondition;
    bool firstPublisherEntered = false;
    bool releaseFirstPublisher = false;
    std::atomic_bool secondPublisherEntered{false};
    QJsonObject concurrentFirstResult;
    QJsonObject concurrentSecondResult;
    std::thread firstHelper([&] {
        QSettings helperSettings(actionSettingsPath,
                                 QSettings::IniFormat);
        concurrentFirstResult =
            forkmesh::mirror_actions::applyConfiguration(
                concurrentFirst, helperSettings,
                [&](const QString &, bool) {
                    QMutexLocker guard(&concurrencyMutex);
                    firstPublisherEntered = true;
                    concurrencyCondition.wakeAll();
                    while (!releaseFirstPublisher)
                        concurrencyCondition.wait(&concurrencyMutex);
                    return true;
                });
    });
    {
        QMutexLocker guard(&concurrencyMutex);
        if (!firstPublisherEntered)
            concurrencyCondition.wait(&concurrencyMutex, 10000);
    }
    std::thread secondHelper([&] {
        QSettings helperSettings(actionSettingsPath,
                                 QSettings::IniFormat);
        concurrentSecondResult =
            forkmesh::mirror_actions::applyConfiguration(
                concurrentSecond, helperSettings,
                [&](const QString &, bool) {
                    secondPublisherEntered.store(true);
                    return true;
                });
    });
    QThread::msleep(150);
    const bool secondWasSerialized =
        firstPublisherEntered && !secondPublisherEntered.load();
    {
        QMutexLocker guard(&concurrencyMutex);
        releaseFirstPublisher = true;
        concurrencyCondition.wakeAll();
    }
    firstHelper.join();
    secondHelper.join();
    QSettings concurrentResultSettings(actionSettingsPath,
                                       QSettings::IniFormat);
    check(secondWasSerialized &&
              concurrentFirstResult.value(QStringLiteral("ok")).toBool() &&
              concurrentSecondResult.value(QStringLiteral("ok")).toBool() &&
              secondPublisherEntered.load() &&
              concurrentResultSettings
                      .value(QString::fromLatin1(
                          forkmesh::mirror_actions::
                              kGenerationSetting))
                      .toString() ==
                  concurrentSecond
                      .value(QStringLiteral("requestId"))
                      .toString() &&
              !QFileInfo::exists(recoveryJournal),
          "concurrent helpers serialize publication and commit under one process-safe lock");

    const QString summaryStoreRoot =
        mirrorActionsRoot.filePath(QStringLiteral("summary-store"));
    ActionStore summaryStore(summaryStoreRoot);
    const qint64 summaryNow = 1784900000000LL;
    for (int index = 1; index <= 25; ++index) {
        ActionRun run;
        run.owner = QStringLiteral("forkmesh");
        run.name = QStringLiteral("forkmesh");
        run.workflowPath =
            QStringLiteral(".forkmesh/private-command-%1.yml").arg(index);
        run.workflowName = QStringLiteral("Build %1").arg(index);
        run.workflowContent =
            QStringLiteral("run: never-publish-command-%1").arg(index);
        run.commit = QStringLiteral("%1").arg(
            index, 40, 16, QLatin1Char('0'));
        run.ref = QStringLiteral("refs/heads/main");
        run.status = index == 25 ? ActionStatus::Running
                                 : ActionStatus::Success;
        run = summaryStore.createRun(run);
        run.createdAtMs = summaryNow - (25 - index) * 1000;
        run.startedAtMs = run.createdAtMs + 10;
        run.finishedAtMs =
            run.status == ActionStatus::Running
                ? 0
                : run.createdAtMs + 500;
        summaryStore.saveRun(run);
        const QString log =
            QString(20 * 1024, QLatin1Char('x')) +
            (index == 25
                 ? QString::fromUtf8(
                       "\nredacted=***\nvalid utf8 \xF0\x9F\x9A\x80")
                 : QStringLiteral("\nalready-redacted run log"));
        summaryStore.appendLog(run, log);
    }
    const QList<ActionRun> summaryRuns = summaryStore.loadAllRuns();
    const QJsonObject summary =
        forkmesh::mirror_actions::buildSummary(
            QStringLiteral("mirror2"), summaryRuns, summaryStore,
            summaryNow);
    QSet<QString> summaryKeys;
    for (auto it = summary.constBegin(); it != summary.constEnd(); ++it)
        summaryKeys.insert(it.key());
    const QSet<QString> expectedSummaryKeys{
        QStringLiteral("schemaVersion"), QStringLiteral("type"),
        QStringLiteral("node"), QStringLiteral("updatedAt"),
        QStringLiteral("expiresAt"), QStringLiteral("runs")};
    const QJsonArray summaryItems =
        summary.value(QStringLiteral("runs")).toArray();
    const QJsonObject latestSummaryRun =
        summaryItems.isEmpty() ? QJsonObject()
                               : summaryItems.at(0).toObject();
    QSet<QString> summaryRunKeys;
    for (auto it = latestSummaryRun.constBegin();
         it != latestSummaryRun.constEnd(); ++it) {
        summaryRunKeys.insert(it.key());
    }
    const QSet<QString> expectedSummaryRunKeys{
        QStringLiteral("id"), QStringLiteral("owner"),
        QStringLiteral("repository"), QStringLiteral("workflow"),
        QStringLiteral("commit"), QStringLiteral("ref"),
        QStringLiteral("status"), QStringLiteral("createdAt"),
        QStringLiteral("startedAt"), QStringLiteral("finishedAt"),
        QStringLiteral("logTail")};
    const QByteArray summaryJson =
        QJsonDocument(summary).toJson(QJsonDocument::Compact);
    check(summaryKeys == expectedSummaryKeys &&
              summary.value(QStringLiteral("type")).toString() ==
                  QStringLiteral("forkmesh.mirror-actions-summary") &&
              summary.value(QStringLiteral("node")).toString() ==
                  QStringLiteral("mirror2") &&
              summary.value(QStringLiteral("expiresAt")).toVariant()
                      .toLongLong() >
                  summary.value(QStringLiteral("updatedAt")).toVariant()
                      .toLongLong() &&
              summary.value(QStringLiteral("expiresAt")).toVariant()
                          .toLongLong() -
                      summary.value(QStringLiteral("updatedAt")).toVariant()
                          .toLongLong() <=
                  15 * 60 * 1000 &&
              summaryItems.size() == 20 &&
              summaryJson.size() + 1 <= 256 * 1024 &&
              summaryRunKeys == expectedSummaryRunKeys &&
              latestSummaryRun.value(QStringLiteral("id")).toInt() == 25 &&
              latestSummaryRun.value(QStringLiteral("status")).toString() ==
                  ActionStatus::Running &&
              latestSummaryRun.value(QStringLiteral("logTail"))
                      .toString()
                      .toUtf8()
                      .size() <=
                  forkmesh::mirror_actions::
                      kMaximumSummaryLogTailBytes &&
              latestSummaryRun.value(QStringLiteral("logTail"))
                  .toString()
                  .contains(QString::fromUtf8("\xF0\x9F\x9A\x80")) &&
              latestSummaryRun.value(QStringLiteral("logTail"))
                  .toString()
                  .contains(QStringLiteral("redacted=***")) &&
              !summaryJson.contains("workflowContent") &&
              !summaryJson.contains("workflowPath") &&
              !summaryJson.contains("never-publish-command") &&
              !summaryJson.contains("private-command"),
          "mirror Actions summary is exact, fresh, bounded, UTF-8, and secret-free");

    const QString summaryDirectory =
        mirrorActionsRoot.filePath(QStringLiteral("summary-public"));
    QDir().mkpath(summaryDirectory);
    QFile::setPermissions(
        summaryDirectory,
        QFileDevice::ReadOwner | QFileDevice::WriteOwner |
            QFileDevice::ExeOwner);
    const QString summaryPath =
        QDir(summaryDirectory)
            .filePath(QStringLiteral("actions-summary.json"));
    check(forkmesh::mirror_actions::writeSummaryFile(
              summaryPath, QStringLiteral("mirror2"), summaryRuns,
              summaryStore, summaryNow),
          "mirror Actions summary commits through an owner-only QSaveFile");
    QFile summaryFile(summaryPath);
    check(summaryFile.open(QIODevice::ReadOnly) &&
              QJsonDocument::fromJson(summaryFile.readAll()).isObject() &&
              !(QFileInfo(summaryPath).permissions() &
                (QFileDevice::ReadGroup | QFileDevice::WriteGroup |
                 QFileDevice::ExeGroup | QFileDevice::ReadOther |
                 QFileDevice::WriteOther | QFileDevice::ExeOther)),
          "mirror Actions summary remains strict JSON mode 0600");
    summaryFile.close();
    const QString statePath =
        QDir(summaryDirectory)
            .filePath(QStringLiteral("actions-state.json"));
    check(forkmesh::mirror_actions::writeStateFile(
              statePath, QStringLiteral("mirror2"),
              QStringLiteral("running"), summaryNow),
          "mirror Actions state commits through the protected atomic writer");
    QFile stateFile(statePath);
    const QJsonObject writtenState =
        stateFile.open(QIODevice::ReadOnly)
            ? QJsonDocument::fromJson(stateFile.readAll()).object()
            : QJsonObject();
    const QSet<QString> expectedStateKeys{
        QStringLiteral("schemaVersion"), QStringLiteral("type"),
        QStringLiteral("node"), QStringLiteral("state"),
        QStringLiteral("updatedAt"), QStringLiteral("expiresAt")};
    QSet<QString> writtenStateKeys;
    for (auto it = writtenState.constBegin();
         it != writtenState.constEnd(); ++it) {
        writtenStateKeys.insert(it.key());
    }
    check(writtenStateKeys == expectedStateKeys &&
              writtenState.value(QStringLiteral("type")).toString() ==
                  QStringLiteral("forkmesh.mirror-actions-state") &&
              writtenState.value(QStringLiteral("state")).toString() ==
                  QStringLiteral("running") &&
              !(QFileInfo(statePath).permissions() &
                (QFileDevice::ReadGroup | QFileDevice::WriteGroup |
                 QFileDevice::ExeGroup | QFileDevice::ReadOther |
                 QFileDevice::WriteOther | QFileDevice::ExeOther)),
          "mirror Actions state remains exact bounded JSON mode 0600");
    stateFile.close();
    check(!forkmesh::mirror_actions::writeStateFile(
              statePath, QStringLiteral("mirror2"),
              QStringLiteral("unknown"), summaryNow),
          "mirror Actions state rejects an unknown runtime state");

    using forkmesh::mirror_actions::detail::SummaryWritePolicy;
    const auto policy = [](
                            const QString &path,
                            quint64 effectiveUserId,
                            quint64 parentUserId,
                            quint64 parentGroupId,
                            quint32 parentMode,
                            bool targetExists,
                            bool targetRegular,
                            bool targetSymlink,
                            quint64 targetUserId,
                            quint64 targetGroupId,
                            quint32 targetMode,
                            quint64 targetLinks,
                            bool realParent = true) {
        return forkmesh::mirror_actions::detail::
            classifySummaryWritePolicy(
                path, effectiveUserId, parentUserId, parentGroupId,
                parentMode, realParent, targetExists, targetRegular,
                targetSymlink, targetUserId, targetGroupId, targetMode,
                targetLinks);
    };
    check(policy(
              summaryPath, 1000, 1000, 1000, 0700,
              true, true, false, 1000, 1000, 0600, 1) ==
              SummaryWritePolicy::SameAccount,
          "same-account Actions handoff accepts only owner mode 0600");
    for (const QString &fixedPath : {
             QString::fromLatin1(
                 forkmesh::mirror_actions::kSystemSummaryPath),
             QString::fromLatin1(
                 forkmesh::mirror_actions::kSystemStatePath)}) {
        check(policy(
                  fixedPath, 0, 991, 992, 0700,
                  false, false, false, 0, 0, 0, 0) ==
                  SummaryWritePolicy::RootGatewayHandoff &&
                  policy(
                      fixedPath, 0, 991, 992, 0700,
                      true, true, false, 0, 992, 0640, 1) ==
                  SummaryWritePolicy::RootGatewayHandoff,
              "fixed Actions files accept root-to-gateway group handoff");
    }
    const QString fixedSummary = QString::fromLatin1(
        forkmesh::mirror_actions::kSystemSummaryPath);
    check(policy(
              QStringLiteral("/tmp/actions-summary.json"),
              0, 991, 992, 0700, false, false, false,
              0, 0, 0, 0) == SummaryWritePolicy::Reject &&
              policy(
                  fixedSummary, 0, 991, 992, 0700,
                  true, true, false, 991, 992, 0640, 1) ==
                  SummaryWritePolicy::Reject,
          "root Actions handoff rejects arbitrary paths and service-owned spoofs");
    check(policy(
              fixedSummary, 0, 991, 992, 0700,
              true, true, false, 0, 991, 0640, 1) ==
              SummaryWritePolicy::Reject &&
              policy(
                  fixedSummary, 0, 991, 992, 0700,
                  true, true, false, 0, 992, 0600, 1) ==
                  SummaryWritePolicy::Reject &&
              policy(
                  fixedSummary, 0, 991, 992, 0700,
                  true, true, true, 0, 992, 0640, 1) ==
                  SummaryWritePolicy::Reject &&
              policy(
                  fixedSummary, 0, 991, 992, 0700,
                  true, true, false, 0, 992, 0640, 2) ==
                  SummaryWritePolicy::Reject,
          "root Actions handoff rejects wrong group, mode, symlink, and hardlink");
    check(policy(
              fixedSummary, 0, 991, 992, 0750,
              false, false, false, 0, 0, 0, 0) ==
              SummaryWritePolicy::Reject &&
              policy(
                  fixedSummary, 0, 991, 992, 0700,
                  false, false, false, 0, 0, 0, 0, false) ==
                  SummaryWritePolicy::Reject,
          "root Actions handoff requires an exact mode-0700 real parent");
    check(forkmesh::mirror_actions::buildSummary(
              QStringLiteral("invalid node"), summaryRuns,
              summaryStore, summaryNow)
              .isEmpty(),
          "mirror Actions summary rejects an invalid node identity");

    QJsonObject wrongNodeRequest = nodeActionsRequest;
    wrongNodeRequest.insert(QStringLiteral("node"),
                            QStringLiteral("mirror3"));
    check(!forkmesh::mirror_actions::applyConfiguration(
               wrongNodeRequest, actionSettings)
               .value(QStringLiteral("ok"))
               .toBool(),
          "node helper rejects a request for another mirror identity");
    qunsetenv("FORKMESH_MIRROR_REFRESH_CONFIG");

    if (failures == 0)
        std::fprintf(stdout, "control-node tests passed\n");
    return failures == 0 ? 0 : 1;
}
