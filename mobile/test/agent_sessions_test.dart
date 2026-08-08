import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:forkmesh/models/models.dart';
import 'package:forkmesh/screens/repo_detail_screen.dart';
import 'package:forkmesh/services/api_service.dart';
import 'package:forkmesh/services/settings_service.dart';
import 'package:forkmesh/theme.dart';
import 'package:provider/provider.dart';
import 'package:shared_preferences/shared_preferences.dart';

class AgentApiService extends ApiService {
  AgentApiService(super.settings);

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
  Future<List<AgentSession>> agentSessions(
    String owner,
    String name, {
    String ownerAccount = '',
  }) async => [
    AgentSession(
      id: 42,
      issueNumber: 12,
      issueTitle: 'Stabilize signed inbox',
      status: 'running',
      provider: 'openai',
      model: 'gpt-5.5',
      branchName: 'agent/fix-inbox',
      numTurns: 7,
      durationMs: 125000,
      costUsd: 1.25,
    ),
  ];

  @override
  Future<AgentTranscript> agentTranscript(
    String owner,
    String name,
    int id, {
    String ownerAccount = '',
  }) async => const AgentTranscript(
    status: 'done',
    transcript: 'Running tests\nflutter test passed\nOpened PR #7',
  );
}

class PollingAgentApiService extends AgentApiService {
  PollingAgentApiService(super.settings);

  int calls = 0;

  @override
  Future<List<AgentSession>> agentSessions(
    String owner,
    String name, {
    String ownerAccount = '',
  }) async {
    calls += 1;
    return [
      AgentSession(
        id: 7,
        issueNumber: 4,
        issueTitle: 'Ship mobile agent monitor',
        status: calls == 1 ? 'running' : 'done',
        provider: 'codex',
        branchName: 'agent/mobile-monitor',
        numTurns: calls,
      ),
    ];
  }
}

Future<void> _pumpRepo(WidgetTester tester, ApiService api) async {
  tester.view.physicalSize = const Size(900, 1200);
  tester.view.devicePixelRatio = 1;
  addTearDown(tester.view.resetPhysicalSize);
  addTearDown(tester.view.resetDevicePixelRatio);
  await tester.pumpWidget(
    Provider<ApiService>.value(
      value: api,
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
  test('AgentSession parses Worker agent session payloads', () {
    final session = AgentSession.fromJson({
      'id': '42',
      'issueNumber': '12',
      'issueTitle': 'Stabilize signed inbox',
      'status': 'running',
      'provider': 'openai',
      'model': 'gpt-5.5',
      'branchName': 'agent/fix-inbox',
      'lastError': '',
      'createdAtMs': '1700000000000',
      'startedAtMs': 1700000001000,
      'finishedAtMs': 0,
      'numTurns': '7',
      'durationMs': 125000,
      'costUsd': '1.25',
      'createPr': true,
      'prNumber': '7',
      'baseRef': 'abc123',
      'baseBranch': 'main',
      'merged': true,
      'mergedAtMs': '1700000100000',
      'promptTokens': '1000',
      'completionTokens': 500,
      'totalTokens': '1500',
      'contextTokens': '12000',
      'contextWindow': '32000',
      'maxOutputTokens': '4000',
      'estimatedCredits': '6',
      'spendBeforeUsd': '2.00',
      'spendAfterUsd': '3.25',
      'diffStats': {
        'files': '3',
        'ahead': '2',
        'behind': '1',
        'conflicted': true,
      },
    });

    expect(session.id, 42);
    expect(session.issueNumber, 12);
    expect(session.issueTitle, 'Stabilize signed inbox');
    expect(session.statusLabel, 'Merged');
    expect(session.providerLabel, 'OpenAI');
    expect(session.displayTitle, 'Stabilize signed inbox');
    expect(session.branchName, 'agent/fix-inbox');
    expect(session.numTurns, 7);
    expect(session.durationLabel, '2m 5s');
    expect(session.costLabel, r'$1.25');
    expect(session.isActive, isFalse);
    expect(session.createPr, isTrue);
    expect(session.prNumber, 7);
    expect(session.prLabel, 'PR #7');
    expect(session.baseRef, 'abc123');
    expect(session.baseBranch, 'main');
    expect(session.merged, isTrue);
    expect(session.mergedAtMs, 1700000100000);
    expect(session.promptTokens, 1000);
    expect(session.completionTokens, 500);
    expect(session.totalTokens, 1500);
    expect(session.contextTokens, 12000);
    expect(session.contextWindow, 32000);
    expect(session.maxOutputTokens, 4000);
    expect(session.estimatedCredits, 6);
    expect(session.spendBeforeUsd, 2.0);
    expect(session.spendAfterUsd, 3.25);
    expect(session.filesChanged, 3);
    expect(session.ahead, 2);
    expect(session.behind, 1);
    expect(session.conflicted, isTrue);
    expect(session.diffLabel, '3 files · ↑2 ↓1');
  });

  testWidgets('repo Agents tab lists sessions and opens transcript detail', (
    tester,
  ) async {
    SharedPreferences.setMockInitialValues({});
    final settings = await SettingsService.create();
    await _pumpRepo(tester, AgentApiService(settings));

    await tester.ensureVisible(find.text('Agents'));
    await tester.tap(find.text('Agents'));
    await tester.pumpAndSettle();

    expect(find.text('AGENT SESSIONS'), findsOneWidget);
    expect(find.text('Stabilize signed inbox'), findsOneWidget);
    expect(find.text('RUNNING'), findsWidgets);
    expect(find.textContaining('OpenAI'), findsOneWidget);
    expect(find.text('#12'), findsOneWidget);
    expect(find.text('AGENT/FIX-INBOX'), findsOneWidget);

    await tester.tap(find.text('Stabilize signed inbox'));
    await tester.pumpAndSettle();

    expect(find.text('Agent session #42'), findsOneWidget);
    expect(find.text('Transcript'), findsOneWidget);
    expect(find.textContaining('flutter test passed'), findsOneWidget);
  });

  testWidgets('repo Agents tab auto-refreshes while sessions are running', (
    tester,
  ) async {
    SharedPreferences.setMockInitialValues({});
    final settings = await SettingsService.create();
    final api = PollingAgentApiService(settings);
    await _pumpRepo(tester, api);

    await tester.ensureVisible(find.text('Agents'));
    await tester.tap(find.text('Agents'));
    await tester.pumpAndSettle();

    expect(api.calls, 1);
    expect(find.text('RUNNING'), findsWidgets);
    expect(find.text('Live refresh on'), findsOneWidget);

    await tester.pump(const Duration(seconds: 11));
    await tester.pumpAndSettle();

    expect(api.calls, greaterThanOrEqualTo(2));
    expect(find.text('DONE'), findsWidgets);
  });
}
