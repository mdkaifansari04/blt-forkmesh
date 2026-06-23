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
  late final TabController _tabs = TabController(length: 4, vsync: this)
    ..addListener(() => setState(() {}));

  @override
  void dispose() {
    _tabs.dispose();
    super.dispose();
  }

  Repository get repo => widget.repo;

  Widget? _fab() {
    if (_tabs.index == 2) {
      return FloatingActionButton.extended(
        onPressed: () => showNewIssueDialog(context, repo),
        icon: const Icon(Icons.add),
        label: const Text('New issue'),
      );
    }
    if (_tabs.index == 3) {
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
          indicatorColor: FmColors.accentEdge,
          tabs: const [
            Tab(text: 'About'),
            Tab(text: 'Commits'),
            Tab(text: 'Issues'),
            Tab(text: 'Pull requests'),
          ],
        ),
      ),
      floatingActionButton: _fab(),
      body: TabBarView(
        controller: _tabs,
        children: [
          _AboutTab(repo: repo),
          _CommitsTab(api: api, repo: repo),
          _IssuesTab(api: api, repo: repo),
          _PullsTab(api: api, repo: repo),
        ],
      ),
    );
  }
}

// ---------------------------------------------------------------------------
// Write-path dialogs and sheets (all submit to the signed inbox).
// ---------------------------------------------------------------------------

Future<void> _run(BuildContext context, Future<void> Function() action,
    String okMessage) async {
  final messenger = ScaffoldMessenger.of(context);
  try {
    await action();
    messenger.showSnackBar(SnackBar(content: Text(okMessage)));
  } catch (e) {
    messenger.showSnackBar(SnackBar(
        backgroundColor: FmColors.danger, content: Text('Failed: $e')));
  }
}

const _pendingNote = 'Submitted to the inbox — pending the repo owner applying it.';

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
        child: Column(mainAxisSize: MainAxisSize.min, children: [
          TextField(controller: title, decoration: const InputDecoration(labelText: 'Title')),
          const SizedBox(height: 12),
          TextField(
            controller: body,
            minLines: 4,
            maxLines: 10,
            decoration: const InputDecoration(labelText: 'Description', alignLabelWithHint: true),
          ),
        ]),
      ),
      actions: [
        TextButton(onPressed: () => Navigator.pop(ctx, false), child: const Text('Cancel')),
        FilledButton(onPressed: () => Navigator.pop(ctx, true), child: const Text('Submit')),
      ],
    ),
  );
  if (ok != true || title.text.trim().isEmpty) return;
  if (!context.mounted) return;
  await _run(
    context,
    () => inbox.submitNewIssue(repo.owner, repo.name,
        title: title.text.trim(), body: body.text.trim()),
    _pendingNote,
  );
}

Future<void> showIssueActions(BuildContext context, Repository repo, Issue issue) async {
  final inbox = context.read<InboxService>();
  await showModalBottomSheet<void>(
    context: context,
    backgroundColor: FmColors.surface,
    showDragHandle: true,
    builder: (ctx) => SafeArea(
      child: Column(mainAxisSize: MainAxisSize.min, children: [
        ListTile(
          title: Text('#${issue.number} · ${issue.title}',
              style: const TextStyle(fontWeight: FontWeight.w700)),
          subtitle: issue.body.isEmpty ? null : Text(issue.body, maxLines: 4, overflow: TextOverflow.ellipsis),
        ),
        const Divider(height: 1),
        ListTile(
          leading: const Icon(Icons.how_to_vote_outlined),
          title: const Text('Vote'),
          onTap: () {
            Navigator.pop(ctx);
            _run(context, () => inbox.voteOnIssue(repo.owner, repo.name, issue.number),
                'Vote submitted to the inbox.');
          },
        ),
        ListTile(
          leading: const Icon(Icons.chat_bubble_outline),
          title: const Text('Comment'),
          onTap: () async {
            Navigator.pop(ctx);
            final body = await _promptText(context, 'Comment on #${issue.number}', multiline: true);
            if (body == null || body.trim().isEmpty || !context.mounted) return;
            await _run(context,
                () => inbox.commentOnIssue(repo.owner, repo.name, issue.number, body.trim()),
                _pendingNote);
          },
        ),
        ListTile(
          leading: Icon(issue.isOpen ? Icons.check_circle_outline : Icons.refresh),
          title: Text(issue.isOpen ? 'Close issue' : 'Reopen issue'),
          onTap: () {
            Navigator.pop(ctx);
            _run(
              context,
              () => inbox.setIssueStatus(repo.owner, repo.name, issue.number,
                  issue.isOpen ? 'closed' : 'open'),
              _pendingNote,
            );
          },
        ),
      ]),
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
        child: Column(mainAxisSize: MainAxisSize.min, children: [
          TextField(controller: title, decoration: const InputDecoration(labelText: 'Title')),
          const SizedBox(height: 10),
          Row(children: [
            Expanded(child: TextField(controller: base, decoration: const InputDecoration(labelText: 'Base branch'))),
            const SizedBox(width: 10),
            Expanded(child: TextField(controller: head, decoration: const InputDecoration(labelText: 'Head branch'))),
          ]),
          const SizedBox(height: 10),
          TextField(
            controller: patch,
            minLines: 5,
            maxLines: 14,
            style: const TextStyle(fontFamily: 'monospace', fontSize: 12),
            decoration: const InputDecoration(
                labelText: 'Unified diff (patch)', alignLabelWithHint: true),
          ),
        ]),
      ),
      actions: [
        TextButton(onPressed: () => Navigator.pop(ctx, false), child: const Text('Cancel')),
        FilledButton(onPressed: () => Navigator.pop(ctx, true), child: const Text('Submit')),
      ],
    ),
  );
  if (ok != true || title.text.trim().isEmpty || patch.text.trim().isEmpty) return;
  if (!context.mounted) return;
  await _run(
    context,
    () => inbox.submitNewPull(repo.owner, repo.name,
        title: title.text.trim(),
        base: base.text.trim(),
        head: head.text.trim(),
        patch: patch.text),
    _pendingNote,
  );
}

Future<void> showPullActions(BuildContext context, Repository repo, PullRequest pr) async {
  final inbox = context.read<InboxService>();
  await showModalBottomSheet<void>(
    context: context,
    backgroundColor: FmColors.surface,
    showDragHandle: true,
    builder: (ctx) => SafeArea(
      child: Column(mainAxisSize: MainAxisSize.min, children: [
        ListTile(
          title: Text('#${pr.number} · ${pr.title}',
              style: const TextStyle(fontWeight: FontWeight.w700)),
          subtitle: Text('${pr.headBranch} → ${pr.baseBranch} · ${pr.status}'),
        ),
        const Divider(height: 1),
        ListTile(
          leading: const Icon(Icons.chat_bubble_outline),
          title: const Text('Comment'),
          onTap: () async {
            Navigator.pop(ctx);
            final body = await _promptText(context, 'Comment on PR #${pr.number}', multiline: true);
            if (body == null || body.trim().isEmpty || !context.mounted) return;
            await _run(context,
                () => inbox.commentOnPull(repo.owner, repo.name, pr.number, body.trim()),
                _pendingNote);
          },
        ),
        ListTile(
          leading: const Icon(Icons.check_circle_outline, color: FmColors.success),
          title: const Text('Approve'),
          onTap: () => _review(context, ctx, inbox, repo, pr, 'approve'),
        ),
        ListTile(
          leading: const Icon(Icons.cancel_outlined, color: FmColors.danger),
          title: const Text('Request changes'),
          onTap: () => _review(context, ctx, inbox, repo, pr, 'request-changes'),
        ),
      ]),
    ),
  );
}

Future<void> _review(BuildContext context, BuildContext sheetCtx, InboxService inbox,
    Repository repo, PullRequest pr, String state) async {
  Navigator.pop(sheetCtx);
  final body = await _promptText(context, 'Review note (optional)', multiline: true);
  if (!context.mounted) return;
  await _run(context,
      () => inbox.reviewPull(repo.owner, repo.name, pr.number, state, body?.trim() ?? ''),
      _pendingNote);
}

Future<String?> _promptText(BuildContext context, String title, {bool multiline = false}) {
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
        TextButton(onPressed: () => Navigator.pop(ctx), child: const Text('Cancel')),
        FilledButton(onPressed: () => Navigator.pop(ctx, c.text), child: const Text('Submit')),
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
        const Text('About', style: TextStyle(fontSize: 15, fontWeight: FontWeight.w700)),
        const SizedBox(height: 8),
        Text(repo.description.isEmpty ? 'No description provided.' : repo.description),
        const SizedBox(height: 16),
        _kv('Default branch', repo.defaultBranch),
        _kv('Language', repo.language.isEmpty ? '—' : repo.language),
        _kv('Stars', '${repo.stars}'),
        _kv('Forks', '${repo.forks}'),
        _kv('Mirrors', '${repo.mirrors}'),
        _kv('Visibility', repo.isPrivate ? 'Private' : 'Public'),
      ],
    );
  }

  Widget _kv(String k, String v) => Padding(
        padding: const EdgeInsets.symmetric(vertical: 4),
        child: Row(children: [
          SizedBox(width: 130, child: Text(k, style: const TextStyle(color: FmColors.textMuted))),
          Expanded(child: Text(v)),
        ]),
      );
}

class _IssuesTab extends StatelessWidget {
  const _IssuesTab({required this.api, required this.repo});
  final ApiService api;
  final Repository repo;
  @override
  Widget build(BuildContext context) {
    return _AsyncList<Issue>(
      future: api.issues(repo.owner, repo.name),
      empty: 'No issues. Tap “New issue” to open one.',
      itemBuilder: (issue) => ListTile(
        leading: Icon(
          issue.isOpen ? Icons.error_outline : Icons.check_circle_outline,
          color: issue.isOpen ? FmColors.success : FmColors.accentEdge,
        ),
        title: Text(issue.title, maxLines: 2, overflow: TextOverflow.ellipsis),
        subtitle: Text('#${issue.number} · ${issue.author}'
            '${issue.votes > 0 ? " · ${issue.votes} votes" : ""}'
            '${issue.bountyUsd > 0 ? " · \$${issue.bountyUsd.toStringAsFixed(0)} bounty" : ""}'),
        onTap: () => showIssueActions(context, repo, issue),
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
    return _AsyncList<PullRequest>(
      future: api.pulls(repo.owner, repo.name),
      empty: 'No pull requests. Tap “New PR” to open one.',
      itemBuilder: (pr) => ListTile(
        leading: Icon(Icons.call_merge,
            color: pr.status == 'merged'
                ? const Color(0xFF8250DF)
                : (pr.status == 'closed' ? FmColors.danger : FmColors.success)),
        title: Text(pr.title, maxLines: 2, overflow: TextOverflow.ellipsis),
        subtitle: Text('#${pr.number} · ${pr.author} · ${pr.status}'),
        onTap: () => showPullActions(context, repo, pr),
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
        return ListTile(
          leading: const Icon(Icons.commit, color: FmColors.textMuted),
          title: Text((c['subject'] ?? c['message'] ?? '').toString(),
              maxLines: 1, overflow: TextOverflow.ellipsis),
          subtitle: Text('${(c['author'] ?? '').toString()} · $short'),
        );
      },
    );
  }
}

class _AsyncList<T> extends StatelessWidget {
  const _AsyncList({required this.future, required this.itemBuilder, required this.empty});
  final Future<List<T>> future;
  final Widget Function(T) itemBuilder;
  final String empty;

  @override
  Widget build(BuildContext context) {
    return FutureBuilder<List<T>>(
      future: future,
      builder: (context, snap) {
        if (snap.connectionState == ConnectionState.waiting) {
          return const Center(child: CircularProgressIndicator());
        }
        if (snap.hasError) {
          return Center(
              child: Text('${snap.error}',
                  textAlign: TextAlign.center,
                  style: const TextStyle(color: FmColors.textMuted)));
        }
        final items = snap.data ?? [];
        if (items.isEmpty) {
          return Center(
              child: Padding(
            padding: const EdgeInsets.all(24),
            child: Text(empty,
                textAlign: TextAlign.center, style: const TextStyle(color: FmColors.textMuted)),
          ));
        }
        return ListView.separated(
          itemCount: items.length,
          separatorBuilder: (_, _) => const Divider(height: 1),
          itemBuilder: (_, i) => itemBuilder(items[i]),
        );
      },
    );
  }
}
