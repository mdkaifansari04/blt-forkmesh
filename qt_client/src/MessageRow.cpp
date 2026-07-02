#include "MessageRow.h"

#include <QApplication>
#include <QBuffer>
#include <QClipboard>
#include <QDateTime>
#include <QEvent>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QMouseEvent>
#include <QMovie>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QPushButton>
#include <QSettings>
#include <QStyleHints>
#include <QTimer>
#include <QVBoxLayout>

namespace {

constexpr int kAvatarSize = 36;
constexpr int kMaxMediaWidth = 360;

struct ReactionChoice {
    const char *value;
    const char *label;
};

const ReactionChoice kReactionChoices[] = {{"like", "Like"},
                                           {"love", "Love"},
                                           {"laugh", "Laugh"},
                                           {"celebrate", "Celebrate"},
                                           {"surprised", "Surprised"},
                                           {"sad", "Sad"},
                                           {"thanks", "Thanks"},
                                           {"hot", "Hot"}};

QString fromCodepoint(char32_t codepoint)
{
    const char32_t points[] = {codepoint};
    return QString::fromUcs4(points, 1);
}

QString fromCodepoints(char32_t first, char32_t second)
{
    const char32_t points[] = {first, second};
    return QString::fromUcs4(points, 2);
}

QString reactionDisplayName(const QString &value)
{
    for (const ReactionChoice &choice : kReactionChoices) {
        if (value == QString::fromLatin1(choice.value))
            return QString::fromLatin1(choice.label);
    }

    // Legacy reaction payloads used emoji values; display those as text labels.
    if (value == fromCodepoint(0x1F44D))
        return QStringLiteral("Like");
    if (value == fromCodepoints(0x2764, 0xFE0F))
        return QStringLiteral("Love");
    if (value == fromCodepoint(0x1F602))
        return QStringLiteral("Laugh");
    if (value == fromCodepoint(0x1F389))
        return QStringLiteral("Celebrate");
    if (value == fromCodepoint(0x1F62E))
        return QStringLiteral("Surprised");
    if (value == fromCodepoint(0x1F622))
        return QStringLiteral("Sad");
    if (value == fromCodepoint(0x1F64F))
        return QStringLiteral("Thanks");
    if (value == fromCodepoint(0x1F525))
        return QStringLiteral("Hot");
    return value;
}

// A rounded-rectangle fallback avatar: the sender's initial on a colored tile.
QPixmap initialsAvatar(const QString &name, const QString &color)
{
    QPixmap pixmap(kAvatarSize, kAvatarSize);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setBrush(QColor(color));
    painter.setPen(Qt::NoPen);
    painter.drawRoundedRect(0, 0, kAvatarSize, kAvatarSize, 9, 9);
    painter.setPen(Qt::white);
    QFont font = painter.font();
    font.setBold(true);
    font.setPixelSize(16);
    painter.setFont(font);
    const QString initial = name.isEmpty() ? "?" : name.left(1).toUpper();
    painter.drawText(pixmap.rect(), Qt::AlignCenter, initial);
    return pixmap;
}

QString humanSize(qint64 bytes)
{
    if (bytes < 1024)
        return QString::number(bytes) + " B";
    if (bytes < 1024 * 1024)
        return QString::number(bytes / 1024.0, 'f', 1) + " KB";
    return QString::number(bytes / (1024.0 * 1024.0), 'f', 1) + " MB";
}

// Mirrors forkmesh::ui::currentThemeIsDark() (MainWindowInternal.h), kept as a
// standalone copy here so this widget doesn't have to pull in that header.
bool themeIsDark()
{
    const QString pref =
        QSettings().value(QStringLiteral("app/theme"), "system").toString();
    if (pref == "light")
        return false;
    if (pref == "dark")
        return true;
    return QGuiApplication::styleHints()->colorScheme() != Qt::ColorScheme::Light;
}

// Ticks every message row's countdown ring on a single shared timer, rather
// than one QTimer per row (a long-lived conversation can have hundreds).
QTimer *expiryRingTicker()
{
    static QTimer *timer = [] {
        auto *t = new QTimer;
        t->start(60 * 1000);
        return t;
    }();
    return timer;
}

// Small ring next to the timestamp showing how close a message is to its
// 7-day retention cutoff (kChatMessageRetentionMs), after which it's pruned
// from local history and the relay stops retaining it too.
class ExpiryRing : public QWidget
{
public:
    explicit ExpiryRing(qint64 timestampMs, QWidget *parent = nullptr)
        : QWidget(parent), m_timestampMs(timestampMs)
    {
        setFixedSize(kDiameter, kDiameter);
        refreshTooltip();
        connect(expiryRingTicker(), &QTimer::timeout, this, [this] {
            refreshTooltip();
            update();
        });
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        const qint64 elapsed =
            QDateTime::currentMSecsSinceEpoch() - m_timestampMs;
        const double frac = qBound(
            0.0, double(elapsed) / double(kChatMessageRetentionMs), 1.0);

        const bool dark = themeIsDark();
        QRectF box(1, 1, kDiameter - 2, kDiameter - 2);
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.setPen(QPen(QColor(dark ? "#30363d" : "#d0d7de"), 1.2));
        painter.setBrush(Qt::NoBrush);
        painter.drawEllipse(box);
        if (frac > 0.004) {
            painter.setPen(Qt::NoPen);
            painter.setBrush(QColor(dark ? "#8b949e" : "#9a6700"));
            // Sweep clockwise from 12 o'clock; Qt pie angles are 1/16°, CCW+.
            painter.drawPie(box, 90 * 16, -int(frac * 360.0 * 16));
        }
    }

private:
    void refreshTooltip()
    {
        const qint64 remainingMs =
            qMax<qint64>(0, m_timestampMs + kChatMessageRetentionMs -
                                QDateTime::currentMSecsSinceEpoch());
        const qint64 days = remainingMs / (24 * 60 * 60 * 1000);
        const qint64 hours = remainingMs / (60 * 60 * 1000);
        QString text;
        if (days >= 1)
            text = QString("Disappears in %1 day%2")
                       .arg(days)
                       .arg(days == 1 ? "" : "s");
        else if (hours >= 1)
            text = QString("Disappears in %1 hour%2")
                       .arg(hours)
                       .arg(hours == 1 ? "" : "s");
        else
            text = QStringLiteral("Disappears soon");
        setToolTip(text);
    }

    qint64 m_timestampMs;
    static constexpr int kDiameter = 10;
};

} // namespace

MessageRow::MessageRow(const ChatMessage &message, const QString &nameColor,
                       bool canModerate, QWidget *parent)
    : QFrame(parent), m_message(message), m_nameColor(nameColor)
{
    setObjectName("messageRow");

    auto *outer = new QHBoxLayout(this);
    // A little more breathing room so messages don't hug the panel edges; the
    // enclosing chat list has no margins of its own.
    outer->setContentsMargins(18, 7, 18, 7);
    outer->setSpacing(10);

    m_avatarLabel = new QLabel;
    m_avatarLabel->setFixedSize(kAvatarSize, kAvatarSize);
    m_avatarLabel->setPixmap(initialsAvatar(message.senderName, nameColor));
    m_avatarLabel->setCursor(Qt::PointingHandCursor);
    m_avatarLabel->setToolTip("View node profile");
    m_avatarLabel->installEventFilter(this);
    outer->addWidget(m_avatarLabel, 0, Qt::AlignTop);

    auto *column = new QVBoxLayout;
    column->setContentsMargins(0, 0, 0, 0);
    column->setSpacing(3);

    const QDateTime sent = QDateTime::fromMSecsSinceEpoch(message.timestampMs);
    const QString time = sent.toString("hh:mm");
    auto *header = new QLabel(
        "<span style='color:" + nameColor + "; font-weight:700'>" +
        message.senderName.toHtmlEscaped() + "</span>"
        "&nbsp;&nbsp;<span style='color:#6b7280; font-size:11px'>" + time +
        (message.edited ? " (edited)" : "") + "</span>");
    header->setTextFormat(Qt::RichText);
    header->setCursor(Qt::PointingHandCursor);
    // Hovering the header (where the abbreviated time sits next to the name)
    // reveals the full date and time the message was sent.
    header->setToolTip(sent.toString("dddd, MMMM d, yyyy  h:mm:ss AP"));
    header->installEventFilter(this);
    m_senderLabel = header;
    auto *headerRow = new QHBoxLayout;
    headerRow->setContentsMargins(0, 0, 0, 0);
    headerRow->setSpacing(6);
    headerRow->addWidget(header);
    if (!message.deleted)
        headerRow->addWidget(new ExpiryRing(message.timestampMs), 0, Qt::AlignVCenter);
    headerRow->addStretch();
    // A quick Copy action on any message that carries text, regardless of who
    // sent it, so the body can be lifted to the clipboard in one click.
    if (!message.deleted && !message.text.isEmpty()) {
        auto *copy = new QPushButton("Copy");
        copy->setObjectName("messageAction");
        copy->setCursor(Qt::PointingHandCursor);
        copy->setToolTip("Copy message text");
        connect(copy, &QPushButton::clicked, this, [this, copy] {
            QApplication::clipboard()->setText(m_message.text);
            copy->setText("Copied");
        });
        headerRow->addWidget(copy);
    }
    if (!message.deleted) {
        // Edit stays author-only (you can only rewrite your own words).
        if (message.self && !message.text.isEmpty()) {
            auto *edit = new QPushButton("Edit");
            edit->setObjectName("messageAction");
            edit->setCursor(Qt::PointingHandCursor);
            edit->setToolTip("Edit message");
            connect(edit, &QPushButton::clicked, this, [this] {
                emit editRequested(m_message.id, m_message.text);
            });
            headerRow->addWidget(edit);
        }
        // Delete is shown on EVERY message for an admin (a full moderation
        // override that deletes any message, including the admin's own), and on
        // your own messages otherwise. The admin path goes through
        // moderateDeleteRequested, which deletes unconditionally; the self path
        // is the ordinary author delete.
        if (canModerate) {
            auto *del = new QPushButton("Delete");
            del->setObjectName("messageAction");
            del->setCursor(Qt::PointingHandCursor);
            del->setToolTip("Delete this message as an administrator");
            connect(del, &QPushButton::clicked, this, [this] {
                emit moderateDeleteRequested(m_message.id);
            });
            headerRow->addWidget(del);
        } else if (message.self) {
            auto *del = new QPushButton("Delete");
            del->setObjectName("messageAction");
            del->setCursor(Qt::PointingHandCursor);
            del->setToolTip("Delete message");
            connect(del, &QPushButton::clicked, this, [this] {
                emit deleteRequested(m_message.id);
            });
            headerRow->addWidget(del);
        }
    }
    column->addLayout(headerRow);

    if (message.deleted) {
        auto *body = new QLabel("<span style='color:#9ca3af'><i>Message deleted</i></span>");
        body->setObjectName("messageText");
        body->setTextFormat(Qt::RichText);
        column->addWidget(body);
    } else if (!message.text.isEmpty()) {
        auto *body = new QLabel(message.text.toHtmlEscaped());
        body->setObjectName("messageText");
        body->setWordWrap(true);
        body->setTextInteractionFlags(Qt::TextSelectableByMouse |
                                      Qt::LinksAccessibleByMouse);
        body->setOpenExternalLinks(true);
        column->addWidget(body);
    }

    if (!message.deleted && message.hasFile())
        buildAttachment(this, column);

    m_reactionsBar = nullptr;
    if (!message.deleted) {
        auto *reactionsRow = new QWidget;
        m_reactionsBar = new QHBoxLayout(reactionsRow);
        m_reactionsBar->setContentsMargins(0, 2, 0, 0);
        m_reactionsBar->setSpacing(4);
        auto *addReaction = new QPushButton("+");
        addReaction->setObjectName("reactionAdd");
        addReaction->setCursor(Qt::PointingHandCursor);
        addReaction->setToolTip("Add reaction");
        connect(addReaction, &QPushButton::clicked, this,
                &MessageRow::showReactionPicker);
        m_reactionsBar->addWidget(addReaction);
        m_reactionsBar->addStretch();
        column->addWidget(reactionsRow);
    }

    outer->addLayout(column, 1);
}

void MessageRow::buildAttachment(QWidget *, QVBoxLayout *layout)
{
    const QString mime = m_message.fileMime;
    const QByteArray &data = m_message.fileData;

    // Mark an inline image label as clickable so it can be opened full-size in
    // the image detail viewer.
    auto makeClickable = [this](QLabel *label) {
        m_imageLabel = label;
        label->setCursor(Qt::PointingHandCursor);
        label->setToolTip(QStringLiteral("Click to view full size"));
        label->installEventFilter(this);
    };

    if (mime == "image/gif") {
        auto *label = new QLabel;
        auto *buffer = new QBuffer(label);
        buffer->setData(data);
        buffer->open(QIODevice::ReadOnly);
        auto *movie = new QMovie(buffer, QByteArray(), label);
        if (movie->isValid()) {
            movie->jumpToFrame(0);
            QSize size = movie->currentImage().size();
            if (size.width() > kMaxMediaWidth)
                size.scale(kMaxMediaWidth, kMaxMediaWidth * size.height() /
                                               qMax(1, size.width()),
                           Qt::KeepAspectRatio);
            movie->setScaledSize(size);
            label->setMovie(movie);
            movie->start();
            makeClickable(label);
            layout->addWidget(label);
            return;
        }
    } else if (mime.startsWith("image/")) {
        QPixmap pixmap;
        if (pixmap.loadFromData(data)) {
            if (pixmap.width() > kMaxMediaWidth)
                pixmap = pixmap.scaledToWidth(kMaxMediaWidth,
                                              Qt::SmoothTransformation);
            auto *label = new QLabel;
            label->setPixmap(pixmap);
            makeClickable(label);
            layout->addWidget(label);
            return;
        }
    }

    // Non-image (or undecodable image): a downloadable file chip.
    auto *chip = new QWidget;
    chip->setObjectName("fileChip");
    auto *chipLayout = new QHBoxLayout(chip);
    chipLayout->setContentsMargins(10, 8, 10, 8);
    chipLayout->setSpacing(8);
    auto *icon = new QLabel("File");
    auto *name = new QLabel(m_message.fileName.toHtmlEscaped() + "  <span "
                            "style='color:#9ca3af'>(" +
                            humanSize(data.size()) + ")</span>");
    name->setTextFormat(Qt::RichText);
    auto *save = new QPushButton("Save");
    save->setObjectName("ghostButton");
    save->setCursor(Qt::PointingHandCursor);
    connect(save, &QPushButton::clicked, this, [this] {
        emit saveFileRequested(m_message.fileName, m_message.fileData);
    });
    chipLayout->addWidget(icon);
    chipLayout->addWidget(name, 1);
    chipLayout->addWidget(save);
    layout->addWidget(chip);
}

bool MessageRow::eventFilter(QObject *watched, QEvent *event)
{
    if ((watched == m_avatarLabel || watched == m_senderLabel) &&
        event->type() == QEvent::MouseButtonRelease) {
        auto *me = static_cast<QMouseEvent *>(event);
        if (me->button() == Qt::LeftButton) {
            emit senderClicked(m_message.senderId, m_message.senderName);
            return true;
        }
    }
    if (watched == m_imageLabel &&
        event->type() == QEvent::MouseButtonRelease) {
        auto *me = static_cast<QMouseEvent *>(event);
        if (me->button() == Qt::LeftButton) {
            emit imageActivated(m_message.fileName, m_message.fileData);
            return true;
        }
    }
    return QFrame::eventFilter(watched, event);
}

void MessageRow::setAvatar(const QPixmap &pixmap)
{
    if (pixmap.isNull())
        return;
    QPixmap rounded(kAvatarSize, kAvatarSize);
    rounded.fill(Qt::transparent);
    QPainter painter(&rounded);
    painter.setRenderHint(QPainter::Antialiasing);
    QPainterPath clip;
    clip.addRoundedRect(0, 0, kAvatarSize, kAvatarSize, 9, 9);
    painter.setClipPath(clip);
    painter.drawPixmap(0, 0,
                       pixmap.scaled(kAvatarSize, kAvatarSize,
                                     Qt::KeepAspectRatioByExpanding,
                                     Qt::SmoothTransformation));
    m_avatarLabel->setPixmap(rounded);
}

void MessageRow::setReactions(const QMap<QString, QStringList> &reactions)
{
    if (!m_reactionsBar)
        return;
    // Clear existing chips (keep the trailing "+" button and stretch).
    while (m_reactionsBar->count() > 2) {
        QLayoutItem *item = m_reactionsBar->takeAt(0);
        if (item->widget())
            item->widget()->deleteLater();
        delete item;
    }
    int insertAt = 0;
    for (auto it = reactions.constBegin(); it != reactions.constEnd(); ++it) {
        auto *chip = new QPushButton(reactionDisplayName(it.key()) + " " +
                                     QString::number(it.value().size()));
        chip->setObjectName("reactionChip");
        chip->setCursor(Qt::PointingHandCursor);
        chip->setToolTip(it.value().join(", "));
        const QString reaction = it.key();
        connect(chip, &QPushButton::clicked, this,
                [this, reaction] { emit reactionToggled(m_message.id, reaction); });
        m_reactionsBar->insertWidget(insertAt++, chip);
    }
}

void MessageRow::showReactionPicker()
{
    QMenu menu(this);
    for (const ReactionChoice &choice : kReactionChoices) {
        QAction *action = menu.addAction(QString::fromLatin1(choice.label));
        const QString value = QString::fromLatin1(choice.value);
        connect(action, &QAction::triggered, this,
                [this, value] { emit reactionToggled(m_message.id, value); });
    }
    menu.exec(QCursor::pos());
}
