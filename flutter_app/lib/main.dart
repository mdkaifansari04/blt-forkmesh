import 'dart:async';

import 'package:flutter/material.dart';
import 'package:provider/provider.dart';

import 'screens/home_shell.dart';
import 'services/api_service.dart';
import 'services/identity.dart';
import 'services/inbox_service.dart';
import 'services/relay_service.dart';
import 'services/settings_service.dart';
import 'theme.dart';

Future<void> main() async {
  WidgetsFlutterBinding.ensureInitialized();
  final settings = await SettingsService.create();
  final identity = await Identity.loadOrCreate();
  final relay = RelayService(settings, identity);
  final api = ApiService(settings);
  final inbox = InboxService(settings, identity);

  // Auto-join the relay on launch, like the Qt client.
  unawaited(relay.connect());

  runApp(ForkMeshApp(
    settings: settings,
    identity: identity,
    relay: relay,
    api: api,
    inbox: inbox,
  ));
}

class ForkMeshApp extends StatelessWidget {
  const ForkMeshApp({
    super.key,
    required this.settings,
    required this.identity,
    required this.relay,
    required this.api,
    required this.inbox,
  });

  final SettingsService settings;
  final Identity identity;
  final RelayService relay;
  final ApiService api;
  final InboxService inbox;

  @override
  Widget build(BuildContext context) {
    return MultiProvider(
      providers: [
        ChangeNotifierProvider.value(value: settings),
        Provider.value(value: identity),
        ChangeNotifierProvider.value(value: relay),
        Provider.value(value: api),
        Provider.value(value: inbox),
      ],
      child: MaterialApp(
        title: 'ForkMesh',
        debugShowCheckedModeBanner: false,
        theme: buildForkMeshTheme(),
        home: const HomeShell(),
      ),
    );
  }
}
