#include "ControlNode.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonArray>
#include <QRegularExpression>
#include <QStandardPaths>

#include <algorithm>
#include <cmath>

namespace forkmesh::control {
namespace {

bool isDnsName(const QString &value)
{
    if (value.isEmpty() || value.size() > 253 ||
        value.startsWith(QLatin1Char('.')) ||
        value.endsWith(QLatin1Char('.')) ||
        value.contains(QStringLiteral(".."))) {
        return false;
    }
    static const QRegularExpression labelPattern(
        QStringLiteral("^[a-z0-9](?:[a-z0-9-]{0,61}[a-z0-9])?$"));
    const QStringList labels = value.split(QLatin1Char('.'));
    if (labels.size() < 2)
        return false;
    for (const QString &label : labels) {
        if (!labelPattern.match(label).hasMatch())
            return false;
    }
    return true;
}

QString normalizedDnsName(const QString &value)
{
    return value.trimmed().toLower();
}

bool isBase64UrlPublicKey(const QString &value)
{
    if (!QRegularExpression(QStringLiteral("^[A-Za-z0-9_-]{43}$"))
             .match(value)
             .hasMatch()) {
        return false;
    }
    return QByteArray::fromBase64(
               value.toLatin1(), QByteArray::Base64UrlEncoding)
               .size() == 32;
}

bool isSafeMainRelayUrl(const QString &value)
{
    if (value.trimmed().isEmpty())
        return true;
    const QUrl url(value.trimmed());
    if (!url.isValid() || url.scheme() != QLatin1String("https") ||
        url.host().isEmpty() || !url.userInfo().isEmpty() ||
        !url.query().isEmpty() || !url.fragment().isEmpty()) {
        return false;
    }
    return url.path().isEmpty() || url.path() == QLatin1String("/");
}

QString canonicalCandidate(const QString &path)
{
    const QFileInfo info(path);
    if (!info.exists() || !info.isFile() || !info.isReadable())
        return {};
    return info.canonicalFilePath();
}

QStringList installedToolCandidates(const QString &fileName,
                                    const QString &applicationDir)
{
    QStringList candidates;
    const QString resourceOverride =
        qEnvironmentVariable("FORKMESH_RESOURCE_DIR").trimmed();
    if (!resourceOverride.isEmpty()) {
        candidates.append(
            QDir(resourceOverride)
                .absoluteFilePath(QStringLiteral("tools/") + fileName));
    }
    const QString appDir =
        applicationDir.trimmed().isEmpty()
            ? QCoreApplication::applicationDirPath()
            : applicationDir.trimmed();
    // Linux CMake installs use <prefix>/bin + <prefix>/share/forkmesh.
    candidates.append(
        QDir(appDir).absoluteFilePath(
            QStringLiteral("../share/forkmesh/tools/") + fileName));
    // A macOS bundle uses ForkMesh.app/Contents/{MacOS,Resources}.
    candidates.append(
        QDir(appDir).absoluteFilePath(
            QStringLiteral("../Resources/forkmesh/tools/") + fileName));
    // The per-user Windows/NSIS layout keeps resources beside the executable.
    candidates.append(
        QDir(appDir).absoluteFilePath(
            QStringLiteral("resources/forkmesh/tools/") + fileName));
    return candidates;
}

bool hasCompleteWorkerBundle(const QString &scriptPath)
{
    const QDir root(QFileInfo(scriptPath).absoluteDir().absoluteFilePath(
        QStringLiteral("..")));
    const QDir worker(root.absoluteFilePath(QStringLiteral("cloudflare_worker")));
    const QStringList files{
        QStringLiteral("wrangler.toml"),
        QStringLiteral("pywrangler.sh"),
        QStringLiteral("pyproject.toml"),
        QStringLiteral("tools/build_dashboard_assets.py"),
        QStringLiteral("src/entry.py"),
    };
    for (const QString &file : files) {
        const QFileInfo info(worker.absoluteFilePath(file));
        if (!info.isFile() || !info.isReadable())
            return false;
    }
    for (const QString &directory :
         {QStringLiteral("public"), QStringLiteral("migrations")}) {
        const QFileInfo info(worker.absoluteFilePath(directory));
        if (!info.isDir() || !info.isReadable())
            return false;
    }
    return true;
}

bool appendPythonJsonString(const QString &value, QByteArray *output)
{
    if (!output)
        return false;
    output->append('"');
    for (qsizetype i = 0; i < value.size(); ++i) {
        const ushort code = value.at(i).unicode();
        switch (code) {
        case '"':
            output->append("\\\"");
            break;
        case '\\':
            output->append("\\\\");
            break;
        case '\b':
            output->append("\\b");
            break;
        case '\f':
            output->append("\\f");
            break;
        case '\n':
            output->append("\\n");
            break;
        case '\r':
            output->append("\\r");
            break;
        case '\t':
            output->append("\\t");
            break;
        default:
            if (code < 0x20 || code >= 0x7f) {
                output->append("\\u");
                output->append(
                    QByteArray::number(code, 16).rightJustified(4, '0'));
            } else {
                output->append(char(code));
            }
            break;
        }
    }
    output->append('"');
    return true;
}

bool appendPythonCanonicalJson(const QJsonValue &value, QByteArray *output,
                               QString *error)
{
    if (value.isNull() || value.isUndefined()) {
        output->append("null");
        return true;
    }
    if (value.isBool()) {
        output->append(value.toBool() ? "true" : "false");
        return true;
    }
    if (value.isDouble()) {
        const double number = value.toDouble();
        const qint64 integer = qint64(number);
        if (!std::isfinite(number) || double(integer) != number) {
            if (error)
                *error = QStringLiteral(
                    "Catalog canonical JSON accepts integer numbers only.");
            return false;
        }
        output->append(QByteArray::number(integer));
        return true;
    }
    if (value.isString())
        return appendPythonJsonString(value.toString(), output);
    if (value.isArray()) {
        output->append('[');
        const QJsonArray array = value.toArray();
        for (qsizetype i = 0; i < array.size(); ++i) {
            if (i)
                output->append(',');
            if (!appendPythonCanonicalJson(array.at(i), output, error))
                return false;
        }
        output->append(']');
        return true;
    }
    if (value.isObject()) {
        output->append('{');
        const QJsonObject object = value.toObject();
        QStringList keys = object.keys();
        std::sort(keys.begin(), keys.end());
        bool first = true;
        for (const QString &key : keys) {
            if (!first)
                output->append(',');
            first = false;
            appendPythonJsonString(key, output);
            output->append(':');
            if (!appendPythonCanonicalJson(object.value(key), output, error))
                return false;
        }
        output->append('}');
        return true;
    }
    if (error)
        *error = QStringLiteral("Catalog canonical JSON contains an unsupported value.");
    return false;
}

} // namespace

QString validateCloudflareBootstrapRequest(
    const CloudflareBootstrapRequest &request,
    bool allowAutomaticTopology)
{
    const QString hostname = normalizedDnsName(request.hostname);
    const QString zone = normalizedDnsName(request.zoneName);
    const bool automaticTopology =
        allowAutomaticTopology && hostname.isEmpty() && zone.isEmpty();
    if (!automaticTopology) {
        if (!isDnsName(hostname))
            return QStringLiteral(
                "Enter a DNS hostname such as mirror.example.com (without a URL or path).");
        if (!isDnsName(zone))
            return QStringLiteral(
                "Enter the Cloudflare zone name such as example.com.");
        if (hostname != zone &&
            !hostname.endsWith(QLatin1Char('.') + zone)) {
            return QStringLiteral(
                "The deployment hostname must be inside the selected zone.");
        }
    }

    static const QRegularExpression resourceName(
        QStringLiteral("^[a-z][a-z0-9-]{0,62}$"));
    const QString node = request.nodeName.trimmed();
    if (!node.isEmpty() && !resourceName.match(node).hasMatch()) {
        return QStringLiteral(
            "The node name must start with a letter and contain only lowercase "
            "letters, digits, or hyphens (63 characters maximum).");
    }
    const QString account = request.accountId.trimmed();
    static const QRegularExpression accountId(
        QStringLiteral("^[A-Za-z0-9_-]{1,128}$"));
    if (!account.isEmpty() && !accountId.match(account).hasMatch())
        return QStringLiteral("The Cloudflare account ID contains invalid characters.");
    const QString label = request.relayLabel.trimmed();
    if (label.size() > 80) {
        return QStringLiteral("The relay label must be 80 characters or fewer.");
    }
    for (const QChar ch : label) {
        if (ch.unicode() < 32 || ch.unicode() == 127)
            return QStringLiteral("The relay label contains a control character.");
    }
    if (!isSafeMainRelayUrl(request.mainRelayUrl)) {
        return QStringLiteral(
            "The upstream relay must be an HTTPS origin with no credentials, "
            "query, fragment, or private path.");
    }
    return {};
}

QString findCloudflareBootstrapScript(const QString &sourceDir,
                                      const QString &applicationDir)
{
    QStringList candidates;
    const QString overridePath =
        qEnvironmentVariable("FORKMESH_CLOUDFLARE_BOOTSTRAP").trimmed();
    if (!overridePath.isEmpty())
        candidates.append(overridePath);

    const QString source = sourceDir.trimmed();
    if (!source.isEmpty()) {
        candidates.append(
            QDir(source).absoluteFilePath(QStringLiteral("../tools/cloudflare_bootstrap.py")));
        candidates.append(
            QDir(source).absoluteFilePath(QStringLiteral("tools/cloudflare_bootstrap.py")));
    }

    const QString appDir = applicationDir.trimmed().isEmpty()
                               ? QCoreApplication::applicationDirPath()
                               : applicationDir.trimmed();
    candidates.append(
        QDir(appDir).absoluteFilePath(QStringLiteral("../tools/cloudflare_bootstrap.py")));
    candidates.append(
        QDir(appDir).absoluteFilePath(QStringLiteral("tools/cloudflare_bootstrap.py")));
    candidates.append(
        installedToolCandidates(
            QStringLiteral("cloudflare_bootstrap.py"), appDir));

    for (const QString &candidate : candidates) {
        const QString resolved = canonicalCandidate(candidate);
        if (!resolved.isEmpty() && hasCompleteWorkerBundle(resolved))
            return resolved;
    }
    return {};
}

namespace {

QString findPinnedTool(const QString &fileName, const QString &overrideName,
                       const QString &sourceDir,
                       const QString &applicationDir)
{
    QStringList candidates;
    const QString overridePath =
        qEnvironmentVariable(overrideName.toLatin1().constData()).trimmed();
    if (!overridePath.isEmpty())
        candidates.append(overridePath);
    const QString source = sourceDir.trimmed();
    if (!source.isEmpty()) {
        candidates.append(
            QDir(source).absoluteFilePath(
                QStringLiteral("../tools/") + fileName));
        candidates.append(
            QDir(source).absoluteFilePath(
                QStringLiteral("tools/") + fileName));
    }
    const QString appDir =
        applicationDir.trimmed().isEmpty()
            ? QCoreApplication::applicationDirPath()
            : applicationDir.trimmed();
    candidates.append(
        QDir(appDir).absoluteFilePath(
            QStringLiteral("../tools/") + fileName));
    candidates.append(
        QDir(appDir).absoluteFilePath(
            QStringLiteral("tools/") + fileName));
    candidates.append(installedToolCandidates(fileName, appDir));
    for (const QString &candidate : candidates) {
        const QString resolved = canonicalCandidate(candidate);
        if (!resolved.isEmpty())
            return resolved;
    }
    return {};
}

} // namespace

QString findCloudflareTunnelBootstrapScript(
    const QString &sourceDir, const QString &applicationDir)
{
    return findPinnedTool(
        QStringLiteral("cloudflare_tunnel_bootstrap.py"),
        QStringLiteral("FORKMESH_CLOUDFLARE_TUNNEL_BOOTSTRAP"),
        sourceDir, applicationDir);
}

QString findMirrorGatewayScript(
    const QString &sourceDir, const QString &applicationDir)
{
    return findPinnedTool(
        QStringLiteral("mirror_gateway.py"),
        QStringLiteral("FORKMESH_MIRROR_GATEWAY"),
        sourceDir, applicationDir);
}

QString findCloudflaredInstallerScript(
    const QString &sourceDir, const QString &applicationDir)
{
    return findPinnedTool(
        QStringLiteral("cloudflared_install.py"),
        QStringLiteral("FORKMESH_CLOUDFLARED_INSTALLER"),
        sourceDir, applicationDir);
}

QString shlexQuote(const QString &value)
{
    QString escaped = value;
    escaped.replace(QLatin1Char('\''), QStringLiteral("'\"'\"'"));
    return QLatin1Char('\'') + escaped + QLatin1Char('\'');
}

QByteArray pythonCanonicalJson(const QJsonValue &value, QString *error)
{
    QByteArray output;
    if (!appendPythonCanonicalJson(value, &output, error))
        return {};
    if (error)
        error->clear();
    return output;
}

QByteArray catalogV2SigningPayload(QJsonObject normalizedRecord,
                                   QString *error)
{
    normalizedRecord.remove(QStringLiteral("signature"));
    const QByteArray canonical =
        pythonCanonicalJson(normalizedRecord, error);
    if (canonical.isEmpty())
        return {};
    const QByteArray digest =
        QCryptographicHash::hash(canonical, QCryptographicHash::Sha256).toHex();
    if (error)
        error->clear();
    return QByteArrayLiteral("forkmesh-catalog-v2\n") + digest;
}

QByteArray privateReplicaRouteSigningPayload(
    const QString &owner, const QString &repository, const QString &node,
    const QString &opaqueId, const QString &replicaSha256, quint64 keyEpoch,
    bool active, qint64 issuedAtMs, QString *error)
{
    const auto cleanSegment = [](const QString &value) {
        return !value.trimmed().isEmpty() &&
               !value.contains(QLatin1Char('\n')) &&
               !value.contains(QLatin1Char('\r')) &&
               value == value.trimmed();
    };
    static const QRegularExpression digestPattern(
        QStringLiteral("^[0-9a-f]{64}$"));
    if (!cleanSegment(owner) || !cleanSegment(repository) ||
        !cleanSegment(node) || !digestPattern.match(opaqueId).hasMatch() ||
        !digestPattern.match(replicaSha256).hasMatch() || keyEpoch == 0 ||
        issuedAtMs <= 0) {
        if (error)
            *error = QStringLiteral("The private-replica route fields are invalid.");
        return {};
    }
    if (error)
        error->clear();
    return QByteArrayLiteral("forkmesh-private-route-v1\n") +
           owner.toUtf8() + '\n' + repository.toUtf8() + '\n' +
           node.toUtf8() + '\n' + opaqueId.toUtf8() + '\n' +
           replicaSha256.toUtf8() + '\n' +
           QByteArray::number(keyEpoch) + '\n' +
           (active ? QByteArrayLiteral("1") : QByteArrayLiteral("0")) +
           '\n' + QByteArray::number(issuedAtMs);
}

CloudflareBootstrapCommand buildCloudflareBootstrapCommand(
    const CloudflareBootstrapRequest &request,
    const QString &apiToken,
    const QString &scriptPath,
    const QString &pythonProgram,
    const QString &signerProgram,
    const QString &nodePublicKey)
{
    CloudflareBootstrapCommand command;
    command.program = pythonProgram.trimmed();
    command.arguments = {scriptPath};
    if (request.hostname.trimmed().isEmpty() &&
        request.zoneName.trimmed().isEmpty()) {
        command.arguments << QStringLiteral("--auto-configure");
    } else {
        command.arguments
            << QStringLiteral("--hostname")
            << normalizedDnsName(request.hostname)
            << QStringLiteral("--zone")
            << normalizedDnsName(request.zoneName);
    }
    if (!request.accountId.trimmed().isEmpty()) {
        command.arguments << QStringLiteral("--account-id")
                          << request.accountId.trimmed();
    }
    if (!request.nodeName.trimmed().isEmpty()) {
        command.arguments << QStringLiteral("--node-name")
                          << request.nodeName.trimmed();
    }
    if (!request.relayLabel.trimmed().isEmpty()) {
        command.arguments << QStringLiteral("--relay-label")
                          << request.relayLabel.trimmed();
    }
    // Passing an explicit empty value creates a standalone/main relay; otherwise
    // the bootstrapper links the new relay into the selected upstream mesh.
    command.arguments << QStringLiteral("--main-relay-url")
                      << request.mainRelayUrl.trimmed();
    if (request.dryRun)
        command.arguments << QStringLiteral("--dry-run");
    command.arguments << QStringLiteral("--json-stdout");

    if (!nodePublicKey.trimmed().isEmpty() &&
        !signerProgram.trimmed().isEmpty()) {
        command.arguments << QStringLiteral("--mirror-public-key")
                          << nodePublicKey.trimmed()
                          << QStringLiteral("--manifest-signer-command")
                          << (shlexQuote(signerProgram.trimmed()) +
                              QStringLiteral(" --sign-mirror-manifest"));
    } else {
        // A deployment can still provide routing without publishing a trust
        // manifest. The UI normally supplies the local identity signer; this is
        // only the explicit safe fallback when no identity exists.
        command.arguments << QStringLiteral("--skip-mirror-manifest");
    }

    command.environment = QProcessEnvironment::systemEnvironment();
    for (const QString &name : {
             QStringLiteral("CLOUDFLARE_API_TOKEN"),
             QStringLiteral("CF_API_TOKEN"),
             QStringLiteral("CLOUDFLARE_TOKEN"),
             QStringLiteral("CF_TOKEN"),
         }) {
        command.environment.remove(name);
    }
    command.environment.insert(QStringLiteral("CLOUDFLARE_API_TOKEN"), apiToken);
    return command;
}

QJsonObject parseCloudflareBootstrapResult(const QByteArray &output,
                                           QString *error)
{
    constexpr qsizetype kMaximumOutputBytes = 1024 * 1024;
    const QByteArray prefix("FORKMESH_BOOTSTRAP_RESULT=");
    if (output.size() > kMaximumOutputBytes) {
        if (error)
            *error = QStringLiteral("Cloudflare bootstrap output was too large.");
        return {};
    }
    QByteArray encoded;
    int matches = 0;
    for (const QByteArray &rawLine : output.split('\n')) {
        const QByteArray line = rawLine.trimmed();
        if (!line.startsWith(prefix))
            continue;
        ++matches;
        encoded = line.mid(prefix.size());
    }
    if (matches != 1 || encoded.isEmpty() || encoded.size() > 256 * 1024) {
        if (error)
            *error = QStringLiteral(
                "Cloudflare bootstrap returned no unique machine result.");
        return {};
    }
    const QByteArray json = QByteArray::fromBase64(
        encoded, QByteArray::Base64UrlEncoding |
                     QByteArray::AbortOnBase64DecodingErrors);
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(json, &parseError);
    if (json.isEmpty() || parseError.error != QJsonParseError::NoError ||
        !document.isObject()) {
        if (error)
            *error = QStringLiteral(
                "Cloudflare bootstrap returned an invalid machine result.");
        return {};
    }
    const QJsonObject object = document.object();
    if (!object.value(QStringLiteral("ok")).toBool()) {
        if (error)
            *error = QStringLiteral(
                "Cloudflare bootstrap did not confirm a successful result.");
        return {};
    }
    if (error)
        error->clear();
    return object;
}

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
    const QString &connectorTokenPath)
{
    CloudflareBootstrapCommand command;
    command.program = pythonProgram.trimmed();
    command.arguments = {
        scriptPath,
        QStringLiteral("--hostname"),
        normalizedDnsName(mirrorHostname),
        QStringLiteral("--zone"),
        normalizedDnsName(request.zoneName),
        QStringLiteral("--node-name"),
        request.nodeName.trimmed().toLower(),
        QStringLiteral("--origin-host"),
        QStringLiteral("127.0.0.1"),
        QStringLiteral("--origin-port"),
        QStringLiteral("8790"),
        QStringLiteral("--gateway-config"),
        gatewayConfigPath,
        QStringLiteral("--mirror-public-key"),
        nodePublicKey.trimmed(),
        QStringLiteral("--manifest-signer-command"),
        shlexQuote(signerProgram.trimmed()) +
            QStringLiteral(" --sign-mirror-manifest"),
        QStringLiteral("--manifest-output"),
        manifestOutputPath,
        QStringLiteral("--tunnel-token-file"),
        connectorTokenPath,
    };
    if (!request.accountId.trimmed().isEmpty()) {
        command.arguments << QStringLiteral("--account-id")
                          << request.accountId.trimmed();
    }
    if (request.dryRun)
        command.arguments << QStringLiteral("--dry-run");
    command.environment = QProcessEnvironment::systemEnvironment();
    for (const QString &name : {
             QStringLiteral("CLOUDFLARE_API_TOKEN"),
             QStringLiteral("CF_API_TOKEN"),
             QStringLiteral("CLOUDFLARE_TOKEN"),
             QStringLiteral("CF_TOKEN"),
         }) {
        command.environment.remove(name);
    }
    command.environment.insert(
        QStringLiteral("CLOUDFLARE_API_TOKEN"), apiToken);
    command.environment.insert(
        QStringLiteral("PYTHONUNBUFFERED"), QStringLiteral("1"));
    return command;
}

QByteArray httpsMirrorRegistrationSigningPayload(
    const QString &node, const QString &baseUrl,
    const QString &publicKey, qint64 issuedAtMs, QString *error)
{
    static const QRegularExpression nodePattern(
        QStringLiteral("^[a-z](?:[a-z0-9-]{0,61}[a-z0-9])?$"));
    static const QRegularExpression keyPattern(
        QStringLiteral("^[A-Za-z0-9_-]{43}$"));
    QUrl url(baseUrl.trimmed(), QUrl::StrictMode);
    if (!nodePattern.match(node).hasMatch() ||
        !keyPattern.match(publicKey).hasMatch() ||
        !url.isValid() ||
        url.scheme() != QLatin1String("https") ||
        url.host().isEmpty() || !url.userInfo().isEmpty() ||
        !url.query().isEmpty() || !url.fragment().isEmpty() ||
        (url.path() != QLatin1String("") &&
         url.path() != QLatin1String("/")) ||
        (url.port(-1) != -1 && url.port(-1) != 443) ||
        issuedAtMs <= 0) {
        if (error)
            *error = QStringLiteral(
                "The direct-HTTPS endpoint registration fields are invalid.");
        return {};
    }
    url.setScheme(QStringLiteral("https"));
    url.setPath(QString());
    url.setPort(-1);
    const QString normalized =
        url.toString(QUrl::RemoveUserInfo | QUrl::StripTrailingSlash)
            .toLower();
    if (error)
        error->clear();
    return QByteArrayLiteral("forkmesh-https-endpoint-v1\n") +
           node.toUtf8() + '\n' + normalized.toUtf8() + '\n' +
           publicKey.toUtf8() + '\n' +
           QByteArray::number(issuedAtMs);
}

QString redactProcessOutput(const QString &text,
                            const QStringList &exactSecrets)
{
    QString safe = text;
    safe.remove(QChar(u'\0'));
    // Strip terminal control sequences before inserting output into a rich UI.
    static const QRegularExpression ansi(
        QStringLiteral("\\x1B(?:\\[[0-?]*[ -/]*[@-~]|\\][^\\x07]*(?:\\x07|\\x1B\\\\))"));
    safe.remove(ansi);

    for (const QString &secret : exactSecrets) {
        if (!secret.isEmpty())
            safe.replace(secret, QStringLiteral("<redacted>"));
    }

    static const QRegularExpression bearer(
        QStringLiteral("(?i)\\bBearer\\s+[A-Za-z0-9._~+/-]{8,}"));
    safe.replace(bearer, QStringLiteral("Bearer <redacted>"));
    static const QRegularExpression namedCredential(
        QStringLiteral(
            "(?i)\\b(CLOUDFLARE_API_TOKEN|CF_API_TOKEN|CLOUDFLARE_TOKEN|"
            "CF_TOKEN|authorization|api[_ -]?token|password|secret)"
            "(\\s*(?::|=)\\s*|\\s+Bearer\\s+)[^\\s,;]+"));
    safe.replace(namedCredential, QStringLiteral("\\1=<redacted>"));
    return safe;
}

bool isValidSolanaPublicAddress(const QString &address)
{
    const QByteArray input = address.trimmed().toLatin1();
    if (input.size() < 32 || input.size() > 44)
        return false;
    static const QByteArray alphabet(
        "123456789ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz");
    QByteArray decoded(1, '\0');
    for (const char ch : input) {
        const int value = alphabet.indexOf(ch);
        if (value < 0)
            return false;
        int carry = value;
        for (int i = decoded.size() - 1; i >= 0; --i) {
            carry += static_cast<unsigned char>(decoded.at(i)) * 58;
            decoded[i] = char(carry & 0xff);
            carry >>= 8;
        }
        while (carry > 0) {
            decoded.prepend(char(carry & 0xff));
            carry >>= 8;
        }
    }
    int leadingZeroes = 0;
    while (leadingZeroes < input.size() && input.at(leadingZeroes) == '1')
        ++leadingZeroes;
    while (!decoded.isEmpty() && decoded.front() == '\0')
        decoded.remove(0, 1);
    decoded.prepend(QByteArray(leadingZeroes, '\0'));
    return decoded.size() == 32;
}

QUrl worldUrlForRelay(const QString &relayUrl)
{
    QString value = relayUrl.trimmed();
    if (value.isEmpty())
        return {};
    if (!value.contains(QStringLiteral("://")))
        value.prepend(QStringLiteral("https://"));
    QUrl url(value);
    if (!url.isValid() || url.host().isEmpty())
        return {};
    if (url.scheme() == QLatin1String("wss"))
        url.setScheme(QStringLiteral("https"));
    else if (url.scheme() == QLatin1String("ws"))
        url.setScheme(QStringLiteral("http"));
    if (url.scheme() != QLatin1String("https") &&
        url.scheme() != QLatin1String("http")) {
        return {};
    }
    url.setUserInfo(QString());
    url.setPath(QStringLiteral("/world/"));
    url.setQuery(QString());
    url.setFragment(QString());
    return url;
}

QByteArray mirrorManifestSigningPayload(const QJsonObject &request,
                                        const QString &expectedPublicKey,
                                        QString *error)
{
    auto fail = [error](const QString &message) {
        if (error)
            *error = message;
        return QByteArray();
    };
    if (!isBase64UrlPublicKey(expectedPublicKey))
        return fail(QStringLiteral("The local identity public key is invalid."));
    if (request.value(QStringLiteral("schemaVersion")).toInt() != 1 ||
        request.value(QStringLiteral("type")).toString() !=
            QLatin1String("forkmesh.mirror-endpoint-signing-request") ||
        request.value(QStringLiteral("algorithm")).toString() !=
            QLatin1String("Ed25519") ||
        request.value(QStringLiteral("encoding")).toString() !=
            QLatin1String("base64url-no-padding") ||
        request.value(QStringLiteral("canonicalization")).toString() !=
            QLatin1String("forkmesh-json-sort-v1")) {
        return fail(QStringLiteral("The manifest signing request has an unsupported format."));
    }
    if (request.value(QStringLiteral("publicKey")).toString() !=
        expectedPublicKey) {
        return fail(QStringLiteral("The manifest signing request targets another identity."));
    }
    const QString encoded =
        request.value(QStringLiteral("payloadBase64")).toString();
    if (encoded.isEmpty() || encoded.contains(QLatin1Char('=')) ||
        !QRegularExpression(QStringLiteral("^[A-Za-z0-9_-]+$"))
             .match(encoded)
             .hasMatch()) {
        return fail(QStringLiteral("The manifest signing payload is not valid base64url."));
    }
    const QByteArray payload = QByteArray::fromBase64(
        encoded.toLatin1(), QByteArray::Base64UrlEncoding);
    if (payload.isEmpty() || payload.size() > 64 * 1024)
        return fail(QStringLiteral("The manifest signing payload size is invalid."));
    const QString expectedDigest =
        QString::fromLatin1(
            QCryptographicHash::hash(payload, QCryptographicHash::Sha256).toHex());
    const QString suppliedDigest =
        request.value(QStringLiteral("payloadSha256")).toString().toLower();
    if (suppliedDigest.size() != 64 || suppliedDigest != expectedDigest) {
        return fail(QStringLiteral("The manifest signing payload checksum does not match."));
    }
    const QJsonDocument manifest = QJsonDocument::fromJson(payload);
    if (!manifest.isObject())
        return fail(QStringLiteral("The manifest signing payload is not a JSON object."));
    const QJsonObject root = manifest.object();
    if (root.contains(QStringLiteral("signature")) ||
        root.value(QStringLiteral("type")).toString() !=
            QLatin1String("forkmesh.mirror-endpoint")) {
        return fail(QStringLiteral("The manifest signing payload has an invalid type."));
    }
    if (error)
        error->clear();
    return payload;
}

} // namespace forkmesh::control
