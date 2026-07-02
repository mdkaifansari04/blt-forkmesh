import 'package:flutter/material.dart';
import 'package:provider/provider.dart';

import '../models/models.dart';
import '../services/api_service.dart';
import '../services/inbox_service.dart';
import '../theme.dart';

/// Repo detail with the GitHub-style tabs the Qt client has: About/Code,
/// Commits, Issues, Pull requests. Reads over the worker REST API; writes
/// (new issue/PR, comment, vote, review, status) go to the signed relay inbox.
class RepoDetailScreen extends StatefulWidget {
  const RepoDetailScreen({super.key, required this.repo});
  final Repository repo;

  @override
  State<RepoDetailScreen> createState() => _RepoDetailScreenState();
}

class _RepoDetailScreenState extends State<RepoDetailScreen>
    with SingleTickerProviderStateMixin {
  late final TabController _tabs = TabController(length: 7, vsync: this)
    ..addListener(() => setState(() {}));

  @override
  void dispose() {
    _tabs.dispose();
    super.dispose();
  }

  Repository get repo => widget.repo;

  Widget? _fab() {
    if (_tabs.index == 1) {
      return FloatingActionButton.extended(
        onPressed: () => showNewIssueDialog(context, repo),
        icon: const Icon(Icons.add),
        label: const Text('New issue'),
      );
    }
    if (_tabs.index == 2) {
      return FloatingActionButton.extended(
        onPressed: () => showNewPullDialog(context, repo),
        icon: const Icon(Icons.add),
        label: const Text('New PR'),
      );
    }
    return null;
  }

  @override
  Widget build(BuildContext context) {
    final api = context.read<ApiService>();
    return Scaffold(
      appBar: AppBar(
        title: Text(repo.fullName, style: const TextStyle(fontSize: 16)),
        bottom: TabBar(
          controller: _tabs,
          isScrollable: true,
          indicatorColor: FmColors.text,
          labelColor: FmColors.text,
          unselectedLabelColor: FmColors.textMuted,
          tabs: const [
            Tab(text: 'Code'),
            Tab(text: 'Issues'),
            Tab(text: 'Pulls'),
            Tab(text: 'Discussions'),
            Tab(text: 'Commits'),
            Tab(text: 'Mirrors'),
            Tab(text: 'About'),
          ],
        ),
      ),
      floatingActionButton: _fab(),
      body: TabBarView(
        controller: _tabs,
        children: [
          _CodeTab(api: api, repo: repo),
          _IssuesTab(api: api, repo: repo),
          _PullsTab(api: api, repo: repo),
          _DiscussionsTab(api: api, repo: repo),
          _CommitsTab(api: api, repo: repo),
          _MirrorsTab(api: api, repo: repo),
          _AboutTab(repo: repo),
        ],
      ),
    );
  }
}

// ---------------------------------------------------------------------------
// Write-path dialogs and sheets (all submit to the signed inbox).
// ---------------------------------------------------------------------------

Future<void> _run(
  BuildContext context,
  Future<void> Function() action,
  String okMessage,
) async {
  final messenger = ScaffoldMessenger.of(context);
  try {
    await action();
    messenger.showSnackBar(SnackBar(content: Text(okMessage)));
  } catch (e) {
    messenger.showSnackBar(
      SnackBar(backgroundColor: FmColors.danger, content: Text('Failed: $e')),
    );
  }
}

const _pendingNote =
    'Submitted to the inbox — pending the repo owner applying it.';

Future<void> showNewIssueDialog(BuildContext context, Repository repo) async {
  final inbox = context.read<InboxService>();
  final title = TextEditingController();
  final body = TextEditingController();
  final ok = await showDialog<bool>(
    context: context,
    builder: (ctx) => AlertDialog(
      backgroundColor: FmColors.surface,
      title: const Text('New issue'),
      content: SizedBox(
        width: 460,
        child: Column(
          mainAxisSize: MainAxisSize.min,
          children: [
            TextField(
              controller: title,
              decoration: const InputDecoration(labelText: 'Title'),
            ),
            const SizedBox(height: 12),
            TextField(
              controller: body,
              minLines: 4,
              maxLines: 10,
              decoration: const InputDecoration(
                labelText: 'Description',
                alignLabelWithHint: true,
              ),
            ),
          ],
        ),
      ),
      actions: [
        TextButton(
          onPressed: () => Navigator.pop(ctx, false),
          child: const Text('Cancel'),
        ),
        FilledButton(
          onPressed: () => Navigator.pop(ctx, true),
          child: const Text('Submit'),
        ),
      ],
    ),
  );
  if (ok != true || title.text.trim().isEmpty) return;
  if (!context.mounted) return;
  await _run(
    context,
    () => inbox.submitNewIssue(
      repo.owner,
      repo.name,
      title: title.text.trim(),
      body: body.text.trim(),
    ),
    _pendingNote,
  );
}

Future<void> showIssueActions(
  BuildContext context,
  Repository repo,
  Issue issue,
) async {
  final inbox = context.read<InboxService>();
  await showModalBottomSheet<void>(
    context: context,
    backgroundColor: FmColors.surface,
    showDragHandle: true,
    builder: (ctx) => SafeArea(
      child: Column(
        mainAxisSize: MainAxisSize.min,
        children: [
          ListTile(
            title: Text(
              '#${issue.number} · ${issue.title}',
              style: const TextStyle(fontWeight: FontWeight.w700),
            ),
            subtitle: issue.body.isEmpty
                ? null
                : Text(
                    issue.body,
                    maxLines: 4,
                    overflow: TextOverflow.ellipsis,
                  ),
          ),
          const Divider(height: 1),
          ListTile(
            leading: const Icon(Icons.how_to_vote_outlined),
            title: const Text('Vote'),
            onTap: () {
              Navigator.pop(ctx);
              _run(
                context,
                () => inbox.voteOnIssue(repo.owner, repo.name, issue.number),
                'Vote submitted to the inbox.',
              );
            },
          ),
          ListTile(
            leading: const Icon(Icons.chat_bubble_outline),
            title: const Text('Comment'),
            onTap: () async {
              Navigator.pop(ctx);
              final body = await _promptText(
                context,
                'Comment on #${issue.number}',
                multiline: true,
              );
              if (body == null || body.trim().isEmpty || !context.mounted) {
                return;
              }
              await _run(
                context,
                () => inbox.commentOnIssue(
                  repo.owner,
                  repo.name,
                  issue.number,
                  body.trim(),
                ),
                _pendingNote,
              );
            },
          ),
          ListTile(
            leading: Icon(
              issue.isOpen ? Icons.check_circle_outline : Icons.refresh,
            ),
            title: Text(issue.isOpen ? 'Close issue' : 'Reopen issue'),
            onTap: () {
              Navigator.pop(ctx);
              _run(
                context,
                () => inbox.setIssueStatus(
                  repo.owner,
                  repo.name,
                  issue.number,
                  issue.isOpen ? 'closed' : 'open',
                ),
                _pendingNote,
              );
            },
          ),
        ],
      ),
    ),
  );
}

Future<void> showNewPullDialog(BuildContext context, Repository repo) async {
  final inbox = context.read<InboxService>();
  final title = TextEditingController();
  final base = TextEditingController(text: repo.defaultBranch);
  final head = TextEditingController();
  final patch = TextEditingController();
  final ok = await showDialog<bool>(
    context: context,
    builder: (ctx) => AlertDialog(
      backgroundColor: FmColors.surface,
      title: const Text('New pull request'),
      content: SizedBox(
        width: 520,
        child: Column(
          mainAxisSize: MainAxisSize.min,
          children: [
            TextField(
              controller: title,
              decoration: const InputDecoration(labelText: 'Title'),
            ),
            const SizedBox(height: 10),
            Row(
              children: [
                Expanded(
                  child: TextField(
                    controller: base,
                    decoration: const InputDecoration(labelText: 'Base branch'),
                  ),
                ),
                const SizedBox(width: 10),
                Expanded(
                  child: TextField(
                    controller: head,
                    decoration: const InputDecoration(labelText: 'Head branch'),
                  ),
                ),
              ],
            ),
            const SizedBox(height: 10),
            TextField(
              controller: patch,
              minLines: 5,
              maxLines: 14,
              style: const TextStyle(fontFamily: 'monospace', fontSize: 12),
              decoration: const InputDecoration(
                labelText: 'Unified diff (patch)',
                alignLabelWithHint: true,
              ),
            ),
          ],
        ),
      ),
      actions: [
        TextButton(
          onPressed: () => Navigator.pop(ctx, false),
          child: const Text('Cancel'),
        ),
        FilledButton(
          onPressed: () => Navigator.pop(ctx, true),
          child: const Text('Submit'),
        ),
      ],
    ),
  );
  if (ok != true || title.text.trim().isEmpty || patch.text.trim().isEmpty) {
    return;
  }
  if (!context.mounted) return;
  await _run(
    context,
    () => inbox.submitNewPull(
      repo.owner,
      repo.name,
      title: title.text.trim(),
      base: base.text.trim(),
      head: head.text.trim(),
      patch: patch.text,
    ),
    _pendingNote,
  );
}

Future<void> showPullActions(
  BuildContext context,
  Repository repo,
  PullRequest pr,
) async {
  final inbox = context.read<InboxService>();
  await showModalBottomSheet<void>(
    context: context,
    backgroundColor: FmColors.surface,
    showDragHandle: true,
    builder: (ctx) => SafeArea(
      child: Column(
        mainAxisSize: MainAxisSize.min,
        children: [
          ListTile(
            title: Text(
              '#${pr.number} · ${pr.title}',
              style: const TextStyle(fontWeight: FontWeight.w700),
            ),
            subtitle: Text(
              '${pr.headBranch} → ${pr.baseBranch} · ${pr.status}',
            ),
          ),
          const Divider(height: 1),
          ListTile(
            leading: const Icon(Icons.chat_bubble_outline),
            title: const Text('Comment'),
            onTap: () async {
              Navigator.pop(ctx);
              final body = await _promptText(
                context,
                'Comment on PR #${pr.number}',
                multiline: true,
              );
              if (body == null || body.trim().isEmpty || !context.mounted) {
                return;
              }
              await _run(
                context,
                () => inbox.commentOnPull(
                  repo.owner,
                  repo.name,
                  pr.number,
                  body.trim(),
                ),
                _pendingNote,
              );
            },
          ),
          ListTile(
            leading: const Icon(
              Icons.check_circle_outline,
              color: FmColors.success,
            ),
            title: const Text('Approve'),
            onTap: () => _review(context, ctx, inbox, repo, pr, 'approve'),
          ),
          ListTile(
            leading: const Icon(Icons.cancel_outlined, color: FmColors.danger),
            title: const Text('Request changes'),
            onTap: () =>
                _review(context, ctx, inbox, repo, pr, 'request-changes'),
          ),
        ],
      ),
    ),
  );
}

Future<void> _review(
  BuildContext context,
  BuildContext sheetCtx,
  InboxService inbox,
  Repository repo,
  PullRequest pr,
  String state,
) async {
  Navigator.pop(sheetCtx);
  final body = await _promptText(
    context,
    'Review note (optional)',
    multiline: true,
  );
  if (!context.mounted) return;
  await _run(
    context,
    () => inbox.reviewPull(
      repo.owner,
      repo.name,
      pr.number,
      state,
      body?.trim() ?? '',
    ),
    _pendingNote,
  );
}

Future<String?> _promptText(
  BuildContext context,
  String title, {
  bool multiline = false,
}) {
  final c = TextEditingController();
  return showDialog<String>(
    context: context,
    builder: (ctx) => AlertDialog(
      backgroundColor: FmColors.surface,
      title: Text(title),
      content: SizedBox(
        width: 460,
        child: TextField(
          controller: c,
          autofocus: true,
          minLines: multiline ? 3 : 1,
          maxLines: multiline ? 8 : 1,
        ),
      ),
      actions: [
        TextButton(
          onPressed: () => Navigator.pop(ctx),
          child: const Text('Cancel'),
        ),
        FilledButton(
          onPressed: () => Navigator.pop(ctx, c.text),
          child: const Text('Submit'),
        ),
      ],
    ),
  );
}

// ---------------------------------------------------------------------------
// Read-only tabs
// ---------------------------------------------------------------------------

class _AboutTab extends StatelessWidget {
  const _AboutTab({required this.repo});
  final Repository repo;
  @override
  Widget build(BuildContext context) {
    return ListView(
      padding: const EdgeInsets.all(16),
      children: [
        _RepoHeaderCard(repo: repo),
        const SizedBox(height: 14),
        _InfoCard(
          title: 'Repository details',
          children: [
            _kv('Default branch', repo.defaultBranch),
            _kv('Language', repo.language.isEmpty ? '—' : repo.language),
            _kv('Stars', '${repo.stars}'),
            _kv('Forks', '${repo.forks}'),
            _kv('Mirrors', '${repo.mirrors}'),
            _kv('Visibility', repo.isPrivate ? 'Private' : 'Public'),
          ],
        ),
      ],
    );
  }
}

class _CodeTab extends StatefulWidget {
  const _CodeTab({required this.api, required this.repo});
  final ApiService api;
  final Repository repo;

  @override
  State<_CodeTab> createState() => _CodeTabState();
}

class _CodeTabState extends State<_CodeTab> {
  String _path = '';
  late Future<RepoTree> _future = widget.api.tree(
    widget.repo.owner,
    widget.repo.name,
  );

  void _openDir(String path) {
    setState(() {
      _path = path;
      _future = widget.api.tree(
        widget.repo.owner,
        widget.repo.name,
        path: path,
      );
    });
  }

  void _up() {
    if (_path.isEmpty) return;
    final parts = _path.split('/')..removeLast();
    _openDir(parts.join('/'));
  }

  @override
  Widget build(BuildContext context) {
    return FutureBuilder<RepoTree>(
      future: _future,
      builder: (context, snap) {
        final loading = snap.connectionState == ConnectionState.waiting;
        final tree = snap.data;
        return ListView(
          padding: const EdgeInsets.all(16),
          children: [
            _RepoHeaderCard(repo: widget.repo),
            const SizedBox(height: 14),
            _PathBar(path: _path, onUp: _path.isEmpty ? null : _up),
            const SizedBox(height: 10),
            if (loading)
              const _LoadingCard(label: 'Loading files…')
            else if (snap.hasError)
              _ErrorCard(
                message: 'Could not load files: ${snap.error}',
                onRetry: () => _openDir(_path),
              )
            else if (tree == null || tree.entries.isEmpty)
              const _EmptyCard(message: 'No files found in this folder.')
            else
              _FileList(
                entries: tree.entries,
                onDir: _openDir,
                onFile: (entry) => Navigator.of(context).push(
                  MaterialPageRoute(
                    builder: (_) => RepoFileScreen(
                      api: widget.api,
                      repo: widget.repo,
                      path: entry.path,
                    ),
                  ),
                ),
              ),
          ],
        );
      },
    );
  }
}

class RepoFileScreen extends StatelessWidget {
  const RepoFileScreen({
    super.key,
    required this.api,
    required this.repo,
    required this.path,
  });
  final ApiService api;
  final Repository repo;
  final String path;

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      appBar: AppBar(
        title: Text(path.split('/').last, overflow: TextOverflow.ellipsis),
      ),
      body: FutureBuilder<RepoBlob>(
        future: api.blob(repo.owner, repo.name, path),
        builder: (context, snap) {
          if (snap.connectionState == ConnectionState.waiting) {
            return const Center(child: CircularProgressIndicator());
          }
          if (snap.hasError) {
            return Center(
              child: Padding(
                padding: const EdgeInsets.all(24),
                child: Text('${snap.error}', textAlign: TextAlign.center),
              ),
            );
          }
          final blob = snap.data ?? RepoBlob(path: path, content: '');
          return ListView(
            padding: const EdgeInsets.all(16),
            children: [
              _InfoCard(
                title: path,
                children: [
                  SelectableText(
                    blob.content.isEmpty
                        ? 'Empty file or binary content.'
                        : blob.content,
                    style: const TextStyle(
                      fontFamily: 'monospace',
                      fontSize: 13,
                      height: 1.45,
                    ),
                  ),
                ],
              ),
            ],
          );
        },
      ),
    );
  }
}

class _IssuesTab extends StatelessWidget {
  const _IssuesTab({required this.api, required this.repo});
  final ApiService api;
  final Repository repo;
  @override
  Widget build(BuildContext context) {
    return _AsyncList<Issue>(
      future: api.publishedIssues(repo.owner, repo.name),
      empty: 'No published issues yet.',
      itemBuilder: (issue) => _MobileCard(
        icon: issue.isOpen ? Icons.error_outline : Icons.check_circle_outline,
        iconColor: issue.isOpen ? FmColors.success : FmColors.accentEdge,
        title: issue.title,
        subtitle:
            '#${issue.number}${issue.author.isNotEmpty ? " · ${issue.author}" : ""}',
        badge: issue.status,
        onTap: () => Navigator.of(context).push(
          MaterialPageRoute(
            builder: (_) => _IssueDetailScreen(repo: repo, issue: issue),
          ),
        ),
      ),
    );
  }
}

class _PullsTab extends StatelessWidget {
  const _PullsTab({required this.api, required this.repo});
  final ApiService api;
  final Repository repo;
  @override
  Widget build(BuildContext context) {
    return _AsyncList<PublishedPull>(
      future: api.publishedPulls(repo.owner, repo.name),
      empty: 'No published pull requests yet.',
      itemBuilder: (pr) => _MobileCard(
        icon: Icons.call_merge,
        iconColor: pr.status == 'merged'
            ? FmColors.accentEdge
            : (pr.status == 'closed' ? FmColors.danger : FmColors.success),
        title: pr.title,
        subtitle:
            '#${pr.number}${pr.head.isNotEmpty || pr.base.isNotEmpty ? " · ${pr.head} → ${pr.base}" : ""}',
        badge: pr.signed ? 'signed' : pr.status,
        onTap: () => Navigator.of(context).push(
          MaterialPageRoute(
            builder: (_) => _PullDetailScreen(repo: repo, pull: pr),
          ),
        ),
      ),
    );
  }
}

class _IssueDetailScreen extends StatelessWidget {
  const _IssueDetailScreen({required this.repo, required this.issue});

  final Repository repo;
  final Issue issue;

  @override
  Widget build(BuildContext context) => Scaffold(
    appBar: AppBar(title: Text('#${issue.number}')),
    body: ListView(
      padding: const EdgeInsets.all(20),
      children: [
        _InfoCard(
          title: issue.title,
          children: [
            Wrap(
              spacing: 8,
              runSpacing: 8,
              children: [
                _Chip(icon: Icons.label_outline, label: issue.status),
                if (issue.author.isNotEmpty)
                  _Chip(icon: Icons.person_outline, label: issue.author),
                _Chip(icon: Icons.folder_outlined, label: repo.fullName),
              ],
            ),
            const SizedBox(height: 16),
            SelectableText(
              issue.body.isEmpty ? 'No description provided.' : issue.body,
              style: const TextStyle(height: 1.45),
            ),
          ],
        ),
        const SizedBox(height: 12),
        _InfoCard(
          title: 'Actions',
          children: [
            const Text(
              'Comments, votes, and status changes will route through Worker account/device capabilities.',
            ),
            const SizedBox(height: 12),
            FilledButton.icon(
              onPressed: () => showIssueActions(context, repo, issue),
              icon: const Icon(Icons.add_comment_outlined),
              label: const Text('Open available actions'),
            ),
          ],
        ),
      ],
    ),
  );
}

class _PullDetailScreen extends StatelessWidget {
  const _PullDetailScreen({required this.repo, required this.pull});

  final Repository repo;
  final PublishedPull pull;

  @override
  Widget build(BuildContext context) => Scaffold(
    appBar: AppBar(title: Text('PR #${pull.number}')),
    body: ListView(
      padding: const EdgeInsets.all(20),
      children: [
        _InfoCard(
          title: pull.title,
          children: [
            Wrap(
              spacing: 8,
              runSpacing: 8,
              children: [
                _Chip(icon: Icons.label_outline, label: pull.status),
                if (pull.signed)
                  const _Chip(icon: Icons.verified_outlined, label: 'signed'),
                if (pull.head.isNotEmpty || pull.base.isNotEmpty)
                  _Chip(
                    icon: Icons.account_tree_outlined,
                    label: '${pull.head} → ${pull.base}',
                  ),
              ],
            ),
            const SizedBox(height: 16),
            SelectableText(
              pull.body.isEmpty ? 'No description provided.' : pull.body,
              style: const TextStyle(height: 1.45),
            ),
          ],
        ),
        const SizedBox(height: 12),
        const _InfoCard(
          title: 'Review and merge',
          children: [
            Text(
              'Patch viewing, review comments, status updates, and merge/apply flows will be enabled with Worker device capabilities.',
            ),
          ],
        ),
      ],
    ),
  );
}

class _DiscussionsTab extends StatelessWidget {
  const _DiscussionsTab({required this.api, required this.repo});
  final ApiService api;
  final Repository repo;
  @override
  Widget build(BuildContext context) {
    return _AsyncList<RepoDiscussion>(
      future: api.publishedDiscussions(repo.owner, repo.name),
      empty: 'No discussions have been published yet.',
      itemBuilder: (d) => _MobileCard(
        icon: Icons.forum_outlined,
        iconColor: FmColors.accent,
        title: d.title,
        subtitle: '#${d.number}${d.author.isNotEmpty ? " · ${d.author}" : ""}',
        badge: 'discussion',
      ),
    );
  }
}

class _MirrorsTab extends StatelessWidget {
  const _MirrorsTab({required this.api, required this.repo});
  final ApiService api;
  final Repository repo;
  @override
  Widget build(BuildContext context) {
    return _AsyncList<RepoMirror>(
      future: api.mirrors(repo.owner, repo.name),
      empty: 'No mirrors reported for this repository yet.',
      itemBuilder: (m) => _MobileCard(
        icon: m.online ? Icons.cloud_done_outlined : Icons.cloud_off_outlined,
        iconColor: m.online ? FmColors.success : FmColors.textMuted,
        title: m.label,
        subtitle: m.online ? 'Online mirror' : 'Offline or recently unseen',
        badge: m.online ? 'live' : 'offline',
      ),
    );
  }
}

class _CommitsTab extends StatelessWidget {
  const _CommitsTab({required this.api, required this.repo});
  final ApiService api;
  final Repository repo;
  @override
  Widget build(BuildContext context) {
    return _AsyncList<Map<String, dynamic>>(
      future: api.commits(repo.owner, repo.name),
      empty: 'No commits.',
      itemBuilder: (c) {
        final hash = (c['hash'] ?? c['sha'] ?? '').toString();
        final short = hash.length < 7 ? hash : hash.substring(0, 7);
        return _MobileCard(
          icon: Icons.commit,
          iconColor: FmColors.textMuted,
          title: (c['subject'] ?? c['message'] ?? 'Commit').toString(),
          subtitle: [
            (c['author'] ?? '').toString(),
            short,
          ].where((s) => s.isNotEmpty).join(' · '),
          badge: short,
        );
      },
    );
  }
}

class _RepoHeaderCard extends StatelessWidget {
  const _RepoHeaderCard({required this.repo});
  final Repository repo;
  @override
  Widget build(BuildContext context) => _InfoCard(
    title: repo.fullName,
    children: [
      Text(
        repo.description.isEmpty
            ? 'No description provided.'
            : repo.description,
        style: const TextStyle(color: FmColors.textMuted, height: 1.35),
      ),
      const SizedBox(height: 14),
      Wrap(
        spacing: 8,
        runSpacing: 8,
        children: [
          _Chip(icon: Icons.account_tree_outlined, label: repo.defaultBranch),
          if (repo.language.isNotEmpty)
            _Chip(icon: Icons.circle, label: repo.language),
          _Chip(icon: Icons.star_border, label: '${repo.stars}'),
          _Chip(icon: Icons.call_split, label: '${repo.forks}'),
          _Chip(icon: Icons.dns_outlined, label: '${repo.mirrors} mirrors'),
          _Chip(
            icon: repo.isPrivate ? Icons.lock_outline : Icons.public,
            label: repo.isPrivate ? 'Private' : 'Public',
          ),
        ],
      ),
    ],
  );
}

class _InfoCard extends StatelessWidget {
  const _InfoCard({required this.title, required this.children});
  final String title;
  final List<Widget> children;
  @override
  Widget build(BuildContext context) => Container(
    padding: const EdgeInsets.all(16),
    decoration: BoxDecoration(
      color: FmColors.surface,
      border: Border.all(color: FmColors.border),
      borderRadius: BorderRadius.circular(22),
    ),
    child: Column(
      crossAxisAlignment: CrossAxisAlignment.start,
      children: [
        Text(
          title,
          style: const TextStyle(fontSize: 17, fontWeight: FontWeight.w800),
        ),
        const SizedBox(height: 12),
        ...children,
      ],
    ),
  );
}

class _Chip extends StatelessWidget {
  const _Chip({required this.icon, required this.label});
  final IconData icon;
  final String label;
  @override
  Widget build(BuildContext context) => Container(
    padding: const EdgeInsets.symmetric(horizontal: 10, vertical: 7),
    decoration: BoxDecoration(
      color: FmColors.canvas,
      borderRadius: BorderRadius.circular(99),
      border: Border.all(color: FmColors.borderMuted),
    ),
    child: Row(
      mainAxisSize: MainAxisSize.min,
      children: [
        Icon(icon, size: 14, color: FmColors.textMuted),
        const SizedBox(width: 5),
        Text(
          label,
          style: const TextStyle(fontSize: 12, fontWeight: FontWeight.w700),
        ),
      ],
    ),
  );
}

class _PathBar extends StatelessWidget {
  const _PathBar({required this.path, required this.onUp});
  final String path;
  final VoidCallback? onUp;
  @override
  Widget build(BuildContext context) => Container(
    padding: const EdgeInsets.symmetric(horizontal: 12, vertical: 10),
    decoration: BoxDecoration(
      color: FmColors.surface,
      borderRadius: BorderRadius.circular(16),
      border: Border.all(color: FmColors.border),
    ),
    child: Row(
      children: [
        IconButton(
          onPressed: onUp,
          icon: const Icon(Icons.arrow_upward_rounded),
          tooltip: 'Up',
        ),
        Expanded(
          child: Text(
            path.isEmpty ? repoRootLabel : path,
            overflow: TextOverflow.ellipsis,
            style: const TextStyle(fontWeight: FontWeight.w800),
          ),
        ),
      ],
    ),
  );
  static const repoRootLabel = 'Repository root';
}

class _FileList extends StatelessWidget {
  const _FileList({
    required this.entries,
    required this.onDir,
    required this.onFile,
  });
  final List<RepoTreeEntry> entries;
  final ValueChanged<String> onDir;
  final ValueChanged<RepoTreeEntry> onFile;
  @override
  Widget build(BuildContext context) => Container(
    decoration: BoxDecoration(
      color: FmColors.surface,
      border: Border.all(color: FmColors.border),
      borderRadius: BorderRadius.circular(22),
    ),
    child: Column(
      children: [
        for (var i = 0; i < entries.length; i++) ...[
          ListTile(
            leading: Icon(
              entries[i].isDirectory
                  ? Icons.folder_rounded
                  : Icons.description_outlined,
              color: entries[i].isDirectory
                  ? FmColors.accent
                  : FmColors.textMuted,
            ),
            title: Text(
              entries[i].name,
              maxLines: 1,
              overflow: TextOverflow.ellipsis,
              style: const TextStyle(fontWeight: FontWeight.w700),
            ),
            subtitle: entries[i].isDirectory
                ? const Text('Folder')
                : Text(
                    entries[i].size > 0 ? '${entries[i].size} bytes' : 'File',
                  ),
            trailing: const Icon(Icons.chevron_right),
            onTap: () => entries[i].isDirectory
                ? onDir(entries[i].path)
                : onFile(entries[i]),
          ),
          if (i != entries.length - 1) const Divider(indent: 56, height: 1),
        ],
      ],
    ),
  );
}

class _MobileCard extends StatelessWidget {
  const _MobileCard({
    required this.icon,
    required this.iconColor,
    required this.title,
    required this.subtitle,
    this.badge = '',
    this.onTap,
  });
  final IconData icon;
  final Color iconColor;
  final String title;
  final String subtitle;
  final String badge;
  final VoidCallback? onTap;
  @override
  Widget build(BuildContext context) => Padding(
    padding: const EdgeInsets.fromLTRB(16, 6, 16, 6),
    child: Material(
      color: FmColors.surface,
      borderRadius: BorderRadius.circular(20),
      child: InkWell(
        onTap: onTap,
        borderRadius: BorderRadius.circular(20),
        child: Container(
          padding: const EdgeInsets.all(14),
          decoration: BoxDecoration(
            border: Border.all(color: FmColors.border),
            borderRadius: BorderRadius.circular(20),
          ),
          child: Row(
            crossAxisAlignment: CrossAxisAlignment.start,
            children: [
              Icon(icon, color: iconColor),
              const SizedBox(width: 12),
              Expanded(
                child: Column(
                  crossAxisAlignment: CrossAxisAlignment.start,
                  children: [
                    Text(
                      title,
                      maxLines: 2,
                      overflow: TextOverflow.ellipsis,
                      style: const TextStyle(fontWeight: FontWeight.w800),
                    ),
                    if (subtitle.isNotEmpty) ...[
                      const SizedBox(height: 5),
                      Text(
                        subtitle,
                        style: const TextStyle(
                          color: FmColors.textMuted,
                          fontSize: 12,
                        ),
                      ),
                    ],
                  ],
                ),
              ),
              if (badge.isNotEmpty) _SmallBadge(badge),
            ],
          ),
        ),
      ),
    ),
  );
}

class _SmallBadge extends StatelessWidget {
  const _SmallBadge(this.text);
  final String text;
  @override
  Widget build(BuildContext context) => Container(
    padding: const EdgeInsets.symmetric(horizontal: 8, vertical: 4),
    decoration: BoxDecoration(
      color: FmColors.canvas,
      borderRadius: BorderRadius.circular(99),
      border: Border.all(color: FmColors.borderMuted),
    ),
    child: Text(
      text,
      style: const TextStyle(
        fontSize: 11,
        fontWeight: FontWeight.w800,
        color: FmColors.textMuted,
      ),
    ),
  );
}

class _LoadingCard extends StatelessWidget {
  const _LoadingCard({required this.label});
  final String label;
  @override
  Widget build(BuildContext context) =>
      _InfoCard(title: label, children: const [LinearProgressIndicator()]);
}

class _ErrorCard extends StatelessWidget {
  const _ErrorCard({required this.message, required this.onRetry});
  final String message;
  final VoidCallback onRetry;
  @override
  Widget build(BuildContext context) => _InfoCard(
    title: 'Something went wrong',
    children: [
      Text(message, style: const TextStyle(color: FmColors.textMuted)),
      const SizedBox(height: 12),
      OutlinedButton(onPressed: onRetry, child: const Text('Retry')),
    ],
  );
}

class _EmptyCard extends StatelessWidget {
  const _EmptyCard({required this.message});
  final String message;
  @override
  Widget build(BuildContext context) => _InfoCard(
    title: 'Nothing here yet',
    children: [
      Text(message, style: const TextStyle(color: FmColors.textMuted)),
    ],
  );
}

Widget _kv(String k, String v) => Padding(
  padding: const EdgeInsets.symmetric(vertical: 6),
  child: Row(
    children: [
      SizedBox(
        width: 130,
        child: Text(k, style: const TextStyle(color: FmColors.textMuted)),
      ),
      Expanded(
        child: Text(v, style: const TextStyle(fontWeight: FontWeight.w700)),
      ),
    ],
  ),
);

class _AsyncList<T> extends StatelessWidget {
  const _AsyncList({
    required this.future,
    required this.itemBuilder,
    required this.empty,
  });
  final Future<List<T>> future;
  final Widget Function(T) itemBuilder;
  final String empty;

  @override
  Widget build(BuildContext context) {
    return FutureBuilder<List<T>>(
      future: future,
      builder: (context, snap) {
        if (snap.connectionState == ConnectionState.waiting) {
          return ListView(
            padding: const EdgeInsets.all(16),
            children: const [_LoadingCard(label: 'Loading…')],
          );
        }
        if (snap.hasError) {
          return ListView(
            padding: const EdgeInsets.all(16),
            children: [_EmptyCard(message: '${snap.error}')],
          );
        }
        final items = snap.data ?? [];
        if (items.isEmpty) {
          return ListView(
            padding: const EdgeInsets.all(16),
            children: [_EmptyCard(message: empty)],
          );
        }
        return ListView.builder(
          padding: const EdgeInsets.symmetric(vertical: 10),
          itemCount: items.length,
          itemBuilder: (_, i) => itemBuilder(items[i]),
        );
      },
    );
  }
}
