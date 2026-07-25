#pragma once

#include <QPointer>
#include <QUrl>
#include <QWidget>

class QNetworkAccessManager;
class QNetworkReply;
class QVBoxLayout;

// Read-only projection of remote ActivityPub replies. This widget never
// creates IssueEvent/PullEvent/DiscussionEvent records and never writes to a
// repository's signed native event log.
class FederatedThreadView final : public QWidget
{
public:
    explicit FederatedThreadView(QNetworkAccessManager *network,
                                 QWidget *parent = nullptr);

    void load(const QUrl &server, const QString &owner, const QString &repo,
              const QString &kind, int number);

private:
    void showMessage(const QString &message);
    void render(const QByteArray &payload);
    void clearReplies();

    QPointer<QNetworkAccessManager> m_network;
    QPointer<QNetworkReply> m_reply;
    QVBoxLayout *m_replies = nullptr;
    quint64 m_generation = 0;
};
