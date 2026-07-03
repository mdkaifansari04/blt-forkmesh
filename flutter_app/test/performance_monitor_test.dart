import 'package:flutter/material.dart';
import 'package:flutter/services.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:forkmesh/services/api_service.dart';
import 'package:forkmesh/services/performance_monitor_service.dart';
import 'package:forkmesh/services/settings_service.dart';
import 'package:forkmesh/theme.dart';
import 'package:forkmesh/widgets/dev_performance_overlay.dart';
import 'package:shared_preferences/shared_preferences.dart';

void main() {
  test('track logs only operations over the threshold', () async {
    final printed = <String>[];
    final monitor = PerformanceMonitorService(
      enabled: true,
      threshold: const Duration(milliseconds: 500),
      logSink: printed.add,
      now: () => DateTime.fromMillisecondsSinceEpoch(1000),
      elapsedForTest: () => const Duration(milliseconds: 650),
    );

    final result = await monitor.track(
      'repos.load',
      () async => 42,
      details: const {'screen': 'Code'},
    );

    expect(result, 42);
    expect(monitor.events, hasLength(1));
    expect(monitor.events.single.name, 'repos.load');
    expect(monitor.events.single.kind, PerformanceEventKind.operation);
    expect(monitor.events.single.duration, const Duration(milliseconds: 650));
    expect(monitor.events.single.details['screen'], 'Code');
    expect(printed.single, contains('repos.load'));
    expect(printed.single, contains('650ms'));
  });

  test('track ignores fast operations and disabled monitors', () async {
    final enabledMonitor = PerformanceMonitorService(
      enabled: true,
      threshold: const Duration(milliseconds: 500),
      elapsedForTest: () => const Duration(milliseconds: 120),
    );
    final disabledMonitor = PerformanceMonitorService(
      enabled: false,
      threshold: const Duration(milliseconds: 500),
      elapsedForTest: () => const Duration(seconds: 2),
    );

    await enabledMonitor.track('fast', () async {});
    await disabledMonitor.track('disabled', () async {});

    expect(enabledMonitor.events, isEmpty);
    expect(disabledMonitor.events, isEmpty);
  });

  test('records slow frame timing with useful debug details', () {
    final monitor = PerformanceMonitorService(
      enabled: true,
      threshold: const Duration(milliseconds: 500),
      logSink: (_) {},
    );

    monitor.recordFrameDelay(
      buildDuration: const Duration(milliseconds: 320),
      rasterDuration: const Duration(milliseconds: 260),
    );

    expect(monitor.events, hasLength(1));
    final event = monitor.events.single;
    expect(event.kind, PerformanceEventKind.frame);
    expect(event.name, 'frame');
    expect(event.duration, const Duration(milliseconds: 580));
    expect(event.details['buildMs'], 320);
    expect(event.details['rasterMs'], 260);
  });

  test('ApiService reports slow REST requests', () async {
    SharedPreferences.setMockInitialValues({});
    final settings = await SettingsService.create();
    await settings.setServerUrl('ws://localhost:8787/ws');
    final monitor = PerformanceMonitorService(
      enabled: true,
      elapsedForTest: () => const Duration(milliseconds: 700),
      logSink: (_) {},
    );
    final api = ApiService(settings, performanceMonitor: monitor);

    await expectLater(api.repositories(), throwsException);

    expect(monitor.events, hasLength(1));
    expect(monitor.events.single.name, 'api.GET /api/repositories');
    expect(monitor.events.single.details['path'], '/api/repositories');
    expect(monitor.events.single.details['error'], contains('HTTP 400'));
  });

  testWidgets('dev overlay opens slow event details in a popup', (
    tester,
  ) async {
    final monitor = PerformanceMonitorService(
      enabled: true,
      threshold: const Duration(milliseconds: 500),
      logSink: (_) {},
    );
    monitor.recordSlowEvent(
      kind: PerformanceEventKind.operation,
      name: 'auth.login',
      duration: const Duration(milliseconds: 700),
      details: const {'screen': 'Login'},
    );

    await tester.pumpWidget(
      MaterialApp(
        theme: buildForkMeshLightTheme(),
        home: Scaffold(
          body: DevPerformanceOverlay(
            monitor: monitor,
            child: const SizedBox.expand(),
          ),
        ),
      ),
    );

    expect(find.byKey(const ValueKey('dev-performance-indicator')), findsOne);
    expect(find.text('700ms'), findsOneWidget);

    await tester.tap(find.byKey(const ValueKey('dev-performance-indicator')));
    await tester.pumpAndSettle();

    expect(find.byKey(const ValueKey('dev-performance-log-popup')), findsOne);
    expect(find.text('Performance monitor'), findsOneWidget);
    expect(find.text('auth.login'), findsOneWidget);
    expect(find.textContaining('screen: Login'), findsOneWidget);
  });

  testWidgets(
    'dev overlay popup works from MaterialApp.builder and copies logs',
    (tester) async {
      final copiedText = <String>[];
      TestDefaultBinaryMessengerBinding.instance.defaultBinaryMessenger
          .setMockMethodCallHandler(SystemChannels.platform, (call) async {
            if (call.method == 'Clipboard.setData') {
              final data = Map<String, dynamic>.from(call.arguments as Map);
              copiedText.add(data['text'] as String);
            }
            return null;
          });
      addTearDown(
        () => TestDefaultBinaryMessengerBinding.instance.defaultBinaryMessenger
            .setMockMethodCallHandler(SystemChannels.platform, null),
      );

      final monitor = PerformanceMonitorService(
        enabled: true,
        threshold: const Duration(milliseconds: 500),
        logSink: (_) {},
        now: () => DateTime(2026, 7, 3, 12, 34, 56),
      );
      monitor.recordSlowEvent(
        kind: PerformanceEventKind.operation,
        name: 'repos.load',
        duration: const Duration(milliseconds: 820),
        details: const {'screen': 'Repos'},
        stackTrace: 'stack line 1\nstack line 2',
      );

      await tester.pumpWidget(
        MaterialApp(
          theme: buildForkMeshLightTheme(),
          builder: (context, child) {
            return DevPerformanceOverlay(
              monitor: monitor,
              child: child ?? const SizedBox.shrink(),
            );
          },
          home: const Scaffold(body: Text('Home')),
        ),
      );

      await tester.tap(find.byKey(const ValueKey('dev-performance-indicator')));
      await tester.pump();

      expect(tester.takeException(), isNull);
      expect(find.byKey(const ValueKey('dev-performance-log-popup')), findsOne);
      expect(find.text('Copy'), findsOneWidget);
      expect(find.text('Close'), findsOneWidget);

      await tester.tap(find.text('Copy'));
      await tester.pump();

      expect(copiedText.single, contains('repos.load'));
      expect(copiedText.single, contains('820ms'));
      expect(copiedText.single, contains('screen: Repos'));
      expect(copiedText.single, contains('stack line 1'));

      await tester.tap(find.text('Close'));
      await tester.pumpAndSettle();

      expect(
        find.byKey(const ValueKey('dev-performance-log-popup')),
        findsNothing,
      );
    },
  );

  testWidgets('dev overlay indicator can be dragged', (tester) async {
    final monitor = PerformanceMonitorService(
      enabled: true,
      threshold: const Duration(milliseconds: 500),
      logSink: (_) {},
    );
    monitor.recordSlowEvent(
      kind: PerformanceEventKind.operation,
      name: 'repos.load',
      duration: const Duration(milliseconds: 820),
    );

    await tester.pumpWidget(
      MaterialApp(
        theme: buildForkMeshLightTheme(),
        home: Scaffold(
          body: DevPerformanceOverlay(
            monitor: monitor,
            child: const SizedBox.expand(),
          ),
        ),
      ),
    );

    final indicator = find.byKey(const ValueKey('dev-performance-indicator'));
    final initialRect = tester.getRect(indicator);

    await tester.drag(indicator, const Offset(-120, 80));
    await tester.pump();

    final movedRect = tester.getRect(indicator);
    expect(movedRect.left, closeTo(initialRect.left - 120, 2));
    expect(movedRect.top, closeTo(initialRect.top + 80, 2));
  });

  testWidgets('dev overlay is hidden when disabled or empty', (tester) async {
    final disabled = PerformanceMonitorService(enabled: false);
    final empty = PerformanceMonitorService(enabled: true);

    await tester.pumpWidget(
      MaterialApp(
        home: DevPerformanceOverlay(
          monitor: disabled,
          child: const Text('App'),
        ),
      ),
    );

    expect(
      find.byKey(const ValueKey('dev-performance-indicator')),
      findsNothing,
    );
    expect(find.text('App'), findsOneWidget);

    await tester.pumpWidget(
      MaterialApp(
        home: DevPerformanceOverlay(monitor: empty, child: const Text('App')),
      ),
    );

    expect(
      find.byKey(const ValueKey('dev-performance-indicator')),
      findsNothing,
    );
  });
}
