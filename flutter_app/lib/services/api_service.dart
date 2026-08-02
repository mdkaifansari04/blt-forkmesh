import 'dart:async';
import 'dart:convert';

import 'package:http/http.dart' as http;

import '../models/models.dart';
import 'performance_monitor_service.dart';
import 'settings_service.dart';

/// Read access to the relay/worker REST API (the same endpoints the Qt client
/// and the website use): the public catalog, per-repo issues/pulls/commits, and
/// network stats. Derives the https host from the configured relay URL.
class ApiService {
  ApiService(this._settings, {PerformanceMonitorService? performanceMonitor})
    : _performanceMonitor = performanceMonitor;

  static const cacheTtl = Duration(minutes: 1);
  static const offlineRepoTtl = Duration(minutes: 1);
  static const catalogTtl = Duration(seconds: 30);

  final SettingsService _settings;
  final PerformanceMonitorService? _performanceMonitor;

  // Supplies the logged-in account's signed session token, or "" when logged
  // out. Wired to AuthService.session in main.dart. When present it is sent as
  // an `Authorization: Bearer` header so per-account endpoints (notifications,
  // agent list/prompt) authorize this account instead of a self-asserted name.
  String Function()? sessionTokenProvider;

  Map<String, String> _withAuth(Map<String, String> headers) {
    final token = sessionTokenProvider?.call() ?? '';
    if (token.isEmpty) return headers;
    return {...headers, 'Authorization': 'Bearer $token'};
  }

  _CacheEntry<List<Repository>>? _repositoriesCache;
  _CacheEntry<NetworkStats>? _networkStatsCache;
  _CacheEntry<NetworkLeaderboards>? _networkLeaderboardsCache;
  final Map<String, DateTime> _offlineRepoUntil = {};
  final Map<String, _CacheEntry<RepoTree>> _treeCache = {};
  final Map<String, _CacheEntry<RepoBlob>> _blobCache = {};
  final Map<String, _CacheEntry<List<Issue>>> _issuesCache = {};
  final Map<String, _CacheEntry<List<PublishedPull>>> _pullsCache = {};
  final Map<String, _CacheEntry<List<RepoDiscussion>>> _discussionsCache = {};
  final Map<String, _CacheEntry<List<Map<String, dynamic>>>> _commitsCache = {};
  final Map<String, _CacheEntry<List<RepoRelease>>> _releasesCache = {};

  void clearRepoCache(String owner, String name) {
    final prefix = '$owner/$name:';
    _treeCache.removeWhere((key, _) => key.startsWith(prefix));
    _blobCache.removeWhere((key, _) => key.startsWith(prefix));
    _issuesCache.remove('$owner/$name');
    _pullsCache.remove('$owner/$name');
    _discussionsCache.remove('$owner/$name');
    _commitsCache.remove('$owner/$name');
    _releasesCache.remove('$owner/$name');
    _offlineRepoUntil.remove('$owner/$name');
  }

  bool _isFresh<T>(_CacheEntry<T>? entry, Duration ttl) {
    if (entry == null) return false;
    return DateTime.now().difference(entry.createdAt) < ttl;
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

  Uri rawUri(String owner, String name, String path, {String ref = ''}) =>
      _base('/api/repo/$owner/$name/raw', {
        'path': path,
        if (ref.isNotEmpty) 'ref': ref,
      });

  Exception? _repoOfflineError(String repoKey) {
    final until = _offlineRepoUntil[repoKey];
    if (until == null) return null;
    if (DateTime.now().isBefore(until)) {
      return Exception(
        'Repo host is offline. Skipping refetch for a minute; tap Retry after reconnecting the desktop host.',
      );
    }
    _offlineRepoUntil.remove(repoKey);
    return null;
  }

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
    // Observe failures only to evict the cache entry. Returning a second
    // Future.error from an ignored catchError chain created an unhandled
    // asynchronous exception even when the actual caller correctly caught the
    // original request failure.
    unawaited(
      future.then<void>(
        (_) {},
        onError: (Object _, StackTrace _) {
          if (identical(cache[key]?.future, future)) cache.remove(key);
        },
      ),
    );
    return future;
  }

  Future<List<Repository>> repositories() async {
    if (_isFresh(_repositoriesCache, catalogTtl)) {
      return _repositoriesCache!.future;
    }
    final future = () async {
      final data = await _getJson(_base('/api/repositories'));
      final list = _asList(data);
      return list
          .whereType<Map<String, dynamic>>()
          .map(Repository.fromJson)
          .where((r) => r.owner.isNotEmpty && r.name.isNotEmpty)
          .toList();
    }();
    _repositoriesCache = _CacheEntry(future, DateTime.now());
    future.then(
      (_) {},
      onError: (Object error, StackTrace stackTrace) {
        if (identical(_repositoriesCache?.future, future)) {
          _repositoriesCache = null;
        }
      },
    );
    return future;
  }

  Future<NetworkStats> networkStats() async {
    if (_isFresh(_networkStatsCache, catalogTtl)) {
      return _networkStatsCache!.future;
    }
    final future = () async {
      try {
        final data = await _getJson(
          _base('/api/network/stats', {'payouts': '1'}),
        );
        if (data is Map<String, dynamic>) return NetworkStats.fromJson(data);
      } catch (_) {}
      return const NetworkStats();
    }();
    _networkStatsCache = _CacheEntry(future, DateTime.now());
    return future;
  }

  Future<NetworkLeaderboards> networkLeaderboards() async {
    if (_isFresh(_networkLeaderboardsCache, catalogTtl)) {
      return _networkLeaderboardsCache!.future;
    }
    final future = () async {
      try {
        final data = await _getJson(_base('/api/network/leaderboards'));
        if (data is Map<String, dynamic>) {
          return NetworkLeaderboards.fromJson(data);
        }
      } catch (_) {}
      return const NetworkLeaderboards();
    }();
    _networkLeaderboardsCache = _CacheEntry(future, DateTime.now());
    return future;
  }

  /// The shared room-chat passphrase, derived server-side from DATA_KEY and
  /// handed only to authenticated clients (bearer token attached by _getJson).
  /// Replaces the old public app-wide constant; every client feeds it into the
  /// same PBKDF2 room-key derivation so they all converge on one AES key.
  Future<String> roomChatPassphrase() async {
    final data = await _getJson(_base('/api/chat/room-key'));
    if (data is Map && data['passphrase'] is String) {
      return data['passphrase'] as String;
    }
    throw Exception('room key unavailable');
  }

  Future<NotificationPage> notifications(String node, {int limit = 40}) async {
    final cleanNode = node.trim().toLowerCase();
    if (cleanNode.isEmpty) {
      return NotificationPage(notifications: const [], unread: 0);
    }
    final data = await _getJson(
      _base('/api/notifications', {'node': cleanNode, 'limit': '$limit'}),
    );
    return NotificationPage.fromJson(
      data is Map<String, dynamic> ? data : const <String, dynamic>{},
    );
  }

  Future<void> markNotificationsRead(
    String node, {
    List<String> ids = const [],
    bool all = false,
  }) async {
    final cleanNode = node.trim().toLowerCase();
    if (cleanNode.isEmpty) return;
    await _postJson(_base('/api/notifications'), {
      'node': cleanNode,
      if (all) 'all': true,
      if (!all && ids.isNotEmpty) 'ids': ids,
    });
  }

  // --- Organizations -------------------------------------------------------
  // The /api/orgs endpoints authorize by the session bearer token (attached by
  // _withAuth) and return structured {"error": "<code>"} bodies on rejection.
  // _orgRequest decodes that body and raises an OrgApiException carrying text
  // from orgErrorMessage, instead of the generic HTTP error _decodeResponse
  // throws, so the org UI can show a human-readable reason.

  static String _orgSeg(String value) =>
      Uri.encodeComponent(value.trim().toLowerCase());

  Future<dynamic> _orgRequest(
    String method,
    Uri uri, [
    Map<String, dynamic>? body,
  ]) async {
    final headers = _withAuth({
      'Accept': 'application/json',
      if (body != null) 'Content-Type': 'application/json',
    });
    final encoded = body == null ? null : jsonEncode(body);
    Future<dynamic> load() async {
      final http.Response resp;
      switch (method) {
        case 'GET':
          resp = await http
              .get(uri, headers: headers)
              .timeout(const Duration(seconds: 12));
        case 'POST':
          resp = await http
              .post(uri, headers: headers, body: encoded)
              .timeout(const Duration(seconds: 12));
        case 'DELETE':
          resp = await http
              .delete(uri, headers: headers, body: encoded)
              .timeout(const Duration(seconds: 12));
        default:
          throw ArgumentError('unsupported method $method');
      }
      dynamic decoded;
      if (resp.body.isNotEmpty) {
        try {
          decoded = jsonDecode(resp.body);
        } catch (_) {}
      }
      if (resp.statusCode >= 200 && resp.statusCode < 300) {
        return decoded ?? const <String, dynamic>{};
      }
      final code = decoded is Map && decoded['error'] is String
          ? decoded['error'] as String
          : '';
      throw OrgApiException(
        code.isNotEmpty ? code : 'http_${resp.statusCode}',
        orgErrorMessage(code),
      );
    }

    final monitor = _performanceMonitor;
    if (monitor == null) return load();
    return monitor.track(
      'api.$method ${uri.path}',
      load,
      details: {'host': uri.host, 'path': uri.path},
    );
  }

  /// The organizations the signed-in account belongs to (`GET /api/orgs`).
  Future<List<OrgSummary>> myOrgs() async {
    final data = await _orgRequest('GET', _base('/api/orgs'));
    final list = data is Map<String, dynamic> && data['orgs'] is List
        ? data['orgs'] as List
        : const [];
    return list
        .whereType<Map>()
        .map((o) => OrgSummary.fromJson(Map<String, dynamic>.from(o)))
        .where((o) => o.name.isNotEmpty)
        .toList();
  }

  /// Create an org; the calling account becomes its first owner.
  Future<OrgSummary> createOrg({
    required String name,
    String displayName = '',
    String description = '',
  }) async {
    final data = await _orgRequest('POST', _base('/api/orgs'), {
      'name': name.trim().toLowerCase(),
      if (displayName.trim().isNotEmpty) 'displayName': displayName.trim(),
      if (description.trim().isNotEmpty) 'description': description.trim(),
    });
    final map = data is Map<String, dynamic> ? data : const <String, dynamic>{};
    return OrgSummary(
      name: (map['org'] ?? name).toString(),
      role: (map['role'] ?? 'owner').toString(),
    );
  }

  /// Public org profile plus the viewer's role (`GET /api/orgs/<name>`).
  Future<OrgProfile> orgProfile(String org) async {
    final data = await _orgRequest('GET', _base('/api/orgs/${_orgSeg(org)}'));
    return OrgProfile.fromJson(
      data is Map<String, dynamic> ? data : const <String, dynamic>{},
    );
  }

  /// Dissolve an org and all its rows (owners only).
  Future<void> deleteOrg(String org) =>
      _orgRequest('DELETE', _base('/api/orgs/${_orgSeg(org)}'), const {});

  Future<List<OrgMember>> orgMembers(String org) async {
    final data = await _orgRequest(
      'GET',
      _base('/api/orgs/${_orgSeg(org)}/members'),
    );
    final list = data is Map<String, dynamic> && data['members'] is List
        ? data['members'] as List
        : const [];
    return list
        .whereType<Map>()
        .map((m) => OrgMember.fromJson(Map<String, dynamic>.from(m)))
        .where((m) => m.name.isNotEmpty)
        .toList();
  }

  /// Add a member or change its role (owner/admin session).
  Future<void> setOrgMember(
    String org,
    String member, {
    String role = 'member',
  }) => _orgRequest('POST', _base('/api/orgs/${_orgSeg(org)}/members'), {
    'member': member.trim().toLowerCase(),
    'role': role.trim().toLowerCase(),
  });

  Future<void> removeOrgMember(String org, String member) => _orgRequest(
    'DELETE',
    _base('/api/orgs/${_orgSeg(org)}/members'),
    {'member': member.trim().toLowerCase()},
  );

  Future<List<OrgTeam>> orgTeams(String org) async {
    final data = await _orgRequest(
      'GET',
      _base('/api/orgs/${_orgSeg(org)}/teams'),
    );
    final list = data is Map<String, dynamic> && data['teams'] is List
        ? data['teams'] as List
        : const [];
    return list
        .whereType<Map>()
        .map((t) => OrgTeam.fromJson(Map<String, dynamic>.from(t)))
        .where((t) => t.team.isNotEmpty)
        .toList();
  }

  /// Create a team or update its permission (owner/admin session).
  Future<void> setOrgTeam(
    String org,
    String team, {
    String permission = 'read',
  }) => _orgRequest('POST', _base('/api/orgs/${_orgSeg(org)}/teams'), {
    'team': team.trim().toLowerCase(),
    'permission': permission.trim().toLowerCase(),
  });

  Future<void> deleteOrgTeam(String org, String team) => _orgRequest(
    'DELETE',
    _base('/api/orgs/${_orgSeg(org)}/teams'),
    {'team': team.trim().toLowerCase()},
  );

  /// The org members on one team (`GET /api/orgs/<org>/teams/<team>/members`).
  Future<List<OrgMember>> orgTeamMembers(String org, String team) async {
    final data = await _orgRequest(
      'GET',
      _base('/api/orgs/${_orgSeg(org)}/teams/${_orgSeg(team)}/members'),
    );
    final list = data is Map<String, dynamic> && data['members'] is List
        ? data['members'] as List
        : const [];
    return list
        .whereType<Map>()
        .map((m) => OrgMember.fromJson(Map<String, dynamic>.from(m)))
        .where((m) => m.name.isNotEmpty)
        .toList();
  }

  /// Add an existing org member to a team (owner/admin session).
  Future<void> addOrgTeamMember(String org, String team, String member) =>
      _orgRequest(
        'POST',
        _base('/api/orgs/${_orgSeg(org)}/teams/${_orgSeg(team)}/members'),
        {'member': member.trim().toLowerCase()},
      );

  Future<void> removeOrgTeamMember(String org, String team, String member) =>
      _orgRequest(
        'DELETE',
        _base('/api/orgs/${_orgSeg(org)}/teams/${_orgSeg(team)}/members'),
        {'member': member.trim().toLowerCase()},
      );

  Future<List<OrgRepo>> orgRepos(String org) async {
    final data = await _orgRequest(
      'GET',
      _base('/api/orgs/${_orgSeg(org)}/repos'),
    );
    final list = data is Map<String, dynamic> && data['repos'] is List
        ? data['repos'] as List
        : const [];
    return list
        .whereType<Map>()
        .map((r) => OrgRepo.fromJson(Map<String, dynamic>.from(r)))
        .where((r) => r.repo.isNotEmpty)
        .toList();
  }

  /// Link one of your node's published repos under the org's `/<org>/<repo>`
  /// namespace (owner/admin session; the node must be your own account).
  Future<void> linkOrgRepo(String org, String repo, {String node = ''}) =>
      _orgRequest('POST', _base('/api/orgs/${_orgSeg(org)}/repos'), {
        'repo': repo.trim().toLowerCase(),
        if (node.trim().isNotEmpty) 'node': node.trim().toLowerCase(),
      });

  Future<void> unlinkOrgRepo(String org, String repo) => _orgRequest(
    'DELETE',
    _base('/api/orgs/${_orgSeg(org)}/repos'),
    {'repo': repo.trim().toLowerCase()},
  );

  Future<List<AgentSession>> agentSessions(
    String owner,
    String name, {
    String ownerAccount = '',
  }) async {
    final data = await _postJson(_base('/api/repo/$owner/$name/agents/list'), {
      if (ownerAccount.trim().isNotEmpty)
        'ownerAccount': ownerAccount.trim().toLowerCase(),
    });
    final raw = data is Map<String, dynamic>
        ? _asList(data)
        : data is List
        ? data
        : const [];
    return raw
        .whereType<Map>()
        .map((item) => AgentSession.fromJson(Map<String, dynamic>.from(item)))
        .toList();
  }

  Future<AgentTranscript> agentTranscript(
    String owner,
    String name,
    int id, {
    String ownerAccount = '',
  }) async {
    final data =
        await _postJson(_base('/api/repo/$owner/$name/agents/$id/transcript'), {
          if (ownerAccount.trim().isNotEmpty)
            'ownerAccount': ownerAccount.trim().toLowerCase(),
        });
    return AgentTranscript.fromJson(
      data is Map<String, dynamic> ? data : const <String, dynamic>{},
    );
  }

  Future<RepoActions> repoActions(
    String owner,
    String name, {
    String ownerAccount = '',
    String ts = '',
    String sig = '',
  }) async {
    final data = await _postJson(_base('/api/repo/$owner/$name/actions/list'), {
      if (ownerAccount.trim().isNotEmpty)
        'ownerAccount': ownerAccount.trim().toLowerCase(),
      if (ts.trim().isNotEmpty) 'ts': ts.trim(),
      if (sig.trim().isNotEmpty) 'sig': sig.trim(),
    });
    return RepoActions.fromJson(
      data is Map<String, dynamic> ? data : const <String, dynamic>{},
    );
  }

  Future<ActionLog> actionLog(
    String owner,
    String name,
    int id, {
    String ownerAccount = '',
    String ts = '',
    String sig = '',
  }) async {
    final data =
        await _postJson(_base('/api/repo/$owner/$name/actions/$id/log'), {
          if (ownerAccount.trim().isNotEmpty)
            'ownerAccount': ownerAccount.trim().toLowerCase(),
          if (ts.trim().isNotEmpty) 'ts': ts.trim(),
          if (sig.trim().isNotEmpty) 'sig': sig.trim(),
        });
    return ActionLog.fromJson(
      data is Map<String, dynamic> ? data : const <String, dynamic>{},
    );
  }

  Future<DesktopCommandResult> desktopCommand(
    String owner,
    String name, {
    required String ownerAccount,
    required String command,
    required String target,
    required String ts,
    required String sig,
    Map<String, Object?> payload = const {},
  }) async {
    final data =
        await _postJson(_base('/api/repo/$owner/$name/desktop-commands'), {
          'ownerAccount': ownerAccount.trim().toLowerCase(),
          'command': command.trim(),
          'target': target.trim(),
          'ts': ts.trim(),
          'sig': sig.trim(),
          'payload': payload,
        });
    return DesktopCommandResult.fromJson(
      data is Map<String, dynamic> ? data : const <String, dynamic>{},
    );
  }

  Future<IssueBounty> issueBountyStatus(
    String owner,
    String repo,
    int number,
  ) async {
    try {
      final data = await _postJson(_base('/api/repo/$owner/$repo/bounty'), {
        'action': 'status',
        'number': number,
      });
      return IssueBounty.fromJson(data);
    } catch (error) {
      if ('$error'.contains('HTTP 404')) return const IssueBounty();
      rethrow;
    }
  }

  Future<IssueBounty> createIssueBounty(
    String owner,
    String repo, {
    required int number,
    required double amountUsd,
    String payee = '',
    String payeeNode = '',
    required String ts,
    required String sig,
  }) async {
    throw UnsupportedError(
      'Worker-held issue-bounty wallets are retired. Use an external '
      'self-custodial wallet, reviewed program, or multisig.',
    );
  }

  Future<BountyWallet> bountyWallet(
    String owner,
    String repo, {
    required String ts,
    required String sig,
  }) async {
    throw UnsupportedError(
      'Worker-held bounty wallets are retired and cannot be created or '
      'prepared by this client.',
    );
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

  /// Remote ActivityPub replies for a native thread. These are fetched from a
  /// dedicated Worker projection and never merged into signed repository event
  /// records.
  Future<List<FederatedReply>> federatedReplies(
    String owner,
    String name, {
    required String kind,
    required int number,
  }) async {
    if (!const {'issue', 'pull', 'discussion'}.contains(kind) || number <= 0) {
      return const <FederatedReply>[];
    }
    final data = await _getJson(
      _base('/api/repo/$owner/$name/fedi-comments', {
        'kind': kind,
        'number': '$number',
      }),
    );
    final raw = data is Map && data['comments'] is List
        ? data['comments'] as List
        : data is Map && data['items'] is List
        ? data['items'] as List
        : const [];
    return raw
        .whereType<Map>()
        .map((item) => FederatedReply.fromJson(Map<String, dynamic>.from(item)))
        .where((item) => item.remoteId.isNotEmpty || item.backlink.isNotEmpty)
        .toList();
  }

  Future<List<Map<String, dynamic>>> commits(String owner, String name) {
    final key = '$owner/$name';
    return _cached(_commitsCache, key, () async {
      final data = await _getJson(_base('/api/repo/$owner/$name/commits'));
      return _asList(data).whereType<Map<String, dynamic>>().toList();
    });
  }

  Future<RepoCommitDetail> commitDetail(
    String owner,
    String name,
    String hash, {
    Map<String, dynamic> fallbackCommit = const {},
  }) async {
    final data = await _getJson(
      _base('/api/repo/$owner/$name/commit', {'path': hash}),
    );
    return RepoCommitDetail.fromJson(data, fallbackCommit: fallbackCommit);
  }

  Future<RepoTree> tree(
    String owner,
    String name, {
    String path = '',
    String ref = '',
  }) {
    final repoKey = '$owner/$name';
    final offline = _repoOfflineError(repoKey);
    if (offline != null) return Future<RepoTree>.error(offline);
    final key = '$repoKey:$ref:$path';
    return _cached(_treeCache, key, () async {
      final data = await _getJson(
        _base('/api/repo/$owner/$name/tree', {
          'path': path,
          if (ref.isNotEmpty) 'ref': ref,
        }),
      );
      return RepoTree.fromJson(data, path: path);
    });
  }

  Future<RepoBlob> blob(
    String owner,
    String name,
    String path, {
    String ref = '',
  }) {
    final repoKey = '$owner/$name';
    final offline = _repoOfflineError(repoKey);
    if (offline != null) return Future<RepoBlob>.error(offline);
    final key = '$repoKey:$ref:$path';
    return _cached(_blobCache, key, () async {
      final data = await _getJson(
        _base('/api/repo/$owner/$name/blob', {
          'path': path,
          if (ref.isNotEmpty) 'ref': ref,
        }),
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

  Future<List<RepoRelease>> releases(String owner, String name) {
    final key = '$owner/$name';
    return _cached(_releasesCache, key, () async {
      final downloads = await _releaseDownloadCounts(owner, name);
      final RepoTree treeRoot;
      try {
        final data = await _getJson(
          _base('/api/repo/$owner/$name/tree', {'path': '.forkmesh/releases'}),
        );
        treeRoot = RepoTree.fromJson(data, path: '.forkmesh/releases');
      } catch (_) {
        return const <RepoRelease>[];
      }
      final dirs = treeRoot.entries.where((entry) => entry.isDirectory).toList()
        ..sort((a, b) => a.name.compareTo(b.name));
      final items = await Future.wait(
        dirs.map((dir) async {
          try {
            final blob = await this.blob(
              owner,
              name,
              '.forkmesh/releases/${dir.name}/release.json',
            );
            final decoded = jsonDecode(blob.content);
            if (decoded is! Map<String, dynamic>) return null;
            return RepoRelease.fromJson(decoded, downloads: downloads);
          } catch (_) {
            return null;
          }
        }),
      );
      final releases = items.whereType<RepoRelease>().toList();
      releases.sort((a, b) {
        final byTime = b.createdAtMs.compareTo(a.createdAtMs);
        if (byTime != 0) return byTime;
        return b.tag.compareTo(a.tag);
      });
      return releases;
    });
  }

  Future<Map<String, int>> _releaseDownloadCounts(
    String owner,
    String name,
  ) async {
    try {
      final data = await _getJson(
        _base('/api/repo/$owner/$name/releases/downloads'),
      );
      final raw = data is Map<String, dynamic> && data['counts'] is Map
          ? data['counts'] as Map
          : data is Map
          ? data
          : const {};
      return raw.map(
        (key, value) => MapEntry(
          key.toString(),
          value is int
              ? value
              : value is num
              ? value.toInt()
              : int.tryParse('$value') ?? 0,
        ),
      );
    } catch (_) {
      _offlineRepoUntil.remove('$owner/$name');
      return const {};
    }
  }

  Future<RepoSearchResults> searchRepo(
    String owner,
    String name,
    String query, {
    String ref = '',
  }) async {
    final data = await _getJson(
      _base('/api/repo/$owner/$name/search', {
        'q': query,
        if (ref.isNotEmpty) 'ref': ref,
      }),
    );
    return RepoSearchResults.fromJson(data);
  }

  Future<List<Issue>> publishedIssues(String owner, String name) {
    final key = '$owner/$name';
    return _cached(_issuesCache, key, () async {
      // Issues are split by status into .forkmesh/issues/open/<n>/ and
      // .forkmesh/issues/closed/<n>/ (pre-split repos keep <n>/ directly under
      // the root), one signed-event issue-<n>.json record per folder. A split
      // copy of a number wins over a stale legacy one.
      final dirByNumber = <String, String>{};
      for (final dir in await _numberedFolders(
        owner,
        name,
        '.forkmesh/issues',
      )) {
        dirByNumber[dir] = '.forkmesh/issues/$dir';
      }
      for (final sub in const ['open', 'closed']) {
        final subDirs = await _numberedFolders(
          owner,
          name,
          '.forkmesh/issues/$sub',
        );
        for (final dir in subDirs) {
          dirByNumber[dir] = '.forkmesh/issues/$sub/$dir';
        }
      }
      // Repos from before the JSON tracker published markdown records at
      // issues/<n>/issue.md; keep reading those when no JSON records exist.
      if (dirByNumber.isEmpty) return _legacyMarkdownIssues(owner, name);
      final items = await Future.wait(
        dirByNumber.entries.map((entry) async {
          try {
            final b = await blob(
              owner,
              name,
              '${entry.value}/issue-${entry.key}.json',
            );
            return _issueFromRecordJson(entry.key, b.content);
          } catch (_) {
            return null;
          }
        }),
      );
      return items.whereType<Issue>().toList()
        ..sort((a, b) => b.number.compareTo(a.number));
    });
  }

  Future<List<Issue>> _legacyMarkdownIssues(String owner, String name) async {
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
  }

  // One .forkmesh/issues/{open,closed}/<n>/issue-<n>.json signed-event record
  // -> Issue. The top-level fields mirror the desktop's Issue::toJson; the
  // body and display author ride on the "open" event, matching the website's
  // parseIssueJson.
  Issue? _issueFromRecordJson(String number, String text) {
    final decoded = jsonDecode(text);
    if (decoded is! Map<String, dynamic>) return null;
    Map<String, dynamic>? open;
    final events = decoded['events'];
    if (events is List) {
      for (final event in events.whereType<Map>()) {
        if (event['type'] == 'open') {
          open = Map<String, dynamic>.from(event);
          break;
        }
      }
    }
    return Issue.fromJson({
      ...decoded,
      'number': decoded['number'] ?? int.tryParse(number) ?? 0,
      'title': decoded['title'] ?? open?['title'] ?? 'Issue #$number',
      'body': decoded['body'] ?? open?['body'] ?? '',
      'author':
          decoded['authorName'] ??
          open?['authorName'] ??
          decoded['author'] ??
          '',
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
            final events = await _pullEvents(owner, name, dir);
            return _pullFromMarkdown(dir, b.content, events: events);
          } catch (_) {
            return null;
          }
        }),
      );
      return items.whereType<PublishedPull>().toList()
        ..sort((a, b) => b.number.compareTo(a.number));
    });
  }

  Future<List<PullEvent>> _pullEvents(
    String owner,
    String name,
    String number,
  ) async {
    try {
      final t = await tree(owner, name, path: 'pulls/$number');
      final eventFiles =
          t.entries
              .where(
                (e) =>
                    !e.isDirectory &&
                    e.name.endsWith('.md') &&
                    e.name != 'pull.md',
              )
              .toList()
            ..sort((a, b) => a.name.compareTo(b.name));
      final events = <PullEvent>[];
      for (final file in eventFiles) {
        try {
          final b = await blob(owner, name, file.path);
          events.add(_pullEventFromMarkdown(b.content));
        } catch (_) {}
      }
      return events;
    } catch (_) {
      return const [];
    }
  }

  Future<List<RepoDiscussion>> publishedDiscussions(String owner, String name) {
    final key = '$owner/$name';
    return _cached(_discussionsCache, key, () async {
      final dirs = await _numberedFolders(owner, name, '.forkmesh/discussions');
      final items = await Future.wait(
        dirs.map((dir) async {
          try {
            final b = await blob(
              owner,
              name,
              '.forkmesh/discussions/$dir/discussion.md',
            );
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
      final t = await tree(owner, name, path: '.forkmesh/discussions/$number');
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
    List<String> metaList(String key) => (meta[key] ?? '')
        .replaceAll('[', '')
        .replaceAll(']', '')
        .split(',')
        .map((s) => s.trim())
        .where((s) => s.isNotEmpty)
        .toList();

    final title = meta['title'] ?? _firstHeading(md) ?? 'Issue #$number';
    final status =
        meta['status'] ?? (md.contains('status: closed') ? 'closed' : 'open');
    return Issue(
      number: int.tryParse(number) ?? 0,
      title: title,
      status: status,
      body: _bodyWithoutFrontMatter(md),
      author: meta['authorName'] ?? meta['author'] ?? '',
      labels: metaList('labels'),
      milestone: meta['milestone'] ?? '',
      priority: int.tryParse(meta['priority'] ?? '') ?? 0,
      assignees: metaList('assignees'),
      events: events,
      votes: events.where((event) => event.type == 'vote').length,
      bountyUsd: double.tryParse(meta['bountyUsd'] ?? '') ?? 0,
      bountyAddress: meta['bountyAddress'] ?? meta['address'] ?? '',
      bountyStatus: meta['bountyStatus'] ?? '',
      bountyRequiredLamports:
          int.tryParse(meta['bountyRequiredLamports'] ?? '') ?? 0,
      bountyReceivedLamports:
          int.tryParse(meta['bountyReceivedLamports'] ?? '') ?? 0,
      bountyAmountSol: double.tryParse(meta['bountyAmountSol'] ?? '') ?? 0,
      bountyPayUri: meta['bountyPayUri'] ?? meta['payUri'] ?? meta['uri'] ?? '',
      bountyPayee: meta['bountyPayee'] ?? meta['payee'] ?? '',
      bountyPayoutSig: meta['bountyPayoutSig'] ?? meta['payoutSig'] ?? '',
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

  PublishedPull _pullFromMarkdown(
    String number,
    String md, {
    List<PullEvent> events = const [],
  }) {
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
      events: events,
    );
  }

  PullEvent _pullEventFromMarkdown(String md) {
    final meta = _frontMatter(md);
    return PullEvent(
      type: meta['type'] ?? 'comment',
      body: _bodyWithoutFrontMatter(md),
      author: meta['author'] ?? '',
      authorName: meta['authorName'] ?? '',
      state: meta['state'] ?? '',
      ts: int.tryParse(meta['ts'] ?? '') ?? 0,
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
      category: meta['category'] ?? 'general',
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
        'agents',
      ]) {
        if (data[key] is List) return data[key] as List;
      }
    }
    return const [];
  }

  String? _repoKeyFromPath(String path) {
    final parts = path.split('/').where((p) => p.isNotEmpty).toList();
    if (parts.length >= 4 && parts[0] == 'api' && parts[1] == 'repo') {
      return '${parts[2]}/${parts[3]}';
    }
    return null;
  }

  Future<dynamic> _getJson(Uri uri) async {
    final monitor = _performanceMonitor;
    Future<dynamic> load() async {
      http.Response? resp;
      for (var attempt = 0; attempt < 2; attempt++) {
        resp = await http
            .get(uri, headers: _withAuth({'Accept': 'application/json'}))
            .timeout(
              Duration(seconds: uri.path.contains('/api/repo/') ? 8 : 12),
            );
        if (resp.statusCode < 500 || resp.statusCode == 503 || attempt == 1) {
          break;
        }
        await Future<void>.delayed(const Duration(milliseconds: 250));
      }
      resp!;
      return _decodeResponse(uri, resp);
    }

    if (monitor == null) return load();
    return monitor.track(
      'api.GET ${uri.path}',
      load,
      details: {
        'host': uri.host,
        'path': uri.path,
        if (uri.query.isNotEmpty) 'query': uri.query,
      },
    );
  }

  Future<dynamic> _postJson(Uri uri, Map<String, dynamic> payload) async {
    final monitor = _performanceMonitor;
    Future<dynamic> load() async {
      final resp = await http
          .post(
            uri,
            headers: _withAuth({
              'Accept': 'application/json',
              'Content-Type': 'application/json',
            }),
            body: jsonEncode(payload),
          )
          .timeout(const Duration(seconds: 12));
      return _decodeResponse(uri, resp);
    }

    if (monitor == null) return load();
    return monitor.track(
      'api.POST ${uri.path}',
      load,
      details: {'host': uri.host, 'path': uri.path},
    );
  }

  dynamic _decodeResponse(Uri uri, http.Response resp) {
    if (resp.statusCode >= 200 && resp.statusCode < 300) {
      if (resp.body.isEmpty) return const [];
      return jsonDecode(resp.body);
    }
    if (resp.statusCode == 503 && uri.path.contains('/api/repo/')) {
      final repoKey = _repoKeyFromPath(uri.path);
      if (repoKey != null) {
        _offlineRepoUntil[repoKey] = DateTime.now().add(offlineRepoTtl);
      }
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
