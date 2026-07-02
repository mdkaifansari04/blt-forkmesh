// ignore_for_file: dangling_library_doc_comments
/// Plain data models mirroring the Qt client's structs (MemberInfo, ChatMessage,
/// RepositoryRecord, Issue, PullRequest) — only the fields the Flutter UI needs.

class Member {
  Member({
    required this.id,
    required this.name,
    this.self = false,
    this.online = false,
    this.platform = '',
    this.version = '',
    this.solanaAddress = '',
    this.mirrors = const [],
  });

  final String id;
  final String name;
  final bool self;
  final bool online;
  final String platform;
  final String version;
  final String solanaAddress;
  final List<String> mirrors;
}

class ChatMessage {
  ChatMessage({
    required this.id,
    required this.conversation,
    required this.senderId,
    required this.senderName,
    required this.text,
    required this.timestamp,
    this.self = false,
    this.fileName = '',
  });

  final String id;
  final String conversation; // "#channel" or "@peerId"
  final String senderId;
  final String senderName;
  final String text;
  final DateTime timestamp;
  final bool self;
  final String fileName;
}

class Repository {
  Repository({
    required this.owner,
    required this.name,
    this.description = '',
    this.language = '',
    this.stars = 0,
    this.forks = 0,
    this.mirrors = 1,
    this.defaultBranch = 'main',
    this.isPrivate = false,
    this.liveHost = false,
    this.cloneOnline = false,
    this.updatedMs = 0,
  });

  final String owner;
  final String name;
  final String description;
  final String language;
  final int stars;
  final int forks;
  final int mirrors;
  final String defaultBranch;
  final bool isPrivate;
  final bool liveHost;
  final bool cloneOnline;
  final int updatedMs;

  String get fullName => '$owner/$name';

  factory Repository.fromJson(Map<String, dynamic> j) {
    int asInt(dynamic v) =>
        v is int ? v : (v is num ? v.toInt() : int.tryParse('$v') ?? 0);
    return Repository(
      owner: (j['owner'] ?? '').toString(),
      name: (j['name'] ?? '').toString(),
      description: (j['description'] ?? '').toString(),
      language: (j['language'] ?? '').toString(),
      stars: asInt(j['stars']),
      forks: asInt(j['forks']),
      mirrors: asInt(j['mirrors'] ?? 1),
      defaultBranch: (j['defaultBranch'] ?? 'main').toString(),
      isPrivate: j['isPrivate'] == true,
      liveHost: j['liveHost'] == true,
      cloneOnline: j['cloneOnline'] == true,
      updatedMs: asInt(j['updatedAt'] ?? j['publishedAt'] ?? 0),
    );
  }
}

class Issue {
  Issue({
    required this.number,
    required this.title,
    this.status = 'open',
    this.body = '',
    this.author = '',
    this.labels = const [],
    this.events = const [],
    this.votes = 0,
    this.bountyUsd = 0,
  });

  final int number;
  final String title;
  final String status; // open | closed
  final String body;
  final String author;
  final List<String> labels;
  final List<IssueEvent> events;
  final int votes;
  final double bountyUsd;

  bool get isOpen => status != 'closed';

  factory Issue.fromJson(Map<String, dynamic> j) {
    int asInt(dynamic v) =>
        v is int ? v : (v is num ? v.toInt() : int.tryParse('$v') ?? 0);
    return Issue(
      number: asInt(j['number']),
      title: (j['title'] ?? '').toString(),
      status: (j['status'] ?? 'open').toString(),
      body: (j['body'] ?? '').toString(),
      author: (j['author'] ?? '').toString(),
      labels:
          (j['labels'] as List?)?.map((e) => e.toString()).toList() ?? const [],
      events:
          (j['events'] as List?)
              ?.whereType<Map>()
              .map((e) => IssueEvent.fromJson(Map<String, dynamic>.from(e)))
              .toList() ??
          const [],
      votes: asInt(j['votes']),
      bountyUsd: (j['bountyUsd'] is num)
          ? (j['bountyUsd'] as num).toDouble()
          : 0,
    );
  }
}

class IssueEvent {
  IssueEvent({
    required this.type,
    this.body = '',
    this.author = '',
    this.authorName = '',
    this.status = '',
    this.ts = 0,
  });

  final String type;
  final String body;
  final String author;
  final String authorName;
  final String status;
  final int ts;

  String get displayAuthor => authorName.isNotEmpty ? authorName : author;

  factory IssueEvent.fromJson(Map<String, dynamic> j) {
    int asInt(dynamic v) =>
        v is int ? v : (v is num ? v.toInt() : int.tryParse('$v') ?? 0);
    return IssueEvent(
      type: (j['type'] ?? '').toString(),
      body: (j['body'] ?? '').toString(),
      author: (j['author'] ?? '').toString(),
      authorName: (j['authorName'] ?? '').toString(),
      status: (j['status'] ?? '').toString(),
      ts: asInt(j['ts']),
    );
  }
}

class PullRequest {
  PullRequest({
    required this.number,
    required this.title,
    this.status = 'open',
    this.body = '',
    this.author = '',
    this.baseBranch = '',
    this.headBranch = '',
    this.createdMs = 0,
  });

  final int number;
  final String title;
  final String status; // open | merged | closed
  final String body;
  final String author;
  final String baseBranch;
  final String headBranch;
  final int createdMs; // epoch ms the PR was opened; 0 when unknown

  /// "yyyy-MM-dd" the PR was opened, or "" when no timestamp is available.
  /// Mirrors the Created column on the Qt client's pull request list.
  String get createdDate {
    if (createdMs <= 0) return '';
    final d = DateTime.fromMillisecondsSinceEpoch(createdMs);
    final m = d.month.toString().padLeft(2, '0');
    final day = d.day.toString().padLeft(2, '0');
    return '${d.year}-$m-$day';
  }

  factory PullRequest.fromJson(Map<String, dynamic> j) {
    int asInt(dynamic v) =>
        v is int ? v : (v is num ? v.toInt() : int.tryParse('$v') ?? 0);
    return PullRequest(
      number: asInt(j['number']),
      title: (j['title'] ?? '').toString(),
      status: (j['status'] ?? 'open').toString(),
      body: (j['body'] ?? '').toString(),
      author: (j['author'] ?? '').toString(),
      baseBranch: (j['base'] ?? j['baseBranch'] ?? '').toString(),
      headBranch: (j['head'] ?? j['headBranch'] ?? '').toString(),
      createdMs: asInt(j['createdAt'] ?? j['ts'] ?? j['submittedAt'] ?? 0),
    );
  }
}

class RepoTreeEntry {
  RepoTreeEntry({
    required this.name,
    required this.path,
    required this.type,
    this.size = 0,
    this.sha = '',
  });

  final String name;
  final String path;
  final String type; // file | dir | symlink | submodule
  final int size;
  final String sha;

  bool get isDirectory => type == 'dir' || type == 'tree' || type == 'folder';

  factory RepoTreeEntry.fromJson(Map<String, dynamic> j) {
    int asInt(dynamic v) =>
        v is int ? v : (v is num ? v.toInt() : int.tryParse('$v') ?? 0);
    final rawPath = (j['path'] ?? j['name'] ?? '').toString();
    final name =
        (j['name'] ??
                rawPath.split('/').where((p) => p.isNotEmpty).lastOrNull ??
                rawPath)
            .toString();
    final rawType = (j['type'] ?? j['kind'] ?? j['mode'] ?? '')
        .toString()
        .toLowerCase();
    final type =
        rawType.contains('tree') ||
            rawType == 'directory' ||
            rawType == 'folder' ||
            rawType == 'dir'
        ? 'dir'
        : 'file';
    return RepoTreeEntry(
      name: name,
      path: rawPath,
      type: type,
      size: asInt(j['size'] ?? j['bytes']),
      sha: (j['sha'] ?? j['hash'] ?? j['oid'] ?? '').toString(),
    );
  }
}

class RepoTree {
  RepoTree({
    required this.path,
    required this.entries,
    this.source = '',
    this.cachedAt = 0,
  });

  final String path;
  final List<RepoTreeEntry> entries;
  final String source;
  final int cachedAt;

  factory RepoTree.fromJson(dynamic data, {String path = ''}) {
    int asInt(dynamic v) =>
        v is int ? v : (v is num ? v.toInt() : int.tryParse('$v') ?? 0);
    List<dynamic> rawEntries = const [];
    String source = '';
    var cachedAt = 0;
    if (data is List) {
      rawEntries = data;
    } else if (data is Map<String, dynamic>) {
      for (final key in ['entries', 'tree', 'items', 'files', 'data']) {
        if (data[key] is List) {
          rawEntries = data[key] as List;
          break;
        }
      }
      source = (data['source'] ?? data['servedBy'] ?? '').toString();
      cachedAt = asInt(data['cachedAt'] ?? data['ts']);
      path = (data['path'] ?? path).toString();
    }
    final entries =
        rawEntries
            .whereType<Map<String, dynamic>>()
            .map(RepoTreeEntry.fromJson)
            .map((e) {
              if (path.isEmpty || e.path.contains('/')) return e;
              return RepoTreeEntry(
                name: e.name,
                path: '$path/${e.path}',
                type: e.type,
                size: e.size,
                sha: e.sha,
              );
            })
            .where((e) => e.name.isNotEmpty)
            .toList()
          ..sort((a, b) {
            if (a.isDirectory != b.isDirectory) {
              return a.isDirectory ? -1 : 1;
            }
            return a.name.toLowerCase().compareTo(b.name.toLowerCase());
          });
    return RepoTree(
      path: path,
      entries: entries,
      source: source,
      cachedAt: cachedAt,
    );
  }
}

class RepoBlob {
  RepoBlob({
    required this.path,
    required this.content,
    this.encoding = '',
    this.size = 0,
    this.source = '',
  });

  final String path;
  final String content;
  final String encoding;
  final int size;
  final String source;

  factory RepoBlob.fromJson(dynamic data, {String path = ''}) {
    int asInt(dynamic v) =>
        v is int ? v : (v is num ? v.toInt() : int.tryParse('$v') ?? 0);
    if (data is String) {
      return RepoBlob(path: path, content: data, size: data.length);
    }
    if (data is Map<String, dynamic>) {
      return RepoBlob(
        path: (data['path'] ?? path).toString(),
        content: (data['content'] ?? data['text'] ?? data['data'] ?? '')
            .toString(),
        encoding: (data['encoding'] ?? '').toString(),
        size: asInt(data['size'] ?? data['bytes']),
        source: (data['source'] ?? data['servedBy'] ?? '').toString(),
      );
    }
    return RepoBlob(path: path, content: '');
  }
}

class RepoBranch {
  RepoBranch({required this.name, this.sha = '', this.isDefault = false});

  final String name;
  final String sha;
  final bool isDefault;

  factory RepoBranch.fromJson(Map<String, dynamic> j) => RepoBranch(
    name: (j['name'] ?? j['branch'] ?? '').toString(),
    sha: (j['sha'] ?? j['hash'] ?? j['commit'] ?? '').toString(),
    isDefault: j['default'] == true || j['isDefault'] == true,
  );
}

class RepoMirror {
  RepoMirror({
    required this.owner,
    required this.name,
    this.online = false,
    this.lastSeenMs = 0,
    this.firstHostedMs = 0,
  });

  final String owner;
  final String name;
  final bool online;
  final int lastSeenMs;
  final int firstHostedMs;

  String get label => owner.isEmpty ? name : '$owner/$name';

  factory RepoMirror.fromJson(Map<String, dynamic> j) {
    int asInt(dynamic v) =>
        v is int ? v : (v is num ? v.toInt() : int.tryParse('$v') ?? 0);
    return RepoMirror(
      owner: (j['owner'] ?? j['node'] ?? j['name'] ?? '').toString(),
      name: (j['repo'] ?? j['repository'] ?? j['name'] ?? '').toString(),
      online:
          j['online'] == true || j['live'] == true || j['status'] == 'online',
      lastSeenMs: asInt(j['lastSeen'] ?? j['lastSeenAt'] ?? j['ts']),
      firstHostedMs: asInt(j['firstHosted'] ?? j['firstHostedAt']),
    );
  }
}

class RepoDiscussion {
  RepoDiscussion({
    required this.number,
    required this.title,
    this.body = '',
    this.author = '',
    this.updatedMs = 0,
    this.events = const [],
  });

  final int number;
  final String title;
  final String body;
  final String author;
  final int updatedMs;
  final List<DiscussionEvent> events;
}

class DiscussionEvent {
  DiscussionEvent({
    required this.type,
    this.body = '',
    this.author = '',
    this.authorName = '',
    this.ts = 0,
  });

  final String type;
  final String body;
  final String author;
  final String authorName;
  final int ts;

  String get displayAuthor => authorName.isNotEmpty ? authorName : author;
}

class PublishedPull {
  PublishedPull({
    required this.number,
    required this.title,
    this.status = 'open',
    this.body = '',
    this.base = '',
    this.head = '',
    this.patch = '',
    this.signed = false,
  });

  final int number;
  final String title;
  final String status;
  final String body;
  final String base;
  final String head;
  final String patch;
  final bool signed;
}

class NetworkStats {
  NetworkStats({this.nodesOnline = 0, this.hostsOnline = 0, this.repos = 0});
  final int nodesOnline;
  final int hostsOnline;
  final int repos;

  factory NetworkStats.fromJson(Map<String, dynamic> j) {
    int asInt(dynamic v) =>
        v is int ? v : (v is num ? v.toInt() : int.tryParse('$v') ?? 0);
    return NetworkStats(
      nodesOnline: asInt(j['nodesOnline'] ?? j['clients'] ?? 0),
      hostsOnline: asInt(j['hostsOnline'] ?? j['hosts'] ?? 0),
      repos: asInt(j['repos'] ?? j['repositories'] ?? 0),
    );
  }
}
