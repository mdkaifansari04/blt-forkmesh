import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:forkmesh/models/models.dart';
import 'package:forkmesh/screens/orgs_screen.dart';
import 'package:forkmesh/services/api_service.dart';
import 'package:forkmesh/services/auth_service.dart';
import 'package:forkmesh/services/identity.dart';
import 'package:forkmesh/services/settings_service.dart';
import 'package:forkmesh/theme.dart';
import 'package:provider/provider.dart';
import 'package:shared_preferences/shared_preferences.dart';

class _TestAuthService extends AuthService {
  _TestAuthService(super.settings, super.identity, super.prefs, this._session);

  final AuthSession? _session;

  @override
  AuthSession? get session => _session;

  @override
  bool get isAuthenticated => _session != null;
}



class _FakeApiService extends ApiService {
  _FakeApiService(super.settings, this.orgs);

  final List<OrgSummary> orgs;

  @override
  Future<List<OrgSummary>> myOrgs() async => orgs;
}

Future<Widget> _app({
  required List<OrgSummary> orgs,
  required AuthSession? session,
}) async {
  SharedPreferences.setMockInitialValues({});
  final settings = await SettingsService.create();
  final identity = await Identity.loadOrCreate();
  final api = _FakeApiService(settings, orgs);
  final auth = _TestAuthService(
    settings,
    identity,
    await SharedPreferences.getInstance(),
    session,
  );
  return MultiProvider(
    providers: [
      Provider<ApiService>.value(value: api),
      ChangeNotifierProvider<AuthService>.value(value: auth),
    ],
    child: MaterialApp(
      theme: buildForkMeshLightTheme(),
      home: const OrgsScreen(),
    ),
  );
}

AuthSession _session() => AuthSession.fromJson({
  'nodeName': 'alice',
  'email': 'alice@example.com',
  'status': 'active',
});

void main() {
  testWidgets('lists the account orgs with role badges', (tester) async {
    tester.view.physicalSize = const Size(430, 932);
    tester.view.devicePixelRatio = 1;
    addTearDown(tester.view.resetPhysicalSize);
    addTearDown(tester.view.resetDevicePixelRatio);

    await tester.pumpWidget(
      await _app(
        orgs: const [
          OrgSummary(name: 'acme', role: 'owner'),
          OrgSummary(name: 'globex', role: 'member'),
        ],
        session: _session(),
      ),
    );
    await tester.pumpAndSettle();

    expect(find.text('Organizations'), findsOneWidget);
    expect(find.text('acme'), findsOneWidget);
    expect(find.text('globex'), findsOneWidget);
    expect(find.text('OWNER'), findsOneWidget);
    expect(find.text('MEMBER'), findsOneWidget);

    expect(find.byKey(const ValueKey('org-create-button')), findsOneWidget);
  });

  testWidgets('prompts sign-in when there is no account session', (
    tester,
  ) async {
    tester.view.physicalSize = const Size(430, 932);
    tester.view.devicePixelRatio = 1;
    addTearDown(tester.view.resetPhysicalSize);
    addTearDown(tester.view.resetDevicePixelRatio);

    await tester.pumpWidget(await _app(orgs: const [], session: null));
    await tester.pumpAndSettle();

    expect(find.text('Sign in to manage organizations'), findsOneWidget);
    expect(find.byKey(const ValueKey('org-create-button')), findsNothing);
  });
}
