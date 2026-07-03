import 'package:flutter/material.dart';
import 'package:provider/provider.dart';

import '../models/models.dart';
import '../services/relay_service.dart';
import '../theme.dart';
import '../widgets/fm_ui.dart';

/// Encrypted chat against the live relay room: channel list, member roster, and
/// the message transcript with a composer. Interoperates with Qt-client nodes.
class ChatScreen extends StatefulWidget {
  const ChatScreen({super.key});

  @override
  State<ChatScreen> createState() => _ChatScreenState();
}

class _ChatScreenState extends State<ChatScreen> {
  final _composer = TextEditingController();
  final _scroll = ScrollController();

  @override
  void dispose() {
    _composer.dispose();
    _scroll.dispose();
    super.dispose();
  }

  Future<void> _send(RelayService relay) async {
    final text = _composer.text;
    if (text.trim().isEmpty) return;
    final sent = await relay.sendMessage(text);
    if (!mounted) return;
    if (!sent) {
      ScaffoldMessenger.of(context).showSnackBar(
        const SnackBar(content: Text('Chat is offline. Reconnecting...')),
      );
      relay.connect();
      return;
    }
    _composer.clear();
    WidgetsBinding.instance.addPostFrameCallback((_) {
      if (_scroll.hasClients) {
        _scroll.animateTo(
          _scroll.position.maxScrollExtent,
          duration: const Duration(milliseconds: 200),
          curve: Curves.easeOut,
        );
      }
    });
  }

  @override
  Widget build(BuildContext context) {
    final relay = context.watch<RelayService>();
    final wide = MediaQuery.of(context).size.width >= 720;
    final messages = relay.messages(relay.currentConversation);
    final roster = relay.roster();

    final channelList = _ChannelList(relay: relay, roster: roster);
    final transcript = _Transcript(
      relay: relay,
      messages: messages,
      scroll: _scroll,
      composer: _composer,
      onSend: () => _send(relay),
    );

    if (wide) {
      return Row(
        children: [
          SizedBox(width: 240, child: channelList),
          Container(width: FmSpace.x2, color: FmTheme.bgBase(context)),
          Expanded(child: transcript),
        ],
      );
    }
    return transcript;
  }
}

class _ChannelList extends StatelessWidget {
  const _ChannelList({required this.relay, required this.roster});
  final RelayService relay;
  final List<Member> roster;

  @override
  Widget build(BuildContext context) {
    return Material(
      color: FmTheme.bgRaised(context),
      child: ListView(
        padding: const EdgeInsets.all(FmSpace.x3),
        children: [
          const FmSectionHeader(title: 'Channels'),
          const SizedBox(height: FmSpace.x2),
          for (final c in relay.channels)
            _ChannelTile(
              title: c,
              icon: Icons.tag,
              selected: relay.currentConversation == c,
              unread: relay.unread.contains(c),
              onTap: () => relay.switchConversation(c),
            ),
          const SizedBox(height: FmSpace.x4),
          const FmSectionHeader(title: 'Nodes'),
          const SizedBox(height: FmSpace.x2),
          for (final m in roster)
            _ChannelTile(
              title: m.self ? '${m.name} (you)' : m.name,
              subtitle: m.platform.isEmpty ? null : m.platform,
              statusColor: m.online ? FmColors.success : FmColors.offline,
              selected: relay.currentConversation == '@${m.id}',
              onTap: m.self ? null : () => relay.switchConversation('@${m.id}'),
            ),
        ],
      ),
    );
  }
}

class _ChannelTile extends StatelessWidget {
  const _ChannelTile({
    required this.title,
    required this.selected,
    this.subtitle,
    this.icon,
    this.statusColor,
    this.unread = false,
    this.onTap,
  });

  final String title;
  final bool selected;
  final String? subtitle;
  final IconData? icon;
  final Color? statusColor;
  final bool unread;
  final VoidCallback? onTap;

  @override
  Widget build(BuildContext context) => Padding(
    padding: const EdgeInsets.only(bottom: FmSpace.x1),
    child: Material(
      color: selected ? FmTheme.accentSubtle(context) : Colors.transparent,
      borderRadius: BorderRadius.circular(FmRadius.md),
      child: InkWell(
        onTap: onTap,
        borderRadius: BorderRadius.circular(FmRadius.md),
        child: ConstrainedBox(
          constraints: const BoxConstraints(minHeight: 44),
          child: Padding(
            padding: const EdgeInsets.symmetric(
              horizontal: FmSpace.x3,
              vertical: FmSpace.x2,
            ),
            child: Row(
              children: [
                if (statusColor != null)
                  Icon(Icons.circle, size: 10, color: statusColor)
                else
                  Icon(
                    icon ?? Icons.circle,
                    size: 17,
                    color: selected
                        ? FmTheme.accent(context)
                        : FmTheme.textTertiary(context),
                  ),
                const SizedBox(width: FmSpace.x3),
                Expanded(
                  child: Column(
                    mainAxisAlignment: MainAxisAlignment.center,
                    crossAxisAlignment: CrossAxisAlignment.start,
                    children: [
                      Text(
                        title,
                        maxLines: 1,
                        overflow: TextOverflow.ellipsis,
                        style: TextStyle(
                          color: selected
                              ? FmTheme.textPrimary(context)
                              : FmTheme.textSecondary(context),
                          fontSize: 14,
                          fontWeight: selected
                              ? FontWeight.w700
                              : FontWeight.w600,
                        ),
                      ),
                      if (subtitle != null) ...[
                        const SizedBox(height: FmSpace.x1),
                        Text(
                          subtitle!,
                          maxLines: 1,
                          overflow: TextOverflow.ellipsis,
                          style: TextStyle(
                            fontSize: 11,
                            color: FmTheme.textTertiary(context),
                          ),
                        ),
                      ],
                    ],
                  ),
                ),
                if (unread) ...[
                  const SizedBox(width: FmSpace.x2),
                  Icon(Icons.circle, size: 8, color: FmTheme.danger(context)),
                ],
              ],
            ),
          ),
        ),
      ),
    ),
  );
}

class _Transcript extends StatelessWidget {
  const _Transcript({
    required this.relay,
    required this.messages,
    required this.scroll,
    required this.composer,
    required this.onSend,
  });

  final RelayService relay;
  final List<ChatMessage> messages;
  final ScrollController scroll;
  final TextEditingController composer;
  final VoidCallback onSend;

  String get _title {
    final c = relay.currentConversation;
    if (c.startsWith('@')) {
      final m = relay.roster().firstWhere(
        (e) => e.id == c.substring(1),
        orElse: () => Member(id: c, name: 'direct message'),
      );
      return '@${m.name}';
    }
    return c;
  }

  @override
  Widget build(BuildContext context) {
    final connected = relay.state == RelayConnectionState.connected;
    return Column(
      children: [
        Material(
          color: FmTheme.bgRaised(context),
          child: FmPanelHeader(
            title: _title,
            leading: Icon(
              _title.startsWith('@') ? Icons.alternate_email : Icons.tag,
              size: 18,
              color: FmTheme.textTertiary(context),
            ),
            trailing: FmStatusBadge(
              label: connected ? 'encrypted' : 'reconnecting',
              color: connected
                  ? FmTheme.success(context)
                  : FmTheme.warning(context),
            ),
          ),
        ),
        Expanded(
          child: ColoredBox(
            color: FmTheme.bgBase(context),
            child: messages.isEmpty
                ? const FmEmptyState(
                    icon: Icons.chat_bubble_outline,
                    title: 'No messages yet',
                    message: 'Encrypted relay messages will appear here.',
                  )
                : ListView.builder(
                    controller: scroll,
                    padding: const EdgeInsets.symmetric(
                      horizontal: FmSpace.x2,
                      vertical: FmSpace.x3,
                    ),
                    itemCount: messages.length,
                    itemBuilder: (_, i) => _MessageRow(msg: messages[i]),
                  ),
          ),
        ),
        FmBottomActionBar(
          padding: const EdgeInsets.all(FmSpace.x3),
          child: Row(
            children: [
              Expanded(
                child: TextField(
                  controller: composer,
                  minLines: 1,
                  maxLines: 5,
                  decoration: InputDecoration(
                    hintText: 'Message...',
                    filled: true,
                    fillColor: FmTheme.bgBase(context),
                    contentPadding: const EdgeInsets.symmetric(
                      horizontal: FmSpace.x4,
                      vertical: FmSpace.x3,
                    ),
                    enabledBorder: OutlineInputBorder(
                      borderRadius: BorderRadius.circular(FmRadius.lg),
                      borderSide: BorderSide.none,
                    ),
                    focusedBorder: OutlineInputBorder(
                      borderRadius: BorderRadius.circular(FmRadius.lg),
                      borderSide: BorderSide(
                        color: FmTheme.accent(context),
                        width: 1.2,
                      ),
                    ),
                  ),
                  onSubmitted: connected ? (_) => onSend() : null,
                ),
              ),
              const SizedBox(width: FmSpace.x2),
              SizedBox(
                width: 48,
                height: 48,
                child: IconButton.filled(
                  onPressed: connected ? onSend : () => relay.connect(),
                  style: IconButton.styleFrom(
                    backgroundColor: FmTheme.accent(context),
                    foregroundColor: Colors.white,
                    disabledBackgroundColor: FmTheme.bgElevated(context),
                    disabledForegroundColor: FmTheme.textTertiary(context),
                    shape: RoundedRectangleBorder(
                      borderRadius: BorderRadius.circular(FmRadius.md),
                    ),
                  ),
                  icon: const Icon(Icons.send, size: 18),
                ),
              ),
            ],
          ),
        ),
      ],
    );
  }
}

class _MessageRow extends StatelessWidget {
  const _MessageRow({required this.msg});
  final ChatMessage msg;

  @override
  Widget build(BuildContext context) {
    final ts = msg.timestamp;
    final time =
        '${ts.hour.toString().padLeft(2, '0')}:${ts.minute.toString().padLeft(2, '0')}';
    final initial = msg.senderName.isEmpty
        ? '?'
        : msg.senderName.characters.first.toUpperCase();
    return Padding(
      padding: const EdgeInsets.symmetric(horizontal: 12, vertical: 5),
      child: Row(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          CircleAvatar(
            radius: 14,
            backgroundColor: FmColors.senderColor(msg.senderName),
            child: Text(
              initial,
              style: const TextStyle(fontSize: 12, color: Colors.white),
            ),
          ),
          const SizedBox(width: 8),
          Expanded(
            child: Column(
              crossAxisAlignment: CrossAxisAlignment.start,
              children: [
                Row(
                  children: [
                    Text(
                      msg.self ? '${msg.senderName} (you)' : msg.senderName,
                      style: TextStyle(
                        fontWeight: FontWeight.w700,
                        color: FmColors.senderColor(msg.senderName),
                      ),
                    ),
                    const SizedBox(width: 8),
                    Text(
                      time,
                      style: TextStyle(
                        fontSize: 11,
                        color: FmTheme.textTertiary(context),
                      ),
                    ),
                  ],
                ),
                if (msg.text.isNotEmpty)
                  Text(
                    msg.text,
                    style: TextStyle(color: FmTheme.textPrimary(context)),
                  ),
                if (msg.fileName.isNotEmpty)
                  Row(
                    children: [
                      Icon(
                        Icons.attach_file,
                        size: 14,
                        color: FmTheme.textTertiary(context),
                      ),
                      Text(
                        msg.fileName,
                        style: TextStyle(color: FmTheme.accent(context)),
                      ),
                    ],
                  ),
              ],
            ),
          ),
        ],
      ),
    );
  }
}
