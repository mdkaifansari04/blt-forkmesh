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
  final int updatedMs;

  String get fullName => '$owner/$name';

  factory Repository.fromJson(Map<String, dynamic> j) {
    int asInt(dynamic v) => v is int ? v : (v is num ? v.toInt() : int.tryParse('$v') ?? 0);
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
    this.votes = 0,
    this.bountyUsd = 0,
  });

  final int number;
  final String title;
  final String status; // open | closed
  final String body;
  final String author;
  final List<String> labels;
  final int votes;
  final double bountyUsd;

  bool get isOpen => status != 'closed';

  factory Issue.fromJson(Map<String, dynamic> j) {
    int asInt(dynamic v) => v is int ? v : (v is num ? v.toInt() : int.tryParse('$v') ?? 0);
    return Issue(
      number: asInt(j['number']),
      title: (j['title'] ?? '').toString(),
      status: (j['status'] ?? 'open').toString(),
      body: (j['body'] ?? '').toString(),
      author: (j['author'] ?? '').toString(),
      labels: (j['labels'] as List?)?.map((e) => e.toString()).toList() ?? const [],
      votes: asInt(j['votes']),
      bountyUsd: (j['bountyUsd'] is num) ? (j['bountyUsd'] as num).toDouble() : 0,
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
  });

  final int number;
  final String title;
  final String status; // open | merged | closed
  final String body;
  final String author;
  final String baseBranch;
  final String headBranch;

  factory PullRequest.fromJson(Map<String, dynamic> j) {
    int asInt(dynamic v) => v is int ? v : (v is num ? v.toInt() : int.tryParse('$v') ?? 0);
    return PullRequest(
      number: asInt(j['number']),
      title: (j['title'] ?? '').toString(),
      status: (j['status'] ?? 'open').toString(),
      body: (j['body'] ?? '').toString(),
      author: (j['author'] ?? '').toString(),
      baseBranch: (j['base'] ?? j['baseBranch'] ?? '').toString(),
      headBranch: (j['head'] ?? j['headBranch'] ?? '').toString(),
    );
  }
}

class NetworkStats {
  NetworkStats({this.nodesOnline = 0, this.hostsOnline = 0, this.repos = 0});
  final int nodesOnline;
  final int hostsOnline;
  final int repos;

  factory NetworkStats.fromJson(Map<String, dynamic> j) {
    int asInt(dynamic v) => v is int ? v : (v is num ? v.toInt() : int.tryParse('$v') ?? 0);
    return NetworkStats(
      nodesOnline: asInt(j['nodesOnline'] ?? j['clients'] ?? 0),
      hostsOnline: asInt(j['hostsOnline'] ?? j['hosts'] ?? 0),
      repos: asInt(j['repos'] ?? j['repositories'] ?? 0),
    );
  }
}
