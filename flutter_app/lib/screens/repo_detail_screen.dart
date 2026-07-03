import 'dart:convert';

import 'package:flutter/material.dart';
import 'package:provider/provider.dart';

import '../models/models.dart';
import '../services/api_service.dart';
import '../services/inbox_service.dart';
import '../theme.dart';
import '../widgets/fm_ui.dart';

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
                Tab(text: 'About'),
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

Future<void> showNewIssueDialog(BuildContext context, Repository repo) async {
  final inbox = context.read<InboxService>();
  final title = TextEditingController();
  final body = TextEditingController();
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

Future<void> showNewDiscussionDialog(
  BuildContext context,
  Repository repo,
) async {
  final inbox = context.read<InboxService>();
  final title = TextEditingController();
  final category = TextEditingController(text: 'general');
  final body = TextEditingController();
  final ok = await showDialog<bool>(
    context: context,
    builder: (ctx) => AlertDialog(
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
          children: [
            TextField(
              controller: title,
              decoration: const InputDecoration(labelText: 'Title'),
            ),
            const SizedBox(height: 10),
            TextField(
              controller: category,
              decoration: const InputDecoration(labelText: 'Category'),
            ),
            const SizedBox(height: 10),
            TextField(
              controller: body,
              minLines: 5,
              maxLines: 12,
              decoration: const InputDecoration(
                labelText: 'Body',
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
      category: category.text.trim().isEmpty ? 'general' : category.text.trim(),
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
  final body = await _promptText(
    context,
    state == 'comment' ? 'Review comment' : 'Review note (optional)',
    multiline: true,
  );
  if (!context.mounted) return;
  await _run(
    context,
    () => inbox.reviewPull(
      repo.owner,
      repo.name,
      pull.number,
      state,
      body?.trim() ?? '',
    ),
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
      backgroundColor: FmTheme.bgOverlay(ctx),
      surfaceTintColor: Colors.transparent,
      shape: RoundedRectangleBorder(
        borderRadius: BorderRadius.circular(FmRadius.lg),
      ),
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

  void _openDir(String path, {bool refresh = false}) {
    if (refresh) {
      widget.api.clearRepoCache(widget.repo.owner, widget.repo.name);
    }
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
                onRetry: () => _openDir(_path, refresh: true),
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
          return _FilePreview(api: api, repo: repo, blob: blob);
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
  });

  final ApiService api;
  final Repository repo;
  final RepoBlob blob;

  @override
  Widget build(BuildContext context) {
    final path = blob.path;
    final kind = _previewKind(path);
    if (kind == _PreviewKind.image) {
      return ColoredBox(
        color: FmTheme.bgBase(context),
        child: Center(
          child: InteractiveViewer(
            minScale: .5,
            maxScale: 5,
            child: Image.network(
              api.rawUri(repo.owner, repo.name, path).toString(),
              fit: BoxFit.contain,
              errorBuilder: (_, error, _) => _UnsupportedPreview(
                path: path,
                message:
                    'Could not render this image preview. ${error.toString()}',
              ),
            ),
          ),
        ),
      );
    }
    if (kind == _PreviewKind.video) {
      return _UnsupportedPreview(
        path: path,
        message:
            'Video preview is not bundled in the mobile app yet. Use the raw file URL below to open or download it.',
        rawUrl: api.rawUri(repo.owner, repo.name, path).toString(),
      );
    }
    final decoded = _decodedText(blob);
    if (decoded == null) {
      return _UnsupportedPreview(
        path: path,
        message:
            'This looks like a binary file, so ForkMesh is not showing it as text.',
        rawUrl: api.rawUri(repo.owner, repo.name, path).toString(),
      );
    }
    return _CodePreview(
      path: path,
      content: decoded.isEmpty ? 'Empty file.' : decoded,
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
  });

  final String path;
  final String message;
  final String rawUrl;

  @override
  Widget build(BuildContext context) => ColoredBox(
    color: FmTheme.bgBase(context),
    child: Center(
      child: Padding(
        padding: const EdgeInsets.all(FmSpace.x5),
        child: _InfoCard(
          title: path.split('/').last,
          children: [
            Text(
              message,
              style: TextStyle(color: FmTheme.textSecondary(context)),
            ),
            if (rawUrl.isNotEmpty) ...[
              const SizedBox(height: FmSpace.x3),
              SelectableText(
                rawUrl,
                style: TextStyle(color: FmTheme.accent(context), fontSize: 12),
              ),
            ],
          ],
        ),
      ),
    ),
  );
}

class _CodePreview extends StatelessWidget {
  const _CodePreview({required this.path, required this.content});

  final String path;
  final String content;

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
          Container(
            height: 38,
            padding: const EdgeInsets.symmetric(horizontal: FmSpace.x4),
            decoration: BoxDecoration(
              color: editorBg,
              border: Border(
                bottom: BorderSide(color: FmTheme.border(context)),
              ),
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
              ],
            ),
          ),
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
                style: baseStyle,
              ),
            ),
          ),
        ],
      ),
    );
  }
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
          title: 'Timeline',
          children: [
            _IssueTimeline(issue: issue),
            const SizedBox(height: 14),
            _IssueQuickActions(repo: repo, issue: issue),
          ],
        ),
      ],
    ),
  );
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
    children: [
      FilledButton.icon(
        onPressed: () => showIssueActions(context, repo, issue),
        icon: const Icon(Icons.add_comment_outlined),
        label: const Text('Comment / vote / status'),
      ),
    ],
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
          title: 'Review actions',
          children: [
            const Text(
              'Submit comments, approvals, or requested changes through the signed Worker pull inbox.',
            ),
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
              ],
            ),
          ],
        ),
      ],
    ),
  );
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
            title: 'Reply',
            children: [
              TextField(
                controller: body,
                minLines: 3,
                maxLines: 8,
                decoration: const InputDecoration(
                  labelText: 'Write a reply',
                  alignLabelWithHint: true,
                ),
              ),
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
        subtitle: '#${d.number}${d.author.isNotEmpty ? " · ${d.author}" : ""}',
        badge: 'discussion',
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
            _CommitHistoryTimeline(repo: repo, commits: commits),
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
  const _CommitHistoryTimeline({required this.repo, required this.commits});

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
    required this.repo,
    required this.commit,
    required this.isHead,
    required this.isFirst,
    required this.isLast,
  });

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
          builder: (_) => _CommitDetailScreen(repo: repo, commit: commit),
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
  const _CommitDetailScreen({required this.repo, required this.commit});

  final Repository repo;
  final Map<String, dynamic> commit;

  @override
  Widget build(BuildContext context) {
    final hash = (commit['hash'] ?? commit['sha'] ?? '').toString();
    final message = (commit['subject'] ?? commit['message'] ?? 'Commit')
        .toString();
    final author = (commit['author'] ?? commit['authorName'] ?? '').toString();
    final body = (commit['body'] ?? commit['description'] ?? '').toString();
    final short = hash.length < 7 ? hash : hash.substring(0, 7);
    return Scaffold(
      appBar: AppBar(title: Text(short.isEmpty ? 'Commit' : short)),
      body: ListView(
        padding: const EdgeInsets.all(20),
        children: [
          _InfoCard(
            title: message,
            children: [
              Wrap(
                spacing: 8,
                runSpacing: 8,
                children: [
                  _Chip(
                    icon: Icons.tag,
                    label: hash.isEmpty ? 'unknown' : hash,
                  ),
                  if (author.isNotEmpty)
                    _Chip(icon: Icons.person_outline, label: author),
                  _Chip(icon: Icons.folder_outlined, label: repo.fullName),
                ],
              ),
              if (body.isNotEmpty) ...[
                const SizedBox(height: 16),
                SelectableText(body, style: const TextStyle(height: 1.45)),
              ],
            ],
          ),
          const SizedBox(height: 12),
          _InfoCard(
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
          ),
        ],
      ),
    );
  }
}

Future<void> _commentOnCommit(
  BuildContext context,
  Repository repo,
  String hash,
) async {
  final inbox = context.read<InboxService>();
  final body = await _promptText(
    context,
    'Comment on commit ${hash.length < 7 ? hash : hash.substring(0, 7)}',
    multiline: true,
  );
  if (body == null || body.trim().isEmpty || !context.mounted) return;
  await _run(
    context,
    () => inbox.commentOnCommit(repo.owner, repo.name, hash, body.trim()),
    _pendingNote,
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

class _PathBar extends StatelessWidget {
  const _PathBar({required this.path, required this.onUp});
  final String path;
  final VoidCallback? onUp;
  @override
  Widget build(BuildContext context) => FmCard(
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
          child: Text(
            path.isEmpty ? repoRootLabel : path,
            overflow: TextOverflow.ellipsis,
            style: TextStyle(
              color: FmTheme.textPrimary(context),
              fontWeight: FontWeight.w800,
            ),
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
              ],
            ),
          ),
          if (badge.isNotEmpty) ...[
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
