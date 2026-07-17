import 'dart:async';
import 'dart:convert';

import 'package:flutter/material.dart';
import 'package:flutter/services.dart';
import 'package:provider/provider.dart';

import '../models/models.dart';
import '../services/api_service.dart';
import '../services/auth_service.dart';
import '../services/identity.dart';
import '../services/inbox_service.dart';
import '../services/notification_deep_link.dart';
import '../theme.dart';
import '../widgets/compose_identity_bar.dart';
import '../widgets/fm_ui.dart';

/// Repo detail with the GitHub-style tabs the Qt client has: About/Code,
/// Commits, Issues, Pull requests. Reads over the worker REST API; writes
/// (new issue/PR, comment, vote, review, status) go to the signed relay inbox.
class RepoDetailScreen extends StatefulWidget {
  const RepoDetailScreen({
    super.key,
    required this.repo,
    this.initialTab = RepoDetailTab.code,
    this.initialTarget,
  });
  final Repository repo;
  final RepoDetailTab initialTab;
  final NotificationDeepLink? initialTarget;

  @override
  State<RepoDetailScreen> createState() => _RepoDetailScreenState();
}

class _RepoDetailScreenState extends State<RepoDetailScreen>
    with SingleTickerProviderStateMixin {
  late final TabController _tabs;
  bool _openedInitialTarget = false;

  @override
  void initState() {
    super.initState();
    _tabs = TabController(
      length: RepoDetailTab.values.length,
      initialIndex: widget.initialTab.index,
      vsync: this,
    )..addListener(() => setState(() {}));
    WidgetsBinding.instance.addPostFrameCallback((_) {
      if (mounted) _openInitialTarget();
    });
  }

  @override
  void dispose() {
    _tabs.dispose();
    super.dispose();
  }

  Repository get repo => widget.repo;

  Future<void> _openInitialTarget() async {
    if (_openedInitialTarget) return;
    _openedInitialTarget = true;
    final target = widget.initialTarget;
    if (target == null) return;
    final api = context.read<ApiService>();
    try {
      switch (target.initialTab) {
        case RepoDetailTab.issues:
          if (target.number <= 0) return;
          final issues = await api.publishedIssues(repo.owner, repo.name);
          if (!mounted) return;
          if (!(ModalRoute.of(context)?.isCurrent ?? false)) return;
          final issue = _firstWhereOrNull<Issue>(
            issues,
            (item) => item.number == target.number,
          );
          if (issue == null) return;
          Navigator.of(context).push(
            MaterialPageRoute<void>(
              builder: (_) => _IssueDetailScreen(repo: repo, issue: issue),
            ),
          );
        case RepoDetailTab.pulls:
          if (target.number <= 0) return;
          final pulls = await api.publishedPulls(repo.owner, repo.name);
          if (!mounted) return;
          if (!(ModalRoute.of(context)?.isCurrent ?? false)) return;
          final pull = _firstWhereOrNull<PublishedPull>(
            pulls,
            (item) => item.number == target.number,
          );
          if (pull == null) return;
          Navigator.of(context).push(
            MaterialPageRoute<void>(
              builder: (_) => _PullDetailScreen(repo: repo, pull: pull),
            ),
          );
        case RepoDetailTab.discussions:
          if (target.number <= 0) return;
          final discussions = await api.publishedDiscussions(
            repo.owner,
            repo.name,
          );
          if (!mounted) return;
          if (!(ModalRoute.of(context)?.isCurrent ?? false)) return;
          final discussion = _firstWhereOrNull<RepoDiscussion>(
            discussions,
            (item) => item.number == target.number,
          );
          if (discussion == null) return;
          Navigator.of(context).push(
            MaterialPageRoute<void>(
              builder: (_) =>
                  _DiscussionDetailScreen(repo: repo, discussion: discussion),
            ),
          );
        case RepoDetailTab.commits:
          final hash = target.reference.trim();
          if (hash.isEmpty) return;
          if (!(ModalRoute.of(context)?.isCurrent ?? false)) return;
          Navigator.of(context).push(
            MaterialPageRoute<void>(
              builder: (_) => _CommitDetailScreen(
                api: api,
                repo: repo,
                commit: {'hash': hash, 'message': 'Commit $hash'},
              ),
            ),
          );
        case RepoDetailTab.code:
        case RepoDetailTab.mirrors:
        case RepoDetailTab.releases:
        case RepoDetailTab.about:
        case RepoDetailTab.actions:
        case RepoDetailTab.agents:
        case RepoDetailTab.worktrees:
          return;
      }
    } catch (_) {
      // Keep the repo tab open if the exact item cannot be loaded yet.
    }
  }

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
    if (_tabs.index == 3) {
      return FloatingActionButton.extended(
        onPressed: () => showNewDiscussionDialog(context, repo),
        icon: const Icon(Icons.add),
        label: const Text('New discussion'),
      );
    }
    return null;
  }

  @override
  Widget build(BuildContext context) {
    final api = context.read<ApiService>();
    return Scaffold(
      appBar: AppBar(
        backgroundColor: FmTheme.bgRaised(context),
        surfaceTintColor: Colors.transparent,
        title: Text(
          repo.fullName,
          overflow: TextOverflow.ellipsis,
          style: TextStyle(
            color: FmTheme.textPrimary(context),
            fontSize: 16,
            fontWeight: FontWeight.w700,
          ),
        ),
        bottom: PreferredSize(
          preferredSize: const Size.fromHeight(48),
          child: ColoredBox(
            color: FmTheme.bgRaised(context),
            child: TabBar(
              controller: _tabs,
              isScrollable: true,
              indicatorSize: TabBarIndicatorSize.tab,
              indicator: UnderlineTabIndicator(
                borderSide: BorderSide(
                  color: FmTheme.activeUnderline(context),
                  width: 2,
                ),
                insets: const EdgeInsets.symmetric(horizontal: FmSpace.x3),
              ),
              labelColor: FmTheme.textPrimary(context),
              unselectedLabelColor: FmTheme.textTertiary(context),
              labelStyle: const TextStyle(
                fontSize: 15,
                fontWeight: FontWeight.w700,
              ),
              unselectedLabelStyle: const TextStyle(
                fontSize: 15,
                fontWeight: FontWeight.w600,
              ),
              dividerColor: FmTheme.border(context),
              tabAlignment: TabAlignment.start,
              padding: const EdgeInsets.fromLTRB(
                FmSpace.x3,
                FmSpace.x0,
                FmSpace.x3,
                FmSpace.x2,
              ),
              tabs: const [
                Tab(text: 'Code'),
                Tab(text: 'Issues'),
                Tab(text: 'Pulls'),
                Tab(text: 'Discussions'),
                Tab(text: 'Commits'),
                Tab(text: 'Mirrors'),
                Tab(text: 'Releases'),
                Tab(text: 'About'),
                Tab(text: 'Actions'),
                Tab(text: 'Agents'),
                Tab(text: 'Worktrees'),
              ],
            ),
          ),
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
          _ReleasesTab(api: api, repo: repo),
          _AboutTab(repo: repo),
          _ActionsTab(api: api, repo: repo),
          _AgentsTab(api: api, repo: repo),
          _WorktreesTab(api: api, repo: repo),
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
  final dangerColor = FmTheme.danger(context);
  try {
    await action();
    messenger.showSnackBar(SnackBar(content: Text(okMessage)));
  } catch (e) {
    messenger.showSnackBar(
      SnackBar(backgroundColor: dangerColor, content: Text('Failed: $e')),
    );
  }
}

const _pendingNote =
    'Submitted to the inbox - pending the repo owner applying it.';
const _threadSubscriptionNote =
    'Thread notifications are signed with your node key and delivered through the mainnode notification inbox.';

Future<void> _copyText(
  BuildContext context, {
  required String label,
  required String value,
}) async {
  await Clipboard.setData(ClipboardData(text: value));
  if (!context.mounted) return;
  ScaffoldMessenger.of(
    context,
  ).showSnackBar(SnackBar(content: Text('$label copied')));
}

const Map<String, String> _discussionCategoryLabels = {
  'general': 'General',
  'ideas': 'Ideas',
  'help': 'Help',
  'show-and-tell': 'Show & tell',
};

T? _firstWhereOrNull<T>(Iterable<T> items, bool Function(T item) test) {
  for (final item in items) {
    if (test(item)) return item;
  }
  return null;
}

Future<void> _requestDesktopCommand(
  BuildContext context,
  ApiService api,
  Repository repo, {
  required String command,
  required String target,
  required Map<String, Object?> payload,
  required String successMessage,
  String failureMessage = 'Desktop request was not queued.',
}) async {
  final messenger = ScaffoldMessenger.of(context);
  try {
    final auth = context.read<AuthService?>();
    final identity = context.read<Identity?>();
    final session = auth?.session;
    final account = session?.nodeName.trim().toLowerCase() ?? '';
    if (session == null || identity == null || account.isEmpty) {
      throw Exception(
        'Sign in with the repo owner account to request desktop control.',
      );
    }
    if (account != repo.owner.trim().toLowerCase()) {
      throw Exception(
        'Only the repo owner can request desktop control for this repo.',
      );
    }
    if (session.pubkey.trim() != identity.publicKeyB64url) {
      throw Exception(
        'Pair this mobile device with the owner key before requesting desktop control.',
      );
    }
    final ts = DateTime.now().millisecondsSinceEpoch.toString();
    final canonical =
        'forkmesh-desktop-command-v1\n${repo.owner}\n${repo.name}\n$command\n$target\n$ts';
    final sig = await identity.sign(utf8.encode(canonical));
    final result = await api.desktopCommand(
      repo.owner,
      repo.name,
      ownerAccount: account,
      command: command,
      target: target,
      ts: ts,
      sig: sig,
      payload: payload,
    );
    if (!context.mounted) return;
    messenger.showSnackBar(
      SnackBar(content: Text(result.ok ? successMessage : failureMessage)),
    );
  } catch (error) {
    if (!context.mounted) return;
    messenger.showSnackBar(SnackBar(content: Text('$error')));
  }
}

Future<void> showNewIssueDialog(BuildContext context, Repository repo) async {
  final inbox = context.read<InboxService>();
  final title = TextEditingController();
  final body = TextEditingController();
  final labels = TextEditingController();
  final priority = TextEditingController();
  final milestone = TextEditingController();
  final assignees = TextEditingController();
  final ok = await showDialog<bool>(
    context: context,
    builder: (ctx) => AlertDialog(
      backgroundColor: FmTheme.bgOverlay(ctx),
      surfaceTintColor: Colors.transparent,
      shape: RoundedRectangleBorder(
        borderRadius: BorderRadius.circular(FmRadius.lg),
      ),
      title: const Text('New issue'),
      content: SizedBox(
        width: 520,
        child: SingleChildScrollView(
          child: Column(
            mainAxisSize: MainAxisSize.min,
            crossAxisAlignment: CrossAxisAlignment.start,
            children: [
              const ComposeIdentityBar(verb: 'Posting'),
              const SizedBox(height: 8),
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
              const SizedBox(height: 12),
              TextField(
                controller: labels,
                decoration: const InputDecoration(
                  labelText: 'Labels',
                  helperText: 'Comma-separated, e.g. mobile, bug',
                ),
              ),
              const SizedBox(height: 12),
              Row(
                children: [
                  Expanded(
                    child: TextField(
                      controller: priority,
                      keyboardType: TextInputType.number,
                      decoration: const InputDecoration(labelText: 'Priority'),
                    ),
                  ),
                  const SizedBox(width: 12),
                  Expanded(
                    child: TextField(
                      controller: milestone,
                      decoration: const InputDecoration(labelText: 'Milestone'),
                    ),
                  ),
                ],
              ),
              const SizedBox(height: 12),
              TextField(
                controller: assignees,
                decoration: const InputDecoration(
                  labelText: 'Assignees',
                  helperText: 'Comma-separated node/user names',
                ),
              ),
              const SizedBox(height: 12),
              const _PendingInboxNote(),
            ],
          ),
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
      labels: _splitCommaSeparated(labels.text),
      milestone: milestone.text.trim(),
      priority: int.tryParse(priority.text.trim()) ?? 0,
      assignees: _splitCommaSeparated(assignees.text),
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
    backgroundColor: FmTheme.bgOverlay(context),
    shape: const RoundedRectangleBorder(
      borderRadius: BorderRadius.vertical(top: Radius.circular(FmRadius.lg)),
    ),
    clipBehavior: Clip.antiAlias,
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
          const SizedBox(height: FmSpace.x2),
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
      backgroundColor: FmTheme.bgOverlay(ctx),
      surfaceTintColor: Colors.transparent,
      shape: RoundedRectangleBorder(
        borderRadius: BorderRadius.circular(FmRadius.lg),
      ),
      title: const Text('New pull request'),
      content: SizedBox(
        width: 520,
        child: Column(
          mainAxisSize: MainAxisSize.min,
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            const ComposeIdentityBar(verb: 'Posting'),
            const SizedBox(height: 8),
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

Future<void> showNewDiscussionDialog(
  BuildContext context,
  Repository repo,
) async {
  final inbox = context.read<InboxService>();
  final title = TextEditingController();
  var selectedCategory = 'general';
  final body = TextEditingController();
  final ok = await showDialog<bool>(
    context: context,
    builder: (ctx) => StatefulBuilder(
      builder: (ctx, setDialogState) => AlertDialog(
        backgroundColor: FmTheme.bgOverlay(ctx),
        surfaceTintColor: Colors.transparent,
        shape: RoundedRectangleBorder(
          borderRadius: BorderRadius.circular(FmRadius.lg),
        ),
        title: const Text('New discussion'),
        content: SizedBox(
          width: 520,
          child: Column(
            mainAxisSize: MainAxisSize.min,
            crossAxisAlignment: CrossAxisAlignment.start,
            children: [
              const ComposeIdentityBar(verb: 'Posting'),
              const SizedBox(height: 8),
              TextField(
                controller: title,
                decoration: const InputDecoration(labelText: 'Title'),
              ),
              const SizedBox(height: 12),
              const Text(
                'Category',
                style: TextStyle(fontWeight: FontWeight.w800),
              ),
              const SizedBox(height: 8),
              Wrap(
                spacing: 8,
                runSpacing: 8,
                children: [
                  for (final entry in _discussionCategoryLabels.entries)
                    ChoiceChip(
                      label: Text(entry.value),
                      selected: selectedCategory == entry.key,
                      onSelected: (_) => setDialogState(() {
                        selectedCategory = entry.key;
                      }),
                    ),
                ],
              ),
              const SizedBox(height: 12),
              TextField(
                controller: body,
                minLines: 5,
                maxLines: 12,
                decoration: const InputDecoration(
                  labelText: 'Body',
                  alignLabelWithHint: true,
                ),
              ),
              const SizedBox(height: 12),
              const _PendingInboxNote(),
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
    ),
  );
  if (ok != true || title.text.trim().isEmpty || body.text.trim().isEmpty) {
    return;
  }
  if (!context.mounted) return;
  await _run(
    context,
    () => inbox.submitNewDiscussion(
      repo.owner,
      repo.name,
      title: title.text.trim(),
      body: body.text.trim(),
      category: selectedCategory,
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
    backgroundColor: FmTheme.bgOverlay(context),
    shape: const RoundedRectangleBorder(
      borderRadius: BorderRadius.vertical(top: Radius.circular(FmRadius.lg)),
    ),
    clipBehavior: Clip.antiAlias,
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
          const SizedBox(height: FmSpace.x2),
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
            leading: Icon(
              Icons.check_circle_outline,
              color: FmTheme.success(ctx),
            ),
            title: const Text('Approve'),
            onTap: () => _review(context, ctx, inbox, repo, pr, 'approve'),
          ),
          ListTile(
            leading: Icon(Icons.cancel_outlined, color: FmTheme.danger(ctx)),
            title: const Text('Request changes'),
            onTap: () =>
                _review(context, ctx, inbox, repo, pr, 'request-changes'),
          ),
        ],
      ),
    ),
  );
}

Future<void> showPublishedPullActions(
  BuildContext context,
  Repository repo,
  PublishedPull pull,
) async {
  final inbox = context.read<InboxService>();
  await showModalBottomSheet<void>(
    context: context,
    backgroundColor: FmTheme.bgOverlay(context),
    shape: const RoundedRectangleBorder(
      borderRadius: BorderRadius.vertical(top: Radius.circular(FmRadius.lg)),
    ),
    clipBehavior: Clip.antiAlias,
    showDragHandle: true,
    builder: (ctx) => SafeArea(
      child: Column(
        mainAxisSize: MainAxisSize.min,
        children: [
          ListTile(
            title: Text(
              '#${pull.number} · ${pull.title}',
              style: const TextStyle(fontWeight: FontWeight.w700),
            ),
            subtitle: Text('${pull.head} → ${pull.base} · ${pull.status}'),
          ),
          const SizedBox(height: FmSpace.x2),
          ListTile(
            leading: const Icon(Icons.chat_bubble_outline),
            title: const Text('Comment'),
            onTap: () async {
              Navigator.pop(ctx);
              final body = await _promptText(
                context,
                'Comment on PR #${pull.number}',
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
                  pull.number,
                  body.trim(),
                ),
                _pendingNote,
              );
            },
          ),
          ListTile(
            leading: Icon(
              Icons.check_circle_outline,
              color: FmTheme.success(ctx),
            ),
            title: const Text('Approve'),
            onTap: () => _reviewPublishedPull(
              context,
              ctx,
              inbox,
              repo,
              pull,
              'approve',
            ),
          ),
          ListTile(
            leading: Icon(Icons.cancel_outlined, color: FmTheme.danger(ctx)),
            title: const Text('Request changes'),
            onTap: () => _reviewPublishedPull(
              context,
              ctx,
              inbox,
              repo,
              pull,
              'request-changes',
            ),
          ),
          ListTile(
            leading: const Icon(Icons.rate_review_outlined),
            title: const Text('Leave review comment'),
            onTap: () => _reviewPublishedPull(
              context,
              ctx,
              inbox,
              repo,
              pull,
              'comment',
            ),
          ),
        ],
      ),
    ),
  );
}

Future<void> _reviewPublishedPull(
  BuildContext context,
  BuildContext sheetCtx,
  InboxService inbox,
  Repository repo,
  PublishedPull pull,
  String state,
) async {
  Navigator.pop(sheetCtx);
  final body = await _showPullReviewComposer(
    context,
    number: pull.number,
    state: state,
  );
  if (body == null || !context.mounted) return;
  await _run(
    context,
    () => inbox.reviewPull(repo.owner, repo.name, pull.number, state, body),
    _pendingNote,
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
  final body = await _showPullReviewComposer(
    context,
    number: pr.number,
    state: state,
  );
  if (body == null || !context.mounted) return;
  await _run(
    context,
    () => inbox.reviewPull(repo.owner, repo.name, pr.number, state, body),
    _pendingNote,
  );
}

Future<String?> _showPullReviewComposer(
  BuildContext context, {
  required int number,
  required String state,
}) {
  final controller = TextEditingController();
  var errorText = '';
  final title = switch (state) {
    'approve' => 'Approve PR #$number',
    'request-changes' => 'Request changes on PR #$number',
    _ => 'Review comment on PR #$number',
  };
  final helper = switch (state) {
    'approve' =>
      'Approval is signed and sent to the pull inbox. The repo owner still applies canonical changes from their desktop node.',
    'request-changes' =>
      'Request-changes reviews are signed and sent to the pull inbox. Add a clear note so the author knows what to fix.',
    _ =>
      'Review comments are signed and sent to the pull inbox, pending the repo owner applying them.',
  };
  return showDialog<String>(
    context: context,
    builder: (ctx) => StatefulBuilder(
      builder: (ctx, setDialogState) => AlertDialog(
        backgroundColor: FmTheme.bgOverlay(ctx),
        surfaceTintColor: Colors.transparent,
        shape: RoundedRectangleBorder(
          borderRadius: BorderRadius.circular(FmRadius.lg),
        ),
        title: Text(title),
        content: SizedBox(
          width: 520,
          child: Column(
            mainAxisSize: MainAxisSize.min,
            crossAxisAlignment: CrossAxisAlignment.start,
            children: [
              const ComposeIdentityBar(verb: 'Reviewing'),
              const SizedBox(height: 8),
              Text(helper, style: const TextStyle(height: 1.4)),
              const SizedBox(height: 12),
              TextField(
                controller: controller,
                autofocus: true,
                minLines: 4,
                maxLines: 8,
                decoration: InputDecoration(
                  labelText: 'Review note',
                  alignLabelWithHint: true,
                  errorText: errorText.isEmpty ? null : errorText,
                ),
              ),
              const SizedBox(height: 12),
              const _PendingInboxNote(),
            ],
          ),
        ),
        actions: [
          TextButton(
            onPressed: () => Navigator.pop(ctx),
            child: const Text('Cancel'),
          ),
          FilledButton(
            onPressed: () {
              final body = controller.text.trim();
              if (state == 'request-changes' && body.isEmpty) {
                setDialogState(() {
                  errorText = 'A note is required to request changes.';
                });
                return;
              }
              Navigator.pop(ctx, body);
            },
            child: const Text('Submit review'),
          ),
        ],
      ),
    ),
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
      backgroundColor: FmTheme.bgOverlay(ctx),
      surfaceTintColor: Colors.transparent,
      shape: RoundedRectangleBorder(
        borderRadius: BorderRadius.circular(FmRadius.lg),
      ),
      title: Text(title),
      content: SizedBox(
        width: 460,
        child: Column(
          mainAxisSize: MainAxisSize.min,
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            const ComposeIdentityBar(verb: 'Commenting'),
            const SizedBox(height: 8),
            TextField(
              controller: c,
              autofocus: true,
              minLines: multiline ? 3 : 1,
              maxLines: multiline ? 8 : 1,
            ),
          ],
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
        if (repo.donationAddress.isNotEmpty) ...[
          const SizedBox(height: 14),
          _InfoCard(
            title: 'Repository funding',
            children: [
              Text(
                'Optional donations use an external Solana wallet and the public repository donation address.',
                style: TextStyle(
                  color: FmTheme.textSecondary(context),
                  height: 1.35,
                ),
              ),
              const SizedBox(height: 10),
              _kv('Donation address', repo.donationAddress),
            ],
          ),
        ],
        const SizedBox(height: 14),
        _OwnerBountyWalletPanel(repo: repo),
        const SizedBox(height: 14),
        _InfoCard(
          title: 'Repository details',
          children: [
            _kv('Default branch', repo.defaultBranch),
            _kv('Language', repo.language.isEmpty ? '-' : repo.language),
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

class _OwnerBountyWalletPanel extends StatefulWidget {
  const _OwnerBountyWalletPanel({required this.repo});

  final Repository repo;

  @override
  State<_OwnerBountyWalletPanel> createState() =>
      _OwnerBountyWalletPanelState();
}

class _OwnerBountyWalletPanelState extends State<_OwnerBountyWalletPanel> {
  BountyWallet? _wallet;
  bool _busy = false;
  String _message = '';

  Repository get repo => widget.repo;

  String _gateCopy(BuildContext context) {
    final auth = context.watch<AuthService?>();
    final identity = context.watch<Identity?>();
    final session = auth?.session;
    final account = session?.nodeName.trim().toLowerCase() ?? '';
    if (session == null || identity == null || account.isEmpty) {
      return 'Sign in as ${repo.owner} with this mobile device paired to the owner key to prepare the Worker-custodied bounty wallet deposit address.';
    }
    if (account != repo.owner.trim().toLowerCase()) {
      return 'Signed in as $account. Only ${repo.owner} can prepare this owner bounty wallet deposit address.';
    }
    if (session.pubkey.trim() != identity.publicKeyB64url) {
      return 'Owner account is present, but this mobile identity is not the owner key.';
    }
    return '';
  }

  Future<void> _prepareWallet() async {
    if (_busy) return;
    final auth = context.read<AuthService?>();
    final identity = context.read<Identity?>();
    final api = context.read<ApiService>();
    final session = auth?.session;
    final account = session?.nodeName.trim().toLowerCase() ?? '';
    try {
      if (session == null || identity == null || account.isEmpty) {
        throw Exception(
          'Sign in as ${repo.owner} with this mobile device paired to the owner key.',
        );
      }
      if (account != repo.owner.trim().toLowerCase()) {
        throw Exception('Only ${repo.owner} can prepare this wallet deposit.');
      }
      if (session.pubkey.trim() != identity.publicKeyB64url) {
        throw Exception('This mobile identity is not the owner key.');
      }
      final ts = DateTime.now().millisecondsSinceEpoch.toString();
      final canonical = 'forkmesh-bounty-wallet-v1\n${repo.owner}\n$ts';
      setState(() {
        _busy = true;
        _message = '';
      });
      final sig = await identity.sign(utf8.encode(canonical));
      final wallet = await api.bountyWallet(
        repo.owner,
        repo.name,
        ts: ts,
        sig: sig,
      );
      if (!mounted) return;
      setState(() {
        _wallet = wallet;
        _message = 'Wallet deposit state refreshed.';
      });
    } catch (error) {
      if (!mounted) return;
      setState(() => _message = '$error');
    } finally {
      if (mounted) setState(() => _busy = false);
    }
  }

  @override
  Widget build(BuildContext context) {
    final gateCopy = _gateCopy(context);
    final canPrepare = gateCopy.isEmpty && !_busy;
    final wallet = _wallet;
    return _InfoCard(
      title: 'Owner bounty wallet',
      children: [
        Text(
          'Worker-custodied bounty wallet deposit address',
          style: TextStyle(
            color: FmTheme.textPrimary(context),
            fontWeight: FontWeight.w800,
          ),
        ),
        const SizedBox(height: 6),
        Text(
          'Prepare this owner-scoped deposit address from a repo funding panel, then fund it from an external Solana wallet. Mobile never receives private keys and cannot spend or debit this wallet.',
          style: TextStyle(color: FmTheme.textSecondary(context), height: 1.35),
        ),
        const SizedBox(height: 12),
        FilledButton.icon(
          key: const Key('repo-owner-bounty-wallet-prepare'),
          onPressed: canPrepare ? _prepareWallet : null,
          icon: const Icon(Icons.account_balance_wallet_outlined, size: 18),
          label: Text(
            _busy ? 'Preparing wallet...' : 'Prepare/view wallet deposit',
          ),
        ),
        if (gateCopy.isNotEmpty) ...[
          const SizedBox(height: 10),
          Text(
            gateCopy,
            style: TextStyle(
              color: FmTheme.textTertiary(context),
              fontSize: 12,
              height: 1.35,
            ),
          ),
        ],
        if (wallet != null && wallet.hasWallet) ...[
          const SizedBox(height: 14),
          _CopyableValue(
            label: 'Deposit address',
            value: wallet.address,
            copyLabel: 'Owner bounty wallet address',
            copyKey: const Key('repo-owner-bounty-wallet-copy-address'),
          ),
          const SizedBox(height: 10),
          _CopyableValue(
            label: 'Balance',
            value: wallet.balanceLabel,
            copyLabel: 'Owner bounty wallet balance',
            copyKey: const Key('repo-owner-bounty-wallet-copy-balance'),
          ),
          if (wallet.payUri.isNotEmpty) ...[
            const SizedBox(height: 10),
            _CopyableValue(
              label: 'Payment URI',
              value: wallet.payUri,
              copyLabel: 'Owner bounty wallet payment URI',
              copyKey: const Key('repo-owner-bounty-wallet-copy-pay-uri'),
            ),
          ],
          if (wallet.explorerAddressUrl.isNotEmpty) ...[
            const SizedBox(height: 10),
            _CopyableValue(
              label: 'Address explorer URL',
              value: wallet.explorerAddressUrl,
              copyLabel: 'Owner bounty wallet explorer URL',
              copyKey: const Key('repo-owner-bounty-wallet-copy-explorer'),
            ),
          ],
        ],
        if (_message.isNotEmpty) ...[
          const SizedBox(height: 10),
          Text(
            _message,
            style: TextStyle(
              color: _message.startsWith('Exception')
                  ? FmTheme.danger(context)
                  : FmTheme.textSecondary(context),
              fontSize: 12,
            ),
          ),
        ],
      ],
    );
  }
}

class _ActionReadProof {
  const _ActionReadProof({
    required this.ownerAccount,
    required this.ts,
    required this.sig,
  });

  final String ownerAccount;
  final String ts;
  final String sig;
}

class _ActionsTab extends StatefulWidget {
  const _ActionsTab({required this.api, required this.repo});

  final ApiService api;
  final Repository repo;

  @override
  State<_ActionsTab> createState() => _ActionsTabState();
}

class _ActionsTabState extends State<_ActionsTab> {
  static const _pollInterval = Duration(seconds: 10);

  Future<RepoActions>? _future;
  Timer? _pollTimer;

  @override
  void didChangeDependencies() {
    super.didChangeDependencies();
    _future ??= _loadActions();
  }

  @override
  void dispose() {
    _pollTimer?.cancel();
    super.dispose();
  }

  Future<_ActionReadProof> _readProof(BuildContext context) async {
    final auth = context.read<AuthService?>();
    final identity = context.read<Identity?>();
    final session = auth?.session;
    final account = session?.nodeName.trim().toLowerCase() ?? '';
    if (session == null || identity == null || account.isEmpty) {
      throw Exception(
        'Actions are owner-key gated. Sign in with the repo owner account and local owner key to view workflow logs.',
      );
    }
    if (account != widget.repo.owner.trim().toLowerCase()) {
      throw Exception(
        'Actions are owner-key gated. This device is signed in as $account, not ${widget.repo.owner}.',
      );
    }
    if (session.pubkey.trim() != identity.publicKeyB64url) {
      throw Exception(
        'Actions are owner-key gated. Pair this mobile device with the owner key before viewing workflow logs.',
      );
    }
    final ts = DateTime.now().millisecondsSinceEpoch.toString();
    final canonical =
        'forkmesh-actions-read-v1\n${widget.repo.owner}\n${widget.repo.name}\n$ts';
    final sig = await identity.sign(utf8.encode(canonical));
    return _ActionReadProof(ownerAccount: account, ts: ts, sig: sig);
  }

  Future<RepoActions> _loadActions() async {
    final proof = await _readProof(context);
    final actions = await widget.api.repoActions(
      widget.repo.owner,
      widget.repo.name,
      ownerAccount: proof.ownerAccount,
      ts: proof.ts,
      sig: proof.sig,
    );
    _syncPolling(actions);
    return actions;
  }

  void _syncPolling(RepoActions actions) {
    if (!mounted) return;
    if (actions.runs.any((run) => run.isActive)) {
      _pollTimer ??= Timer.periodic(_pollInterval, (_) {
        if (!mounted) return;
        setState(() {
          _future = _loadActions();
        });
      });
    } else {
      _pollTimer?.cancel();
      _pollTimer = null;
    }
  }

  Future<void> _refresh() async {
    setState(() {
      _future = _loadActions();
    });
    await _future;
  }

  @override
  Widget build(BuildContext context) {
    final future = _future ??= _loadActions();
    return FutureBuilder<RepoActions>(
      future: future,
      builder: (context, snap) {
        final actions = snap.data ?? const RepoActions();
        final polling =
            _pollTimer != null || actions.runs.any((run) => run.isActive);
        return ListView(
          padding: const EdgeInsets.symmetric(vertical: 10),
          children: [
            Padding(
              padding: const EdgeInsets.fromLTRB(
                FmSpace.x4,
                FmSpace.x2,
                FmSpace.x4,
                FmSpace.x2,
              ),
              child: FmSectionHeader(
                title: 'Workflows',
                trailing: Row(
                  mainAxisSize: MainAxisSize.min,
                  children: [
                    if (snap.hasData)
                      Text(
                        '${actions.workflows.length} workflows · ${actions.runs.length} runs',
                        style: TextStyle(
                          color: FmTheme.textSecondary(context),
                          fontSize: 12,
                          fontWeight: FontWeight.w700,
                        ),
                      ),
                    if (polling) ...[
                      const SizedBox(width: FmSpace.x2),
                      const Text('Live refresh on'),
                    ],
                    const SizedBox(width: FmSpace.x1),
                    IconButton(
                      tooltip: 'Refresh actions',
                      visualDensity: VisualDensity.compact,
                      onPressed: _refresh,
                      icon: const Icon(Icons.refresh, size: 18),
                    ),
                  ],
                ),
              ),
            ),
            Padding(
              padding: const EdgeInsets.fromLTRB(
                FmSpace.x4,
                0,
                FmSpace.x4,
                FmSpace.x3,
              ),
              child: Text(
                'Desktop node runs workflows and owns rerun, cancel, approve, secrets, and artifacts. Mobile watches status and logs until signed desktop pairing is designed.',
                style: TextStyle(
                  color: FmTheme.textSecondary(context),
                  fontSize: 12,
                ),
              ),
            ),
            if (snap.connectionState == ConnectionState.waiting &&
                actions.workflows.isEmpty &&
                actions.runs.isEmpty)
              const Padding(
                padding: EdgeInsets.all(FmSpace.x4),
                child: _LoadingCard(label: 'Loading actions…'),
              )
            else if (snap.hasError)
              Padding(
                padding: const EdgeInsets.all(FmSpace.x4),
                child: _EmptyCard(message: '${snap.error}'),
              )
            else if (actions.workflows.isEmpty && actions.runs.isEmpty)
              const Padding(
                padding: EdgeInsets.all(FmSpace.x4),
                child: _EmptyCard(
                  message:
                      'No action snapshots have been pushed by the desktop node for this repo yet.',
                ),
              )
            else ...[
              for (final workflow in actions.workflows)
                _MobileCard(
                  icon: Icons.playlist_play_outlined,
                  iconColor: FmTheme.accent(context),
                  title: workflow.displayName,
                  subtitle: workflow.path,
                  chips: [
                    workflow.triggerLabel,
                    if (workflow.stepCount > 0)
                      workflow.stepCount == 1
                          ? '1 step'
                          : '${workflow.stepCount} steps',
                    if (workflow.manual) 'Manual run',
                    if (!workflow.valid) 'Invalid',
                  ],
                ),
              if (actions.runs.isNotEmpty)
                Padding(
                  padding: const EdgeInsets.fromLTRB(
                    FmSpace.x4,
                    FmSpace.x4,
                    FmSpace.x4,
                    FmSpace.x2,
                  ),
                  child: FmSectionHeader(title: 'Runs'),
                ),
              for (final run in actions.runs)
                _MobileCard(
                  icon: Icons.terminal_outlined,
                  iconColor: FmTheme.accent(context),
                  title: run.displayName,
                  subtitle: [
                    if (run.refLabel.isNotEmpty) run.refLabel,
                    if (run.shortCommit.isNotEmpty) run.shortCommit,
                  ].join(' • '),
                  trailing: _ActionStatusBadge(run: run),
                  onTap: () async {
                    final navigator = Navigator.of(context);
                    final proof = await _readProof(context);
                    if (!mounted) return;
                    navigator.push(
                      MaterialPageRoute<void>(
                        builder: (_) => _ActionRunDetailScreen(
                          api: widget.api,
                          repo: widget.repo,
                          run: run,
                          proof: proof,
                        ),
                      ),
                    );
                  },
                  chips: [
                    run.statusLabel,
                    if (run.refLabel.isNotEmpty) run.refLabel,
                    if (run.shortCommit.isNotEmpty) run.shortCommit,
                    if (run.durationLabel.isNotEmpty) run.durationLabel,
                    if (run.workflowPath.isNotEmpty) run.workflowPath,
                  ],
                ),
            ],
          ],
        );
      },
    );
  }
}

class _ActionRunDetailScreen extends StatelessWidget {
  const _ActionRunDetailScreen({
    required this.api,
    required this.repo,
    required this.run,
    required this.proof,
  });

  final ApiService api;
  final Repository repo;
  final ActionRun run;
  final _ActionReadProof proof;

  Future<void> _requestRerun(BuildContext context) async {
    await _requestDesktopCommand(
      context,
      api,
      repo,
      command: 'action.rerun',
      target: 'run:${run.id}',
      payload: {
        'runId': run.id,
        if (run.workflowPath.isNotEmpty) 'workflowPath': run.workflowPath,
      },
      successMessage:
          'Desktop request queued. Approve it on the Qt desktop node to rerun.',
    );
  }

  @override
  Widget build(BuildContext context) {
    final logFuture = api.actionLog(
      repo.owner,
      repo.name,
      run.id,
      ownerAccount: proof.ownerAccount,
      ts: proof.ts,
      sig: proof.sig,
    );
    return Scaffold(
      appBar: AppBar(title: Text('Action run #${run.id}')),
      body: ListView(
        padding: const EdgeInsets.all(20),
        children: [
          _InfoCard(
            title: run.displayName,
            children: [
              Wrap(
                spacing: 8,
                runSpacing: 8,
                children: [
                  _Chip(icon: Icons.info_outline, label: run.statusLabel),
                  if (run.refLabel.isNotEmpty)
                    _Chip(
                      icon: Icons.account_tree_outlined,
                      label: run.refLabel,
                    ),
                  if (run.shortCommit.isNotEmpty)
                    _Chip(icon: Icons.commit_outlined, label: run.shortCommit),
                  if (run.workflowPath.isNotEmpty)
                    _Chip(
                      icon: Icons.description_outlined,
                      label: run.workflowPath,
                    ),
                ],
              ),
              const SizedBox(height: 12),
              Text(
                'Desktop-controlled workflow. Mobile can inspect run output and queue signed requests; rerun, cancel, approval, and secret editing still require explicit approval on the desktop node.',
                style: TextStyle(color: FmTheme.textSecondary(context)),
              ),
              const SizedBox(height: 12),
              FilledButton.icon(
                onPressed: () => _requestRerun(context),
                icon: const Icon(Icons.replay_outlined, size: 18),
                label: const Text('Request rerun on desktop'),
              ),
            ],
          ),
          const SizedBox(height: 12),
          FutureBuilder<ActionLog>(
            future: logFuture,
            builder: (context, snap) {
              if (snap.connectionState == ConnectionState.waiting) {
                return const _LoadingCard(label: 'Loading action log…');
              }
              if (snap.hasError) {
                return _EmptyCard(message: '${snap.error}');
              }
              final detail =
                  snap.data ?? ActionLog(id: run.id, status: run.status);
              return _InfoCard(
                title: 'Run log',
                children: [
                  Wrap(
                    spacing: 8,
                    runSpacing: 8,
                    children: [
                      _Chip(
                        icon: Icons.info_outline,
                        label: detail.statusLabel,
                      ),
                      _Chip(icon: Icons.folder_outlined, label: repo.fullName),
                    ],
                  ),
                  const SizedBox(height: 12),
                  SelectableText(
                    detail.log.trim().isEmpty
                        ? 'No log has been pushed by the desktop node yet.'
                        : detail.log.trim(),
                    style: const TextStyle(
                      fontFamily: 'monospace',
                      fontSize: 12,
                      height: 1.45,
                    ),
                  ),
                ],
              );
            },
          ),
        ],
      ),
    );
  }
}

class _ActionStatusBadge extends StatelessWidget {
  const _ActionStatusBadge({required this.run});

  final ActionRun run;

  @override
  Widget build(BuildContext context) {
    final color = switch (run.statusLabel) {
      'Success' => FmTheme.success(context),
      'Failed' || 'Rejected' || 'Cancelled' => FmTheme.danger(context),
      'Running' || 'Queued' || 'Awaiting approval' => FmTheme.accent(context),
      _ => FmTheme.textTertiary(context),
    };
    return FmStatusBadge(label: run.statusLabel, color: color);
  }
}

class _AgentsTab extends StatefulWidget {
  const _AgentsTab({required this.api, required this.repo});

  final ApiService api;
  final Repository repo;

  @override
  State<_AgentsTab> createState() => _AgentsTabState();
}

class _AgentsTabState extends State<_AgentsTab> {
  static const _pollInterval = Duration(seconds: 10);

  Future<List<AgentSession>>? _future;
  Timer? _pollTimer;

  @override
  void didChangeDependencies() {
    super.didChangeDependencies();
    _future ??= _loadSessions();
  }

  @override
  void dispose() {
    _pollTimer?.cancel();
    super.dispose();
  }

  String _ownerAccount(BuildContext context) {
    final auth = context.read<AuthService?>();
    final node = auth?.session?.nodeName.trim().toLowerCase() ?? '';
    return node.isNotEmpty ? node : widget.repo.owner;
  }

  Future<void> _refresh() async {
    setState(() {
      _future = _loadSessions();
    });
    await _future;
  }

  Future<List<AgentSession>> _loadSessions() async {
    final items = await widget.api.agentSessions(
      widget.repo.owner,
      widget.repo.name,
      ownerAccount: _ownerAccount(context),
    );
    _syncPolling(items);
    return items;
  }

  void _syncPolling(List<AgentSession> items) {
    if (!mounted) return;
    if (items.any((session) => session.isActive)) {
      _pollTimer ??= Timer.periodic(_pollInterval, (_) {
        if (!mounted) return;
        setState(() {
          _future = _loadSessions();
        });
      });
    } else {
      _pollTimer?.cancel();
      _pollTimer = null;
    }
  }

  @override
  Widget build(BuildContext context) {
    final future = _future ??= _loadSessions();
    return FutureBuilder<List<AgentSession>>(
      future: future,
      builder: (context, snap) {
        final items = snap.data ?? const <AgentSession>[];
        final polling =
            _pollTimer != null || items.any((session) => session.isActive);
        return ListView(
          padding: const EdgeInsets.symmetric(vertical: 10),
          children: [
            Padding(
              padding: const EdgeInsets.fromLTRB(
                FmSpace.x4,
                FmSpace.x2,
                FmSpace.x4,
                FmSpace.x2,
              ),
              child: FmSectionHeader(
                title: 'Agent sessions',
                trailing: Row(
                  mainAxisSize: MainAxisSize.min,
                  children: [
                    if (snap.hasData)
                      Text(
                        '${items.length}',
                        style: TextStyle(
                          color: FmTheme.textSecondary(context),
                          fontSize: 12,
                          fontWeight: FontWeight.w700,
                        ),
                      ),
                    if (polling) ...[
                      const SizedBox(width: FmSpace.x2),
                      const Text('Live refresh on'),
                    ],
                    const SizedBox(width: FmSpace.x1),
                    IconButton(
                      tooltip: 'Refresh agent sessions',
                      visualDensity: VisualDensity.compact,
                      onPressed: _refresh,
                      icon: const Icon(Icons.refresh, size: 18),
                    ),
                  ],
                ),
              ),
            ),
            if (snap.connectionState == ConnectionState.waiting &&
                items.isEmpty)
              const Padding(
                padding: EdgeInsets.all(FmSpace.x4),
                child: _LoadingCard(label: 'Loading agent sessions…'),
              )
            else if (snap.hasError)
              Padding(
                padding: const EdgeInsets.all(FmSpace.x4),
                child: _EmptyCard(message: '${snap.error}'),
              )
            else if (items.isEmpty)
              const Padding(
                padding: EdgeInsets.all(FmSpace.x4),
                child: _EmptyCard(
                  message:
                      'No agent sessions have been pushed by the desktop node for this repo yet.',
                ),
              )
            else
              for (final session in items)
                _MobileCard(
                  icon: Icons.smart_toy_outlined,
                  iconColor: FmTheme.accent(context),
                  title: session.displayTitle,
                  subtitle: session.issueNumber > 0
                      ? 'Issue #${session.issueNumber} • ${session.providerLabel}'
                      : session.providerLabel,
                  trailing: _AgentStatusBadge(session: session),
                  onTap: () => Navigator.of(context).push(
                    MaterialPageRoute<void>(
                      builder: (_) => _AgentSessionDetailScreen(
                        api: widget.api,
                        repo: widget.repo,
                        session: session,
                        ownerAccount: _ownerAccount(context),
                      ),
                    ),
                  ),
                  chips: [
                    if (session.issueNumber > 0) '#${session.issueNumber}',
                    session.providerLabel,
                    session.statusLabel,
                    if (session.branchName.isNotEmpty) session.branchName,
                    if (session.numTurns > 0) '${session.numTurns} turns',
                    if (session.durationLabel.isNotEmpty) session.durationLabel,
                    if (session.costLabel.isNotEmpty) session.costLabel,
                    if (session.prLabel.isNotEmpty) session.prLabel,
                    if (session.diffLabel.isNotEmpty) session.diffLabel,
                    if (session.conflicted) 'Conflict',
                    if (session.tokenLabel.isNotEmpty) session.tokenLabel,
                  ],
                ),
          ],
        );
      },
    );
  }
}

class _AgentSessionDetailScreen extends StatelessWidget {
  const _AgentSessionDetailScreen({
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
        padding: const EdgeInsets.all(20),
        children: [
          _InfoCard(
            title: session.displayTitle,
            children: [
              Wrap(
                spacing: 8,
                runSpacing: 8,
                children: [
                  _Chip(
                    icon: Icons.smart_toy_outlined,
                    label: session.providerLabel,
                  ),
                  _Chip(icon: Icons.info_outline, label: session.statusLabel),
                  if (session.model.isNotEmpty)
                    _Chip(icon: Icons.memory_outlined, label: session.model),
                  if (session.issueNumber > 0)
                    _Chip(
                      icon: Icons.error_outline,
                      label: '#${session.issueNumber}',
                    ),
                  if (session.branchName.isNotEmpty)
                    _Chip(
                      icon: Icons.account_tree_outlined,
                      label: session.branchName,
                    ),
                  if (session.numTurns > 0)
                    _Chip(
                      icon: Icons.chat_bubble_outline,
                      label: '${session.numTurns} turns',
                    ),
                  if (session.durationLabel.isNotEmpty)
                    _Chip(
                      icon: Icons.timer_outlined,
                      label: session.durationLabel,
                    ),
                  if (session.costLabel.isNotEmpty)
                    _Chip(icon: Icons.paid_outlined, label: session.costLabel),
                  if (session.prLabel.isNotEmpty)
                    _Chip(
                      icon: Icons.call_merge_outlined,
                      label: session.prLabel,
                    ),
                  if (session.diffLabel.isNotEmpty)
                    _Chip(
                      icon: Icons.difference_outlined,
                      label: session.diffLabel,
                    ),
                  if (session.conflicted)
                    _Chip(
                      icon: Icons.warning_amber_outlined,
                      label: 'Conflict',
                    ),
                  if (session.tokenLabel.isNotEmpty)
                    _Chip(
                      icon: Icons.data_usage_outlined,
                      label: session.tokenLabel,
                    ),
                ],
              ),
              const SizedBox(height: 12),
              Text(
                'Desktop-controlled session. Mobile can watch progress and open results; start, stop, retry, resume, merge, and apply stay on the desktop node until signed pairing is designed.',
                style: TextStyle(color: FmTheme.textSecondary(context)),
              ),
              if (session.lastError.isNotEmpty) ...[
                const SizedBox(height: 12),
                Text(
                  session.lastError,
                  style: TextStyle(color: FmTheme.danger(context)),
                ),
              ],
            ],
          ),
          const SizedBox(height: 12),
          FutureBuilder<AgentTranscript>(
            future: transcriptFuture,
            builder: (context, snap) {
              if (snap.connectionState == ConnectionState.waiting) {
                return const _LoadingCard(label: 'Loading transcript…');
              }
              if (snap.hasError) {
                return _EmptyCard(message: '${snap.error}');
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
              return _InfoCard(
                title: 'Transcript',
                children: [
                  Wrap(
                    spacing: 8,
                    runSpacing: 8,
                    children: [
                      _Chip(icon: Icons.info_outline, label: status),
                      _Chip(icon: Icons.folder_outlined, label: repo.fullName),
                    ],
                  ),
                  const SizedBox(height: 12),
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
              );
            },
          ),
        ],
      ),
    );
  }
}

class _AgentStatusBadge extends StatelessWidget {
  const _AgentStatusBadge({required this.session});

  final AgentSession session;

  @override
  Widget build(BuildContext context) {
    final color = switch (session.statusLabel) {
      'Merged' || 'Done' => FmTheme.success(context),
      'Failed' || 'Stopped' => FmTheme.danger(context),
      'Running' || 'Queued' || 'Waiting' => FmTheme.accent(context),
      _ => FmTheme.textTertiary(context),
    };
    return FmStatusBadge(label: session.statusLabel, color: color);
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
  late String _selectedRef = widget.repo.defaultBranch;
  late final Future<List<RepoBranch>> _branchesFuture = widget.api.branches(
    widget.repo.owner,
    widget.repo.name,
  );
  late Future<RepoTree> _future = _loadTree('');

  Future<RepoTree> _loadTree(String path) => widget.api.tree(
    widget.repo.owner,
    widget.repo.name,
    path: path,
    ref: _selectedRef,
  );

  void _openDir(String path, {bool refresh = false}) {
    if (refresh) {
      widget.api.clearRepoCache(widget.repo.owner, widget.repo.name);
    }
    setState(() {
      _path = path;
      _future = _loadTree(path);
    });
  }

  void _switchRef(String ref) {
    if (ref == _selectedRef) return;
    widget.api.clearRepoCache(widget.repo.owner, widget.repo.name);
    setState(() {
      _selectedRef = ref;
      _path = '';
      _future = _loadTree('');
    });
  }

  void _openFilePath(String path) {
    Navigator.of(context).push(
      MaterialPageRoute(
        builder: (_) => RepoFileScreen(
          api: widget.api,
          repo: widget.repo,
          path: path,
          ref: _selectedRef,
        ),
      ),
    );
  }

  void _showGoToFile() {
    showModalBottomSheet<void>(
      context: context,
      useSafeArea: true,
      isScrollControlled: true,
      backgroundColor: Colors.transparent,
      builder: (_) => _GoToFileSheet(
        api: widget.api,
        repo: widget.repo,
        ref: _selectedRef,
        onFileSelected: _openFilePath,
      ),
    );
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
            _BranchSelector(
              selectedRef: _selectedRef,
              branchesFuture: _branchesFuture,
              onSelected: _switchRef,
            ),
            const SizedBox(height: 10),
            _PathBar(
              path: _path,
              onUp: _path.isEmpty ? null : _up,
              onPath: _openDir,
              onGoToFile: _showGoToFile,
            ),
            if (!loading && _sourceLabel(tree?.source ?? '') != null) ...[
              const SizedBox(height: 10),
              Align(
                alignment: Alignment.centerLeft,
                child: _SourceChip(source: tree!.source),
              ),
            ],
            const SizedBox(height: 10),
            if (loading)
              const _LoadingCard(label: 'Loading files…')
            else if (snap.hasError)
              _ErrorCard(
                message: 'Could not load files: ${snap.error}',
                onRetry: () => _openDir(_path, refresh: true),
              )
            else if (tree == null || tree.entries.isEmpty)
              const _EmptyCard(message: 'No files found in this folder.')
            else
              _FileList(
                entries: tree.entries,
                onDir: _openDir,
                onFile: (entry) => _openFilePath(entry.path),
              ),
          ],
        );
      },
    );
  }
}

class _BranchSelector extends StatelessWidget {
  const _BranchSelector({
    required this.selectedRef,
    required this.branchesFuture,
    required this.onSelected,
  });

  final String selectedRef;
  final Future<List<RepoBranch>> branchesFuture;
  final ValueChanged<String> onSelected;

  @override
  Widget build(BuildContext context) {
    return FutureBuilder<List<RepoBranch>>(
      future: branchesFuture,
      builder: (context, snap) {
        final branchNames = <String>{
          if (selectedRef.isNotEmpty) selectedRef,
          for (final branch in snap.data ?? const <RepoBranch>[])
            if (branch.name.isNotEmpty) branch.name,
        }.toList();
        final canSwitch = branchNames.isNotEmpty;
        return PopupMenuButton<String>(
          tooltip: 'Switch branch',
          enabled: canSwitch,
          onSelected: onSelected,
          itemBuilder: (context) => [
            for (final name in branchNames)
              PopupMenuItem<String>(
                value: name,
                child: Row(
                  children: [
                    Icon(
                      name == selectedRef
                          ? Icons.check_circle
                          : Icons.circle_outlined,
                      size: 16,
                      color: name == selectedRef
                          ? FmTheme.accent(context)
                          : FmTheme.textTertiary(context),
                    ),
                    const SizedBox(width: FmSpace.x2),
                    Expanded(
                      child: Text(name, overflow: TextOverflow.ellipsis),
                    ),
                  ],
                ),
              ),
          ],
          child: _BranchPill(
            selectedRef: selectedRef,
            canSwitch: canSwitch,
            loading: snap.connectionState == ConnectionState.waiting,
            hasError: snap.hasError,
          ),
        );
      },
    );
  }
}

class _BranchPill extends StatelessWidget {
  const _BranchPill({
    required this.selectedRef,
    required this.canSwitch,
    required this.loading,
    required this.hasError,
  });

  final String selectedRef;
  final bool canSwitch;
  final bool loading;
  final bool hasError;

  @override
  Widget build(BuildContext context) {
    final muted = !canSwitch || loading || hasError;
    return FmCard(
      radius: FmRadius.lg,
      padding: const EdgeInsets.symmetric(
        horizontal: FmSpace.x3,
        vertical: FmSpace.x2,
      ),
      child: Row(
        mainAxisSize: MainAxisSize.min,
        children: [
          Icon(
            Icons.account_tree_outlined,
            size: 17,
            color: muted
                ? FmTheme.textTertiary(context)
                : FmTheme.accent(context),
          ),
          const SizedBox(width: FmSpace.x2),
          Text(
            'Branch: ${selectedRef.isEmpty ? 'Default branch' : selectedRef}',
            style: TextStyle(
              color: FmTheme.textPrimary(context),
              fontWeight: FontWeight.w800,
              fontSize: 13,
            ),
          ),
          const SizedBox(width: FmSpace.x1),
          if (loading)
            SizedBox(
              width: 14,
              height: 14,
              child: CircularProgressIndicator(
                strokeWidth: 2,
                color: FmTheme.textTertiary(context),
              ),
            )
          else
            Icon(
              canSwitch
                  ? Icons.keyboard_arrow_down_rounded
                  : Icons.lock_outline,
              size: 17,
              color: FmTheme.textTertiary(context),
            ),
        ],
      ),
    );
  }
}

class RepoFileScreen extends StatelessWidget {
  const RepoFileScreen({
    super.key,
    required this.api,
    required this.repo,
    required this.path,
    this.ref = '',
  });
  final ApiService api;
  final Repository repo;
  final String path;
  final String ref;

  @override
  Widget build(BuildContext context) {
    final rawUrl = api.rawUri(repo.owner, repo.name, path, ref: ref).toString();
    Future<void> copy(String label, String value) async {
      await Clipboard.setData(ClipboardData(text: value));
      if (!context.mounted) return;
      ScaffoldMessenger.of(
        context,
      ).showSnackBar(SnackBar(content: Text('$label copied')));
    }

    return Scaffold(
      appBar: AppBar(
        title: Text(path.split('/').last, overflow: TextOverflow.ellipsis),
        actions: [
          IconButton(
            tooltip: 'Copy path',
            onPressed: () => copy('Path', path),
            icon: const Icon(Icons.copy_all_outlined),
          ),
          IconButton(
            tooltip: 'Copy raw URL',
            onPressed: () => copy('Raw URL', rawUrl),
            icon: const Icon(Icons.link),
          ),
        ],
      ),
      body: FutureBuilder<RepoBlob>(
        future: api.blob(repo.owner, repo.name, path, ref: ref),
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
          return _FilePreview(api: api, repo: repo, blob: blob, ref: ref);
        },
      ),
    );
  }
}

class _FilePreview extends StatelessWidget {
  const _FilePreview({
    required this.api,
    required this.repo,
    required this.blob,
    required this.ref,
  });

  final ApiService api;
  final Repository repo;
  final RepoBlob blob;
  final String ref;

  @override
  Widget build(BuildContext context) {
    final path = blob.path;
    final rawUrl = api.rawUri(repo.owner, repo.name, path, ref: ref).toString();
    final kind = _previewKind(path);
    if (kind == _PreviewKind.image) {
      return ColoredBox(
        color: FmTheme.bgBase(context),
        child: Column(
          children: [
            _FilePreviewHeader(path: path, source: blob.source),
            Expanded(
              child: Center(
                child: InteractiveViewer(
                  minScale: .5,
                  maxScale: 5,
                  child: Image.network(
                    rawUrl,
                    fit: BoxFit.contain,
                    errorBuilder: (_, error, _) => _UnsupportedPreview(
                      path: path,
                      message:
                          'Could not render this image preview. ${error.toString()}',
                      rawUrl: rawUrl,
                      source: blob.source,
                    ),
                  ),
                ),
              ),
            ),
          ],
        ),
      );
    }
    if (kind == _PreviewKind.video) {
      return _UnsupportedPreview(
        path: path,
        message:
            'Video preview is not bundled in the mobile app yet. Use the raw file URL below to open or download it.',
        rawUrl: rawUrl,
        source: blob.source,
      );
    }
    final decoded = _decodedText(blob);
    if (decoded == null) {
      return _UnsupportedPreview(
        path: path,
        message:
            'This looks like a binary file, so ForkMesh is not showing it as text.',
        rawUrl: rawUrl,
        source: blob.source,
      );
    }
    return _CodePreview(
      path: path,
      content: decoded.isEmpty ? 'Empty file.' : decoded,
      source: blob.source,
    );
  }

  String? _decodedText(RepoBlob blob) {
    final content = blob.content;
    if (blob.encoding.toLowerCase() == 'base64') {
      try {
        final bytes = base64.decode(content.replaceAll(RegExp(r'\s+'), ''));
        if (bytes.take(512).contains(0)) return null;
        return utf8.decode(bytes, allowMalformed: true);
      } catch (_) {
        return null;
      }
    }
    if (content.runes.take(512).contains(0)) return null;
    return content;
  }
}

enum _PreviewKind { text, image, video }

_PreviewKind _previewKind(String path) {
  final lower = path.toLowerCase();
  if (lower.endsWith('.png') ||
      lower.endsWith('.jpg') ||
      lower.endsWith('.jpeg') ||
      lower.endsWith('.gif') ||
      lower.endsWith('.webp') ||
      lower.endsWith('.bmp')) {
    return _PreviewKind.image;
  }
  if (lower.endsWith('.mp4') ||
      lower.endsWith('.mov') ||
      lower.endsWith('.webm') ||
      lower.endsWith('.mkv')) {
    return _PreviewKind.video;
  }
  return _PreviewKind.text;
}

class _UnsupportedPreview extends StatelessWidget {
  const _UnsupportedPreview({
    required this.path,
    required this.message,
    this.rawUrl = '',
    this.source = '',
  });

  final String path;
  final String message;
  final String rawUrl;
  final String source;

  @override
  Widget build(BuildContext context) => ColoredBox(
    color: FmTheme.bgBase(context),
    child: SafeArea(
      child: SingleChildScrollView(
        padding: const EdgeInsets.all(FmSpace.x5),
        child: _InfoCard(
          title: path.split('/').last,
          children: [
            if (_sourceLabel(source) != null) ...[
              Align(
                alignment: Alignment.centerLeft,
                child: _SourceChip(source: source),
              ),
              const SizedBox(height: FmSpace.x3),
            ],
            Text(
              message,
              style: TextStyle(color: FmTheme.textSecondary(context)),
            ),
            const SizedBox(height: FmSpace.x3),
            _PreviewValue(label: 'Path', value: path),
            if (rawUrl.isNotEmpty) ...[
              const SizedBox(height: FmSpace.x3),
              _PreviewValue(label: 'Raw URL', value: rawUrl, accent: true),
            ],
          ],
        ),
      ),
    ),
  );
}

class _PreviewValue extends StatelessWidget {
  const _PreviewValue({
    required this.label,
    required this.value,
    this.accent = false,
  });

  final String label;
  final String value;
  final bool accent;

  @override
  Widget build(BuildContext context) => Column(
    crossAxisAlignment: CrossAxisAlignment.start,
    children: [
      Text(
        label,
        style: TextStyle(
          color: FmTheme.textTertiary(context),
          fontSize: 11,
          fontWeight: FontWeight.w800,
        ),
      ),
      const SizedBox(height: FmSpace.x1),
      Container(
        width: double.infinity,
        padding: const EdgeInsets.symmetric(
          horizontal: FmSpace.x3,
          vertical: FmSpace.x2,
        ),
        decoration: BoxDecoration(
          color: FmTheme.bgBase(context),
          borderRadius: BorderRadius.circular(FmRadius.sm),
          border: Border.all(color: FmTheme.border(context)),
        ),
        child: SelectableText(
          value,
          style: TextStyle(
            color: accent
                ? FmTheme.accent(context)
                : FmTheme.textSecondary(context),
            fontFamily: accent ? null : 'monospace',
            fontSize: 12,
          ),
        ),
      ),
    ],
  );
}

class _FilePreviewHeader extends StatelessWidget {
  const _FilePreviewHeader({required this.path, this.source = ''});

  final String path;
  final String source;

  @override
  Widget build(BuildContext context) {
    final editorBg = FmTheme.isDark(context)
        ? FmColors.darkBgRaised
        : FmColors.bgRaised;
    return Container(
      constraints: const BoxConstraints(minHeight: 38),
      padding: const EdgeInsets.symmetric(
        horizontal: FmSpace.x4,
        vertical: FmSpace.x2,
      ),
      decoration: BoxDecoration(
        color: editorBg,
        border: Border(bottom: BorderSide(color: FmTheme.border(context))),
      ),
      child: Row(
        children: [
          Icon(
            Icons.description_outlined,
            size: 17,
            color: FmTheme.textTertiary(context),
          ),
          const SizedBox(width: FmSpace.x2),
          Expanded(
            child: Text(
              path,
              maxLines: 1,
              overflow: TextOverflow.ellipsis,
              style: TextStyle(
                color: FmTheme.textSecondary(context),
                fontSize: 12,
                fontWeight: FontWeight.w700,
              ),
            ),
          ),
          if (_sourceLabel(source) != null) ...[
            const SizedBox(width: FmSpace.x2),
            Flexible(child: _SourceChip(source: source)),
          ],
        ],
      ),
    );
  }
}

class _CodePreview extends StatelessWidget {
  const _CodePreview({
    required this.path,
    required this.content,
    this.source = '',
  });

  final String path;
  final String content;
  final String source;

  @override
  Widget build(BuildContext context) {
    final lines = const LineSplitter().convert(content);
    final editorBg = FmTheme.isDark(context)
        ? FmColors.darkBgRaised
        : FmColors.bgRaised;
    final gutterBg = FmTheme.isDark(context)
        ? FmColors.editorGutter
        : FmColors.bgOverlay;
    final currentLineBg = FmTheme.isDark(context)
        ? FmColors.editorCurrentLine
        : FmColors.bgOverlay.withValues(alpha: .72);
    final longestLine = lines.fold<int>(
      0,
      (max, line) => line.length > max ? line.length : max,
    );
    final estimatedWidth = (longestLine * 8.2) + 72;

    return ColoredBox(
      color: FmTheme.bgBase(context),
      child: Column(
        children: [
          _FilePreviewHeader(path: path, source: source),
          Expanded(
            child: LayoutBuilder(
              builder: (context, constraints) {
                return Scrollbar(
                  child: SingleChildScrollView(
                    scrollDirection: Axis.horizontal,
                    child: SizedBox(
                      width: estimatedWidth < constraints.maxWidth
                          ? constraints.maxWidth
                          : estimatedWidth,
                      child: ListView.builder(
                        padding: EdgeInsets.zero,
                        itemCount: lines.length,
                        itemBuilder: (context, index) => _CodeLine(
                          lineNumber: index + 1,
                          line: lines[index],
                          gutterBg: gutterBg,
                          editorBg: editorBg,
                          currentLineBg: index == 0 ? currentLineBg : null,
                        ),
                      ),
                    ),
                  ),
                );
              },
            ),
          ),
        ],
      ),
    );
  }
}

class _CodeLine extends StatelessWidget {
  const _CodeLine({
    required this.lineNumber,
    required this.line,
    required this.gutterBg,
    required this.editorBg,
    this.currentLineBg,
  });

  final int lineNumber;
  final String line;
  final Color gutterBg;
  final Color editorBg;
  final Color? currentLineBg;

  @override
  Widget build(BuildContext context) {
    final baseStyle = TextStyle(
      color: FmTheme.isDark(context)
          ? const Color(0xFFD7D9E0)
          : FmTheme.textPrimary(context),
      fontFamily: 'monospace',
      fontSize: 13,
      height: 1.45,
      letterSpacing: 0,
    );
    return ColoredBox(
      color: currentLineBg ?? editorBg,
      child: Row(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          Container(
            width: 54,
            color: gutterBg,
            padding: const EdgeInsets.only(right: FmSpace.x3, top: 3),
            child: Text(
              '$lineNumber',
              textAlign: TextAlign.right,
              style: TextStyle(
                color: FmTheme.textTertiary(context),
                fontFamily: 'monospace',
                fontSize: 13,
                height: 1.45,
              ),
            ),
          ),
          Expanded(
            child: Padding(
              padding: const EdgeInsets.fromLTRB(FmSpace.x3, 3, FmSpace.x4, 3),
              child: SelectableText.rich(
                TextSpan(children: _highlightCodeLine(context, line)),
                maxLines: 1,
                style: baseStyle,
              ),
            ),
          ),
        ],
      ),
    );
  }
}

String? _sourceLabel(String source) {
  final value = source.trim();
  if (value.isEmpty) return null;
  final lower = value.toLowerCase();
  if (lower == 'live' || lower == 'live_source' || lower == 'live-source') {
    return 'live source';
  }
  if (lower == 'live mirror' ||
      lower == 'live_mirror' ||
      lower == 'live-mirror') {
    return 'live mirror';
  }
  if (lower.startsWith('served by ') || lower.startsWith('live ')) {
    return value;
  }
  return 'served by $value';
}

List<TextSpan> _highlightCodeLine(BuildContext context, String line) {
  final baseColor = FmTheme.isDark(context)
      ? const Color(0xFFD7D9E0)
      : FmTheme.textPrimary(context);
  final base = TextStyle(color: baseColor);
  final keyword = TextStyle(color: FmColors.codeKeyword);
  final type = TextStyle(color: FmColors.codeType);
  final string = TextStyle(color: FmColors.codeString);
  final comment = TextStyle(
    color: FmColors.codeComment,
    fontStyle: FontStyle.italic,
  );
  final number = TextStyle(color: FmColors.codeNumber);
  final function = TextStyle(color: FmColors.codeFunction);
  final tokenPattern = RegExp(
    r"""("[^"\\]*(?:\\.[^"\\]*)*"|'[^'\\]*(?:\\.[^'\\]*)*'|`[^`]*`|//.*|\b(import|from|export|class|function|const|final|return|if|else|for|while|await|async|try|catch|void|Future|Widget|State|extends|required|super|new|var|let|String|int|double|bool)\b|\b\d+(?:\.\d+)?\b|[A-Za-z_][A-Za-z0-9_]*(?=\())""",
  );
  final spans = <TextSpan>[];
  var cursor = 0;
  for (final match in tokenPattern.allMatches(line)) {
    if (match.start > cursor) {
      spans.add(
        TextSpan(text: line.substring(cursor, match.start), style: base),
      );
    }
    final token = match.group(0) ?? '';
    TextStyle style;
    if (token.startsWith('//')) {
      style = comment;
    } else if (token.startsWith('"') ||
        token.startsWith("'") ||
        token.startsWith('`')) {
      style = string;
    } else if (RegExp(r'^\d').hasMatch(token)) {
      style = number;
    } else if (RegExp(
      r'^(Future|Widget|State|String|int|double|bool)$',
    ).hasMatch(token)) {
      style = type;
    } else if (match.group(2) != null) {
      style = keyword;
    } else {
      style = function;
    }
    spans.add(TextSpan(text: token, style: style));
    cursor = match.end;
    if (token.startsWith('//')) break;
  }
  if (cursor < line.length) {
    spans.add(TextSpan(text: line.substring(cursor), style: base));
  }
  if (spans.isEmpty) return [TextSpan(text: line, style: base)];
  return spans;
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
        iconColor: issue.isOpen
            ? FmTheme.success(context)
            : FmColors.accentEdge,
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
            : (pr.status == 'closed'
                  ? FmTheme.danger(context)
                  : FmTheme.success(context)),
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

class _IssueDetailScreen extends StatefulWidget {
  const _IssueDetailScreen({required this.repo, required this.issue});

  final Repository repo;
  final Issue issue;

  @override
  State<_IssueDetailScreen> createState() => _IssueDetailScreenState();
}

class _IssueDetailScreenState extends State<_IssueDetailScreen> {
  late Issue _issue = widget.issue;
  late final TextEditingController _bountyAmount = TextEditingController(
    text: (_issue.bountyUsd > 0 ? _issue.bountyUsd : 1.0).toStringAsFixed(2),
  );
  bool _bountyBusy = false;
  String _bountyMessage = '';

  Repository get repo => widget.repo;

  @override
  void dispose() {
    _bountyAmount.dispose();
    super.dispose();
  }

  Future<void> _refreshBounty() async {
    if (_bountyBusy) return;
    setState(() {
      _bountyBusy = true;
      _bountyMessage = '';
    });
    try {
      final bounty = await context.read<ApiService>().issueBountyStatus(
        repo.owner,
        repo.name,
        _issue.number,
      );
      if (!mounted) return;
      setState(() {
        if (bounty.hasFunding) {
          _issue = _issue.copyWithBounty(bounty);
          _bountyMessage = 'Bounty status refreshed.';
        } else {
          _bountyMessage = 'No Worker bounty deposit has been prepared yet.';
        }
      });
    } catch (error) {
      if (!mounted) return;
      setState(() => _bountyMessage = 'Refresh failed: $error');
    } finally {
      if (mounted) setState(() => _bountyBusy = false);
    }
  }

  Future<void> _prepareBountyDeposit() async {
    if (_bountyBusy) return;
    final auth = context.read<AuthService?>();
    final identity = context.read<Identity?>();
    final api = context.read<ApiService>();
    final session = auth?.session;
    final account = session?.nodeName.trim().toLowerCase() ?? '';
    final amount = double.tryParse(_bountyAmount.text.trim());
    if (amount == null || amount <= 0) {
      setState(() => _bountyMessage = 'Enter a funding amount above \$0.');
      return;
    }
    try {
      if (session == null || identity == null || account.isEmpty) {
        throw Exception(
          'Sign in with the repo owner account to prepare a funding deposit.',
        );
      }
      if (account != repo.owner.trim().toLowerCase()) {
        throw Exception(
          'Only the repo owner can prepare a funding deposit for this repo.',
        );
      }
      if (session.pubkey.trim() != identity.publicKeyB64url) {
        throw Exception(
          'Pair this mobile device with the owner key before preparing a funding deposit.',
        );
      }
      final rawPayee = _issue.bountyPayee.trim();
      if (rawPayee.isEmpty) {
        throw Exception(
          'Publish or refresh a bounty payee before preparing a funding deposit.',
        );
      }
      final payeeIsAddress = _looksLikeSolanaAddress(rawPayee);
      final payeeId = payeeIsAddress ? rawPayee : rawPayee.toLowerCase();
      final ts = DateTime.now().millisecondsSinceEpoch.toString();
      final canonical =
          'forkmesh-bounty-create-v1\n${repo.owner}\n${repo.name}\n${_issue.number}\n$payeeId\n$ts';
      setState(() {
        _bountyBusy = true;
        _bountyMessage = '';
      });
      final sig = await identity.sign(utf8.encode(canonical));
      final bounty = await api.createIssueBounty(
        repo.owner,
        repo.name,
        number: _issue.number,
        amountUsd: amount,
        payee: payeeIsAddress ? payeeId : '',
        payeeNode: payeeIsAddress ? '' : payeeId,
        ts: ts,
        sig: sig,
      );
      if (!mounted) return;
      setState(() {
        _issue = _issue.copyWithBounty(bounty);
        _bountyMessage =
            'Deposit address prepared. Fund it externally from a Solana wallet.';
      });
    } catch (error) {
      if (!mounted) return;
      setState(() => _bountyMessage = '$error');
    } finally {
      if (mounted) setState(() => _bountyBusy = false);
    }
  }

  String _fundingGateCopy(BuildContext context) {
    final auth = context.watch<AuthService?>();
    final identity = context.watch<Identity?>();
    final session = auth?.session;
    final account = session?.nodeName.trim().toLowerCase() ?? '';
    if (session == null || identity == null || account.isEmpty) {
      return 'Sign in with the repo owner account and local owner key to prepare funding deposits.';
    }
    if (account != repo.owner.trim().toLowerCase()) {
      return 'Signed in as $account. Only ${repo.owner} can prepare funding deposits.';
    }
    if (session.pubkey.trim() != identity.publicKeyB64url) {
      return 'Owner account is present, but this mobile identity is not the owner key.';
    }
    if (_issue.bountyPayee.trim().isEmpty) {
      return 'Publish or refresh a bounty payee before preparing a funding deposit.';
    }
    return '';
  }

  @override
  Widget build(BuildContext context) {
    final issue = _issue;
    return Scaffold(
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
                  for (final label in issue.labels)
                    _Chip(icon: Icons.sell_outlined, label: label),
                  if (issue.priority > 0)
                    _Chip(
                      icon: Icons.priority_high_outlined,
                      label: 'priority ${issue.priority}',
                    ),
                  if (issue.milestone.isNotEmpty)
                    _Chip(icon: Icons.flag_outlined, label: issue.milestone),
                  for (final assignee in issue.assignees)
                    _Chip(icon: Icons.assignment_ind_outlined, label: assignee),
                  if (issue.votes > 0)
                    _Chip(
                      icon: Icons.how_to_vote_outlined,
                      label:
                          '${issue.votes} ${issue.votes == 1 ? 'vote' : 'votes'}',
                    ),
                  if (issue.bountyUsd > 0)
                    _Chip(
                      icon: Icons.attach_money,
                      label: '${_formatUsd(issue.bountyUsd)} bounty',
                    ),
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
            title: 'Timeline',
            children: [
              _IssueTimeline(issue: issue),
              const SizedBox(height: 10),
              const Text(_threadSubscriptionNote),
              const SizedBox(height: 14),
              _IssueQuickActions(repo: repo, issue: issue),
            ],
          ),
          const SizedBox(height: 12),
          _BountyFundingCard(
            issue: issue,
            amountController: _bountyAmount,
            busy: _bountyBusy,
            message: _bountyMessage,
            gateCopy: _fundingGateCopy(context),
            onRefresh: _refreshBounty,
            onPrepare: _prepareBountyDeposit,
          ),
        ],
      ),
    );
  }
}

class _BountyFundingCard extends StatelessWidget {
  const _BountyFundingCard({
    required this.issue,
    required this.amountController,
    required this.busy,
    required this.message,
    required this.gateCopy,
    required this.onRefresh,
    required this.onPrepare,
  });

  final Issue issue;
  final TextEditingController amountController;
  final bool busy;
  final String message;
  final String gateCopy;
  final VoidCallback onRefresh;
  final VoidCallback onPrepare;

  @override
  Widget build(BuildContext context) {
    final canPrepare = gateCopy.isEmpty && !busy;
    final addressExplorerUrl = solanaExplorerAddressUrl(issue.bountyAddress);
    final payoutExplorerUrl = solanaExplorerSignatureUrl(issue.bountyPayoutSig);
    return _InfoCard(
      title: 'Bounty funding',
      children: [
        if (issue.bountyUsd > 0)
          Text(
            '${_formatUsd(issue.bountyUsd)} pledged',
            style: TextStyle(
              color: FmTheme.textPrimary(context),
              fontWeight: FontWeight.w800,
            ),
          )
        else
          Text(
            'No pledge amount published yet.',
            style: TextStyle(color: FmTheme.textSecondary(context)),
          ),
        const SizedBox(height: 8),
        Text('Status: ${issue.bountyStatusLabel}'),
        if (issue.bountyProgressLabel.isNotEmpty) ...[
          const SizedBox(height: 8),
          Text(issue.bountyProgressLabel),
        ],
        if (issue.bountyAddress.isNotEmpty) ...[
          const SizedBox(height: 12),
          _CopyableValue(
            label: 'Deposit address',
            value: issue.bountyAddress,
            copyLabel: 'Bounty deposit address',
            copyKey: const Key('issue-bounty-copy-address'),
          ),
          if (addressExplorerUrl.isNotEmpty) ...[
            const SizedBox(height: 8),
            _CopyableValue(
              label: 'Address explorer URL',
              value: addressExplorerUrl,
              copyLabel: 'Bounty address explorer URL',
              copyKey: const Key('issue-bounty-copy-address-explorer'),
            ),
          ],
        ],
        if (issue.bountyPayUri.isNotEmpty) ...[
          const SizedBox(height: 8),
          _CopyableValue(
            label: 'Payment URI',
            value: issue.bountyPayUri,
            copyLabel: 'Bounty payment URI',
            copyKey: const Key('issue-bounty-copy-pay-uri'),
          ),
        ],
        if (issue.bountyPayoutSig.isNotEmpty) ...[
          const SizedBox(height: 12),
          _CopyableValue(
            label: 'Payout signature',
            value: issue.bountyPayoutSig,
            copyLabel: 'Bounty payout signature',
            copyKey: const Key('issue-bounty-copy-payout-sig'),
          ),
          if (payoutExplorerUrl.isNotEmpty) ...[
            const SizedBox(height: 8),
            _CopyableValue(
              label: 'Payout transaction explorer URL',
              value: payoutExplorerUrl,
              copyLabel: 'Bounty payout explorer URL',
              copyKey: const Key('issue-bounty-copy-payout-explorer'),
            ),
          ],
        ],
        const SizedBox(height: 12),
        Text(
          'Worker-custodied Solana escrow. Desktop and owner-key flows apply or pay bounties; mobile only prepares a deposit address and shows public funding state.',
          style: TextStyle(
            color: FmTheme.textSecondary(context),
            fontSize: 12,
            height: 1.35,
          ),
        ),
        const SizedBox(height: 14),
        TextField(
          controller: amountController,
          keyboardType: const TextInputType.numberWithOptions(decimal: true),
          decoration: InputDecoration(
            labelText: 'Funding amount USD',
            filled: true,
            fillColor: FmTheme.bgBase(context),
            contentPadding: const EdgeInsets.symmetric(
              horizontal: FmSpace.x3,
              vertical: FmSpace.x2,
            ),
            border: OutlineInputBorder(
              borderRadius: BorderRadius.circular(FmRadius.md),
              borderSide: BorderSide.none,
            ),
          ),
        ),
        const SizedBox(height: 12),
        Wrap(
          spacing: 10,
          runSpacing: 10,
          crossAxisAlignment: WrapCrossAlignment.center,
          children: [
            OutlinedButton.icon(
              key: const Key('issue-bounty-refresh-status'),
              onPressed: busy ? null : onRefresh,
              icon: const Icon(Icons.refresh, size: 18),
              label: const Text('Refresh bounty status'),
            ),
            FilledButton.icon(
              key: const Key('issue-bounty-prepare-deposit'),
              onPressed: canPrepare ? onPrepare : null,
              icon: const Icon(Icons.account_balance_wallet_outlined, size: 18),
              label: const Text('Prepare funding deposit'),
            ),
          ],
        ),
        if (gateCopy.isNotEmpty) ...[
          const SizedBox(height: 10),
          Text(
            gateCopy,
            style: TextStyle(
              color: FmTheme.textTertiary(context),
              fontSize: 12,
            ),
          ),
        ],
        if (message.isNotEmpty) ...[
          const SizedBox(height: 10),
          Text(
            message,
            style: TextStyle(
              color:
                  message.startsWith('Refresh failed') ||
                      message.startsWith('Exception')
                  ? FmTheme.danger(context)
                  : FmTheme.textSecondary(context),
              fontSize: 12,
            ),
          ),
        ],
      ],
    );
  }
}

class _IssueTimeline extends StatelessWidget {
  const _IssueTimeline({required this.issue});

  final Issue issue;

  @override
  Widget build(BuildContext context) {
    if (issue.events.isEmpty) {
      return const Text('No timeline events have been published yet.');
    }
    return Column(
      children: issue.events
          .map((event) => _IssueTimelineTile(event: event))
          .toList(),
    );
  }
}

class _IssueTimelineTile extends StatelessWidget {
  const _IssueTimelineTile({required this.event});

  final IssueEvent event;

  @override
  Widget build(BuildContext context) {
    final isStatus = event.type == 'status';
    final title = isStatus
        ? 'Status changed to ${event.status.isEmpty ? 'updated' : event.status}'
        : event.type == 'vote'
        ? 'Vote added'
        : 'Comment';
    return Container(
      margin: const EdgeInsets.only(bottom: 10),
      padding: const EdgeInsets.all(12),
      decoration: BoxDecoration(
        color: FmTheme.bgBase(context),
        borderRadius: BorderRadius.circular(FmRadius.lg),
      ),
      child: Row(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          Icon(
            isStatus ? Icons.sync_alt : Icons.chat_bubble_outline,
            color: isStatus
                ? FmTheme.accent(context)
                : FmTheme.textTertiary(context),
            size: 18,
          ),
          const SizedBox(width: 10),
          Expanded(
            child: Column(
              crossAxisAlignment: CrossAxisAlignment.start,
              children: [
                Text(
                  title,
                  style: const TextStyle(fontWeight: FontWeight.w800),
                ),
                if (event.displayAuthor.isNotEmpty) ...[
                  const SizedBox(height: 2),
                  Text(
                    event.displayAuthor,
                    style: TextStyle(
                      color: FmTheme.textSecondary(context),
                      fontSize: 12,
                    ),
                  ),
                ],
                if (event.body.trim().isNotEmpty) ...[
                  const SizedBox(height: 8),
                  SelectableText(event.body.trim()),
                ],
              ],
            ),
          ),
        ],
      ),
    );
  }
}

class _IssueQuickActions extends StatelessWidget {
  const _IssueQuickActions({required this.repo, required this.issue});

  final Repository repo;
  final Issue issue;

  @override
  Widget build(BuildContext context) => Wrap(
    spacing: 10,
    runSpacing: 10,
    crossAxisAlignment: WrapCrossAlignment.center,
    children: [
      FilledButton.icon(
        onPressed: () => showIssueActions(context, repo, issue),
        icon: const Icon(Icons.add_comment_outlined),
        label: const Text('Comment / vote / status'),
      ),
      _ThreadSubscriptionButton(
        repo: repo,
        source: 'issue',
        number: issue.number,
      ),
      const _PendingInboxNote(),
    ],
  );
}

class _ThreadSubscriptionButton extends StatefulWidget {
  const _ThreadSubscriptionButton({
    required this.repo,
    required this.source,
    required this.number,
  });

  final Repository repo;
  final String source;
  final int number;

  @override
  State<_ThreadSubscriptionButton> createState() =>
      _ThreadSubscriptionButtonState();
}

class _ThreadSubscriptionButtonState extends State<_ThreadSubscriptionButton> {
  bool _subscribed = false;
  bool _busy = false;

  String _signedAccountNode(BuildContext context) {
    final auth = context.watch<AuthService?>();
    final identity = context.watch<Identity?>();
    final session = auth?.session;
    if (session == null || identity == null) return '';
    final node = session.nodeName.trim().toLowerCase();
    if (node.isEmpty) return '';
    if (session.pubkey != identity.publicKeyB64url) return '';
    return node;
  }

  Future<void> _toggle(String node) async {
    if (_busy || node.isEmpty) return;
    final next = !_subscribed;
    final inbox = context.read<InboxService>();
    final messenger = ScaffoldMessenger.of(context);
    final dangerColor = FmTheme.danger(context);
    setState(() => _busy = true);
    try {
      await inbox.setThreadSubscription(
        widget.repo.owner,
        widget.repo.name,
        node: node,
        source: widget.source,
        number: widget.number,
        subscribed: next,
      );
      if (!mounted) return;
      setState(() => _subscribed = next);
      messenger.showSnackBar(
        SnackBar(
          content: Text(
            next
                ? 'Subscribed to thread notifications.'
                : 'Unsubscribed from thread notifications.',
          ),
        ),
      );
    } catch (e) {
      if (!mounted) return;
      messenger.showSnackBar(
        SnackBar(backgroundColor: dangerColor, content: Text('Failed: $e')),
      );
    } finally {
      if (mounted) setState(() => _busy = false);
    }
  }

  @override
  Widget build(BuildContext context) {
    final node = _signedAccountNode(context);
    final canSign = node.isNotEmpty;
    final label = !canSign
        ? 'Thread alerts unavailable'
        : _subscribed
        ? 'Unsubscribe thread'
        : 'Subscribe thread';
    final icon = !canSign
        ? Icons.lock_outline
        : _subscribed
        ? Icons.notifications_off_outlined
        : Icons.notifications_active_outlined;
    return OutlinedButton.icon(
      onPressed: _busy || !canSign ? null : () => _toggle(node),
      icon: _busy
          ? const SizedBox.square(
              dimension: 16,
              child: CircularProgressIndicator(strokeWidth: 2),
            )
          : Icon(icon),
      label: Text(label),
    );
  }
}

class _PullDetailScreen extends StatelessWidget {
  const _PullDetailScreen({required this.repo, required this.pull});

  final Repository repo;
  final PublishedPull pull;

  bool get _canRequestMerge => pull.status.trim().toLowerCase() == 'open';

  Future<void> _requestPullCommand(
    BuildContext context, {
    required String command,
    required String successMessage,
  }) async {
    await _requestDesktopCommand(
      context,
      context.read<ApiService>(),
      repo,
      command: command,
      target: 'pull:${pull.number}',
      payload: {
        'pullNumber': pull.number,
        if (pull.base.isNotEmpty) 'base': pull.base,
        if (pull.head.isNotEmpty) 'head': pull.head,
      },
      successMessage: successMessage,
    );
  }

  Future<void> _requestMerge(BuildContext context) async {
    await _requestPullCommand(
      context,
      command: 'pull.merge',
      successMessage:
          'Merge request queued. Approve it on the Qt desktop node to merge PR #${pull.number}.',
    );
  }

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
            const SizedBox(height: 10),
            const _PendingInboxNote(),
            const SizedBox(height: 16),
            SelectableText(
              pull.body.isEmpty ? 'No description provided.' : pull.body,
              style: const TextStyle(height: 1.45),
            ),
          ],
        ),
        const SizedBox(height: 12),
        _InfoCard(
          title: 'Patch',
          children: [
            if (pull.patch.isEmpty)
              const Text(
                'No patch content was published for this pull request yet.',
              )
            else
              _DiffViewer(diff: pull.patch),
          ],
        ),
        const SizedBox(height: 12),
        _InfoCard(
          title: 'Timeline',
          children: [_PullTimeline(pull: pull)],
        ),
        const SizedBox(height: 12),
        _InfoCard(
          title: 'Ship controls',
          children: [
            Text(
              _canRequestMerge
                  ? 'Queue signed PR requests. The desktop node will verify mergeability, show conflicts if any, and run Git only after the repo owner approves locally.'
                  : 'This pull request is ${pull.status}; merge/review-changing requests may be rejected by the desktop node if the local state does not support them.',
              style: TextStyle(color: FmTheme.textSecondary(context)),
            ),
            const SizedBox(height: 10),
            Wrap(
              spacing: 8,
              runSpacing: 8,
              children: [
                FilledButton.icon(
                  onPressed: _canRequestMerge
                      ? () => _requestMerge(context)
                      : null,
                  icon: const Icon(Icons.call_merge_outlined),
                  label: const Text('Request merge on desktop'),
                ),
                OutlinedButton.icon(
                  onPressed: _canRequestMerge
                      ? () => _requestPullCommand(
                          context,
                          command: 'pull.merge_delete',
                          successMessage:
                              'Merge + delete request queued. Approve it on the Qt desktop node.',
                        )
                      : null,
                  icon: const Icon(Icons.delete_sweep_outlined),
                  label: const Text('Request merge + delete branch'),
                ),
                OutlinedButton.icon(
                  onPressed: () => _requestPullCommand(
                    context,
                    command: 'pull.fix_agent',
                    successMessage:
                        'Fix-with-agent request queued. Approve it on the Qt desktop node.',
                  ),
                  icon: const Icon(Icons.smart_toy_outlined),
                  label: const Text('Request fix with agent'),
                ),
                OutlinedButton.icon(
                  onPressed: () => _requestPullCommand(
                    context,
                    command: 'pull.close',
                    successMessage:
                        'Close request queued. Approve it on the Qt desktop node.',
                  ),
                  icon: const Icon(Icons.archive_outlined),
                  label: const Text('Request close on desktop'),
                ),
                OutlinedButton.icon(
                  onPressed: () => _requestPullCommand(
                    context,
                    command: 'pull.reopen',
                    successMessage:
                        'Reopen request queued. Approve it on the Qt desktop node.',
                  ),
                  icon: const Icon(Icons.lock_open_outlined),
                  label: const Text('Request reopen on desktop'),
                ),
                OutlinedButton.icon(
                  onPressed: () => _requestPullCommand(
                    context,
                    command: 'pull.delete',
                    successMessage:
                        'Delete request queued. Approve it on the Qt desktop node.',
                  ),
                  icon: const Icon(Icons.delete_outline),
                  label: const Text('Request delete on desktop'),
                ),
              ],
            ),
          ],
        ),
        const SizedBox(height: 12),
        _InfoCard(
          title: 'Review actions',
          children: [
            const Text(
              'Submit comments, approvals, or requested changes through the signed Worker pull inbox.',
            ),
            const SizedBox(height: 8),
            const _PendingInboxNote(),
            const SizedBox(height: 8),
            const Text(_threadSubscriptionNote),
            const SizedBox(height: 12),
            Wrap(
              spacing: 10,
              runSpacing: 10,
              children: [
                FilledButton.icon(
                  onPressed: () =>
                      showPublishedPullActions(context, repo, pull),
                  icon: const Icon(Icons.rate_review_outlined),
                  label: const Text('Review / comment'),
                ),
                _ThreadSubscriptionButton(
                  repo: repo,
                  source: 'pull',
                  number: pull.number,
                ),
              ],
            ),
          ],
        ),
      ],
    ),
  );
}

class _PullTimeline extends StatelessWidget {
  const _PullTimeline({required this.pull});

  final PublishedPull pull;

  @override
  Widget build(BuildContext context) {
    if (pull.events.isEmpty) {
      return const Text('No review or comment events have been published yet.');
    }
    return Column(
      children: pull.events
          .map((event) => _PullTimelineTile(event: event))
          .toList(),
    );
  }
}

class _PullTimelineTile extends StatelessWidget {
  const _PullTimelineTile({required this.event});

  final PullEvent event;

  @override
  Widget build(BuildContext context) {
    final isApproval = event.displayTitle == 'Approved';
    final isRequestChanges = event.displayTitle == 'Requested changes';
    return Container(
      margin: const EdgeInsets.only(bottom: 10),
      padding: const EdgeInsets.all(12),
      decoration: BoxDecoration(
        color: FmTheme.bgBase(context),
        borderRadius: BorderRadius.circular(FmRadius.lg),
      ),
      child: Row(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          Icon(
            isApproval
                ? Icons.check_circle_outline
                : isRequestChanges
                ? Icons.cancel_outlined
                : Icons.rate_review_outlined,
            color: isApproval
                ? FmTheme.success(context)
                : isRequestChanges
                ? FmTheme.danger(context)
                : FmTheme.textTertiary(context),
            size: 18,
          ),
          const SizedBox(width: 10),
          Expanded(
            child: Column(
              crossAxisAlignment: CrossAxisAlignment.start,
              children: [
                Text(
                  event.displayTitle,
                  style: const TextStyle(fontWeight: FontWeight.w800),
                ),
                if (event.displayAuthor.isNotEmpty) ...[
                  const SizedBox(height: 2),
                  Text(
                    event.displayAuthor,
                    style: TextStyle(
                      color: FmTheme.textSecondary(context),
                      fontSize: 12,
                    ),
                  ),
                ],
                if (event.body.trim().isNotEmpty) ...[
                  const SizedBox(height: 8),
                  SelectableText(event.body.trim()),
                ],
              ],
            ),
          ),
        ],
      ),
    );
  }
}

class _DiscussionDetailScreen extends StatelessWidget {
  const _DiscussionDetailScreen({required this.repo, required this.discussion});

  final Repository repo;
  final RepoDiscussion discussion;

  @override
  Widget build(BuildContext context) {
    final body = TextEditingController();
    final inbox = context.read<InboxService>();
    return Scaffold(
      appBar: AppBar(title: Text('Discussion #${discussion.number}')),
      body: ListView(
        padding: const EdgeInsets.all(20),
        children: [
          _InfoCard(
            title: discussion.title,
            children: [
              Wrap(
                spacing: 8,
                runSpacing: 8,
                children: [
                  const _Chip(icon: Icons.forum_outlined, label: 'discussion'),
                  _Chip(
                    icon: Icons.category_outlined,
                    label: discussion.category,
                  ),
                  if (discussion.author.isNotEmpty)
                    _Chip(icon: Icons.person_outline, label: discussion.author),
                  _Chip(icon: Icons.folder_outlined, label: repo.fullName),
                ],
              ),
              const SizedBox(height: 16),
              SelectableText(
                discussion.body.isEmpty
                    ? 'No description provided.'
                    : discussion.body,
                style: const TextStyle(height: 1.45),
              ),
            ],
          ),
          const SizedBox(height: 12),
          _InfoCard(
            title: 'Timeline',
            children: [_DiscussionTimeline(discussion: discussion)],
          ),
          const SizedBox(height: 12),
          _InfoCard(
            title: 'Signed discussion reply',
            children: [
              const Text(
                'Replies are signed and sent to the discussion inbox, pending the repo owner applying them.',
              ),
              const SizedBox(height: 12),
              const ComposeIdentityBar(verb: 'Replying'),
              const SizedBox(height: 8),
              TextField(
                controller: body,
                minLines: 3,
                maxLines: 8,
                decoration: const InputDecoration(
                  labelText: 'Write a signed reply',
                  alignLabelWithHint: true,
                ),
              ),
              const SizedBox(height: 12),
              const _PendingInboxNote(),
              const SizedBox(height: 12),
              FilledButton.icon(
                onPressed: () {
                  final text = body.text.trim();
                  if (text.isEmpty) return;
                  _run(
                    context,
                    () => inbox.commentOnDiscussion(
                      repo.owner,
                      repo.name,
                      discussion.number,
                      text,
                    ),
                    _pendingNote,
                  );
                },
                icon: const Icon(Icons.reply_outlined),
                label: const Text('Submit reply'),
              ),
            ],
          ),
        ],
      ),
    );
  }
}

class _DiscussionTimeline extends StatelessWidget {
  const _DiscussionTimeline({required this.discussion});

  final RepoDiscussion discussion;

  @override
  Widget build(BuildContext context) {
    if (discussion.events.isEmpty) {
      return const Text('No discussion replies have been published yet.');
    }
    return Column(
      children: discussion.events
          .map((event) => _DiscussionTimelineTile(event: event))
          .toList(),
    );
  }
}

class _DiscussionTimelineTile extends StatelessWidget {
  const _DiscussionTimelineTile({required this.event});

  final DiscussionEvent event;

  @override
  Widget build(BuildContext context) => Container(
    margin: const EdgeInsets.only(bottom: 10),
    padding: const EdgeInsets.all(12),
    decoration: BoxDecoration(
      color: FmTheme.bgBase(context),
      borderRadius: BorderRadius.circular(FmRadius.lg),
    ),
    child: Row(
      crossAxisAlignment: CrossAxisAlignment.start,
      children: [
        Icon(
          Icons.forum_outlined,
          color: FmTheme.textTertiary(context),
          size: 18,
        ),
        const SizedBox(width: 10),
        Expanded(
          child: Column(
            crossAxisAlignment: CrossAxisAlignment.start,
            children: [
              const Text(
                'Reply',
                style: TextStyle(fontWeight: FontWeight.w800),
              ),
              if (event.displayAuthor.isNotEmpty) ...[
                const SizedBox(height: 2),
                Text(
                  event.displayAuthor,
                  style: TextStyle(
                    color: FmTheme.textSecondary(context),
                    fontSize: 12,
                  ),
                ),
              ],
              if (event.body.trim().isNotEmpty) ...[
                const SizedBox(height: 8),
                SelectableText(event.body.trim()),
              ],
            ],
          ),
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
        iconColor: FmTheme.accent(context),
        title: d.title,
        subtitle:
            '#${d.number} · ${d.category}${d.author.isNotEmpty ? " · ${d.author}" : ""}',
        badge: d.category,
        onTap: () => Navigator.of(context).push(
          MaterialPageRoute(
            builder: (_) => _DiscussionDetailScreen(repo: repo, discussion: d),
          ),
        ),
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
        iconColor: m.online
            ? FmTheme.success(context)
            : FmTheme.textTertiary(context),
        title: m.label,
        subtitle: m.online ? 'Online mirror' : 'Offline or recently unseen',
        badge: m.online ? 'live' : 'offline',
      ),
    );
  }
}

class _ReleasesTab extends StatelessWidget {
  const _ReleasesTab({required this.api, required this.repo});

  final ApiService api;
  final Repository repo;

  Future<void> _requestRelease(
    BuildContext context,
    RepoRelease release, {
    required String command,
    required String successMessage,
  }) async {
    await _requestDesktopCommand(
      context,
      api,
      repo,
      command: command,
      target: 'release:${release.tag}',
      payload: {
        'tag': release.tag,
        if (release.channel.isNotEmpty) 'channel': release.channel,
      },
      successMessage: successMessage,
    );
  }

  @override
  Widget build(BuildContext context) {
    return FutureBuilder<List<RepoRelease>>(
      future: api.releases(repo.owner, repo.name),
      builder: (context, snap) {
        if (snap.connectionState == ConnectionState.waiting) {
          return ListView(
            padding: const EdgeInsets.all(FmSpace.x4),
            children: const [_LoadingCard(label: 'Loading releases...')],
          );
        }
        if (snap.hasError) {
          return ListView(
            padding: const EdgeInsets.all(FmSpace.x4),
            children: [_EmptyCard(message: '${snap.error}')],
          );
        }
        final releases = snap.data ?? const <RepoRelease>[];
        if (releases.isEmpty) {
          return ListView(
            padding: const EdgeInsets.all(FmSpace.x4),
            children: const [
              _EmptyCard(
                message:
                    'No releases yet. Draft and publish from a paired desktop node.',
              ),
            ],
          );
        }
        return ListView.separated(
          padding: const EdgeInsets.all(FmSpace.x4),
          itemCount: releases.length,
          separatorBuilder: (context, index) => const SizedBox(height: 12),
          itemBuilder: (context, index) {
            final release = releases[index];
            return _InfoCard(
              title: release.tag,
              children: [
                Wrap(
                  spacing: 8,
                  runSpacing: 8,
                  children: [
                    if (release.channel.isNotEmpty)
                      _Chip(
                        icon: Icons.local_offer_outlined,
                        label: release.channel,
                      ),
                    if (release.shortCommit.isNotEmpty)
                      _Chip(
                        icon: Icons.commit_outlined,
                        label: release.shortCommit,
                      ),
                    if (release.createdDate.isNotEmpty)
                      _Chip(
                        icon: Icons.calendar_today_outlined,
                        label: release.createdDate,
                      ),
                    _Chip(
                      icon: Icons.inventory_2_outlined,
                      label: release.assetSummary,
                    ),
                  ],
                ),
                const SizedBox(height: 12),
                Text(
                  'Release artifacts are content-addressed and verified by sha256 integrity pins. Mobile can request desktop release work; the paired Qt node builds, signs, mirrors, and publishes locally.',
                  style: TextStyle(color: FmTheme.textSecondary(context)),
                ),
                const SizedBox(height: 12),
                if (release.assets.isEmpty)
                  const Text('No artifacts are attached to this release yet.')
                else
                  ...release.assets.map(
                    (asset) => _ReleaseAssetRow(asset: asset),
                  ),
                const SizedBox(height: 12),
                Wrap(
                  spacing: 8,
                  runSpacing: 8,
                  children: [
                    OutlinedButton.icon(
                      onPressed: () => _requestRelease(
                        context,
                        release,
                        command: 'release.draft',
                        successMessage:
                            'Draft release request queued. Approve it on the Qt desktop node.',
                      ),
                      icon: const Icon(Icons.edit_note_outlined),
                      label: const Text('Draft release'),
                    ),
                    OutlinedButton.icon(
                      onPressed: () => _requestRelease(
                        context,
                        release,
                        command: 'release.notes',
                        successMessage:
                            'Generate notes request queued. Approve it on the Qt desktop node.',
                      ),
                      icon: const Icon(Icons.notes_outlined),
                      label: const Text('Generate notes'),
                    ),
                    FilledButton.icon(
                      onPressed: () => _requestRelease(
                        context,
                        release,
                        command: 'release.create',
                        successMessage:
                            'Create release request queued. Approve it on the Qt desktop node.',
                      ),
                      icon: const Icon(Icons.new_releases_outlined),
                      label: const Text('Create release'),
                    ),
                    FilledButton.icon(
                      onPressed: () => _requestRelease(
                        context,
                        release,
                        command: 'release.publish',
                        successMessage:
                            'Publish artifacts request queued. Approve it on the Qt desktop node.',
                      ),
                      icon: const Icon(Icons.cloud_upload_outlined),
                      label: const Text('Publish artifacts'),
                    ),
                  ],
                ),
              ],
            );
          },
        );
      },
    );
  }
}

class _ReleaseAssetRow extends StatelessWidget {
  const _ReleaseAssetRow({required this.asset});

  final RepoReleaseAsset asset;

  @override
  Widget build(BuildContext context) {
    return Container(
      margin: const EdgeInsets.only(bottom: 8),
      padding: const EdgeInsets.all(12),
      decoration: BoxDecoration(
        color: FmTheme.bgBase(context),
        borderRadius: BorderRadius.circular(FmRadius.lg),
        border: Border.all(color: FmTheme.border(context)),
      ),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          Text(
            asset.name.isEmpty ? asset.shortSha : asset.name,
            style: const TextStyle(fontWeight: FontWeight.w800),
          ),
          const SizedBox(height: 6),
          Wrap(
            spacing: 8,
            runSpacing: 8,
            children: [
              _Chip(icon: Icons.devices_outlined, label: asset.platformLabel),
              _Chip(icon: Icons.storage_outlined, label: asset.sizeLabel),
              _Chip(icon: Icons.download_outlined, label: asset.downloadLabel),
            ],
          ),
          if (asset.sha256.isNotEmpty) ...[
            const SizedBox(height: 8),
            SelectableText(
              'sha256: ${asset.sha256}',
              style: TextStyle(
                color: FmTheme.textSecondary(context),
                fontFamily: 'monospace',
                fontSize: 12,
              ),
            ),
          ],
        ],
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
    return FutureBuilder<List<Map<String, dynamic>>>(
      future: api.commits(repo.owner, repo.name),
      builder: (context, snap) {
        if (snap.connectionState == ConnectionState.waiting) {
          return ListView(
            padding: const EdgeInsets.all(FmSpace.x4),
            children: const [_LoadingCard(label: 'Loading commits...')],
          );
        }
        if (snap.hasError) {
          return ListView(
            padding: const EdgeInsets.all(FmSpace.x4),
            children: [_EmptyCard(message: '${snap.error}')],
          );
        }
        final commits = snap.data ?? [];
        if (commits.isEmpty) {
          return ListView(
            padding: const EdgeInsets.all(FmSpace.x4),
            children: const [_EmptyCard(message: 'No commits.')],
          );
        }
        return ListView(
          padding: EdgeInsets.zero,
          children: [
            _GitHistoryHeader(commits: commits.length),
            _CommitHistoryTimeline(api: api, repo: repo, commits: commits),
            const SizedBox(height: FmSpace.x4),
          ],
        );
      },
    );
  }
}

// Static section header for the commit timeline. Only History exists as a
// view here, so it is presented as a plain header rather than a tab strip
// that would imply switchable Changes/Branches views.
class _GitHistoryHeader extends StatelessWidget {
  const _GitHistoryHeader({required this.commits});

  final int commits;

  @override
  Widget build(BuildContext context) {
    return Material(
      color: FmTheme.bgRaised(context),
      child: Container(
        height: 54,
        padding: const EdgeInsets.symmetric(horizontal: FmSpace.x4),
        alignment: Alignment.centerLeft,
        decoration: BoxDecoration(
          border: Border(bottom: BorderSide(color: FmTheme.border(context))),
        ),
        child: Row(
          mainAxisSize: MainAxisSize.min,
          children: [
            Text(
              'History',
              style: TextStyle(
                color: FmTheme.textPrimary(context),
                fontSize: 15,
                fontWeight: FontWeight.w800,
              ),
            ),
            const SizedBox(width: FmSpace.x1),
            Container(
              padding: const EdgeInsets.symmetric(
                horizontal: FmSpace.x2,
                vertical: 2,
              ),
              decoration: BoxDecoration(
                color: FmTheme.accent(context),
                borderRadius: BorderRadius.circular(FmRadius.full),
              ),
              child: Text(
                '$commits',
                style: const TextStyle(
                  color: Colors.white,
                  fontSize: 11,
                  fontWeight: FontWeight.w800,
                  height: 1,
                ),
              ),
            ),
          ],
        ),
      ),
    );
  }
}

class _CommitHistoryTimeline extends StatelessWidget {
  const _CommitHistoryTimeline({
    required this.api,
    required this.repo,
    required this.commits,
  });

  final ApiService api;
  final Repository repo;
  final List<Map<String, dynamic>> commits;

  @override
  Widget build(BuildContext context) {
    return ColoredBox(
      color: FmTheme.bgBase(context),
      child: Column(
        children: [
          for (var i = 0; i < commits.length; i++)
            _CommitTimelineRow(
              api: api,
              repo: repo,
              commit: commits[i],
              isHead: i == 0,
              isFirst: i == 0,
              isLast: i == commits.length - 1,
            ),
        ],
      ),
    );
  }
}

class _CommitTimelineRow extends StatelessWidget {
  const _CommitTimelineRow({
    required this.api,
    required this.repo,
    required this.commit,
    required this.isHead,
    required this.isFirst,
    required this.isLast,
  });

  final ApiService api;
  final Repository repo;
  final Map<String, dynamic> commit;
  final bool isHead;
  final bool isFirst;
  final bool isLast;

  @override
  Widget build(BuildContext context) {
    final subject = _commitSubject(commit);
    final author = _commitAuthor(commit);
    final short = _commitShort(commit);
    final time = _relativeCommitTime(commit);
    return InkWell(
      onTap: () => Navigator.of(context).push(
        MaterialPageRoute(
          builder: (_) =>
              _CommitDetailScreen(api: api, repo: repo, commit: commit),
        ),
      ),
      child: SizedBox(
        height: 82,
        child: Row(
          crossAxisAlignment: CrossAxisAlignment.stretch,
          children: [
            SizedBox(
              width: 54,
              child: _CommitRailNode(
                active: isHead,
                isFirst: isFirst,
                isLast: isLast,
              ),
            ),
            Expanded(
              child: Padding(
                padding: const EdgeInsets.fromLTRB(
                  FmSpace.x1,
                  FmSpace.x3,
                  FmSpace.x2,
                  FmSpace.x3,
                ),
                child: Column(
                  mainAxisAlignment: MainAxisAlignment.center,
                  crossAxisAlignment: CrossAxisAlignment.start,
                  children: [
                    Row(
                      crossAxisAlignment: CrossAxisAlignment.center,
                      children: [
                        if (isHead) ...[
                          const _HeadBadge(),
                          const SizedBox(width: FmSpace.x2),
                        ],
                        Expanded(
                          child: Text(
                            subject,
                            maxLines: 1,
                            overflow: TextOverflow.ellipsis,
                            style: TextStyle(
                              color: FmTheme.textPrimary(context),
                              fontSize: 15,
                              fontWeight: FontWeight.w800,
                              height: 1.15,
                            ),
                          ),
                        ),
                        if (time.isNotEmpty) ...[
                          const SizedBox(width: FmSpace.x1),
                          Text(
                            time,
                            style: TextStyle(
                              color: FmTheme.textTertiary(context),
                              fontSize: 12,
                              fontWeight: FontWeight.w700,
                            ),
                          ),
                        ],
                      ],
                    ),
                    const SizedBox(height: FmSpace.x2),
                    Row(
                      children: [
                        if (short.isNotEmpty) ...[
                          _HashBadge(short),
                          const SizedBox(width: FmSpace.x2),
                        ],
                        Expanded(
                          child: Text(
                            author.isEmpty ? 'Unknown author' : author,
                            maxLines: 1,
                            overflow: TextOverflow.ellipsis,
                            style: TextStyle(
                              color: FmTheme.textSecondary(context),
                              fontSize: 12,
                              fontWeight: FontWeight.w600,
                            ),
                          ),
                        ),
                      ],
                    ),
                  ],
                ),
              ),
            ),
            Padding(
              padding: const EdgeInsets.only(right: FmSpace.x3),
              child: Icon(
                Icons.chevron_right,
                color: FmTheme.textTertiary(context),
              ),
            ),
          ],
        ),
      ),
    );
  }
}

class _CommitRailNode extends StatelessWidget {
  const _CommitRailNode({
    required this.active,
    required this.isFirst,
    required this.isLast,
  });

  final bool active;
  final bool isFirst;
  final bool isLast;

  @override
  Widget build(BuildContext context) {
    final lineColor = FmTheme.isDark(context)
        ? const Color(0xFF4A4A4D)
        : const Color(0xFFC6C6C8);
    final nodeColor = active ? FmTheme.success(context) : lineColor;
    return Stack(
      alignment: Alignment.center,
      children: [
        Positioned(
          top: isFirst ? 41 : 0,
          bottom: isLast ? 41 : 0,
          child: Container(width: 2, color: lineColor),
        ),
        Container(
          width: 18,
          height: 18,
          decoration: BoxDecoration(
            color: active ? nodeColor : FmTheme.bgBase(context),
            shape: BoxShape.circle,
            border: Border.all(color: nodeColor, width: active ? 0 : 2),
          ),
        ),
      ],
    );
  }
}

class _HeadBadge extends StatelessWidget {
  const _HeadBadge();

  @override
  Widget build(BuildContext context) {
    return Container(
      padding: const EdgeInsets.symmetric(
        horizontal: FmSpace.x2,
        vertical: FmSpace.x1,
      ),
      decoration: BoxDecoration(
        color: FmTheme.success(context).withValues(alpha: .18),
        borderRadius: BorderRadius.circular(FmRadius.sm),
      ),
      child: Text(
        'HEAD',
        style: TextStyle(
          color: FmTheme.success(context),
          fontSize: 11,
          fontWeight: FontWeight.w900,
          height: 1,
        ),
      ),
    );
  }
}

class _HashBadge extends StatelessWidget {
  const _HashBadge(this.hash);

  final String hash;

  @override
  Widget build(BuildContext context) {
    const blue = Color(0xFF58A6FF);
    return Container(
      padding: const EdgeInsets.symmetric(
        horizontal: FmSpace.x2,
        vertical: FmSpace.x1,
      ),
      decoration: BoxDecoration(
        color: blue.withValues(alpha: FmTheme.isDark(context) ? .18 : .12),
        borderRadius: BorderRadius.circular(FmRadius.sm),
      ),
      child: Text(
        hash,
        style: const TextStyle(
          color: blue,
          fontFamily: 'monospace',
          fontSize: 12,
          fontWeight: FontWeight.w800,
          height: 1,
        ),
      ),
    );
  }
}

String _commitHash(Map<String, dynamic> commit) =>
    (commit['hash'] ?? commit['sha'] ?? commit['id'] ?? '').toString();

String _commitShort(Map<String, dynamic> commit) {
  final hash = _commitHash(commit);
  return hash.length < 7 ? hash : hash.substring(0, 7);
}

String _commitSubject(Map<String, dynamic> commit) {
  final raw = (commit['subject'] ?? commit['message'] ?? 'Commit').toString();
  final firstLine = raw.split('\n').first.trim();
  return firstLine.isEmpty ? 'Commit' : firstLine;
}

String _commitAuthor(Map<String, dynamic> commit) {
  final author = commit['author'];
  if (author is Map<String, dynamic>) {
    return (author['name'] ?? author['email'] ?? '').toString();
  }
  return (author ?? commit['authorName'] ?? commit['committer'] ?? '')
      .toString();
}

DateTime? _commitDate(Map<String, dynamic> commit) {
  for (final key in [
    'date',
    'time',
    'timestamp',
    'createdAt',
    'committedDate',
    'authorDate',
    'ts',
  ]) {
    final value = commit[key];
    if (value is int) {
      final millis = value > 100000000000 ? value : value * 1000;
      return DateTime.fromMillisecondsSinceEpoch(millis);
    }
    if (value is double) {
      final rounded = value.round();
      final millis = rounded > 100000000000 ? rounded : rounded * 1000;
      return DateTime.fromMillisecondsSinceEpoch(millis);
    }
    if (value is String && value.trim().isNotEmpty) {
      final parsedNumber = int.tryParse(value);
      if (parsedNumber != null) {
        final millis = parsedNumber > 100000000000
            ? parsedNumber
            : parsedNumber * 1000;
        return DateTime.fromMillisecondsSinceEpoch(millis);
      }
      final parsed = DateTime.tryParse(value);
      if (parsed != null) return parsed;
    }
  }
  return null;
}

String _relativeCommitTime(Map<String, dynamic> commit) {
  final date = _commitDate(commit);
  if (date == null) return '';
  final diff = DateTime.now().difference(date);
  if (diff.inMinutes < 1) return 'now';
  if (diff.inHours < 1) return '${diff.inMinutes}m';
  if (diff.inDays < 1) return '${diff.inHours}h';
  if (diff.inDays < 30) return '${diff.inDays}d';
  if (diff.inDays < 365) return '${(diff.inDays / 30).floor()}mo';
  return '${(diff.inDays / 365).floor()}y';
}

class _CommitDetailScreen extends StatelessWidget {
  const _CommitDetailScreen({
    required this.api,
    required this.repo,
    required this.commit,
  });

  final ApiService api;
  final Repository repo;
  final Map<String, dynamic> commit;

  @override
  Widget build(BuildContext context) {
    final hash = _commitHash(commit);
    final short = hash.length < 7 ? hash : hash.substring(0, 7);
    return Scaffold(
      appBar: AppBar(title: Text(short.isEmpty ? 'Commit' : short)),
      body: hash.isEmpty
          ? ListView(
              padding: const EdgeInsets.all(20),
              children: [
                _CommitSummaryCard(
                  repo: repo,
                  detail: RepoCommitDetail(commit: commit),
                ),
              ],
            )
          : FutureBuilder<RepoCommitDetail>(
              future: api.commitDetail(
                repo.owner,
                repo.name,
                hash,
                fallbackCommit: commit,
              ),
              builder: (context, snap) {
                final detail = snap.data ?? RepoCommitDetail(commit: commit);
                return ListView(
                  padding: const EdgeInsets.all(20),
                  children: [
                    _CommitSummaryCard(repo: repo, detail: detail),
                    const SizedBox(height: 12),
                    if (snap.connectionState == ConnectionState.waiting)
                      const _LoadingCard(label: 'Loading commit diff...')
                    else if (snap.hasError)
                      _EmptyCard(
                        message: 'Could not load commit diff: ${snap.error}',
                      )
                    else ...[
                      _CommitFilesCard(files: detail.files),
                      const SizedBox(height: 12),
                      _CommitDiffCard(
                        diff: detail.diff,
                        truncated: detail.truncated,
                      ),
                    ],
                    const SizedBox(height: 12),
                    _CommitCommentCard(repo: repo, hash: hash),
                  ],
                );
              },
            ),
    );
  }
}

class _CommitSummaryCard extends StatelessWidget {
  const _CommitSummaryCard({required this.repo, required this.detail});

  final Repository repo;
  final RepoCommitDetail detail;

  @override
  Widget build(BuildContext context) {
    final commit = detail.commit;
    final hash = _commitHash(commit);
    final message = _commitSubject(commit);
    final author = _commitAuthor(commit);
    final body = (commit['body'] ?? commit['description'] ?? '').toString();
    return _InfoCard(
      title: message,
      children: [
        Wrap(
          spacing: 8,
          runSpacing: 8,
          children: [
            _Chip(icon: Icons.tag, label: hash.isEmpty ? 'unknown' : hash),
            if (author.isNotEmpty)
              _Chip(icon: Icons.person_outline, label: author),
            _Chip(icon: Icons.folder_outlined, label: repo.fullName),
            if (_sourceLabel(detail.source) != null)
              _Chip(
                icon: Icons.dns_outlined,
                label: _sourceLabel(detail.source)!,
              ),
          ],
        ),
        if (body.isNotEmpty) ...[
          const SizedBox(height: 16),
          SelectableText(body, style: const TextStyle(height: 1.45)),
        ],
      ],
    );
  }
}

class _CommitFilesCard extends StatelessWidget {
  const _CommitFilesCard({required this.files});

  final List<RepoCommitFile> files;

  @override
  Widget build(BuildContext context) {
    if (files.isEmpty) {
      return const _EmptyCard(
        message: 'No changed files reported for this commit.',
      );
    }
    return _InfoCard(
      title: '${files.length} changed ${files.length == 1 ? "file" : "files"}',
      children: [for (final file in files) _CommitFileRow(file: file)],
    );
  }
}

class _CommitFileRow extends StatelessWidget {
  const _CommitFileRow({required this.file});

  final RepoCommitFile file;

  @override
  Widget build(BuildContext context) => Padding(
    padding: const EdgeInsets.only(bottom: FmSpace.x2),
    child: Row(
      children: [
        Icon(
          Icons.description_outlined,
          size: 18,
          color: FmTheme.textTertiary(context),
        ),
        const SizedBox(width: FmSpace.x2),
        Expanded(
          child: Text(
            file.path,
            maxLines: 1,
            overflow: TextOverflow.ellipsis,
            style: TextStyle(
              color: FmTheme.textPrimary(context),
              fontWeight: FontWeight.w700,
            ),
          ),
        ),
        const SizedBox(width: FmSpace.x2),
        if (file.adds.isNotEmpty)
          Text(
            '+${file.adds}',
            style: TextStyle(
              color: FmTheme.success(context),
              fontWeight: FontWeight.w800,
            ),
          ),
        if (file.dels.isNotEmpty) ...[
          const SizedBox(width: FmSpace.x1),
          Text(
            '-${file.dels}',
            style: TextStyle(
              color: FmTheme.danger(context),
              fontWeight: FontWeight.w800,
            ),
          ),
        ],
      ],
    ),
  );
}

class _CommitDiffCard extends StatelessWidget {
  const _CommitDiffCard({required this.diff, required this.truncated});

  final String diff;
  final bool truncated;

  @override
  Widget build(BuildContext context) {
    if (diff.isEmpty) {
      return const _EmptyCard(
        message: 'No text diff available for this commit.',
      );
    }
    return _InfoCard(
      title: 'Unified diff',
      children: [
        if (truncated) ...[
          Text(
            'Diff truncated by the desktop host to keep the mobile response bounded.',
            style: TextStyle(
              color: FmTheme.warning(context),
              fontWeight: FontWeight.w700,
            ),
          ),
          const SizedBox(height: FmSpace.x3),
        ],
        _DiffViewer(diff: diff),
      ],
    );
  }
}

class _CommitCommentCard extends StatelessWidget {
  const _CommitCommentCard({required this.repo, required this.hash});

  final Repository repo;
  final String hash;

  @override
  Widget build(BuildContext context) => _InfoCard(
    title: 'Commit comments',
    children: [
      const Text(
        'Add a signed comment to this commit. The repo owner drains it through the Worker commit inbox.',
      ),
      const SizedBox(height: 12),
      FilledButton.icon(
        onPressed: hash.isEmpty
            ? null
            : () => _commentOnCommit(context, repo, hash),
        icon: const Icon(Icons.add_comment_outlined),
        label: const Text('Comment on commit'),
      ),
    ],
  );
}

Future<void> _commentOnCommit(
  BuildContext context,
  Repository repo,
  String hash,
) async {
  final inbox = context.read<InboxService>();
  final body = await _showCommitCommentComposer(context, hash: hash);
  if (body == null || !context.mounted) return;
  await _run(
    context,
    () => inbox.commentOnCommit(repo.owner, repo.name, hash, body),
    _pendingNote,
  );
}

Future<String?> _showCommitCommentComposer(
  BuildContext context, {
  required String hash,
}) {
  final controller = TextEditingController();
  final short = hash.length < 7 ? hash : hash.substring(0, 7);
  return showDialog<String>(
    context: context,
    builder: (ctx) => AlertDialog(
      backgroundColor: FmTheme.bgOverlay(ctx),
      surfaceTintColor: Colors.transparent,
      shape: RoundedRectangleBorder(
        borderRadius: BorderRadius.circular(FmRadius.lg),
      ),
      title: const Text('Signed commit comment'),
      content: SizedBox(
        width: 520,
        child: Column(
          mainAxisSize: MainAxisSize.min,
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            Text(
              'Commit comments are signed and sent to the commit inbox for $short, pending the repo owner applying them.',
              style: const TextStyle(height: 1.4),
            ),
            const SizedBox(height: 12),
            const ComposeIdentityBar(verb: 'Commenting'),
            const SizedBox(height: 8),
            TextField(
              controller: controller,
              autofocus: true,
              minLines: 4,
              maxLines: 8,
              decoration: const InputDecoration(
                labelText: 'Comment',
                alignLabelWithHint: true,
              ),
            ),
            const SizedBox(height: 12),
            const _PendingInboxNote(),
          ],
        ),
      ),
      actions: [
        TextButton(
          onPressed: () => Navigator.pop(ctx),
          child: const Text('Cancel'),
        ),
        FilledButton(
          onPressed: () {
            final body = controller.text.trim();
            if (body.isEmpty) return;
            Navigator.pop(ctx, body);
          },
          child: const Text('Submit comment'),
        ),
      ],
    ),
  );
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
        style: TextStyle(color: FmTheme.textSecondary(context), height: 1.35),
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

class _DiffViewer extends StatelessWidget {
  const _DiffViewer({required this.diff});

  final String diff;

  @override
  Widget build(BuildContext context) {
    final lines = const LineSplitter().convert(diff);
    return Container(
      constraints: const BoxConstraints(maxHeight: 420),
      decoration: BoxDecoration(
        color: FmTheme.bgBase(context),
        borderRadius: BorderRadius.circular(FmRadius.lg),
      ),
      child: ListView.builder(
        shrinkWrap: true,
        itemCount: lines.length,
        itemBuilder: (context, index) {
          final line = lines[index];
          final color = line.startsWith('+') && !line.startsWith('+++')
              ? FmTheme.success(context).withValues(alpha: .12)
              : line.startsWith('-') && !line.startsWith('---')
              ? FmTheme.danger(context).withValues(alpha: .10)
              : line.startsWith('@@')
              ? FmTheme.accent(context).withValues(alpha: .10)
              : Colors.transparent;
          final textColor = line.startsWith('+') && !line.startsWith('+++')
              ? FmTheme.success(context)
              : line.startsWith('-') && !line.startsWith('---')
              ? FmTheme.danger(context)
              : line.startsWith('@@')
              ? FmTheme.accent(context)
              : FmTheme.textPrimary(context);
          return Container(
            color: color,
            padding: const EdgeInsets.symmetric(horizontal: 12, vertical: 3),
            child: SelectableText(
              line,
              style: TextStyle(
                fontFamily: 'monospace',
                fontSize: 12,
                height: 1.35,
                color: textColor,
              ),
            ),
          );
        },
      ),
    );
  }
}

class _InfoCard extends StatelessWidget {
  const _InfoCard({required this.title, required this.children});
  final String title;
  final List<Widget> children;
  @override
  Widget build(BuildContext context) => FmCard(
    radius: FmRadius.lg,
    padding: const EdgeInsets.all(FmSpace.x4),
    child: Column(
      crossAxisAlignment: CrossAxisAlignment.start,
      children: [
        Text(
          title,
          style: TextStyle(
            color: FmTheme.textPrimary(context),
            fontSize: 17,
            fontWeight: FontWeight.w800,
          ),
        ),
        const SizedBox(height: FmSpace.x3),
        ...children,
      ],
    ),
  );
}

class _CopyableValue extends StatelessWidget {
  const _CopyableValue({
    required this.label,
    required this.value,
    required this.copyLabel,
    required this.copyKey,
  });

  final String label;
  final String value;
  final String copyLabel;
  final Key copyKey;

  @override
  Widget build(BuildContext context) {
    return Column(
      crossAxisAlignment: CrossAxisAlignment.start,
      children: [
        Text(
          label,
          style: TextStyle(
            color: FmTheme.textSecondary(context),
            fontSize: 12,
            fontWeight: FontWeight.w700,
          ),
        ),
        const SizedBox(height: 4),
        Row(
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            Expanded(
              child: SelectableText(
                value,
                style: TextStyle(
                  color: FmTheme.textPrimary(context),
                  height: 1.35,
                ),
              ),
            ),
            const SizedBox(width: 8),
            IconButton(
              key: copyKey,
              tooltip: 'Copy $label',
              onPressed: () =>
                  _copyText(context, label: copyLabel, value: value),
              icon: const Icon(Icons.copy_all_outlined, size: 18),
            ),
          ],
        ),
      ],
    );
  }
}

class _Chip extends StatelessWidget {
  const _Chip({required this.icon, required this.label});
  final IconData icon;
  final String label;
  @override
  Widget build(BuildContext context) => Container(
    padding: const EdgeInsets.symmetric(
      horizontal: FmSpace.x3,
      vertical: FmSpace.x2,
    ),
    decoration: BoxDecoration(
      color: FmTheme.bgBase(context),
      borderRadius: BorderRadius.circular(FmRadius.full),
    ),
    child: Row(
      mainAxisSize: MainAxisSize.min,
      children: [
        Icon(icon, size: 14, color: FmTheme.textTertiary(context)),
        const SizedBox(width: FmSpace.x1),
        Text(
          label,
          style: TextStyle(
            color: FmTheme.textSecondary(context),
            fontSize: 12,
            fontWeight: FontWeight.w700,
          ),
        ),
      ],
    ),
  );
}

class _PendingInboxNote extends StatelessWidget {
  const _PendingInboxNote();

  @override
  Widget build(BuildContext context) => Container(
    padding: const EdgeInsets.symmetric(
      horizontal: FmSpace.x3,
      vertical: FmSpace.x2,
    ),
    decoration: BoxDecoration(
      color: FmTheme.bgBase(context),
      borderRadius: BorderRadius.circular(FmRadius.lg),
      border: Border.all(color: FmTheme.border(context)),
    ),
    child: Row(
      mainAxisSize: MainAxisSize.min,
      children: [
        Icon(
          Icons.verified_user_outlined,
          size: 14,
          color: FmTheme.textTertiary(context),
        ),
        const SizedBox(width: FmSpace.x1),
        Flexible(
          child: Text(
            'Signed inbox action - pending the repo owner applying it.',
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

String _formatUsd(double value) {
  if (value == value.roundToDouble()) return '\$${value.toInt()}';
  return '\$${value.toStringAsFixed(2)}';
}

bool _looksLikeSolanaAddress(String value) =>
    RegExp(r'^[1-9A-HJ-NP-Za-km-z]{32,44}$').hasMatch(value.trim());

List<String> _splitCommaSeparated(String value) => value
    .split(',')
    .map((part) => part.trim())
    .where((part) => part.isNotEmpty)
    .toList();

class _SourceChip extends StatelessWidget {
  const _SourceChip({required this.source});

  final String source;

  @override
  Widget build(BuildContext context) {
    final label = _sourceLabel(source);
    if (label == null) return const SizedBox.shrink();
    return ConstrainedBox(
      constraints: const BoxConstraints(maxWidth: 220),
      child: Container(
        padding: const EdgeInsets.symmetric(
          horizontal: FmSpace.x3,
          vertical: FmSpace.x2,
        ),
        decoration: BoxDecoration(
          color: FmTheme.bgBase(context),
          borderRadius: BorderRadius.circular(FmRadius.full),
          border: Border.all(color: FmTheme.border(context)),
        ),
        child: Row(
          mainAxisSize: MainAxisSize.min,
          children: [
            Icon(
              Icons.dns_outlined,
              size: 14,
              color: FmTheme.textTertiary(context),
            ),
            const SizedBox(width: FmSpace.x1),
            Flexible(
              child: Text(
                label,
                maxLines: 1,
                overflow: TextOverflow.ellipsis,
                style: TextStyle(
                  color: FmTheme.textSecondary(context),
                  fontSize: 12,
                  fontWeight: FontWeight.w700,
                ),
              ),
            ),
          ],
        ),
      ),
    );
  }
}

class _PathBar extends StatelessWidget {
  const _PathBar({
    required this.path,
    required this.onUp,
    required this.onPath,
    required this.onGoToFile,
  });

  final String path;
  final VoidCallback? onUp;
  final ValueChanged<String> onPath;
  final VoidCallback onGoToFile;

  @override
  Widget build(BuildContext context) {
    final segments = path.split('/').where((p) => p.isNotEmpty).toList();
    var prefix = '';
    final crumbs = <Widget>[
      TextButton.icon(
        onPressed: () => onPath(''),
        icon: const Icon(Icons.home_outlined, size: 17),
        label: const Text(repoRootLabel),
      ),
    ];
    for (final segment in segments) {
      prefix = prefix.isEmpty ? segment : '$prefix/$segment';
      final target = prefix;
      crumbs.add(
        Icon(
          Icons.chevron_right,
          size: 16,
          color: FmTheme.textTertiary(context),
        ),
      );
      crumbs.add(
        TextButton(
          onPressed: () => onPath(target),
          child: Text(segment, overflow: TextOverflow.ellipsis),
        ),
      );
    }

    return FmCard(
      radius: FmRadius.lg,
      padding: const EdgeInsets.symmetric(
        horizontal: FmSpace.x2,
        vertical: FmSpace.x2,
      ),
      child: Row(
        children: [
          IconButton(
            onPressed: onUp,
            icon: const Icon(Icons.arrow_upward_rounded),
            tooltip: 'Up',
          ),
          Expanded(
            child: SingleChildScrollView(
              scrollDirection: Axis.horizontal,
              child: Row(children: crumbs),
            ),
          ),
          const SizedBox(width: FmSpace.x2),
          OutlinedButton.icon(
            onPressed: onGoToFile,
            icon: const Icon(Icons.search, size: 17),
            label: const Text('Go to file'),
          ),
        ],
      ),
    );
  }

  static const repoRootLabel = 'Repository root';
}

class _GoToFileSheet extends StatefulWidget {
  const _GoToFileSheet({
    required this.api,
    required this.repo,
    required this.ref,
    required this.onFileSelected,
  });

  final ApiService api;
  final Repository repo;
  final String ref;
  final ValueChanged<String> onFileSelected;

  @override
  State<_GoToFileSheet> createState() => _GoToFileSheetState();
}

class _GoToFileSheetState extends State<_GoToFileSheet> {
  final _query = TextEditingController();
  Future<List<RepoCodeSearchMatch>>? _future;
  String _lastQuery = '';

  @override
  void dispose() {
    _query.dispose();
    super.dispose();
  }

  void _search() {
    final q = _query.text.trim();
    if (q.length < 2) return;
    setState(() {
      _lastQuery = q;
      _future = widget.api
          .searchRepo(widget.repo.owner, widget.repo.name, q, ref: widget.ref)
          .then((results) => results.code);
    });
  }

  void _select(String path) {
    Navigator.of(context).pop();
    widget.onFileSelected(path);
  }

  @override
  Widget build(BuildContext context) {
    final bottom = MediaQuery.viewInsetsOf(context).bottom;
    return Padding(
      padding: EdgeInsets.only(bottom: bottom),
      child: Container(
        constraints: BoxConstraints(
          maxHeight: MediaQuery.sizeOf(context).height * .78,
        ),
        decoration: BoxDecoration(
          color: FmTheme.bgRaised(context),
          borderRadius: const BorderRadius.vertical(
            top: Radius.circular(FmRadius.lg),
          ),
          border: Border(top: BorderSide(color: FmTheme.border(context))),
        ),
        child: Column(
          mainAxisSize: MainAxisSize.min,
          children: [
            Padding(
              padding: const EdgeInsets.all(FmSpace.x4),
              child: Column(
                crossAxisAlignment: CrossAxisAlignment.start,
                children: [
                  Row(
                    children: [
                      Expanded(
                        child: Text(
                          'Go to file',
                          style: TextStyle(
                            color: FmTheme.textPrimary(context),
                            fontSize: 20,
                            fontWeight: FontWeight.w800,
                          ),
                        ),
                      ),
                      IconButton(
                        tooltip: 'Close',
                        onPressed: () => Navigator.of(context).pop(),
                        icon: const Icon(Icons.close),
                      ),
                    ],
                  ),
                  const SizedBox(height: FmSpace.x3),
                  Row(
                    children: [
                      Expanded(
                        child: TextField(
                          controller: _query,
                          autofocus: true,
                          textInputAction: TextInputAction.search,
                          decoration: const InputDecoration(
                            hintText: 'Search file names or code...',
                            prefixIcon: Icon(Icons.search),
                          ),
                          onSubmitted: (_) => _search(),
                        ),
                      ),
                      const SizedBox(width: FmSpace.x2),
                      FilledButton(
                        onPressed: _search,
                        child: const Text('Search'),
                      ),
                    ],
                  ),
                  const SizedBox(height: FmSpace.x2),
                  Text(
                    'Searching ${widget.repo.fullName} on ${widget.ref}.',
                    style: TextStyle(
                      color: FmTheme.textTertiary(context),
                      fontSize: 12,
                    ),
                  ),
                ],
              ),
            ),
            Flexible(
              child: FutureBuilder<List<RepoCodeSearchMatch>>(
                future: _future,
                builder: (context, snap) {
                  if (_future == null) {
                    return const FmEmptyState(
                      icon: Icons.search,
                      title: 'Search this repository',
                      message:
                          'Type at least two characters to find files quickly.',
                    );
                  }
                  if (snap.connectionState == ConnectionState.waiting) {
                    return const Center(child: CircularProgressIndicator());
                  }
                  if (snap.hasError) {
                    return _ErrorCard(
                      message: 'Could not search files: ${snap.error}',
                      onRetry: _search,
                    );
                  }
                  final matches = snap.data ?? const <RepoCodeSearchMatch>[];
                  if (matches.isEmpty) {
                    return FmEmptyState(
                      icon: Icons.search_off,
                      title: 'No file matches',
                      message: 'No code results for "$_lastQuery".',
                    );
                  }
                  return ListView.separated(
                    shrinkWrap: true,
                    padding: const EdgeInsets.fromLTRB(
                      FmSpace.x4,
                      FmSpace.x0,
                      FmSpace.x4,
                      FmSpace.x5,
                    ),
                    itemCount: matches.length,
                    separatorBuilder: (_, _) =>
                        const SizedBox(height: FmSpace.x2),
                    itemBuilder: (context, index) {
                      final match = matches[index];
                      return _SearchFileRow(
                        match: match,
                        onTap: () => _select(match.path),
                      );
                    },
                  );
                },
              ),
            ),
          ],
        ),
      ),
    );
  }
}

class _SearchFileRow extends StatelessWidget {
  const _SearchFileRow({required this.match, required this.onTap});

  final RepoCodeSearchMatch match;
  final VoidCallback onTap;

  @override
  Widget build(BuildContext context) => FmCard(
    onTap: onTap,
    radius: FmRadius.md,
    padding: const EdgeInsets.all(FmSpace.x3),
    child: Row(
      children: [
        Icon(
          Icons.description_outlined,
          color: FmTheme.textTertiary(context),
          size: 20,
        ),
        const SizedBox(width: FmSpace.x3),
        Expanded(
          child: Column(
            crossAxisAlignment: CrossAxisAlignment.start,
            children: [
              Text(
                match.path,
                maxLines: 1,
                overflow: TextOverflow.ellipsis,
                style: TextStyle(
                  color: FmTheme.textPrimary(context),
                  fontWeight: FontWeight.w800,
                ),
              ),
              if (match.text.isNotEmpty) ...[
                const SizedBox(height: FmSpace.x1),
                Text(
                  match.line > 0 ? '${match.line}: ${match.text}' : match.text,
                  maxLines: 2,
                  overflow: TextOverflow.ellipsis,
                  style: TextStyle(
                    color: FmTheme.textSecondary(context),
                    fontSize: 12,
                  ),
                ),
              ],
            ],
          ),
        ),
      ],
    ),
  );
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
  Widget build(BuildContext context) => FmCard(
    color: FmTheme.bgRaised(context),
    radius: FmRadius.lg,
    padding: const EdgeInsets.symmetric(vertical: FmSpace.x1),
    child: Column(
      children: [
        for (var i = 0; i < entries.length; i++) ...[
          _FileRow(
            entry: entries[i],
            onTap: () => entries[i].isDirectory
                ? onDir(entries[i].path)
                : onFile(entries[i]),
          ),
          if (i != entries.length - 1) const SizedBox(height: FmSpace.x2),
        ],
      ],
    ),
  );
}

class _FileRow extends StatelessWidget {
  const _FileRow({required this.entry, required this.onTap});

  final RepoTreeEntry entry;
  final VoidCallback onTap;

  @override
  Widget build(BuildContext context) => Material(
    color: Colors.transparent,
    borderRadius: BorderRadius.circular(FmRadius.md),
    clipBehavior: Clip.antiAlias,
    child: InkWell(
      onTap: onTap,
      borderRadius: BorderRadius.circular(FmRadius.md),
      child: ConstrainedBox(
        constraints: const BoxConstraints(minHeight: 72),
        child: Padding(
          padding: const EdgeInsets.symmetric(
            horizontal: FmSpace.x2,
            vertical: FmSpace.x2,
          ),
          child: Row(
            children: [
              Container(
                width: 44,
                height: 44,
                decoration: BoxDecoration(
                  color: entry.isDirectory
                      ? FmTheme.accentSubtle(context)
                      : FmTheme.bgOverlay(context),
                  borderRadius: BorderRadius.circular(FmRadius.md),
                ),
                child: Icon(
                  entry.isDirectory
                      ? Icons.folder_outlined
                      : Icons.description_outlined,
                  color: entry.isDirectory
                      ? FmTheme.accent(context)
                      : FmTheme.textTertiary(context),
                  size: 24,
                ),
              ),
              const SizedBox(width: FmSpace.x4),
              Expanded(
                child: Column(
                  mainAxisAlignment: MainAxisAlignment.center,
                  crossAxisAlignment: CrossAxisAlignment.start,
                  children: [
                    Text(
                      entry.name,
                      maxLines: 1,
                      overflow: TextOverflow.ellipsis,
                      style: TextStyle(
                        color: FmTheme.textPrimary(context),
                        fontSize: 16,
                        fontWeight: FontWeight.w800,
                      ),
                    ),
                    const SizedBox(height: FmSpace.x1),
                    Text(
                      entry.isDirectory
                          ? 'Directory'
                          : entry.size > 0
                          ? '${entry.size} bytes'
                          : 'File',
                      style: TextStyle(
                        color: FmTheme.textSecondary(context),
                        fontSize: 12,
                        fontWeight: FontWeight.w600,
                      ),
                    ),
                  ],
                ),
              ),
              const SizedBox(width: FmSpace.x2),
              Icon(Icons.chevron_right, color: FmTheme.textTertiary(context)),
            ],
          ),
        ),
      ),
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
    this.chips = const [],
    this.trailing,
    this.onTap,
  });
  final IconData icon;
  final Color iconColor;
  final String title;
  final String subtitle;
  final String badge;
  final List<String> chips;
  final Widget? trailing;
  final VoidCallback? onTap;
  @override
  Widget build(BuildContext context) => Padding(
    padding: const EdgeInsets.fromLTRB(
      FmSpace.x4,
      FmSpace.x2,
      FmSpace.x4,
      FmSpace.x2,
    ),
    child: FmCard(
      onTap: onTap,
      radius: FmRadius.lg,
      padding: const EdgeInsets.all(FmSpace.x4),
      child: Row(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          Icon(icon, color: iconColor),
          const SizedBox(width: FmSpace.x3),
          Expanded(
            child: Column(
              crossAxisAlignment: CrossAxisAlignment.start,
              children: [
                Text(
                  title,
                  maxLines: 2,
                  overflow: TextOverflow.ellipsis,
                  style: TextStyle(
                    color: FmTheme.textPrimary(context),
                    fontWeight: FontWeight.w800,
                  ),
                ),
                if (subtitle.isNotEmpty) ...[
                  const SizedBox(height: FmSpace.x1),
                  Text(
                    subtitle,
                    style: TextStyle(
                      color: FmTheme.textSecondary(context),
                      fontSize: 12,
                    ),
                  ),
                ],
                if (chips.isNotEmpty) ...[
                  const SizedBox(height: FmSpace.x2),
                  Wrap(
                    spacing: 6,
                    runSpacing: 6,
                    children: [for (final chip in chips) _SmallBadge(chip)],
                  ),
                ],
              ],
            ),
          ),
          if (trailing != null) ...[
            const SizedBox(width: FmSpace.x2),
            trailing!,
          ] else if (badge.isNotEmpty) ...[
            const SizedBox(width: FmSpace.x2),
            _SmallBadge(badge),
          ],
        ],
      ),
    ),
  );
}

Color _badgeColor(BuildContext context, String text) {
  final normalized = text.toLowerCase();
  if (normalized.contains('open') ||
      normalized.contains('live') ||
      normalized.contains('signed')) {
    return FmTheme.success(context);
  }
  if (normalized.contains('closed') ||
      normalized.contains('offline') ||
      normalized.contains('request')) {
    return FmTheme.danger(context);
  }
  if (normalized.contains('merged')) return FmTheme.accent(context);
  return FmTheme.textSecondary(context);
}

class _SmallBadge extends StatelessWidget {
  const _SmallBadge(this.text);
  final String text;
  @override
  Widget build(BuildContext context) =>
      FmStatusBadge(label: text, color: _badgeColor(context, text));
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
      Text(message, style: TextStyle(color: FmTheme.textSecondary(context))),
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
      Text(message, style: TextStyle(color: FmTheme.textSecondary(context))),
    ],
  );
}

Widget _kv(String k, String v) => FmKeyValueRow(label: k, value: v);

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

class _WorktreesTab extends StatelessWidget {
  const _WorktreesTab({required this.api, required this.repo});
  final ApiService api;
  final Repository repo;

  @override
  Widget build(BuildContext context) {
    return _AsyncList<RepoBranch>(
      future: api.branches(repo.owner, repo.name),
      empty: 'No worktrees found for this repository.',
      itemBuilder: (branch) {
        if (!branch.hasWorktree) return const SizedBox.shrink();
        return _MobileCard(
          icon: Icons.folder_special_outlined,
          iconColor: FmTheme.accent(context),
          title: branch.name,
          subtitle: branch.worktreePath,
          onTap: () => Navigator.of(context).push(
            MaterialPageRoute<void>(
              builder: (_) => _WorktreeDetailScreen(
                repo: repo,
                branch: branch,
              ),
            ),
          ),
          chips: [
            if (branch.isDefault) 'default',
            if (branch.sha.isNotEmpty) branch.sha.substring(0, 7),
          ],
        );
      },
    );
  }
}

class _WorktreeDetailScreen extends StatelessWidget {
  const _WorktreeDetailScreen({
    required this.repo,
    required this.branch,
  });

  final Repository repo;
  final RepoBranch branch;

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      appBar: AppBar(title: Text('Worktree: ${branch.name}')),
      body: ListView(
        padding: const EdgeInsets.all(20),
        children: [
          _InfoCard(
            title: branch.name,
            children: [
              Wrap(
                spacing: 8,
                runSpacing: 8,
                children: [
                  _Chip(
                    icon: Icons.account_tree_outlined,
                    label: branch.name,
                  ),
                  if (branch.isDefault)
                    const _Chip(
                      icon: Icons.flag_outlined,
                      label: 'default branch',
                    ),
                  if (branch.sha.isNotEmpty)
                    _Chip(
                      icon: Icons.commit_outlined,
                      label: branch.sha.substring(0, 7),
                    ),
                  _Chip(icon: Icons.folder_outlined, label: repo.fullName),
                ],
              ),
              const SizedBox(height: 16),
              _kv('Branch', branch.name),
              _kv('Worktree path', branch.worktreePath),
              if (branch.sha.isNotEmpty) _kv('Commit', branch.sha),
              const SizedBox(height: 16),
              Text(
                'This worktree is managed by the desktop node. Mobile can view worktree state; operations like merge, update, and delete are desktop-controlled.',
                style: TextStyle(
                  color: FmTheme.textSecondary(context),
                  fontSize: 12,
                  height: 1.35,
                ),
              ),
            ],
          ),
          const SizedBox(height: 12),
          _InfoCard(
            title: 'Worktree actions',
            children: [
              const Text(
                'Worktree operations are controlled from the desktop node. Use the Qt client to merge, update from main, or delete this worktree.',
              ),
              const SizedBox(height: 12),
              OutlinedButton.icon(
                onPressed: () => _copyText(
                  context,
                  label: 'Worktree path',
                  value: branch.worktreePath,
                ),
                icon: const Icon(Icons.copy_all_outlined, size: 18),
                label: const Text('Copy path'),
              ),
            ],
          ),
        ],
      ),
    );
  }
}
