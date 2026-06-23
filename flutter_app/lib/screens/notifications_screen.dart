import 'package:flutter/material.dart';
import 'package:provider/provider.dart';

import '../models/models.dart';
import '../services/api_service.dart';
import '../services/relay_service.dart';
import '../theme.dart';

/// Network activity: live network stats + the relay connection log (the Qt
/// client's network log dock). Action/agent run notifications would land here.
class NotificationsScreen extends StatefulWidget {
  const NotificationsScreen({super.key});

  @override
  State<NotificationsScreen> createState() => _NotificationsScreenState();
}

class _NotificationsScreenState extends State<NotificationsScreen> {
  late Future<NetworkStats> _stats;

  @override
  void initState() {
    super.initState();
    _stats = context.read<ApiService>().networkStats();
  }

  @override
  Widget build(BuildContext context) {
    final relay = context.watch<RelayService>();
    return ListView(
      padding: const EdgeInsets.all(16),
      children: [
        const Text('Network', style: TextStyle(fontSize: 16, fontWeight: FontWeight.w700)),
        const SizedBox(height: 8),
        FutureBuilder<NetworkStats>(
          future: _stats,
          builder: (context, snap) {
            final s = snap.data ?? NetworkStats();
            return Row(children: [
              _StatCard(label: 'Nodes online', value: '${s.nodesOnline}'),
              const SizedBox(width: 12),
              _StatCard(label: 'Hosts online', value: '${s.hostsOnline}'),
              const SizedBox(width: 12),
              _StatCard(label: 'Repositories', value: '${s.repos}'),
            ]);
          },
        ),
        const SizedBox(height: 20),
        const Text('Activity log', style: TextStyle(fontSize: 16, fontWeight: FontWeight.w700)),
        const SizedBox(height: 8),
        Container(
          decoration: BoxDecoration(
            color: FmColors.surface,
            border: Border.all(color: FmColors.border),
            borderRadius: BorderRadius.circular(8),
          ),
          padding: const EdgeInsets.all(12),
          child: relay.log.isEmpty
              ? const Text('No activity yet.', style: TextStyle(color: FmColors.textMuted))
              : Column(
                  crossAxisAlignment: CrossAxisAlignment.start,
                  children: [
                    for (final line in relay.log.reversed.take(100))
                      Padding(
                        padding: const EdgeInsets.symmetric(vertical: 2),
                        child: Text(line,
                            style: const TextStyle(
                                fontFamily: 'monospace', fontSize: 12, color: FmColors.textMuted)),
                      ),
                  ],
                ),
        ),
      ],
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
          decoration: BoxDecoration(
            color: FmColors.surface,
            border: Border.all(color: FmColors.border),
            borderRadius: BorderRadius.circular(8),
          ),
          child: Column(crossAxisAlignment: CrossAxisAlignment.start, children: [
            Text(value, style: const TextStyle(fontSize: 22, fontWeight: FontWeight.w800)),
            const SizedBox(height: 2),
            Text(label, style: const TextStyle(fontSize: 12, color: FmColors.textMuted)),
          ]),
        ),
      );
}
