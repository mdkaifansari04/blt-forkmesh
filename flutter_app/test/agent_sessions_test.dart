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
    });

    expect(session.id, 42);
    expect(session.issueNumber, 12);
    expect(session.issueTitle, 'Stabilize signed inbox');
    expect(session.statusLabel, 'Running');
    expect(session.providerLabel, 'OpenAI');
    expect(session.displayTitle, 'Stabilize signed inbox');
    expect(session.branchName, 'agent/fix-inbox');
    expect(session.numTurns, 7);
    expect(session.durationLabel, '2m 5s');
    expect(session.costLabel, r'$1.25');
    expect(session.isActive, isTrue);
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
}
