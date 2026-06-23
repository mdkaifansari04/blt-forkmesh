import 'package:flutter/material.dart';
import 'package:provider/provider.dart';

import '../models/models.dart';
import '../services/relay_service.dart';
import '../theme.dart';

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

  void _send(RelayService relay) {
    final text = _composer.text;
    if (text.trim().isEmpty) return;
    relay.sendMessage(text);
    _composer.clear();
    WidgetsBinding.instance.addPostFrameCallback((_) {
      if (_scroll.hasClients) {
        _scroll.animateTo(_scroll.position.maxScrollExtent,
            duration: const Duration(milliseconds: 200), curve: Curves.easeOut);
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
          const VerticalDivider(width: 1),
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
    return Container(
      color: FmColors.rail,
      child: ListView(
        padding: const EdgeInsets.symmetric(vertical: 8),
        children: [
          const _SectionLabel('CHANNELS'),
          for (final c in relay.channels)
            ListTile(
              dense: true,
              selected: relay.currentConversation == c,
              selectedTileColor: FmColors.surface,
              title: Text(c, style: const TextStyle(fontSize: 14)),
              trailing: relay.unread.contains(c)
                  ? const Icon(Icons.circle, size: 8, color: FmColors.danger)
                  : null,
              onTap: () => relay.switchConversation(c),
            ),
          const _SectionLabel('NODES'),
          for (final m in roster)
            ListTile(
              dense: true,
              leading: Icon(Icons.circle,
                  size: 10, color: m.online ? FmColors.success : FmColors.offline),
              title: Text(m.self ? '${m.name} (you)' : m.name,
                  style: const TextStyle(fontSize: 14), overflow: TextOverflow.ellipsis),
              subtitle: m.platform.isEmpty ? null : Text(m.platform,
                  style: const TextStyle(fontSize: 11, color: FmColors.textMuted)),
              onTap: m.self ? null : () => relay.switchConversation('@${m.id}'),
            ),
        ],
      ),
    );
  }
}

class _SectionLabel extends StatelessWidget {
  const _SectionLabel(this.text);
  final String text;
  @override
  Widget build(BuildContext context) => Padding(
        padding: const EdgeInsets.fromLTRB(16, 12, 16, 6),
        child: Text(text,
            style: const TextStyle(
                fontSize: 11, fontWeight: FontWeight.w700, color: FmColors.textMuted, letterSpacing: 0.5)),
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
    return Column(
      children: [
        Container(
          padding: const EdgeInsets.all(12),
          alignment: Alignment.centerLeft,
          child: Row(children: [
            const Icon(Icons.tag, size: 18, color: FmColors.textMuted),
            const SizedBox(width: 6),
            Text(_title, style: const TextStyle(fontWeight: FontWeight.w700)),
            const Spacer(),
            const Icon(Icons.lock_outline, size: 14, color: FmColors.success),
            const SizedBox(width: 4),
            const Text('encrypted', style: TextStyle(fontSize: 12, color: FmColors.textMuted)),
          ]),
        ),
        const Divider(height: 1),
        Expanded(
          child: messages.isEmpty
              ? const Center(
                  child: Text('No messages yet.', style: TextStyle(color: FmColors.textMuted)))
              : ListView.builder(
                  controller: scroll,
                  padding: const EdgeInsets.symmetric(vertical: 8),
                  itemCount: messages.length,
                  itemBuilder: (_, i) => _MessageRow(msg: messages[i]),
                ),
        ),
        const Divider(height: 1),
        Padding(
          padding: const EdgeInsets.all(10),
          child: Row(children: [
            Expanded(
              child: TextField(
                controller: composer,
                minLines: 1,
                maxLines: 5,
                decoration: const InputDecoration(hintText: 'Message…'),
                onSubmitted: (_) => onSend(),
              ),
            ),
            const SizedBox(width: 8),
            IconButton.filled(onPressed: onSend, icon: const Icon(Icons.send, size: 18)),
          ]),
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
    final initial = msg.senderName.isEmpty ? '?' : msg.senderName.characters.first.toUpperCase();
    return Padding(
      padding: const EdgeInsets.symmetric(horizontal: 12, vertical: 5),
      child: Row(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          CircleAvatar(
            radius: 14,
            backgroundColor: FmColors.senderColor(msg.senderName),
            child: Text(initial, style: const TextStyle(fontSize: 12, color: Colors.white)),
          ),
          const SizedBox(width: 8),
          Expanded(
            child: Column(
              crossAxisAlignment: CrossAxisAlignment.start,
              children: [
                Row(children: [
                  Text(msg.self ? '${msg.senderName} (you)' : msg.senderName,
                      style: TextStyle(
                          fontWeight: FontWeight.w700,
                          color: FmColors.senderColor(msg.senderName))),
                  const SizedBox(width: 8),
                  Text(time, style: const TextStyle(fontSize: 11, color: FmColors.textMuted)),
                ]),
                if (msg.text.isNotEmpty)
                  Text(msg.text, style: const TextStyle(color: FmColors.text)),
                if (msg.fileName.isNotEmpty)
                  Row(children: [
                    const Icon(Icons.attach_file, size: 14, color: FmColors.textMuted),
                    Text(msg.fileName, style: const TextStyle(color: FmColors.accent)),
                  ]),
              ],
            ),
          ),
        ],
      ),
    );
  }
}
