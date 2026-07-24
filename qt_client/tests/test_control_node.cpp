#include "ControlNode.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
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

    if (failures == 0)
        std::fprintf(stdout, "control-node tests passed\n");
    return failures == 0 ? 0 : 1;
}
