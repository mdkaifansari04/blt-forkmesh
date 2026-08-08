#pragma once

#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QList>
#include <QMap>
#include <QProcessEnvironment>
#include <QString>
#include <QStringList>
#include <QUrl>

class QSettings;

namespace forkmesh::control {

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

// Ephemeral request sent by the desktop controller to a saved mirror host.
// Variable values deliberately live only in this in-memory request and the
// SSH stdin payload. They must never be added to argv, QSettings, process
// output, or the mirror's signed public catalog.
struct MirrorActionsConfigurationRequest {
    QString requestId;
    QString host;
    QString sshUser;
    QString nodeName;
    bool actionsEnabled = false;
    bool replaceVariables = false;
    QMap<QString, QString> variables;
};

struct MirrorActionsSshCommand {
    QString program;
    QStringList arguments;
    QProcessEnvironment environment;
    QByteArray standardInput;
};

// A direct controller-to-host SSH command. Host verification always uses
// OpenSSH's persistent known_hosts database in accept-new (TOFU) mode: a first
// key is retained, and a later mismatch fails closed. An empty password selects
// the user's SSH agent/default keys; a non-empty password is exposed only to
// sshpass through SSHPASS and public-key authentication is still attempted
// first.
struct HostSshCommand {
    QString program;
    QStringList arguments;
    QProcessEnvironment environment;
};

QString sharedHostKeyDirectory();

// Path of the one ForkMesh-managed private key every host in the fleet
// authorizes. Devices that still hold the older per-provider key file keep
// using that file, so mirrors provisioned before the switch stay reachable.
// The file itself may not exist yet; sharedHostKeyPath() only names it.
QString sharedHostKeyPath();

QString existingSharedHostIdentityFile();

// Build the common authenticated transport used by install, logs, uninstall,
// and Actions. The caller owns remoteCommand, which must not contain credentials.
// identityFile pins authentication to one ForkMesh-managed private key — the
// shared fleet key, or whatever key a saved host recorded; the path is public
// metadata, the key material never leaves disk.
HostSshCommand buildHostSshCommand(const QString &host,
                                   const QString &sshUser,
                                   const QString &sshPassword,
                                   const QString &remoteCommand,
                                   QString *error = nullptr,
                                   const QString &identityFile = QString());

// Same authenticated transport as buildHostSshCommand, but with a remote TTY
// forced (-tt). ssh only allocates one automatically for a bare login; a call
// that carries a remote command needs this before any interactive remote
// program — a provider sign-in prompt, for instance — can read a keystroke.
HostSshCommand buildHostInteractiveSshCommand(
    const QString &host, const QString &sshUser, const QString &sshPassword,
    const QString &remoteCommand, QString *error = nullptr,
    const QString &identityFile = QString());

QString hostSshCommandLine(const HostSshCommand &command);

// The interactive shell an operator lands in after ForkMesh installs the agent
// CLIs on a mirror. It puts both user-scoped install prefixes on PATH, prints
// the two provider sign-in commands, and then execs a login shell so
// `claude` /login and `codex login` can be completed by hand — the installer
// deliberately copies no tokens, so this is where credentials are entered. The
// command itself never carries a credential.
QString buildHostAgentLoginRemoteCommand();

// Stable, metadata-only key for a password retained in MainWindow memory for
// this process lifetime. It is never written to QSettings.
QString savedHostCredentialKey(const QString &nodeName, const QString &host,
                               const QString &sshUser);

struct HostDiskEntry {
    QString name;      // basename, as shown in the size map
    QString path;      // absolute remote path
    qint64 bytes = 0;  // disk usage in bytes
    bool directory = false;
};

struct HostDiskUsage {
    QString path;
    qint64 totalBytes = 0;         // disk usage of `path` itself
    QList<HostDiskEntry> entries;  // children, largest first
    QString error;                 // non-empty when the host refused the read
    bool complete = false;         // the end sentinel arrived
};

struct HostMountUsage {
    QString path;                 // absolute mount point, e.g. "/" or "/data"
    qint64 totalBytes = 0;
    qint64 usedBytes = 0;
    qint64 availableBytes = 0;
};

struct HostMountUsageList {
    QList<HostMountUsage> mounts;
    QString error;
    bool complete = false;
};

QString normalizeRemoteDiskPath(const QString &path);

QString buildHostDiskUsageCommand(const QString &path,
                                  QString *error = nullptr);

QString buildHostMountUsageCommand();
HostMountUsageList parseHostMountUsage(const QByteArray &output);

HostDiskUsage parseHostDiskUsage(const QByteArray &output,
                                 const QString &path);

QString formatDiskSize(qint64 bytes);

QString sshConnectionFailureHint(int exitCode, const QString &outputTail,
                                 const QString &host = QString());

// True when a failed SSH run was rejected for its credentials, so asking the
// operator for the host's SSH password and retrying can actually succeed. Only
// the credential rejections qualify: a timeout, a refused port or a changed
// host key all exit 255 too, and no password fixes any of them.
bool sshFailureNeedsPassword(int exitCode, const QString &outputTail);

// Describe the non-routable IPv4 range `host` falls in (RFC 1918 private,
// RFC 6598 carrier-grade NAT, link-local, loopback), or an empty string when it
// is a routable address or not an IPv4 literal at all.
QString nonRoutableAddressNote(const QString &host);

QString sshFailureSummary(int exitCode, const QString &outputTail);

// Load saved host metadata and atomically migrate legacy plaintext password
// fields out of QSettings. When supplied, sessionPasswords receives those
// values in memory so the current app session is not interrupted. saveSavedHosts
// also strips password-like fields defensively before persisting.
QJsonArray loadSavedHosts(QSettings &settings, const QString &settingsKey,
                          QHash<QString, QString> *sessionPasswords = nullptr);
void saveSavedHosts(QSettings &settings, const QString &settingsKey,
                    const QJsonArray &hosts);

QString validateMirrorActionsConfigurationRequest(
    const MirrorActionsConfigurationRequest &request);

// Serialize the exact v1 request consumed by a remote ForkMesh Actions helper.
// Secrets appear only in the returned stdin bytes. Callers should overwrite
// and clear that byte array immediately after QProcess::write().
QByteArray buildMirrorActionsConfigurationPayload(
    const MirrorActionsConfigurationRequest &request,
    QString *error = nullptr);

// Build a direct SSH invocation. With a password, sshpass reads it only from
// SSHPASS. Without a password, OpenSSH uses the user's agent/default keys in
// non-interactive public-key mode. The fixed remote helper command and all argv
// fields are secret-free; the configuration payload is standardInput only.
MirrorActionsSshCommand buildMirrorActionsSshCommand(
    const MirrorActionsConfigurationRequest &request,
    const QString &sshPassword,
    QString *error = nullptr,
    const QString &identityFile = QString());

QJsonObject parseMirrorActionsConfigurationResult(
    const QByteArray &output, const QString &expectedRequestId,
    const QString &expectedNodeName, QString *error = nullptr);

QString validateCloudflareBootstrapRequest(
    const CloudflareBootstrapRequest &request,
    bool allowAutomaticTopology = false);

QString findCloudflareBootstrapScript(
    const QString &sourceDir = QString(),
    const QString &applicationDir = QString());

QString findCloudflareWorkerDirectory(
    const QString &sourceDir = QString(),
    const QString &applicationDir = QString());

QString findSiteDeployScript(
    const QString &sourceDir = QString(),
    const QString &applicationDir = QString());

QString findCloudflareTunnelBootstrapScript(
    const QString &sourceDir = QString(),
    const QString &applicationDir = QString());
QString findMirrorGatewayScript(
    const QString &sourceDir = QString(),
    const QString &applicationDir = QString());
QString findMcpServerScript(
    const QString &sourceDir = QString(),
    const QString &applicationDir = QString());
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

CloudflareBootstrapCommand buildCloudflareBootstrapCommand(
    const CloudflareBootstrapRequest &request,
    const QString &apiToken,
    const QString &scriptPath,
    const QString &pythonProgram,
    const QString &signerProgram,
    const QString &nodePublicKey);

// Resolve the Cloudflare credentials this node already stores as action
// variables/secrets (Settings > Secrets & Coves) so live tooling can reuse the
// token the deploy workflow authenticates with instead of asking for a fresh
// one. Names are matched case-insensitively; blank or multi-line values are
// ignored so a pasted credential file can never smuggle extra lines into the
// child environment.
QString cloudflareApiTokenFromVariables(
    const QMap<QString, QString> &variables);
QString cloudflareAccountIdFromVariables(
    const QMap<QString, QString> &variables);

constexpr int kWranglerMinimumNodeMajor = 22;

struct NodeToolchain {
    QString npx;     // absolute npx to run
    QString binDir;  // MUST lead the child's PATH: both npx and the wrangler bin
    int majorVersion = 0;
    bool isValid() const { return !npx.isEmpty(); }
};

QStringList nodeBinDirectoryCandidates();

int nodeMajorVersionFromText(const QString &text);

NodeToolchain findNodeToolchain(int minimumMajor);

CloudflareBootstrapCommand buildCloudflareTailCommand(
    const QString &apiToken,
    const QString &accountId,
    const QString &npxProgram,
    const QString &nodeBinDir = QString());

struct CloudflareTailEvent {
    bool parsed = false;    // false for banner/plain lines: show them verbatim
    bool isError = false;   // an exception, a non-ok outcome, a 5xx, error logs
    QString summary;        // the rendered one-line form for the viewer
    QString method;
    QString url;
    QString userAgent;      // request user-agent header, empty when absent
    QString outcome;
    int status = 0;         // response status, 0 when the event carries none
    QStringList messages;   // console logs and exception text, newest first
};

QStringList takeCloudflareTailRecords(QByteArray *buffer);

CloudflareTailEvent parseCloudflareTailLine(const QString &line);

constexpr qint64 kCloudTailHealthyUptimeMs = 60'000;

struct CloudTailRestartPlan {
    bool restart = false;  // start another tail
    int delayMs = 0;       // after waiting this long
    bool giveUp = false;   // retrying cannot help: stop, and say so once
};

CloudTailRestartPlan planCloudflareTailRestart(int consecutiveFailures,
                                               qint64 uptimeMs);

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

QUrl worldUrlForRelay(const QString &relayUrl);

QUrl worldDevServerUrl(const QString &configured);

QStringList directMirrorRepositoryOwners(const QString &canonicalOwner,
                                         const QString &catalogOwner);

// True when `value` is one deployed Worker's mirror-router public key: 43
// base64url characters that decode to exactly 32 bytes.
bool isValidMirrorRouterPublicKey(const QString &value);

// True when this node holds every setting its direct HTTPS mirror gateway
// needs: a DNS-label node name, an https origin for the mirror hostname (as
// returned by the caller's origin normalizer, empty when the hostname is
// unusable), and the deployed Worker's router public key. A node that never
// provisioned a Cloudflare endpoint leaves these empty and serves through the
// relay instead, so an incomplete set is a supported steady state rather than
// a failure worth reporting.
bool directMirrorGatewayIsConfigured(const QString &nodeName,
                                     const QString &mirrorOrigin,
                                     const QString &routerPublicKey);

QByteArray mirrorManifestSigningPayload(const QJsonObject &request,
                                        const QString &expectedPublicKey,
                                        QString *error = nullptr);

// Quote one public command component for Python shlex.split(). This is used only
// for the signer executable path/flag; no secret is ever placed in the string.
QString shlexQuote(const QString &value);

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

QByteArray httpsMirrorRegistrationSigningPayload(
    const QString &node, const QString &baseUrl,
    const QString &publicKey, qint64 issuedAtMs,
    QString *error = nullptr);


QString validateVultrMirrorRequest(const QString &apiKey,
                                   const QString &nodeName);

bool vultrPlanHasIpv4(const QJsonObject &plan);

// From GET /v2/plans: the cheapest plan that can actually run an encrypted
// mirror (at least 1 GiB RAM, monthly_cost > 0, a US location, and IPv4). Ties
// break toward more RAM, then the lexicographically smallest id, so selection
// is deterministic.
QJsonObject cheapestVultrPlan(const QJsonArray &plans);

QString vultrPlanRegion(const QJsonObject &plan);

QJsonObject latestVultrDebianOs(const QJsonArray &osList);

bool localBinaryRunsOnVultrMirror(const QString &kernelType,
                                  const QString &cpuArch);

QJsonObject vultrInstanceCreatePayload(const QString &nodeName,
                                       const QString &planId,
                                       const QString &regionId,
                                       int osId,
                                       const QString &sshKeyId);

QString vultrInstanceReadyIp(const QJsonObject &instance);

bool vultrInstanceIsIpv6Only(const QJsonObject &instance);

QString nextMirrorNodeName(const QStringList &existingNames);

QStringList vultrApiKeyVariableNames();

QString vultrApiKeyFromVariables(const QMap<QString, QString> &variables);

bool vultrInstallNeedsLocalBinary(const QString &installOutput);


QString vultrSshProbeRemoteCommand();

bool vultrSshProbeReady(int exitCode, const QString &outputTail);


QString savedHostVultrInstanceId(const QJsonObject &host);

QString vultrInstanceIdForAddress(const QJsonArray &instances,
                                  const QString &address);

QString validateVultrDestroyRequest(const QString &apiKey,
                                    const QString &instanceId);

enum class MirrorFleetAction {
    None,
    Create,
    Destroy,
};

struct MirrorFleetReconcilePlan {
    MirrorFleetAction action = MirrorFleetAction::None;
    QString nodeName; // populated only for Destroy
    int managedCount = 0;
    int healthyCount = 0;
};

bool mirrorCatalogEntryIsHealthy(const QJsonObject &mirror);

MirrorFleetReconcilePlan planMirrorFleetReconciliation(
    int desiredHealthy, const QJsonArray &savedHosts,
    const QJsonArray &catalogMirrors);

// --- Agent CLIs on a fresh mirror -----------------------------
// A brand-new mirror can install the Claude Code and Codex CLIs, but until it
// is signed in they cannot run a single session — and there is no browser on a
// headless VPS to sign in with. These helpers copy this device's own logins to
// the new node so it comes up able to run agent sessions on our access.
// Everything sensitive lives in the stdin payload only: the remote helper
// command is fixed and secret-free, exactly like the Actions configuration
// path, so nothing ever reaches argv, the process table or the install log.
struct AgentCliCredentials {
    QByteArray claudeCredentials;  // verbatim ~/.claude/.credentials.json
    QByteArray codexAuth;          // verbatim ~/.codex/auth.json
    QMap<QString, QString> env;
};

bool agentCliCredentialsAreEmpty(const AgentCliCredentials &credentials);

QString describeAgentCliCredentials(const AgentCliCredentials &credentials);

QByteArray agentCliEnvFileContents(const QMap<QString, QString> &env);

// Serialize the credentials into the "<section> <base64>" lines the remote
// helper reads on stdin. Secrets appear only in these bytes; callers should
// overwrite and clear the array right after QProcess::write(). Returns an empty
// array (with *error set) when the bundle is malformed or implausibly large.
QByteArray buildAgentCliBootstrapPayload(const AgentCliCredentials &credentials,
                                         QString *error = nullptr);

// The fixed remote command that installs the official user-scoped Claude Code
// and Codex CLIs. With withCredentials the same command also consumes the
// stdin payload above and writes the login files; without it, no stdin is sent
// and the mirror is left unauthenticated (the per-host button's behaviour).
QString agentCliBootstrapRemoteCommand(bool withCredentials);


QString cloudflareZoneNameFromVariables(const QMap<QString, QString> &variables);

QString vultrMirrorDnsHostname(const QString &nodeName,
                               const QString &zoneName);

QJsonObject vultrMirrorDnsRecordPayload(const QString &hostname,
                                        const QString &ip);

QString cloudflareZoneId(const QJsonArray &zones, const QString &zoneName);

QString cloudflareDnsRecordId(const QJsonArray &records,
                              const QString &hostname,
                              const QString &recordType);


struct CloudflareTokenRequirement {
    QString key;             // stable id, also the probe-result map key
    QString label;           // dashboard wording, e.g. "Workers Scripts: Edit"
    QString purpose;         // what stops working without it
    QStringList groupNames;  // acceptable permission-group names, mint-first
    QString scope;           // "account", "zone" or "user"
    QString probePath;       // "{account}"/"{zone}" placeholders, may be empty
    bool required = true;    // false: only this page's own token tooling needs it
};

QList<CloudflareTokenRequirement> cloudflareTokenRequirements();

QString cloudflareTokenProbePath(const CloudflareTokenRequirement &requirement,
                                 const QString &accountId,
                                 const QString &zoneId);

// Loose shape check before a token is put on the wire: Cloudflare issues
// 40-character base62 tokens, but the bound stays wide so a future format keeps
// working. Whitespace and newlines are rejected, which is what a pasted
// credential file looks like.
bool isPlausibleCloudflareApiToken(const QString &token);

QString cloudflareTokenVerifyStatus(const QJsonObject &verifyResult);
QString cloudflareTokenVerifyId(const QJsonObject &verifyResult);

// Distinct permission-group names allowed by GET /user/tokens/<id>, sorted.
// Groups inside a deny policy are not reported as granted.
QStringList cloudflareTokenPermissionGroupNames(const QJsonObject &tokenDetail);

bool cloudflareTokenGrantsRequirement(
    const CloudflareTokenRequirement &requirement,
    const QStringList &grantedGroupNames);

// The account ids and the user resource key a token's own policies name. A
// token that cannot list accounts still reveals which account it belongs to
// this way, and the user key is the only way to scope user-level permission
// groups (API Tokens) on a replacement token.
QStringList cloudflareTokenAccountIds(const QJsonObject &tokenDetail);
QString cloudflareTokenUserResourceKey(const QJsonObject &tokenDetail);

// Exact POST /user/tokens body for a replacement ForkMesh deployment token.
// Permission-group ids come from the account's own
// GET /user/tokens/permission_groups catalog, so no id is hardcoded here.
// Account- and zone-scoped groups become one policy each; user-scoped groups
// are included only when userResourceKey is known. Fails closed with *error
// when a required group has no id or no resource to bind to; optional groups
// are dropped silently.
QJsonObject cloudflareTokenCreatePayload(
    const QString &tokenName, const QString &accountId, const QString &zoneId,
    const QString &userResourceKey, const QJsonArray &permissionGroupCatalog,
    QString *error = nullptr);

// The new secret from POST /user/tokens, validated as a usable token.
QString cloudflareCreatedTokenValue(const QJsonObject &createResult);

QString updatedEnvAssignment(const QString &contents, const QString &name,
                             const QString &value);

QString siteDeployEnvFilePath(const QString &sourceDir = QString(),
                              const QString &applicationDir = QString());

// "…9f3c" — the last four characters of a token, so the page can name which
// credential is in play without ever echoing one.
QString maskedTokenSuffix(const QString &token);

} // namespace forkmesh::control
