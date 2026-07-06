import 'dart:convert';

import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:forkmesh/models/models.dart';
import 'package:forkmesh/screens/repo_detail_screen.dart';
import 'package:forkmesh/services/api_service.dart';
import 'package:forkmesh/services/auth_service.dart';
import 'package:forkmesh/services/identity.dart';
import 'package:forkmesh/services/settings_service.dart';
import 'package:forkmesh/theme.dart';
import 'package:provider/provider.dart';
import 'package:shared_preferences/shared_preferences.dart';

class ActionApiService extends ApiService {
  ActionApiService(super.settings);

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
  Future<RepoActions> repoActions(
    String owner,
    String name, {
    String ownerAccount = '',
    String ts = '',
    String sig = '',
  }) async => RepoActions(
    workflows: const [
      ActionWorkflow(
        path: '.forkmesh/test.yml',
        name: 'Test suite',
        triggers: ['push', 'workflow_dispatch'],
        stepCount: 2,
        manual: true,
        valid: true,
      ),
    ],
    runs: const [
      ActionRun(
        id: 7,
        workflowPath: '.forkmesh/test.yml',
        workflowName: 'Test suite',
        commit: 'abcdef123456',
        ref: 'refs/heads/main',
        status: 'running',
        createdAtMs: 1700000000000,
        startedAtMs: 1700000001000,
      ),
    ],
  );

  @override
  Future<ActionLog> actionLog(
    String owner,
    String name,
    int id, {
    String ownerAccount = '',
    String ts = '',
    String sig = '',
  }) async => const ActionLog(
    id: 7,
    status: 'success',
    log: 'checkout\nflutter test passed\nartifact uploaded',
  );
}

class PollingActionApiService extends ActionApiService {
  PollingActionApiService(super.settings);

  int calls = 0;

  @override
  Future<RepoActions> repoActions(
    String owner,
    String name, {
    String ownerAccount = '',
    String ts = '',
    String sig = '',
  }) async {
    calls += 1;
    return RepoActions(
      workflows: const [
        ActionWorkflow(path: '.forkmesh/test.yml', name: 'Test suite'),
      ],
      runs: [
        ActionRun(
          id: 7,
          workflowName: 'Test suite',
          status: calls == 1 ? 'running' : 'success',
          ref: 'refs/heads/main',
        ),
      ],
    );
  }
}

Future<void> _pumpRepo(
  WidgetTester tester,
  ApiService api,
  SettingsService settings,
) async {
  tester.view.physicalSize = const Size(900, 1200);
  tester.view.devicePixelRatio = 1;
  addTearDown(tester.view.resetPhysicalSize);
  addTearDown(tester.view.resetDevicePixelRatio);
  final identity = await Identity.loadOrCreate();
  final prefs = await SharedPreferences.getInstance();
  await prefs.setString(
    'auth/session',
    jsonEncode(
      AuthSession(
        nodeName: 'owner',
        email: 'owner@example.com',
        status: 'active',
        pubkey: identity.publicKeyB64url,
        emailVerified: true,
        isAdmin: false,
        solana: '',
        hasPayoutAddress: false,
        avatarPng: '',
        avatarUpdatedAt: 0,
        createdAt: 1,
        savedAt: 1,
      ).toJson(),
    ),
  );
  final auth = AuthService(settings, identity, prefs);
  await tester.pumpWidget(
    MultiProvider(
      providers: [
        Provider<ApiService>.value(value: api),
        Provider<Identity>.value(value: identity),
        ChangeNotifierProvider<AuthService>.value(value: auth),
      ],
      child: MaterialApp(
        theme: buildForkMeshLightTheme(),
        home: RepoDetailScreen(
          repo: Repository(owner: 'owner', name: 'repo', defaultBranch: 'main'),
        ),
      ),
    ),
  );
  await tester.pumpAndSettle();
}

void main() {
  test('Action workflow, run, and log parse Worker payloads', () {
    final actions = RepoActions.fromJson({
      'workflows': [
        {
          'path': '.forkmesh/test.yml',
          'name': 'Test suite',
          'on': ['push', 'workflow_dispatch'],
          'stepCount': '2',
          'manual': true,
          'valid': true,
        },
      ],
      'runs': [
        {
          'id': '7',
          'workflowPath': '.forkmesh/test.yml',
          'workflowName': 'Test suite',
          'commit': 'abcdef123456',
          'ref': 'refs/heads/main',
          'status': 'running',
          'createdAtMs': '1700000000000',
          'startedAtMs': 1700000001000,
          'finishedAtMs': 0,
        },
      ],
    });

    expect(actions.workflows.single.name, 'Test suite');
    expect(actions.workflows.single.triggers, ['push', 'workflow_dispatch']);
    expect(actions.workflows.single.stepCount, 2);
    expect(actions.workflows.single.manual, isTrue);
    expect(actions.runs.single.id, 7);
    expect(actions.runs.single.workflowName, 'Test suite');
    expect(actions.runs.single.statusLabel, 'Running');
    expect(actions.runs.single.isActive, isTrue);
    expect(actions.runs.single.refLabel, 'main');
    expect(actions.runs.single.shortCommit, 'abcdef12');

    final log = ActionLog.fromJson({
      'id': '7',
      'status': 'success',
      'log': 'tests passed',
    });
    expect(log.id, 7);
    expect(log.statusLabel, 'Success');
    expect(log.log, 'tests passed');
  });

  testWidgets('repo Actions tab lists workflows and opens run log detail', (
    tester,
  ) async {
    SharedPreferences.setMockInitialValues({});
    final settings = await SettingsService.create();
    await _pumpRepo(tester, ActionApiService(settings), settings);

    await tester.ensureVisible(find.text('Actions'));
    await tester.tap(find.text('Actions'));
    await tester.pumpAndSettle();

    expect(find.text('WORKFLOWS'), findsOneWidget);
    expect(find.text('Test suite'), findsWidgets);
    expect(find.text('RUNNING'), findsWidgets);
    expect(find.text('MAIN'), findsOneWidget);
    expect(find.text('.forkmesh/test.yml'), findsOneWidget);
    expect(find.textContaining('Desktop node runs workflows'), findsOneWidget);

    await tester.tap(find.text('Test suite').last);
    await tester.pumpAndSettle();

    expect(find.text('Action run #7'), findsOneWidget);
    expect(find.text('Run log'), findsOneWidget);
    expect(find.textContaining('flutter test passed'), findsOneWidget);
    expect(find.textContaining('rerun, cancel, approve'), findsOneWidget);
  });

  testWidgets('repo Actions tab auto-refreshes while runs are active', (
    tester,
  ) async {
    SharedPreferences.setMockInitialValues({});
    final settings = await SettingsService.create();
    final api = PollingActionApiService(settings);
    await _pumpRepo(tester, api, settings);

    await tester.ensureVisible(find.text('Actions'));
    await tester.tap(find.text('Actions'));
    await tester.pumpAndSettle();

    expect(api.calls, 1);
    expect(find.text('RUNNING'), findsWidgets);
    expect(find.text('Live refresh on'), findsOneWidget);

    await tester.pump(const Duration(seconds: 11));
    await tester.pumpAndSettle();

    expect(api.calls, greaterThanOrEqualTo(2));
    expect(find.text('SUCCESS'), findsWidgets);
  });
}
