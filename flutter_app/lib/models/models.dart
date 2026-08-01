



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
    this.owner = '',
    this.nodeName = '',
  });

  final String id;
  final String name;
  final bool self;
  final bool online;
  final String platform;
  final String version;
  final String solanaAddress;
  final List<String> mirrors;




  final String owner;



  final String nodeName;
}





class MemberGroup {
  MemberGroup({
    required this.name,
    required this.members,
    this.self = false,
    this.disambiguator = '',
  });

  final String name;


  final List<Member> members;

  final bool self;




  final String disambiguator;

  bool get online => members.any((m) => m.online);


  Member get primary => members.first;

  String get id => primary.id;

  int get nodeCount => members.length;
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
  final String conversation;
  final String senderId;
  final String senderName;
  final String text;
  final DateTime timestamp;
  final bool self;
  final String fileName;
}

class NotificationPage {
  NotificationPage({required this.notifications, required this.unread});

  final List<ForkNotification> notifications;
  final int unread;

  factory NotificationPage.fromJson(Map<String, dynamic> json) {
    int asInt(dynamic v) =>
        v is int ? v : (v is num ? v.toInt() : int.tryParse('$v') ?? 0);
    final raw = json['notifications'] is List
        ? json['notifications'] as List
        : json['items'] is List
        ? json['items'] as List
        : const [];
    final notifications = raw
        .whereType<Map>()
        .map(
          (item) => ForkNotification.fromJson(Map<String, dynamic>.from(item)),
        )
        .toList();
    return NotificationPage(
      notifications: notifications,
      unread: asInt(json['unread']),
    );
  }
}





class FederatedReply {
  const FederatedReply({
    required this.remoteId,
    required this.author,
    required this.body,
    this.authorName = '',
    this.authorUrl = '',
    this.backlink = '',
    this.parentRemoteId = '',
    this.sourceInstance = '',
    this.sourceSoftware = 'activitypub',
    this.lifecycle = 'active',
    this.depth = 0,
    this.timestamp = 0,
    this.nativeEvent = false,
  });

  final String remoteId;
  final String parentRemoteId;
  final String author;
  final String authorName;
  final String authorUrl;
  final String body;
  final String backlink;
  final String sourceInstance;
  final String sourceSoftware;
  final String lifecycle;
  final int depth;
  final int timestamp;
  final bool nativeEvent;

  bool get edited => lifecycle == 'edited';
  bool get tombstone => lifecycle == 'tombstoned';
  bool get moderated =>
      lifecycle == 'moderated' || lifecycle == 'awaiting-redelivery';
  String get displayBody => tombstone
      ? 'Deleted on the remote instance'
      : moderated
      ? 'Hidden by remote moderation'
      : body;

  factory FederatedReply.fromJson(Map<String, dynamic> json) {
    int asInt(dynamic value) => value is int
        ? value
        : value is num
        ? value.toInt()
        : int.tryParse('$value') ?? 0;
    final provenance = json['provenance'] is Map
        ? Map<String, dynamic>.from(json['provenance'] as Map)
        : const <String, dynamic>{};
    return FederatedReply(
      remoteId: (json['remoteId'] ?? json['id'] ?? '').toString(),
      parentRemoteId: (json['parentRemoteId'] ?? '').toString(),
      author: (json['author'] ?? '').toString(),
      authorName: (json['authorName'] ?? '').toString(),
      authorUrl: (json['authorUrl'] ?? '').toString(),
      body: (json['body'] ?? '').toString(),
      backlink:
          (json['url'] ??
                  json['backlink'] ??
                  provenance['backlink'] ??
                  json['remoteId'] ??
                  '')
              .toString(),
      sourceInstance: (json['sourceInstance'] ?? provenance['instance'] ?? '')
          .toString(),
      sourceSoftware:
          (json['sourceSoftware'] ?? provenance['software'] ?? 'activitypub')
              .toString(),
      lifecycle:
          (json['lifecycle'] ??
                  (json['tombstone'] == true
                      ? 'tombstoned'
                      : json['moderated'] == true
                      ? 'moderated'
                      : json['edited'] == true
                      ? 'edited'
                      : 'active'))
              .toString(),
      depth: asInt(json['depth']).clamp(0, 8),
      timestamp: asInt(json['ts']),

      nativeEvent: false,
    );
  }
}

class ForkNotification {
  ForkNotification({
    required this.id,
    required this.kind,
    required this.title,
    this.body = '',
    this.repo = '',
    this.href = '',
    this.actor = '',
    this.source = '',
    this.ts = 0,
    this.readAt = 0,
    this.meta = const {},
  });

  final String id;
  final String kind;
  final String title;
  final String body;
  final String repo;
  final String href;
  final String actor;
  final String source;
  final int ts;
  final int readAt;
  final Map<String, dynamic> meta;

  bool get isUnread => readAt <= 0;

  String get kindLabel => switch (kind) {
    'mention' => 'Mention',
    'subscribed' => 'Subscribed',
    'pull_submitted' => 'Pull request',
    'issue_assigned' => 'Assignment',
    'repo_shared' => 'Repo share',
    'bounty_funded' || 'bounty_paid' => 'Bounty',
    'release_published' => 'Release',
    'host_online' || 'host_offline' => 'Host',
    'credits_refilled' => 'Credits',
    'pending_inbox' => 'Inbox',
    _ => 'Notification',
  };

  String get filterGroup {
    if (kind == 'mention') return 'Mentions';
    if (kind == 'host_online' ||
        kind == 'host_offline' ||
        kind == 'release_published' ||
        kind == 'bounty_funded' ||
        kind == 'bounty_paid' ||
        kind == 'credits_refilled') {
      return 'System';
    }
    return 'Repo';
  }

  ForkNotification copyWith({int? readAt}) => ForkNotification(
    id: id,
    kind: kind,
    title: title,
    body: body,
    repo: repo,
    href: href,
    actor: actor,
    source: source,
    ts: ts,
    readAt: readAt ?? this.readAt,
    meta: meta,
  );

  factory ForkNotification.fromJson(Map<String, dynamic> json) {
    int asInt(dynamic v) =>
        v is int ? v : (v is num ? v.toInt() : int.tryParse('$v') ?? 0);
    return ForkNotification(
      id: (json['id'] ?? '').toString(),
      kind: (json['kind'] ?? 'pending_inbox').toString(),
      title: (json['title'] ?? 'Notification').toString(),
      body: (json['body'] ?? '').toString(),
      repo: (json['repo'] ?? '').toString(),
      href: (json['href'] ?? '').toString(),
      actor: (json['actor'] ?? '').toString(),
      source: (json['source'] ?? '').toString(),
      ts: asInt(json['ts']),
      readAt: asInt(json['readAt'] ?? json['read_at']),
      meta: json['meta'] is Map
          ? Map<String, dynamic>.from(json['meta'] as Map)
          : const {},
    );
  }
}

class RepoActions {
  const RepoActions({this.workflows = const [], this.runs = const []});

  final List<ActionWorkflow> workflows;
  final List<ActionRun> runs;

  factory RepoActions.fromJson(Map<String, dynamic> json) {
    final workflowsRaw = json['workflows'] is List
        ? json['workflows'] as List
        : const [];
    final runsRaw = json['runs'] is List ? json['runs'] as List : const [];
    return RepoActions(
      workflows: workflowsRaw
          .whereType<Map>()
          .map(
            (item) => ActionWorkflow.fromJson(Map<String, dynamic>.from(item)),
          )
          .toList(),
      runs: runsRaw
          .whereType<Map>()
          .map((item) => ActionRun.fromJson(Map<String, dynamic>.from(item)))
          .toList(),
    );
  }
}

class ActionWorkflow {
  const ActionWorkflow({
    this.path = '',
    this.name = '',
    this.triggers = const [],
    this.stepCount = 0,
    this.manual = false,
    this.valid = true,
    this.error = '',
  });

  final String path;
  final String name;
  final List<String> triggers;
  final int stepCount;
  final bool manual;
  final bool valid;
  final String error;

  String get displayName => name.isNotEmpty ? name : path;
  String get triggerLabel =>
      triggers.isEmpty ? 'No triggers' : triggers.join(', ');

  factory ActionWorkflow.fromJson(Map<String, dynamic> json) {
    int asInt(dynamic v) =>
        v is int ? v : (v is num ? v.toInt() : int.tryParse('$v') ?? 0);
    final rawTriggers = json['on'] is List
        ? json['on'] as List
        : json['triggers'] is List
        ? json['triggers'] as List
        : json['on'] is String
        ? [json['on']]
        : const [];
    return ActionWorkflow(
      path: (json['path'] ?? '').toString(),
      name: (json['name'] ?? '').toString(),
      triggers: rawTriggers.map((value) => value.toString()).toList(),
      stepCount: asInt(json['stepCount'] ?? json['steps']),
      manual: json['manual'] == true,
      valid: json['valid'] != false,
      error: (json['error'] ?? '').toString(),
    );
  }
}

class ActionRun {
  const ActionRun({
    required this.id,
    this.workflowPath = '',
    this.workflowName = '',
    this.commit = '',
    this.ref = '',
    this.status = '',
    this.createdAtMs = 0,
    this.startedAtMs = 0,
    this.finishedAtMs = 0,
  });

  final int id;
  final String workflowPath;
  final String workflowName;
  final String commit;
  final String ref;
  final String status;
  final int createdAtMs;
  final int startedAtMs;
  final int finishedAtMs;

  bool get isActive {
    final clean = status.toLowerCase().replaceAll('_', '-');
    return clean == 'queued' ||
        clean == 'running' ||
        clean == 'awaiting-approval';
  }

  String get displayName =>
      workflowName.isNotEmpty ? workflowName : 'Action run #$id';

  String get statusLabel {
    final clean = status.toLowerCase().replaceAll('_', '-');
    return switch (clean) {
      'awaiting-approval' => 'Awaiting approval',
      'queued' => 'Queued',
      'running' => 'Running',
      'success' || 'passed' => 'Success',
      'failed' || 'failure' || 'error' => 'Failed',
      'rejected' => 'Rejected',
      'cancelled' || 'canceled' || 'stopped' => 'Cancelled',
      _ => status.isEmpty ? 'Unknown' : _titleCase(status),
    };
  }

  String get refLabel {
    if (ref.startsWith('refs/heads/')) {
      return ref.substring('refs/heads/'.length);
    }
    if (ref.startsWith('refs/tags/')) return ref.substring('refs/tags/'.length);
    return ref;
  }

  String get shortCommit =>
      commit.length <= 8 ? commit : commit.substring(0, 8);

  String get durationLabel {
    if (startedAtMs <= 0 || finishedAtMs <= startedAtMs) return '';
    final totalSeconds = (finishedAtMs - startedAtMs) ~/ 1000;
    final minutes = totalSeconds ~/ 60;
    final seconds = totalSeconds % 60;
    if (minutes <= 0) return '${seconds}s';
    return '${minutes}m ${seconds}s';
  }

  factory ActionRun.fromJson(Map<String, dynamic> json) {
    int asInt(dynamic v) =>
        v is int ? v : (v is num ? v.toInt() : int.tryParse('$v') ?? 0);
    return ActionRun(
      id: asInt(json['id']),
      workflowPath: (json['workflowPath'] ?? '').toString(),
      workflowName: (json['workflowName'] ?? '').toString(),
      commit: (json['commit'] ?? '').toString(),
      ref: (json['ref'] ?? '').toString(),
      status: (json['status'] ?? '').toString(),
      createdAtMs: asInt(json['createdAtMs']),
      startedAtMs: asInt(json['startedAtMs']),
      finishedAtMs: asInt(json['finishedAtMs']),
    );
  }
}

class ActionLog {
  const ActionLog({required this.id, this.status = '', this.log = ''});

  final int id;
  final String status;
  final String log;

  String get statusLabel => ActionRun(id: id, status: status).statusLabel;

  factory ActionLog.fromJson(Map<String, dynamic> json) {
    int asInt(dynamic v) =>
        v is int ? v : (v is num ? v.toInt() : int.tryParse('$v') ?? 0);
    return ActionLog(
      id: asInt(json['id']),
      status: (json['status'] ?? '').toString(),
      log: (json['log'] ?? '').toString(),
    );
  }
}

class DesktopCommandResult {
  const DesktopCommandResult({required this.ok, this.queued = 0});

  final bool ok;
  final int queued;

  factory DesktopCommandResult.fromJson(Map<String, dynamic> json) {
    final rawQueued = json['queued'];
    final queued = rawQueued is int
        ? rawQueued
        : rawQueued is num
        ? rawQueued.toInt()
        : int.tryParse('$rawQueued') ?? 0;
    return DesktopCommandResult(ok: json['ok'] == true, queued: queued);
  }
}

class AgentTranscript {
  const AgentTranscript({required this.status, required this.transcript});

  final String status;
  final String transcript;

  factory AgentTranscript.fromJson(Map<String, dynamic> json) =>
      AgentTranscript(
        status: (json['status'] ?? '').toString(),
        transcript: (json['transcript'] ?? '').toString(),
      );
}

class AgentSession {
  AgentSession({
    required this.id,
    this.issueNumber = 0,
    this.issueTitle = '',
    this.status = '',
    this.provider = '',
    this.model = '',
    this.branchName = '',
    this.lastError = '',
    this.createdAtMs = 0,
    this.startedAtMs = 0,
    this.finishedAtMs = 0,
    this.numTurns = 0,
    this.durationMs = 0,
    this.costUsd = 0,
    this.createPr = false,
    this.prNumber = 0,
    this.baseRef = '',
    this.baseBranch = '',
    this.merged = false,
    this.mergedAtMs = 0,
    this.promptTokens = 0,
    this.completionTokens = 0,
    this.totalTokens = 0,
    this.contextTokens = 0,
    this.contextWindow = 0,
    this.maxOutputTokens = 0,
    this.estimatedCredits = 0,
    this.spendBeforeUsd = 0,
    this.spendAfterUsd = 0,
    this.filesChanged = -1,
    this.ahead = -1,
    this.behind = -1,
    this.conflicted = false,
  });

  final int id;
  final int issueNumber;
  final String issueTitle;
  final String status;
  final String provider;
  final String model;
  final String branchName;
  final String lastError;
  final int createdAtMs;
  final int startedAtMs;
  final int finishedAtMs;
  final int numTurns;
  final int durationMs;
  final double costUsd;
  final bool createPr;
  final int prNumber;
  final String baseRef;
  final String baseBranch;
  final bool merged;
  final int mergedAtMs;
  final int promptTokens;
  final int completionTokens;
  final int totalTokens;
  final int contextTokens;
  final int contextWindow;
  final int maxOutputTokens;
  final int estimatedCredits;
  final double spendBeforeUsd;
  final double spendAfterUsd;
  final int filesChanged;
  final int ahead;
  final int behind;
  final bool conflicted;

  bool get isActive {
    if (merged) return false;
    final clean = status.toLowerCase().replaceAll('_', '-');
    return clean == 'queued' || clean == 'running' || clean == 'waiting';
  }

  String get displayTitle => issueTitle.isNotEmpty
      ? issueTitle
      : issueNumber > 0
      ? 'Issue #$issueNumber'
      : 'Agent session #$id';

  String get statusLabel {
    if (merged) return 'Merged';
    final clean = status.toLowerCase().replaceAll('_', '-');
    return switch (clean) {
      'queued' => 'Queued',
      'running' => 'Running',
      'waiting' || 'waiting-for-input' => 'Waiting',
      'done' || 'completed' || 'success' => 'Done',
      'failed' || 'error' => 'Failed',
      'stopped' || 'cancelled' || 'canceled' => 'Stopped',
      _ => status.isEmpty ? 'Unknown' : _titleCase(status),
    };
  }

  String get providerLabel => switch (provider.toLowerCase()) {
    'claude-code' => 'Claude Code',
    'claude-api' => 'Claude API',
    'openai' => 'OpenAI',
    'codex' => 'Codex',
    _ => provider.isEmpty ? 'Agent' : _titleCase(provider),
  };

  String get durationLabel {
    if (durationMs <= 0) return '';
    final totalSeconds = durationMs ~/ 1000;
    final minutes = totalSeconds ~/ 60;
    final seconds = totalSeconds % 60;
    if (minutes <= 0) return '${seconds}s';
    return '${minutes}m ${seconds}s';
  }

  String get costLabel {
    if (costUsd <= 0) return '';
    if (costUsd < 0.01) return '<\$0.01';
    return '\$${costUsd.toStringAsFixed(2)}';
  }

  String get prLabel => prNumber > 0 ? 'PR #$prNumber' : '';

  String get tokenLabel => totalTokens > 0 ? '$totalTokens tokens' : '';

  String get diffLabel {
    final parts = <String>[];
    if (filesChanged >= 0) {
      parts.add(filesChanged == 1 ? '1 file' : '$filesChanged files');
    }
    if (ahead >= 0 && behind >= 0 && (ahead > 0 || behind > 0)) {
      parts.add('↑$ahead ↓$behind');
    }
    return parts.join(' · ');
  }

  factory AgentSession.fromJson(Map<String, dynamic> json) {
    int asInt(dynamic value) => value is int
        ? value
        : value is num
        ? value.toInt()
        : int.tryParse('$value') ?? 0;
    int asOptionalInt(dynamic value) {
      if (value == null) return -1;
      return value is int
          ? value
          : value is num
          ? value.toInt()
          : int.tryParse('$value') ?? -1;
    }

    double asDouble(dynamic value) =>
        value is num ? value.toDouble() : double.tryParse('$value') ?? 0;
    final diffStats = json['diffStats'] is Map
        ? Map<String, dynamic>.from(json['diffStats'] as Map)
        : const <String, dynamic>{};
    return AgentSession(
      id: asInt(json['id']),
      issueNumber: asInt(json['issueNumber']),
      issueTitle: (json['issueTitle'] ?? '').toString(),
      status: (json['status'] ?? '').toString(),
      provider: (json['provider'] ?? '').toString(),
      model: (json['model'] ?? '').toString(),
      branchName: (json['branchName'] ?? '').toString(),
      lastError: (json['lastError'] ?? '').toString(),
      createdAtMs: asInt(json['createdAtMs']),
      startedAtMs: asInt(json['startedAtMs']),
      finishedAtMs: asInt(json['finishedAtMs']),
      numTurns: asInt(json['numTurns']),
      durationMs: asInt(json['durationMs']),
      costUsd: asDouble(json['costUsd']),
      createPr: json['createPr'] == true,
      prNumber: asInt(json['prNumber']),
      baseRef: (json['baseRef'] ?? '').toString(),
      baseBranch: (json['baseBranch'] ?? '').toString(),
      merged: json['merged'] == true,
      mergedAtMs: asInt(json['mergedAtMs']),
      promptTokens: asInt(json['promptTokens']),
      completionTokens: asInt(json['completionTokens']),
      totalTokens: asInt(json['totalTokens']),
      contextTokens: asInt(json['contextTokens']),
      contextWindow: asInt(json['contextWindow']),
      maxOutputTokens: asInt(json['maxOutputTokens']),
      estimatedCredits: asInt(json['estimatedCredits']),
      spendBeforeUsd: asDouble(json['spendBeforeUsd']),
      spendAfterUsd: asDouble(json['spendAfterUsd']),
      filesChanged: asOptionalInt(
        diffStats['files'] ?? json['filesChanged'] ?? json['changedFiles'],
      ),
      ahead: asOptionalInt(diffStats['ahead'] ?? json['ahead']),
      behind: asOptionalInt(diffStats['behind'] ?? json['behind']),
      conflicted: diffStats['conflicted'] == true || json['conflicted'] == true,
    );
  }
}

String _titleCase(String value) {
  final clean = value.replaceAll('_', ' ').replaceAll('-', ' ').trim();
  if (clean.isEmpty) return '';
  return clean
      .split(RegExp(r'\s+'))
      .map(
        (part) => part.isEmpty
            ? part
            : '${part[0].toUpperCase()}${part.substring(1).toLowerCase()}',
      )
      .join(' ');
}

int _modelInt(dynamic value) => value is int
    ? value
    : (value is num ? value.toInt() : int.tryParse('$value') ?? 0);

double _modelDouble(dynamic value) =>
    value is num ? value.toDouble() : double.tryParse('$value') ?? 0;

bool _modelBool(dynamic value) {
  if (value is bool) return value;
  final text = '$value'.trim().toLowerCase();
  return text == 'true' || text == '1' || text == 'yes';
}

String _modelString(dynamic value) => value == null ? '' : '$value';

String _firstModelString(Map<String, dynamic> json, List<String> keys) {
  for (final key in keys) {
    final value = _modelString(json[key]).trim();
    if (value.isNotEmpty) return value;
  }
  return '';
}

const solanaExplorerBaseUrl = 'https://explorer.solana.com';

String solanaExplorerAddressUrl(String address) {
  final clean = address.trim();
  if (clean.isEmpty) return '';
  return '$solanaExplorerBaseUrl/address/${Uri.encodeComponent(clean)}';
}

String solanaExplorerSignatureUrl(String signature) {
  final clean = signature.trim();
  if (clean.isEmpty) return '';
  return '$solanaExplorerBaseUrl/tx/${Uri.encodeComponent(clean)}';
}

String _trimModelDouble(double value) {
  final text = value.toString();
  return text.contains('.') ? text.replaceFirst(RegExp(r'\.?0+$'), '') : text;
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
    this.solana = '',
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
  final String solana;

  String get fullName => '$owner/$name';
  String get donationAddress => solana;

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
      solana: _firstModelString(j, const [
        'solana',
        'donationAddress',
        'payoutAddress',
      ]),
    );
  }
}

class BountyWallet {
  const BountyWallet({
    this.address = '',
    this.balanceLamports = 0,
    this.balanceSol = 0,
    this.payUri = '',
  });

  final String address;
  final int balanceLamports;
  final double balanceSol;
  final String payUri;

  bool get hasWallet => address.trim().isNotEmpty;

  String get balanceLabel {
    if (balanceSol > 0) {
      return '${_trimModelDouble(balanceSol)} SOL ($balanceLamports lamports)';
    }
    return '$balanceLamports lamports';
  }

  String get shortAddress {
    final clean = address.trim();
    if (clean.length <= 16) return clean;
    return '${clean.substring(0, 6)}...${clean.substring(clean.length - 6)}';
  }

  String get explorerAddressUrl => solanaExplorerAddressUrl(address);

  factory BountyWallet.fromJson(dynamic data) {
    final source = data is Map<String, dynamic>
        ? data
        : data is Map
        ? Map<String, dynamic>.from(data)
        : const <String, dynamic>{};
    return BountyWallet(
      address: _firstModelString(source, const ['address', 'walletAddress']),
      balanceLamports: _modelInt(source['balanceLamports']),
      balanceSol: _modelDouble(source['balanceSol']),
      payUri: _firstModelString(source, const ['uri', 'payUri']),
    );
  }
}

class IssueBounty {
  const IssueBounty({
    this.address = '',
    this.status = 'open',
    this.amountUsd = 0,
    this.requiredLamports = 0,
    this.receivedLamports = 0,
    this.amountSol = 0,
    this.payUri = '',
    this.payee = '',
    this.payoutSig = '',
    this.confirmed = false,
  });

  final String address;
  final String status;
  final double amountUsd;
  final int requiredLamports;
  final int receivedLamports;
  final double amountSol;
  final String payUri;
  final String payee;
  final String payoutSig;
  final bool confirmed;

  bool get hasFunding =>
      address.isNotEmpty ||
      amountUsd > 0 ||
      requiredLamports > 0 ||
      receivedLamports > 0 ||
      payoutSig.isNotEmpty;

  String get statusLabel {
    final clean = status.trim();
    if (clean.isNotEmpty) return clean;
    if (payoutSig.isNotEmpty) return 'paid';
    if (requiredLamports > 0 && receivedLamports >= requiredLamports) {
      return 'funded';
    }
    return 'open';
  }

  String get progressLabel {
    if (requiredLamports > 0) {
      return '$receivedLamports / $requiredLamports lamports';
    }
    if (receivedLamports > 0) return '$receivedLamports lamports received';
    return '';
  }

  factory IssueBounty.fromJson(dynamic data) {
    final source = data is Map<String, dynamic>
        ? data
        : data is Map
        ? Map<String, dynamic>.from(data)
        : const <String, dynamic>{};
    final nested = source['bounty'] is Map
        ? Map<String, dynamic>.from(source['bounty'] as Map)
        : source['funding'] is Map
        ? Map<String, dynamic>.from(source['funding'] as Map)
        : source;
    return IssueBounty(
      address: _firstModelString(nested, const ['address', 'bountyAddress']),
      status: _firstModelString(nested, const [
        'status',
        'bountyStatus',
      ]).ifEmpty('open'),
      amountUsd: _modelDouble(nested['amountUsd'] ?? nested['bountyUsd']),
      requiredLamports: _modelInt(
        nested['requiredLamports'] ?? nested['bountyRequiredLamports'],
      ),
      receivedLamports: _modelInt(
        nested['receivedLamports'] ?? nested['bountyReceivedLamports'],
      ),
      amountSol: _modelDouble(nested['amountSol'] ?? nested['bountyAmountSol']),
      payUri: _firstModelString(nested, const [
        'uri',
        'payUri',
        'bountyPayUri',
      ]),
      payee: _firstModelString(nested, const ['payee', 'bountyPayee']),
      payoutSig: _firstModelString(nested, const [
        'payoutSig',
        'bountyPayoutSig',
      ]),
      confirmed: nested['confirmed'] == true,
    );
  }
}

extension _StringDefault on String {
  String ifEmpty(String fallback) => isEmpty ? fallback : this;
}

class Issue {
  Issue({
    required this.number,
    required this.title,
    this.status = 'open',
    this.body = '',
    this.author = '',
    this.labels = const [],
    this.milestone = '',
    this.priority = 0,
    this.assignees = const [],
    this.events = const [],
    this.votes = 0,
    this.bountyUsd = 0,
    this.bountyAddress = '',
    this.bountyStatus = '',
    this.bountyRequiredLamports = 0,
    this.bountyReceivedLamports = 0,
    this.bountyAmountSol = 0,
    this.bountyPayUri = '',
    this.bountyPayee = '',
    this.bountyPayoutSig = '',
  });

  final int number;
  final String title;
  final String status;
  final String body;
  final String author;
  final List<String> labels;
  final String milestone;
  final int priority;
  final List<String> assignees;
  final List<IssueEvent> events;
  final int votes;
  final double bountyUsd;
  final String bountyAddress;
  final String bountyStatus;
  final int bountyRequiredLamports;
  final int bountyReceivedLamports;
  final double bountyAmountSol;
  final String bountyPayUri;
  final String bountyPayee;
  final String bountyPayoutSig;

  bool get isOpen => status != 'closed';
  IssueBounty get bounty => IssueBounty(
    address: bountyAddress,
    status: bountyStatus.isEmpty ? 'open' : bountyStatus,
    amountUsd: bountyUsd,
    requiredLamports: bountyRequiredLamports,
    receivedLamports: bountyReceivedLamports,
    amountSol: bountyAmountSol,
    payUri: bountyPayUri,
    payee: bountyPayee,
    payoutSig: bountyPayoutSig,
  );
  String get bountyStatusLabel => bounty.statusLabel;
  String get bountyProgressLabel => bounty.progressLabel;
  bool get hasBountyFunding =>
      bountyUsd > 0 ||
      bountyAddress.isNotEmpty ||
      bountyRequiredLamports > 0 ||
      bountyReceivedLamports > 0 ||
      bountyPayoutSig.isNotEmpty;

  Issue copyWithBounty(IssueBounty bounty) => Issue(
    number: number,
    title: title,
    status: status,
    body: body,
    author: author,
    labels: labels,
    milestone: milestone,
    priority: priority,
    assignees: assignees,
    events: events,
    votes: votes,
    bountyUsd: bounty.amountUsd > 0 ? bounty.amountUsd : bountyUsd,
    bountyAddress: bounty.address,
    bountyStatus: bounty.statusLabel,
    bountyRequiredLamports: bounty.requiredLamports,
    bountyReceivedLamports: bounty.receivedLamports,
    bountyAmountSol: bounty.amountSol,
    bountyPayUri: bounty.payUri,
    bountyPayee: bounty.payee,
    bountyPayoutSig: bounty.payoutSig,
  );

  factory Issue.fromJson(Map<String, dynamic> j) {
    int asInt(dynamic v) =>
        v is int ? v : (v is num ? v.toInt() : int.tryParse('$v') ?? 0);
    final bounty = IssueBounty.fromJson(
      j['bounty'] is Map
          ? j['bounty']
          : {
              'address': j['bountyAddress'] ?? j['address'],
              'status': j['bountyStatus'],
              'amountUsd': j['bountyUsd'] ?? j['amountUsd'],
              'requiredLamports':
                  j['bountyRequiredLamports'] ?? j['requiredLamports'],
              'receivedLamports':
                  j['bountyReceivedLamports'] ?? j['receivedLamports'],
              'amountSol': j['bountyAmountSol'] ?? j['amountSol'],
              'uri': j['bountyPayUri'] ?? j['payUri'] ?? j['uri'],
              'payee': j['bountyPayee'] ?? j['payee'],
              'payoutSig': j['bountyPayoutSig'] ?? j['payoutSig'],
            },
    );
    return Issue(
      number: asInt(j['number']),
      title: (j['title'] ?? '').toString(),
      status: (j['status'] ?? 'open').toString(),
      body: (j['body'] ?? '').toString(),
      author: (j['author'] ?? '').toString(),
      labels:
          (j['labels'] as List?)?.map((e) => e.toString()).toList() ?? const [],
      milestone: (j['milestone'] ?? '').toString(),
      priority: asInt(j['priority']),
      assignees:
          (j['assignees'] as List?)?.map((e) => e.toString()).toList() ??
          const [],
      events:
          (j['events'] as List?)
              ?.whereType<Map>()
              .map((e) => IssueEvent.fromJson(Map<String, dynamic>.from(e)))
              .toList() ??
          const [],
      votes: asInt(j['votes']),
      bountyUsd: (j['bountyUsd'] is num)
          ? (j['bountyUsd'] as num).toDouble()
          : bounty.amountUsd,
      bountyAddress: bounty.address,
      bountyStatus: bounty.status,
      bountyRequiredLamports: bounty.requiredLamports,
      bountyReceivedLamports: bounty.receivedLamports,
      bountyAmountSol: bounty.amountSol,
      bountyPayUri: bounty.payUri,
      bountyPayee: bounty.payee,
      bountyPayoutSig: bounty.payoutSig,
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
  final String status;
  final String body;
  final String author;
  final String baseBranch;
  final String headBranch;
  final int createdMs;



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
  final String type;
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

class RepoCommitFile {
  RepoCommitFile({required this.path, this.adds = '', this.dels = ''});

  final String path;
  final String adds;
  final String dels;

  factory RepoCommitFile.fromJson(Map<String, dynamic> j) => RepoCommitFile(
    path: (j['path'] ?? j['file'] ?? '').toString(),
    adds: (j['adds'] ?? j['additions'] ?? '').toString(),
    dels: (j['dels'] ?? j['deletions'] ?? '').toString(),
  );
}

class RepoCommitDetail {
  RepoCommitDetail({
    required this.commit,
    this.files = const [],
    this.diff = '',
    this.truncated = false,
    this.source = '',
  });

  final Map<String, dynamic> commit;
  final List<RepoCommitFile> files;
  final String diff;
  final bool truncated;
  final String source;

  factory RepoCommitDetail.fromJson(
    dynamic data, {
    Map<String, dynamic> fallbackCommit = const {},
  }) {
    if (data is! Map<String, dynamic>) {
      return RepoCommitDetail(commit: fallbackCommit);
    }
    final commit = data['commit'] is Map
        ? Map<String, dynamic>.from(data['commit'] as Map)
        : Map<String, dynamic>.from(fallbackCommit);
    final rawFiles = data['files'] is List ? data['files'] as List : const [];
    return RepoCommitDetail(
      commit: commit,
      files: rawFiles
          .whereType<Map>()
          .map((f) => RepoCommitFile.fromJson(Map<String, dynamic>.from(f)))
          .where((f) => f.path.isNotEmpty)
          .toList(),
      diff: (data['diff'] ?? '').toString(),
      truncated: data['truncated'] == true,
      source: (data['source'] ?? data['servedBy'] ?? '').toString(),
    );
  }
}

class RepoBranch {
  RepoBranch({
    required this.name,
    this.sha = '',
    this.isDefault = false,
    this.worktreePath = '',
  });

  final String name;
  final String sha;
  final bool isDefault;
  final String worktreePath;

  bool get hasWorktree => worktreePath.isNotEmpty;

  factory RepoBranch.fromJson(Map<String, dynamic> j) => RepoBranch(
    name: (j['name'] ?? j['branch'] ?? '').toString(),
    sha: (j['sha'] ?? j['hash'] ?? j['commit'] ?? '').toString(),
    isDefault: j['default'] == true || j['isDefault'] == true,
    worktreePath: (j['worktree'] ?? j['worktreePath'] ?? '').toString(),
  );
}

class RepoCodeSearchMatch {
  RepoCodeSearchMatch({required this.path, this.line = 0, this.text = ''});

  final String path;
  final int line;
  final String text;

  factory RepoCodeSearchMatch.fromJson(Map<String, dynamic> j) {
    int asInt(dynamic v) =>
        v is int ? v : (v is num ? v.toInt() : int.tryParse('$v') ?? 0);
    return RepoCodeSearchMatch(
      path: (j['path'] ?? j['file'] ?? '').toString(),
      line: asInt(j['line'] ?? j['lineNumber']),
      text: (j['text'] ?? j['snippet'] ?? j['match'] ?? '').toString(),
    );
  }
}

class RepoSearchResults {
  RepoSearchResults({this.code = const []});

  final List<RepoCodeSearchMatch> code;

  factory RepoSearchResults.fromJson(dynamic data) {
    if (data is! Map<String, dynamic>) return RepoSearchResults();
    final rawCode = data['code'] is List ? data['code'] as List : const [];
    return RepoSearchResults(
      code: rawCode
          .whereType<Map<String, dynamic>>()
          .map(RepoCodeSearchMatch.fromJson)
          .where((m) => m.path.isNotEmpty)
          .toList(),
    );
  }
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
    this.category = 'general',
    this.updatedMs = 0,
    this.events = const [],
  });

  final int number;
  final String title;
  final String body;
  final String author;
  final String category;
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

class PullEvent {
  const PullEvent({
    required this.type,
    this.body = '',
    this.author = '',
    this.authorName = '',
    this.state = '',
    this.ts = 0,
  });

  final String type;
  final String body;
  final String author;
  final String authorName;
  final String state;
  final int ts;

  String get displayAuthor => authorName.isNotEmpty ? authorName : author;

  String get displayTitle {
    if (type == 'review') {
      switch (state) {
        case 'approve':
        case 'approved':
          return 'Approved';
        case 'request-changes':
        case 'changes-requested':
        case 'changes_requested':
        case 'requested_changes':
          return 'Requested changes';
        case 'comment':
          return 'Review comment';
      }
      return 'Review';
    }
    if (type == 'line-comment') return 'Line comment';
    if (type == 'thread-comment') return 'Thread comment';
    if (type == 'thread-reply') return 'Thread reply';
    if (type == 'thread-state') return 'Thread state';
    if (type == 'suggestion-state') return 'Suggestion state';
    return 'Comment';
  }
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
    this.events = const [],
  });

  final int number;
  final String title;
  final String status;
  final String body;
  final String base;
  final String head;
  final String patch;
  final bool signed;
  final List<PullEvent> events;
}

class RepoReleaseAsset {
  const RepoReleaseAsset({
    required this.name,
    required this.sha256,
    this.size = 0,
    this.os = '',
    this.arch = '',
    this.downloads = 0,
  });

  final String name;
  final String sha256;
  final int size;
  final String os;
  final String arch;
  final int downloads;

  String get shortSha => sha256.length > 12 ? sha256.substring(0, 12) : sha256;

  String get platformLabel {
    final parts = [os, arch].where((p) => p.trim().isNotEmpty).toList();
    return parts.isEmpty ? 'artifact' : parts.join(' / ');
  }

  String get sizeLabel {
    if (size <= 0) return 'unknown size';
    const units = ['B', 'KB', 'MB', 'GB'];
    var value = size.toDouble();
    var unit = 0;
    while (value >= 1024 && unit < units.length - 1) {
      value /= 1024;
      unit += 1;
    }
    if (unit == 0) return '${value.toStringAsFixed(0)} ${units[unit]}';
    return '${value.toStringAsFixed(1)} ${units[unit]}';
  }

  String get downloadLabel =>
      '$downloads ${downloads == 1 ? 'download' : 'downloads'}';

  factory RepoReleaseAsset.fromJson(
    Map<String, dynamic> json, {
    int downloads = 0,
  }) {
    int asInt(dynamic value) => value is int
        ? value
        : value is num
        ? value.toInt()
        : int.tryParse('$value') ?? 0;
    return RepoReleaseAsset(
      name: (json['name'] ?? json['filename'] ?? '').toString(),
      sha256: (json['sha256'] ?? json['blob_sha256'] ?? json['hash'] ?? '')
          .toString(),
      size: asInt(json['size'] ?? json['bytes']),
      os: (json['os'] ?? '').toString(),
      arch: (json['arch'] ?? '').toString(),
      downloads: downloads,
    );
  }
}

class RepoRelease {
  const RepoRelease({
    required this.tag,
    this.channel = '',
    this.tagCommit = '',
    this.createdAtMs = 0,
    this.assets = const [],
  });

  final String tag;
  final String channel;
  final String tagCommit;
  final int createdAtMs;
  final List<RepoReleaseAsset> assets;

  String get shortCommit =>
      tagCommit.length > 8 ? tagCommit.substring(0, 8) : tagCommit;

  String get createdDate {
    if (createdAtMs <= 0) return '';
    final d = DateTime.fromMillisecondsSinceEpoch(createdAtMs);
    final month = d.month.toString().padLeft(2, '0');
    final day = d.day.toString().padLeft(2, '0');
    return '${d.year}-$month-$day';
  }

  String get assetSummary => assets.isEmpty
      ? 'No published artifacts yet'
      : '${assets.length} ${assets.length == 1 ? 'artifact' : 'artifacts'}';

  factory RepoRelease.fromJson(
    Map<String, dynamic> json, {
    Map<String, int> downloads = const {},
  }) {
    int asInt(dynamic value) => value is int
        ? value
        : value is num
        ? value.toInt()
        : int.tryParse('$value') ?? 0;
    final rawAssets = json['assets'] is List
        ? json['assets'] as List
        : const [];
    final assets = rawAssets
        .whereType<Map>()
        .map((asset) {
          final map = Map<String, dynamic>.from(asset);
          final sha = (map['sha256'] ?? map['blob_sha256'] ?? map['hash'] ?? '')
              .toString();
          return RepoReleaseAsset.fromJson(map, downloads: downloads[sha] ?? 0);
        })
        .where((asset) => asset.name.isNotEmpty || asset.sha256.isNotEmpty)
        .toList();
    return RepoRelease(
      tag: (json['tag'] ?? json['name'] ?? '').toString(),
      channel: (json['channel'] ?? '').toString(),
      tagCommit:
          (json['tag_commit'] ?? json['tagCommit'] ?? json['commit'] ?? '')
              .toString(),
      createdAtMs: asInt(json['created_at'] ?? json['createdAt'] ?? json['ts']),
      assets: assets,
    );
  }
}

class NetworkStats {
  const NetworkStats({
    this.nodesOnline = 0,
    this.hostsOnline = 0,
    this.repos = 0,
    this.payoutNodes = const [],
  });

  final int nodesOnline;
  final int hostsOnline;
  final int repos;
  final List<PayoutNode> payoutNodes;

  factory NetworkStats.fromJson(Map<String, dynamic> j) {
    int asInt(dynamic v) =>
        v is int ? v : (v is num ? v.toInt() : int.tryParse('$v') ?? 0);
    final payoutRaw = j['payoutNodes'] is List
        ? j['payoutNodes'] as List
        : j['payout_nodes'] is List
        ? j['payout_nodes'] as List
        : const [];
    return NetworkStats(
      nodesOnline: asInt(j['nodesOnline'] ?? j['clients'] ?? 0),
      hostsOnline: asInt(j['hostsOnline'] ?? j['hosts'] ?? 0),
      repos: asInt(j['repos'] ?? j['repositories'] ?? 0),
      payoutNodes: payoutRaw
          .whereType<Map>()
          .map((item) => PayoutNode.fromJson(Map<String, dynamic>.from(item)))
          .toList(),
    );
  }
}

class PayoutNode {
  const PayoutNode({
    this.name = '',
    this.wallet = '',
    this.balanceLamports = 0,
    this.balanceSol = 0,
    this.online = false,
    this.payoutEligible = false,
    this.eligibilityReason = '',
    this.relay = '',
  });

  final String name;
  final String wallet;
  final int balanceLamports;
  final double balanceSol;
  final bool online;
  final bool payoutEligible;
  final String eligibilityReason;
  final String relay;

  String get shortWallet {
    final clean = wallet.trim();
    if (clean.length <= 16) return clean;
    return '${clean.substring(0, 6)}...${clean.substring(clean.length - 6)}';
  }

  String get eligibilityLabel {
    final reason = eligibilityReason.trim();
    if (reason.isNotEmpty) {
      return reason.replaceAll('_', ' ').replaceAll('-', ' ');
    }
    return payoutEligible ? 'eligible' : 'not eligible';
  }

  String get balanceLabel {
    if (balanceSol > 0) {
      return '${_trimModelDouble(balanceSol)} SOL ($balanceLamports lamports)';
    }
    return '$balanceLamports lamports';
  }

  factory PayoutNode.fromJson(Map<String, dynamic> json) => PayoutNode(
    name: _firstModelString(json, const ['name', 'node', 'id']),
    wallet: _firstModelString(json, const [
      'wallet',
      'walletAddress',
      'payoutWallet',
      'address',
    ]),
    balanceLamports: _modelInt(json['balanceLamports']),
    balanceSol: _modelDouble(json['balanceSol']),
    online: _modelBool(json['online']),
    payoutEligible: _modelBool(json['payoutEligible']),
    eligibilityReason: _firstModelString(json, const [
      'eligibilityReason',
      'reason',
    ]),
    relay: _firstModelString(json, const ['relay']),
  );
}

class NetworkLeaderboards {
  const NetworkLeaderboards({
    this.fundsMainnodes = const [],
    this.fundsContributors = const [],
    this.fundsProjects = const [],
  });

  final List<FundsReceivedEntry> fundsMainnodes;
  final List<FundsReceivedEntry> fundsContributors;
  final List<FundsReceivedEntry> fundsProjects;

  bool get isEmpty =>
      fundsMainnodes.isEmpty &&
      fundsContributors.isEmpty &&
      fundsProjects.isEmpty;

  factory NetworkLeaderboards.fromJson(Map<String, dynamic> json) =>
      NetworkLeaderboards(
        fundsMainnodes: _fundsReceivedList(json['fundsMainnodes']),
        fundsContributors: _fundsReceivedList(json['fundsContributors']),
        fundsProjects: _fundsReceivedList(json['fundsProjects']),
      );

  static List<FundsReceivedEntry> _fundsReceivedList(dynamic raw) {
    final items = raw is List ? raw : const [];
    return items
        .whereType<Map>()
        .map(
          (item) =>
              FundsReceivedEntry.fromJson(Map<String, dynamic>.from(item)),
        )
        .toList();
  }
}

class FundsReceivedEntry {
  const FundsReceivedEntry({this.name = '', this.lamports = 0, this.sol = 0});

  final String name;
  final int lamports;
  final double sol;

  String get amountLabel {
    if (sol > 0) return '${_trimModelDouble(sol)} SOL ($lamports lamports)';
    return '$lamports lamports';
  }

  factory FundsReceivedEntry.fromJson(Map<String, dynamic> json) =>
      FundsReceivedEntry(
        name: _firstModelString(json, const [
          'name',
          'node',
          'account',
          'project',
          'repo',
        ]),
        lamports: _modelInt(json['lamports']),
        sol: _modelDouble(json['sol']),
      );
}



const orgRoles = ['owner', 'admin', 'member'];



const orgTeamPermissions = ['read', 'write', 'maintain', 'admin'];



class OrgSummary {
  const OrgSummary({required this.name, this.role = 'member'});

  final String name;
  final String role;

  bool get canManage => role == 'owner' || role == 'admin';

  factory OrgSummary.fromJson(Map<String, dynamic> json) => OrgSummary(
    name: _firstModelString(json, const ['name', 'org']),
    role: _modelString(json['role']).trim().toLowerCase(),
  );
}



class OrgProfile {
  const OrgProfile({
    required this.name,
    this.displayName = '',
    this.description = '',
    this.createdMs = 0,
    this.members = 0,
    this.teams = 0,
    this.repos = const [],
    this.viewerRole = '',
  });

  final String name;
  final String displayName;
  final String description;
  final int createdMs;
  final int members;
  final int teams;
  final List<OrgRepo> repos;
  final String viewerRole;

  String get title => displayName.isNotEmpty ? displayName : name;
  bool get canManage => viewerRole == 'owner' || viewerRole == 'admin';
  bool get isOwner => viewerRole == 'owner';

  factory OrgProfile.fromJson(Map<String, dynamic> json) => OrgProfile(
    name: _firstModelString(json, const ['org', 'name']),
    displayName: _modelString(json['displayName']),
    description: _modelString(json['description']),
    createdMs: _modelInt(json['createdAt']),
    members: _modelInt(json['members']),
    teams: _modelInt(json['teams']),
    repos: (json['repos'] is List ? json['repos'] as List : const [])
        .whereType<Map>()
        .map((r) => OrgRepo.fromJson(Map<String, dynamic>.from(r)))
        .where((r) => r.repo.isNotEmpty)
        .toList(),
    viewerRole: _modelString(json['viewerRole']).trim().toLowerCase(),
  );
}


class OrgMember {
  const OrgMember({required this.name, this.role = 'member', this.sinceMs = 0});

  final String name;
  final String role;
  final int sinceMs;

  factory OrgMember.fromJson(Map<String, dynamic> json) => OrgMember(
    name: _modelString(json['name']).trim().toLowerCase(),
    role: _modelString(json['role']).trim().toLowerCase(),
    sinceMs: _modelInt(json['since']),
  );
}


class OrgTeam {
  const OrgTeam({
    required this.team,
    this.permission = 'read',
    this.members = 0,
  });

  final String team;
  final String permission;
  final int members;

  factory OrgTeam.fromJson(Map<String, dynamic> json) => OrgTeam(
    team: _modelString(json['team']).trim().toLowerCase(),
    permission: _modelString(json['permission']).trim().toLowerCase(),
    members: _modelInt(json['members']),
  );
}



class OrgRepo {
  const OrgRepo({required this.repo, this.node = ''});

  final String repo;
  final String node;

  factory OrgRepo.fromJson(Map<String, dynamic> json) => OrgRepo(
    repo: _modelString(json['repo']).trim().toLowerCase(),
    node: _firstModelString(json, const ['node', 'node_owner']),
  );
}



class OrgApiException implements Exception {
  const OrgApiException(this.code, this.message);

  final String code;
  final String message;

  @override
  String toString() => message;
}



String orgErrorMessage(String code) {
  switch (code) {
    case 'invalid_org_name':
      return 'Invalid name. Use lowercase letters, numbers and dashes.';
    case 'invalid_team_name':
      return 'Invalid team name. Use lowercase letters, numbers and dashes.';
    case 'org_name_taken':
      return 'That name is already taken.';
    case 'too_many_orgs':
      return 'You have reached the organization limit for this account.';
    case 'too_many_members':
      return 'This organization has reached its member limit.';
    case 'too_many_teams':
      return 'This organization has reached its team limit.';
    case 'too_many_repos':
      return 'This organization has reached its linked-repo limit.';
    case 'invalid_session':
      return 'Sign in to manage organizations.';
    case 'forbidden':
      return 'You do not have permission to do that.';
    case 'not_found':
      return 'Not found.';
    case 'last_owner':
      return 'An organization must keep at least one owner.';
    case 'unknown_account':
      return 'No account exists with that name.';
    case 'not_a_member':
      return 'That account is not a member of this organization.';
    case 'bad_role':
      return 'Invalid role.';
    case 'bad_permission':
      return 'Invalid permission.';
    case 'member_required':
      return 'Enter a member account name.';
    case 'repo_required':
      return 'Enter a repository name.';
    case 'not_your_node':
      return 'You can only link repositories from your own node.';
    case 'unknown_repo':
      return 'That repository is not published on your node.';
    case '':
      return 'Request failed.';
    default:
      return 'Request failed ($code).';
  }
}
