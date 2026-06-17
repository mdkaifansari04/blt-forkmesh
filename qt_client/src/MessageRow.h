#pragma once

#include "ChatBackend.h"

#include <QFrame>
#include <QMap>
#include <QString>
#include <QStringList>

class QHBoxLayout;
class QLabel;
class QVBoxLayout;

// One chat message: avatar, sender + time, text and/or attachment (inline
// image, animated GIF, or a downloadable file chip), and a reactions bar.
class MessageRow : public QFrame
{
    Q_OBJECT
public:
    MessageRow(const ChatMessage &message, const QString &nameColor,
               QWidget *parent = nullptr);

    QString messageId() const { return m_message.id; }
    QString senderId() const { return m_message.senderId; }

    void setAvatar(const QPixmap &pixmap);
    // emoji -> reactor display names
    void setReactions(const QMap<QString, QStringList> &reactions);

signals:
    void reactionToggled(const QString &messageId, const QString &emoji);
    void editRequested(const QString &messageId, const QString &currentText);
    void deleteRequested(const QString &messageId);
    void saveFileRequested(const QString &fileName, const QByteArray &data);
    // The avatar or sender name was clicked (to open that node's profile).
    void senderClicked(const QString &id, const QString &name);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void buildAttachment(QWidget *parent, QVBoxLayout *layout);
    void showReactionPicker();

    ChatMessage m_message;
    QString m_nameColor;
    QLabel *m_avatarLabel;
    QLabel *m_senderLabel = nullptr;
    QHBoxLayout *m_reactionsBar;
};
