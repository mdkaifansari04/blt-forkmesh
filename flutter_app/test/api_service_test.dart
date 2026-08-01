import 'dart:convert';
import 'dart:io';

import 'package:flutter_test/flutter_test.dart';
import 'package:forkmesh/models/models.dart';
import 'package:forkmesh/services/api_service.dart';
import 'package:forkmesh/services/settings_service.dart';
import 'package:shared_preferences/shared_preferences.dart';

void main() {
  test('BountyWallet.fromJson parses wallet fields and builds labels/links', () {
    final wallet = BountyWallet.fromJson({
      'address': 'OwnerBountyWallet111111111111111111111111111',
      'balanceLamports': '1234567890',
      'balanceSol': '1.23456789',
      'uri': 'solana:OwnerBountyWallet111111111111111111111111111',
    });

    expect(wallet.hasWallet, isTrue);
    expect(wallet.address, 'OwnerBountyWallet111111111111111111111111111');
    expect(wallet.balanceLamports, 1234567890);
    expect(wallet.balanceSol, 1.23456789);
    expect(
      wallet.payUri,
      'solana:OwnerBountyWallet111111111111111111111111111',
    );
    expect(wallet.balanceLabel, '1.23456789 SOL (1234567890 lamports)');
    expect(wallet.shortAddress, 'OwnerB...111111');
    expect(
      wallet.explorerAddressUrl,
      'https://explorer.solana.com/address/OwnerBountyWallet111111111111111111111111111',
    );
    expect(
      solanaExplorerSignatureUrl('payout-signature'),
      'https://explorer.solana.com/tx/payout-signature',
    );
  });

  test('Issue.fromJson parses public bounty funding fields', () {
    final issue = Issue.fromJson({
      'number': 8,
      'title': 'Pay a maintainer',
      'bountyUsd': 75,
      'bountyAddress': 'escrow111',
      'bountyStatus': 'funded',
      'bountyRequiredLamports': 9000,
      'bountyReceivedLamports': 7000,
      'bountyAmountSol': 0.000009,
      'bountyPayUri': 'solana:escrow111?amount=0.000009',
      'bountyPayee': 'maintainer-node',
      'bountyPayoutSig': 'payout-sig',
    });

    expect(issue.bountyUsd, 75);
    expect(issue.bountyAddress, 'escrow111');
    expect(issue.bountyStatus, 'funded');
    expect(issue.bountyRequiredLamports, 9000);
    expect(issue.bountyReceivedLamports, 7000);
    expect(issue.bountyAmountSol, 0.000009);
    expect(issue.bountyPayUri, 'solana:escrow111?amount=0.000009');
    expect(issue.bountyPayee, 'maintainer-node');
    expect(issue.bountyPayoutSig, 'payout-sig');
    expect(issue.bountyProgressLabel, '7000 / 9000 lamports');
  });

  test('repositories retries after a failed catalog load', () async {
    var requestCount = 0;
    final server = await HttpServer.bind(InternetAddress.loopbackIPv4, 0);
    final subscription = server.listen((request) async {
      requestCount += 1;
      expect(request.uri.path, '/api/repositories');

      if (requestCount <= 2) {
        request.response.statusCode = HttpStatus.internalServerError;
        request.response.write('temporary failure');
      } else {
        request.response.headers.contentType = ContentType.json;
        request.response.write(
          jsonEncode({
            'ok': true,
            'repositories': [
              {'owner': 'owner', 'name': 'forkmesh'},
            ],
          }),
        );
      }

      await request.response.close();
    });
    addTearDown(() async {
      await subscription.cancel();
      await server.close(force: true);
    });

    SharedPreferences.setMockInitialValues({});
    final settings = await SettingsService.create();
    await settings.setServerUrl(
      'ws://${server.address.host}:${server.port}/ws',
    );
    final api = ApiService(settings);

    await expectLater(api.repositories(), throwsException);
    expect(requestCount, 2);

    final repos = await api.repositories();

    expect(requestCount, 3);
    expect(repos.single.fullName, 'owner/forkmesh');
  });

  test(
    'network stats requests payout readiness and parses payout nodes',
    () async {
      final requests = <Uri>[];
      final server = await HttpServer.bind(InternetAddress.loopbackIPv4, 0);
      final subscription = server.listen((request) async {
        requests.add(request.uri);
        request.response.headers.contentType = ContentType.json;
        if (request.method == 'GET' &&
            request.uri.path == '/api/network/stats') {
          request.response.write(
            jsonEncode({
              'nodesOnline': 2,
              'hostsOnline': 1,
              'repos': 3,
              'payoutNodes': [
                {
                  'name': 'mainnode-a',
                  'wallet': 'Wallet11111111111111111111111111111111',
                  'balanceLamports': '1250000000',
                  'balanceSol': '1.25',
                  'online': true,
                  'payoutEligible': true,
                  'eligibilityReason': 'eligible',
                },
              ],
            }),
          );
        } else {
          request.response.statusCode = HttpStatus.notFound;
        }
        await request.response.close();
      });
      addTearDown(() async {
        await subscription.cancel();
        await server.close(force: true);
      });

      SharedPreferences.setMockInitialValues({});
      final settings = await SettingsService.create();
      await settings.setServerUrl(
        'ws://${server.address.host}:${server.port}/ws',
      );
      final api = ApiService(settings);

      final stats = await api.networkStats();

      expect(requests.single.path, '/api/network/stats');
      expect(requests.single.queryParameters['payouts'], '1');
      expect(stats.nodesOnline, 2);
      expect(stats.payoutNodes.single.name, 'mainnode-a');
      expect(stats.payoutNodes.single.payoutEligible, isTrue);
      expect(
        stats.payoutNodes.single.balanceLabel,
        '1.25 SOL (1250000000 lamports)',
      );
    },
  );

  test('network leaderboards parse funds received boards', () async {
    final requests = <Uri>[];
    final server = await HttpServer.bind(InternetAddress.loopbackIPv4, 0);
    final subscription = server.listen((request) async {
      requests.add(request.uri);
      request.response.headers.contentType = ContentType.json;
      if (request.method == 'GET' &&
          request.uri.path == '/api/network/leaderboards') {
        request.response.write(
          jsonEncode({
            'fundsMainnodes': [
              {'name': 'mainnode-a', 'lamports': '1250000000', 'sol': '1.25'},
            ],
            'fundsContributors': [
              {'name': 'alice', 'lamports': 500000000, 'sol': 0.5},
            ],
            'fundsProjects': [
              {'name': 'forkmesh/mobile', 'lamports': '42'},
            ],
          }),
        );
      } else {
        request.response.statusCode = HttpStatus.notFound;
      }
      await request.response.close();
    });
    addTearDown(() async {
      await subscription.cancel();
      await server.close(force: true);
    });

    SharedPreferences.setMockInitialValues({});
    final settings = await SettingsService.create();
    await settings.setServerUrl(
      'ws://${server.address.host}:${server.port}/ws',
    );
    final api = ApiService(settings);

    final boards = await api.networkLeaderboards();

    expect(requests.single.path, '/api/network/leaderboards');
    expect(boards.fundsMainnodes.single.name, 'mainnode-a');
    expect(
      boards.fundsContributors.single.amountLabel,
      '0.5 SOL (500000000 lamports)',
    );
    expect(boards.fundsProjects.single.amountLabel, '42 lamports');
  });

  test('published markdown issues include bounty and vote count', () async {
    final server = await HttpServer.bind(InternetAddress.loopbackIPv4, 0);
    final subscription = server.listen((request) async {
      request.response.headers.contentType = ContentType.json;
      final path = request.uri.queryParameters['path'] ?? '';
      if (request.uri.path.endsWith('/tree') && path == 'issues') {
        request.response.write(
          jsonEncode({
            'entries': [
              {'name': '12', 'path': 'issues/12', 'type': 'dir'},
            ],
          }),
        );
      } else if (request.uri.path.endsWith('/tree') && path == 'issues/12') {
        request.response.write(
          jsonEncode({
            'entries': [
              {
                'name': 'issue.md',
                'path': 'issues/12/issue.md',
                'type': 'file',
              },
              {
                'name': '001-vote.md',
                'path': 'issues/12/001-vote.md',
                'type': 'file',
              },
              {
                'name': '002-vote.md',
                'path': 'issues/12/002-vote.md',
                'type': 'file',
              },
            ],
          }),
        );
      } else if (request.uri.path.endsWith('/blob') &&
          path == 'issues/12/issue.md') {
        request.response.write(
          jsonEncode({
            'content': '''---
title: Stabilize signed inbox
status: open
authorName: Alice
labels: [mobile, signed]
milestone: v2 mobile
priority: 4
assignees: [mona, kai]
bountyUsd: 150
bountyAddress: escrow-md
bountyStatus: open
bountyRequiredLamports: 4500
bountyReceivedLamports: 1500
bountyAmountSol: 0.0000045
bountyPayUri: solana:escrow-md?amount=0.0000045
bountyPayee: maintainer-node
bountyPayoutSig: payout-md
---
Mobile should make signed collaboration clear.''',
          }),
        );
      } else if (request.uri.path.endsWith('/blob') &&
          (path == 'issues/12/001-vote.md' ||
              path == 'issues/12/002-vote.md')) {
        request.response.write(
          jsonEncode({
            'content': '''---
type: vote
authorName: Voter
ts: 10
---''',
          }),
        );
      } else {
        request.response.statusCode = HttpStatus.notFound;
        request.response.write('not found');
      }
      await request.response.close();
    });
    addTearDown(() async {
      await subscription.cancel();
      await server.close(force: true);
    });

    SharedPreferences.setMockInitialValues({});
    final settings = await SettingsService.create();
    await settings.setServerUrl(
      'ws://${server.address.host}:${server.port}/ws',
    );
    final api = ApiService(settings);

    final issues = await api.publishedIssues('owner', 'repo');

    expect(issues.single.title, 'Stabilize signed inbox');
    expect(issues.single.labels, ['mobile', 'signed']);
    expect(issues.single.milestone, 'v2 mobile');
    expect(issues.single.priority, 4);
    expect(issues.single.assignees, ['mona', 'kai']);
    expect(issues.single.votes, 2);
    expect(issues.single.bountyUsd, 150);
    expect(issues.single.bountyAddress, 'escrow-md');
    expect(issues.single.bountyStatus, 'open');
    expect(issues.single.bountyRequiredLamports, 4500);
    expect(issues.single.bountyReceivedLamports, 1500);
    expect(issues.single.bountyAmountSol, 0.0000045);
    expect(issues.single.bountyPayUri, 'solana:escrow-md?amount=0.0000045');
    expect(issues.single.bountyPayee, 'maintainer-node');
    expect(issues.single.bountyPayoutSig, 'payout-md');
  });

  test(
    'published issues read the split .forkmesh/issues JSON layout',
    () async {



      String record(int number, String status, String title, String body) =>
          jsonEncode({
            'schema': 'forkmesh-issue-v1',
            'number': number,
            'title': title,
            'status': status,
            'authorName': 'Alice',
            'labels': ['mobile'],
            'votes': 1,
            'events': [
              {
                'type': 'open',
                'id': 'open-$number',
                'authorName': 'Alice',
                'ts': 1,
                'title': title,
                'body': body,
                'attachments': [],
              },
            ],
          });
      final server = await HttpServer.bind(InternetAddress.loopbackIPv4, 0);
      final subscription = server.listen((request) async {
        request.response.headers.contentType = ContentType.json;
        final path = request.uri.queryParameters['path'] ?? '';
        Map<String, dynamic>? tree;
        if (request.uri.path.endsWith('/tree') && path == '.forkmesh/issues') {
          tree = {
            'entries': [
              {'name': 'open', 'path': '.forkmesh/issues/open', 'type': 'dir'},
              {
                'name': 'closed',
                'path': '.forkmesh/issues/closed',
                'type': 'dir',
              },
              {'name': '2', 'path': '.forkmesh/issues/2', 'type': 'dir'},
            ],
          };
        } else if (request.uri.path.endsWith('/tree') &&
            path == '.forkmesh/issues/open') {
          tree = {
            'entries': [
              {'name': '3', 'path': '.forkmesh/issues/open/3', 'type': 'dir'},
            ],
          };
        } else if (request.uri.path.endsWith('/tree') &&
            path == '.forkmesh/issues/closed') {
          tree = {
            'entries': [
              {'name': '1', 'path': '.forkmesh/issues/closed/1', 'type': 'dir'},
            ],
          };
        }
        if (tree != null) {
          request.response.write(jsonEncode(tree));
        } else if (request.uri.path.endsWith('/blob') &&
            path == '.forkmesh/issues/open/3/issue-3.json') {
          request.response.write(
            jsonEncode({
              'content': record(
                3,
                'open',
                'Split open',
                'From the open folder',
              ),
            }),
          );
        } else if (request.uri.path.endsWith('/blob') &&
            path == '.forkmesh/issues/closed/1/issue-1.json') {
          request.response.write(
            jsonEncode({
              'content': record(1, 'closed', 'Split closed', 'done'),
            }),
          );
        } else if (request.uri.path.endsWith('/blob') &&
            path == '.forkmesh/issues/2/issue-2.json') {
          request.response.write(
            jsonEncode({
              'content': record(2, 'open', 'Legacy spot', 'old spot'),
            }),
          );
        } else {
          request.response.statusCode = HttpStatus.notFound;
          request.response.write('not found');
        }
        await request.response.close();
      });
      addTearDown(() async {
        await subscription.cancel();
        await server.close(force: true);
      });

      SharedPreferences.setMockInitialValues({});
      final settings = await SettingsService.create();
      await settings.setServerUrl(
        'ws://${server.address.host}:${server.port}/ws',
      );
      final api = ApiService(settings);

      final issues = await api.publishedIssues('owner', 'repo');

      expect(issues.map((issue) => issue.number).toList(), [3, 2, 1]);
      expect(issues.first.title, 'Split open');
      expect(issues.first.body, 'From the open folder');
      expect(issues.first.author, 'Alice');
      expect(issues.first.labels, ['mobile']);
      expect(issues.first.votes, 1);
      expect(issues.first.isOpen, isTrue);
      expect(issues.last.status, 'closed');
    },
  );

  test('issue bounty status posts body and parses funding state', () async {
    final requests = <Map<String, dynamic>>[];
    final server = await HttpServer.bind(InternetAddress.loopbackIPv4, 0);
    final subscription = server.listen((request) async {
      requests.add({
        'method': request.method,
        'path': request.uri.path,
        'body': await utf8.decoder.bind(request).join(),
      });
      request.response.headers.contentType = ContentType.json;
      if (request.method == 'POST' &&
          request.uri.path == '/api/repo/owner/repo/bounty') {
        request.response.write(
          jsonEncode({
            'ok': true,
            'bounty': {
              'address': 'escrow-status',
              'amountUsd': 25,
              'requiredLamports': 250000000,
              'amountSol': 0.25,
              'receivedLamports': 125000000,
              'confirmed': false,
              'status': 'open',
              'payee': 'dev-node',
              'payoutSig': '',
              'uri': 'solana:escrow-status?amount=0.25',
            },
          }),
        );
      } else {
        request.response.statusCode = HttpStatus.notFound;
      }
      await request.response.close();
    });
    addTearDown(() async {
      await subscription.cancel();
      await server.close(force: true);
    });

    SharedPreferences.setMockInitialValues({});
    final settings = await SettingsService.create();
    await settings.setServerUrl(
      'ws://${server.address.host}:${server.port}/ws',
    );
    final api = ApiService(settings);

    final bounty = await api.issueBountyStatus('owner', 'repo', 12);

    expect(jsonDecode(requests.single['body'] as String), {
      'action': 'status',
      'number': 12,
    });
    expect(bounty.address, 'escrow-status');
    expect(bounty.amountUsd, 25);
    expect(bounty.requiredLamports, 250000000);
    expect(bounty.receivedLamports, 125000000);
    expect(bounty.amountSol, 0.25);
    expect(bounty.status, 'open');
    expect(bounty.payee, 'dev-node');
    expect(bounty.payUri, 'solana:escrow-status?amount=0.25');
  });

  test(
    'custodial bounty creation and wallet preparation fail locally',
    () async {
      SharedPreferences.setMockInitialValues({});
      final settings = await SettingsService.create();
      final api = ApiService(settings);

      await expectLater(
        api.createIssueBounty(
          'owner',
          'repo',
          number: 12,
          amountUsd: 150,
          payee: 'owner',
          ts: '1770000000000',
          sig: 'sig-create',
        ),
        throwsA(isA<UnsupportedError>()),
      );
      await expectLater(
        api.bountyWallet(
          'owner',
          'repo',
          ts: '1770000000000',
          sig: 'wallet-sig',
        ),
        throwsA(isA<UnsupportedError>()),
      );
    },
  );

  test('published discussions include category and replies', () async {
    final server = await HttpServer.bind(InternetAddress.loopbackIPv4, 0);
    final subscription = server.listen((request) async {
      request.response.headers.contentType = ContentType.json;
      final path = request.uri.queryParameters['path'] ?? '';
      if (request.uri.path.endsWith('/tree') &&
          path == '.forkmesh/discussions') {
        request.response.write(
          jsonEncode({
            'entries': [
              {'name': '3', 'path': '.forkmesh/discussions/3', 'type': 'dir'},
            ],
          }),
        );
      } else if (request.uri.path.endsWith('/tree') &&
          path == '.forkmesh/discussions/3') {
        request.response.write(
          jsonEncode({
            'entries': [
              {
                'name': 'discussion.md',
                'path': '.forkmesh/discussions/3/discussion.md',
                'type': 'file',
              },
              {
                'name': '001-comment.md',
                'path': '.forkmesh/discussions/3/001-comment.md',
                'type': 'file',
              },
            ],
          }),
        );
      } else if (request.uri.path.endsWith('/blob') &&
          path == '.forkmesh/discussions/3/discussion.md') {
        request.response.write(
          jsonEncode({
            'content': '''---
title: Mobile maintainer workflow
category: ideas
authorName: Alice
---
How should maintainers review from phones?''',
          }),
        );
      } else if (request.uri.path.endsWith('/blob') &&
          path == '.forkmesh/discussions/3/001-comment.md') {
        request.response.write(
          jsonEncode({
            'content': '''---
type: comment
authorName: Mona
ts: 10
---
Keep desktop node as the canonical apply surface.''',
          }),
        );
      } else {
        request.response.statusCode = HttpStatus.notFound;
        request.response.write('not found');
      }
      await request.response.close();
    });
    addTearDown(() async {
      await subscription.cancel();
      await server.close(force: true);
    });

    SharedPreferences.setMockInitialValues({});
    final settings = await SettingsService.create();
    await settings.setServerUrl(
      'ws://${server.address.host}:${server.port}/ws',
    );
    final api = ApiService(settings);

    final discussions = await api.publishedDiscussions('owner', 'repo');

    expect(discussions.single.title, 'Mobile maintainer workflow');
    expect(discussions.single.category, 'ideas');
    expect(
      discussions.single.events.single.body,
      'Keep desktop node as the canonical apply surface.',
    );
  });

  test('notifications fetch and mark read through worker API', () async {
    final requests = <Map<String, dynamic>>[];
    final server = await HttpServer.bind(InternetAddress.loopbackIPv4, 0);
    final subscription = server.listen((request) async {
      requests.add({
        'method': request.method,
        'path': request.uri.path,
        'query': request.uri.queryParameters,
        'body': await utf8.decoder.bind(request).join(),
      });
      request.response.headers.contentType = ContentType.json;
      if (request.method == 'GET' && request.uri.path == '/api/notifications') {
        expect(request.uri.queryParameters['node'], 'mona');
        expect(request.uri.queryParameters['limit'], '25');
        request.response.write(
          jsonEncode({
            'ok': true,
            'unread': 1,
            'notifications': [
              {
                'id': 'n1',
                'kind': 'mention',
                'title': 'You were mentioned in mona/forkmesh',
                'repo': 'mona/forkmesh',
                'ts': 1770000000000,
              },
            ],
          }),
        );
      } else if (request.method == 'POST' &&
          request.uri.path == '/api/notifications') {
        request.response.write(jsonEncode({'ok': true}));
      } else {
        request.response.statusCode = HttpStatus.notFound;
      }
      await request.response.close();
    });
    addTearDown(() async {
      await subscription.cancel();
      await server.close(force: true);
    });

    SharedPreferences.setMockInitialValues({});
    final settings = await SettingsService.create();
    await settings.setServerUrl(
      'ws://${server.address.host}:${server.port}/ws',
    );
    final api = ApiService(settings);

    final page = await api.notifications('mona', limit: 25);
    await api.markNotificationsRead('mona', ids: ['n1']);
    await api.markNotificationsRead('mona', all: true);

    expect(page.unread, 1);
    expect(page.notifications.single.kind, 'mention');
    final idsPost = jsonDecode(requests[1]['body'] as String);
    expect(idsPost, {
      'node': 'mona',
      'ids': ['n1'],
    });
    final allPost = jsonDecode(requests[2]['body'] as String);
    expect(allPost, {'node': 'mona', 'all': true});
  });

  test(
    'published markdown pulls include signed comment and review timeline events',
    () async {
      final server = await HttpServer.bind(InternetAddress.loopbackIPv4, 0);
      final subscription = server.listen((request) async {
        request.response.headers.contentType = ContentType.json;
        final path = request.uri.queryParameters['path'] ?? '';
        if (request.uri.path.endsWith('/tree') && path == 'pulls') {
          request.response.write(
            jsonEncode({
              'entries': [
                {'name': '7', 'path': 'pulls/7', 'type': 'dir'},
              ],
            }),
          );
        } else if (request.uri.path.endsWith('/tree') && path == 'pulls/7') {
          request.response.write(
            jsonEncode({
              'entries': [
                {'name': 'pull.md', 'path': 'pulls/7/pull.md', 'type': 'file'},
                {
                  'name': '001-comment.md',
                  'path': 'pulls/7/001-comment.md',
                  'type': 'file',
                },
                {
                  'name': '002-review.md',
                  'path': 'pulls/7/002-review.md',
                  'type': 'file',
                },
              ],
            }),
          );
        } else if (request.uri.path.endsWith('/blob') &&
            path == 'pulls/7/pull.md') {
          request.response.write(
            jsonEncode({
              'content': '''---
title: Add signed review flow
status: open
base: main
head: review/mobile
sig: abc
---
Adds mobile review controls.

```diff
diff --git a/lib/a.dart b/lib/a.dart
+review
```''',
            }),
          );
        } else if (request.uri.path.endsWith('/blob') &&
            path == 'pulls/7/001-comment.md') {
          request.response.write(
            jsonEncode({
              'content': '''---
type: comment
authorName: Kai
ts: 10
---
Review changes before merge.''',
            }),
          );
        } else if (request.uri.path.endsWith('/blob') &&
            path == 'pulls/7/002-review.md') {
          request.response.write(
            jsonEncode({
              'content': '''---
type: review
state: approve
authorName: Mona
ts: 11
---
LGTM from mobile.''',
            }),
          );
        } else {
          request.response.statusCode = HttpStatus.notFound;
          request.response.write('not found');
        }
        await request.response.close();
      });
      addTearDown(() async {
        await subscription.cancel();
        await server.close(force: true);
      });

      SharedPreferences.setMockInitialValues({});
      final settings = await SettingsService.create();
      await settings.setServerUrl(
        'ws://${server.address.host}:${server.port}/ws',
      );
      final api = ApiService(settings);

      final pulls = await api.publishedPulls('owner', 'repo');

      expect(pulls.single.title, 'Add signed review flow');
      expect(pulls.single.signed, isTrue);
      expect(pulls.single.events, hasLength(2));
      expect(pulls.single.events.first.body, 'Review changes before merge.');
      expect(pulls.single.events.last.state, 'approve');
    },
  );

  test(
    'releases parse release manifests with artifact download counts',
    () async {
      final requests = <String>[];
      final server = await HttpServer.bind(InternetAddress.loopbackIPv4, 0);
      final sha =
          '1938916325d5839850fbc39db05a3a1f836b10615ac4467725b9f49e864884fb';
      final secondSha =
          '2938916325d5839850fbc39db05a3a1f836b10615ac4467725b9f49e864884fc';
      final subscription = server.listen((request) async {
        requests.add('${request.uri.path}?${request.uri.query}');
        request.response.headers.contentType = ContentType.json;
        if (request.uri.path == '/api/repo/owner/repo/tree') {
          request.response.write(
            jsonEncode({
              'entries': [
                {
                  'name': 'latest',
                  'path': '.forkmesh/releases/latest',
                  'type': 'dir',
                },
              ],
            }),
          );
        } else if (request.uri.path == '/api/repo/owner/repo/blob') {
          request.response.write(
            jsonEncode({
              'content': jsonEncode({
                'schema': 'forkmesh-release-v1',
                'tag': 'v1.2.3',
                'channel': 'latest',
                'tag_commit': 'abcdef1234567890',
                'created_at': 1700000000000,
                'assets': [
                  {
                    'name': 'forkmesh-linux-x86_64',
                    'blob_sha256': sha,
                    'size': 14949672,
                    'os': 'linux',
                    'arch': 'x86_64',
                  },
                  {
                    'name': 'forkmesh-darwin-arm64',
                    'sha256': secondSha,
                    'size': '2048',
                    'os': 'darwin',
                    'arch': 'arm64',
                  },
                ],
              }),
            }),
          );
        } else if (request.uri.path ==
            '/api/repo/owner/repo/releases/downloads') {
          request.response.write(
            jsonEncode({
              'ok': true,
              'counts': {sha: 3, secondSha: '5'},
            }),
          );
        } else {
          request.response.statusCode = 404;
          request.response.write(jsonEncode({'error': 'not_found'}));
        }
        await request.response.close();
      });
      addTearDown(() async {
        await subscription.cancel();
        await server.close(force: true);
      });

      SharedPreferences.setMockInitialValues({});
      final settings = await SettingsService.create();
      await settings.setServerUrl(
        'ws://${server.address.host}:${server.port}/ws',
      );
      final api = ApiService(settings);

      final releases = await api.releases('owner', 'repo');

      expect(releases.single.tag, 'v1.2.3');
      expect(releases.single.channel, 'latest');
      expect(releases.single.shortCommit, 'abcdef12');
      expect(releases.single.assets.map((asset) => asset.sha256), [
        sha,
        secondSha,
      ]);
      expect(releases.single.assets.first.downloads, 3);
      expect(releases.single.assets.first.sizeLabel, '14.3 MB');
      expect(releases.single.assets.last.downloads, 5);
      expect(releases.single.assets.last.sizeLabel, '2.0 KB');
      expect(requests, contains('/api/repo/owner/repo/releases/downloads?'));
    },
  );

  test('releases return empty list when release tree is absent', () async {
    final requests = <String>[];
    final server = await HttpServer.bind(InternetAddress.loopbackIPv4, 0);
    final subscription = server.listen((request) async {
      requests.add(request.uri.path);
      request.response.headers.contentType = ContentType.json;
      if (request.uri.path == '/api/repo/owner/repo/releases/downloads') {
        request.response.statusCode = HttpStatus.serviceUnavailable;
        request.response.write(jsonEncode({'error': 'offline'}));
      } else if (request.uri.path == '/api/repo/owner/repo/tree') {
        expect(request.uri.queryParameters['path'], '.forkmesh/releases');
        request.response.statusCode = HttpStatus.notFound;
        request.response.write(jsonEncode({'error': 'not_found'}));
      } else {
        request.response.statusCode = HttpStatus.notFound;
        request.response.write(jsonEncode({'error': 'not_found'}));
      }
      await request.response.close();
    });
    addTearDown(() async {
      await subscription.cancel();
      await server.close(force: true);
    });

    SharedPreferences.setMockInitialValues({});
    final settings = await SettingsService.create();
    await settings.setServerUrl(
      'ws://${server.address.host}:${server.port}/ws',
    );
    final api = ApiService(settings);

    await expectLater(api.releases('owner', 'repo'), completion(isEmpty));
    expect(requests, contains('/api/repo/owner/repo/releases/downloads'));
    expect(requests, contains('/api/repo/owner/repo/tree'));
  });

  test('agent session list posts ownerAccount and parses agents', () async {
    final server = await HttpServer.bind(InternetAddress.loopbackIPv4, 0);
    Map<String, dynamic> posted = const {};
    final subscription = server.listen((request) async {
      expect(request.method, 'POST');
      expect(request.uri.path, '/api/repo/owner/repo/agents/list');
      posted =
          jsonDecode(await utf8.decoder.bind(request).join())
              as Map<String, dynamic>;
      request.response.headers.contentType = ContentType.json;
      request.response.write(
        jsonEncode({
          'ok': true,
          'agents': [
            {
              'id': 42,
              'status': 'done',
              'provider': 'claude-code',
              'prNumber': 7,
              'diffStats': {'files': 3, 'ahead': 2, 'behind': 1},
            },
          ],
        }),
      );
      await request.response.close();
    });
    addTearDown(() async {
      await subscription.cancel();
      await server.close(force: true);
    });

    SharedPreferences.setMockInitialValues({});
    final settings = await SettingsService.create();
    await settings.setServerUrl(
      'ws://${server.address.host}:${server.port}/ws',
    );
    final api = ApiService(settings);

    final sessions = await api.agentSessions(
      'owner',
      'repo',
      ownerAccount: 'owner',
    );

    expect(posted['ownerAccount'], 'owner');
    expect(sessions.single.id, 42);
    expect(sessions.single.providerLabel, 'Claude Code');
    expect(sessions.single.prNumber, 7);
    expect(sessions.single.diffLabel, '3 files · ↑2 ↓1');
  });

  test(
    'agent transcript posts ownerAccount and parses transcript detail',
    () async {
      final server = await HttpServer.bind(InternetAddress.loopbackIPv4, 0);
      Map<String, dynamic> posted = const {};
      final subscription = server.listen((request) async {
        expect(request.method, 'POST');
        expect(request.uri.path, '/api/repo/owner/repo/agents/42/transcript');
        posted =
            jsonDecode(await utf8.decoder.bind(request).join())
                as Map<String, dynamic>;
        request.response.headers.contentType = ContentType.json;
        request.response.write(
          jsonEncode({
            'ok': true,
            'status': 'done',
            'transcript': 'Running tests\nflutter test passed',
          }),
        );
        await request.response.close();
      });
      addTearDown(() async {
        await subscription.cancel();
        await server.close(force: true);
      });

      SharedPreferences.setMockInitialValues({});
      final settings = await SettingsService.create();
      await settings.setServerUrl(
        'ws://${server.address.host}:${server.port}/ws',
      );
      final api = ApiService(settings);

      final detail = await api.agentTranscript(
        'owner',
        'repo',
        42,
        ownerAccount: 'owner',
      );

      expect(posted['ownerAccount'], 'owner');
      expect(detail.status, 'done');
      expect(detail.transcript, contains('flutter test passed'));
    },
  );

  test(
    'action list and log post ownerAccount and parse action payloads',
    () async {
      final requests = <Map<String, dynamic>>[];
      final server = await HttpServer.bind(InternetAddress.loopbackIPv4, 0);
      final subscription = server.listen((request) async {
        final body = await utf8.decoder.bind(request).join();
        requests.add({
          'method': request.method,
          'path': request.uri.path,
          'body': body,
        });
        request.response.headers.contentType = ContentType.json;
        if (request.uri.path == '/api/repo/owner/repo/actions/list') {
          request.response.write(
            jsonEncode({
              'ok': true,
              'workflows': [
                {
                  'path': '.forkmesh/test.yml',
                  'name': 'Test suite',
                  'on': ['push'],
                  'stepCount': 2,
                },
              ],
              'runs': [
                {
                  'id': 7,
                  'workflowName': 'Test suite',
                  'status': 'running',
                  'ref': 'refs/heads/main',
                },
              ],
            }),
          );
        } else if (request.uri.path == '/api/repo/owner/repo/actions/7/log') {
          request.response.write(
            jsonEncode({
              'ok': true,
              'id': 7,
              'status': 'success',
              'log': 'flutter test passed',
            }),
          );
        } else {
          request.response.statusCode = HttpStatus.notFound;
          request.response.write('not found');
        }
        await request.response.close();
      });
      addTearDown(() async {
        await subscription.cancel();
        await server.close(force: true);
      });

      SharedPreferences.setMockInitialValues({});
      final settings = await SettingsService.create();
      await settings.setServerUrl(
        'ws://${server.address.host}:${server.port}/ws',
      );
      final api = ApiService(settings);

      final actions = await api.repoActions(
        'owner',
        'repo',
        ownerAccount: 'owner',
        ts: '1000',
        sig: 'signed-list',
      );
      final log = await api.actionLog(
        'owner',
        'repo',
        7,
        ownerAccount: 'owner',
        ts: '1001',
        sig: 'signed-log',
      );

      expect(requests[0]['method'], 'POST');
      expect(requests[0]['path'], '/api/repo/owner/repo/actions/list');
      expect(jsonDecode(requests[0]['body'] as String), {
        'ownerAccount': 'owner',
        'ts': '1000',
        'sig': 'signed-list',
      });
      expect(actions.workflows.single.name, 'Test suite');
      expect(actions.runs.single.statusLabel, 'Running');
      expect(requests[1]['path'], '/api/repo/owner/repo/actions/7/log');
      expect(jsonDecode(requests[1]['body'] as String), {
        'ownerAccount': 'owner',
        'ts': '1001',
        'sig': 'signed-log',
      });
      expect(log.statusLabel, 'Success');
      expect(log.log, 'flutter test passed');
    },
  );

  test(
    'desktopCommand posts signed mobile request payload and parses queue status',
    () async {
      final requests = <Map<String, Object?>>[];
      final server = await HttpServer.bind(InternetAddress.loopbackIPv4, 0);
      final subscription = server.listen((request) async {
        final body = await utf8.decoder.bind(request).join();
        requests.add({
          'method': request.method,
          'path': request.uri.path,
          'body': body,
        });
        request.response.headers.contentType = ContentType.json;
        request.response.write(jsonEncode({'ok': true, 'queued': 1}));
        await request.response.close();
      });
      addTearDown(() async {
        await subscription.cancel();
        await server.close(force: true);
      });

      SharedPreferences.setMockInitialValues({});
      final settings = await SettingsService.create();
      await settings.setServerUrl(
        'ws://${server.address.host}:${server.port}/ws',
      );
      final api = ApiService(settings);

      final result = await api.desktopCommand(
        'owner',
        'repo',
        ownerAccount: 'owner',
        command: 'action.rerun',
        target: 'run:7',
        ts: '1000',
        sig: 'signed-command',
        payload: const {'runId': 7, 'workflowPath': '.forkmesh/test.yml'},
      );

      expect(result.ok, isTrue);
      expect(result.queued, 1);
      expect(requests.single['method'], 'POST');
      expect(requests.single['path'], '/api/repo/owner/repo/desktop-commands');
      expect(jsonDecode(requests.single['body'] as String), {
        'ownerAccount': 'owner',
        'command': 'action.rerun',
        'target': 'run:7',
        'ts': '1000',
        'sig': 'signed-command',
        'payload': {'runId': 7, 'workflowPath': '.forkmesh/test.yml'},
      });
    },
  );
}
