import 'package:flutter/material.dart';
import 'package:flutter/services.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:forkmesh/main.dart';
import 'package:forkmesh/models/models.dart';
import 'package:forkmesh/screens/auth_mock_flow.dart';
import 'package:forkmesh/services/api_service.dart';
import 'package:forkmesh/services/auth_service.dart';
import 'package:forkmesh/services/identity.dart';
import 'package:forkmesh/services/inbox_service.dart';
import 'package:forkmesh/services/relay_service.dart';
import 'package:forkmesh/services/settings_service.dart';
import 'package:forkmesh/theme.dart';
import 'package:forkmesh/widgets/connection_dot.dart';
import 'package:ming_cute_icons/ming_cute_icons.dart';
import 'package:shared_preferences/shared_preferences.dart';

class FakeApiService extends ApiService {
  FakeApiService(super.settings, {List<Repository> repositories = const []})
    : _repositories = repositories;

  final List<Repository> _repositories;

  @override
  Future<List<Repository>> repositories() async => _repositories;

  @override
  Future<NetworkStats> networkStats() async =>
      NetworkStats(nodesOnline: 2, hostsOnline: 1, repos: 3);
}

class SeededNotificationRelayService extends RelayService {
  SeededNotificationRelayService(
    super.settings,
    super.identity, {
    required List<ChatMessage> messages,
  }) : _messages = messages;

  final List<ChatMessage> _messages;
  final Set<String> _seenNotificationIds = {};

  @override
  List<ChatMessage> get unreadMessages => _messages
      .where((message) => !_seenNotificationIds.contains(message.id))
      .toList();

  @override
  List<ChatMessage> get notificationMessages => _messages;

  @override
  int get unseenNotificationCount => unreadMessages.length;

  @override
  void markNotificationsSeen() {
    _seenNotificationIds.addAll(_messages.map((message) => message.id));
    notifyListeners();
  }
}

void main() {
  void usePhoneView(WidgetTester tester, {Size size = const Size(430, 932)}) {
    tester.view.physicalSize = size;
    tester.view.devicePixelRatio = 1;
    addTearDown(tester.view.resetPhysicalSize);
    addTearDown(tester.view.resetDevicePixelRatio);
  }

  void usePhoneStatusPadding(WidgetTester tester) {
    tester.view.padding = const FakeViewPadding(top: 47, bottom: 34);
    tester.view.viewPadding = const FakeViewPadding(top: 47, bottom: 34);
    addTearDown(tester.view.resetPadding);
    addTearDown(tester.view.resetViewPadding);
  }

  Widget buildFlow({VoidCallback? onAuthenticated}) {
    return MaterialApp(
      home: AuthMockFlow(onAuthenticated: onAuthenticated ?? () {}),
    );
  }

  Future<void> tapFilledButton(WidgetTester tester, String label) async {
    final finder = find.widgetWithText(FilledButton, label);
    await tester.ensureVisible(finder);
    await tester.pumpAndSettle();
    await tester.tap(finder);
  }

  testWidgets('debug settings default to the local Worker relay URL', (
    tester,
  ) async {
    SharedPreferences.setMockInitialValues({});
    final settings = await SettingsService.create();

    expect(settings.serverUrl, contains(':8787/api/repo/mainnode/forkmesh'));
    expect(settings.serverUrl, startsWith('ws://'));
  });

  testWidgets('starts on the dark ForkMesh welcome screen', (tester) async {
    await tester.pumpWidget(buildFlow());

    expect(find.text('Build open, sync easy'), findsOneWidget);
    expect(
      find.text(
        'Private relay chat, repository mirrors, and signed collaboration from your phone.',
      ),
      findsOneWidget,
    );
    expect(find.widgetWithText(OutlinedButton, 'Log in'), findsOneWidget);
    expect(find.widgetWithText(FilledButton, 'Sign up'), findsOneWidget);
  });

  testWidgets('opens the login screen from the welcome screen', (tester) async {
    await tester.pumpWidget(buildFlow());

    await tester.tap(find.widgetWithText(OutlinedButton, 'Log in'));
    await tester.pumpAndSettle();

    expect(find.text('Login'), findsOneWidget);
    expect(find.text('Welcome back'), findsOneWidget);
    expect(find.text('Continue with your ForkMesh account.'), findsOneWidget);
    expect(find.byKey(const ValueKey('forkmesh-auth-logo')), findsWidgets);
    expect(find.text('ForkMesh'), findsNothing);
    expect(find.text('Email or node name'), findsOneWidget);
    expect(find.text('Password'), findsOneWidget);
    expect(find.widgetWithText(FilledButton, 'Continue'), findsOneWidget);
    expect(
      find.widgetWithText(TextButton, 'Create an account'),
      findsOneWidget,
    );

    final primaryFrame = tester.widget<SizedBox>(
      find.byKey(const ValueKey('primary-auth-button-frame')),
    );
    expect(primaryFrame.height, 48);
  });

  testWidgets('opens the signup screen from the welcome screen', (
    tester,
  ) async {
    await tester.pumpWidget(buildFlow());

    await tester.tap(find.widgetWithText(FilledButton, 'Sign up'));
    await tester.pumpAndSettle();

    expect(find.text('Sign up'), findsOneWidget);
    expect(find.text('Create your account'), findsOneWidget);
    expect(find.text('Set your ForkMesh profile to continue.'), findsOneWidget);
    expect(find.text('ForkMesh'), findsNothing);
    expect(find.text('Public node name'), findsOneWidget);
    expect(find.widgetWithText(FilledButton, 'Create account'), findsOneWidget);

    final primaryFrame = tester.widget<SizedBox>(
      find.byKey(const ValueKey('primary-auth-button-frame')),
    );
    expect(primaryFrame.height, 48);
  });

  testWidgets('mock login CTA calls onAuthenticated', (tester) async {
    var authenticated = false;
    await tester.pumpWidget(
      buildFlow(onAuthenticated: () => authenticated = true),
    );

    await tester.tap(find.widgetWithText(OutlinedButton, 'Log in'));
    await tester.pumpAndSettle();
    await tapFilledButton(tester, 'Continue');

    expect(authenticated, isTrue);
  });

  testWidgets('auth flow registers and renders the real ForkMesh logo asset', (
    tester,
  ) async {
    final logo = await rootBundle.load('assets/images/logo.png');
    expect(logo.lengthInBytes, greaterThan(0));

    await tester.pumpWidget(buildFlow());
    await tester.tap(find.widgetWithText(OutlinedButton, 'Log in'));
    await tester.pumpAndSettle();

    expect(find.byKey(const ValueKey('forkmesh-auth-logo')), findsWidgets);
  });

  testWidgets(
    'mock auth enters the refreshed shell with bottom navigation labels',
    (tester) async {
      SharedPreferences.setMockInitialValues({});
      final settings = await SettingsService.create();
      final identity = await Identity.loadOrCreate();
      final relay = RelayService(settings, identity);
      final api = FakeApiService(settings);
      final auth = await AuthService.create(settings, identity);
      final inbox = InboxService(settings, identity);

      await tester.pumpWidget(
        ForkMeshApp(
          settings: settings,
          identity: identity,
          relay: relay,
          api: api,
          auth: auth,
          inbox: inbox,
        ),
      );

      expect(find.text('Build open, sync easy'), findsOneWidget);

      await tester.tap(find.widgetWithText(OutlinedButton, 'Log in'));
      await tester.pumpAndSettle();
      await tapFilledButton(tester, 'Continue');
      await tester.pumpAndSettle();

      expect(find.text('Code'), findsWidgets);
      expect(find.text('Chat'), findsWidgets);
      expect(find.text('Activity'), findsWidgets);
      expect(find.text('Profile'), findsWidgets);
      expect(find.text('Tools'), findsWidgets);
    },
  );

  testWidgets('app shell follows system theme after mock auth', (tester) async {
    tester.platformDispatcher.platformBrightnessTestValue = Brightness.dark;
    addTearDown(tester.platformDispatcher.clearPlatformBrightnessTestValue);
    SharedPreferences.setMockInitialValues({});
    final settings = await SettingsService.create();
    final identity = await Identity.loadOrCreate();
    final relay = RelayService(settings, identity);
    final api = FakeApiService(settings);
    final auth = await AuthService.create(settings, identity);
    final inbox = InboxService(settings, identity);

    await tester.pumpWidget(
      ForkMeshApp(
        settings: settings,
        identity: identity,
        relay: relay,
        api: api,
        auth: auth,
        inbox: inbox,
      ),
    );

    await tester.tap(find.widgetWithText(OutlinedButton, 'Log in'));
    await tester.pumpAndSettle();
    await tapFilledButton(tester, 'Continue');
    await tester.pumpAndSettle();

    final theme = Theme.of(tester.element(find.text('ForkMesh')));
    final app = tester.widget<MaterialApp>(find.byType(MaterialApp));
    expect(app.themeMode, ThemeMode.system);
    expect(app.darkTheme, isNotNull);
    expect(theme.brightness, Brightness.dark);
    expect(theme.scaffoldBackgroundColor, const Color(0xFF0B0B0C));
  });

  testWidgets('repository metadata wraps without overflow on phone width', (
    tester,
  ) async {
    usePhoneView(tester);
    SharedPreferences.setMockInitialValues({});
    final settings = await SettingsService.create();
    final identity = await Identity.loadOrCreate();
    final relay = RelayService(settings, identity);
    final api = FakeApiService(
      settings,
      repositories: [
        Repository(
          owner: 'forkmesh',
          name: 'flutter_app',
          description: 'Mobile client refresh preview',
          language: 'TypeScript',
          stars: 4200,
          forks: 1700,
          mirrors: 328,
        ),
      ],
    );
    final auth = await AuthService.create(settings, identity);
    final inbox = InboxService(settings, identity);

    await tester.pumpWidget(
      ForkMeshApp(
        settings: settings,
        identity: identity,
        relay: relay,
        api: api,
        auth: auth,
        inbox: inbox,
      ),
    );

    await tester.tap(find.widgetWithText(OutlinedButton, 'Log in'));
    await tester.pumpAndSettle();
    await tapFilledButton(tester, 'Continue');
    await tester.pumpAndSettle();

    expect(find.text('forkmesh/flutter_app'), findsOneWidget);
    expect(find.byType(NavigationRail), findsNothing);
    expect(tester.takeException(), isNull);
  });

  testWidgets('mobile shell uses a layered bottom bar', (tester) async {
    usePhoneView(tester);
    usePhoneStatusPadding(tester);
    SharedPreferences.setMockInitialValues({});
    final settings = await SettingsService.create();
    final identity = await Identity.loadOrCreate();
    final relay = RelayService(settings, identity);
    final api = FakeApiService(settings);
    final auth = await AuthService.create(settings, identity);
    final inbox = InboxService(settings, identity);

    await tester.pumpWidget(
      ForkMeshApp(
        settings: settings,
        identity: identity,
        relay: relay,
        api: api,
        auth: auth,
        inbox: inbox,
      ),
    );

    await tester.tap(find.widgetWithText(OutlinedButton, 'Log in'));
    await tester.pumpAndSettle();
    await tapFilledButton(tester, 'Continue');
    await tester.pumpAndSettle();

    final nav = tester.widget<Container>(
      find.byKey(const ValueKey('compact-bottom-menu')),
    );
    final decoration = nav.decoration! as BoxDecoration;
    final content = tester.widget<SizedBox>(
      find.byKey(const ValueKey('compact-bottom-menu-content')),
    );
    final navBottom = tester
        .getBottomLeft(find.byKey(const ValueKey('compact-bottom-menu')))
        .dy;
    final contentBottom = tester
        .getBottomLeft(
          find.byKey(const ValueKey('compact-bottom-menu-content')),
        )
        .dy;

    final radius = decoration.borderRadius! as BorderRadius;

    expect(content.height, inInclusiveRange(56, 64));
    expect(navBottom - contentBottom, 14);
    expect(decoration.color, FmColors.bgRaised);
    expect(decoration.border, isNull);
    expect(radius.topLeft.x, FmRadius.lg);
    expect(radius.topRight.x, FmRadius.lg);
    expect(radius.bottomLeft.x, 0);
    expect(radius.bottomRight.x, 0);
    expect(find.byKey(const ValueKey('bottom-nav-selected-Code')), findsOne);
    expect(
      find.byKey(const ValueKey('bottom-nav-selected-bubble-Code')),
      findsNothing,
    );
    expect(find.byIcon(Icons.chat_bubble_outline_rounded), findsWidgets);
    expect(find.byIcon(Icons.chat_bubble_outline), findsNothing);
    expect(find.text('Code'), findsWidgets);
    expect(find.text('Chat'), findsWidgets);
    expect(find.text('Activity'), findsWidgets);
    expect(find.text('Profile'), findsWidgets);
    expect(find.text('Tools'), findsWidgets);
  });

  testWidgets('tools navigation opens the tools sheet', (tester) async {
    usePhoneView(tester);
    SharedPreferences.setMockInitialValues({});
    final settings = await SettingsService.create();
    final identity = await Identity.loadOrCreate();
    final relay = RelayService(settings, identity);
    final api = FakeApiService(settings);
    final auth = await AuthService.create(settings, identity);
    final inbox = InboxService(settings, identity);

    await tester.pumpWidget(
      ForkMeshApp(
        settings: settings,
        identity: identity,
        relay: relay,
        api: api,
        auth: auth,
        inbox: inbox,
      ),
    );

    await tester.tap(find.widgetWithText(OutlinedButton, 'Log in'));
    await tester.pumpAndSettle();
    await tapFilledButton(tester, 'Continue');
    await tester.pumpAndSettle();
    await tester.tap(find.text('Tools'));
    await tester.pumpAndSettle();

    expect(find.text('Tools'), findsWidgets);
    expect(find.text('Explorer'), findsOneWidget);
    expect(find.text('Search'), findsOneWidget);
    expect(find.text('Ports'), findsOneWidget);
    expect(find.text('Processes'), findsOneWidget);
    expect(find.text('API Client'), findsOneWidget);
    expect(find.text('Monitor'), findsOneWidget);
  });

  testWidgets('tools sheet is half height with calmer row typography', (
    tester,
  ) async {
    usePhoneView(tester);
    usePhoneStatusPadding(tester);
    SharedPreferences.setMockInitialValues({});
    final settings = await SettingsService.create();
    final identity = await Identity.loadOrCreate();
    final relay = RelayService(settings, identity);
    final api = FakeApiService(settings);
    final auth = await AuthService.create(settings, identity);
    final inbox = InboxService(settings, identity);

    await tester.pumpWidget(
      ForkMeshApp(
        settings: settings,
        identity: identity,
        relay: relay,
        api: api,
        auth: auth,
        inbox: inbox,
      ),
    );

    await tester.tap(find.widgetWithText(OutlinedButton, 'Log in'));
    await tester.pumpAndSettle();
    await tapFilledButton(tester, 'Continue');
    await tester.pumpAndSettle();
    await tester.tap(find.text('Tools'));
    await tester.pumpAndSettle();

    final sheet = find.byKey(const ValueKey('tools-sheet-panel'));
    expect(sheet, findsOneWidget);
    expect(
      tester.getTopLeft(sheet).dy,
      closeTo(tester.view.physicalSize.height * 0.5, 1),
    );
    expect(tester.getBottomLeft(sheet).dy, tester.view.physicalSize.height);

    final explorer = tester.widget<Text>(find.text('Explorer'));
    expect(explorer.style?.fontSize, 16);
    expect(explorer.style?.fontWeight, FontWeight.w600);
  });

  testWidgets('activity tab no longer contains the notifications section', (
    tester,
  ) async {
    usePhoneView(tester);
    SharedPreferences.setMockInitialValues({});
    final settings = await SettingsService.create();
    final identity = await Identity.loadOrCreate();
    final relay = RelayService(settings, identity);
    final api = FakeApiService(settings);
    final auth = await AuthService.create(settings, identity);
    final inbox = InboxService(settings, identity);

    await tester.pumpWidget(
      ForkMeshApp(
        settings: settings,
        identity: identity,
        relay: relay,
        api: api,
        auth: auth,
        inbox: inbox,
      ),
    );

    await tester.tap(find.widgetWithText(OutlinedButton, 'Log in'));
    await tester.pumpAndSettle();
    await tapFilledButton(tester, 'Continue');
    await tester.pumpAndSettle();
    await tester.tap(find.text('Activity'));
    await tester.pumpAndSettle();

    expect(find.text('NETWORK'), findsOneWidget);
    expect(find.text('ACTIVITY LOG'), findsOneWidget);
    expect(find.text('NOTIFICATIONS'), findsNothing);
  });

  testWidgets('top bar bell opens the dedicated notifications page', (
    tester,
  ) async {
    usePhoneView(tester);
    SharedPreferences.setMockInitialValues({});
    final settings = await SettingsService.create();
    final identity = await Identity.loadOrCreate();
    final relay = RelayService(settings, identity);
    final api = FakeApiService(settings);
    final auth = await AuthService.create(settings, identity);
    final inbox = InboxService(settings, identity);

    await tester.pumpWidget(
      ForkMeshApp(
        settings: settings,
        identity: identity,
        relay: relay,
        api: api,
        auth: auth,
        inbox: inbox,
      ),
    );

    await tester.tap(find.widgetWithText(OutlinedButton, 'Log in'));
    await tester.pumpAndSettle();
    await tapFilledButton(tester, 'Continue');
    await tester.pumpAndSettle();

    final notificationButton = find.byKey(
      const ValueKey('top-notifications-button'),
    );

    expect(notificationButton, findsOneWidget);
    expect(find.byIcon(MingCuteIcons.mgc_notification_line), findsOneWidget);
    expect(
      tester.getCenter(notificationButton).dx,
      lessThan(tester.getCenter(find.byType(AvatarWithDot)).dx),
    );

    await tester.tap(notificationButton);
    await tester.pumpAndSettle();

    expect(find.text('Notifications'), findsOneWidget);
    expect(find.text('Inbox'), findsOneWidget);
    expect(find.text('Channels'), findsOneWidget);
    expect(find.text('Direct'), findsOneWidget);
    expect(find.text('Attachments'), findsOneWidget);
    expect(find.text('All'), findsNothing);
    expect(find.text('Reminders'), findsNothing);
    expect(find.text('Payment'), findsNothing);
    expect(find.text('Booking'), findsNothing);
    expect(find.byIcon(Icons.hexagon_outlined), findsNothing);
    expect(find.text('Activity log'), findsNothing);
  });

  testWidgets('viewing notifications clears the top bar alert', (tester) async {
    usePhoneView(tester);
    SharedPreferences.setMockInitialValues({});
    final settings = await SettingsService.create();
    final identity = await Identity.loadOrCreate();
    final relay = SeededNotificationRelayService(
      settings,
      identity,
      messages: [
        ChatMessage(
          id: 'n-1',
          conversation: '#general',
          senderId: 'remote-node',
          senderName: 'Rinkit',
          text: 'ForkMesh mirror finished syncing flutter_app on main.',
          timestamp: DateTime(2026, 7, 3, 9, 41),
        ),
      ],
    );
    final api = FakeApiService(settings);
    final auth = await AuthService.create(settings, identity);
    final inbox = InboxService(settings, identity);

    await tester.pumpWidget(
      ForkMeshApp(
        settings: settings,
        identity: identity,
        relay: relay,
        api: api,
        auth: auth,
        inbox: inbox,
      ),
    );

    await tester.tap(find.widgetWithText(OutlinedButton, 'Log in'));
    await tester.pumpAndSettle();
    await tapFilledButton(tester, 'Continue');
    await tester.pumpAndSettle();

    final notificationButton = find.byKey(
      const ValueKey('top-notifications-button'),
    );

    expect(
      find.descendant(of: notificationButton, matching: find.byType(Badge)),
      findsOneWidget,
    );

    await tester.tap(notificationButton);
    await tester.pumpAndSettle();

    expect(
      find.text('ForkMesh mirror finished syncing flutter_app on main.'),
      findsOneWidget,
    );

    await tester.tap(find.byTooltip('Back'));
    await tester.pumpAndSettle();

    expect(
      find.descendant(of: notificationButton, matching: find.byType(Badge)),
      findsNothing,
    );
  });

  testWidgets('mobile shell keeps the top bar below status indicators', (
    tester,
  ) async {
    usePhoneView(tester);
    usePhoneStatusPadding(tester);
    SharedPreferences.setMockInitialValues({});
    final settings = await SettingsService.create();
    final identity = await Identity.loadOrCreate();
    final relay = RelayService(settings, identity);
    final api = FakeApiService(settings);
    final auth = await AuthService.create(settings, identity);
    final inbox = InboxService(settings, identity);

    await tester.pumpWidget(
      ForkMeshApp(
        settings: settings,
        identity: identity,
        relay: relay,
        api: api,
        auth: auth,
        inbox: inbox,
      ),
    );

    await tester.tap(find.widgetWithText(OutlinedButton, 'Log in'));
    await tester.pumpAndSettle();
    await tapFilledButton(tester, 'Continue');
    await tester.pumpAndSettle();

    final topBarOffset = tester.getTopLeft(
      find.byKey(const ValueKey('home-top-bar')),
    );

    expect(topBarOffset.dy, greaterThanOrEqualTo(47));
    expect(tester.takeException(), isNull);
  });

  testWidgets('settings actions wrap without overflow on compact phone width', (
    tester,
  ) async {
    usePhoneView(tester, size: const Size(340, 852));
    SharedPreferences.setMockInitialValues({});
    final settings = await SettingsService.create();
    final identity = await Identity.loadOrCreate();
    final relay = RelayService(settings, identity);
    final api = FakeApiService(settings);
    final auth = await AuthService.create(settings, identity);
    final inbox = InboxService(settings, identity);

    await tester.pumpWidget(
      ForkMeshApp(
        settings: settings,
        identity: identity,
        relay: relay,
        api: api,
        auth: auth,
        inbox: inbox,
      ),
    );

    await tester.tap(find.widgetWithText(OutlinedButton, 'Log in'));
    await tester.pumpAndSettle();
    await tapFilledButton(tester, 'Continue');
    await tester.pumpAndSettle();
    await tester.tap(find.text('Profile'));
    await tester.pumpAndSettle();

    expect(tester.takeException(), isNull);
  });
}
