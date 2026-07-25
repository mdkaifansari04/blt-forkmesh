import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:forkmesh/screens/settings_screen.dart';
import 'package:forkmesh/services/auth_service.dart';
import 'package:forkmesh/services/identity.dart';
import 'package:forkmesh/services/relay_service.dart';
import 'package:forkmesh/services/settings_service.dart';
import 'package:forkmesh/theme.dart';
import 'package:provider/provider.dart';
import 'package:shared_preferences/shared_preferences.dart';

class TestAuthService extends AuthService {
  TestAuthService(super.settings, super.identity, super.prefs, this._session);

  final AuthSession? _session;

  @override
  AuthSession? get session => _session;

  @override
  bool get isAuthenticated => _session != null;
}

AuthSession _sessionWithDevices() => AuthSession.fromJson({
  'nodeName': 'alice',
  'email': 'alice@example.com',
  'status': 'active',
  'pubkey': 'desktop-primary-key',
  'emailVerified': true,
  'solana': 'alice-public-solana',
  'hasPayoutAddress': true,
  'devices': [
    {
      'id': 'dev-1',
      'pubkey': 'desktop-primary-key',
      'kind': 'desktop_node',
      'label': 'MacBook Pro',
      'capabilities': ['host_repo', 'publish_repo', 'owner_sign'],
      'enabled': true,
      'lastSeen': 1782999999000,
    },
    {
      'id': 'dev-2',
      'pubkey': 'phone-key',
      'kind': 'mobile_client',
      'label': 'iPhone',
      'capabilities': ['browse', 'comment'],
      'enabled': true,
    },
  ],
});

void main() {
  test('AuthSession parses desktop device inventory from login payload', () {
    final session = _sessionWithDevices();

    expect(session.devices, hasLength(2));
    expect(session.desktopDevices.single.label, 'MacBook Pro');
    expect(session.desktopDevices.single.canOwnerSign, isTrue);
    expect(
      session.desktopDevices.single.capabilityLabel,
      contains('Host repo'),
    );
  });

  testWidgets(
    'settings shows desktop node pairing inventory and command lock copy',
    (tester) async {
      tester.view.physicalSize = const Size(430, 932);
      tester.view.devicePixelRatio = 1;
      addTearDown(tester.view.resetPhysicalSize);
      addTearDown(tester.view.resetDevicePixelRatio);

      SharedPreferences.setMockInitialValues({});
      final settings = await SettingsService.create();
      final identity = await Identity.loadOrCreate();
      final relay = RelayService(settings, identity);
      final auth = TestAuthService(
        settings,
        identity,
        await SharedPreferences.getInstance(),
        _sessionWithDevices(),
      );

      await tester.pumpWidget(
        MultiProvider(
          providers: [
            ChangeNotifierProvider<SettingsService>.value(value: settings),
            Provider<Identity>.value(value: identity),
            ChangeNotifierProvider<RelayService>.value(value: relay),
            ChangeNotifierProvider<AuthService>.value(value: auth),
          ],
          child: MaterialApp(
            theme: buildForkMeshLightTheme(),
            home: const Scaffold(body: SettingsScreen()),
          ),
        ),
      );

      expect(find.text('DESKTOP NODES'), findsOneWidget);
      expect(find.text('MacBook Pro'), findsOneWidget);
      expect(find.text('READY FOR SIGNED CONTROLS'), findsOneWidget);
      expect(
        find.textContaining(
          'Remote commands stay locked until a desktop approval flow is added.',
        ),
        findsOneWidget,
      );
    },
  );

  testWidgets('settings shows funding payout card and public address warning', (
    tester,
  ) async {
    tester.view.physicalSize = const Size(430, 932);
    tester.view.devicePixelRatio = 1;
    addTearDown(tester.view.resetPhysicalSize);
    addTearDown(tester.view.resetDevicePixelRatio);

    SharedPreferences.setMockInitialValues({});
    final settings = await SettingsService.create();
    final identity = await Identity.loadOrCreate();
    final relay = RelayService(settings, identity);
    final auth = TestAuthService(
      settings,
      identity,
      await SharedPreferences.getInstance(),
      _sessionWithDevices(),
    );

    await tester.pumpWidget(
      MultiProvider(
        providers: [
          ChangeNotifierProvider<SettingsService>.value(value: settings),
          Provider<Identity>.value(value: identity),
          ChangeNotifierProvider<RelayService>.value(value: relay),
          ChangeNotifierProvider<AuthService>.value(value: auth),
        ],
        child: MaterialApp(
          theme: buildForkMeshLightTheme(),
          home: const Scaffold(body: SettingsScreen()),
        ),
      ),
    );

    expect(find.text('FUNDING AND PAYOUTS'), findsOneWidget);
    expect(find.text('Payout address connected'), findsOneWidget);
    expect(find.text('alice-public-solana'), findsOneWidget);
    expect(
      find.textContaining('never fund one or paste a private key'),
      findsOneWidget,
    );
    expect(
      find.textContaining('ForkMesh saves only this public payout address'),
      findsOneWidget,
    );
    expect(
      find.textContaining(
        'Legacy Worker bounty wallets are frozen for offline migration',
      ),
      findsOneWidget,
    );
  });
}
