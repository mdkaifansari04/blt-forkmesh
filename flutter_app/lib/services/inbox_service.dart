import 'dart:convert';

import 'package:crypto/crypto.dart' as crypto;
import 'package:http/http.dart' as http;

import 'identity.dart';
import 'performance_monitor_service.dart';
import 'settings_service.dart';

/// Signed write paths to a repo's relay inbox, mirroring the Qt client's
/// submit*ToInbox functions and the worker's verify_issue_event /
/// verify_pull_event / verify_pull_comment_event.
///
/// The local node (a non-owner) POSTs signed events; the repo owner later drains
/// the inbox, applies, commits, and syncs back. Every canonical string and
/// content layout below must match cloudflare_worker/src/entry.py byte-for-byte
/// or the relay rejects with 401 bad_signature.
class InboxService {
  InboxService(
    this._settings,
    this._identity, {
    PerformanceMonitorService? performanceMonitor,
  }) : _performanceMonitor = performanceMonitor;

  final SettingsService _settings;
  final Identity _identity;
  final PerformanceMonitorService? _performanceMonitor;

  String get _author => _identity.publicKeyB64url;
  int get _now => DateTime.now().millisecondsSinceEpoch;

  Uri _repoEndpoint(String owner, String name, String leaf) {
    final ws = Uri.parse(_settings.serverUrl);
    final scheme = ws.scheme == 'ws' ? 'http' : 'https';
    return Uri(
      scheme: scheme,
      host: ws.host,
      port: ws.hasPort ? ws.port : null,
      path: '/api/repo/$owner/$name/$leaf',
    );
  }

  String _sha256Hex(String content) =>
      crypto.sha256.convert(utf8.encode(content)).toString();

  Future<String> _sign(String canonical) =>
      _identity.sign(utf8.encode(canonical));

  // ---- issues -------------------------------------------------------------

  // issue_event_content() — NUL-joined fields per event type. Public so the
  // contract tests can pin it to the Qt/worker cross-language vectors.
  String issueContent(String type, Map<String, dynamic> ev) {
    final attachments = (ev['attachments'] as List?)?.join(',') ?? '';
    switch (type) {
      case 'open':
        return [ev['title'] ?? '', ev['body'] ?? '', attachments].join('\x00');
      case 'comment':
      case 'edit':
        return [ev['body'] ?? '', attachments].join('\x00');
      case 'title':
        return (ev['title'] ?? '').toString();
      case 'status':
        return (ev['status'] ?? '').toString();
      case 'labels':
        return (ev['labels'] as List?)?.join(',') ?? '';
      case 'milestone':
        return (ev['milestone'] ?? '').toString();
      case 'priority':
        return '${(ev['priority'] as num?)?.toInt() ?? 0}';
      case 'progress':
        return '${(ev['progress'] as num?)?.toInt() ?? 0}';
      case 'assignees':
        return (ev['assignees'] as List?)?.join(',') ?? '';
      case 'delete':
        return (ev['target'] ?? '').toString();
      case 'vote':
        return '';
      default:
        return '';
    }
  }

  Future<Map<String, dynamic>> _signedIssueEvent(
    int number,
    String type,
    Map<String, dynamic> fields,
  ) async {
    final ts = _now;
    final ev = <String, dynamic>{'type': type, ...fields};
    final contentHash = _sha256Hex(issueContent(type, ev));
    final canonical =
        'forkmesh-issue-event-v1\n$type\n$number\n$_author\n$ts\n$contentHash';
    final sig = await _sign(canonical);
    ev['author'] = _author;
    ev['ts'] = ts;
    ev['sig'] = sig;
    return ev;
  }

  Future<void> submitNewIssue(
    String owner,
    String name, {
    required String title,
    required String body,
    List<String> labels = const [],
    String milestone = '',
    int priority = 0,
    List<String> assignees = const [],
  }) async {
    final event = await _signedIssueEvent(0, 'open', {
      'title': title,
      'body': body,
      'attachments': const <String>[],
    });
    await _post(_repoEndpoint(owner, name, 'issues'), {
      'number': 0,
      'titleIfNew': title,
      'event': event,
      'meta': {
        'labels': labels,
        'milestone': milestone,
        'priority': priority,
        'assignees': assignees,
      },
    });
  }

  Future<void> commentOnIssue(
    String owner,
    String name,
    int number,
    String body,
  ) async {
    final event = await _signedIssueEvent(number, 'comment', {
      'body': body,
      'attachments': const <String>[],
    });
    await _post(_repoEndpoint(owner, name, 'issues'), {
      'number': number,
      'event': event,
    });
  }

  Future<void> voteOnIssue(String owner, String name, int number) async {
    final event = await _signedIssueEvent(number, 'vote', const {});
    await _post(_repoEndpoint(owner, name, 'issues'), {
      'number': number,
      'event': event,
    });
  }

  Future<void> setIssueStatus(
    String owner,
    String name,
    int number,
    String status,
  ) async {
    final event = await _signedIssueEvent(number, 'status', {'status': status});
    await _post(_repoEndpoint(owner, name, 'issues'), {
      'number': number,
      'event': event,
    });
  }

  // ---- pull requests ------------------------------------------------------

  Future<void> submitNewPull(
    String owner,
    String name, {
    required String title,
    required String base,
    required String head,
    required String patch,
  }) async {
    final ts = _now;
    final content = [title, base, head, patch].join('\x00');
    final contentHash = _sha256Hex(content);
    final canonical = 'forkmesh-pull-event-v1\n$_author\n$ts\n$contentHash';
    final sig = await _sign(canonical);
    await _post(_repoEndpoint(owner, name, 'pulls'), {
      'pull': {
        'title': title,
        'base': base,
        'head': head,
        'patch': patch,
        'author': _author,
        'ts': ts,
        'sig': sig,
      },
    });
  }

  // pull_comment_content() — NUL-joined per type. Public so the contract
  // tests can pin it to the Qt/worker cross-language vectors.
  String pullCommentContent(String type, Map<String, dynamic> ev) {
    switch (type) {
      case 'comment':
        return (ev['body'] ?? '').toString();
      case 'review':
        return [ev['state'] ?? '', ev['body'] ?? ''].join('\x00');
      case 'line-comment':
        final line = '${(ev['line'] as num?)?.toInt() ?? 0}';
        return [
          ev['path'] ?? '',
          ev['side'] ?? '',
          line,
          ev['body'] ?? '',
        ].join('\x00');
      case 'thread-comment':
        final lineStart = '${(ev['lineStart'] as num?)?.toInt() ?? 0}';
        final lineEnd = '${(ev['lineEnd'] as num?)?.toInt() ?? 0}';
        return [
          ev['threadId'] ?? '',
          ev['path'] ?? '',
          ev['side'] ?? '',
          lineStart,
          lineEnd,
          ev['body'] ?? '',
          ev['suggestionPatch'] ?? '',
        ].join('\x00');
      case 'thread-reply':
        return [
          ev['threadId'] ?? '',
          ev['parentId'] ?? '',
          ev['body'] ?? '',
        ].join('\x00');
      case 'thread-state':
        return [
          ev['threadId'] ?? '',
          ev['state'] ?? '',
          ev['body'] ?? '',
        ].join('\x00');
      case 'suggestion-state':
        return [
          ev['threadId'] ?? '',
          ev['state'] ?? '',
          ev['appliedCommit'] ?? '',
          ev['body'] ?? '',
        ].join('\x00');
      default:
        return '';
    }
  }

  Future<Map<String, dynamic>> _signedPullComment(
    int number,
    String type,
    Map<String, dynamic> fields,
  ) async {
    final ts = _now;
    final ev = <String, dynamic>{'type': type, ...fields};
    final contentHash = _sha256Hex(pullCommentContent(type, ev));
    final canonical =
        'forkmesh-pull-comment-v1\n$type\n$number\n$_author\n$ts\n$contentHash';
    final sig = await _sign(canonical);
    ev['author'] = _author;
    ev['ts'] = ts;
    ev['sig'] = sig;
    return ev;
  }

  Future<void> commentOnPull(
    String owner,
    String name,
    int number,
    String body,
  ) async {
    final event = await _signedPullComment(number, 'comment', {'body': body});
    await _post(_repoEndpoint(owner, name, 'pulls'), {
      'number': number,
      'event': event,
    });
  }

  /// [state] is "approve" | "request-changes" | "comment".
  Future<void> reviewPull(
    String owner,
    String name,
    int number,
    String state,
    String body,
  ) async {
    final event = await _signedPullComment(number, 'review', {
      'state': state,
      'body': body,
    });
    await _post(_repoEndpoint(owner, name, 'pulls'), {
      'number': number,
      'event': event,
    });
  }

  // ---- commit comments -----------------------------------------------------

  Future<void> commentOnCommit(
    String owner,
    String name,
    String sha,
    String body,
  ) async {
    final ts = _now;
    final contentHash = _sha256Hex(body);
    final canonical =
        'forkmesh-commit-comment-v1\n$sha\n$_author\n$ts\n$contentHash';
    final sig = await _sign(canonical);
    await _post(_repoEndpoint(owner, name, 'commits'), {
      'sha': sha,
      'comment': {'body': body, 'author': _author, 'ts': ts, 'sig': sig},
    });
  }

  // ---- discussions ---------------------------------------------------------

  String _discussionContent(String type, Map<String, dynamic> ev) {
    switch (type) {
      case 'open':
        return [
          ev['title'] ?? '',
          ev['body'] ?? '',
          ev['category'] ?? '',
        ].join('\x00');
      case 'comment':
        return (ev['body'] ?? '').toString();
      default:
        return '';
    }
  }

  Future<Map<String, dynamic>> _signedDiscussionEvent(
    int number,
    String type,
    Map<String, dynamic> fields,
  ) async {
    final ts = _now;
    final ev = <String, dynamic>{'type': type, ...fields};
    final contentHash = _sha256Hex(_discussionContent(type, ev));
    final canonical =
        'forkmesh-discussion-event-v1\n$type\n$number\n$_author\n$ts\n$contentHash';
    final sig = await _sign(canonical);
    ev['author'] = _author;
    ev['ts'] = ts;
    ev['sig'] = sig;
    return ev;
  }

  Future<void> submitNewDiscussion(
    String owner,
    String name, {
    required String title,
    required String body,
    String category = 'general',
  }) async {
    final event = await _signedDiscussionEvent(0, 'open', {
      'title': title,
      'body': body,
      'category': category,
    });
    await _post(_repoEndpoint(owner, name, 'discussions'), {
      'number': 0,
      'titleIfNew': title,
      'event': event,
    });
  }

  Future<void> commentOnDiscussion(
    String owner,
    String name,
    int number,
    String body,
  ) async {
    final event = await _signedDiscussionEvent(number, 'comment', {
      'body': body,
    });
    await _post(_repoEndpoint(owner, name, 'discussions'), {
      'number': number,
      'event': event,
    });
  }

  // ---- transport ----------------------------------------------------------

  Future<void> _post(Uri uri, Map<String, dynamic> body) async {
    Future<void> send() async {
      final resp = await http
          .post(
            uri,
            headers: {'Content-Type': 'application/json'},
            body: jsonEncode(body),
          )
          .timeout(const Duration(seconds: 20));
      if (resp.statusCode == 200 || resp.statusCode == 201) return;
      String detail = 'HTTP ${resp.statusCode}';
      try {
        final j = jsonDecode(resp.body);
        if (j is Map && j['error'] != null) detail = j['error'].toString();
      } catch (_) {}
      throw Exception(detail);
    }

    final monitor = _performanceMonitor;
    if (monitor == null) return send();
    return monitor.track(
      'inbox.POST ${uri.path}',
      send,
      details: {'path': uri.path, 'host': uri.host},
    );
  }
}
