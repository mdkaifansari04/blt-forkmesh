import 'package:flutter/material.dart';
import 'package:provider/provider.dart';

import '../models/models.dart';
import '../services/api_service.dart';
import '../services/relay_service.dart';
import '../theme.dart';
import '../widgets/fm_ui.dart';

/// Network activity: live network stats and the relay connection log.
class ActivityScreen extends StatefulWidget {
  const ActivityScreen({super.key});

  @override
  State<ActivityScreen> createState() => _ActivityScreenState();
}

class _ActivityScreenState extends State<ActivityScreen> {
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
    final logLines = relay.log.reversed.toList();
    final visibleLogs = _showAllLogs
        ? logLines
        : logLines.take(_initialLogCount).toList();

    return ListView(
      padding: const EdgeInsets.all(FmSpace.x4),
      children: [
        const FmSectionHeader(title: 'Network'),
        const SizedBox(height: FmSpace.x2),
        FutureBuilder<NetworkStats>(
          future: _stats,
          builder: (context, snap) {
            final s = snap.data ?? NetworkStats();
            return Row(
              children: [
                _StatCard(label: 'Nodes online', value: '${s.nodesOnline}'),
                const SizedBox(width: FmSpace.x3),
                _StatCard(label: 'Hosts online', value: '${s.hostsOnline}'),
                const SizedBox(width: FmSpace.x3),
                _StatCard(label: 'Repositories', value: '${s.repos}'),
              ],
            );
          },
        ),
        const SizedBox(height: FmSpace.x5),
        FmSectionHeader(
          title: 'Activity log',
          trailing: Text(
            '${relay.log.length} events',
            style: TextStyle(
              color: FmTheme.textTertiary(context),
              fontSize: 12,
              fontWeight: FontWeight.w700,
            ),
          ),
        ),
        const SizedBox(height: FmSpace.x2),
        FmCard(
          radius: FmRadius.lg,
          color: FmTheme.bgRaised(context),
          child: relay.log.isEmpty
              ? const FmEmptyState(
                  icon: Icons.history,
                  title: 'No activity yet',
                  message:
                      'Connection events and network updates will appear here.',
                )
              : Column(
                  crossAxisAlignment: CrossAxisAlignment.stretch,
                  children: [
                    for (final line in visibleLogs) _ActivityLogRow(line: line),
                    if (relay.log.length > _initialLogCount) ...[
                      const SizedBox(height: FmSpace.x3),
                      Align(
                        alignment: Alignment.centerLeft,
                        child: TextButton.icon(
                          onPressed: () => setState(() {
                            _showAllLogs = !_showAllLogs;
                          }),
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

class _ActivityLogRow extends StatelessWidget {
  const _ActivityLogRow({required this.line});

  final String line;

  @override
  Widget build(BuildContext context) {
    final timestamp = line.length >= 8 ? line.substring(0, 8) : '';
    final body = line.length > 10 ? line.substring(10).trim() : line;
    final lower = body.toLowerCase();
    final icon = lower.contains('failed') || lower.contains('error')
        ? Icons.error_outline
        : lower.contains('connected')
        ? Icons.check_circle_outline
        : lower.contains('left')
        ? Icons.logout
        : Icons.bolt_outlined;
    final accent = lower.contains('failed') || lower.contains('error')
        ? FmTheme.danger(context)
        : lower.contains('connected')
        ? FmTheme.success(context)
        : FmTheme.accent(context);

    return Container(
      margin: const EdgeInsets.only(bottom: FmSpace.x2),
      padding: const EdgeInsets.symmetric(
        horizontal: FmSpace.x3,
        vertical: FmSpace.x2,
      ),
      decoration: BoxDecoration(
        color: FmTheme.bgBase(context),
        borderRadius: BorderRadius.circular(FmRadius.md),
        border: Border.all(color: FmTheme.border(context)),
      ),
      child: Row(
        children: [
          Icon(icon, size: 16, color: accent),
          const SizedBox(width: FmSpace.x2),
          SizedBox(
            width: 64,
            child: Text(
              timestamp,
              style: TextStyle(
                fontFamily: 'monospace',
                fontSize: 11,
                fontWeight: FontWeight.w800,
                color: FmTheme.textTertiary(context),
              ),
            ),
          ),
          const SizedBox(width: FmSpace.x2),
          Expanded(
            child: Text(
              body,
              maxLines: 2,
              overflow: TextOverflow.ellipsis,
              style: TextStyle(
                color: FmTheme.textSecondary(context),
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
    child: FmCard(
      radius: FmRadius.lg,
      padding: const EdgeInsets.all(FmSpace.x3),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          Text(
            value,
            style: TextStyle(
              color: FmTheme.textPrimary(context),
              fontSize: 24,
              fontWeight: FontWeight.w800,
              height: 1,
            ),
          ),
          const SizedBox(height: FmSpace.x2),
          Text(
            label,
            maxLines: 2,
            overflow: TextOverflow.ellipsis,
            style: TextStyle(
              fontSize: 12,
              color: FmTheme.textSecondary(context),
              height: 1.2,
            ),
          ),
        ],
      ),
    ),
  );
}
