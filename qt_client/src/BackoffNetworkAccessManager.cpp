#include "BackoffNetworkAccessManager.h"

#include "BackgroundActivity.h"

#include <QByteArray>
#include <QDateTime>
#include <QIODevice>
#include <QNetworkReply>
#include <QSet>
#include <QStringList>
#include <QTimer>

#include <algorithm>
#include <utility>

namespace {

// Base/cap for the host-wide 429 cooldown. Deliberately faster than
// NetworkBackoff's default poll cadence (60s base) so a single transient
// 429 doesn't stall one-shot user actions for a full minute, while a
// sustained quota outage still backs off to a low, capped steady-state rate.
constexpr qint64 kBaseMs = 3LL * 1000;
constexpr qint64 kCapMs = 5LL * 60 * 1000;
constexpr int kPayloadPreviewBytes = 16 * 1024;
constexpr int kPayloadPreviewChars = 8192;

QString endpointFullUrl(const QUrl &url)
{
    return url.toString(QUrl::RemoveUserInfo);
}

QString payloadPreview(const QByteArray &bytes, qint64 totalBytes = -1)
{
    if (bytes.isEmpty())
        return QStringLiteral("-");

    const qint64 knownTotal = totalBytes > 0 ? totalBytes : bytes.size();
    const QByteArray sample = bytes.left(kPayloadPreviewBytes);
    int controlBytes = 0;
    bool binary = false;
    for (char raw : sample) {
        const uchar byte = static_cast<uchar>(raw);
        if (byte == 0) {
            binary = true;
            break;
        }
        if (byte < 0x20 && byte != '\n' && byte != '\r' && byte != '\t')
            ++controlBytes;
    }
    if (binary || (sample.size() > 0 && controlBytes > sample.size() / 8))
        return QStringLiteral("<binary body: %1 bytes>").arg(knownTotal);

    QString text = QString::fromUtf8(sample.constData(), sample.size());
    text.replace(QLatin1Char('\r'), QStringLiteral("\\r"));
    text.replace(QLatin1Char('\n'), QStringLiteral("\\n"));
    text.replace(QLatin1Char('\t'), QStringLiteral("\\t"));
    bool truncated = knownTotal > sample.size();
    if (text.size() > kPayloadPreviewChars) {
        text = text.left(kPayloadPreviewChars);
        truncated = true;
    }
    if (truncated)
        text += QStringLiteral(" ... (%1 bytes)").arg(knownTotal);
    return text.trimmed().isEmpty() ? QStringLiteral("-") : text;
}

QString requestBodyPreview(QIODevice *device)
{
    if (!device)
        return QString();
    if (!device->isReadable())
        return QStringLiteral("<unreadable request body>");
    if (device->isSequential())
        return QStringLiteral("<streamed request body>");

    const qint64 knownTotal = qMax<qint64>(0, device->size());
    QByteArray sample = device->peek(kPayloadPreviewBytes + 1);
    if (sample.isEmpty() && knownTotal > 0) {
        const qint64 pos = device->pos();
        if (pos >= 0 && device->seek(pos)) {
            sample = device->read(kPayloadPreviewBytes + 1);
            device->seek(pos);
        }
    }
    if (sample.isEmpty())
        return knownTotal > 0
                   ? QStringLiteral("<request body: %1 bytes>").arg(knownTotal)
                   : QString();
    return payloadPreview(sample, knownTotal > 0 ? knownTotal : sample.size());
}

QString requestDataPreview(QNetworkAccessManager::Operation op,
                           const QUrl &url, QIODevice *outgoingData)
{
    const QString body = requestBodyPreview(outgoingData);
    if (!body.isEmpty())
        return body;
    if (url.hasQuery())
        return QStringLiteral("query: %1").arg(url.query(QUrl::FullyDecoded));
    switch (op) {
    case QNetworkAccessManager::PostOperation:
    case QNetworkAccessManager::PutOperation:
    case QNetworkAccessManager::CustomOperation:
        return QStringLiteral("<empty request body>");
    default:
        return QStringLiteral("-");
    }
}

QString responseDataPreview(QNetworkReply *reply, qint64 downloaded)
{
    QByteArray body = reply->peek(kPayloadPreviewBytes + 1);
    if (!body.isEmpty())
        return payloadPreview(body, downloaded > 0 ? downloaded : body.size());
    if (reply->error() != QNetworkReply::NoError)
        return reply->errorString();
    if (downloaded > 0)
        return QStringLiteral("<response body consumed before diagnostics: %1 bytes>")
            .arg(downloaded);
    return QStringLiteral("-");
}

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

class EndpointTransferTracker : public QObject
{
public:
    using QObject::QObject;
    qint64 uploaded = 0;
    qint64 downloaded = 0;
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

QNetworkReply *BackoffNetworkAccessManager::createNetworkRequest(
    Operation op, const QNetworkRequest &request, QIODevice *outgoingData)
{
    return QNetworkAccessManager::createRequest(op, request, outgoingData);
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

QList<BackoffNetworkAccessManager::EndpointStats>
BackoffNetworkAccessManager::endpointStats() const
{
    QList<EndpointStats> rows;
    rows.reserve(m_endpointStats.size());
    for (const EndpointStats &stats : m_endpointStats) {
        EndpointStats summary = stats;
        summary.requests.clear();
        rows.append(summary);
    }
    std::sort(rows.begin(), rows.end(), [](const EndpointStats &a,
                                           const EndpointStats &b) {
        return a.lastCalledMs > b.lastCalledMs;
    });
    return rows;
}

QList<BackoffNetworkAccessManager::RequestRecord>
BackoffNetworkAccessManager::endpointRequests(const QString &method,
                                              const QString &endpoint) const
{
    Q_UNUSED(method);
    QList<RequestRecord> rows;
    for (const EndpointStats &stats : m_endpointStats) {
        if (stats.endpoint == endpoint) {
            rows = stats.requests;
            break;
        }
    }
    std::sort(rows.begin(), rows.end(), [](const RequestRecord &a,
                                           const RequestRecord &b) {
        return qMax(a.finishedMs, a.startedMs) >
               qMax(b.finishedMs, b.startedMs);
    });
    return rows;
}

bool BackoffNetworkAccessManager::hostInCooldown(const QString &host) const
{
    // Same channel key createRequest() uses: the lowercased URL host.
    return !m_backoff.ready(normalizedHost(host),
                            QDateTime::currentMSecsSinceEpoch());
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

QString BackoffNetworkAccessManager::endpointStatsUrl(const QUrl &url)
{
    QUrl grouped = url;
    grouped.setQuery(QString());
    grouped.setFragment(QString());
    return endpointFullUrl(grouped);
}

QString BackoffNetworkAccessManager::endpointStatsKey(Operation op, const QUrl &url)
{
    Q_UNUSED(op);
    return endpointStatsUrl(url);
}

BackoffNetworkAccessManager::EndpointStats &
BackoffNetworkAccessManager::endpointStatsFor(Operation op, const QUrl &url)
{
    EndpointStats &stats = m_endpointStats[endpointStatsKey(op, url)];
    const QString method = firewallMethodName(op);
    if (stats.method.isEmpty()) {
        stats.method = method;
    } else {
        const QStringList methods =
            stats.method.split(QStringLiteral(", "), Qt::SkipEmptyParts);
        if (!methods.contains(method))
            stats.method += QStringLiteral(", ") + method;
    }
    stats.endpoint = endpointStatsUrl(url);
    return stats;
}

int BackoffNetworkAccessManager::noteEndpointAttempt(Operation op,
                                                     const QUrl &url,
                                                     const QString &requestData)
{
    EndpointStats &stats = endpointStatsFor(op, url);
    ++stats.calls;
    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    stats.lastFullUrl = endpointFullUrl(url);
    stats.lastRequestData = requestData.isEmpty() ? QStringLiteral("-")
                                                  : requestData;
    stats.lastResponseData = QStringLiteral("Pending");
    stats.lastCalledMs = nowMs;

    RequestRecord record;
    record.method = firewallMethodName(op);
    record.url = stats.lastFullUrl;
    record.requestData = stats.lastRequestData;
    record.responseData = stats.lastResponseData;
    record.startedMs = nowMs;
    stats.requests.append(record);
    emit endpointStatsChanged();
    return stats.requests.size() - 1;
}

void BackoffNetworkAccessManager::noteEndpointAllowed(Operation op, const QUrl &url)
{
    EndpointStats &stats = endpointStatsFor(op, url);
    ++stats.allowed;
    stats.firewallDecision = QStringLiteral("Allowed");
    stats.lastCalledMs = QDateTime::currentMSecsSinceEpoch();
    emit endpointStatsChanged();
}

void BackoffNetworkAccessManager::noteEndpointBlocked(Operation op, const QUrl &url,
                                                      const QString &reason,
                                                      int requestIndex)
{
    EndpointStats &stats = endpointStatsFor(op, url);
    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    ++stats.blocked;
    ++stats.failures;
    stats.firewallDecision = QStringLiteral("Blocked");
    stats.lastError = reason;
    stats.lastResponseData = reason;
    stats.lastStatus = 0;
    stats.lastCalledMs = nowMs;
    if (requestIndex >= 0 && requestIndex < stats.requests.size()) {
        RequestRecord &record = stats.requests[requestIndex];
        record.responseData = reason;
        record.error = reason;
        record.blocked = true;
        record.finishedMs = nowMs;
    }
    emit endpointStatsChanged();
}

void BackoffNetworkAccessManager::noteEndpointFinished(Operation op, const QUrl &url,
                                                       qint64 uploaded,
                                                       qint64 downloaded,
                                                       int status,
                                                       const QString &error,
                                                       int requestIndex,
                                                       const QString &responseData)
{
    EndpointStats &stats = endpointStatsFor(op, url);
    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    stats.uploadBytes += qMax<qint64>(0, uploaded);
    stats.downloadBytes += qMax<qint64>(0, downloaded);
    stats.lastStatus = status;
    stats.lastError = error;
    stats.lastResponseData = responseData.isEmpty()
                                 ? (error.isEmpty() ? QStringLiteral("-")
                                                    : error)
                                 : responseData;
    stats.lastCalledMs = nowMs;
    if (status >= 400 || !error.isEmpty())
        ++stats.failures;
    if (requestIndex >= 0 && requestIndex < stats.requests.size()) {
        RequestRecord &record = stats.requests[requestIndex];
        record.uploadBytes = qMax<qint64>(0, uploaded);
        record.downloadBytes = qMax<qint64>(0, downloaded);
        record.status = status;
        record.error = error;
        record.responseData = stats.lastResponseData;
        record.finishedMs = nowMs;
    }
    emit endpointStatsChanged();
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
    const QString requestData = requestDataPreview(op, url, outgoingData);
    const int requestIndex = noteEndpointAttempt(op, url, requestData);
    if (!firewallAllows(url)) {
        QString rule = firewallRuleForUrl(url);
        const QString method = firewallMethodName(op);
        const bool allowed = m_firewallPrompt && m_firewallPrompt(method, url, &rule);
        const QString canonical = canonicalFirewallRule(rule);
        if (allowed && !canonical.isEmpty())
            addFirewallRule(canonical);
        emit firewallRequestDecided(method, url, canonical, allowed);
        if (!allowed) {
            noteEndpointBlocked(op, url, QStringLiteral("Firewall blocked"),
                                requestIndex);
            return new FirewallDeniedReply(request, op, this);
        }
    }
    noteEndpointAllowed(op, url);

    auto createTrackedRequest = [&]() {
        const qint64 outgoingKnown =
            (outgoingData && !outgoingData->isSequential())
                ? qMax<qint64>(0, outgoingData->size())
                : 0;
        QNetworkReply *reply = createNetworkRequest(op, request, outgoingData);
        auto *tracker = new EndpointTransferTracker(reply);
        tracker->uploaded = outgoingKnown;
        // Footer background strip (adhoc #421): one "net" ticket per in-flight
        // reply. Retired on destruction rather than on finished() so aborted and
        // never-finished replies can't leave the spinner running forever.
        const quint64 activity = forkmesh::BackgroundActivity::begin(
            QStringLiteral("net"),
            firewallMethodName(op) + QLatin1Char(' ') + endpointStatsUrl(url));
        connect(reply, &QNetworkReply::finished, this, [reply, activity] {
            forkmesh::BackgroundActivity::end(
                activity,
                reply->error() == QNetworkReply::NoError
                    ? QStringLiteral("succeeded")
                    : QStringLiteral("failed"));
        });
        connect(reply, &QObject::destroyed, this,
                [activity] {
                    forkmesh::BackgroundActivity::end(
                        activity, QStringLiteral("cancelled"));
                });
        connect(reply, &QNetworkReply::uploadProgress, this,
                [tracker](qint64 sent, qint64) {
                    tracker->uploaded = qMax(tracker->uploaded, sent);
                });
        connect(reply, &QNetworkReply::downloadProgress, this,
                [tracker](qint64 received, qint64) {
                    tracker->downloaded = qMax(tracker->downloaded, received);
                });
        connect(reply, &QNetworkReply::finished, this,
                [this, reply, tracker, op, url, requestIndex] {
                    const QVariant code =
                        reply->attribute(QNetworkRequest::HttpStatusCodeAttribute);
                    const int status = code.isValid() ? code.toInt() : 0;
                    const QString error =
                        reply->error() == QNetworkReply::NoError
                            ? QString()
                            : reply->errorString();
                    const qint64 downloaded =
                        qMax(tracker->downloaded, reply->bytesAvailable());
                    noteEndpointFinished(
                        op, url, tracker->uploaded, downloaded, status, error,
                        requestIndex, responseDataPreview(reply, downloaded));
                });
        return reply;
    };

    if (!url.path().startsWith(QStringLiteral("/api/")))
        return createTrackedRequest();

    const QString channel = url.host().toLower();
    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    if (!m_backoff.ready(channel, nowMs)) {
        noteEndpointFinished(op, url, 0, 0, 0,
                             QStringLiteral("Rate-limit backoff suppressed"),
                             requestIndex,
                             QStringLiteral("Rate-limit backoff suppressed"));
        return new BackoffSuppressedReply(request, op, this);
    }

    // Host-tunnel CONTENT paths can legitimately return 5xx when the specific
    // desktop node serving them is offline (the worker maps a dead host DO to
    // 502/503) — that is not the relay overloading, so those must NOT trip the
    // host-wide cooldown. The core relay API (heartbeat, sync, repositories,
    // agents, mirrors, …) 5xx only when the Worker itself is failing.
    const QString lastSeg = url.path().section(QLatin1Char('/'), -1);
    static const QSet<QString> kTunnelOps = {
        QStringLiteral("tree"), QStringLiteral("blob"),
        QStringLiteral("blobs"), QStringLiteral("raw"),
        QStringLiteral("history"), QStringLiteral("commit"),
        QStringLiteral("branches"), QStringLiteral("search")};
    const bool tunnelContent =
        kTunnelOps.contains(lastSeg)
        || url.path().contains(QStringLiteral("/releases/blob/"));

    QNetworkReply *reply = createTrackedRequest();
    connect(reply, &QNetworkReply::finished, this,
            [this, reply, channel, tunnelContent] {
        const int code =
            reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        // 429 (rate limit) OR a Worker 5xx (Cloudflare 1101/1102 resource
        // exhaustion) trips the cooldown: during the 2026-07-11 outage every
        // core endpoint 500'd for 100 minutes while the client kept firing at
        // full cadence because only 429 was treated as backpressure.
        const bool overloaded =
            code == 429 || (code >= 500 && code <= 599 && !tunnelContent);
        if (overloaded)
            m_backoff.noteFailure(channel, QDateTime::currentMSecsSinceEpoch(),
                                  kBaseMs, kCapMs);
        else if (reply->error() == QNetworkReply::NoError)
            m_backoff.noteSuccess(channel);
    });
    return reply;
}
