import 'dart:convert';
import 'dart:io';

import 'package:flutter_test/flutter_test.dart';
import 'package:forkmesh/services/api_service.dart';
import 'package:forkmesh/services/settings_service.dart';
import 'package:shared_preferences/shared_preferences.dart';

void main() {
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
  });

  test('published discussions include category and replies', () async {
    final server = await HttpServer.bind(InternetAddress.loopbackIPv4, 0);
    final subscription = server.listen((request) async {
      request.response.headers.contentType = ContentType.json;
      final path = request.uri.queryParameters['path'] ?? '';
      if (request.uri.path.endsWith('/tree') && path == 'discussions') {
        request.response.write(
          jsonEncode({
            'entries': [
              {'name': '3', 'path': 'discussions/3', 'type': 'dir'},
            ],
          }),
        );
      } else if (request.uri.path.endsWith('/tree') &&
          path == 'discussions/3') {
        request.response.write(
          jsonEncode({
            'entries': [
              {
                'name': 'discussion.md',
                'path': 'discussions/3/discussion.md',
                'type': 'file',
              },
              {
                'name': '001-comment.md',
                'path': 'discussions/3/001-comment.md',
                'type': 'file',
              },
            ],
          }),
        );
      } else if (request.uri.path.endsWith('/blob') &&
          path == 'discussions/3/discussion.md') {
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
          path == 'discussions/3/001-comment.md') {
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
            {'id': 42, 'status': 'done', 'provider': 'claude-code'},
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
}
