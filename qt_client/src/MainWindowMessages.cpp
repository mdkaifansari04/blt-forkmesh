// MainWindowMessages: MainWindow feature methods, split out of MainWindow.cpp.
// Headless/status messages and the files view.
//
// These are MainWindow member functions defined in their own translation unit;
// the class itself is declared in MainWindow.h. Shared helpers live in
// MainWindowInternal.h / MainWindowShared.cpp (namespace forkmesh::ui).

#include "MainWindow.h"
#include "MainWindowInternal.h"

using namespace forkmesh::ui;

// ------------------------------------------------------------------ messages

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
    const bool canModerate = m_isAdmin;
    auto *row = new MessageRow(message, senderColor(message.senderName), canModerate);
    if (m_avatars.contains(message.senderId))
        row->setAvatar(m_avatars.value(message.senderId));
    if (m_reactions.contains(message.id))
        row->setReactions(m_reactions.value(message.id));
    connect(row, &MessageRow::reactionToggled, this,
            [this](const QString &messageId, const QString &emoji) {
                if (m_backend)
                    m_backend->sendReaction(m_currentConversation, messageId, emoji);
            });
    connect(row, &MessageRow::editRequested, this, &MainWindow::promptEditMessage);
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
    // Mark unread (and light the chat button) for any incoming message the user
    // isn't actively reading — either a different conversation, or the chat view
    // isn't the focused, on-screen tab. Own messages never mark unread.
    if (!message.self &&
        !(conversation == m_currentConversation && isChatViewVisible())) {
        m_unread.insert(conversation);
        ++m_unreadCounts[conversation];
        refreshChannelList();
        refreshDmList();
    }
    updateChatButton();

    if (!message.self) {
        const QString where = isDirectConversation(conversation)
                                  ? "sent you a message"
                                  : "in " + conversation;
        const QString preview =
            message.hasFile() ? "File: " + message.fileName : message.text;
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
    if (!m_backend || messageId.isEmpty())
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
    if (!m_backend || messageId.isEmpty())
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
    // Repaint the online-members column so its avatar tile picks up the image.
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
    m_channels = channels;
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
            MemberInfo offline = prev;
            offline.online = false;
            newRoster.append(offline);
        }
    }

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
            if (!previouslyOnline.contains(m.id)) {
                const QString displayName =
                    m.name.trimmed().isEmpty() ? m.id : m.name.trimmed();
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
    // brand-new node greets the shared #welcome room — once, ever (issue #192).
    maybeAnnounceWelcome();
    refreshChatMembers();
    // The members list is gone (nodes are the members); keep DM tab titles in
    // sync with renamed/rediscovered nodes.
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

void MainWindow::refreshChatMembers()
{
    // Keep the @-mention candidates in step with the roster (this runs on every
    // roster update), even before the members column itself exists.
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

    // This column is titled "ONLINE": only list nodes that are actually online
    // right now (m_homeRoster also carries stale/offline entries so the Node
    // dropdown keeps them selectable — see setRoster), never a "last seen
    // recently" ghost. ServerNode::flushRosterAndStatus already drops peers
    // that haven't sent a frame in kPeerStaleMs (3 minutes, comfortably inside
    // the 5-minute freshness window this panel promises), so member.online
    // here is never more than a few minutes stale.
    QList<MemberInfo> members;
    // De-duplicate by identity so one person running several nodes (or a node
    // that re-registered under a new key while an old roster entry still
    // heartbeats) shows up once, not four times. Key on the display name
    // (case-insensitive) since that is "the person"; keep our own entry, and
    // prefer the copy that already has a real avatar so the tile isn't a
    // generated letter when a photo is available.
    QHash<QString, int> seenByName; // lowercased name -> index in `members`
    for (const MemberInfo &member : std::as_const(m_homeRoster)) {
        const bool online = member.self ? (m_backend != nullptr) : member.online;
        if (!online)
            continue;
        const QString key = member.self
                                ? QStringLiteral("\x01self")
                                : member.name.trimmed().toLower();
        if (key.isEmpty()) {
            members.append(member);
            continue;
        }
        const auto it = seenByName.constFind(key);
        if (it == seenByName.constEnd()) {
            seenByName.insert(key, members.size());
            members.append(member);
        } else if (m_avatars.value(members.at(*it).id).isNull() &&
                   !m_avatars.value(member.id).isNull()) {
            // Replace the earlier avatar-less duplicate with this richer one.
            members[*it] = member;
        }
    }
    std::sort(members.begin(), members.end(),
              [](const MemberInfo &a, const MemberInfo &b) {
                  return a.name.compare(b.name, Qt::CaseInsensitive) < 0;
              });

    int onlineCount = 0;
    for (const MemberInfo &member : std::as_const(members)) {
        ++onlineCount;

        auto *card = new QWidget;
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

        // Name prefixed with a green status dot; every card here is online.
        auto *nameLabel = new QLabel(
            QString::fromUtf8("<span style='color:#3fb950'>\xE2\x97\x8F</span> %1%2")
                .arg(member.name.toHtmlEscaped(),
                     member.self ? " <span style='color:#8b949e'>(you)</span>"
                                 : QString()));
        nameLabel->setTextFormat(Qt::RichText);
        nameLabel->setToolTip(QStringLiteral("Online"));
        row->addWidget(nameLabel, 1);

        m_chatMembersLayout->insertWidget(m_chatMembersLayout->count() - 1, card);
    }

    if (m_chatMembersHeading)
        m_chatMembersHeading->setText(
            QString::fromUtf8("ONLINE \xE2\x80\x94 %1").arg(onlineCount));
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
    m_messageInput->setPlaceholderText("Message " + title);
    if (m_inviteButton)
        m_inviteButton->setVisible(m_privateChannels.contains(conversation));
    rebuildConversationView();
    refreshTypingLabel();

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

    // Build the list of invitable members: every online node that isn't us,
    // de-duplicated by name so one person's several nodes aren't offered twice.
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

void MainWindow::sendCurrentMessage()
{
    const QString text = m_messageInput->text().trimmed();
    if (text.isEmpty() || !m_backend || m_currentConversation.isEmpty())
        return;
    sendTypingState(false);
    if (isDirectConversation(m_currentConversation))
        m_backend->sendDirect(dmPeerId(m_currentConversation), text);
    else
        m_backend->sendChat(m_currentConversation, text);
    m_messageInput->clear();
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

// Names a message can @-mention: every roster node except ourselves, de-duped
// and sorted so the popup is stable. Self is excluded — you don't ping yourself.
void MainWindow::refreshMentionCandidates()
{
    if (!m_mentionModel)
        return;
    QStringList names;
    for (const MemberInfo &member : std::as_const(m_homeRoster)) {
        if (member.self || member.name.trimmed().isEmpty())
            continue;
        if (!names.contains(member.name))
            names.append(member.name);
    }
    names.sort(Qt::CaseInsensitive);
    m_mentionModel->setStringList(names);
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
    if (!m_backend || m_currentConversation.isEmpty())
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
    if (!m_backend || m_currentConversation.isEmpty())
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
