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
    expect(issues.single.votes, 2);
    expect(issues.single.bountyUsd, 150);
  });

  test(
    'published pulls include signed comment and review timeline events',
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
}
