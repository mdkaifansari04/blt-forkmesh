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
  late Future<NetworkLeaderboards> _leaderboards;
  bool _showAllLogs = false;

  @override
  void initState() {
    super.initState();
    final api = context.read<ApiService>();
    _stats = api.networkStats();
    _leaderboards = api.networkLeaderboards();
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
        FutureBuilder<NetworkStats>(
          future: _stats,
          builder: (context, snap) => _PayoutReadinessSection(
            nodes: snap.data?.payoutNodes ?? const [],
          ),
        ),
        const SizedBox(height: FmSpace.x5),
        FutureBuilder<NetworkLeaderboards>(
          future: _leaderboards,
          builder: (context, snap) => _FundsReceivedSection(
            boards: snap.data ?? const NetworkLeaderboards(),
          ),
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

class _PayoutReadinessSection extends StatelessWidget {
  const _PayoutReadinessSection({required this.nodes});

  final List<PayoutNode> nodes;

  @override
  Widget build(BuildContext context) {
    final visibleNodes = nodes.take(5).toList();
    return Column(
      crossAxisAlignment: CrossAxisAlignment.stretch,
      children: [
        const FmSectionHeader(title: 'Payout readiness'),
        const SizedBox(height: FmSpace.x2),
        FmCard(
          radius: FmRadius.lg,
          color: FmTheme.bgRaised(context),
          child: Column(
            crossAxisAlignment: CrossAxisAlignment.stretch,
            children: [
              Text(
                'Public readiness only. Non-custodial: these addresses and balances are public-chain observations; ForkMesh does not hold node funds, store wallet keys, or execute payouts from mobile. Eligibility never guarantees selection or payment.',
                style: TextStyle(
                  color: FmTheme.textSecondary(context),
                  fontSize: 12,
                  height: 1.35,
                ),
              ),
              const SizedBox(height: FmSpace.x3),
              if (visibleNodes.isEmpty)
                const FmEmptyState(
                  icon: Icons.account_balance_wallet_outlined,
                  title: 'No payout-ready nodes reported',
                  message:
                      'Payout-eligible nodes appear here when the mainnode reports public payout readiness.',
                )
              else ...[
                for (final node in visibleNodes) _PayoutNodeRow(node: node),
                if (nodes.length > visibleNodes.length)
                  Padding(
                    padding: const EdgeInsets.only(top: FmSpace.x2),
                    child: Text(
                      '+${nodes.length - visibleNodes.length} more nodes',
                      style: TextStyle(
                        color: FmTheme.textTertiary(context),
                        fontSize: 12,
                        fontWeight: FontWeight.w700,
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

class _PayoutNodeRow extends StatelessWidget {
  const _PayoutNodeRow({required this.node});

  final PayoutNode node;

  @override
  Widget build(BuildContext context) {
    final details = <String>[
      node.eligibilityLabel,
      if (node.shortWallet.isNotEmpty) node.shortWallet,
      if (node.balanceLamports > 0 || node.balanceSol > 0) node.balanceLabel,
      if (node.relay.trim().isNotEmpty) node.relay.trim(),
    ];

    return Container(
      margin: const EdgeInsets.only(bottom: FmSpace.x2),
      padding: const EdgeInsets.all(FmSpace.x3),
      decoration: BoxDecoration(
        color: FmTheme.bgBase(context),
        borderRadius: BorderRadius.circular(FmRadius.md),
        border: Border.all(color: FmTheme.border(context)),
      ),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.stretch,
        children: [
          Row(
            crossAxisAlignment: CrossAxisAlignment.start,
            children: [
              Expanded(
                child: Text(
                  node.name.isEmpty ? 'Unnamed node' : node.name,
                  maxLines: 1,
                  overflow: TextOverflow.ellipsis,
                  style: TextStyle(
                    color: FmTheme.textPrimary(context),
                    fontSize: 14,
                    fontWeight: FontWeight.w800,
                  ),
                ),
              ),
              const SizedBox(width: FmSpace.x2),
              _MiniBadge(
                label: node.online ? 'online' : 'offline',
                color: node.online
                    ? FmTheme.success(context)
                    : FmTheme.textTertiary(context),
              ),
              const SizedBox(width: FmSpace.x1),
              _MiniBadge(
                label: node.payoutEligible ? 'eligible' : 'not eligible',
                color: node.payoutEligible
                    ? FmTheme.success(context)
                    : FmTheme.warning(context),
              ),
            ],
          ),
          if (details.isNotEmpty) ...[
            const SizedBox(height: FmSpace.x2),
            Wrap(
              spacing: FmSpace.x2,
              runSpacing: FmSpace.x1,
              children: [
                for (final detail in details) _DetailToken(label: detail),
              ],
            ),
          ],
        ],
      ),
    );
  }
}

class _DetailToken extends StatelessWidget {
  const _DetailToken({required this.label});

  final String label;

  @override
  Widget build(BuildContext context) => Container(
    padding: const EdgeInsets.symmetric(
      horizontal: FmSpace.x2,
      vertical: FmSpace.x1,
    ),
    decoration: BoxDecoration(
      color: FmTheme.bgRaised(context),
      borderRadius: BorderRadius.circular(FmRadius.sm),
      border: Border.all(color: FmTheme.border(context)),
    ),
    child: Text(
      label,
      style: TextStyle(
        color: FmTheme.textSecondary(context),
        fontSize: 11,
        fontWeight: FontWeight.w700,
        height: 1.1,
      ),
    ),
  );
}

class _FundsReceivedSection extends StatelessWidget {
  const _FundsReceivedSection({required this.boards});

  final NetworkLeaderboards boards;

  @override
  Widget build(BuildContext context) => Column(
    crossAxisAlignment: CrossAxisAlignment.stretch,
    children: [
      const FmSectionHeader(title: 'Funds received'),
      const SizedBox(height: FmSpace.x2),
      FmCard(
        radius: FmRadius.lg,
        color: FmTheme.bgRaised(context),
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.stretch,
          children: [
            Text(
              'Historical finalized public accounting only, not a ForkMesh or current wallet balance. User-owned funds, community-pool funds, pending allocations, and completed on-chain transfers are distinct states.',
              style: TextStyle(
                color: FmTheme.textSecondary(context),
                fontSize: 12,
                height: 1.35,
              ),
            ),
            const SizedBox(height: FmSpace.x3),
            if (boards.isEmpty)
              const FmEmptyState(
                icon: Icons.leaderboard_outlined,
                title: 'No funds received yet',
                message:
                    'Public funds-received totals appear here after the mainnode reports them.',
              )
            else ...[
              _FundsGroup(title: 'Mainnodes', entries: boards.fundsMainnodes),
              _FundsGroup(
                title: 'Contributors',
                entries: boards.fundsContributors,
              ),
              _FundsGroup(title: 'Projects', entries: boards.fundsProjects),
            ],
          ],
        ),
      ),
    ],
  );
}

class _FundsGroup extends StatelessWidget {
  const _FundsGroup({required this.title, required this.entries});

  final String title;
  final List<FundsReceivedEntry> entries;

  @override
  Widget build(BuildContext context) {
    final visibleEntries = entries.take(3).toList();
    if (visibleEntries.isEmpty) return const SizedBox.shrink();

    return Padding(
      padding: const EdgeInsets.only(bottom: FmSpace.x3),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.stretch,
        children: [
          Text(
            title,
            style: TextStyle(
              color: FmTheme.textTertiary(context),
              fontSize: 11,
              fontWeight: FontWeight.w800,
              letterSpacing: 0.5,
            ),
          ),
          const SizedBox(height: FmSpace.x2),
          for (final entry in visibleEntries)
            _FundsRow(
              name: entry.name.isEmpty ? 'Unnamed' : entry.name,
              amount: entry.amountLabel,
            ),
        ],
      ),
    );
  }
}

class _FundsRow extends StatelessWidget {
  const _FundsRow({required this.name, required this.amount});

  final String name;
  final String amount;

  @override
  Widget build(BuildContext context) => Padding(
    padding: const EdgeInsets.only(bottom: FmSpace.x1),
    child: Row(
      children: [
        Expanded(
          child: Text(
            name,
            maxLines: 1,
            overflow: TextOverflow.ellipsis,
            style: TextStyle(
              color: FmTheme.textPrimary(context),
              fontSize: 13,
              fontWeight: FontWeight.w700,
            ),
          ),
        ),
        const SizedBox(width: FmSpace.x3),
        Flexible(
          child: Text(
            amount,
            maxLines: 1,
            overflow: TextOverflow.ellipsis,
            textAlign: TextAlign.right,
            style: TextStyle(
              color: FmTheme.textSecondary(context),
              fontSize: 12,
              fontWeight: FontWeight.w700,
            ),
          ),
        ),
      ],
    ),
  );
}

class _MiniBadge extends StatelessWidget {
  const _MiniBadge({required this.label, required this.color});

  final String label;
  final Color color;

  @override
  Widget build(BuildContext context) => Container(
    padding: const EdgeInsets.symmetric(
      horizontal: FmSpace.x2,
      vertical: FmSpace.x1,
    ),
    decoration: BoxDecoration(
      color: color.withAlpha(31),
      borderRadius: BorderRadius.circular(FmRadius.full),
    ),
    child: Text(
      label,
      maxLines: 1,
      overflow: TextOverflow.ellipsis,
      style: TextStyle(
        color: color,
        fontSize: 10,
        fontWeight: FontWeight.w800,
        height: 1,
      ),
    ),
  );
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
