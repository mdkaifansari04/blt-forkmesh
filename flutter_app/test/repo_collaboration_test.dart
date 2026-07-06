import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:forkmesh/models/models.dart';
import 'package:forkmesh/screens/repo_detail_screen.dart';
import 'package:forkmesh/services/api_service.dart';
import 'package:forkmesh/services/settings_service.dart';
import 'package:forkmesh/theme.dart';
import 'package:provider/provider.dart';
import 'package:shared_preferences/shared_preferences.dart';

class CollaborationApiService extends ApiService {
  CollaborationApiService(super.settings);

  @override
  Future<List<RepoBranch>> branches(String owner, String name) async => [
    RepoBranch(name: 'main', isDefault: true),
  ];

  @override
  Future<RepoTree> tree(
    String owner,
    String name, {
    String path = '',
    String ref = '',
  }) async => RepoTree(path: path, entries: const []);

  @override
  Future<List<Issue>> publishedIssues(String owner, String name) async => [
    Issue(
      number: 12,
      title: 'Stabilize signed inbox',
      status: 'open',
      body: 'Mobile should make signed collaboration clear.',
      author: 'alice',
      labels: const ['mobile', 'signed'],
      votes: 2,
      bountyUsd: 150,
      events: [
        IssueEvent(
          type: 'comment',
          body: 'I can reproduce this on mobile.',
          authorName: 'Mona',
        ),
      ],
    ),
  ];

  @override
  Future<List<PublishedPull>> publishedPulls(String owner, String name) async =>
      [
        PublishedPull(
          number: 7,
          title: 'Add signed review flow',
          status: 'open',
          body: 'Adds mobile review controls.',
          base: 'main',
          head: 'review/mobile',
          patch: 'diff --git a/lib/a.dart b/lib/a.dart\n+review',
          signed: true,
          events: const [
            PullEvent(
              type: 'comment',
              body: 'Review changes before merge.',
              authorName: 'Kai',
            ),
            PullEvent(
              type: 'review',
              state: 'approve',
              body: 'LGTM from mobile.',
              authorName: 'Mona',
            ),
            PullEvent(
              type: 'review',
              state: 'changes_requested',
              body: 'Please add a regression test.',
              authorName: 'Lin',
            ),
          ],
        ),
      ];
}

Future<void> _pumpRepo(WidgetTester tester, ApiService api) async {
  final repo = Repository(owner: 'owner', name: 'repo', defaultBranch: 'main');
  await tester.pumpWidget(
    Provider<ApiService>.value(
      value: api,
      child: MaterialApp(
        theme: buildForkMeshLightTheme(),
        home: RepoDetailScreen(repo: repo),
      ),
    ),
  );
  await tester.pumpAndSettle();
}

void main() {
  testWidgets('issue detail surfaces signed collaboration state', (
    tester,
  ) async {
    SharedPreferences.setMockInitialValues({});
    final settings = await SettingsService.create();
    await _pumpRepo(tester, CollaborationApiService(settings));

    await tester.tap(find.text('Issues'));
    await tester.pumpAndSettle();
    await tester.tap(find.text('Stabilize signed inbox'));
    await tester.pumpAndSettle();

    expect(find.text('mobile'), findsOneWidget);
    expect(find.text('signed'), findsOneWidget);
    expect(find.text('2 votes'), findsOneWidget);
    expect(find.text(r'$150 bounty'), findsOneWidget);
    expect(find.text('I can reproduce this on mobile.'), findsOneWidget);
    expect(
      find.textContaining('pending the repo owner applying'),
      findsOneWidget,
    );
  });

  testWidgets('pull detail renders signed comment and review timeline', (
    tester,
  ) async {
    SharedPreferences.setMockInitialValues({});
    final settings = await SettingsService.create();
    await _pumpRepo(tester, CollaborationApiService(settings));

    await tester.tap(find.text('Pulls'));
    await tester.pumpAndSettle();
    await tester.tap(find.text('Add signed review flow'));
    await tester.pumpAndSettle();

    expect(find.text('Timeline'), findsOneWidget);
    expect(find.text('Comment'), findsOneWidget);
    expect(find.text('Approved'), findsOneWidget);
    expect(find.text('Requested changes'), findsOneWidget);
    expect(find.text('Review changes before merge.'), findsOneWidget);
    expect(find.text('LGTM from mobile.'), findsOneWidget);
    expect(find.text('Please add a regression test.'), findsOneWidget);
    expect(
      find.textContaining('pending the repo owner applying'),
      findsOneWidget,
    );
  });
}
