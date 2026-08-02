#pragma once

#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QUrl>

#include <functional>

// Compatibility status object for releases upgrading from the retired
// per-repository WebSocket transport. It opens no socket and serves no bytes.
// Repository content uses the signed direct-HTTPS gateway; small collaboration
// changes arrive through the client's bounded HTTPS sync poll.
class RepoHost : public QObject
{
    Q_OBJECT
public:
    RepoHost(const QString &owner, const QString &name,
             const QString &mirrorPath, const QUrl &url,
             QObject *parent = nullptr);

    void start();
    void stop();
    QJsonObject networkDiagnostics() const;
    static QString generalizedRequestLog(const QJsonObject &request);

    // Kept as source-compatible no-ops for one desktop release. Repository
    // socket credentials and connection authorization are no longer consumed.
    void setTokenProvider(std::function<QString()> provider);
    void setConnectionAuthorizer(
        std::function<bool(const QUrl &)> authorizer);

signals:
    void log(const QString &line);
    void requestServed(const QString &owner, const QString &name, bool clone);
    void relayEventReceived(const QString &owner, const QString &name,
                            const QString &topic);
    void networkDiagnosticsChanged();

private:
    QString m_owner;
    QString m_name;
    QString m_mirrorPath;
    QUrl m_url;
};
