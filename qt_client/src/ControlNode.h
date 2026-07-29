#pragma once

#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QMap>
#include <QProcessEnvironment>
#include <QString>
#include <QStringList>
#include <QUrl>

class QSettings;

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

// Build the common authenticated transport used by install, logs, uninstall,
// and Actions. The caller owns remoteCommand, which must not contain credentials.
// identityFile optionally pins authentication to one ForkMesh-managed private
// key (used for auto-provisioned Vultr mirrors); the path is public metadata,
// the key material never leaves disk.
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

// Flatten a built command into one single-quoted POSIX shell command line, for
// the callers that hand it to a shell (the embedded PTY terminal) instead of
// running it through QProcess argv. Every word is quoted, so no argument value
// can re-enter that shell as syntax.
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

// One child of a browsed remote directory in the host size map. `bytes` is
// disk usage as `du` reports it (allocated blocks), not apparent file size.
struct HostDiskEntry {
    QString name;      // basename, as shown in the size map
    QString path;      // absolute remote path
    qint64 bytes = 0;  // disk usage in bytes
    bool directory = false;
};

// One measured level of a host's size map.
struct HostDiskUsage {
    QString path;
    qint64 totalBytes = 0;         // disk usage of `path` itself
    QList<HostDiskEntry> entries;  // children, largest first
    QString error;                 // non-empty when the host refused the read
    bool complete = false;         // the end sentinel arrived
};

// Capacity for one mounted filesystem in the host size-map navigator. These
// records power the compact, clickable mount maps beside the folder browser.
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

// Collapse a browsed remote path to a canonical absolute POSIX path (no "."
// or ".." components, no duplicate or trailing slashes). Returns an empty
// string when the input is not usable as a remote path at all.
QString normalizeRemoteDiskPath(const QString &path);

// Build the bounded, read-only remote shell command that measures one level of
// a host's disk usage. It only ever runs `du` over a single directory level and
// frames its answer in base64 sentinel lines, so filenames — including ones
// holding spaces, quotes or shell metacharacters — can never re-enter the
// remote shell or this parser as syntax.
QString buildHostDiskUsageCommand(const QString &path,
                                  QString *error = nullptr);

// Build and parse the read-only filesystem-capacity query shown as compact
// mount maps on the right side of the host folder browser.
QString buildHostMountUsageCommand();
HostMountUsageList parseHostMountUsage(const QByteArray &output);

// Parse the sentinel-framed listing produced by buildHostDiskUsageCommand().
// Login banners and other noise around the sentinels are ignored, and entries
// come back sorted largest first.
HostDiskUsage parseHostDiskUsage(const QByteArray &output,
                                 const QString &path);

// Human-readable byte size for the size map ("1.4 GB", "912 KB").
QString formatDiskSize(qint64 bytes);

// Classify a failed SSH install/uninstall attempt from OpenSSH's own exit code
// and the tail of its (merged stdout+stderr) output, and return an actionable
// hint to append after the generic "failed (exit N)" line — or an empty
// string when the failure does not match a known connection-level pattern
// (e.g. the remote command itself exited non-zero, which is not an SSH
// transport problem). OpenSSH exits 255 for any failure before or during the
// connection (unreachable host, refused/reset port, auth failure, host-key
// mismatch); a non-255 code is always the remote command's own exit status, so
// no SSH-side hint applies. When `host` is supplied and it is an address that
// cannot be routed on the public internet, a timeout is diagnosed as that
// rather than as a firewall.
QString sshConnectionFailureHint(int exitCode, const QString &outputTail,
                                 const QString &host = QString());

// Describe the non-routable IPv4 range `host` falls in (RFC 1918 private,
// RFC 6598 carrier-grade NAT, link-local, loopback), or an empty string when it
// is a routable address or not an IPv4 literal at all.
QString nonRoutableAddressNote(const QString &host);

// One-line record of a failed SSH run — its exit code plus the last output
// line — short enough to list one per attempt in the install window's attempt
// history (adhoc #342).
QString sshFailureSummary(int exitCode, const QString &outputTail);

// Load saved host metadata and atomically migrate legacy plaintext password
// fields out of QSettings. When supplied, sessionPasswords receives those
// values in memory so the current app session is not interrupted. saveSavedHosts
// also strips password-like fields defensively before persisting.
QJsonArray loadSavedHosts(QSettings &settings, const QString &settingsKey,
                          QHash<QString, QString> *sessionPasswords = nullptr);
void saveSavedHosts(QSettings &settings, const QString &settingsKey,
                    const QJsonArray &hosts);

// Validate the bounded v1 mirror-Actions controller contract. An empty string
// means the request is safe to serialize and send.
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

// Decode the remote helper's single bounded, base64url result sentinel.
// Success is not inferred from an SSH exit code alone.
QJsonObject parseMirrorActionsConfigurationResult(
    const QByteArray &output, const QString &expectedRequestId,
    const QString &expectedNodeName, QString *error = nullptr);

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

// Resolve the complete Worker bundle that belongs to the bootstrapper. Read-only
// tools such as the live log tail use this to run against the same checked-in or
// installed wrangler configuration as deployments.
QString findCloudflareWorkerDirectory(
    const QString &sourceDir = QString(),
    const QString &applicationDir = QString());

// Resolve the direct-HTTPS components separately from the Worker bootstrap.
QString findCloudflareTunnelBootstrapScript(
    const QString &sourceDir = QString(),
    const QString &applicationDir = QString());
QString findMirrorGatewayScript(
    const QString &sourceDir = QString(),
    const QString &applicationDir = QString());
// Resolve the MCP server that Settings -> MCP hands to external agents.
QString findMcpServerScript(
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

// Build a direct (non-shell) invocation of ForkMesh's pinned Wrangler tail.
// The token and optional account ID are placed only in the child environment,
// never in argv.
CloudflareBootstrapCommand buildCloudflareTailCommand(
    const QString &apiToken,
    const QString &accountId,
    const QString &npxProgram);

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

// Normalize the configured local World dev server (tools/world_dev_server.py)
// into a probe/open URL. Empty input yields the default
// http://127.0.0.1:8788/world/; "off" disables the probe; anything that does
// not resolve to a loopback plain-http host fails closed (invalid QUrl) so the
// World button can never be redirected off this machine by a stray setting.
QUrl worldDevServerUrl(const QString &configured);

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

// --- One-click Vultr mirror provisioning (adhoc #315) ----------------------
// Pure helpers behind the Hosts page's "Create a Vultr mirror" flow. All
// networking, key generation and polling stay in the UI layer; everything here
// is deterministic over the raw Vultr v2 JSON so the selection and payload
// contracts are testable. The API key travels only in the Authorization header
// of the desktop's HTTPS calls — never in argv, QSettings, or logs.

// Empty string when the key and node name are safe to use, otherwise a
// user-facing error. The key shape is deliberately loose (alphanumeric,
// bounded) so future Vultr formats keep working.
QString validateVultrMirrorRequest(const QString &apiKey,
                                   const QString &nodeName);

// True when a plan ships a routable IPv4 address. Vultr's cheapest tiers are
// IPv6-only ("...-v6" plan ids): they boot fine but nothing in the mesh (SSH
// provisioning, the A record, clients cloning) can reach them, so they must
// never win the cheapest-plan race (adhoc #344).
bool vultrPlanHasIpv4(const QJsonObject &plan);

// From GET /v2/plans: the cheapest plan that can actually run an encrypted
// mirror (at least 1 GiB RAM, monthly_cost > 0, a location, and IPv4). Ties
// break toward more RAM, then the lexicographically smallest id, so selection
// is deterministic.
QJsonObject cheapestVultrPlan(const QJsonArray &plans);

// Deterministic region for a chosen plan: its lexicographically first
// location. Empty when the plan has none.
QString vultrPlanRegion(const QJsonObject &plan);

// From GET /v2/os: the newest x64 Debian image (highest version number in the
// name; ties break toward the higher os id).
QJsonObject latestVultrDebianOs(const QJsonArray &osList);

// True when this machine's own ForkMesh binary can run on a freshly created
// Vultr mirror (always x64 Debian, see latestVultrDebianOs), so the provisioner
// can upload it straight over the SSH session instead of asking the new host to
// clone from an online mirror that may not exist yet. Takes QSysInfo's
// kernelType() ("linux"/"darwin"/"winnt") and currentCpuArchitecture().
bool localBinaryRunsOnVultrMirror(const QString &kernelType,
                                  const QString &cpuArch);

// Exact POST /v2/instances body for a ForkMesh mirror: chosen plan/region/OS,
// the managed SSH key, no backups, no activation email, tagged so the instance
// is recognizable in the Vultr panel.
QJsonObject vultrInstanceCreatePayload(const QString &nodeName,
                                       const QString &planId,
                                       const QString &regionId,
                                       int osId,
                                       const QString &sshKeyId);

// The instance's routable IPv4 once it is ready for SSH provisioning
// (status active, power running, real main_ip); empty while it is still
// booting or when the object is malformed.
QString vultrInstanceReadyIp(const QJsonObject &instance);

// True when a booted instance only ever got an IPv6 address (v6_main_ip set,
// main_ip still unassigned). Polling such an instance can only time out, so the
// provisioning flow fails fast with an explanation instead (adhoc #344).
bool vultrInstanceIsIpv6Only(const QJsonObject &instance);

// Default node name for a one-click mirror: the next free "mirrorN" over every
// name already in use (saved hosts plus the account's linked nodes), so a new
// instance joins the fleet as mirror5 next to mirror1..mirror4 instead of
// carrying its hosting provider in its name (adhoc #344).
QString nextMirrorNodeName(const QStringList &existingNames);

// Resolve a Vultr API key this node already stores as a device-local Actions
// variable (same contract as cloudflareApiTokenFromVariables).
QString vultrApiKeyFromVariables(const QMap<QString, QString> &variables);

// True when a failed install's output shows the host could not obtain ForkMesh
// from the mesh at all — no online node to clone from, or no prebuilt release
// published for its platform. Retrying the same relay download can never fix
// either, so the provisioning flow switches to uploading this app's own binary
// (adhoc #408).
bool vultrInstallNeedsLocalBinary(const QString &installOutput);

// --- Destroying a Vultr mirror (adhoc #24) ---------------------------------
// The Hosts page's "Destroy" button deletes the VPS itself on the user's Vultr
// account (billing stops), unlike Uninstall (wipes ForkMesh, keeps the server)
// and Remove (forgets the host here only).

// The Vultr instance id recorded for a saved host when this app provisioned it,
// or empty when the host is not a Vultr instance we can address by id (another
// provider, or a host added before the id was recorded — those are resolved by
// address instead, see vultrInstanceIdForAddress).
QString savedHostVultrInstanceId(const QJsonObject &host);

// From GET /v2/instances: the id of the instance serving `address`, matched
// against main_ip, v6_main_ip and the instance label/hostname so a saved host
// stored under its DNS name still resolves. Empty when nothing matches, and
// also empty when more than one instance matches — destroying the wrong server
// is unrecoverable, so an ambiguous match must fail closed.
QString vultrInstanceIdForAddress(const QJsonArray &instances,
                                  const QString &address);

// Empty string when the key and instance id are safe to send to DELETE
// /v2/instances/{id}, otherwise a user-facing error. Same loose key shape as
// validateVultrMirrorRequest; the id must look like the UUID Vultr issues.
QString validateVultrDestroyRequest(const QString &apiKey,
                                    const QString &instanceId);

// --- Agent CLIs on a fresh mirror (adhoc #418) -----------------------------
// A brand-new mirror can install the Claude Code and Codex CLIs, but until it
// is signed in they cannot run a single session — and there is no browser on a
// headless VPS to sign in with. These helpers copy this device's own logins to
// the new node so it comes up able to run agent sessions on our access.
//
// Everything sensitive lives in the stdin payload only: the remote helper
// command is fixed and secret-free, exactly like the Actions configuration
// path, so nothing ever reaches argv, the process table or the install log.
struct AgentCliCredentials {
    QByteArray claudeCredentials;  // verbatim ~/.claude/.credentials.json
    QByteArray codexAuth;          // verbatim ~/.codex/auth.json
    // API-key fallbacks (ANTHROPIC_API_KEY, OPENAI_API_KEY) for the providers
    // this device has no CLI login for. A key is deliberately NOT sent
    // alongside that provider's login: the CLI then warns that auth "may not
    // work as expected" and silently switches to API billing.
    QMap<QString, QString> env;
};

// True when there is nothing to copy, so the caller can install the binaries
// and say plainly that the mirror still needs a login of its own.
bool agentCliCredentialsAreEmpty(const AgentCliCredentials &credentials);

// One log line naming what is being copied ("Claude Code login, OPENAI_API_KEY")
// — names only, never a value.
QString describeAgentCliCredentials(const AgentCliCredentials &credentials);

// Contents of the ~/.forkmesh/agent-env file sourced by the mirror's shells:
// one `export NAME='value'` per variable, single-quote escaped so no value can
// re-enter the remote shell as syntax. Invalid names are dropped.
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

// --- Cloudflare DNS for a fresh Vultr mirror (adhoc #331) ------------------
// A brand-new Vultr instance is only reachable at a raw address, so it never
// joins the mesh under a stable name the way the hand-provisioned mirrors do.
// These helpers derive the node's record in the operator's own Cloudflare zone
// so one-click provisioning ends with "<node>.<zone>" resolving to it. The
// record is DNS-only: the node answers on SSH and its own ports, and the
// proxied Tunnel hostname of the direct HTTPS gateway stays a separate record.

// The zone the mesh's mirror records live in, stored as a device-local Actions
// variable (the Control node page's saved zone wins over this).
QString cloudflareZoneNameFromVariables(const QMap<QString, QString> &variables);

// "<node>.<zone>" for a mirror node, lowercased. Empty when either half is
// missing or is not a plain DNS label/zone name, so a malformed pair can never
// reach the Cloudflare API.
QString vultrMirrorDnsHostname(const QString &nodeName,
                               const QString &zoneName);

// Exact DNS record body for the node: a DNS-only A record at automatic TTL,
// commented so the record is recognizable in the Cloudflare dashboard. Empty
// when the hostname or IPv4 address is malformed.
QJsonObject vultrMirrorDnsRecordPayload(const QString &hostname,
                                        const QString &ip);

// From GET /zones: the id of the requested zone, matched on the exact name.
// Empty unless exactly one zone matches, so an ambiguous token never writes.
QString cloudflareZoneId(const QJsonArray &zones, const QString &zoneName);

// From GET /zones/<id>/dns_records: the id of an existing record for this
// hostname, so repeated deploys of the same node name update in place instead
// of stacking duplicate answers. Empty when there is no single such record.
QString cloudflareDnsRecordId(const QJsonArray &records,
                              const QString &hostname,
                              const QString &recordType);

} // namespace forkmesh::control
