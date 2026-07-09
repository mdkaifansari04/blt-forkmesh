import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:forkmesh/models/models.dart';
import 'package:forkmesh/screens/repo_detail_screen.dart';
import 'package:forkmesh/services/api_service.dart';
import 'package:forkmesh/services/settings_service.dart';
import 'package:forkmesh/theme.dart';
import 'package:provider/provider.dart';
import 'package:shared_preferences/shared_preferences.dart';

class BranchAwareApiService extends ApiService {
  BranchAwareApiService(super.settings);

  final treeRefs = <String>[];
  final blobRefs = <String>[];
  final searchRefs = <String>[];
  final commitDetailHashes = <String>[];
  var nextBlob = RepoBlob(
    path: 'README.md',
    content: 'void main() {}',
    source: 'qt-node-a',
  );

  @override
  Future<List<RepoBranch>> branches(String owner, String name) async {
    return [
      RepoBranch(name: 'main', sha: 'aaa', isDefault: true),
      RepoBranch(name: 'dev', sha: 'bbb'),
    ];
  }

  @override
  Future<RepoTree> tree(
    String owner,
    String name, {
    String path = '',
    String ref = '',
  }) async {
    treeRefs.add(ref);
    return RepoTree(
      path: path,
      source: 'qt-node-a',
      entries: [
        RepoTreeEntry(name: 'README.md', path: 'README.md', type: 'file'),
        RepoTreeEntry(name: 'lib', path: 'lib', type: 'dir'),
      ],
    );
  }

  @override
  Future<RepoBlob> blob(
    String owner,
    String name,
    String path, {
    String ref = '',
  }) async {
    blobRefs.add(ref);
    return RepoBlob(
      path: path,
      content: nextBlob.content,
      encoding: nextBlob.encoding,
      size: nextBlob.size,
      source: nextBlob.source,
    );
  }

  @override
  Future<RepoSearchResults> searchRepo(
    String owner,
    String name,
    String query, {
    String ref = '',
  }) async {
    searchRefs.add(ref);
    return RepoSearchResults(
      code: [
        RepoCodeSearchMatch(path: 'lib/main.dart', line: 1, text: 'void main'),
      ],
    );
  }

  @override
  Future<List<Map<String, dynamic>>> commits(String owner, String name) async {
    return [
      {
        'hash': 'abcdef1234567890',
        'author': 'Mona',
        'date': '2026-07-04',
        'subject': 'Add mobile commit diff',
      },
    ];
  }

  @override
  Future<RepoCommitDetail> commitDetail(
    String owner,
    String name,
    String hash, {
    Map<String, dynamic> fallbackCommit = const {},
  }) async {
    commitDetailHashes.add(hash);
    return RepoCommitDetail(
      commit: {
        ...fallbackCommit,
        'hash': hash,
        'subject': 'Add mobile commit diff',
        'author': 'Mona',
        'body': 'Shows changed files and a unified diff.',
      },
      files: [RepoCommitFile(path: 'lib/main.dart', adds: '12', dels: '3')],
      diff: 'diff --git a/lib/main.dart b/lib/main.dart\n+hello\n-old',
      truncated: true,
      source: 'qt-node-a',
    );
  }
}

void main() {
  testWidgets(
    'code tab switches branches and reloads the tree with selected ref',
    (tester) async {
      SharedPreferences.setMockInitialValues({});
      final settings = await SettingsService.create();
      final api = BranchAwareApiService(settings);
      final repo = Repository(
        owner: 'owner',
        name: 'repo',
        defaultBranch: 'main',
      );

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

      expect(find.text('Branch: main'), findsOneWidget);
      expect(api.treeRefs, ['main']);

      await tester.tap(find.byTooltip('Switch branch'));
      await tester.pumpAndSettle();
      await tester.tap(find.text('dev').last);
      await tester.pumpAndSettle();

      expect(find.text('Branch: dev'), findsOneWidget);
      expect(api.treeRefs.last, 'dev');
    },
  );

  testWidgets('empty default branch lets the host choose its default ref', (
    tester,
  ) async {
    SharedPreferences.setMockInitialValues({});
    final settings = await SettingsService.create();
    final api = BranchAwareApiService(settings);
    final repo = Repository(owner: 'owner', name: 'repo', defaultBranch: '');

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

    expect(find.text('Branch: Default branch'), findsOneWidget);
    expect(api.treeRefs, ['']);
    expect(api.treeRefs, isNot(contains('main')));
  });

  testWidgets('go to file opens a searched file with the selected branch ref', (
    tester,
  ) async {
    SharedPreferences.setMockInitialValues({});
    final settings = await SettingsService.create();
    final api = BranchAwareApiService(settings);
    final repo = Repository(
      owner: 'owner',
      name: 'repo',
      defaultBranch: 'main',
    );

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

    await tester.tap(find.text('Go to file').first);
    await tester.pumpAndSettle();
    await tester.enterText(find.byType(TextField).last, 'main');
    await tester.tap(find.text('Search'));
    await tester.pumpAndSettle();

    expect(api.searchRefs, ['main']);
    expect(find.text('lib/main.dart'), findsOneWidget);

    await tester.tap(find.text('lib/main.dart'));
    await tester.pumpAndSettle();

    expect(find.text('main.dart'), findsOneWidget);
    expect(api.blobRefs, ['main']);
    expect(find.byTooltip('Copy path'), findsOneWidget);
    expect(find.byTooltip('Copy raw URL'), findsOneWidget);
  });

  testWidgets('code and file preview surface source context when available', (
    tester,
  ) async {
    SharedPreferences.setMockInitialValues({});
    final settings = await SettingsService.create();
    final api = BranchAwareApiService(settings);
    final repo = Repository(
      owner: 'owner',
      name: 'repo',
      defaultBranch: 'main',
    );

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

    expect(find.text('served by qt-node-a'), findsOneWidget);

    await tester.tap(find.text('README.md'));
    await tester.pumpAndSettle();

    expect(find.text('served by qt-node-a'), findsOneWidget);
  });

  testWidgets('unsupported file preview shows path and ref-aware raw URL', (
    tester,
  ) async {
    SharedPreferences.setMockInitialValues({});
    final settings = await SettingsService.create();
    final api = BranchAwareApiService(settings)
      ..nextBlob = RepoBlob(
        path: 'README.md',
        content: 'AAAB',
        encoding: 'base64',
        source: 'qt-node-a',
      );
    final repo = Repository(
      owner: 'owner',
      name: 'repo',
      defaultBranch: 'main',
    );

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

    await tester.tap(find.text('README.md'));
    await tester.pumpAndSettle();

    expect(find.text('README.md'), findsWidgets);
    expect(find.text('Path'), findsOneWidget);
    expect(find.text('README.md'), findsWidgets);
    expect(find.text('Raw URL'), findsOneWidget);
    expect(find.textContaining('ref=main'), findsOneWidget);
    expect(find.textContaining('binary file'), findsOneWidget);
    expect(find.text('served by qt-node-a'), findsOneWidget);
  });

  testWidgets('commit detail renders changed files and unified diff', (
    tester,
  ) async {
    SharedPreferences.setMockInitialValues({});
    final settings = await SettingsService.create();
    final api = BranchAwareApiService(settings);
    final repo = Repository(
      owner: 'owner',
      name: 'repo',
      defaultBranch: 'main',
    );

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

    await tester.tap(find.text('Commits'));
    await tester.pumpAndSettle();
    await tester.tap(find.text('Add mobile commit diff').first);
    await tester.pumpAndSettle();

    expect(api.commitDetailHashes, ['abcdef1234567890']);
    expect(find.text('1 changed file'), findsOneWidget);
    expect(find.text('lib/main.dart'), findsOneWidget);
    expect(find.text('+12'), findsOneWidget);
    expect(find.text('-3'), findsOneWidget);
    expect(find.text('Unified diff'), findsOneWidget);
    expect(find.textContaining('diff --git'), findsOneWidget);
    expect(find.textContaining('Diff truncated'), findsOneWidget);
    expect(find.text('Comment on commit'), findsOneWidget);
  });
}
