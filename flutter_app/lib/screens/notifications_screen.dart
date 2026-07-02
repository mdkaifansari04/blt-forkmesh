import 'package:flutter/material.dart';
import 'package:provider/provider.dart';

import '../models/models.dart';
import '../services/api_service.dart';
import '../services/relay_service.dart';
import '../theme.dart';

/// Network activity: live network stats + unread relay notifications + a
/// bounded activity log. Action/agent run notifications can land here later.
class NotificationsScreen extends StatefulWidget {
  const NotificationsScreen({super.key});

  @override
  State<NotificationsScreen> createState() => _NotificationsScreenState();
}

class _NotificationsScreenState extends State<NotificationsScreen> {
  static const _initialLogCount = 12;

  late Future<NetworkStats> _stats;
  bool _showAllLogs = false;

  @override
  void initState() {
    super.initState();
    _stats = context.read<ApiService>().networkStats();
  }

  @override
  Widget build(BuildContext context) {
    final relay = context.watch<RelayService>();
    final notifications = relay.unreadMessages;
    final logLines = relay.log.reversed.toList();
    final visibleLogs = _showAllLogs
        ? logLines
        : logLines.take(_initialLogCount).toList();

    return ListView(
      padding: const EdgeInsets.all(16),
      children: [
        const Text(
          'Network',
          style: TextStyle(fontSize: 16, fontWeight: FontWeight.w700),
        ),
        const SizedBox(height: 8),
        FutureBuilder<NetworkStats>(
          future: _stats,
          builder: (context, snap) {
            final s = snap.data ?? NetworkStats();
            return Row(
              children: [
                _StatCard(label: 'Nodes online', value: '${s.nodesOnline}'),
                const SizedBox(width: 12),
                _StatCard(label: 'Hosts online', value: '${s.hostsOnline}'),
                const SizedBox(width: 12),
                _StatCard(label: 'Repositories', value: '${s.repos}'),
              ],
            );
          },
        ),
        const SizedBox(height: 20),
        const Text(
          'Notifications',
          style: TextStyle(fontSize: 16, fontWeight: FontWeight.w700),
        ),
        const SizedBox(height: 8),
        Container(
          decoration: _panelDecoration(context),
          padding: const EdgeInsets.all(12),
          child: notifications.isEmpty
              ? const Text(
                  'No new notifications.',
                  style: TextStyle(color: FmColors.textMuted),
                )
              : Column(
                  children: [
                    for (final msg in notifications.take(20))
                      _NotificationTile(message: msg),
                  ],
                ),
        ),
        const SizedBox(height: 20),
        Row(
          children: [
            const Text(
              'Activity log',
              style: TextStyle(fontSize: 16, fontWeight: FontWeight.w700),
            ),
            const Spacer(),
            Text(
              '${relay.log.length} events',
              style: TextStyle(
                color: _mutedText(context),
                fontSize: 12,
                fontWeight: FontWeight.w700,
              ),
            ),
          ],
        ),
        const SizedBox(height: 8),
        Container(
          decoration: _panelDecoration(context),
          padding: const EdgeInsets.all(12),
          child: relay.log.isEmpty
              ? const Text(
                  'No activity yet.',
                  style: TextStyle(color: FmColors.textMuted),
                )
              : Column(
                  crossAxisAlignment: CrossAxisAlignment.stretch,
                  children: [
                    for (final line in visibleLogs) _ActivityLogRow(line: line),
                    if (relay.log.length > _initialLogCount) ...[
                      const SizedBox(height: 8),
                      Align(
                        alignment: Alignment.centerLeft,
                        child: TextButton.icon(
                          onPressed: () =>
                              setState(() => _showAllLogs = !_showAllLogs),
                          icon: Icon(
                            _showAllLogs
                                ? Icons.expand_less
                                : Icons.expand_more,
                            size: 18,
                          ),
                          label: Text(
                            _showAllLogs
                                ? 'Show less'
                                : 'Show all ${relay.log.length} logs',
                          ),
                        ),
                      ),
                    ],
                  ],
                ),
        ),
      ],
    );
  }
}

BoxDecoration _panelDecoration(BuildContext context) => BoxDecoration(
  color: Theme.of(context).cardColor,
  border: Border.all(color: Theme.of(context).dividerColor),
  borderRadius: BorderRadius.circular(12),
);

Color _mutedText(BuildContext context) =>
    Theme.of(context).brightness == Brightness.dark
    ? const Color(0xFFB8C0CC)
    : const Color(0xFF566171);

class _NotificationTile extends StatelessWidget {
  const _NotificationTile({required this.message});

  final ChatMessage message;

  @override
  Widget build(BuildContext context) {
    final ts = message.timestamp;
    final time =
        '${ts.hour.toString().padLeft(2, '0')}:${ts.minute.toString().padLeft(2, '0')}';
    final preview = message.text.isEmpty ? 'Sent an attachment' : message.text;
    return Container(
      margin: const EdgeInsets.only(bottom: 8),
      padding: const EdgeInsets.all(12),
      decoration: BoxDecoration(
        color: Theme.of(context).colorScheme.secondary.withOpacity(0.08),
        border: Border.all(
          color: Theme.of(context).colorScheme.secondary.withOpacity(0.18),
        ),
        borderRadius: BorderRadius.circular(10),
      ),
      child: Row(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          CircleAvatar(
            radius: 16,
            backgroundColor: Theme.of(context).colorScheme.secondary,
            child: const Icon(
              Icons.chat_bubble_outline,
              size: 16,
              color: Colors.white,
            ),
          ),
          const SizedBox(width: 10),
          Expanded(
            child: Column(
              crossAxisAlignment: CrossAxisAlignment.start,
              children: [
                Text(
                  '${message.senderName} in ${message.conversation}',
                  maxLines: 1,
                  overflow: TextOverflow.ellipsis,
                  style: const TextStyle(
                    fontWeight: FontWeight.w800,
                    fontSize: 13,
                  ),
                ),
                const SizedBox(height: 3),
                Text(
                  preview,
                  maxLines: 2,
                  overflow: TextOverflow.ellipsis,
                  style: TextStyle(
                    color: _mutedText(context),
                    fontSize: 12,
                    height: 1.3,
                  ),
                ),
              ],
            ),
          ),
          const SizedBox(width: 8),
          Text(
            time,
            style: TextStyle(
              color: _mutedText(context),
              fontSize: 11,
              fontWeight: FontWeight.w700,
            ),
          ),
        ],
      ),
    );
  }
}

class _ActivityLogRow extends StatelessWidget {
  const _ActivityLogRow({required this.line});

  final String line;

  @override
  Widget build(BuildContext context) {
    final timestamp = line.length >= 8 ? line.substring(0, 8) : '';
    final body = line.length > 10 ? line.substring(10).trim() : line;
    final lower = body.toLowerCase();
    final isError = lower.contains('failed') || lower.contains('error');
    final isConnected = lower.contains('connected');
    final icon = isError
        ? Icons.error_outline
        : isConnected
        ? Icons.check_circle_outline
        : lower.contains('left')
        ? Icons.logout
        : Icons.bolt_outlined;
    final accent = isError
        ? FmColors.danger
        : isConnected
        ? FmColors.success
        : Theme.of(context).colorScheme.secondary;

    return Container(
      margin: const EdgeInsets.only(bottom: 8),
      padding: const EdgeInsets.symmetric(horizontal: 10, vertical: 9),
      decoration: BoxDecoration(
        color: Theme.of(context).scaffoldBackgroundColor,
        border: Border.all(color: Theme.of(context).dividerColor),
        borderRadius: BorderRadius.circular(10),
      ),
      child: Row(
        children: [
          Icon(icon, size: 16, color: accent),
          const SizedBox(width: 8),
          SizedBox(
            width: 64,
            child: Text(
              timestamp,
              style: TextStyle(
                fontFamily: 'monospace',
                fontSize: 11,
                fontWeight: FontWeight.w800,
                color: _mutedText(context),
              ),
            ),
          ),
          const SizedBox(width: 8),
          Expanded(
            child: Text(
              body,
              maxLines: 2,
              overflow: TextOverflow.ellipsis,
              style: TextStyle(
                color: Theme.of(context).textTheme.bodyMedium?.color,
                fontSize: 12,
                height: 1.35,
              ),
            ),
          ),
        ],
      ),
    );
  }
}

class _StatCard extends StatelessWidget {
  const _StatCard({required this.label, required this.value});
  final String label;
  final String value;
  @override
  Widget build(BuildContext context) => Expanded(
    child: Container(
      padding: const EdgeInsets.all(14),
      decoration: _panelDecoration(context),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          Text(
            value,
            style: const TextStyle(fontSize: 22, fontWeight: FontWeight.w800),
          ),
          const SizedBox(height: 2),
          Text(
            label,
            style: TextStyle(fontSize: 12, color: _mutedText(context)),
          ),
        ],
      ),
    ),
  );
}
