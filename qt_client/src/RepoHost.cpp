#include "RepoHost.h"

#include <QJsonObject>

namespace {

QString operationClass(const QString &op)
{
    if (op == QLatin1String("git-upload-pack"))
        return QStringLiteral("clone/fetch");
    if (op == QLatin1String("git-info-refs"))
        return QStringLiteral("clone handshake");
    if (op == QLatin1String("tree"))
        return QStringLiteral("browse tree");
    if (op == QLatin1String("blob") || op == QLatin1String("raw-blob"))
        return QStringLiteral("view file");
    if (op == QLatin1String("commits") || op == QLatin1String("commit"))
        return QStringLiteral("commit history");
    if (op == QLatin1String("branches"))
        return QStringLiteral("list branches");
    if (op == QLatin1String("search"))
        return QStringLiteral("repository search");
    if (op == QLatin1String("sizes"))
        return QStringLiteral("size map");
    if (op == QLatin1String("release-blob"))
        return QStringLiteral("release download");
    return QStringLiteral("repository request");
}

QString userAgentClass(const QString &ua)
{
    const QString value = ua.toLower();
    if (value.contains(QStringLiteral("git/")) ||
        value.contains(QStringLiteral("libgit2")) ||
        value.contains(QStringLiteral("jgit")))
        return QStringLiteral("git client");
    if (value.contains(QStringLiteral("bot")) ||
        value.contains(QStringLiteral("crawl")) ||
        value.contains(QStringLiteral("curl")) ||
        value.contains(QStringLiteral("wget")) ||
        value.contains(QStringLiteral("python-requests")))
        return QStringLiteral("bot/tool");
    if (value.contains(QStringLiteral("mozilla")) ||
        value.contains(QStringLiteral("chrome")) ||
        value.contains(QStringLiteral("safari")) ||
        value.contains(QStringLiteral("firefox")))
        return QStringLiteral("browser");
    return QStringLiteral("client");
}

QString endpointDisplay(const QUrl &url)
{
    return url.toString(
        QUrl::RemoveUserInfo | QUrl::RemovePath | QUrl::RemoveQuery |
        QUrl::RemoveFragment);
}

}

RepoHost::RepoHost(const QString &owner, const QString &name,
                   const QString &mirrorPath, const QUrl &url,
                   QObject *parent)
    : QObject(parent),
      m_owner(owner),
      m_name(name),
      m_mirrorPath(mirrorPath),
      m_url(url)
{
}

void RepoHost::start()
{
    emit log(QStringLiteral(
        "Repository socket transport is retired; using direct HTTPS and "
        "bounded relay sync."));
    emit networkDiagnosticsChanged();
}

void RepoHost::stop()
{
    emit networkDiagnosticsChanged();
}

void RepoHost::setTokenProvider(std::function<QString()> provider)
{
    Q_UNUSED(provider)
}

void RepoHost::setConnectionAuthorizer(
    std::function<bool(const QUrl &)> authorizer)
{
    Q_UNUSED(authorizer)
}

QJsonObject RepoHost::networkDiagnostics() const
{
    return {
        {QStringLiteral("kind"), QStringLiteral("repository-direct-https")},
        {QStringLiteral("owner"), m_owner},
        {QStringLiteral("repository"), m_name},
        {QStringLiteral("endpoint"), endpointDisplay(m_url)},
        {QStringLiteral("transport"), QStringLiteral("direct-https")},
        {QStringLiteral("connected"), false},
        {QStringLiteral("persistentSocket"), false},
        {QStringLiteral("sync"), QStringLiteral("bounded-https-poll")},
        {QStringLiteral("txBytes"), 0},
        {QStringLiteral("rxBytes"), 0},
        {QStringLiteral("txFrames"), 0},
        {QStringLiteral("rxFrames"), 0},
    };
}

QString RepoHost::generalizedRequestLog(const QJsonObject &request)
{
    return QStringLiteral("Repository request [%1] [%2]")
        .arg(operationClass(request.value(QStringLiteral("op")).toString()),
             userAgentClass(request.value(QStringLiteral("ua")).toString()));
}
