import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:forkmesh/models/models.dart';
import 'package:forkmesh/screens/notifications_screen.dart';
import 'package:forkmesh/services/api_service.dart';
import 'package:forkmesh/services/notification_service.dart';
import 'package:forkmesh/services/settings_service.dart';
import 'package:provider/provider.dart';
import 'package:shared_preferences/shared_preferences.dart';

class NotificationApiService extends ApiService {
  NotificationApiService(super.settings);

  String requestedNode = '';
  int fetchCount = 0;
  int markAllCount = 0;
  List<String> markedIds = const [];
  List<ForkNotification> items = [
    ForkNotification(
      id: 'n1',
      kind: 'mention',
      title: 'You were mentioned in mona/forkmesh',
      body: 'Please review @mona before the desktop owner applies it.',
      repo: 'mona/forkmesh',
      href: '/mona/forkmesh/issues/12',
      actor: 'kai',
      source: 'issue',
      ts: 1770000000000,
    ),
    ForkNotification(
      id: 'n2',
      kind: 'host_offline',
      title: 'Host offline for mona/forkmesh',
      body: 'No live desktop host has checked in recently.',
      repo: 'mona/forkmesh',
      source: 'host',
      ts: 1769990000000,
      readAt: 1769995000000,
    ),
  ];

  @override
  Future<NotificationPage> notifications(String node, {int limit = 40}) async {
    requestedNode = node;
    fetchCount += 1;
    return NotificationPage(
      notifications: items,
      unread: items.where((item) => item.isUnread).length,
    );
  }

  @override
  Future<void> markNotificationsRead(
    String node, {
    List<String> ids = const [],
    bool all = false,
  }) async {
    requestedNode = node;
    if (all) markAllCount += 1;
    markedIds = ids;
    items = [for (final item in items) item.copyWith(readAt: 1770001000000)];
  }

  @override
  Future<List<Issue>> publishedIssues(String owner, String name) async => [
    Issue(
      number: 12,
      title: 'Stabilize signed inbox',
      body: 'Open the exact issue detail from a notification.',
      author: 'kai',
      labels: const ['mobile'],
    ),
  ];
}

Future<NotificationApiService> _pumpNotifications(WidgetTester tester) async {
  SharedPreferences.setMockInitialValues({});
  final settings = await SettingsService.create();
  final api = NotificationApiService(settings);
  final service = NotificationService(api, nodeName: () => 'mona');
  addTearDown(service.dispose);

  await tester.pumpWidget(
    MultiProvider(
      providers: [
        Provider<ApiService>.value(value: api),
        ChangeNotifierProvider<NotificationService>.value(value: service),
      ],
      child: const MaterialApp(home: NotificationsScreen()),
    ),
  );
  await tester.pumpAndSettle();
  return api;
}

void main() {
  testWidgets('notifications screen reads worker notifications by node', (
    tester,
  ) async {
    final api = await _pumpNotifications(tester);

    expect(api.requestedNode, 'mona');
    expect(find.text('Notifications'), findsOneWidget);
    expect(find.text('1 unread'), findsOneWidget);
    expect(find.text('You were mentioned in mona/forkmesh'), findsOneWidget);
    expect(find.text('Host offline for mona/forkmesh'), findsOneWidget);
    expect(find.text('mona/forkmesh'), findsWidgets);
    expect(find.text('Mention'), findsOneWidget);
    expect(find.text('Host'), findsOneWidget);
  });

  testWidgets('notifications screen filters unread and marks all read', (
    tester,
  ) async {
    final api = await _pumpNotifications(tester);

    await tester.tap(find.text('Unread'));
    await tester.pumpAndSettle();

    expect(find.text('You were mentioned in mona/forkmesh'), findsOneWidget);
    expect(find.text('Host offline for mona/forkmesh'), findsNothing);

    await tester.tap(find.widgetWithText(TextButton, 'Mark all read'));
    await tester.pumpAndSettle();

    expect(api.markAllCount, 1);
    expect(find.text('All caught up'), findsOneWidget);
    expect(find.text('0 unread'), findsOneWidget);
  });

  testWidgets('tapping an issue notification opens the exact issue detail', (
    tester,
  ) async {
    final api = await _pumpNotifications(tester);

    await tester.tap(find.text('You were mentioned in mona/forkmesh'));
    await tester.pumpAndSettle();

    expect(api.markedIds, ['n1']);
    expect(find.text('#12'), findsWidgets);
    expect(find.text('Stabilize signed inbox'), findsOneWidget);
    expect(find.text('Comment / vote / status'), findsOneWidget);
    expect(find.text('Open context: /mona/forkmesh/issues/12'), findsNothing);
  });
}
