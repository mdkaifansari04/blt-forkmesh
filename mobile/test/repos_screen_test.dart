import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:forkmesh/models/models.dart';
import 'package:forkmesh/screens/repos_screen.dart';
import 'package:forkmesh/services/api_service.dart';
import 'package:forkmesh/services/settings_service.dart';
import 'package:forkmesh/theme.dart';
import 'package:provider/provider.dart';
import 'package:shared_preferences/shared_preferences.dart';

class RefreshingApiService extends ApiService {
  RefreshingApiService(super.settings);

  int calls = 0;

  @override
  Future<List<Repository>> repositories() async {
    calls += 1;
    return [Repository(owner: 'owner', name: 'repo$calls')];
  }
}

class CatalogApiService extends ApiService {
  CatalogApiService(super.settings);

  @override
  Future<List<Repository>> repositories() async {
    return [
      Repository(
        owner: 'beta',
        name: 'relay',
        description: 'Mainnode catalog relay',
        language: 'Python',
        mirrors: 1,
        isPrivate: true,
        updatedMs: 1000,
      ),
      Repository(
        owner: 'alpha',
        name: 'mobile',
        description: 'Phone command center',
        language: 'Dart',
        mirrors: 8,
        cloneOnline: true,
        updatedMs: 2000,
      ),
      Repository(
        owner: 'gamma',
        name: 'desktop',
        description: 'Qt canonical host',
        language: 'C++',
        mirrors: 3,
        liveHost: true,
        updatedMs: 3000,
      ),
    ];
  }
}

Future<CatalogApiService> _pumpCatalog(WidgetTester tester) async {
  SharedPreferences.setMockInitialValues({});
  final settings = await SettingsService.create();
  final api = CatalogApiService(settings);

  await tester.pumpWidget(
    Provider<ApiService>.value(
      value: api,
      child: MaterialApp(
        theme: buildForkMeshLightTheme(),
        home: const Scaffold(body: ReposScreen()),
      ),
    ),
  );
  await tester.pumpAndSettle();

  return api;
}

void main() {
  testWidgets('refresh reloads repositories without setState async errors', (
    tester,
  ) async {
    SharedPreferences.setMockInitialValues({});
    final settings = await SettingsService.create();
    final api = RefreshingApiService(settings);

    await tester.pumpWidget(
      Provider<ApiService>.value(
        value: api,
        child: MaterialApp(
          theme: buildForkMeshLightTheme(),
          home: const Scaffold(body: ReposScreen()),
        ),
      ),
    );
    await tester.pumpAndSettle();

    expect(find.text('owner/repo1'), findsOneWidget);

    await tester.tap(find.byTooltip('Refresh repositories'));
    await tester.pumpAndSettle();

    expect(tester.takeException(), isNull);
    expect(api.calls, 2);
    expect(find.text('owner/repo2'), findsOneWidget);
  });

  testWidgets('search matches repository description and language', (
    tester,
  ) async {
    await _pumpCatalog(tester);

    await tester.enterText(find.byType(TextField), 'mainnode');
    await tester.pumpAndSettle();

    expect(find.text('beta/relay'), findsOneWidget);
    expect(find.text('alpha/mobile'), findsNothing);
    expect(find.text('gamma/desktop'), findsNothing);

    await tester.enterText(find.byType(TextField), 'dart');
    await tester.pumpAndSettle();

    expect(find.text('alpha/mobile'), findsOneWidget);
    expect(find.text('beta/relay'), findsNothing);
    expect(find.text('gamma/desktop'), findsNothing);
  });

  testWidgets('filters and sort controls refine the mobile catalog', (
    tester,
  ) async {
    await _pumpCatalog(tester);

    expect(
      tester.getTopLeft(find.text('gamma/desktop')).dy,
      lessThan(tester.getTopLeft(find.text('alpha/mobile')).dy),
    );
    expect(
      tester.getTopLeft(find.text('alpha/mobile')).dy,
      lessThan(tester.getTopLeft(find.text('beta/relay')).dy),
    );
    expect(find.text('Dart'), findsOneWidget);
    expect(find.text('8 mirrors'), findsOneWidget);

    await tester.tap(find.widgetWithText(ChoiceChip, 'Private'));
    await tester.pumpAndSettle();

    expect(find.text('beta/relay'), findsOneWidget);
    expect(find.text('alpha/mobile'), findsNothing);
    expect(find.text('gamma/desktop'), findsNothing);

    await tester.tap(find.widgetWithText(ChoiceChip, 'Online'));
    await tester.pumpAndSettle();

    expect(find.text('alpha/mobile'), findsOneWidget);
    expect(find.text('gamma/desktop'), findsOneWidget);
    expect(find.text('beta/relay'), findsNothing);

    await tester.tap(find.byTooltip('Sort repositories'));
    await tester.pumpAndSettle();
    await tester.tap(find.text('Mirrors high-low').last);
    await tester.pumpAndSettle();

    expect(
      tester.getTopLeft(find.text('alpha/mobile')).dy,
      lessThan(tester.getTopLeft(find.text('gamma/desktop')).dy),
    );
  });
}
