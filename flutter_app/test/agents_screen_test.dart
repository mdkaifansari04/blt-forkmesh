import 'dart:async';

import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:forkmesh/models/models.dart';
import 'package:forkmesh/screens/agents_screen.dart';
import 'package:forkmesh/services/api_service.dart';
import 'package:forkmesh/services/settings_service.dart';
import 'package:forkmesh/theme.dart';
import 'package:provider/provider.dart';
import 'package:shared_preferences/shared_preferences.dart';

class GlobalAgentsApiService extends ApiService {
  GlobalAgentsApiService(super.settings);

  int repositoriesCalls = 0;
  final Map<String, int> sessionCalls = {};

  @override
  Future<List<Repository>> repositories() async {
    repositoriesCalls += 1;
    return [
      Repository(owner: 'alice', name: 'mobile', cloneOnline: true),
      Repository(owner: 'bob', name: 'worker', liveHost: true),
    ];
  }

  @override
  Future<List<AgentSession>> agentSessions(
    String owner,
    String name, {
    String ownerAccount = '',
  }) async {
    final key = '$owner/$name';
    sessionCalls[key] = (sessionCalls[key] ?? 0) + 1;
    if (key == 'alice/mobile') {
      return [
        AgentSession(
          id: 7,
          issueNumber: 12,
          issueTitle: 'Stabilize signed inbox',
          status: sessionCalls[key] == 1 ? 'running' : 'done',
          provider: 'codex',
          branchName: 'agent/signed-inbox',
          numTurns: sessionCalls[key]!,
          createPr: true,
          prNumber: 7,
          baseBranch: 'main',
          filesChanged: 3,
          ahead: 2,
          behind: 1,
          conflicted: true,
          totalTokens: 1500,
        ),
      ];
    }
    return [
      AgentSession(
        id: 8,
        issueTitle: 'Publish Worker relay tests',
        status: 'done',
        provider: 'claude-code',
      ),
    ];
  }

  @override
  Future<AgentTranscript> agentTranscript(
    String owner,
    String name,
    int id, {
    String ownerAccount = '',
  }) async => AgentTranscript(
    status: id == 7 ? 'done' : 'running',
    transcript: 'Session $id transcript for $owner/$name',
  );
}

class SlowGlobalAgentsApiService extends ApiService {
  SlowGlobalAgentsApiService(super.settings);

  int repositoriesCalls = 0;
  int sessionCalls = 0;
  final Completer<List<Repository>> repositoriesCompleter = Completer();

  @override
  Future<List<Repository>> repositories() {
    repositoriesCalls += 1;
    return repositoriesCompleter.future;
  }

  @override
  Future<List<AgentSession>> agentSessions(
    String owner,
    String name, {
    String ownerAccount = '',
  }) async {
    sessionCalls += 1;
    return [
      AgentSession(
        id: 99,
        issueTitle: 'Long running agent',
        status: 'running',
        provider: 'codex',
      ),
    ];
  }
}

Future<GlobalAgentsApiService> _pumpAgents(WidgetTester tester) async {
  SharedPreferences.setMockInitialValues({});
  final settings = await SettingsService.create();
  final api = GlobalAgentsApiService(settings);
  await tester.pumpWidget(
    Provider<ApiService>.value(
      value: api,
      child: MaterialApp(
        theme: buildForkMeshLightTheme(),
        home: const Scaffold(body: AgentsScreen()),
      ),
    ),
  );
  await tester.pumpAndSettle();
  return api;
}

Future<SlowGlobalAgentsApiService> _pumpToggleableAgents(
  WidgetTester tester, {
  required bool enabled,
  SlowGlobalAgentsApiService? api,
}) async {
  SharedPreferences.setMockInitialValues({});
  final settings = await SettingsService.create();
  final service = api ?? SlowGlobalAgentsApiService(settings);
  await tester.pumpWidget(
    Provider<ApiService>.value(
      value: service,
      child: MaterialApp(
        theme: buildForkMeshLightTheme(),
        home: Scaffold(body: AgentsScreen(enabled: enabled)),
      ),
    ),
  );
  await tester.pump();
  return service;
}

void main() {
  testWidgets('global Agents screen groups sessions by repository', (
    tester,
  ) async {
    final api = await _pumpAgents(tester);

    expect(api.repositoriesCalls, 1);
    expect(api.sessionCalls['alice/mobile'], 1);
    expect(api.sessionCalls['bob/worker'], 1);
    expect(find.text('Agents'), findsOneWidget);
    expect(find.text('1 running'), findsOneWidget);
    expect(find.text('alice/mobile'), findsOneWidget);
    expect(find.text('bob/worker'), findsOneWidget);
    expect(find.text('Stabilize signed inbox'), findsOneWidget);
    expect(find.text('Publish Worker relay tests'), findsOneWidget);
    expect(find.text('RUNNING'), findsWidgets);
    expect(find.text('DONE'), findsWidgets);
    expect(find.text('PR #7'), findsOneWidget);
    expect(find.text('3 FILES · ↑2 ↓1'), findsOneWidget);
    expect(find.text('CONFLICT'), findsOneWidget);
    expect(find.text('1500 TOKENS'), findsOneWidget);
    expect(find.text('Live refresh on'), findsOneWidget);
  });

  testWidgets(
    'global Agents screen refreshes running sessions and opens detail',
    (tester) async {
      final api = await _pumpAgents(tester);

      await tester.pump(const Duration(seconds: 11));
      await tester.pumpAndSettle();

      expect(api.sessionCalls['alice/mobile'], greaterThanOrEqualTo(2));
      expect(find.text('DONE'), findsWidgets);

      await tester.tap(find.text('Stabilize signed inbox'));
      await tester.pumpAndSettle();

      expect(find.text('Agent session #7'), findsOneWidget);
      expect(find.text('Transcript'), findsOneWidget);
      expect(find.text('PR #7'), findsOneWidget);
      expect(find.text('3 FILES · ↑2 ↓1'), findsOneWidget);
      expect(find.text('CONFLICT'), findsOneWidget);
      expect(find.text('Open PR #7'), findsOneWidget);
      expect(
        find.textContaining('Session 7 transcript for alice/mobile'),
        findsOneWidget,
      );
    },
  );

  testWidgets('disabled global Agents screen ignores in-flight loads', (
    tester,
  ) async {
    final api = await _pumpToggleableAgents(tester, enabled: true);

    expect(api.repositoriesCalls, 1);

    await _pumpToggleableAgents(tester, enabled: false, api: api);
    api.repositoriesCompleter.complete([
      Repository(owner: 'alice', name: 'mobile'),
    ]);
    await tester.pumpAndSettle();
    await tester.pump(const Duration(seconds: 11));
    await tester.pump();

    expect(api.sessionCalls, 0);
    expect(api.repositoriesCalls, 1);
  });
}
