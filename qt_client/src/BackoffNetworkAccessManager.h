#pragma once

#include <QHash>
#include <QList>
#include <QNetworkAccessManager>
#include <QStringList>
#include <QUrl>

#include "NetworkBackoff.h"

#include <functional>

// Drop-in QNetworkAccessManager that gates every request against a
// "<host>/api/*" path through NetworkBackoff, keyed per host.
//
// Several pollers already avoid *firing* a request while they know they're
// in a cooldown (see MainWindow::m_pollBackoff), but plenty of call sites
// don't: one-shot issue/pull/discussion submissions, the top-bar relay
// latency probe, catalog publish, etc. When a relay's daily Cloudflare quota
// is exhausted it answers *every* request with an HTTP 429 page — including
// the lightweight /api/version probe — so without a host-wide gate, each of
// those independent call sites keeps hammering the relay on its own
// schedule, burning more of the very quota that's missing. This subclass
// catches all of them at the transport layer: the first 429 from a host
// starts an exponential cooldown (NetworkBackoff's normal doubling-with-
// jitter curve) during which further requests to that host's /api/* paths
// are answered locally with a synthetic error instead of ever reaching the
// network, and a subsequent success clears the streak.
class BackoffNetworkAccessManager : public QNetworkAccessManager
{
    Q_OBJECT
public:
    using FirewallPrompt = std::function<bool(const QString &method, const QUrl &url,
                                              QString *allowRuleOut)>;
    struct RequestRecord {
        QString method;
        QString url;
        QString requestData;
        QString responseData;
        qint64 uploadBytes = 0;
        qint64 downloadBytes = 0;
        qint64 startedMs = 0;
        qint64 finishedMs = 0;
        int status = 0;
        QString error;
        bool blocked = false;
    };

    struct EndpointStats {
        QString method;
        QString endpoint;
        QString firewallDecision;
        QString lastFullUrl;
        QString lastRequestData;
        QString lastResponseData;
        int calls = 0;
        int allowed = 0;
        int blocked = 0;
        int failures = 0;
        qint64 uploadBytes = 0;
        qint64 downloadBytes = 0;
        qint64 lastCalledMs = 0;
        int lastStatus = 0;
        QString lastError;
        QList<RequestRecord> requests;
    };

    explicit BackoffNetworkAccessManager(QObject *parent = nullptr);

    void setFirewallEnabled(bool enabled);
    bool firewallEnabled() const { return m_firewallEnabled; }
    void setFirewallRules(const QStringList &rules);
    QStringList firewallRules() const { return m_firewallRules; }
    bool firewallAllowsUrl(const QUrl &url) const { return firewallAllows(url); }
    bool addFirewallRule(const QString &rule);
    bool removeFirewallRule(const QString &rule);
    void setFirewallPrompt(FirewallPrompt prompt);
    QList<EndpointStats> endpointStats() const;
    QList<RequestRecord> endpointRequests(const QString &method,
                                          const QString &endpoint) const;
    // True while this host's /api/* traffic sits inside the 429/5xx cooldown
    // window (createRequest would answer it locally with a synthetic error).
    // Lets periodic work that never passes through this manager — e.g. the
    // git-subprocess mirror fetches — honour the same host-wide backpressure
    // and skip a round instead of hammering an already rate-limited relay.
    bool hostInCooldown(const QString &host) const;

    static QString canonicalFirewallRule(const QString &rule);
    static QString firewallRuleForUrl(const QUrl &url, bool includePort = false);
    static QString firewallRuleForExactUrl(const QUrl &url);
    static QString firewallMethodName(Operation op);
    static QString firewallRuleLabel(const QString &rule);

signals:
    void firewallRequestDecided(const QString &method, const QUrl &url,
                                const QString &rule, bool allowed);
    void endpointStatsChanged();

protected:
    QNetworkReply *createRequest(Operation op, const QNetworkRequest &request,
                                  QIODevice *outgoingData = nullptr) override;
    virtual QNetworkReply *createNetworkRequest(Operation op,
                                                const QNetworkRequest &request,
                                                QIODevice *outgoingData);

private:
    static int normalizedPort(const QUrl &url);
    static QString normalizedHost(const QString &host);
    static QString endpointStatsKey(Operation op, const QUrl &url);
    static QString endpointStatsUrl(const QUrl &url);
    bool firewallAllows(const QUrl &url) const;
    EndpointStats &endpointStatsFor(Operation op, const QUrl &url);
    int noteEndpointAttempt(Operation op, const QUrl &url,
                            const QString &requestData);
    void noteEndpointAllowed(Operation op, const QUrl &url);
    void noteEndpointBlocked(Operation op, const QUrl &url,
                             const QString &reason, int requestIndex);
    void noteEndpointFinished(Operation op, const QUrl &url, qint64 uploaded,
                              qint64 downloaded, int status,
                              const QString &error, int requestIndex,
                              const QString &responseData);

    NetworkBackoff m_backoff;
    bool m_firewallEnabled = false;
    QStringList m_firewallRules;
    FirewallPrompt m_firewallPrompt;
    QHash<QString, EndpointStats> m_endpointStats;
};
