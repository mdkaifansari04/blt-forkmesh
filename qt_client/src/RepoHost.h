#pragma once

#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QUrl>

#include <functional>





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
