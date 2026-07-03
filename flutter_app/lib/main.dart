import 'dart:async';

import 'package:flutter/material.dart';
import 'package:provider/provider.dart';

import 'screens/home_shell.dart';
import 'services/api_service.dart';
import 'services/auth_service.dart';
import 'services/identity.dart';
import 'services/inbox_service.dart';
import 'services/performance_monitor_service.dart';
import 'services/relay_service.dart';
import 'services/settings_service.dart';
import 'screens/auth_mock_flow.dart';
import 'theme.dart';
import 'widgets/dev_performance_overlay.dart';

Future<void> main() async {
  WidgetsFlutterBinding.ensureInitialized();
  final performanceMonitor = PerformanceMonitorService();
  final settings = await performanceMonitor.track(
    'startup.settings',
    SettingsService.create,
  );
  final identity = await performanceMonitor.track(
    'startup.identity',
    Identity.loadOrCreate,
  );
  final relay = RelayService(
    settings,
    identity,
    performanceMonitor: performanceMonitor,
  );
  final api = ApiService(settings, performanceMonitor: performanceMonitor);
  final auth = await performanceMonitor.track(
    'startup.auth',
    () => AuthService.create(
      settings,
      identity,
      performanceMonitor: performanceMonitor,
    ),
  );
  final inbox = InboxService(
    settings,
    identity,
    performanceMonitor: performanceMonitor,
  );

  // Auto-join the relay on launch, like the Qt client.
  unawaited(relay.connect());

  runApp(
    ForkMeshApp(
      settings: settings,
      identity: identity,
      relay: relay,
      api: api,
      auth: auth,
      inbox: inbox,
      performanceMonitor: performanceMonitor,
    ),
  );
}

class ForkMeshApp extends StatefulWidget {
  const ForkMeshApp({
    super.key,
    required this.settings,
    required this.identity,
    required this.relay,
    required this.api,
    required this.auth,
    required this.inbox,
    this.performanceMonitor,
  });

  final SettingsService settings;
  final Identity identity;
  final RelayService relay;
  final ApiService api;
  final AuthService auth;
  final InboxService inbox;
  final PerformanceMonitorService? performanceMonitor;

  @override
  State<ForkMeshApp> createState() => _ForkMeshAppState();
}

class _ForkMeshAppState extends State<ForkMeshApp> {
  late final PerformanceMonitorService _performanceMonitor =
      widget.performanceMonitor ?? PerformanceMonitorService(enabled: false);

  @override
  void initState() {
    super.initState();
    _performanceMonitor.installFrameTimingMonitor(WidgetsBinding.instance);
  }

  @override
  void dispose() {
    _performanceMonitor.uninstallFrameTimingMonitor(WidgetsBinding.instance);
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    return MultiProvider(
      providers: [
        ChangeNotifierProvider.value(value: widget.settings),
        Provider.value(value: widget.identity),
        ChangeNotifierProvider.value(value: widget.relay),
        Provider.value(value: widget.api),
        ChangeNotifierProvider.value(value: widget.auth),
        Provider.value(value: widget.inbox),
      ],
      child: MaterialApp(
        title: 'ForkMesh',
        debugShowCheckedModeBanner: false,
        theme: buildForkMeshLightTheme(),
        darkTheme: buildForkMeshDarkTheme(),
        themeMode: ThemeMode.system,
        builder: (context, child) {
          final content = child ?? const SizedBox.shrink();
          if (!_performanceMonitor.enabled) return content;
          return DevPerformanceOverlay(
            monitor: _performanceMonitor,
            child: content,
          );
        },
        home: Consumer<AuthService>(
          builder: (context, auth, _) => auth.isAuthenticated
              ? const HomeShell()
              : AuthMockFlow(
                  onAuthenticated: () {
                    // Real login/signup writes AuthService.session before this
                    // callback runs. Only create the preview session for the
                    // empty-field design-preview path so it cannot overwrite a
                    // valid Worker account on the profile page.
                    if (!auth.isAuthenticated) {
                      auth.authenticatePreview();
                    }
                  },
                ),
        ),
      ),
    );
  }
}
