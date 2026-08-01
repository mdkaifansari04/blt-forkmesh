import 'package:flutter_test/flutter_test.dart';
import 'package:forkmesh/models/models.dart';
import 'package:forkmesh/services/notification_deep_link.dart';





void main() {
  test('Repository.fromJson reads worker catalog fields with defaults', () {
    final repo = Repository.fromJson({
      'owner': 'alice',
      'name': 'project',
      'stars': '12',
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
        'not-an-event',
      ],
      'labels': ['bug', 42],
      'milestone': 'v2 mobile',
      'priority': '4',
      'assignees': ['mona', 'kai'],
    });

    expect(issue.number, 7);
    expect(issue.isOpen, isTrue);
    expect(issue.events, hasLength(1));
    expect(issue.events.single.body, 'me too');
    expect(issue.labels, ['bug', '42']);
    expect(issue.milestone, 'v2 mobile');
    expect(issue.priority, 4);
    expect(issue.assignees, ['mona', 'kai']);
  });

  test('RepoTree.fromJson accepts list and wrapped-map payloads', () {


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

    expect(wrapped.entries.map((e) => e.name).toList(), [
      'alpha',
      'beta.md',
      'zeta.txt',
    ]);

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

  test('NetworkStats.fromJson reads payout readiness nodes', () {
    final stats = NetworkStats.fromJson({
      'nodesOnline': 2,
      'payoutNodes': [
        {
          'name': 'mainnode-a',
          'wallet': 'Wallet11111111111111111111111111111111',
          'balanceLamports': '1250000000',
          'balanceSol': '1.25',
          'online': true,
          'payoutEligible': true,
          'eligibilityReason': 'eligible',
          'relay': 'us-east',
        },
        {
          'name': 'mainnode-b',
          'online': false,
          'eligibilityReason': 'missing wallet',
        },
      ],
    });

    expect(stats.payoutNodes, hasLength(2));
    expect(stats.payoutNodes.first.name, 'mainnode-a');
    expect(
      stats.payoutNodes.first.wallet,
      'Wallet11111111111111111111111111111111',
    );
    expect(stats.payoutNodes.first.shortWallet, 'Wallet...111111');
    expect(stats.payoutNodes.first.balanceLamports, 1250000000);
    expect(stats.payoutNodes.first.balanceSol, 1.25);
    expect(stats.payoutNodes.first.online, isTrue);
    expect(stats.payoutNodes.first.payoutEligible, isTrue);
    expect(stats.payoutNodes.first.eligibilityLabel, 'eligible');
    expect(
      stats.payoutNodes.first.balanceLabel,
      '1.25 SOL (1250000000 lamports)',
    );
    expect(stats.payoutNodes.first.relay, 'us-east');
    expect(stats.payoutNodes.last.name, 'mainnode-b');
    expect(stats.payoutNodes.last.wallet, isEmpty);
    expect(stats.payoutNodes.last.balanceLamports, 0);
    expect(stats.payoutNodes.last.balanceSol, 0);
    expect(stats.payoutNodes.last.payoutEligible, isFalse);
    expect(stats.payoutNodes.last.eligibilityLabel, 'missing wallet');
  });

  test('NetworkLeaderboards.fromJson parses funds received boards', () {
    final boards = NetworkLeaderboards.fromJson({
      'fundsMainnodes': [
        {'name': 'mainnode-a', 'lamports': '1250000000', 'sol': '1.25'},
      ],
      'fundsContributors': [
        {'name': 'alice', 'lamports': 500000000, 'sol': 0.5},
      ],
      'fundsProjects': [
        {'name': 'forkmesh/mobile', 'lamports': '42'},
      ],
    });

    expect(boards.fundsMainnodes.single.name, 'mainnode-a');
    expect(boards.fundsMainnodes.single.lamports, 1250000000);
    expect(boards.fundsMainnodes.single.sol, 1.25);
    expect(
      boards.fundsMainnodes.single.amountLabel,
      '1.25 SOL (1250000000 lamports)',
    );
    expect(
      boards.fundsContributors.single.amountLabel,
      '0.5 SOL (500000000 lamports)',
    );
    expect(boards.fundsProjects.single.amountLabel, '42 lamports');
    expect(boards.isEmpty, isFalse);
  });

  test('ForkNotification.fromJson parses worker notification payloads', () {
    final notification = ForkNotification.fromJson({
      'id': 'dedupe-1',
      'kind': 'pull_submitted',
      'title': 'Pull request submitted for mona/forkmesh',
      'body': 'Mobile review flow is waiting in your desktop inbox.',
      'repo': 'mona/forkmesh',
      'href': '/mona/forkmesh/pulls/7',
      'actor': 'kai',
      'source': 'pull',
      'ts': '1770000000000',
      'readAt': 0,
      'meta': {'number': 7},
    });

    expect(notification.id, 'dedupe-1');
    expect(notification.kind, 'pull_submitted');
    expect(notification.kindLabel, 'Pull request');
    expect(notification.title, 'Pull request submitted for mona/forkmesh');
    expect(notification.repo, 'mona/forkmesh');
    expect(notification.actor, 'kai');
    expect(notification.ts, 1770000000000);
    expect(notification.isUnread, isTrue);
    expect(notification.meta['number'], 7);
  });

  test('NotificationPage.fromJson reads list and unread count', () {
    final page = NotificationPage.fromJson({
      'ok': true,
      'unread': '2',
      'notifications': [
        {'id': 'n1', 'kind': 'mention', 'title': 'Mention', 'ts': 2},
        {'id': 'n2', 'kind': 'host_offline', 'title': 'Host offline'},
      ],
    });

    expect(page.unread, 2);
    expect(page.notifications, hasLength(2));
    expect(page.notifications.first.kindLabel, 'Mention');
    expect(page.notifications.last.kindLabel, 'Host');
  });

  test('NotificationDeepLink parses safe repo context hrefs', () {
    final issue = NotificationDeepLink.parse('/mona/forkmesh/issues/12');
    expect(issue?.repo.fullName, 'mona/forkmesh');
    expect(issue?.initialTab, RepoDetailTab.issues);
    expect(issue?.number, 12);

    final pull = NotificationDeepLink.parse('/mona/forkmesh/pulls/7');
    expect(pull?.initialTab, RepoDetailTab.pulls);
    expect(pull?.number, 7);

    final commit = NotificationDeepLink.parse('/mona/forkmesh/commit/abc123');
    expect(commit?.initialTab, RepoDetailTab.commits);
    expect(commit?.reference, 'abc123');

    final agents = NotificationDeepLink.parse('/mona/forkmesh/agents');
    expect(agents?.initialTab, RepoDetailTab.agents);

    final actions = NotificationDeepLink.parse('/mona/forkmesh/actions');
    expect(actions?.initialTab, RepoDetailTab.actions);

    expect(
      NotificationDeepLink.parse('https://evil.test/mona/forkmesh'),
      isNull,
    );
    expect(NotificationDeepLink.parse('/api/notifications'), isNull);
  });
}
