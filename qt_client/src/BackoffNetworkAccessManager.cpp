#include "BackoffNetworkAccessManager.h"

#include <QDateTime>
#include <QNetworkReply>
#include <QStringList>
#include <QTimer>

#include <utility>

namespace {

// Base/cap for the host-wide 429 cooldown. Deliberately faster than
// NetworkBackoff's default poll cadence (60s base) so a single transient
// 429 doesn't stall one-shot user actions for a full minute, while a
// sustained quota outage still backs off to a low, capped steady-state rate.
constexpr qint64 kBaseMs = 3LL * 1000;
constexpr qint64 kCapMs = 5LL * 60 * 1000;

// Stands in for a request that was suppressed by the backoff instead of
// reaching the network. Reports as a plain transport failure so every
// existing call site's error handling (which just checks reply->error() and
// surfaces/logs errorString()) keeps working unchanged.
class BackoffSuppressedReply : public QNetworkReply
{
public:
    BackoffSuppressedReply(const QNetworkRequest &request,
                            QNetworkAccessManager::Operation op, QObject *parent)
        : QNetworkReply(parent)
    {
        setRequest(request);
        setUrl(request.url());
        setOperation(op);
        setError(QNetworkReply::UnknownNetworkError,
                 QStringLiteral(
                     "ForkMesh relay is rate-limited (HTTP 429); backing off"));
        setOpenMode(QIODevice::ReadOnly);
        // Real replies always finish asynchronously and callers routinely
        // connect to finished()/errorOccurred() right after issuing the
        // request; emitting synchronously here would fire before those
        // connections exist.
        QTimer::singleShot(0, this, [this] {
            emit errorOccurred(error());
            setFinished(true);
            emit finished();
        });
    }

    void abort() override {}
    qint64 bytesAvailable() const override { return 0; }

protected:
    qint64 readData(char *, qint64) override { return -1; }
};

class FirewallDeniedReply : public QNetworkReply
{
public:
    FirewallDeniedReply(const QNetworkRequest &request,
                        QNetworkAccessManager::Operation op, QObject *parent)
        : QNetworkReply(parent)
    {
        setRequest(request);
        setUrl(request.url());
        setOperation(op);
        setError(QNetworkReply::ContentAccessDenied,
                 QStringLiteral("ForkMesh firewall blocked %1")
                     .arg(request.url().toDisplayString()));
        setOpenMode(QIODevice::ReadOnly);
        QTimer::singleShot(0, this, [this] {
            emit errorOccurred(error());
            setFinished(true);
            emit finished();
        });
    }

    void abort() override {}
    qint64 bytesAvailable() const override { return 0; }

protected:
    qint64 readData(char *, qint64) override { return -1; }
};

QString canonicalExactUrl(const QUrl &input)
{
    QUrl url = input;
    url.setScheme(url.scheme().toLower());
    url.setHost(url.host().toLower());
    if (url.path().isEmpty())
        url.setPath(QStringLiteral("/"));
    return url.toString(QUrl::RemoveUserInfo | QUrl::FullyEncoded);
}

} // namespace

BackoffNetworkAccessManager::BackoffNetworkAccessManager(QObject *parent)
    : QNetworkAccessManager(parent)
{
}

void BackoffNetworkAccessManager::setFirewallEnabled(bool enabled)
{
    m_firewallEnabled = enabled;
}

void BackoffNetworkAccessManager::setFirewallRules(const QStringList &rules)
{
    m_firewallRules.clear();
    for (const QString &rule : rules)
        addFirewallRule(rule);
}

bool BackoffNetworkAccessManager::addFirewallRule(const QString &rule)
{
    const QString canonical = canonicalFirewallRule(rule);
    if (canonical.isEmpty() || m_firewallRules.contains(canonical))
        return false;
    m_firewallRules.append(canonical);
    m_firewallRules.sort(Qt::CaseInsensitive);
    return true;
}

bool BackoffNetworkAccessManager::removeFirewallRule(const QString &rule)
{
    const QString canonical = canonicalFirewallRule(rule);
    return m_firewallRules.removeAll(canonical) > 0;
}

void BackoffNetworkAccessManager::setFirewallPrompt(FirewallPrompt prompt)
{
    m_firewallPrompt = std::move(prompt);
}

QString BackoffNetworkAccessManager::normalizedHost(const QString &host)
{
    return host.trimmed().toLower();
}

int BackoffNetworkAccessManager::normalizedPort(const QUrl &url)
{
    if (url.port() > 0)
        return url.port();
    const QString scheme = url.scheme().toLower();
    if (scheme == QLatin1String("https") || scheme == QLatin1String("wss"))
        return 443;
    if (scheme == QLatin1String("http") || scheme == QLatin1String("ws"))
        return 80;
    return -1;
}

QString BackoffNetworkAccessManager::canonicalFirewallRule(const QString &rule)
{
    QString text = rule.trimmed();
    if (text.isEmpty())
        return QString();
    if (text == QLatin1String("*"))
        return text;

    const QString lower = text.toLower();
    if (lower.startsWith(QStringLiteral("scheme:"))) {
        const QString scheme = lower.mid(7).trimmed();
        return scheme.isEmpty() ? QString() : QStringLiteral("scheme:") + scheme;
    }
    if (lower.startsWith(QStringLiteral("hostwild:"))) {
        const QString host = normalizedHost(text.mid(9));
        return host.isEmpty() ? QString() : QStringLiteral("hostwild:") + host;
    }
    if (lower.startsWith(QStringLiteral("hostport:"))) {
        const QString rest = text.mid(9).trimmed();
        const int colon = rest.lastIndexOf(QLatin1Char(':'));
        if (colon <= 0)
            return QString();
        bool ok = false;
        const int port = rest.mid(colon + 1).toInt(&ok);
        const QString host = normalizedHost(rest.left(colon));
        if (!ok || port <= 0 || host.isEmpty())
            return QString();
        return QStringLiteral("hostport:%1:%2").arg(host).arg(port);
    }
    if (lower.startsWith(QStringLiteral("host:"))) {
        const QString host = normalizedHost(text.mid(5));
        return host.isEmpty() ? QString() : QStringLiteral("host:") + host;
    }
    if (lower.startsWith(QStringLiteral("url:"))) {
        QUrl url(text.mid(4).trimmed());
        if (!url.isValid() || url.scheme().isEmpty() || url.host().isEmpty())
            return QString();
        return QStringLiteral("url:") + canonicalExactUrl(url);
    }

    if (lower.startsWith(QStringLiteral("*."))) {
        const QString host = normalizedHost(text.mid(2));
        return host.isEmpty() ? QString() : QStringLiteral("hostwild:") + host;
    }

    QUrl asUrl = QUrl::fromUserInput(text);
    if (asUrl.isValid() && !asUrl.scheme().isEmpty() && !asUrl.host().isEmpty()) {
        if (!asUrl.path().isEmpty() && asUrl.path() != QLatin1String("/"))
            return QStringLiteral("url:") + canonicalExactUrl(asUrl);
        if (asUrl.hasQuery())
            return QStringLiteral("url:") + canonicalExactUrl(asUrl);
        if (asUrl.port() > 0)
            return QStringLiteral("hostport:%1:%2")
                .arg(normalizedHost(asUrl.host()))
                .arg(normalizedPort(asUrl));
        return QStringLiteral("host:") + normalizedHost(asUrl.host());
    }

    const int colon = text.lastIndexOf(QLatin1Char(':'));
    if (colon > 0) {
        bool ok = false;
        const int port = text.mid(colon + 1).toInt(&ok);
        const QString host = normalizedHost(text.left(colon));
        if (ok && port > 0 && !host.isEmpty())
            return QStringLiteral("hostport:%1:%2").arg(host).arg(port);
    }

    const QString host = normalizedHost(text);
    return host.isEmpty() ? QString() : QStringLiteral("host:") + host;
}

QString BackoffNetworkAccessManager::firewallRuleForUrl(const QUrl &url,
                                                        bool includePort)
{
    if (url.host().isEmpty())
        return QString();
    if (includePort) {
        const int port = normalizedPort(url);
        if (port > 0)
            return QStringLiteral("hostport:%1:%2")
                .arg(normalizedHost(url.host()))
                .arg(port);
    }
    return QStringLiteral("host:") + normalizedHost(url.host());
}

QString BackoffNetworkAccessManager::firewallRuleForExactUrl(const QUrl &url)
{
    if (!url.isValid() || url.scheme().isEmpty() || url.host().isEmpty())
        return QString();
    return QStringLiteral("url:") + canonicalExactUrl(url);
}

QString BackoffNetworkAccessManager::firewallMethodName(Operation op)
{
    switch (op) {
    case HeadOperation:
        return QStringLiteral("HEAD");
    case GetOperation:
        return QStringLiteral("GET");
    case PutOperation:
        return QStringLiteral("PUT");
    case PostOperation:
        return QStringLiteral("POST");
    case DeleteOperation:
        return QStringLiteral("DELETE");
    case CustomOperation:
        return QStringLiteral("CUSTOM");
    default:
        return QStringLiteral("REQ");
    }
}

QString BackoffNetworkAccessManager::firewallRuleLabel(const QString &rule)
{
    const QString canonical = canonicalFirewallRule(rule);
    if (canonical == QLatin1String("*"))
        return QStringLiteral("All destinations");
    if (canonical.startsWith(QStringLiteral("scheme:")))
        return QStringLiteral("All %1 requests").arg(canonical.mid(7).toUpper());
    if (canonical.startsWith(QStringLiteral("hostwild:")))
        return QStringLiteral("Host *.%1").arg(canonical.mid(9));
    if (canonical.startsWith(QStringLiteral("hostport:")))
        return QStringLiteral("Host %1").arg(canonical.mid(9));
    if (canonical.startsWith(QStringLiteral("host:")))
        return QStringLiteral("Host %1").arg(canonical.mid(5));
    if (canonical.startsWith(QStringLiteral("url:")))
        return QStringLiteral("URL %1").arg(canonical.mid(4));
    return canonical;
}

bool BackoffNetworkAccessManager::firewallAllows(const QUrl &url) const
{
    if (!m_firewallEnabled)
        return true;
    if (!url.isValid() || url.host().isEmpty())
        return true;

    const QString scheme = url.scheme().toLower();
    const QString host = normalizedHost(url.host());
    const int port = normalizedPort(url);
    const QString exact = firewallRuleForExactUrl(url);
    const QString hostRule = firewallRuleForUrl(url);
    const QString hostPortRule = firewallRuleForUrl(url, true);
    for (const QString &rule : m_firewallRules) {
        if (rule == QLatin1String("*") || rule == hostRule ||
            rule == exact || (!hostPortRule.isEmpty() && rule == hostPortRule))
            return true;
        if (rule == QStringLiteral("scheme:") + scheme)
            return true;
        if (rule.startsWith(QStringLiteral("hostwild:"))) {
            const QString suffix = rule.mid(9);
            if (host == suffix || host.endsWith(QStringLiteral(".") + suffix))
                return true;
        }
        if (rule.startsWith(QStringLiteral("hostport:")) && port > 0) {
            const QString rest = rule.mid(9);
            const int colon = rest.lastIndexOf(QLatin1Char(':'));
            if (colon > 0 && rest.left(colon) == host &&
                rest.mid(colon + 1).toInt() == port)
                return true;
        }
    }
    return false;
}

QNetworkReply *BackoffNetworkAccessManager::createRequest(
    Operation op, const QNetworkRequest &request, QIODevice *outgoingData)
{
    const QUrl url = request.url();
    if (!firewallAllows(url)) {
        QString rule = firewallRuleForUrl(url);
        const QString method = firewallMethodName(op);
        const bool allowed = m_firewallPrompt && m_firewallPrompt(method, url, &rule);
        const QString canonical = canonicalFirewallRule(rule);
        if (allowed && !canonical.isEmpty())
            addFirewallRule(canonical);
        emit firewallRequestDecided(method, url, canonical, allowed);
        if (!allowed)
            return new FirewallDeniedReply(request, op, this);
    }

    if (!url.path().startsWith(QStringLiteral("/api/")))
        return QNetworkAccessManager::createRequest(op, request, outgoingData);

    const QString channel = url.host().toLower();
    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    if (!m_backoff.ready(channel, nowMs))
        return new BackoffSuppressedReply(request, op, this);

    QNetworkReply *reply = QNetworkAccessManager::createRequest(op, request, outgoingData);
    connect(reply, &QNetworkReply::finished, this, [this, reply, channel] {
        const QVariant code =
            reply->attribute(QNetworkRequest::HttpStatusCodeAttribute);
        if (code.toInt() == 429)
            m_backoff.noteFailure(channel, QDateTime::currentMSecsSinceEpoch(),
                                  kBaseMs, kCapMs);
        else if (reply->error() == QNetworkReply::NoError)
            m_backoff.noteSuccess(channel);
    });
    return reply;
}
