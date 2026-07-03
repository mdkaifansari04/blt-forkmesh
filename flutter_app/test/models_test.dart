import 'package:flutter_test/flutter_test.dart';
import 'package:forkmesh/models/models.dart';

// Parsing contracts for the repo-browsing models added by pulls #4/#5. The
// mobile app reads the same Worker/desktop-host JSON as the website, and the
// endpoints answer in a few historical shapes — these tests pin the tolerant
// parsing (key aliases, string numbers, nesting) the screens rely on.
void main() {
  test('Repository.fromJson reads worker catalog fields with defaults', () {
    final repo = Repository.fromJson({
      'owner': 'alice',
      'name': 'project',
      'stars': '12', // string numbers arrive from D1-backed JSON
      'mirrors': 3,
      'liveHost': true,
      'updatedAt': 1700000000000,
    });

    expect(repo.fullName, 'alice/project');
    expect(repo.stars, 12);
    expect(repo.mirrors, 3);
    expect(repo.defaultBranch, 'main');
    expect(repo.isPrivate, isFalse);
    expect(repo.liveHost, isTrue);
    expect(repo.updatedMs, 1700000000000);
  });

  test('Issue.fromJson parses nested events and tolerates missing fields', () {
    final issue = Issue.fromJson({
      'number': '7',
      'title': 'Crash on open',
      'events': [
        {'type': 'comment', 'body': 'me too'},
        'not-an-event', // stray shapes are skipped, not fatal
      ],
      'labels': ['bug', 42],
    });

    expect(issue.number, 7);
    expect(issue.isOpen, isTrue);
    expect(issue.events, hasLength(1));
    expect(issue.events.single.body, 'me too');
    expect(issue.labels, ['bug', '42']);
  });

  test('RepoTree.fromJson accepts list and wrapped-map payloads', () {
    // Desktop hosts answer a bare entry list; the worker wraps it and names
    // the serving mirror.
    final bare = RepoTree.fromJson([
      {'name': 'src', 'type': 'tree'},
      {'name': 'README.md', 'type': 'blob', 'size': 120},
    ]);
    expect(bare.entries, hasLength(2));

    final wrapped = RepoTree.fromJson({
      'entries': [
        {'name': 'zeta.txt', 'type': 'blob'},
        {'name': 'alpha', 'type': 'tree'},
        {'name': 'beta.md', 'type': 'blob'},
      ],
      'servedBy': 'mirror-node',
      'path': 'docs',
    });
    expect(wrapped.source, 'mirror-node');
    expect(wrapped.path, 'docs');
    // Directories sort first, then names case-insensitively.
    expect(wrapped.entries.map((e) => e.name).toList(), [
      'alpha',
      'beta.md',
      'zeta.txt',
    ]);
    // Bare child names are qualified with the requested path.
    expect(wrapped.entries.last.path, 'docs/zeta.txt');
  });

  test('RepoBlob.fromJson accepts raw strings and wrapped maps', () {
    final raw = RepoBlob.fromJson('hello world', path: 'README.md');
    expect(raw.content, 'hello world');
    expect(raw.path, 'README.md');
    expect(raw.size, 11);

    final wrapped = RepoBlob.fromJson({
      'content': 'line1\n',
      'size': '6',
      'servedBy': 'mirror-node',
    }, path: 'a.txt');
    expect(wrapped.content, 'line1\n');
    expect(wrapped.size, 6);
    expect(wrapped.source, 'mirror-node');
    expect(wrapped.path, 'a.txt');
  });

  test('RepoMirror.fromJson maps the mirror-list key aliases', () {
    final mirror = RepoMirror.fromJson({
      'node': 'backup-node',
      'repo': 'project',
      'status': 'online',
      'lastSeen': '1700000000000',
    });

    expect(mirror.label, 'backup-node/project');
    expect(mirror.online, isTrue);
    expect(mirror.lastSeenMs, 1700000000000);

    final offline = RepoMirror.fromJson({'name': 'solo-node'});
    expect(offline.online, isFalse);
    expect(offline.label, 'solo-node/solo-node');
  });

  test('NetworkStats.fromJson reads both stats endpoint shapes', () {
    final modern = NetworkStats.fromJson({
      'nodesOnline': 4,
      'hostsOnline': 2,
      'repos': 9,
    });
    expect(modern.nodesOnline, 4);
    expect(modern.hostsOnline, 2);
    expect(modern.repos, 9);

    final legacy = NetworkStats.fromJson({
      'clients': '3',
      'hosts': 1,
      'repositories': 5,
    });
    expect(legacy.nodesOnline, 3);
    expect(legacy.hostsOnline, 1);
    expect(legacy.repos, 5);
  });
}
