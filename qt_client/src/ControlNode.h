#pragma once

#include <QJsonObject>
#include <QJsonValue>
#include <QProcessEnvironment>
#include <QString>
#include <QStringList>
#include <QUrl>

namespace forkmesh::control {

// Public deployment settings collected by the desktop control-node page.
// The Cloudflare API token deliberately is not part of this structure: callers
// hand it directly to buildCloudflareBootstrapCommand(), which puts it only in
// the child environment and never in argv or persistent settings.
struct CloudflareBootstrapRequest {
    QString hostname;
    QString zoneName;
    QString accountId;
    QString nodeName;
    QString relayLabel;
    QString mainRelayUrl;
    bool dryRun = false;
};

struct CloudflareBootstrapCommand {
    QString program;
    QStringList arguments;
    QProcessEnvironment environment;
};

// Returns an empty string when the public deployment fields are safe to pass to
// tools/cloudflare_bootstrap.py, otherwise a safe user-facing error.
QString validateCloudflareBootstrapRequest(
    const CloudflareBootstrapRequest &request,
    bool allowAutomaticTopology = false);

// Resolve the repository-pinned bootstrapper from either a source checkout or
// an installed ForkMesh resource tree. The result is returned only when the
// sibling Worker source, static assets, migrations, and build input are all
// present.
QString findCloudflareBootstrapScript(
    const QString &sourceDir = QString(),
    const QString &applicationDir = QString());

// Resolve the direct-HTTPS components separately from the Worker bootstrap.
QString findCloudflareTunnelBootstrapScript(
    const QString &sourceDir = QString(),
    const QString &applicationDir = QString());
QString findMirrorGatewayScript(
    const QString &sourceDir = QString(),
    const QString &applicationDir = QString());
// Resolve the non-shell installer for ForkMesh's exact SHA-256-pinned
// cloudflared release. The helper installs into owner-controlled app data.
QString findCloudflaredInstallerScript(
    const QString &sourceDir = QString(),
    const QString &applicationDir = QString());

// Build the one-click Tunnel/DNS/manifest provisioning command. The API token
// remains in the child environment. The connector token is written only to the
// explicit owner-only local path so Qt can start/stop cloudflared without ever
// putting that credential in argv.
CloudflareBootstrapCommand buildCloudflareTunnelBootstrapCommand(
    const CloudflareBootstrapRequest &request,
    const QString &mirrorHostname,
    const QString &apiToken,
    const QString &scriptPath,
    const QString &pythonProgram,
    const QString &signerProgram,
    const QString &nodePublicKey,
    const QString &gatewayConfigPath,
    const QString &manifestOutputPath,
    const QString &connectorTokenPath);

// Build an invocation with public configuration only in argv. The API token is
// available to the bootstrap process solely as CLOUDFLARE_API_TOKEN.
CloudflareBootstrapCommand buildCloudflareBootstrapCommand(
    const CloudflareBootstrapRequest &request,
    const QString &apiToken,
    const QString &scriptPath,
    const QString &pythonProgram,
    const QString &signerProgram,
    const QString &nodePublicKey);

// Decode the bootstrapper's bounded, non-secret machine result. Human log
// output may surround the sentinel line; malformed or duplicate results fail
// closed.
QJsonObject parseCloudflareBootstrapResult(const QByteArray &output,
                                           QString *error = nullptr);

// Sanitize child-process output before it is shown or copied. Exact in-memory
// secrets are removed first, followed by common credential-shaped assignments.
QString redactProcessOutput(const QString &text,
                            const QStringList &exactSecrets = {});

// A Solana wallet connection in ForkMesh is a public address only. Validation
// decodes base58 and requires exactly 32 bytes; private keys and seed phrases
// therefore cannot be accepted accidentally by this field.
bool isValidSolanaPublicAddress(const QString &address);

// Convert a configured ws/wss/http(s) relay URL into its browser world portal.
QUrl worldUrlForRelay(const QString &relayUrl);

// Return the distinct, normalized repository-owner aliases a direct mirror
// must expose. A mirror account can publish an imported repository under its
// own catalog namespace while the Worker continues routing and challenging the
// canonical source namespace; both names must resolve to the same sealed
// archive.
QStringList directMirrorRepositoryOwners(const QString &canonicalOwner,
                                         const QString &catalogOwner);

// Validate the JSON request emitted by cloudflare_bootstrap.py's external
// manifest signer and return the exact canonical manifest bytes to sign.
// Nothing from the request is persisted or logged.
QByteArray mirrorManifestSigningPayload(const QJsonObject &request,
                                        const QString &expectedPublicKey,
                                        QString *error = nullptr);

// Quote one public command component for Python shlex.split(). This is used only
// for the signer executable path/flag; no secret is ever placed in the string.
QString shlexQuote(const QString &value);

// Python json.dumps(..., sort_keys=True, separators=(",", ":")) compatible
// serialization with ensure_ascii=True. The Worker uses that exact form for
// catalog-v2 record hashing.
QByteArray pythonCanonicalJson(const QJsonValue &value,
                               QString *error = nullptr);

// Build the exact bytes signed as a private-capable catalog-v2 proof. The input
// must already be the Worker's normalized safe_catalog_record shape.
QByteArray catalogV2SigningPayload(QJsonObject normalizedRecord,
                                   QString *error = nullptr);

// Normalize the optional public CPU/RAM/disk fields embedded in a catalog-v2
// record. Every returned key is present; a JSON null means the operator did not
// share that metric. Numbers are bounded integers so Python and C++ produce the
// same canonical signing bytes.
QJsonObject normalizedCatalogHostTelemetry(const QJsonObject &data);

// Exact owner signature payload for a blind private-replica route.
QByteArray privateReplicaRouteSigningPayload(
    const QString &owner, const QString &repository, const QString &node,
    const QString &opaqueId, const QString &replicaSha256, quint64 keyEpoch,
    bool active, qint64 issuedAtMs, QString *error = nullptr);

// Exact endpoint registration canonical shared with edge_routing.py.
QByteArray httpsMirrorRegistrationSigningPayload(
    const QString &node, const QString &baseUrl,
    const QString &publicKey, qint64 issuedAtMs,
    QString *error = nullptr);

} // namespace forkmesh::control
