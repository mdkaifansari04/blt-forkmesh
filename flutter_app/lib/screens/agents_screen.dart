import 'dart:async';

import 'package:flutter/material.dart';
import 'package:provider/provider.dart';

import '../models/models.dart';
import '../services/api_service.dart';
import '../services/auth_service.dart';
import '../theme.dart';
import '../widgets/fm_ui.dart';

const _agentPollInterval = Duration(seconds: 10);

class AgentsScreen extends StatefulWidget {
  const AgentsScreen({super.key, this.enabled = true});

  final bool enabled;

  @override
  State<AgentsScreen> createState() => _AgentsScreenState();
}

class _AgentsScreenState extends State<AgentsScreen> {
  Future<List<_AgentRepoGroup>>? _future;
  Timer? _pollTimer;
  int _loadGeneration = 0;

  @override
  void didChangeDependencies() {
    super.didChangeDependencies();
    if (widget.enabled) _future ??= _startLoad();
  }

  @override
  void didUpdateWidget(covariant AgentsScreen oldWidget) {
    super.didUpdateWidget(oldWidget);
    if (widget.enabled && _future == null) {
      _future = _startLoad();
    }
    if (!widget.enabled && oldWidget.enabled) {
      _loadGeneration += 1;
      _future = null;
      _pollTimer?.cancel();
      _pollTimer = null;
    }
  }

  @override
  void dispose() {
    _pollTimer?.cancel();
    super.dispose();
  }

  Future<void> _refresh() async {
    if (!widget.enabled) return;
    setState(() {
      _future = _startLoad();
    });
    await _future;
  }

  Future<List<_AgentRepoGroup>> _startLoad() {
    final generation = ++_loadGeneration;
    return _loadGroups(generation);
  }

  Future<List<_AgentRepoGroup>> _loadGroups(int generation) async {
    final api = context.read<ApiService>();
    final repos = await api.repositories();
    if (!mounted || !widget.enabled || generation != _loadGeneration) {
      return const [];
    }
    final groups = await Future.wait(
      repos.map((repo) async {
        try {
          final sessions = await api.agentSessions(
            repo.owner,
            repo.name,
            ownerAccount: _ownerAccount(repo),
          );
          final sorted = sessions.toList()..sort(_compareSessions);
          return _AgentRepoGroup(repo: repo, sessions: sorted);
        } catch (_) {
          return _AgentRepoGroup(repo: repo, sessions: const []);
        }
      }),
    );
    if (!mounted || !widget.enabled || generation != _loadGeneration) {
      return const [];
    }
    final visible = groups.where((group) => group.sessions.isNotEmpty).toList()
      ..sort(_compareGroups);
    _syncPolling(visible);
    return visible;
  }

  String _ownerAccount(Repository repo) {
    final auth = context.read<AuthService?>();
    final node = auth?.session?.nodeName.trim().toLowerCase() ?? '';
    return node.isNotEmpty ? node : repo.owner;
  }

  void _syncPolling(List<_AgentRepoGroup> groups) {
    if (!mounted || !widget.enabled) {
      _pollTimer?.cancel();
      _pollTimer = null;
      return;
    }
    final hasActive = groups.any((group) => group.hasActive);
    if (hasActive) {
      _pollTimer ??= Timer.periodic(_agentPollInterval, (_) {
        if (!mounted || !widget.enabled) return;
        setState(() {
          _future = _startLoad();
        });
      });
    } else {
      _pollTimer?.cancel();
      _pollTimer = null;
    }
  }

  @override
  Widget build(BuildContext context) {
    final future = _future;
    if (future == null) {
      return const SizedBox.shrink();
    }
    return FutureBuilder<List<_AgentRepoGroup>>(
      future: future,
      builder: (context, snap) {
        final groups = snap.data ?? const <_AgentRepoGroup>[];
        final sessions = groups.fold<int>(
          0,
          (count, group) => count + group.sessions.length,
        );
        final running = groups.fold<int>(
          0,
          (count, group) =>
              count +
              group.sessions.where((session) => session.isActive).length,
        );
        return Column(
          crossAxisAlignment: CrossAxisAlignment.stretch,
          children: [
            _AgentsHeader(
              running: running,
              sessions: sessions,
              repositories: groups.length,
              polling: _pollTimer != null,
              onRefresh: _refresh,
            ),
            Expanded(
              child: _AgentsBody(
                state: snap.connectionState,
                error: snap.error,
                groups: groups,
                onRefresh: _refresh,
              ),
            ),
          ],
        );
      },
    );
  }
}

class AgentSessionDetailScreen extends StatelessWidget {
  const AgentSessionDetailScreen({
    super.key,
    required this.api,
    required this.repo,
    required this.session,
    required this.ownerAccount,
  });

  final ApiService api;
  final Repository repo;
  final AgentSession session;
  final String ownerAccount;

  @override
  Widget build(BuildContext context) {
    final transcriptFuture = api.agentTranscript(
      repo.owner,
      repo.name,
      session.id,
      ownerAccount: ownerAccount,
    );
    return Scaffold(
      appBar: AppBar(title: Text('Agent session #${session.id}')),
      body: ListView(
        padding: const EdgeInsets.all(FmSpace.x5),
        children: [
          FmCard(
            radius: FmRadius.lg,
            child: Column(
              crossAxisAlignment: CrossAxisAlignment.start,
              children: [
                Text(
                  session.displayTitle,
                  style: TextStyle(
                    color: FmTheme.textPrimary(context),
                    fontSize: 18,
                    fontWeight: FontWeight.w800,
                  ),
                ),
                const SizedBox(height: FmSpace.x3),
                Wrap(
                  spacing: FmSpace.x2,
                  runSpacing: FmSpace.x2,
                  children: [
                    _AgentChip(label: session.providerLabel),
                    _AgentChip(label: session.statusLabel),
                    if (session.model.isNotEmpty)
                      _AgentChip(label: session.model),
                    if (session.issueNumber > 0)
                      _AgentChip(label: '#${session.issueNumber}'),
                    if (session.branchName.isNotEmpty)
                      _AgentChip(label: session.branchName),
                    if (session.numTurns > 0)
                      _AgentChip(label: '${session.numTurns} turns'),
                    if (session.durationLabel.isNotEmpty)
                      _AgentChip(label: session.durationLabel),
                    if (session.costLabel.isNotEmpty)
                      _AgentChip(label: session.costLabel),
                  ],
                ),
                if (session.lastError.isNotEmpty) ...[
                  const SizedBox(height: FmSpace.x3),
                  Text(
                    session.lastError,
                    style: TextStyle(color: FmTheme.danger(context)),
                  ),
                ],
              ],
            ),
          ),
          const SizedBox(height: FmSpace.x3),
          FutureBuilder<AgentTranscript>(
            future: transcriptFuture,
            builder: (context, snap) {
              if (snap.connectionState == ConnectionState.waiting) {
                return const FmCard(
                  radius: FmRadius.lg,
                  child: LinearProgressIndicator(),
                );
              }
              if (snap.hasError) {
                return FmCard(
                  radius: FmRadius.lg,
                  child: Text(
                    '${snap.error}',
                    style: TextStyle(color: FmTheme.textSecondary(context)),
                  ),
                );
              }
              final detail =
                  snap.data ??
                  const AgentTranscript(status: '', transcript: '');
              final status = detail.status.isNotEmpty
                  ? AgentSession(
                      id: session.id,
                      status: detail.status,
                    ).statusLabel
                  : session.statusLabel;
              return FmCard(
                radius: FmRadius.lg,
                child: Column(
                  crossAxisAlignment: CrossAxisAlignment.start,
                  children: [
                    Text(
                      'Transcript',
                      style: TextStyle(
                        color: FmTheme.textPrimary(context),
                        fontSize: 16,
                        fontWeight: FontWeight.w800,
                      ),
                    ),
                    const SizedBox(height: FmSpace.x3),
                    Wrap(
                      spacing: FmSpace.x2,
                      runSpacing: FmSpace.x2,
                      children: [
                        _AgentChip(label: status),
                        _AgentChip(label: repo.fullName),
                      ],
                    ),
                    const SizedBox(height: FmSpace.x3),
                    SelectableText(
                      detail.transcript.trim().isEmpty
                          ? 'No transcript has been pushed by the desktop node yet.'
                          : detail.transcript.trim(),
                      style: const TextStyle(
                        fontFamily: 'monospace',
                        fontSize: 12,
                        height: 1.45,
                      ),
                    ),
                  ],
                ),
              );
            },
          ),
        ],
      ),
    );
  }
}

class _AgentsHeader extends StatelessWidget {
  const _AgentsHeader({
    required this.running,
    required this.sessions,
    required this.repositories,
    required this.polling,
    required this.onRefresh,
  });

  final int running;
  final int sessions;
  final int repositories;
  final bool polling;
  final Future<void> Function() onRefresh;

  @override
  Widget build(BuildContext context) {
    return Padding(
      padding: const EdgeInsets.all(FmSpace.x4),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          Row(
            children: [
              Expanded(
                child: Column(
                  crossAxisAlignment: CrossAxisAlignment.start,
                  children: [
                    Text(
                      'Agents',
                      style: TextStyle(
                        color: FmTheme.textPrimary(context),
                        fontSize: 24,
                        fontWeight: FontWeight.w800,
                      ),
                    ),
                    const SizedBox(height: FmSpace.x1),
                    Text(
                      'Read-only desktop agent monitoring across repositories.',
                      style: TextStyle(
                        color: FmTheme.textSecondary(context),
                        fontSize: 13,
                      ),
                    ),
                  ],
                ),
              ),
              IconButton(
                tooltip: 'Refresh agents',
                onPressed: onRefresh,
                style: IconButton.styleFrom(
                  backgroundColor: FmTheme.bgRaised(context),
                  foregroundColor: FmTheme.textSecondary(context),
                  shape: RoundedRectangleBorder(
                    borderRadius: BorderRadius.circular(FmRadius.lg),
                  ),
                ),
                icon: const Icon(Icons.refresh),
              ),
            ],
          ),
          const SizedBox(height: FmSpace.x3),
          Wrap(
            spacing: FmSpace.x2,
            runSpacing: FmSpace.x2,
            children: [
              Text('$running running'),
              Text('$sessions session${sessions == 1 ? '' : 's'}'),
              Text('$repositories repo${repositories == 1 ? '' : 's'}'),
              if (polling) const Text('Live refresh on'),
            ],
          ),
        ],
      ),
    );
  }
}

class _AgentsBody extends StatelessWidget {
  const _AgentsBody({
    required this.state,
    required this.error,
    required this.groups,
    required this.onRefresh,
  });

  final ConnectionState state;
  final Object? error;
  final List<_AgentRepoGroup> groups;
  final Future<void> Function() onRefresh;

  @override
  Widget build(BuildContext context) {
    if (state == ConnectionState.waiting && groups.isEmpty) {
      return const Center(child: CircularProgressIndicator());
    }
    if (error != null && groups.isEmpty) {
      return Center(
        child: Padding(
          padding: const EdgeInsets.all(FmSpace.x5),
          child: FmCard(
            radius: FmRadius.lg,
            child: Column(
              mainAxisSize: MainAxisSize.min,
              children: [
                Text(
                  '$error',
                  style: TextStyle(color: FmTheme.textSecondary(context)),
                ),
                const SizedBox(height: FmSpace.x3),
                OutlinedButton(
                  onPressed: onRefresh,
                  child: const Text('Retry'),
                ),
              ],
            ),
          ),
        ),
      );
    }
    if (groups.isEmpty) {
      return const FmEmptyState(
        icon: Icons.smart_toy_outlined,
        title: 'No agent sessions',
        message:
            'Agent sessions pushed by desktop nodes will appear here by repository.',
      );
    }
    return RefreshIndicator(
      onRefresh: onRefresh,
      child: ListView.builder(
        padding: const EdgeInsets.fromLTRB(
          FmSpace.x4,
          FmSpace.x0,
          FmSpace.x4,
          FmSpace.x5,
        ),
        itemCount: groups.length,
        itemBuilder: (context, index) => _AgentRepoCard(group: groups[index]),
      ),
    );
  }
}

class _AgentRepoCard extends StatelessWidget {
  const _AgentRepoCard({required this.group});

  final _AgentRepoGroup group;

  @override
  Widget build(BuildContext context) {
    final api = context.read<ApiService>();
    final ownerAccount = _ownerAccount(context, group.repo);
    return FmCard(
      radius: FmRadius.lg,
      margin: const EdgeInsets.only(bottom: FmSpace.x3),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          Row(
            children: [
              Icon(Icons.folder_outlined, color: FmTheme.accent(context)),
              const SizedBox(width: FmSpace.x2),
              Expanded(
                child: Text(
                  group.repo.fullName,
                  style: TextStyle(
                    color: FmTheme.textPrimary(context),
                    fontSize: 16,
                    fontWeight: FontWeight.w800,
                  ),
                ),
              ),
              Text(
                '${group.sessions.length}',
                style: TextStyle(
                  color: FmTheme.textTertiary(context),
                  fontWeight: FontWeight.w700,
                ),
              ),
            ],
          ),
          const SizedBox(height: FmSpace.x3),
          for (var i = 0; i < group.sessions.length; i++) ...[
            _AgentSessionRow(
              repo: group.repo,
              session: group.sessions[i],
              api: api,
              ownerAccount: ownerAccount,
            ),
            if (i != group.sessions.length - 1)
              const SizedBox(height: FmSpace.x2),
          ],
        ],
      ),
    );
  }

  String _ownerAccount(BuildContext context, Repository repo) {
    final auth = context.read<AuthService?>();
    final node = auth?.session?.nodeName.trim().toLowerCase() ?? '';
    return node.isNotEmpty ? node : repo.owner;
  }
}

class _AgentSessionRow extends StatelessWidget {
  const _AgentSessionRow({
    required this.repo,
    required this.session,
    required this.api,
    required this.ownerAccount,
  });

  final Repository repo;
  final AgentSession session;
  final ApiService api;
  final String ownerAccount;

  @override
  Widget build(BuildContext context) {
    return Material(
      color: FmTheme.bgOverlay(context),
      borderRadius: BorderRadius.circular(FmRadius.md),
      clipBehavior: Clip.antiAlias,
      child: InkWell(
        onTap: () => Navigator.of(context).push(
          MaterialPageRoute<void>(
            builder: (_) => AgentSessionDetailScreen(
              api: api,
              repo: repo,
              session: session,
              ownerAccount: ownerAccount,
            ),
          ),
        ),
        child: Padding(
          padding: const EdgeInsets.all(FmSpace.x3),
          child: Row(
            crossAxisAlignment: CrossAxisAlignment.start,
            children: [
              Icon(Icons.smart_toy_outlined, color: FmTheme.accent(context)),
              const SizedBox(width: FmSpace.x3),
              Expanded(
                child: Column(
                  crossAxisAlignment: CrossAxisAlignment.start,
                  children: [
                    Text(
                      session.displayTitle,
                      maxLines: 2,
                      overflow: TextOverflow.ellipsis,
                      style: TextStyle(
                        color: FmTheme.textPrimary(context),
                        fontWeight: FontWeight.w800,
                      ),
                    ),
                    const SizedBox(height: FmSpace.x1),
                    Text(
                      session.issueNumber > 0
                          ? 'Issue #${session.issueNumber} • ${session.providerLabel}'
                          : session.providerLabel,
                      style: TextStyle(
                        color: FmTheme.textSecondary(context),
                        fontSize: 12,
                      ),
                    ),
                    const SizedBox(height: FmSpace.x2),
                    Wrap(
                      spacing: FmSpace.x2,
                      runSpacing: FmSpace.x2,
                      children: [
                        if (session.branchName.isNotEmpty)
                          _AgentChip(label: session.branchName),
                        if (session.numTurns > 0)
                          _AgentChip(label: '${session.numTurns} turns'),
                        if (session.durationLabel.isNotEmpty)
                          _AgentChip(label: session.durationLabel),
                        if (session.costLabel.isNotEmpty)
                          _AgentChip(label: session.costLabel),
                      ],
                    ),
                  ],
                ),
              ),
              const SizedBox(width: FmSpace.x2),
              _AgentStatusBadge(session: session),
            ],
          ),
        ),
      ),
    );
  }
}

class _AgentRepoGroup {
  const _AgentRepoGroup({required this.repo, required this.sessions});

  final Repository repo;
  final List<AgentSession> sessions;

  bool get hasActive => sessions.any((session) => session.isActive);
}

class _AgentStatusBadge extends StatelessWidget {
  const _AgentStatusBadge({required this.session});

  final AgentSession session;

  @override
  Widget build(BuildContext context) {
    final color = switch (session.statusLabel) {
      'Done' => FmTheme.success(context),
      'Failed' || 'Stopped' => FmTheme.danger(context),
      'Running' || 'Queued' || 'Waiting' => FmTheme.accent(context),
      _ => FmTheme.textTertiary(context),
    };
    return FmStatusBadge(label: session.statusLabel, color: color);
  }
}

class _AgentChip extends StatelessWidget {
  const _AgentChip({required this.label});

  final String label;

  @override
  Widget build(BuildContext context) =>
      FmStatusBadge(label: label, color: _agentBadgeColor(context, label));
}

Color _agentBadgeColor(BuildContext context, String label) {
  final normalized = label.toLowerCase();
  if (normalized.contains('running') ||
      normalized.contains('queued') ||
      normalized.contains('waiting')) {
    return FmTheme.accent(context);
  }
  if (normalized.contains('done') || normalized.contains('completed')) {
    return FmTheme.success(context);
  }
  if (normalized.contains('failed') || normalized.contains('stopped')) {
    return FmTheme.danger(context);
  }
  return FmTheme.textSecondary(context);
}

int _compareGroups(_AgentRepoGroup a, _AgentRepoGroup b) {
  if (a.hasActive != b.hasActive) return a.hasActive ? -1 : 1;
  return a.repo.fullName.toLowerCase().compareTo(b.repo.fullName.toLowerCase());
}

int _compareSessions(AgentSession a, AgentSession b) {
  if (a.isActive != b.isActive) return a.isActive ? -1 : 1;
  return b.id.compareTo(a.id);
}
