#include "FederatedThreadView.h"

#include <QDesktopServices>
#include <QFrame>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrlQuery>
#include <QVBoxLayout>

namespace {

QString safeHttpsUrl(const QJsonObject &item)
{
    QString value = item.value(QStringLiteral("url")).toString();
    if (value.isEmpty())
        value = item.value(QStringLiteral("backlink")).toString();
    if (value.isEmpty())
        value = item.value(QStringLiteral("remoteId")).toString();
    const QUrl url(value);
    return url.scheme() == QLatin1String("https") && !url.host().isEmpty() &&
                   url.userInfo().isEmpty()
               ? url.toString()
               : QString();
}

QString lifecycleFor(const QJsonObject &item)
{
    const QString explicitState =
        item.value(QStringLiteral("lifecycle")).toString();
    if (!explicitState.isEmpty())
        return explicitState;
    if (item.value(QStringLiteral("tombstone")).toBool())
        return QStringLiteral("tombstoned");
    if (item.value(QStringLiteral("moderated")).toBool())
        return QStringLiteral("moderated");
    if (item.value(QStringLiteral("edited")).toBool())
        return QStringLiteral("edited");
    return QStringLiteral("active");
}

}

FederatedThreadView::FederatedThreadView(QNetworkAccessManager *network,
                                         QWidget *parent)
    : QWidget(parent), m_network(network)
{
    setObjectName(QStringLiteral("federatedThreadView"));
    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 4, 0, 4);
    outer->setSpacing(8);

    auto *title = new QLabel(QStringLiteral("Fediverse thread"), this);
    title->setObjectName(QStringLiteral("sectionLabel"));
    outer->addWidget(title);

    auto *notice = new QLabel(
        QStringLiteral("Remote ActivityPub replies retain their instance and "
                       "original backlink. They are not signed ForkMesh native "
                       "events and are never merged into the repository event log."),
        this);
    notice->setObjectName(QStringLiteral("statusLine"));
    notice->setWordWrap(true);
    notice->setTextFormat(Qt::PlainText);
    outer->addWidget(notice);

    m_replies = new QVBoxLayout;
    m_replies->setContentsMargins(0, 0, 0, 0);
    m_replies->setSpacing(8);
    outer->addLayout(m_replies);
    showMessage(QStringLiteral("No remote ActivityPub replies loaded."));
}

void FederatedThreadView::clearReplies()
{
    while (QLayoutItem *item = m_replies->takeAt(0)) {
        if (QWidget *widget = item->widget())
            widget->deleteLater();
        delete item;
    }
}

void FederatedThreadView::showMessage(const QString &message)
{
    clearReplies();
    auto *label = new QLabel(message, this);
    label->setObjectName(QStringLiteral("statusLine"));
    label->setTextFormat(Qt::PlainText);
    label->setWordWrap(true);
    m_replies->addWidget(label);
}

void FederatedThreadView::load(const QUrl &server, const QString &owner,
                               const QString &repo, const QString &kind,
                               int number)
{
    ++m_generation;
    if (m_reply)
        m_reply->abort();
    if (!m_network || !server.isValid() || server.host().isEmpty() ||
        owner.isEmpty() || repo.isEmpty() || number <= 0) {
        showMessage(QStringLiteral("Remote replies are unavailable."));
        return;
    }

    QUrl url(server);
    url.setScheme(url.scheme() == QLatin1String("http")
                      ? QStringLiteral("http")
                      : QStringLiteral("https"));
    url.setPath(QStringLiteral("/api/repo/%1/%2/fedi-comments")
                    .arg(QString::fromUtf8(QUrl::toPercentEncoding(owner)),
                         QString::fromUtf8(QUrl::toPercentEncoding(repo))));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("kind"), kind);
    query.addQueryItem(QStringLiteral("number"), QString::number(number));
    url.setQuery(query);

    QNetworkRequest request(url);
    request.setRawHeader("Accept", "application/json");
    showMessage(QStringLiteral("Loading remote ActivityPub replies…"));
    const quint64 generation = m_generation;
    QNetworkReply *reply = m_network->get(request);
    m_reply = reply;
    connect(reply, &QNetworkReply::finished, this,
            [this, reply, generation] {
                const QByteArray payload = reply->readAll();
                const bool ok = reply->error() == QNetworkReply::NoError;
                reply->deleteLater();
                if (m_reply == reply)
                    m_reply.clear();
                if (generation != m_generation)
                    return;
                if (!ok) {
                    showMessage(
                        QStringLiteral("Remote replies are currently unavailable."));
                    return;
                }
                render(payload);
            });
}

void FederatedThreadView::render(const QByteArray &payload)
{
    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(payload, &error);
    if (error.error != QJsonParseError::NoError || !document.isObject()) {
        showMessage(QStringLiteral("Remote reply data was invalid."));
        return;
    }
    const QJsonObject root = document.object();
    QJsonArray items = root.value(QStringLiteral("comments")).toArray();
    if (items.isEmpty())
        items = root.value(QStringLiteral("items")).toArray();
    if (items.isEmpty()) {
        showMessage(QStringLiteral("No remote ActivityPub replies yet."));
        return;
    }

    clearReplies();
    for (const QJsonValue &value : items) {
        if (!value.isObject())
            continue;
        const QJsonObject item = value.toObject();
        const QJsonObject provenance =
            item.value(QStringLiteral("provenance")).toObject();
        const QString lifecycle = lifecycleFor(item);
        const bool unavailable =
            lifecycle == QLatin1String("tombstoned") ||
            lifecycle == QLatin1String("moderated") ||
            lifecycle == QLatin1String("awaiting-redelivery");
        QString author = item.value(QStringLiteral("authorName")).toString();
        if (author.isEmpty())
            author = item.value(QStringLiteral("author")).toString();
        if (author.isEmpty())
            author = QStringLiteral("Remote participant");
        QString instance =
            item.value(QStringLiteral("sourceInstance")).toString();
        if (instance.isEmpty())
            instance = provenance.value(QStringLiteral("instance")).toString();
        QString software =
            item.value(QStringLiteral("sourceSoftware")).toString();
        if (software.isEmpty())
            software = provenance.value(QStringLiteral("software")).toString(
                QStringLiteral("ActivityPub"));
        QString body = item.value(QStringLiteral("body")).toString();
        if (lifecycle == QLatin1String("tombstoned"))
            body = QStringLiteral("Deleted on the remote instance");
        else if (unavailable)
            body = QStringLiteral("Hidden by remote moderation");

        auto *card = new QFrame(this);
        card->setObjectName(QStringLiteral("issueTimelineCard"));
        const int depth =
            qBound(0, item.value(QStringLiteral("depth")).toInt(), 8);
        card->setContentsMargins(12 + depth * 16, 10, 12, 10);
        auto *layout = new QVBoxLayout(card);
        layout->setContentsMargins(12 + depth * 16, 10, 12, 10);
        layout->setSpacing(6);

        auto *header = new QLabel(card);
        header->setTextFormat(Qt::RichText);
        header->setWordWrap(true);
        header->setText(
            QStringLiteral("<b>%1</b>%2 <span style='color:#8b949e'>%3%4</span>")
                .arg(author.toHtmlEscaped(),
                     lifecycle == QLatin1String("edited")
                         ? QStringLiteral(" <span style='color:#8b949e'>(edited)</span>")
                         : QString(),
                     software.toHtmlEscaped(),
                     instance.isEmpty()
                         ? QString()
                         : QStringLiteral(" · %1").arg(instance.toHtmlEscaped())));
        layout->addWidget(header);

        auto *content = new QLabel(body, card);
        content->setTextFormat(Qt::PlainText);
        content->setWordWrap(true);
        if (unavailable)
            content->setStyleSheet(QStringLiteral("font-style: italic;"));
        layout->addWidget(content);

        auto *source = new QLabel(card);
        source->setTextFormat(Qt::RichText);
        source->setWordWrap(true);
        source->setOpenExternalLinks(false);
        const QString backlink = safeHttpsUrl(item);
        source->setText(
            QStringLiteral("<span style='color:#8b949e'>Remote ActivityPub · "
                           "not a signed native event</span>%1")
                .arg(backlink.isEmpty()
                         ? QString()
                         : QStringLiteral(
                               " · <a href='%1'>Open original</a>")
                               .arg(backlink.toHtmlEscaped())));
        connect(source, &QLabel::linkActivated, this,
                [](const QString &href) {
                    const QUrl url(href);
                    if (url.scheme() == QLatin1String("https") &&
                        !url.host().isEmpty() && url.userInfo().isEmpty())
                        QDesktopServices::openUrl(url);
                });
        layout->addWidget(source);
        m_replies->addWidget(card);
    }
    if (m_replies->count() == 0)
        showMessage(QStringLiteral("No remote ActivityPub replies yet."));
}
