// MainWindowMessages: MainWindow feature methods, split out of MainWindow.cpp.
// Headless/status messages and the files view.
//
// These are MainWindow member functions defined in their own translation unit;
// the class itself is declared in MainWindow.h. Shared helpers live in
// MainWindowInternal.h / MainWindowShared.cpp (namespace forkmesh::ui).

#include "MainWindow.h"
#include "MainWindowInternal.h"
#include "OfficeChannelMirror.h"

#include "ChatVisitorPresence.h"

#include <QDesktopServices>
#include <QUrl>

using namespace forkmesh::ui;

// ------------------------------------------------------------------ messages

static QString chatUserDisplayName(const MemberInfo &member);

static void addMentionProfile(QHash<QString, MemberInfo> &profiles,
                              MemberInfo member)
{
    const QString display = chatUserDisplayName(member).trimmed();
    if (display.isEmpty())
        return;
    member.name = display;
    const QString key = display.toLower();
    if (!profiles.contains(key))
        profiles.insert(key, member);
}

static QUrl chatMentionProfileUrl(const QString &serverInput,
                                  const QString &accountName)
{
    QUrl base(canonicalServerUrl(serverInput));
    QString scheme = base.scheme();
    if (scheme == QLatin1String("wss"))
        scheme = QStringLiteral("https");
    else if (scheme == QLatin1String("ws"))
        scheme = QStringLiteral("http");
    else if (scheme.isEmpty())
        scheme = QStringLiteral("https");

    QUrl url;
    url.setScheme(scheme);
    url.setHost(base.host());
    if (base.port() >= 0)
        url.setPort(base.port());
    url.setPath(QStringLiteral("/@") + accountName.trimmed().toLower());
    return url;
}

QString MainWindow::senderColor(const QString &sender) const
{
    const uint hash = qHash(sender);
    return Theme::kSenderPalette[hash % Theme::kSenderPaletteSize];
}

MessageRow *MainWindow::addMessageRow(const ChatMessage &message)
{
    // A deleted message leaves no trace in the transcript: drop the whole row
    // (avatar, sender header, and body) rather than rendering a tombstone. The
    // message stays in m_history (marked deleted) for dedup/sync; it just isn't
    // shown.
    if (message.deleted)
        return nullptr;

    // Admins get a Delete control on EVERY message — anyone's and their own — as
    // a full moderation override (MessageRow routes it through the unconditional
    // admin-delete path).
    QHash<QString, MemberInfo> mentionProfiles;
    for (const MemberInfo &member : std::as_const(m_chatDirectoryUsers))
        addMentionProfile(mentionProfiles, member);
    for (const MemberInfo &member : std::as_const(m_homeRoster))
        addMentionProfile(mentionProfiles, member);

    const bool canModerate = m_isAdmin;
    auto *row = new MessageRow(message, senderColor(message.senderName),
                               mentionProfiles, canModerate);
    if (m_avatars.contains(message.senderId))
        row->setAvatar(m_avatars.value(message.senderId));
    if (m_reactions.contains(message.id))
        row->setReactions(m_reactions.value(message.id));
    connect(row, &MessageRow::reactionToggled, this,
            [this](const QString &messageId, const QString &emoji) {
                if (m_backend && !isOfficeConversation(m_currentConversation))
                    m_backend->sendReaction(m_currentConversation, messageId, emoji);
            });
    connect(row, &MessageRow::editRequested, this, &MainWindow::promptEditMessage);
    connect(row, &MessageRow::createIssueRequested, this,
            &MainWindow::promptIssueFromChatMessage);
    connect(row, &MessageRow::sendToPromptRequested, this,
            &MainWindow::sendMessageToPrompt);
    connect(row, &MessageRow::deleteRequested, this, &MainWindow::confirmDeleteMessage);
    connect(row, &MessageRow::moderateDeleteRequested, this,
            &MainWindow::confirmAdminDeleteMessage);
    connect(row, &MessageRow::saveFileRequested, this,
            &MainWindow::saveIncomingFile);
    connect(row, &MessageRow::imageActivated, this,
            &MainWindow::showChatImageDetail);
    // Clicking a sender's avatar or name in chat opens their node profile. The
    // profile panel lives in the Home section, so switch there first — otherwise
    // the panel updates behind the Chat section and nothing appears to happen.
    connect(row, &MessageRow::senderClicked, this,
            [this](const QString &id, const QString &name) {
                showSection(0);
                showNodeProfile(id, name);
            });
    connect(row, &MessageRow::mentionClicked, this,
            [this](const QString &id, const QString &name) {
                if (id.startsWith(QStringLiteral("user:"))) {
                    QString account = id.mid(QStringLiteral("user:").size()).trimmed();
                    if (account.isEmpty())
                        account = name.trimmed().toLower();
                    const QUrl url = chatMentionProfileUrl(
                        m_serverUrlEdit ? m_serverUrlEdit->text() : QString(),
                        account);
                    if (url.isValid() && !url.host().isEmpty()) {
                        QDesktopServices::openUrl(url);
                        return;
                    }
                }
                showSection(0);
                showNodeProfile(id, name);
            });
    // Insert before the trailing stretch.
    m_messageLayout->insertWidget(m_messageLayout->count() - 1, row);
    m_visibleRows.insert(message.id, row);
    return row;
}

void MainWindow::renderConversationRows()
{
    // Drop existing rows (keep the trailing stretch at the end).
    m_visibleRows.clear();
    while (m_messageLayout->count() > 1) {
        QLayoutItem *item = m_messageLayout->takeAt(0);
        if (item->widget())
            item->widget()->deleteLater();
        delete item;
    }
    for (const ChatMessage &message : m_history.value(m_currentConversation))
        addMessageRow(message);
}

void MainWindow::rebuildConversationView()
{
    renderConversationRows();
    // Switching into a conversation always lands at the newest message.
    scrollToBottom();
}

void MainWindow::scrollToBottom()
{
    m_stickToBottom = true;
    // The rangeChanged handler scrolls once the new rows expand the range, but
    // when the range is unchanged (e.g. it already fit) no signal fires, so
    // pin to the current maximum after layout settles too.
    QTimer::singleShot(0, this, [this] {
        QScrollBar *bar = m_messageScroll->verticalScrollBar();
        bar->setValue(bar->maximum());
    });
}

void MainWindow::onMessage(const ChatMessage &message)
{
    const QString conversation = message.conversation;
    if (conversation.isEmpty())
        return;

    // Drop messages already past the 7-day retention window. Peers replay their
    // whole in-session history on every reconnect; the id-dedupe below only
    // covers messages still in the bounded history file, so an expired message
    // that fell out of it (or a lost file) would otherwise resurrect as "new",
    // re-lighting the unread badge on every restart for something the user read
    // days ago. The hourly sweep would evict it again anyway.
    if (message.timestampMs <=
        QDateTime::currentMSecsSinceEpoch() - kChatMessageRetentionMs)
        return;

    // Skip messages we already have (e.g. loaded from disk then replayed by a
    // peer on reconnect) so history isn't duplicated.
    if (!message.id.isEmpty()) {
        if (m_historyIds.contains(message.id))
            return;
        m_historyIds.insert(message.id);
    }

    // Open a DM tab on first contact. For an incoming DM the author *is* the
    // other party (senderId == peerId), so that names the conversation; our
    // own echoed messages (senderId != peerId) must not rename it.
    if (isDirectConversation(conversation)) {
        const QString peerId = dmPeerId(conversation);
        if (message.senderId == peerId && !message.senderName.isEmpty())
            m_dmNames.insert(peerId, message.senderName);
        if (!m_openDms.contains(peerId)) {
            m_openDms.append(peerId);
            refreshDmList();
        }
    }

    // Keep history sorted oldest-to-newest by send time, so the transcript
    // always reads with the most recent message at the bottom even when a
    // message arrives out of order (e.g. a peer replaying missed history after
    // a reconnect delivers something older than what's already shown).
    QList<ChatMessage> &conversationHistory = m_history[conversation];
    int insertAt = conversationHistory.size();
    while (insertAt > 0 && conversationHistory.at(insertAt - 1).timestampMs > message.timestampMs)
        --insertAt;
    conversationHistory.insert(insertAt, message);
    const bool appendedAtEnd = insertAt == conversationHistory.size() - 1;

    // Keep the live transcript current when this conversation is loaded, even if
    // the chat view isn't on screen right now (so returning to it shows the new
    // rows without a rebuild).
    if (conversation == m_currentConversation) {
        const bool wasAtBottom = m_stickToBottom;
        if (appendedAtEnd)
            addMessageRow(message);
        else
            renderConversationRows(); // landed earlier in the transcript
        // Follow new arrivals only when already reading the latest; the
        // rangeChanged handler does the actual scrolling once the row lays out.
        if (wasAtBottom)
            scrollToBottom();
    }
    // `self` only recognizes this node's own id, so a message this user typed
    // on the website, in the World, or on a second device came back looking
    // like someone else's and lit the unread badge for something they had just
    // written. chatDisplayName() is the very name this account stamps on its
    // own outgoing frames, so match incoming senders against it too. Unread and
    // notification bookkeeping only — nothing that grants an edit or a delete
    // leans on this weaker, unsigned check.
    const QString ownChatName = chatDisplayName().trimmed();
    const bool ownMessage =
        message.self ||
        (!ownChatName.isEmpty() &&
         message.senderName.trimmed().compare(ownChatName, Qt::CaseInsensitive)
             == 0);

    // Mark unread (and light the chat button) for any incoming message the user
    // isn't actively reading — either a different conversation, or the chat view
    // isn't the focused, on-screen tab. Own messages never mark unread.
    if (!ownMessage &&
        !(conversation == m_currentConversation && isChatViewVisible())) {
        m_unread.insert(conversation);
        ++m_unreadCounts[conversation];
        refreshChannelList();
        refreshDmList();
    }
    updateChatButton();

    // #welcome traffic is the new-user roll-call (maybeAnnounceWelcome), so a
    // fresh line there raises a "New user joined" ping whose link brings the
    // reader straight to the channel (adhoc #88).
    if (!ownMessage && conversation == kWelcomeChannel &&
        message.timestampMs >
            QDateTime::currentMSecsSinceEpoch() - kWelcomePingFreshMs) {
        NotificationLink link;
        link.kind = QStringLiteral("chat");
        link.ref = kWelcomeChannel;
        const QString who = message.senderName.trimmed();
        addNotification(QStringLiteral("New user joined"),
                        who.isEmpty()
                            ? QStringLiteral("Someone new said hello in #welcome")
                            : who + QStringLiteral(" said hello in #welcome"),
                        false, link);
    }

    if (!ownMessage) {
        const QString where = isDirectConversation(conversation)
                                  ? "sent you a message"
                                  : "in " + conversation;
        const QString preview =
            message.hasFile() ? "File: " + message.fileName : message.text;
        // Chat is an event like any other: file it on the Pings page and raise
        // it in the area above the log, with a row that opens the conversation
        // it came from (adhoc #77). The desktop toast below stays gated by its
        // own setting; this in-app record is not. Only messages that just
        // arrived qualify — a peer replaying days of history this node has
        // never seen must not land as hundreds of "new events" — and #welcome
        // already raised its own ping above.
        constexpr qint64 kChatPingFreshMs = 10 * 60 * 1000;
        if (conversation != kWelcomeChannel &&
            message.timestampMs >
                QDateTime::currentMSecsSinceEpoch() - kChatPingFreshMs) {
            NotificationLink link;
            link.kind = QStringLiteral("chat");
            link.ref = conversation;
            addNotification(message.senderName + QLatin1Char(' ') + where,
                            preview.simplified(), false, link,
                            QStringLiteral("chat"), message.senderName);
        }
        if (textMentionsNodeName(message.text, m_userName)) {
            if (notifyEnabled(kMentionAlertSetting)) {
                QApplication::alert(this, 0);
                QString cleanPreview = preview.simplified();
                if (cleanPreview.size() > 180)
                    cleanPreview = cleanPreview.left(177) + "...";
                postNotification(message.senderName + " mentioned you",
                                 cleanPreview);
            }
        } else if (notifyEnabled(kChatMessageAlertSetting)) {
            notifyIfInactive(message.senderName + " " + where, preview);
        }
    }
    scheduleChatSave();
}

void MainWindow::onReaction(const QString &conversation, const QString &messageId,
                            const QString &emoji, const QString &reactorName,
                            bool added)
{
    Q_UNUSED(conversation);
    QStringList &reactors = m_reactions[messageId][emoji];
    if (added) {
        if (!reactors.contains(reactorName))
            reactors.append(reactorName);
    } else {
        reactors.removeAll(reactorName);
        if (reactors.isEmpty())
            m_reactions[messageId].remove(emoji);
    }
    if (auto *row = m_visibleRows.value(messageId))
        row->setReactions(m_reactions.value(messageId));
}

void MainWindow::onMessageEdited(const QString &conversation, const QString &messageId,
                                 const QString &newText)
{
    QList<ChatMessage> &messages = m_history[conversation];
    for (ChatMessage &message : messages) {
        if (message.id == messageId) {
            message.text = newText.left(16000);
            message.edited = true;
            break;
        }
    }
    if (conversation == m_currentConversation)
        rebuildConversationView();
    scheduleChatSave();
}

void MainWindow::onMessageDeleted(const QString &conversation, const QString &messageId)
{
    QList<ChatMessage> &messages = m_history[conversation];
    for (ChatMessage &message : messages) {
        if (message.id == messageId) {
            message.text.clear();
            message.fileName.clear();
            message.fileMime.clear();
            message.fileData.clear();
            message.deleted = true;
            break;
        }
    }
    m_reactions.remove(messageId);
    if (conversation == m_currentConversation)
        rebuildConversationView();
    scheduleChatSave();
}

void MainWindow::promptEditMessage(const QString &messageId, const QString &currentText)
{
    if (!m_backend || messageId.isEmpty() ||
        isOfficeConversation(m_currentConversation))
        return;
    bool ok = false;
    const QString text = QInputDialog::getMultiLineText(
        this, "Edit message", "Message:", currentText, &ok);
    const QString trimmed = text.trimmed();
    if (!ok || trimmed.isEmpty() || trimmed == currentText)
        return;
    m_backend->editMessage(m_currentConversation, messageId, trimmed);
}

void MainWindow::confirmDeleteMessage(const QString &messageId)
{
    if (!m_backend || messageId.isEmpty() ||
        isOfficeConversation(m_currentConversation))
        return;
    const int result = QMessageBox::question(
        this, "Delete message", "Delete this message for everyone?");
    if (result == QMessageBox::Yes)
        m_backend->deleteMessage(m_currentConversation, messageId);
}

QString MainWindow::adminDeleteCanonical(const QString &conversation,
                                         const QString &messageId,
                                         const QString &adminPubkey,
                                         qint64 ts) const
{
    // Must match both sides byte-for-byte. Binds the delete to this exact
    // message, conversation, admin key and timestamp.
    return QStringLiteral("forkmesh-admin-delete-v1\n") + conversation + "\n" +
           messageId + "\n" + adminPubkey + "\n" + QString::number(ts);
}

void MainWindow::confirmAdminDeleteMessage(const QString &messageId)
{
    if (!m_backend || messageId.isEmpty() || !m_isAdmin ||
        !hasOwnerSigningCapability(accountOwner()) ||
        !m_profileIdentity.isValid())
        return;
    const int result = QMessageBox::question(
        this, "Delete message",
        "Delete this message for everyone as an administrator?");
    if (result != QMessageBox::Yes)
        return;
    const QString conversation = m_currentConversation;
    const QString pubkey = m_profileIdentity.publicKey();
    const qint64 ts = QDateTime::currentMSecsSinceEpoch();
    const QString sig = m_profileIdentity.signData(
        adminDeleteCanonical(conversation, messageId, pubkey, ts).toUtf8());
    if (sig.isEmpty())
        return;
    // Trust ourselves: apply locally now, and broadcast the signed request so
    // every peer can verify and apply it too.
    m_knownAdminPubkeys.insert(pubkey);
    m_backend->applyAdminDelete(conversation, messageId);
    m_backend->sendAdminDelete(conversation, messageId, ts, sig);
}

void MainWindow::onAdminDeleteRequested(const QString &conversation,
                                        const QString &messageId,
                                        const QString &adminId,
                                        const QString &adminName, qint64 ts,
                                        const QString &sig)
{
    if (messageId.isEmpty() || adminId.isEmpty() || sig.isEmpty())
        return;
    // 1) The signature must be valid for the claimed admin key. This stops any
    //    room member from forging an admin delete (chat frames are otherwise
    //    unsigned), since they can't produce a signature for the admin's key.
    const QByteArray canonical =
        adminDeleteCanonical(conversation, messageId, adminId, ts).toUtf8();
    if (!ForkMeshIdentity::verifySignature(adminId, sig, canonical)) {
        logSystem(QStringLiteral("Ignored an admin delete with a bad signature."));
        return;
    }
    // 2) The signer must actually be an admin. Trust a cached confirmation, else
    //    ask the relay (the source of truth for admin status) and confirm the
    //    account's published key matches the signer before applying.
    if (m_knownAdminPubkeys.contains(adminId)) {
        m_backend->applyAdminDelete(conversation, messageId);
        return;
    }
    if (adminName.trimmed().isEmpty() || !m_networkAccess)
        return;
    QNetworkRequest request(accountsApiUrl(adminName.trimmed()));
    request.setHeader(QNetworkRequest::ContentTypeHeader,
                      QStringLiteral("application/json"));
    QNetworkReply *reply = m_networkAccess->get(request);
    const QString conv = conversation;
    const QString mid = messageId;
    connect(reply, &QNetworkReply::finished, this,
            [this, reply, adminId, conv, mid]() {
                const QByteArray body = reply->readAll();
                const bool ok = reply->error() == QNetworkReply::NoError;
                reply->deleteLater();
                if (!ok || !m_backend)
                    return;
                const QJsonObject rec = QJsonDocument::fromJson(body).object();
                const bool isAdmin = rec.value("isAdmin").toBool();
                const QString pubkey = rec.value("pubkey").toString();
                if (!isAdmin || pubkey != adminId)
                    return; // not an admin, or the key doesn't match: reject
                m_knownAdminPubkeys.insert(adminId);
                m_backend->applyAdminDelete(conv, mid);
            });
}

void MainWindow::onAvatar(const QString &peerId, const QByteArray &pngData)
{
    QPixmap pixmap;
    if (!pixmap.loadFromData(pngData))
        return;
    m_avatars.insert(peerId, pixmap);
    // Cache to disk so this avatar survives a restart and stays visible after
    // the peer goes offline (when they stop re-broadcasting it).
    const QString path = avatarCachePath(peerId);
    if (!path.isEmpty()) {
        QDir().mkpath(QFileInfo(path).absolutePath());
        QFile f(path);
        if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            f.write(peerId.toUtf8());
            f.write("\n");
            f.write(pngData);
        }
    }
    // Update any visible rows authored by this peer.
    for (auto it = m_visibleRows.constBegin(); it != m_visibleRows.constEnd(); ++it) {
        if (it.value()->senderId() == peerId)
            it.value()->setAvatar(pixmap);
    }
    // Repaint the users column so its avatar tile picks up the image.
    refreshChatMembers();
}

void MainWindow::onTypingChanged(const QString &conversation, const QString &peerId,
                                 const QString &peerName, bool active)
{
    if (conversation.isEmpty() || peerId.isEmpty())
        return;
    if (active)
        m_typing[conversation].insert(peerId, peerName);
    else if (m_typing.contains(conversation))
        m_typing[conversation].remove(peerId);
    refreshTypingLabel();
}

void MainWindow::setChannels(const QStringList &channels)
{
    m_channels.clear();
    for (const QString &channel : channels)
        if (!forkmesh::office::isOfficeConversation(channel))
            m_channels.append(channel);
    // The World office's channel rooms sit in the same sidebar as the mesh
    // rooms. OfficeChannelMirror carries them independently of the primary
    // chat backend, so they are merged in here instead —
    // including when this is re-entered with m_channels itself (adhoc #412).
    for (const QString &conversation : std::as_const(m_officeConversations))
        if (!m_channels.contains(conversation))
            m_channels.append(conversation);
    if (m_currentConversation.startsWith('#') &&
        !m_channels.contains(m_currentConversation))
        m_currentConversation.clear();
    refreshChannelList();
    updateHomeStats();
    if (m_currentConversation.isEmpty() && !m_channels.isEmpty())
        m_channelList->setCurrentRow(0); // triggers switchConversation
}

void MainWindow::setRoster(const QList<MemberInfo> &members)
{
    // Build an index of freshly-received (online) members so we can detect which
    // previously-known nodes have gone offline. Also un-remove any node that is
    // back online (it was explicitly removed but has reconnected).
    QSet<QString> freshIds, freshNames;
    for (const MemberInfo &m : members) {
        if (!m.id.isEmpty()) {
            freshIds.insert(m.id);
            m_removedPeerIds.remove(m.id);
        }
        if (!m.name.isEmpty())
            freshNames.insert(m.name);
    }

    // Remember when each peer was last seen live, so a transient visitor can be
    // aged out below. Keyed by id, falling back to the name for a peer that
    // somehow has none.
    const auto peerKey = [](const MemberInfo &m) {
        return m.id.isEmpty() ? m.name.trimmed().toLower() : m.id;
    };
    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    for (const MemberInfo &m : members) {
        const QString key = peerKey(m);
        if (!key.isEmpty() && m.online)
            m_peerLastSeenMs.insert(key, nowMs);
    }

    // Merge fresh (online) members with previously-known nodes that dropped out
    // of the roster, so the Node dropdown keeps them selectable when offline.
    // Skip nodes that were explicitly removed via removeChatMember.
    QList<MemberInfo> newRoster = members;
    for (const MemberInfo &prev : std::as_const(m_homeRoster)) {
        if (prev.self)
            continue;
        if (!prev.id.isEmpty() && m_removedPeerIds.contains(prev.id))
            continue;
        const bool stillPresent =
            (!prev.id.isEmpty() && freshIds.contains(prev.id)) ||
            (!prev.name.isEmpty() && freshNames.contains(prev.name));
        if (!stillPresent) {
            // Guests and World visitors are browser tabs, not nodes: retaining
            // them offline forever filled the users column with dozens of dead
            // "Guest ####" rows, so forget them once they have gone quiet for
            // the idle window (adhoc #404). A visitor we have no sighting for
            // yet starts its clock now.
            if (ChatVisitorPresence::isTransientVisitor(prev.accountKind,
                                                        prev.name)) {
                const QString key = peerKey(prev);
                const qint64 lastSeen = m_peerLastSeenMs.value(key, 0);
                if (lastSeen <= 0)
                    m_peerLastSeenMs.insert(key, nowMs);
                else if (ChatVisitorPresence::visitorIsIdle(lastSeen, nowMs))
                    continue;
            }
            MemberInfo offline = prev;
            offline.online = false;
            newRoster.append(offline);
        }
    }

    // Keep the sighting map bounded to peers the roster still carries: every
    // visitor id we ever saw would otherwise stay in it for the whole run.
    QHash<QString, qint64> keptSightings;
    keptSightings.reserve(newRoster.size());
    for (const MemberInfo &m : std::as_const(newRoster)) {
        const QString key = peerKey(m);
        const auto it = m_peerLastSeenMs.constFind(key);
        if (it != m_peerLastSeenMs.constEnd())
            keptSightings.insert(key, *it);
    }
    m_peerLastSeenMs = keptSightings;

    // #33: optionally pop a desktop notification when another node comes online.
    // Capture who was online before this update (m_homeRoster still holds the
    // previous roster), and skip the very first fill so we don't alert for every
    // node that was already online when we connected.
    const bool firstRoster = m_homeRoster.isEmpty();
    const QString ownId = m_profileIdentity.publicKey();
    const QString ownName = accountOwner();
    QSet<QString> previouslyOnline;
    for (const MemberInfo &m : std::as_const(m_homeRoster)) {
        if (m.self)
            continue;
        if (!ownId.isEmpty() && m.id == ownId)
            continue;
        if (!ownName.isEmpty() && m.name.compare(ownName, Qt::CaseInsensitive) == 0)
            continue;
        if (m.online && !m.id.isEmpty())
            previouslyOnline.insert(m.id);
    }
    const bool firstPeerRoster = previouslyOnline.isEmpty();
    if (!firstPeerRoster &&
        QDateTime::currentMSecsSinceEpoch() >= m_nodeAlertGraceUntilMs) {
        // Never notify about our own node coming online. The roster's "self"
        // flag isn't always set (e.g. on reconnect), so also match our own node
        // id (public key) and account name defensively.
        const bool showNodeConnectAlert =
            QSettings().value(kNodeConnectAlertSetting, false).toBool();
        for (const MemberInfo &m : members) {
            if (m.self || m.id.isEmpty() || !m.online)
                continue;
            if (!ownId.isEmpty() && m.id == ownId)
                continue;
            if (!ownName.isEmpty() && m.name.compare(ownName, Qt::CaseInsensitive) == 0)
                continue;
            // A plain user account (accountKind "user") is a person, not a
            // serving node — e.g. a desktop signed in as a user rather than a
            // linked node, or ForkBot's relayed chat identity. Announcing it as
            // "Node connected" is misleading (adhoc #37 hit this same mix-up in
            // the node switcher); missing accountKind (older peers) still
            // counts as a node for backward compatibility.
            if (m.accountKind == QLatin1String("user"))
                continue;
            // Anonymous chat guests are people passing through, not nodes
            // (adhoc #308). A first-run desktop guest still advertises its
            // machine's nodeName (adhoc #113), so that machine still counts.
            if (isTemporaryChatGuest(m))
                continue;
            if (!previouslyOnline.contains(m.id)) {
                // This is a node alert: name the machine (nodeName first).
                // A first-run desktop's chat alias is "Guest ####" while its
                // machine keeps the generated node name (adhoc #113).
                QString displayName = nodeListIdentityKey(m).trimmed();
                if (displayName.isEmpty())
                    displayName = m.id;
                logSystem(QStringLiteral("Node connected: %1 is online").arg(displayName));
                if (!showNodeConnectAlert)
                    continue;
                postNotification(QStringLiteral("Node connected"),
                                 displayName + QStringLiteral(" is online"));
            }
        }
    }

    m_homeRoster = newRoster;
    // Now that the room link is live (a roster only arrives once connected), a
    // brand-new node sends its one-time greeting to the identity-specific
    // welcome room — once, ever (issue #192).
    maybeAnnounceWelcome();
    refreshChatMembers();
    // Keep DM tab titles in sync with renamed/rediscovered live members.
    for (const MemberInfo &member : members) {
        if (m_dmNames.contains(member.id) && m_dmNames.value(member.id) != member.name) {
            m_dmNames.insert(member.id, member.name);
            refreshDmList();
            if (m_currentConversation == dmKey(member.id))
                m_channelTitle->setText(kDmPrefix + member.name);
        }
    }

    // Nodes live in the top-bar Node dropdown; refresh it (and the repo list)
    // to reflect live connection status.
    refreshRepositoryList();
    updateHomeStats();
    updateConnectionStatus();
    // Keep the open repo's Mirror nodes view (and its tab count) live as peers
    // come and go or re-advertise fresher mirrors. Coalesced: the rebuild runs
    // ~10 synchronous git reads plus per-row lookups, and roster updates arrive
    // in bursts while presence flickers, so rebuild at most twice a second
    // instead of once per event (stall log: loadMirrorNodesPanel <- setRoster).
    if (m_repoDetailIndex >= 0) {
        if (!m_mirrorPanelRosterTimer) {
            m_mirrorPanelRosterTimer = new QTimer(this);
            m_mirrorPanelRosterTimer->setSingleShot(true);
            m_mirrorPanelRosterTimer->setInterval(500);
            connect(m_mirrorPanelRosterTimer, &QTimer::timeout, this, [this] {
                if (m_repoDetailIndex >= 0)
                    loadMirrorNodesPanel();
            });
        }
        if (!m_mirrorPanelRosterTimer->isActive())
            m_mirrorPanelRosterTimer->start();
    }
    // A re-advertised roster can mean the source of truth just moved: pull any
    // repo whose served state is now behind a peer's, immediately, instead of
    // waiting for the next heartbeat/auto-sync.
    if (!firstRoster)
        syncMirrorsBehindRoster();
}

QString MainWindow::welcomeChannelForIdentity() const
{
    // One shared welcome room for everyone now (see maybeAnnounceWelcome for who
    // actually posts a greeting).
    return kWelcomeChannel;
}

void MainWindow::maybeAnnounceWelcome()
{
    // Self-announce, not peer-detect: only the joining node posts, so the room
    // gets exactly one "X joined" line instead of one per node that saw them.
    // The greeting is broadcast (and stored in room history for later joiners),
    // so even an empty network keeps a record of who arrived.
    if (m_welcomeAnnounced || !m_backend)
        return;
    const QString id = m_profileIdentity.publicKey();
    if (id.isEmpty())
        return;
    // Only new *users* with a verified email greet the network. Plain nodes and
    // accounts whose email isn't confirmed never post — this is what stops the
    // stream of node/unverified "just joined" lines the old #welcome-nodes /
    // #welcome-users rooms collected. Checked before the one-time flag is
    // touched so an account that verifies its email later this session still
    // gets to greet on a subsequent roster tick.
    if (!m_profileIsUserAccount ||
        !accountEmailVerified(settingsAccountName()))
        return;
    // The flag lives next to the identity key itself (not QSettings, which can
    // live in a separate, less-persistent config location on some deployments —
    // e.g. a headless node whose identity dir is on a persistent volume but
    // whose settings dir isn't, which was re-triggering this greeting on every
    // restart). Colocating the two means the greeting can only fire again if the
    // identity itself was also lost, which is exactly when it should.
    if (m_profileIdentity.hasAnnouncedWelcome()) {
        m_welcomeAnnounced = true; // greeted in an earlier run; don't repeat
        return;
    }
    // Migrate the legacy QSettings flag: a node that greeted before the flag
    // moved must not greet again on its first run after upgrading.
    if (QSettings().value(kLegacyWelcomeAnnouncedSettingPrefix + id, false)
            .toBool()) {
        m_profileIdentity.markWelcomeAnnounced();
        m_welcomeAnnounced = true;
        return;
    }
    m_profileIdentity.markWelcomeAnnounced();
    m_welcomeAnnounced = true;
    m_backend->sendChat(
        kWelcomeChannel,
        QString::fromUtf8("\xF0\x9F\x91\x8B Just joined ForkMesh \xE2\x80\x94 hello!"));
}

static QString chatUserDisplayName(const MemberInfo &member)
{
    const QString owner = member.ownerUser.trimmed();
    if (!owner.isEmpty())
        return owner;
    const QString name = member.name.trimmed();
    if (!name.isEmpty())
        return name;
    return member.nodeName.trimmed();
}

static QString chatUserKey(const MemberInfo &member)
{
    if (member.self)
        return QStringLiteral("\x01self");
    return chatUserDisplayName(member).toLower();
}

namespace {
// One node belonging to a user, rendered as a small OS badge under the name.
struct ChatNodeBadge {
    QString label;    // node name, shown in the tooltip
    QString platform; // linux/macos/windows/… -> osBadgeIcon glyph
    bool online = false;
};

// A chat participant collapsed across every node they run, so one person shows
// up once instead of once per node (issue #411).
struct ChatUserGroup {
    MemberInfo primary;         // canonical row (prefers the directory user)
    QString key;                // grouping key (owning account, when known)
    QString anchorId;           // a real node id, for disambiguating look-alikes
    QList<ChatNodeBadge> nodes; // this user's nodes, live and directory-listed
    QSet<QString> nodeKeys;     // lower(nodeName|id) already added, to dedupe
    bool online = false;
};

// The account a directory entry represents: its owner, else its own name.
QString directoryUserKey(const MemberInfo &u)
{
    if (u.self)
        return QStringLiteral("\x01self");
    const QString owner = u.ownerUser.trimmed();
    return (owner.isEmpty() ? u.name.trimmed() : owner).toLower();
}

// Same account key, ignoring the "self" shortcut: the users column groups every
// row by account name, so the directory has to be indexed by that name too.
QString directoryAccountKey(const MemberInfo &u)
{
    const QString owner = u.ownerUser.trimmed();
    return (owner.isEmpty() ? u.name.trimmed() : owner).toLower();
}
} // namespace

void MainWindow::refreshChatUserDirectory()
{
    if (!m_networkAccess || m_chatDirectoryFetchInFlight)
        return;
    // Keep polling so a brand-new signup appears in the users column within a
    // minute, like the website's chat does (adhoc #208/#209). Cheap: the worker
    // edge-caches /api/accounts/users and invalidates that cache on signup.
    if (!m_chatDirectoryTimer) {
        m_chatDirectoryTimer = new QTimer(this);
        m_chatDirectoryTimer->setInterval(60 * 1000);
        connect(m_chatDirectoryTimer, &QTimer::timeout, this,
                &MainWindow::refreshChatUserDirectory);
        m_chatDirectoryTimer->start();
    }
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (m_chatDirectoryFetchedMs > 0 &&
        now - m_chatDirectoryFetchedMs < 45 * 1000)
        return;

    m_chatDirectoryFetchInFlight = true;
    QNetworkRequest request(accountsApiUrl(QStringLiteral("users")));
    request.setRawHeader("Accept", "application/json");
    QNetworkReply *reply = m_networkAccess->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        m_chatDirectoryFetchInFlight = false;
        m_chatDirectoryFetchedMs = QDateTime::currentMSecsSinceEpoch();
        const QByteArray body = reply->readAll();
        const bool ok = reply->error() == QNetworkReply::NoError;
        reply->deleteLater();
        if (!ok)
            return;
        const QJsonObject obj = QJsonDocument::fromJson(body).object();
        if (!obj.value(QStringLiteral("ok")).toBool())
            return;
        mergeChatUserDirectory(obj.value(QStringLiteral("users")).toArray());
    });
}

void MainWindow::mergeChatUserDirectory(const QJsonArray &users)
{
    QHash<QString, MemberInfo> next;
    const QString ownName = accountOwner().trimmed();
    for (const QJsonValue &value : users) {
        const QJsonObject obj = value.toObject();
        if (obj.value(QStringLiteral("kind")).toString(QStringLiteral("user")) !=
            QStringLiteral("user"))
            continue;
        const QString name =
            obj.value(QStringLiteral("name"))
                .toString(obj.value(QStringLiteral("nodeName")).toString())
                .trimmed();
        if (name.isEmpty())
            continue;
        const QString key = name.toLower();
        MemberInfo member;
        member.id = QStringLiteral("user:") + key;
        member.name = name;
        member.ownerUser = name;
        member.self = !ownName.isEmpty() &&
                      ownName.compare(name, Qt::CaseInsensitive) == 0;
        member.online = false;
        member.createdAtMs =
            qint64(obj.value(QStringLiteral("createdAt")).toDouble());
        const QJsonArray nodes = obj.value(QStringLiteral("nodes")).toArray();
        QStringList nodeNames;
        for (const QJsonValue &nodeValue : nodes) {
            const QString node = nodeValue.toString().trimmed();
            if (!node.isEmpty())
                nodeNames.append(node);
        }
        member.nodeName = nodeNames.join(QStringLiteral(", "));
        if (!member.nodeName.isEmpty())
            member.note = QStringLiteral("Nodes: ") + member.nodeName;

        const QString avatarPng = obj.value(QStringLiteral("avatarPng")).toString();
        if (!avatarPng.isEmpty()) {
            const QByteArray png = QByteArray::fromBase64(avatarPng.toLatin1());
            QPixmap avatar;
            if (avatar.loadFromData(png))
                m_avatars.insert(member.id, avatar);
            // Our own row is the account's picture as the website shows it —
            // adopt it so the rail avatar matches the web (adhoc #19).
            if (member.self)
                adoptWebAccountAvatar(png);
        }
        next.insert(key, member);
    }
    // Surface signups that happened while we were running (skip the first fill,
    // which would "announce" every existing account).
    if (m_chatDirectoryLoaded) {
        for (auto it = next.constBegin(); it != next.constEnd(); ++it) {
            if (!m_chatDirectoryUsers.contains(it.key()) && !it.value().self)
                logSystem(QStringLiteral("New user joined ForkMesh: %1")
                              .arg(it.value().name));
        }
    }
    m_chatDirectoryLoaded = true;
    m_chatDirectoryUsers = next;
    // The public user directory is also the database-backed source of each
    // account's linked node fleet. Keep the Nodes page in step so offline nodes
    // do not disappear merely because they are absent from this chat roster.
    if (m_nodesTable)
        refreshNodesTable();
    if (m_networkReposTable && !m_networkReposLastPayload.isEmpty())
        renderNetworkRepos(m_networkReposLastPayload);
    refreshMentionCandidates();
    refreshChatMembers();
    if (m_messageLayout && !m_currentConversation.isEmpty()) {
        const bool wasAtBottom = m_stickToBottom;
        renderConversationRows();
        if (wasAtBottom)
            scrollToBottom();
    }
}

void MainWindow::refreshChatMembers()
{
    // Keep the @-mention candidates in step with the roster (this runs on every
    // roster update), even before the room-members popup itself exists.
    refreshMentionCandidates();
    if (!m_chatMembersLayout)
        return;

    // Clear every card but keep the trailing stretch (the last layout item).
    while (m_chatMembersLayout->count() > 1) {
        QLayoutItem *item = m_chatMembersLayout->takeAt(0);
        if (QWidget *w = item->widget())
            w->deleteLater();
        delete item;
    }

    // The backend directory knows which nodes each user owns. Map every owned
    // node name back to its user so an unlinked node (no ownerUser on the wire)
    // still collapses under its owner instead of showing as its own "user".
    QHash<QString, QString> nodeOwner;      // lower(nodeName) -> user key
    QHash<QString, QStringList> ownedNodes; // user key -> owned node names
    for (const MemberInfo &u : std::as_const(m_chatDirectoryUsers)) {
        const QString key = directoryUserKey(u);
        const QStringList names =
            u.nodeName.split(QStringLiteral(", "), Qt::SkipEmptyParts);
        ownedNodes.insert(key, names);
        for (const QString &n : names) {
            const QString nl = n.trimmed().toLower();
            if (!nl.isEmpty())
                nodeOwner.insert(nl, key);
        }
    }

    // Group by the owning account: ownerUser when advertised, else the node's
    // owner resolved through the directory, else the display name. Two genuinely
    // different people with the same name keep separate groups (and get a
    // disambiguating id below); a person's several nodes fold into one.
    auto groupKeyFor = [&](const MemberInfo &m) -> QString {
        const QString owner = m.ownerUser.trimmed().toLower();
        if (!owner.isEmpty())
            return owner;
        const QString nodeL = m.nodeName.trimmed().toLower();
        if (!nodeL.isEmpty() && nodeOwner.contains(nodeL))
            return nodeOwner.value(nodeL);
        const QString nameL = m.name.trimmed().toLower();
        if (!nameL.isEmpty() && nodeOwner.contains(nameL))
            return nodeOwner.value(nameL);
        // "You" must fold into your OWN account's group (its directory entry and
        // owned nodes), not form a separate "\x01self" island — otherwise the
        // local user showed up as a second participant next to their own account
        // (a duplicate "jett"). Only fall back to a self-only key if the account
        // name is somehow unknown.
        if (m.self) {
            const QString selfOwner = accountOwner().trimmed().toLower();
            if (!selfOwner.isEmpty())
                return selfOwner;
        }
        return chatUserKey(m);
    };

    QList<ChatUserGroup> groups;
    QHash<QString, int> groupIndex; // key -> index in `groups`
    auto addMember = [&](MemberInfo member) {
        const bool online = member.self ? (m_backend != nullptr) : member.online;
        member.online = online;
        const QString display = chatUserDisplayName(member);
        if (display.isEmpty())
            return;
        member.name = display;
        const QString key = groupKeyFor(member);
        const bool isDirectory = member.id.startsWith(QStringLiteral("user:"));

        int idx;
        const auto it = groupIndex.constFind(key);
        if (it == groupIndex.constEnd()) {
            ChatUserGroup g;
            g.primary = member;
            g.key = key;
            g.online = online;
            idx = groups.size();
            groups.append(g);
            groupIndex.insert(key, idx);
        } else {
            idx = *it;
            ChatUserGroup &g = groups[idx];
            g.online = g.online || online;
            // Prefer the directory entry as the canonical row (stable account
            // name + avatar), but never let it hide that a node is live.
            const bool curDirectory =
                g.primary.id.startsWith(QStringLiteral("user:"));
            if (isDirectory && !curDirectory) {
                const bool wasOnline = g.primary.online;
                member.online = wasOnline || online;
                // Keep the "you" marker when the canonical row becomes the
                // directory entry, so the merged self group still sorts first
                // and renders as you.
                member.self = member.self || g.primary.self;
                g.primary = member;
            }
            const bool curAvatar = !m_avatars.value(g.primary.id).isNull();
            const bool candAvatar = !m_avatars.value(member.id).isNull();
            if (!curAvatar && candAvatar)
                m_avatars.insert(g.primary.id, m_avatars.value(member.id));
        }

        // A directory entry's nodeName is a joined list, not a single node; its
        // nodes are expanded from ownedNodes below. Live peers are real nodes.
        if (!isDirectory) {
            ChatUserGroup &g = groups[idx];
            if (g.anchorId.isEmpty() && !member.id.isEmpty())
                g.anchorId = member.id;
            ChatNodeBadge badge;
            badge.label = !member.nodeName.trimmed().isEmpty()
                              ? member.nodeName.trimmed()
                              : (!member.name.trimmed().isEmpty()
                                     ? member.name.trimmed()
                                     : member.id);
            badge.platform = member.platform;
            badge.online = online;
            const QString nk = (member.nodeName.trimmed().isEmpty()
                                    ? member.id
                                    : member.nodeName.trimmed())
                                   .toLower();
            if (!nk.isEmpty() && !g.nodeKeys.contains(nk)) {
                g.nodeKeys.insert(nk);
                g.nodes.append(badge);
            }
        }
    };
    // Resolve the live roster to registered accounts first. Roster identities
    // that have no database directory row are transient nodes/Guest users and
    // must never appear in the room's user picker.
    QHash<QString, QList<MemberInfo>> liveByUser;
    for (const MemberInfo &member : std::as_const(m_homeRoster)) {
        if (!member.self && !member.online)
            continue;
        const QString key = groupKeyFor(member);
        if (!m_chatDirectoryUsers.contains(key))
            continue;
        liveByUser[key].append(member);
    }

    QSet<QString> roomUserKeys;
    const bool privateOfficeRoom =
        m_officeChannelMirror &&
        m_officeChannelMirror->isPrivateConversation(m_currentConversation);
    if (privateOfficeRoom) {
        // The channel list is authorized for this account and returns the
        // private room's stored database membership. It is deliberately not
        // inferred from the global presence roster.
        for (const QString &username :
             m_officeChannelMirror->membersForConversation(
                 m_currentConversation)) {
            const QString key = username.trimmed().toLower();
            if (!key.isEmpty() && m_chatDirectoryUsers.contains(key))
                roomUserKeys.insert(key);
        }
    } else if (isDirectConversation(m_currentConversation)) {
        // A direct room contains the two registered accounts, not everyone
        // currently visible on the relay.
        const QString selfKey = accountOwner().trimmed().toLower();
        if (m_chatDirectoryUsers.contains(selfKey))
            roomUserKeys.insert(selfKey);
        const QString peerId = dmPeerId(m_currentConversation);
        const QString peerName =
            m_dmNames.value(peerId).trimmed().toLower();
        if (m_chatDirectoryUsers.contains(peerName))
            roomUserKeys.insert(peerName);
        for (const MemberInfo &member : std::as_const(m_homeRoster)) {
            if (member.id != peerId)
                continue;
            const QString key = groupKeyFor(member);
            if (m_chatDirectoryUsers.contains(key))
                roomUserKeys.insert(key);
            break;
        }
    } else {
        // Public mesh/office rooms (#general and friends) are open to every
        // registered account, so the users column lists the whole database
        // directory instead of only whoever happens to be online right now
        // (adhoc #129). Presence still drives the online dot, the sort order
        // and the node badges below; it just no longer decides membership.
        // Guests and unregistered node aliases have no directory row, so they
        // still stay out.
        for (auto it = m_chatDirectoryUsers.constBegin();
             it != m_chatDirectoryUsers.constEnd(); ++it)
            roomUserKeys.insert(it.key());
        for (auto it = liveByUser.constBegin(); it != liveByUser.constEnd(); ++it)
            roomUserKeys.insert(it.key());
    }

    for (const QString &key : std::as_const(roomUserKeys)) {
        const auto directory = m_chatDirectoryUsers.constFind(key);
        if (directory == m_chatDirectoryUsers.constEnd())
            continue;
        addMember(*directory);
        for (const MemberInfo &member : liveByUser.value(key))
            addMember(member);
    }

    // The users column lists people, and the only people are the registered
    // accounts /api/accounts/users returns. Everything else that turns up on the
    // roster — anonymous "Guest 3923" browser tabs, World visitors, bare nodes
    // with no owning account — is not a user and is dropped here instead of
    // padding the column (and its count) with rows nobody can look up.
    //
    // Only applied once the directory has actually loaded: before the first
    // successful fetch (or if it fails) an empty directory must not blank the
    // column. Our own row always stays, even if our account isn't listed yet.
    if (m_chatDirectoryLoaded) {
        QSet<QString> accountKeys;
        for (const MemberInfo &u : std::as_const(m_chatDirectoryUsers)) {
            const QString key = directoryAccountKey(u);
            if (!key.isEmpty())
                accountKeys.insert(key);
        }
        const QString selfKey = accountOwner().trimmed().toLower();
        if (!selfKey.isEmpty())
            accountKeys.insert(selfKey);
        groups.erase(std::remove_if(groups.begin(), groups.end(),
                                    [&](const ChatUserGroup &g) {
                                        return !g.primary.self &&
                                               !accountKeys.contains(g.key);
                                    }),
                     groups.end());
        groupIndex.clear(); // indices no longer line up; not used past here
    }

    // Fill in any owned nodes we didn't see live, as offline badges, so a user's
    // full fleet shows even when some (or all) of it is offline.
    for (ChatUserGroup &g : groups) {
        for (const QString &n : ownedNodes.value(g.key)) {
            const QString nl = n.trimmed().toLower();
            if (nl.isEmpty() || g.nodeKeys.contains(nl))
                continue;
            g.nodeKeys.insert(nl);
            ChatNodeBadge badge;
            badge.label = n.trimmed();
            badge.online = false;
            g.nodes.append(badge);
        }
    }

    std::sort(groups.begin(), groups.end(),
              [](const ChatUserGroup &a, const ChatUserGroup &b) {
                  if (a.primary.self != b.primary.self)
                      return a.primary.self;
                  if (a.online != b.online)
                      return a.online;
                  return a.primary.name.compare(b.primary.name,
                                                Qt::CaseInsensitive) < 0;
              });

    // Which display names are shared by more than one user? Only those rows need
    // a small id underneath to tell the look-alikes apart.
    QHash<QString, int> nameCounts;
    for (const ChatUserGroup &g : std::as_const(groups))
        ++nameCounts[g.primary.name.toLower()];

    for (const ChatUserGroup &group : std::as_const(groups)) {
        const MemberInfo &member = group.primary;

        // The whole card is clickable: it opens the user's profile popup with
        // their join date and account info (adhoc #209).
        auto *card = new ClickableIssueBody;
        card->setCursor(Qt::PointingHandCursor);
        card->setToolTip(QStringLiteral("View profile"));
        {
            QStringList nodeLines;
            for (const ChatNodeBadge &nb : std::as_const(group.nodes)) {
                QString line = nb.label.isEmpty() ? QStringLiteral("node") : nb.label;
                line += nb.online ? QStringLiteral(" · online")
                                  : QStringLiteral(" · offline");
                if (!nb.platform.trimmed().isEmpty())
                    line += QStringLiteral(" · ") + nb.platform.trimmed();
                nodeLines << line;
            }
            MemberInfo profileMember = member;
            profileMember.online = group.online;
            card->onClicked = [this, profileMember, nodeLines] {
                showChatUserProfile(profileMember, nodeLines);
            };
        }
        auto *row = new QHBoxLayout(card);
        row->setContentsMargins(4, 4, 4, 4);
        row->setSpacing(10);

        // Avatar: real one if known, else a generated letter tile.
        QPixmap avatar = m_avatars.value(member.id);
        if (avatar.isNull())
            avatar = letterFavicon(member.name);
        auto *icon = new QLabel;
        icon->setPixmap(roundedRectPixmap(avatar, 28, 8));
        icon->setFixedSize(28, 28);
        row->addWidget(icon, 0);

        auto *textCol = new QWidget;
        auto *col = new QVBoxLayout(textCol);
        col->setContentsMargins(0, 0, 0, 0);
        col->setSpacing(2);

        const QString dotColor = group.online ? QStringLiteral("#3fb950")
                                              : QStringLiteral("#8b949e");
        const QString state = group.online ? QStringLiteral("Online")
                                            : QStringLiteral("Offline");
        QString tooltip = state;
        if (!member.note.isEmpty())
            tooltip += QStringLiteral(" · ") + member.note;
        auto *nameLabel = new QLabel(
            QString::fromUtf8("<span style='color:%1'>\xE2\x97\x8F</span> %2%3")
                .arg(dotColor, member.name.toHtmlEscaped(),
                     member.self ? " <span style='color:#8b949e'>(you)</span>"
                                 : QString()));
        nameLabel->setTextFormat(Qt::RichText);
        nameLabel->setToolTip(tooltip);
        col->addWidget(nameLabel, 0);

        // Disambiguate look-alike usernames with a short, stable id.
        if (nameCounts.value(member.name.toLower()) > 1) {
            QString raw = group.anchorId.isEmpty() ? group.key : group.anchorId;
            if (raw.startsWith(QStringLiteral("user:")))
                raw = raw.mid(5);
            const QString shortId = raw.size() > 12
                ? QString::fromUtf8("%1\xE2\x80\xA6%2")
                      .arg(raw.left(4), raw.right(4))
                : raw;
            if (!shortId.isEmpty()) {
                auto *idLabel = new QLabel(
                    QStringLiteral("id %1").arg(shortId.toHtmlEscaped()));
                idLabel->setStyleSheet(
                    QStringLiteral("color:#8b949e; font-size:10px;"));
                idLabel->setToolTip(raw);
                col->addWidget(idLabel, 0);
            }
        }

        // The user's nodes as small OS badges, one per node (issue #411).
        if (!group.nodes.isEmpty()) {
            auto *nodesRow = new QWidget;
            auto *nl = new QHBoxLayout(nodesRow);
            nl->setContentsMargins(0, 0, 0, 0);
            nl->setSpacing(3);
            constexpr int kMaxIcons = 8;
            int shown = 0;
            for (const ChatNodeBadge &nb : std::as_const(group.nodes)) {
                if (shown >= kMaxIcons)
                    break;
                auto *ni = new QLabel;
                ni->setPixmap(osBadgeIcon(nb.platform, nb.online, 14).pixmap(14, 14));
                ni->setFixedSize(14, 14);
                QString tip = nb.label.isEmpty() ? QStringLiteral("node") : nb.label;
                tip += nb.online ? QStringLiteral(" · online")
                                 : QStringLiteral(" · offline");
                if (!nb.platform.trimmed().isEmpty())
                    tip += QStringLiteral(" · ") + nb.platform;
                ni->setToolTip(tip);
                nl->addWidget(ni, 0);
                ++shown;
            }
            if (group.nodes.size() > kMaxIcons) {
                auto *more = new QLabel(
                    QStringLiteral("+%1").arg(group.nodes.size() - kMaxIcons));
                more->setStyleSheet(
                    QStringLiteral("color:#8b949e; font-size:10px;"));
                nl->addWidget(more, 0);
            }
            nl->addStretch(1);
            col->addWidget(nodesRow, 0);
        }

        row->addWidget(textCol, 1);

        m_chatMembersLayout->insertWidget(m_chatMembersLayout->count() - 1, card);
    }

    if (m_chatMembersHeading)
        m_chatMembersHeading->setText(
            QString::fromUtf8("DATABASE USERS IN THIS ROOM \xE2\x80\x94 %1")
                .arg(groups.size()));
    if (m_chatMembersButton) {
        m_chatMembersButton->setText(QString::number(groups.size()));
        m_chatMembersButton->setToolTip(
            groups.size() == 1
                ? QStringLiteral("Show the 1 database user in this room")
                : QStringLiteral("Show the %1 database users in this room")
                      .arg(groups.size()));
    }
}

// Profile popup for a chat users-column row (adhoc #209): identity, when they
// joined, their nodes, and richer account info (bio, location, followers…)
// fetched live from the public accounts API.
void MainWindow::showChatUserProfile(const MemberInfo &member,
                                     const QStringList &nodeLines)
{
    const QString account =
        (member.ownerUser.trimmed().isEmpty() ? member.name : member.ownerUser)
            .trimmed()
            .toLower();

    auto *dialog = new QDialog(this);
    dialog->setObjectName("chatUserProfileDialog");
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setWindowTitle(member.name);
    dialog->setMinimumWidth(380);

    // --- Header: avatar + name + online state.
    QPixmap avatar = m_avatars.value(member.id);
    if (avatar.isNull())
        avatar = letterFavicon(member.name);
    auto *icon = new QLabel;
    icon->setPixmap(roundedRectPixmap(avatar, 56, 14));
    icon->setFixedSize(56, 56);

    auto *nameLabel = new QLabel(
        QStringLiteral("<span style='font-size:16px;font-weight:700'>%1</span>%2")
            .arg(member.name.toHtmlEscaped(),
                 member.self
                     ? QStringLiteral(" <span style='color:#8b949e'>(you)</span>")
                     : QString()));
    nameLabel->setTextFormat(Qt::RichText);
    auto *statusLabel = new QLabel(
        QString::fromUtf8("<span style='color:%1'>\xE2\x97\x8F</span> %2")
            .arg(member.online ? QStringLiteral("#3fb950")
                               : QStringLiteral("#8b949e"),
                 member.online ? QStringLiteral("Online")
                               : QStringLiteral("Offline")));
    statusLabel->setTextFormat(Qt::RichText);

    auto *headText = new QVBoxLayout;
    headText->setContentsMargins(0, 0, 0, 0);
    headText->setSpacing(2);
    headText->addWidget(nameLabel);
    headText->addWidget(statusLabel);
    headText->addStretch(1);

    auto *headRow = new QHBoxLayout;
    headRow->setContentsMargins(0, 0, 0, 0);
    headRow->setSpacing(12);
    headRow->addWidget(icon, 0, Qt::AlignTop);
    headRow->addLayout(headText, 1);

    // --- Details table, re-rendered when the async account lookup fills in
    // the rest. Rows we don't know yet are simply absent.
    auto *details = new QLabel;
    details->setTextFormat(Qt::RichText);
    details->setWordWrap(true);
    details->setTextInteractionFlags(Qt::TextSelectableByMouse);

    struct ProfileFacts {
        qint64 joinedMs = 0;
        int followers = -1, following = -1, mirrors = -1;
        QString location;
        bool isAdmin = false;
    };
    auto facts = std::make_shared<ProfileFacts>();
    facts->joinedMs = member.createdAtMs;
    // The directory user carries createdAt; a live node picked before its
    // directory entry may not — the same-account directory row fills it.
    if (facts->joinedMs <= 0) {
        const MemberInfo dirUser = m_chatDirectoryUsers.value(account);
        facts->joinedMs = dirUser.createdAtMs;
    }

    const QString accountShown = account;
    auto renderDetails = [details, facts, accountShown, nodeLines] {
        auto detailRow = [](const QString &key, const QString &value) {
            return QStringLiteral(
                       "<tr><td style='color:#8b949e;padding:2px 14px 2px 0;"
                       "white-space:nowrap'>%1</td>"
                       "<td style='padding:2px 0'>%2</td></tr>")
                .arg(key, value);
        };
        QStringList rows;
        if (!accountShown.isEmpty())
            rows << detailRow(QStringLiteral("Account"),
                              QStringLiteral("@%1").arg(accountShown.toHtmlEscaped()));
        if (facts->joinedMs > 0)
            rows << detailRow(
                QStringLiteral("Joined"),
                QStringLiteral("%1 <span style='color:#8b949e'>(%2)</span>")
                    .arg(formatRepoDate(facts->joinedMs),
                         formatIssueRelativeTime(facts->joinedMs)));
        if (!facts->location.isEmpty())
            rows << detailRow(QStringLiteral("Location"),
                              facts->location.toHtmlEscaped());
        if (facts->followers >= 0)
            rows << detailRow(QStringLiteral("Followers"),
                              QString::number(facts->followers));
        if (facts->following >= 0)
            rows << detailRow(QStringLiteral("Following"),
                              QString::number(facts->following));
        if (facts->mirrors > 0)
            rows << detailRow(QStringLiteral("Mirrored repos"),
                              QString::number(facts->mirrors));
        if (facts->isAdmin)
            rows << detailRow(QStringLiteral("Role"),
                              QString::fromUtf8("Administrator \xF0\x9F\x91\x91"));
        if (!nodeLines.isEmpty())
            rows << detailRow(
                QStringLiteral("Nodes (%1)").arg(nodeLines.size()),
                QStringList(nodeLines).replaceInStrings(
                    QStringLiteral("&"), QStringLiteral("&amp;"))
                    .replaceInStrings(QStringLiteral("<"), QStringLiteral("&lt;"))
                    .join(QStringLiteral("<br>")));
        details->setText(
            QStringLiteral("<table cellspacing='0' cellpadding='0'>%1</table>")
                .arg(rows.join(QString())));
    };
    renderDetails();

    // Bio paragraph, filled by the lookup when the account has one.
    auto *bioLabel = new QLabel;
    bioLabel->setWordWrap(true);
    bioLabel->setVisible(false);
    bioLabel->setStyleSheet(QStringLiteral("color:#c9d1d9;"));
    auto *loadingLabel = new QLabel(QString::fromUtf8("Loading profile\xE2\x80\xA6"));
    loadingLabel->setStyleSheet(QStringLiteral("color:#8b949e; font-size:11px;"));

    // --- Actions: DM them (needs a live node), open the full web profile.
    auto *buttonRow = new QHBoxLayout;
    buttonRow->setContentsMargins(0, 0, 0, 0);
    buttonRow->setSpacing(8);
    if (!member.self) {
        // A DM needs a live peer: prefer the clicked entry's own id when it is
        // a real node, else any online roster node owned by this account.
        QString peerId, peerName;
        if (!member.id.isEmpty() &&
            !member.id.startsWith(QStringLiteral("user:")) && member.online) {
            peerId = member.id;
            peerName = member.name;
        } else {
            for (const MemberInfo &m : std::as_const(m_homeRoster)) {
                if (!m.online || m.id.isEmpty() || m.self)
                    continue;
                const QString owner = m.ownerUser.trimmed().toLower();
                const QString name = m.name.trimmed().toLower();
                if (owner == account || (owner.isEmpty() && name == account)) {
                    peerId = m.id;
                    peerName = member.name;
                    break;
                }
            }
        }
        if (!peerId.isEmpty()) {
            auto *messageButton = new QPushButton(QStringLiteral("Message"));
            messageButton->setObjectName("primaryButton");
            messageButton->setCursor(Qt::PointingHandCursor);
            connect(messageButton, &QPushButton::clicked, this,
                    [this, dialog, peerId, peerName] {
                        dialog->accept();
                        openDirectChat(peerId, peerName);
                    });
            buttonRow->addWidget(messageButton);
        }
    }
    if (!account.isEmpty()) {
        auto *webButton = new QPushButton(QStringLiteral("Web profile"));
        webButton->setObjectName("ghostButton");
        webButton->setCursor(Qt::PointingHandCursor);
        connect(webButton, &QPushButton::clicked, this, [this, account] {
            const QUrl url = chatMentionProfileUrl(
                m_serverUrlEdit ? m_serverUrlEdit->text() : QString(), account);
            if (url.isValid() && !url.host().isEmpty())
                QDesktopServices::openUrl(url);
        });
        buttonRow->addWidget(webButton);
    }
    buttonRow->addStretch(1);
    auto *closeButton = new QPushButton(QStringLiteral("Close"));
    closeButton->setObjectName("ghostButton");
    closeButton->setCursor(Qt::PointingHandCursor);
    connect(closeButton, &QPushButton::clicked, dialog, &QDialog::accept);
    buttonRow->addWidget(closeButton);

    auto *layout = new QVBoxLayout(dialog);
    layout->setContentsMargins(20, 18, 20, 16);
    layout->setSpacing(12);
    layout->addLayout(headRow);
    layout->addWidget(details);
    layout->addWidget(bioLabel);
    layout->addWidget(loadingLabel);
    layout->addStretch(1);
    layout->addLayout(buttonRow);

    // --- Fill in the rest from the public account lookup (join date for live
    // nodes, bio, location, follower counts, admin badge). Guarded with
    // QPointers: the reply may land after the popup was closed.
    if (m_networkAccess && !account.isEmpty()) {
        QNetworkRequest request(accountsApiUrl(account));
        request.setRawHeader("Accept", "application/json");
        QNetworkReply *reply = m_networkAccess->get(request);
        QPointer<QDialog> dialogGuard(dialog);
        QPointer<QLabel> bioGuard(bioLabel);
        QPointer<QLabel> loadingGuard(loadingLabel);
        connect(reply, &QNetworkReply::finished, this,
                [reply, dialogGuard, bioGuard, loadingGuard, facts,
                 renderDetails] {
                    const QByteArray body = reply->readAll();
                    const bool ok = reply->error() == QNetworkReply::NoError;
                    reply->deleteLater();
                    if (!dialogGuard)
                        return;
                    if (loadingGuard)
                        loadingGuard->setVisible(false);
                    if (!ok)
                        return;
                    const QJsonObject rec = QJsonDocument::fromJson(body).object();
                    if (!rec.value(QStringLiteral("exists")).toBool(true))
                        return;
                    const qint64 created =
                        qint64(rec.value(QStringLiteral("createdAt")).toDouble());
                    if (created > 0)
                        facts->joinedMs = created;
                    facts->location =
                        rec.value(QStringLiteral("profileLocation")).toString().trimmed();
                    facts->followers =
                        rec.value(QStringLiteral("followers")).toInt(-1);
                    facts->following =
                        rec.value(QStringLiteral("following")).toInt(-1);
                    facts->mirrors =
                        rec.value(QStringLiteral("mirrorCount")).toInt(-1);
                    facts->isAdmin = rec.value(QStringLiteral("isAdmin")).toBool();
                    renderDetails();
                    const QString bio =
                        rec.value(QStringLiteral("profileBio")).toString().trimmed();
                    if (bioGuard && !bio.isEmpty()) {
                        bioGuard->setText(bio.toHtmlEscaped());
                        bioGuard->setVisible(true);
                    }
                });
    } else {
        loadingLabel->setVisible(false);
    }

    dialog->show();
}

void MainWindow::removeChatMember(const QString &id, const QString &name)
{
    if (id.isEmpty())
        return;

    // Mark before forgetMember fires rosterChanged, so the retain loop in
    // setRoster skips this id and doesn't bring it back as an offline entry.
    m_removedPeerIds.insert(id);

    if (m_backend)
        m_backend->forgetMember(id);

    // Drop any open direct chat with them and leave that conversation.
    const QString conversation = dmKey(id);
    m_openDms.removeAll(id);
    m_dmNames.remove(id);
    m_avatars.remove(id);
    m_unread.remove(conversation);
    m_unreadCounts.remove(conversation);
    const QList<ChatMessage> removedMessages = m_history.take(conversation);
    for (const ChatMessage &message : removedMessages) {
        m_historyIds.remove(message.id);
        m_reactions.remove(message.id);
    }
    m_typing.remove(conversation);
    for (const QString &key : m_typing.keys()) {
        m_typing[key].remove(id);
        if (m_typing.value(key).isEmpty())
            m_typing.remove(key);
    }
    if (m_typingConversation == conversation)
        sendTypingState(false);
    if (m_currentConversation == conversation) {
        if (m_channels.isEmpty()) {
            m_currentConversation.clear();
            m_channelTitle->setText(QStringLiteral("No conversation"));
            m_messageInput->setPlaceholderText(QStringLiteral("Message"));
            rebuildConversationView();
            refreshTypingLabel();
        } else {
            switchConversation(m_channels.first());
        }
    }

    // Remove the stale roster entry now; a fresh roster update re-adds them if
    // they join the network again.
    QList<MemberInfo> remaining;
    for (const MemberInfo &m : std::as_const(m_homeRoster))
        if (m.id != id)
            remaining.append(m);
    refreshDmList();
    setRoster(remaining);
    saveChatHistory();
    logSystem("Removed stale member " + (name.isEmpty() ? id.left(8) : name) + ".");
}

void MainWindow::refreshChannelList()
{
    QSignalBlocker blocker(m_channelList);
    m_channelList->clear();
    for (const QString &channel : std::as_const(m_channels)) {
        // Private rooms get a padlock so they read differently from public
        // channels in the same list.
        const QString prefix = (m_unread.contains(channel) ? "\xE2\x97\x8F " : "") +
                               (m_privateChannels.contains(channel)
                                    ? QString::fromUtf8("\xF0\x9F\x94\x92 ")
                                    : QString());
        auto *item = new QListWidgetItem(prefix + channel);
        item->setData(Qt::UserRole, channel);
        m_channelList->addItem(item);
        if (channel == m_currentConversation)
            m_channelList->setCurrentItem(item);
    }
    updateChatButton();
}

void MainWindow::refreshDmList()
{
    QSignalBlocker blocker(m_dmList);
    m_dmList->clear();
    for (const QString &peerId : std::as_const(m_openDms)) {
        const QString key = dmKey(peerId);
        auto *item = new QListWidgetItem(
            (m_unread.contains(key) ? "\xE2\x97\x8F " : "") + kDmPrefix +
            m_dmNames.value(peerId, QStringLiteral("unknown")));
        item->setData(Qt::UserRole, key);
        m_dmList->addItem(item);
        if (key == m_currentConversation)
            m_dmList->setCurrentItem(item);
    }
    updateHomeStats();
    updateChatButton();
}

void MainWindow::switchConversation(const QString &conversation)
{
    if (conversation.isEmpty() || conversation == m_currentConversation)
        return;
    sendTypingState(false);
    m_currentConversation = conversation;
    m_unread.remove(conversation);
    m_unreadCounts.remove(conversation);
    // Unread state persists across restarts now, so reading a conversation has
    // to reach disk too — otherwise a restart resurrects the cleared badge.
    scheduleChatSave();

    QString title = conversation;
    if (isDirectConversation(conversation))
        title = kDmPrefix + m_dmNames.value(dmPeerId(conversation),
                                            QStringLiteral("unknown"));
    m_channelTitle->setText(title);
    m_messageInput->setReadOnly(false);
    m_messageInput->setPlaceholderText("Message " + title);
    if (m_inviteButton)
        m_inviteButton->setVisible(m_privateChannels.contains(conversation));
    rebuildConversationView();
    refreshTypingLabel();
    refreshChatMembers();

    // Selection lives in exactly one sidebar list at a time.
    if (isDirectConversation(conversation)) {
        QSignalBlocker blocker(m_channelList);
        m_channelList->clearSelection();
        m_channelList->setCurrentItem(nullptr);
    } else {
        QSignalBlocker blocker(m_dmList);
        m_dmList->clearSelection();
        m_dmList->setCurrentItem(nullptr);
    }
    refreshChannelList();
    refreshDmList();
}

void MainWindow::openDirectChat(const QString &peerId, const QString &peerName)
{
    m_dmNames.insert(peerId, peerName);
    if (!m_openDms.contains(peerId)) {
        m_openDms.append(peerId);
        refreshDmList();
    }
    showChatView(); // chat has no tab now — surface the chat view explicitly
    switchConversation(dmKey(peerId));
    m_messageInput->setFocus();
}

void MainWindow::promptAddChannel()
{
    if (!m_backend)
        return;
    bool ok = false;
    const QString name = QInputDialog::getText(
        this, "Add channel", "Channel name:", QLineEdit::Normal, "#", &ok);
    if (ok && !name.trimmed().isEmpty() && name.trimmed() != "#")
        m_backend->addChannel(name);
}

void MainWindow::promptAddPrivateChannel()
{
    if (!m_backend)
        return;
    bool ok = false;
    const QString name = QInputDialog::getText(
        this, "New private room",
        "Room name (only people you invite can see it):", QLineEdit::Normal, "#",
        &ok);
    const QString trimmed = name.trimmed();
    if (!ok || trimmed.isEmpty() || trimmed == "#")
        return;
    m_backend->createPrivateChannel(trimmed);
    // Jump into the new room. createPrivateChannel prepends '#' if missing, so
    // match that when switching to it.
    QString key = trimmed;
    if (!key.startsWith('#'))
        key.prepend('#');
    switchConversation(key);
    // Offer to invite people right away.
    promptInviteToPrivateChannel();
}

void MainWindow::promptInviteToPrivateChannel()
{
    if (!m_backend)
        return;
    const QString channel = m_currentConversation;
    if (channel.isEmpty() || !m_privateChannels.contains(channel)) {
        QMessageBox::information(this, "Invite",
                                 "Open a private room first, then invite people.");
        return;
    }

    // Invites still require a live backend peer id, so offer online members only,
    // de-duplicated by user name so one person's several nodes appear once.
    QMenu menu(this);
    QSet<QString> seen;
    bool any = false;
    for (const MemberInfo &member : std::as_const(m_homeRoster)) {
        if (member.self || !member.online || member.id.isEmpty())
            continue;
        const QString key = member.name.trimmed().toLower();
        if (!key.isEmpty() && seen.contains(key))
            continue;
        if (!key.isEmpty())
            seen.insert(key);
        any = true;
        const QString peerId = member.id;
        const QString peerName = member.name;
        QAction *act = menu.addAction(member.name);
        connect(act, &QAction::triggered, this, [this, peerId, peerName, channel] {
            m_backend->inviteToChannel(peerId, channel);
            logSystem("Invited " + peerName + " to " + channel + ".");
        });
    }
    if (!any) {
        QAction *empty = menu.addAction("No other members online");
        empty->setEnabled(false);
    }
    menu.exec(QCursor::pos());
}

void MainWindow::promptDeleteRoom(const QString &channel)
{
    if (!m_backend || channel.isEmpty() || isDirectConversation(channel))
        return;
    const bool priv = m_privateChannels.contains(channel);
    const QString what = priv ? QStringLiteral("private room")
                              : QStringLiteral("channel");
    const auto choice = QMessageBox::question(
        this, QStringLiteral("Delete room"),
        QStringLiteral("Delete the %1 %2?\n\nIt's removed from this device only — "
                       "other people keep it. You won't see new messages in it "
                       "unless you re-create or are re-invited to it.")
            .arg(what, channel),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (choice != QMessageBox::Yes)
        return;

    m_backend->removeChannel(channel);
    m_privateChannels.remove(channel);
    persistPrivateChannels();
    m_unread.remove(channel);
    m_unreadCounts.remove(channel);
    // Leaving the room we're viewing: fall back to the first remaining channel
    // (or clear the view if none are left).
    if (m_currentConversation == channel) {
        m_currentConversation.clear();
        const QString fallback = m_channels.value(0);
        if (!fallback.isEmpty())
            switchConversation(fallback);
        else
            m_channelTitle->setText(QString());
    }
    scheduleChatSave();
    logSystem(QStringLiteral("Deleted %1 %2 (this device only).").arg(what, channel));
}

void MainWindow::restorePrivateChannels()
{
    if (!m_backend)
        return;
    const QStringList saved =
        QSettings().value(QStringLiteral("chat/privateChannels")).toStringList();
    for (const QString &name : saved) {
        const QString trimmed = name.trimmed();
        if (!trimmed.isEmpty())
            m_backend->createPrivateChannel(trimmed);
    }
}

void MainWindow::persistPrivateChannels()
{
    QStringList list(m_privateChannels.constBegin(), m_privateChannels.constEnd());
    list.sort();
    QSettings().setValue(QStringLiteral("chat/privateChannels"), list);
}

// Office conversations use a separate ticketed socket. This predicate keeps
// unsupported operations (typing/reactions/attachments) off the mesh backend;
// plain text is routed through OfficeChannelMirror below.
bool MainWindow::isOfficeConversation(const QString &conversation) const
{
    return forkmesh::office::isOfficeConversation(conversation);
}

void MainWindow::sendCurrentMessage()
{
    const QString text = m_messageInput->text().trimmed();
    if (text.isEmpty() || m_currentConversation.isEmpty())
        return;
    if (isOfficeConversation(m_currentConversation)) {
        if (!m_officeChannelMirror ||
            !m_officeChannelMirror->sendMessage(m_currentConversation, text)) {
            logSystem(QStringLiteral("Office chat: %1 is not currently writable.")
                          .arg(m_currentConversation));
            return;
        }
        m_messageInput->clear();
        return;
    }
    if (!m_backend)
        return;
    sendTypingState(false);
    if (isDirectConversation(m_currentConversation)) {
        m_backend->sendDirect(dmPeerId(m_currentConversation), text);
    } else {
        m_backend->sendChat(m_currentConversation, text);
        maybeAskForkbot(m_currentConversation, text);
    }
    m_messageInput->clear();
}

void MainWindow::insertEmojiIntoComposer(const QString &emoji)
{
    if (!m_messageInput || emoji.isEmpty())
        return;
    m_messageInput->insert(emoji);
    m_messageInput->setFocus();
}

void MainWindow::sendMessageToPrompt(const QString &text)
{
    if (text.isEmpty())
        return;
    // The footer's bottom-right prompt box, not the chat input: a message worth
    // reusing is almost always a task for an agent, so it lands where the
    // app-wide "Send to Prompt" selection action puts text (adhoc #108).
    appendTextToActivePrompt(text);
}

void MainWindow::showEmojiPicker(QWidget *anchor)
{
    if (!m_messageInput)
        return;
    // A curated grid of common emoji, encoded as UTF-8 (the codebase's
    // convention — a color-emoji font is bundled so these render in color).
    static const char *const kEmoji[] = {
        "\xF0\x9F\x98\x80" /*😀*/, "\xF0\x9F\x98\x81", "\xF0\x9F\x98\x82",
        "\xF0\x9F\xA4\xA3", "\xF0\x9F\x98\x8A", "\xF0\x9F\x98\x8D",
        "\xF0\x9F\x98\x8E", "\xF0\x9F\x98\x89", "\xF0\x9F\x99\x82",
        "\xF0\x9F\x98\xA2", "\xF0\x9F\x98\xAD", "\xF0\x9F\x98\xA1",
        "\xF0\x9F\x98\xB1", "\xF0\x9F\xA4\x94", "\xF0\x9F\x98\xB4",
        "\xF0\x9F\xA4\xAF", "\xF0\x9F\x91\x8D", "\xF0\x9F\x91\x8E",
        "\xF0\x9F\x91\x8F", "\xF0\x9F\x99\x8F", "\xF0\x9F\x92\xAA",
        "\xF0\x9F\x99\x8C", "\xF0\x9F\x91\x8B", "\xF0\x9F\xA4\x9D",
        "\xF0\x9F\x94\xA5", "\xE2\x9C\xA8", "\xF0\x9F\x8E\x89",
        "\xF0\x9F\x92\xAF", "\xE2\x9D\xA4\xEF\xB8\x8F", "\xF0\x9F\x92\x94",
        "\xE2\xAD\x90", "\xE2\x9C\x85", "\xE2\x9D\x8C", "\xF0\x9F\x91\x80",
        "\xF0\x9F\x9A\x80", "\xF0\x9F\x90\x9B", "\xF0\x9F\x92\xA1",
        "\xF0\x9F\x93\x8C", "\xE2\x98\x95", "\xF0\x9F\x8D\x95",
        "\xF0\x9F\x8E\x82", "\xF0\x9F\xA5\xB3",
    };

    QMenu menu(this);
    auto *grid = new QWidget(&menu);
    auto *gridLayout = new QGridLayout(grid);
    gridLayout->setContentsMargins(6, 6, 6, 6);
    gridLayout->setSpacing(2);
    constexpr int kColumns = 7;
    int index = 0;
    for (const char *utf8 : kEmoji) {
        const QString emoji = QString::fromUtf8(utf8);
        auto *button = new QToolButton(grid);
        button->setObjectName("emojiPickerButton");
        button->setText(emoji);
        button->setCursor(Qt::PointingHandCursor);
        button->setAutoRaise(true);
        connect(button, &QToolButton::clicked, &menu, [this, emoji, &menu] {
            insertEmojiIntoComposer(emoji);
            menu.close();
        });
        gridLayout->addWidget(button, index / kColumns, index % kColumns);
        ++index;
    }
    auto *action = new QWidgetAction(&menu);
    action->setDefaultWidget(grid);
    menu.addAction(action);
    if (anchor)
        menu.exec(anchor->mapToGlobal(QPoint(0, -menu.sizeHint().height())));
    else
        menu.exec(QCursor::pos());
}

void MainWindow::maybeAskForkbot(const QString &conversation, const QString &text)
{
    // Mirror of the web chat's maybeAskForkbot: only the message's own author
    // triggers the bot (so several connected clients never double-fire it),
    // only in public channels (a private room's members deliberately excluded
    // the relay — the bot round-trip would leak the text to it), and the
    // recent conversation rides along so ForkBot can resolve references like
    // "that bug" the same way it does for web users.
    static const QRegularExpression forkbotMention(
        QStringLiteral("(?:^|[^A-Za-z0-9_-])@?forkbot\\b"),
        QRegularExpression::CaseInsensitiveOption);
    if (!m_networkAccess || !m_backend)
        return;
    if (m_privateChannels.contains(conversation))
        return;
    if (!forkbotMention.match(text).hasMatch())
        return;

    // The lead-up conversation, oldest first: the last few non-ForkBot lines
    // before the triggering message (which sendChat just appended via
    // emitChat -> onMessage, so it is the final history entry — drop it; the
    // relay receives it separately as `message`).
    QJsonArray context;
    const QList<ChatMessage> &history = m_history.value(conversation);
    const int end = history.size() - 1; // exclude the triggering message
    for (int i = qMax(0, end - 12); i < end; ++i) {
        const ChatMessage &entry = history.at(i);
        if (entry.senderId == QLatin1String("forkbot") || entry.text.isEmpty())
            continue;
        context.append(QJsonObject{
            {QStringLiteral("sender"), entry.senderName.left(32)},
            {QStringLiteral("text"), entry.text.left(600)},
        });
    }

    QUrl url = catalogApiUrl();
    url.setPath(QStringLiteral("/api/forkbot/chat"));
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader,
                      QStringLiteral("application/json"));
    const QJsonObject payload{{QStringLiteral("message"), text},
                              {QStringLiteral("sender"), chatDisplayName()},
                              {QStringLiteral("room"), QStringLiteral("general")},
                              {QStringLiteral("context"), context}};
    const QString channel = conversation;
    QNetworkReply *reply = m_networkAccess->post(
        request, QJsonDocument(payload).toJson(QJsonDocument::Compact));
    connect(reply, &QNetworkReply::finished, this, [this, reply, channel] {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError)
            return;
        const QJsonObject resp =
            QJsonDocument::fromJson(reply->readAll()).object();
        const QString botMessage =
            resp.value(QStringLiteral("botMessage")).toString().trimmed();
        if (botMessage.isEmpty() || !m_backend)
            return;
        // Relay the reply into the room as forkbot so every surface (desktop
        // + web) sees the same answer.
        m_backend->sendBotChat(channel, botMessage);
    });
}

void MainWindow::onComposerEdited(const QString &text)
{
    // Offer @-mention completions as the user types (independent of the typing
    // indicator, which needs a live backend + conversation).
    updateMentionPopup();
    if (!m_backend || m_currentConversation.isEmpty())
        return;
    if (text.trimmed().isEmpty()) {
        sendTypingState(false);
        return;
    }
    sendTypingState(true);
    m_typingStopTimer->start(2500);
}

// The word ending at the caret that an @-mention is being typed into, or empty
// when the caret isn't inside one. `tokenStart` (when given) receives the index
// of the leading '@'. An '@' only opens a mention at the start of the line or
// after whitespace, so emails and "a@b" mid-word don't trigger the popup.
static QString mentionTokenAt(const QString &text, int cursor, int *tokenStart)
{
    if (tokenStart)
        *tokenStart = -1;
    int start = cursor - 1;
    while (start >= 0 && !text.at(start).isSpace() &&
           text.at(start) != QLatin1Char('@'))
        --start;
    if (start < 0 || text.at(start) != QLatin1Char('@'))
        return QString();
    if (start > 0 && !text.at(start - 1).isSpace())
        return QString();
    if (tokenStart)
        *tokenStart = start;
    return text.mid(start + 1, cursor - start - 1);
}

// Names a message can @-mention: every known user except ourselves, de-duped
// and sorted so the popup is stable. Self is excluded — you don't ping yourself.
void MainWindow::refreshMentionCandidates()
{
    if (!m_mentionModel)
        return;
    QStringList names;
    auto addName = [&](const MemberInfo &member) {
        const QString name = chatUserDisplayName(member);
        if (member.self || name.isEmpty())
            return;
        if (!names.contains(name, Qt::CaseInsensitive))
            names.append(name);
    };
    for (const MemberInfo &member : std::as_const(m_homeRoster))
        addName(member);
    for (const MemberInfo &member : std::as_const(m_chatDirectoryUsers))
        addName(member);
    const QString ownName = accountOwner().trimmed();
    if (!ownName.isEmpty()) {
        for (int i = names.size() - 1; i >= 0; --i)
            if (names.at(i).compare(ownName, Qt::CaseInsensitive) == 0)
                names.removeAt(i);
    }
    names.sort(Qt::CaseInsensitive);
    m_mentionModel->setStringList(names);
}

void MainWindow::updateMentionPopup()
{
    if (!m_mentionCompleter || !m_messageInput)
        return;
    int tokenStart = -1;
    const QString token =
        mentionTokenAt(m_messageInput->text(), m_messageInput->cursorPosition(),
                       &tokenStart);
    if (tokenStart < 0 || m_mentionModel->rowCount() == 0) {
        m_mentionCompleter->popup()->hide();
        return;
    }
    m_mentionCompleter->setCompletionPrefix(token);
    if (m_mentionCompleter->completionCount() == 0) {
        m_mentionCompleter->popup()->hide();
        return;
    }
    // Width the popup to its widest entry; complete() anchors it under the input.
    QRect rect = m_messageInput->rect();
    rect.setWidth(m_mentionCompleter->popup()->sizeHintForColumn(0) + 24);
    m_mentionCompleter->complete(rect);
}

void MainWindow::insertMention(const QString &name)
{
    if (!m_messageInput || name.isEmpty())
        return;
    QString text = m_messageInput->text();
    int tokenStart = -1;
    mentionTokenAt(text, m_messageInput->cursorPosition(), &tokenStart);
    if (tokenStart < 0)
        return;
    const QString mention = QLatin1Char('@') + name + QLatin1Char(' ');
    text.replace(tokenStart, m_messageInput->cursorPosition() - tokenStart, mention);
    m_messageInput->setText(text);
    m_messageInput->setCursorPosition(tokenStart + mention.length());
}

void MainWindow::sendTypingState(bool active)
{
    if (!m_backend)
        return;
    if (active && isOfficeConversation(m_currentConversation))
        return;
    if (active) {
        if (m_currentConversation.isEmpty())
            return;
        if (m_typingConversation == m_currentConversation &&
            m_typingStopTimer->isActive())
            return;
        m_typingConversation = m_currentConversation;
        m_backend->sendTyping(m_currentConversation, true);
        return;
    }
    if (!m_typingConversation.isEmpty()) {
        m_backend->sendTyping(m_typingConversation, false);
        m_typingConversation.clear();
    }
    m_typingStopTimer->stop();
}

void MainWindow::refreshTypingLabel()
{
    QStringList names;
    const auto active = m_typing.value(m_currentConversation);
    for (const QString &name : active)
        names.append(name);
    names.removeDuplicates();

    if (names.isEmpty()) {
        m_typingLabel->clear();
    } else if (names.size() == 1) {
        m_typingLabel->setText(names.first() + " is typing...");
    } else if (names.size() == 2) {
        m_typingLabel->setText(names.at(0) + " and " + names.at(1) +
                               " are typing...");
    } else {
        m_typingLabel->setText(QString::number(names.size()) + " people are typing...");
    }
}

// ------------------------------------------------------------------- files

void MainWindow::attachFile()
{
    if (!m_backend || m_currentConversation.isEmpty() ||
        isOfficeConversation(m_currentConversation))
        return;
    const QString path =
        QFileDialog::getOpenFileName(this, "Share a file", QString(), "All files (*)");
    if (path.isEmpty())
        return;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        QMessageBox::warning(this, "Share a file", "Could not read " + path);
        return;
    }
    // Keep LAN transfers reasonable; the wire frame caps at ~96 MB.
    const qint64 maxBytes = 64ll * 1024 * 1024;
    if (file.size() > maxBytes) {
        QMessageBox::warning(this, "Share a file",
                             "That file is larger than 64 MB. Please share a "
                             "smaller file.");
        return;
    }
    const QByteArray data = file.readAll();
    const QFileInfo info(path);
    const QString mime = QMimeDatabase().mimeTypeForFileNameAndData(path, data).name();
    m_backend->sendFile(m_currentConversation, info.fileName(), mime, data);
}

// Open a chat image attachment full-size in a lightbox dialog. The inline row
// only shows a downscaled preview; this restores the original pixels (scaled down
// only if larger than the screen) inside a scrollable, frameless viewer.
void MainWindow::showChatImageDetail(const QString &fileName,
                                     const QByteArray &data)
{
    QPixmap pixmap;
    if (!pixmap.loadFromData(data)) {
        // Not a decodable image after all — fall back to the save dialog.
        saveIncomingFile(fileName, data);
        return;
    }

    auto *dialog = new QDialog(this);
    dialog->setObjectName("imageDetailDialog");
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setWindowTitle(fileName.isEmpty() ? QStringLiteral("Image")
                                              : fileName);

    // Cap the displayed size to most of the available screen so huge images
    // don't open larger than the monitor; smaller images show at native size.
    QSize maxSize(1200, 800);
    if (QScreen *screen = QGuiApplication::primaryScreen()) {
        const QSize avail = screen->availableSize();
        maxSize = QSize(avail.width() * 9 / 10, avail.height() * 9 / 10);
    }
    QPixmap shown = pixmap;
    if (pixmap.width() > maxSize.width() || pixmap.height() > maxSize.height())
        shown = pixmap.scaled(maxSize, Qt::KeepAspectRatio,
                              Qt::SmoothTransformation);

    auto *imageLabel = new QLabel;
    imageLabel->setAlignment(Qt::AlignCenter);
    imageLabel->setPixmap(shown);

    auto *scroll = new QScrollArea;
    scroll->setObjectName("messageView");
    scroll->setWidgetResizable(true);
    scroll->setAlignment(Qt::AlignCenter);
    scroll->setWidget(imageLabel);

    auto *saveButton = new QPushButton(QStringLiteral("Save\xE2\x80\xA6"));
    saveButton->setObjectName("ghostButton");
    saveButton->setCursor(Qt::PointingHandCursor);
    connect(saveButton, &QPushButton::clicked, this,
            [this, fileName, data] { saveIncomingFile(fileName, data); });
    auto *closeButton = new QPushButton(QStringLiteral("Close"));
    closeButton->setObjectName("primaryButton");
    closeButton->setCursor(Qt::PointingHandCursor);
    connect(closeButton, &QPushButton::clicked, dialog, &QDialog::accept);

    auto *buttonRow = new QHBoxLayout;
    buttonRow->setContentsMargins(0, 0, 0, 0);
    buttonRow->addWidget(new QLabel(
        QStringLiteral("%1 \xC3\x97 %2").arg(pixmap.width()).arg(pixmap.height())));
    buttonRow->addStretch();
    buttonRow->addWidget(saveButton);
    buttonRow->addWidget(closeButton);

    auto *layout = new QVBoxLayout(dialog);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setSpacing(10);
    layout->addWidget(scroll, 1);
    layout->addLayout(buttonRow);

    dialog->resize(qMin(shown.width() + 48, maxSize.width()),
                   qMin(shown.height() + 96, maxSize.height()));
    dialog->show();
}

// If the clipboard holds an image (e.g. a screenshot), share it in the current
// conversation as a PNG attachment and return true. Used to support Ctrl+V in
// the composer; returns false so a normal text paste proceeds as usual.
bool MainWindow::trySendClipboardImage()
{
    if (!m_backend || m_currentConversation.isEmpty() ||
        isOfficeConversation(m_currentConversation))
        return false;
    const QMimeData *mime = QGuiApplication::clipboard()->mimeData();
    if (!mime || !mime->hasImage())
        return false;
    const QImage image = qvariant_cast<QImage>(mime->imageData());
    if (image.isNull())
        return false;
    QByteArray data;
    QBuffer buffer(&data);
    buffer.open(QIODevice::WriteOnly);
    if (!image.save(&buffer, "PNG"))
        return false;
    m_backend->sendFile(m_currentConversation,
                        QStringLiteral("pasted-image.png"), QStringLiteral("image/png"),
                        data);
    return true;
}

void MainWindow::saveIncomingFile(const QString &fileName, const QByteArray &data)
{
    const QString safeName = QFileInfo(fileName).fileName().left(180);
    const QString dir =
        QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
    const QString suggested =
        (dir.isEmpty() ? QDir::homePath() : dir) + "/" +
        (safeName.isEmpty() ? QStringLiteral("file") : safeName);
    const QString path =
        QFileDialog::getSaveFileName(this, "Save file", suggested);
    if (path.isEmpty())
        return;
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(data) != data.size()) {
        QMessageBox::warning(this, "Save file", "Could not save to " + path);
        return;
    }
}
