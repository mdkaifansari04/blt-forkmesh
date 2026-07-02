#include "BackoffNetworkAccessManager.h"

#include <QDateTime>
#include <QNetworkReply>
#include <QTimer>

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

} // namespace

BackoffNetworkAccessManager::BackoffNetworkAccessManager(QObject *parent)
    : QNetworkAccessManager(parent)
{
}

QNetworkReply *BackoffNetworkAccessManager::createRequest(
    Operation op, const QNetworkRequest &request, QIODevice *outgoingData)
{
    const QUrl url = request.url();
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
