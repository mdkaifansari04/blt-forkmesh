#include "ControlNode.h"
#include "MirrorActionsConfiguration.h"
#include "MirrorActionsSummary.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QProcess>
#include <QSettings>
#include <QSet>
#include <QTemporaryDir>

#include <cstdio>

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

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);

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
        actionsPasswordCommand.arguments.join(QChar(u'\0'));
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
                  QStringLiteral("StrictHostKeyChecking=accept-new")),
          "Actions transport invokes only the fixed stdin helper and retains host keys");

    const auto actionsKeyCommand =
        forkmesh::control::buildMirrorActionsSshCommand(
            actionsRequest, QString(), &actionsError);
    check(actionsKeyCommand.program == QStringLiteral("ssh") &&
              actionsKeyCommand.arguments.contains(
                  QStringLiteral("BatchMode=yes")) &&
              actionsKeyCommand.arguments.contains(
                  QStringLiteral("PreferredAuthentications=publickey")) &&
              !actionsKeyCommand.environment.contains(
                  QStringLiteral("SSHPASS")),
          "empty password selects non-interactive SSH key authentication");

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
    bool catalogPublishedAfterLocalSetup = false;
    QString publishedRefreshConfiguration;
    bool publishedActionsEnabled = false;
    const QJsonObject applied =
        forkmesh::mirror_actions::applyConfiguration(
            nodeActionsRequest, actionSettings,
            [&](const QString &path, bool enabled) {
                catalogPublisherCalled = true;
                publishedRefreshConfiguration = path;
                publishedActionsEnabled = enabled;
                catalogPublishedAfterLocalSetup =
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
                        .toBool();
                return true;
            });
    check(applied.value(QStringLiteral("ok")).toBool() &&
              applied.value(QStringLiteral("actionsEnabled")).toBool() &&
              applied.value(QStringLiteral("variablesReplaced")).toBool() &&
              applied.value(QStringLiteral("variableCount")).toInt() == 2 &&
              catalogPublisherCalled &&
              catalogPublishedAfterLocalSetup &&
              publishedRefreshConfiguration == gatewayConfig &&
              publishedActionsEnabled &&
              !QJsonDocument(applied)
                   .toJson(QJsonDocument::Compact)
                   .contains("DEPLOY_TOKEN") &&
              !QJsonDocument(applied)
                   .toJson(QJsonDocument::Compact)
                   .contains("node-local-only-value"),
          "node helper publishes the catalog toggle after local setup and returns only bounded metadata");
    QJsonObject unconfirmedRequest = nodeActionsRequest;
    unconfirmedRequest.insert(
        QStringLiteral("requestId"),
        QStringLiteral("fedcba9876543210fedcba9876543210"));
    unconfirmedRequest.remove(QStringLiteral("variables"));
    const QJsonObject unconfirmed =
        forkmesh::mirror_actions::applyConfiguration(
            unconfirmedRequest, actionSettings,
            [](const QString &, bool) { return false; });
    check(!unconfirmed.value(QStringLiteral("ok")).toBool() &&
              unconfirmed.value(QStringLiteral("errorCode")).toString() ==
                  QStringLiteral("catalog_update_failed") &&
              !QJsonDocument(unconfirmed)
                   .toJson(QJsonDocument::Compact)
                   .contains("DEPLOY_TOKEN"),
          "node helper fails closed when the signed catalog toggle is not confirmed");
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
