import 'dart:convert';

import 'package:http/http.dart' as http;

import '../models/models.dart';
import 'settings_service.dart';

/// Read access to the relay/worker REST API (the same endpoints the Qt client
/// and the website use): the public catalog, per-repo issues/pulls/commits, and
/// network stats. Derives the https host from the configured relay URL.
class ApiService {
  ApiService(this._settings);

  static const cacheTtl = Duration(minutes: 1);

  final SettingsService _settings;
  final Map<String, _CacheEntry<RepoTree>> _treeCache = {};
  final Map<String, _CacheEntry<RepoBlob>> _blobCache = {};
  final Map<String, _CacheEntry<List<Issue>>> _issuesCache = {};
  final Map<String, _CacheEntry<List<PublishedPull>>> _pullsCache = {};
  final Map<String, _CacheEntry<List<RepoDiscussion>>> _discussionsCache = {};
  final Map<String, _CacheEntry<List<Map<String, dynamic>>>> _commitsCache = {};

  void clearRepoCache(String owner, String name) {
    final prefix = '$owner/$name:';
    _treeCache.removeWhere((key, _) => key.startsWith(prefix));
    _blobCache.removeWhere((key, _) => key.startsWith(prefix));
    _issuesCache.remove('$owner/$name');
    _pullsCache.remove('$owner/$name');
    _discussionsCache.remove('$owner/$name');
    _commitsCache.remove('$owner/$name');
  }

  Uri _base(String path, [Map<String, String>? query]) {
    final ws = Uri.parse(_settings.serverUrl);
    final scheme = ws.scheme == 'ws' ? 'http' : 'https';
    return Uri(
      scheme: scheme,
      host: ws.host,
      port: ws.hasPort ? ws.port : null,
      path: path,
      queryParameters: query?.isEmpty == true ? null : query,
    );
  }

  Uri rawUri(String owner, String name, String path) =>
      _base('/api/repo/$owner/$name/raw', {'path': path});

  Future<T> _cached<T>(
    Map<String, _CacheEntry<T>> cache,
    String key,
    Future<T> Function() load,
  ) {
    final now = DateTime.now();
    final existing = cache[key];
    if (existing != null && now.difference(existing.createdAt) < cacheTtl) {
      return existing.future;
    }
    final future = load();
    cache[key] = _CacheEntry(future, now);
    future.catchError((Object error, StackTrace stackTrace) {
      if (identical(cache[key]?.future, future)) cache.remove(key);
      return Future<T>.error(error, stackTrace);
    });
    return future;
  }

  Future<List<Repository>> repositories() async {
    final data = await _getJson(_base('/api/repositories'));
    final list = _asList(data);
    return list
        .whereType<Map<String, dynamic>>()
        .map(Repository.fromJson)
        .where((r) => r.owner.isNotEmpty && r.name.isNotEmpty)
        .toList();
  }

  Future<NetworkStats> networkStats() async {
    try {
      final data = await _getJson(_base('/api/network/stats'));
      if (data is Map<String, dynamic>) return NetworkStats.fromJson(data);
    } catch (_) {}
    return NetworkStats();
  }

  Future<List<Issue>> issues(String owner, String name) async {
    final data = await _getJson(_base('/api/repo/$owner/$name/issues'));
    return _asList(
      data,
    ).whereType<Map<String, dynamic>>().map(Issue.fromJson).toList();
  }

  Future<List<PullRequest>> pulls(String owner, String name) async {
    final data = await _getJson(_base('/api/repo/$owner/$name/pulls'));
    return _asList(
      data,
    ).whereType<Map<String, dynamic>>().map(PullRequest.fromJson).toList();
  }

  Future<List<Map<String, dynamic>>> commits(String owner, String name) {
    final key = '$owner/$name';
    return _cached(_commitsCache, key, () async {
      final data = await _getJson(_base('/api/repo/$owner/$name/commits'));
      return _asList(data).whereType<Map<String, dynamic>>().toList();
    });
  }

  Future<RepoTree> tree(String owner, String name, {String path = ''}) {
    final key = '$owner/$name:$path';
    return _cached(_treeCache, key, () async {
      final data = await _getJson(
        _base('/api/repo/$owner/$name/tree', {'path': path}),
      );
      return RepoTree.fromJson(data, path: path);
    });
  }

  Future<RepoBlob> blob(String owner, String name, String path) {
    final key = '$owner/$name:$path';
    return _cached(_blobCache, key, () async {
      final data = await _getJson(
        _base('/api/repo/$owner/$name/blob', {'path': path}),
      );
      return RepoBlob.fromJson(data, path: path);
    });
  }

  Future<List<RepoBranch>> branches(String owner, String name) async {
    final data = await _getJson(_base('/api/repo/$owner/$name/branches'));
    return _asList(data)
        .whereType<Map<String, dynamic>>()
        .map(RepoBranch.fromJson)
        .where((b) => b.name.isNotEmpty)
        .toList();
  }

  Future<List<RepoMirror>> mirrors(String owner, String name) async {
    final data = await _getJson(_base('/api/repo/$owner/$name/mirrors'));
    return _asList(data)
        .whereType<Map<String, dynamic>>()
        .map(RepoMirror.fromJson)
        .where((m) => m.label.isNotEmpty)
        .toList();
  }

  Future<List<Issue>> publishedIssues(String owner, String name) {
    final key = '$owner/$name';
    return _cached(_issuesCache, key, () async {
      final dirs = await _numberedFolders(owner, name, 'issues');
      final items = await Future.wait(
        dirs.map((dir) async {
          try {
            final b = await blob(owner, name, 'issues/$dir/issue.md');
            final events = await _issueEvents(owner, name, dir);
            return _issueFromMarkdown(dir, b.content, events: events);
          } catch (_) {
            return null;
          }
        }),
      );
      return items.whereType<Issue>().toList()
        ..sort((a, b) => b.number.compareTo(a.number));
    });
  }

  Future<List<IssueEvent>> _issueEvents(
    String owner,
    String name,
    String number,
  ) async {
    try {
      final t = await tree(owner, name, path: 'issues/$number');
      final eventFiles =
          t.entries
              .where(
                (e) =>
                    !e.isDirectory &&
                    e.name.endsWith('.md') &&
                    e.name != 'issue.md',
              )
              .toList()
            ..sort((a, b) => a.name.compareTo(b.name));
      final events = <IssueEvent>[];
      for (final file in eventFiles) {
        try {
          final b = await blob(owner, name, file.path);
          events.add(_issueEventFromMarkdown(b.content));
        } catch (_) {}
      }
      return events;
    } catch (_) {
      return const [];
    }
  }

  Future<List<PublishedPull>> publishedPulls(String owner, String name) {
    final key = '$owner/$name';
    return _cached(_pullsCache, key, () async {
      final dirs = await _numberedFolders(owner, name, 'pulls');
      final items = await Future.wait(
        dirs.map((dir) async {
          try {
            final b = await blob(owner, name, 'pulls/$dir/pull.md');
            return _pullFromMarkdown(dir, b.content);
          } catch (_) {
            return null;
          }
        }),
      );
      return items.whereType<PublishedPull>().toList()
        ..sort((a, b) => b.number.compareTo(a.number));
    });
  }

  Future<List<RepoDiscussion>> publishedDiscussions(String owner, String name) {
    final key = '$owner/$name';
    return _cached(_discussionsCache, key, () async {
      final dirs = await _numberedFolders(owner, name, 'discussions');
      final items = await Future.wait(
        dirs.map((dir) async {
          try {
            final b = await blob(owner, name, 'discussions/$dir/discussion.md');
            final events = await _discussionEvents(owner, name, dir);
            return _discussionFromMarkdown(dir, b.content, events: events);
          } catch (_) {
            return null;
          }
        }),
      );
      return items.whereType<RepoDiscussion>().toList()
        ..sort((a, b) => b.number.compareTo(a.number));
    });
  }

  Future<List<DiscussionEvent>> _discussionEvents(
    String owner,
    String name,
    String number,
  ) async {
    try {
      final t = await tree(owner, name, path: 'discussions/$number');
      final eventFiles =
          t.entries
              .where(
                (e) =>
                    !e.isDirectory &&
                    e.name.endsWith('.md') &&
                    e.name != 'discussion.md',
              )
              .toList()
            ..sort((a, b) => a.name.compareTo(b.name));
      final events = <DiscussionEvent>[];
      for (final file in eventFiles) {
        try {
          final b = await blob(owner, name, file.path);
          events.add(_discussionEventFromMarkdown(b.content));
        } catch (_) {}
      }
      return events;
    } catch (_) {
      return const [];
    }
  }

  Future<List<String>> _numberedFolders(
    String owner,
    String name,
    String path,
  ) async {
    try {
      final t = await tree(owner, name, path: path);
      return t.entries
          .where((e) => e.isDirectory && int.tryParse(e.name) != null)
          .map((e) => e.name)
          .toList();
    } catch (_) {
      return const [];
    }
  }

  Issue _issueFromMarkdown(
    String number,
    String md, {
    List<IssueEvent> events = const [],
  }) {
    final meta = _frontMatter(md);
    final title = meta['title'] ?? _firstHeading(md) ?? 'Issue #$number';
    final status =
        meta['status'] ?? (md.contains('status: closed') ? 'closed' : 'open');
    return Issue(
      number: int.tryParse(number) ?? 0,
      title: title,
      status: status,
      body: _bodyWithoutFrontMatter(md),
      author: meta['authorName'] ?? meta['author'] ?? '',
      labels: (meta['labels'] ?? '')
          .replaceAll('[', '')
          .replaceAll(']', '')
          .split(',')
          .map((s) => s.trim())
          .where((s) => s.isNotEmpty)
          .toList(),
      events: events,
    );
  }

  IssueEvent _issueEventFromMarkdown(String md) {
    final meta = _frontMatter(md);
    return IssueEvent(
      type: meta['type'] ?? 'comment',
      body: _bodyWithoutFrontMatter(md),
      author: meta['author'] ?? '',
      authorName: meta['authorName'] ?? '',
      status: meta['status'] ?? '',
      ts: int.tryParse(meta['ts'] ?? '') ?? 0,
    );
  }

  PublishedPull _pullFromMarkdown(String number, String md) {
    final meta = _frontMatter(md);
    final body = _bodyWithoutFrontMatter(md);
    final title = meta['title'] ?? _firstHeading(md) ?? 'Pull request #$number';
    return PublishedPull(
      number: int.tryParse(number) ?? 0,
      title: title,
      status: meta['status'] ?? 'open',
      body: body,
      base: meta['base'] ?? '',
      head: meta['head'] ?? '',
      patch: meta['patch'] ?? meta['diff'] ?? _extractPatch(body),
      signed: (meta['sig'] ?? '').isNotEmpty,
    );
  }

  String _extractPatch(String body) {
    final fenced = RegExp(
      r'```(?:diff|patch)\s*\n([\s\S]*?)```',
      multiLine: true,
    ).firstMatch(body);
    if (fenced != null) return fenced.group(1)?.trimRight() ?? '';
    final lines = const LineSplitter().convert(body);
    final start = lines.indexWhere(
      (line) => line.startsWith('diff --git ') || line.startsWith('--- '),
    );
    if (start < 0) return '';
    return lines.sublist(start).join('\n').trimRight();
  }

  RepoDiscussion _discussionFromMarkdown(
    String number,
    String md, {
    List<DiscussionEvent> events = const [],
  }) {
    final meta = _frontMatter(md);
    return RepoDiscussion(
      number: int.tryParse(number) ?? 0,
      title: meta['title'] ?? _firstHeading(md) ?? 'Discussion #$number',
      body: _bodyWithoutFrontMatter(md),
      author: meta['authorName'] ?? meta['author'] ?? '',
      updatedMs: int.tryParse(meta['updatedAt'] ?? meta['ts'] ?? '') ?? 0,
      events: events,
    );
  }

  DiscussionEvent _discussionEventFromMarkdown(String md) {
    final meta = _frontMatter(md);
    return DiscussionEvent(
      type: meta['type'] ?? 'comment',
      body: _bodyWithoutFrontMatter(md),
      author: meta['author'] ?? '',
      authorName: meta['authorName'] ?? '',
      ts: int.tryParse(meta['ts'] ?? '') ?? 0,
    );
  }

  Map<String, String> _frontMatter(String md) {
    final lines = const LineSplitter().convert(md);
    if (lines.isEmpty || lines.first.trim() != '---') return const {};
    final out = <String, String>{};
    for (var i = 1; i < lines.length; i++) {
      final line = lines[i];
      if (line.trim() == '---') break;
      final idx = line.indexOf(':');
      if (idx > 0) {
        out[line.substring(0, idx).trim()] = line.substring(idx + 1).trim();
      }
    }
    return out;
  }

  String? _firstHeading(String md) {
    for (final line in const LineSplitter().convert(md)) {
      final clean = line.trim();
      if (clean.startsWith('#')) {
        return clean.replaceFirst(RegExp(r'^#+\s*'), '').trim();
      }
      if (clean.isNotEmpty && clean != '---' && !clean.contains(':')) {
        return clean;
      }
    }
    return null;
  }

  String _bodyWithoutFrontMatter(String md) {
    final lines = const LineSplitter().convert(md);
    if (lines.isEmpty || lines.first.trim() != '---') return md.trim();
    var end = -1;
    for (var i = 1; i < lines.length; i++) {
      if (lines[i].trim() == '---') {
        end = i;
        break;
      }
    }
    return end >= 0 ? lines.skip(end + 1).join('\n').trim() : md.trim();
  }

  // Catalog endpoints sometimes wrap the array in {repositories:[...]} / {data:[...]}.
  List<dynamic> _asList(dynamic data) {
    if (data is List) return data;
    if (data is Map<String, dynamic>) {
      for (final key in [
        'repositories',
        'issues',
        'pulls',
        'commits',
        'discussions',
        'mirrors',
        'branches',
        'entries',
        'tree',
        'data',
        'items',
      ]) {
        if (data[key] is List) return data[key] as List;
      }
    }
    return const [];
  }

  Future<dynamic> _getJson(Uri uri) async {
    http.Response? resp;
    for (var attempt = 0; attempt < 2; attempt++) {
      resp = await http
          .get(uri, headers: {'Accept': 'application/json'})
          .timeout(const Duration(seconds: 20));
      if (resp.statusCode < 500 || attempt == 1) break;
      await Future<void>.delayed(const Duration(milliseconds: 350));
    }
    resp!;
    if (resp.statusCode >= 200 && resp.statusCode < 300) {
      if (resp.body.isEmpty) return const [];
      return jsonDecode(resp.body);
    }
    if (resp.statusCode == 503 && uri.path.contains('/api/repo/')) {
      throw Exception(
        'Repo host is offline. Open the Qt desktop node for this owner/repo and make sure it is connected to this Worker, then refresh.',
      );
    }
    if (resp.statusCode == 401 && uri.path.contains('/api/repo/')) {
      throw Exception(
        'This repo needs an authenticated/private repo view token. Mobile private-repo browsing is not wired yet.',
      );
    }
    if (resp.statusCode == 500 && uri.path.contains('/api/repo/')) {
      throw Exception(
        'Repo host returned an internal error while serving this view. Refresh or retry in a moment; if it keeps happening, reconnect the desktop host for this repo.',
      );
    }
    throw Exception('HTTP ${resp.statusCode} for ${uri.path}');
  }
}

class _CacheEntry<T> {
  _CacheEntry(this.future, this.createdAt);

  final Future<T> future;
  final DateTime createdAt;
}
