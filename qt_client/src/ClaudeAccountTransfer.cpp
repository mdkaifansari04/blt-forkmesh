#include "ClaudeAccountTransfer.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonDocument>

namespace ClaudeAccountTransfer {

namespace {
constexpr auto kBundleKind = "forkmesh-claude-account";
constexpr int kBundleVersion = 1;
} // namespace

QString credentialsPath(const QString &home)
{
    return home + QStringLiteral("/.claude/.credentials.json");
}

QJsonObject readOauthObject(const QString &home)
{
    QFile credFile(credentialsPath(home));
    if (!credFile.open(QIODevice::ReadOnly))
        return QJsonObject();
    return QJsonDocument::fromJson(credFile.readAll())
        .object()
        .value(QStringLiteral("claudeAiOauth"))
        .toObject();
}

bool hasCredentials(const QString &home)
{
    return !readOauthObject(home)
                .value(QStringLiteral("accessToken"))
                .toString()
                .isEmpty();
}

QByteArray buildBundle(const QJsonObject &oauth, const QString &apiKey,
                       const QString &exportedFrom)
{
    QJsonObject bundle;
    bundle.insert(QStringLiteral("kind"), QLatin1String(kBundleKind));
    bundle.insert(QStringLiteral("v"), kBundleVersion);
    if (!oauth.isEmpty())
        bundle.insert(QStringLiteral("claudeAiOauth"), oauth);
    const QString key = apiKey.trimmed();
    if (!key.isEmpty())
        bundle.insert(QStringLiteral("anthropicApiKey"), key);
    if (!exportedFrom.isEmpty())
        bundle.insert(QStringLiteral("exportedFrom"), exportedFrom);
    return QJsonDocument(bundle).toJson(QJsonDocument::Compact);
}

QString encodeBundle(const QJsonObject &oauth, const QString &apiKey,
                     const QString &exportedFrom)
{
    return QString::fromLatin1(
        buildBundle(oauth, apiKey, exportedFrom).toBase64());
}

bool parseBundle(const QByteArray &input, QJsonObject &oauthOut,
                 QString &apiKeyOut, QString &err)
{
    oauthOut = QJsonObject();
    apiKeyOut.clear();

    const QByteArray trimmed = input.trimmed();
    if (trimmed.isEmpty()) {
        err = QStringLiteral("empty bundle");
        return false;
    }

    // Accept the base64 form or raw JSON: a keyfile someone saved may be either.
    // Only fall back to base64 when the text isn't already valid JSON so a stray
    // base64-looking JSON string can't be mis-decoded.
    QJsonParseError jsonErr{};
    QJsonDocument doc = QJsonDocument::fromJson(trimmed, &jsonErr);
    if (jsonErr.error != QJsonParseError::NoError || !doc.isObject()) {
        const QByteArray decoded =
            QByteArray::fromBase64(trimmed, QByteArray::AbortOnBase64DecodingErrors);
        doc = QJsonDocument::fromJson(decoded);
    }
    if (!doc.isObject()) {
        err = QStringLiteral("not a valid ForkMesh Claude account bundle");
        return false;
    }

    const QJsonObject bundle = doc.object();
    if (bundle.value(QStringLiteral("kind")).toString() !=
        QLatin1String(kBundleKind)) {
        err = QStringLiteral("not a ForkMesh Claude account bundle");
        return false;
    }
    if (bundle.value(QStringLiteral("v")).toInt() > kBundleVersion) {
        err = QStringLiteral(
            "bundle was exported by a newer ForkMesh; update this node first");
        return false;
    }

    oauthOut = bundle.value(QStringLiteral("claudeAiOauth")).toObject();
    apiKeyOut = bundle.value(QStringLiteral("anthropicApiKey")).toString().trimmed();
    if (oauthOut.value(QStringLiteral("accessToken")).toString().isEmpty() &&
        apiKeyOut.isEmpty()) {
        err = QStringLiteral("bundle carries no Claude Code login or API key");
        return false;
    }
    return true;
}

bool installOauth(const QString &home, const QJsonObject &oauth, QString &err)
{
    if (oauth.isEmpty())
        return true; // Nothing to install (API-key-only bundle); not an error.

    const QString dir = home + QStringLiteral("/.claude");
    if (!QDir().mkpath(dir)) {
        err = QStringLiteral("could not create ") + dir;
        return false;
    }

    // Preserve any sibling keys the CLI keeps alongside claudeAiOauth.
    const QString path = credentialsPath(home);
    QJsonObject creds;
    QFile existing(path);
    if (existing.open(QIODevice::ReadOnly)) {
        creds = QJsonDocument::fromJson(existing.readAll()).object();
        existing.close();
    }
    creds.insert(QStringLiteral("claudeAiOauth"), oauth);

    QFile out(path);
    if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        err = QStringLiteral("could not write ") + path;
        return false;
    }
    out.write(QJsonDocument(creds).toJson(QJsonDocument::Indented));
    out.close();
    // The file holds a live subscription token — keep it owner-only, matching how
    // the CLI itself stores it.
    QFile::setPermissions(path,
                          QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    return true;
}

QString describeOauth(const QJsonObject &oauth)
{
    const QString token = oauth.value(QStringLiteral("accessToken")).toString();
    if (token.isEmpty())
        return QStringLiteral("no Claude Code login");

    QString desc = QStringLiteral("Claude Code login");
    const QString sub =
        oauth.value(QStringLiteral("subscriptionType")).toString();
    if (!sub.isEmpty())
        desc += QStringLiteral(" (%1)").arg(sub);
    // Mask everything but the last 4 chars so status output never leaks the token.
    desc += QStringLiteral(", token …%1").arg(token.right(4));

    const qint64 expiresAt =
        static_cast<qint64>(oauth.value(QStringLiteral("expiresAt")).toDouble());
    if (expiresAt > 0) {
        const QDateTime when = QDateTime::fromMSecsSinceEpoch(expiresAt);
        desc += QStringLiteral(", expires %1")
                    .arg(when.toString(Qt::ISODate));
    }
    return desc;
}

} // namespace ClaudeAccountTransfer
