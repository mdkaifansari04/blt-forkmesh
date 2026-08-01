#pragma once

#include "ChatBackend.h"

#include <QFrame>
#include <QHash>
#include <QMap>
#include <QString>
#include <QStringList>

class QHBoxLayout;
class QLabel;
class QVBoxLayout;



class MessageRow : public QFrame
{
    Q_OBJECT
public:

    MessageRow(const ChatMessage &message, const QString &nameColor,
               const QHash<QString, MemberInfo> &mentionProfiles,
               bool canModerate = false, QWidget *parent = nullptr,
               int threadReplyCount = 0, bool threadContext = false);

    QString messageId() const { return m_message.id; }
    QString senderId() const { return m_message.senderId; }

    void setAvatar(const QPixmap &pixmap);

    void setReactions(const QMap<QString, QStringList> &reactions);

signals:
    void reactionToggled(const QString &messageId, const QString &emoji);
    void threadRequested(const QString &messageId);
    void editRequested(const QString &messageId, const QString &currentText);


    void createIssueRequested(const QString &text, const QString &senderName,
                              qint64 timestampMs);


    void sendToPromptRequested(const QString &text);
    void deleteRequested(const QString &messageId);

    void moderateDeleteRequested(const QString &messageId);
    void saveFileRequested(const QString &fileName, const QByteArray &data);

    void imageActivated(const QString &fileName, const QByteArray &data);

    void senderClicked(const QString &id, const QString &name);

    void mentionClicked(const QString &id, const QString &name);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void buildAttachment(QWidget *parent, QVBoxLayout *layout);
    void showReactionPicker();

    ChatMessage m_message;
    QString m_nameColor;
    QHash<QString, MemberInfo> m_mentionProfiles;
    QLabel *m_avatarLabel;
    QLabel *m_senderLabel = nullptr;
    QLabel *m_imageLabel = nullptr;
    QHBoxLayout *m_reactionsBar;
};
