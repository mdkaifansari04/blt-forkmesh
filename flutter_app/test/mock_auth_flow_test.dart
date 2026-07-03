import 'package:flutter/material.dart';
import 'package:flutter/services.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:provider/provider.dart';
import 'package:forkmesh/main.dart';
import 'package:forkmesh/models/models.dart';
import 'package:forkmesh/screens/auth_mock_flow.dart';
import 'package:forkmesh/services/api_service.dart';
import 'package:forkmesh/services/auth_service.dart';
import 'package:forkmesh/services/identity.dart';
import 'package:forkmesh/services/inbox_service.dart';
import 'package:forkmesh/services/relay_service.dart';
import 'package:forkmesh/services/settings_service.dart';
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

// The login form now performs a real Worker /api/accounts/login round trip
// (pull #4), so widget tests authenticate through this offline stand-in: a
// successful login flips isAuthenticated and notifies, which is what
// ForkMeshApp's Consumer<AuthService> watches to enter the home shell.
class FakeAuthService extends AuthService {
  FakeAuthService(super.settings, super.identity, super.prefs);

  static Future<FakeAuthService> create(
    SettingsService settings,
    Identity identity,
  ) async => FakeAuthService(
    settings,
    identity,
    await SharedPreferences.getInstance(),
  );

  AuthSession? _fakeSession;

  @override
  AuthSession? get session => _fakeSession;

  @override
  bool get isAuthenticated => _fakeSession != null;

  @override
  Future<AuthSession> login({
    required String identifier,
    required String password,
    String totp = '',
  }) async {
    _fakeSession = AuthSession.fromJson({
      'nodeName': 'demo-node',
      'email': 'demo@example.com',
      'status': 'active',
      'emailVerified': true,
      'capabilities': ['submit_issue', 'submit_pr'],
    });
    notifyListeners();
    return _fakeSession!;
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

  Widget buildFlow({VoidCallback? onAuthenticated, AuthService? auth}) {
    final flow = MaterialApp(
      home: AuthMockFlow(onAuthenticated: onAuthenticated ?? () {}),
    );
    if (auth == null) return flow;
    return ChangeNotifierProvider<AuthService>.value(value: auth, child: flow);
  }

  Future<void> tapFilledButton(WidgetTester tester, String label) async {
    final finder = find.widgetWithText(FilledButton, label);
    await tester.ensureVisible(finder);
    await tester.pumpAndSettle();
    await tester.tap(finder);
  }

  // Walks the real login form: credentials are required now, and Continue
  // resolves through the (fake) AuthService before onAuthenticated fires.
  Future<void> submitLogin(WidgetTester tester) async {
    await tester.tap(find.widgetWithText(OutlinedButton, 'Log in'));
    await tester.pumpAndSettle();
    await tester.enterText(
      find.widgetWithText(TextField, 'you@example.com'),
      'demo@example.com',
    );
    await tester.enterText(
      find.widgetWithText(TextField, 'Your password'),
      'correct-horse',
    );
    await tapFilledButton(tester, 'Continue');
    await tester.pumpAndSettle();
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

  testWidgets('opens the light login screen from the welcome screen', (
    tester,
  ) async {
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

  testWidgets('opens the light signup screen from the welcome screen', (
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
    SharedPreferences.setMockInitialValues({});
    final settings = await SettingsService.create();
    final identity = await Identity.loadOrCreate();
    final auth = await FakeAuthService.create(settings, identity);

    var authenticated = false;
    await tester.pumpWidget(
      buildFlow(onAuthenticated: () => authenticated = true, auth: auth),
    );

    // Continue with empty fields never authenticates — the form asks for
    // credentials instead of falling through as the old mock flow did.
    await tester.tap(find.widgetWithText(OutlinedButton, 'Log in'));
    await tester.pumpAndSettle();
    await tapFilledButton(tester, 'Continue');
    await tester.pumpAndSettle();
    expect(authenticated, isFalse);
    expect(
      find.text('Enter your email or node name and password.'),
      findsOneWidget,
    );

    await tester.enterText(
      find.widgetWithText(TextField, 'you@example.com'),
      'demo@example.com',
    );
    await tester.enterText(
      find.widgetWithText(TextField, 'Your password'),
      'correct-horse',
    );
    await tapFilledButton(tester, 'Continue');
    await tester.pumpAndSettle();

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
      final auth = await FakeAuthService.create(settings, identity);
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

      await submitLogin(tester);

      expect(find.text('Code'), findsWidgets);
      expect(find.text('Chat'), findsWidgets);
      expect(find.text('Activity'), findsWidgets);
      expect(find.text('Profile'), findsWidgets);
    },
  );

  testWidgets('app shell uses the light professional theme after mock auth', (
    tester,
  ) async {
    SharedPreferences.setMockInitialValues({});
    final settings = await SettingsService.create();
    final identity = await Identity.loadOrCreate();
    final relay = RelayService(settings, identity);
    final api = FakeApiService(settings);
    final auth = await FakeAuthService.create(settings, identity);
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

    await submitLogin(tester);

    final theme = Theme.of(tester.element(find.text('ForkMesh')));
    expect(theme.brightness, Brightness.light);
    expect(theme.scaffoldBackgroundColor, const Color(0xFFF6F7F9));
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
    final auth = await FakeAuthService.create(settings, identity);
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

    await submitLogin(tester);

    expect(find.text('forkmesh/flutter_app'), findsOneWidget);
    expect(find.byType(NavigationRail), findsNothing);
    expect(tester.takeException(), isNull);
  });

  testWidgets('mobile shell uses a compact white bottom menu', (tester) async {
    usePhoneView(tester);
    usePhoneStatusPadding(tester);
    SharedPreferences.setMockInitialValues({});
    final settings = await SettingsService.create();
    final identity = await Identity.loadOrCreate();
    final relay = RelayService(settings, identity);
    final api = FakeApiService(settings);
    final auth = await FakeAuthService.create(settings, identity);
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

    await submitLogin(tester);

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

    final border = decoration.border! as Border;
    final radius = decoration.borderRadius! as BorderRadius;

    expect(content.height, 58);
    expect(navBottom - contentBottom, 14);
    expect(decoration.color, Colors.white);
    expect(border.top.color, const Color(0xFFE8E6E2));
    expect(radius.topLeft.x, 28);
    expect(radius.topRight.x, 28);
    expect(radius.bottomLeft.x, 0);
    expect(radius.bottomRight.x, 0);
    expect(find.byIcon(Icons.chat_bubble_outline_rounded), findsWidgets);
    expect(find.byIcon(Icons.chat_bubble_outline), findsNothing);
    expect(find.text('Code'), findsWidgets);
    expect(find.text('Chat'), findsWidgets);
    expect(find.text('Activity'), findsWidgets);
    expect(find.text('Profile'), findsWidgets);
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
    final auth = await FakeAuthService.create(settings, identity);
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

    await submitLogin(tester);

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
    final auth = await FakeAuthService.create(settings, identity);
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

    await submitLogin(tester);
    await tester.tap(find.text('Profile'));
    await tester.pumpAndSettle();

    expect(tester.takeException(), isNull);
  });
}
