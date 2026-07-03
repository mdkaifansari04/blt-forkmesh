import 'dart:async';
import 'dart:convert';
import 'dart:io';

import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:forkmesh/screens/chat_screen.dart';
import 'package:forkmesh/services/identity.dart';
import 'package:forkmesh/services/relay_service.dart';
import 'package:forkmesh/services/room_crypto.dart';
import 'package:forkmesh/services/settings_service.dart';
import 'package:forkmesh/theme.dart';
import 'package:provider/provider.dart';
import 'package:shared_preferences/shared_preferences.dart';

void main() {
  TestWidgetsFlutterBinding.ensureInitialized();

  testWidgets('send button shows a new chat message without a refresh', (
    tester,
  ) async {
    final harness = (await tester.runAsync(_RelayHarness.start))!;
    addTearDown(() async => tester.runAsync(harness.close));

    await tester.pumpWidget(
      ChangeNotifierProvider<RelayService>.value(
        value: harness.relay,
        child: MaterialApp(
          theme: buildForkMeshLightTheme(),
          home: const Scaffold(body: ChatScreen()),
        ),
      ),
    );

    await tester.enterText(find.byType(TextField), 'first live message');
    await tester.tap(find.byIcon(Icons.send));
    await tester.pump();

    expect(find.text('first live message'), findsOneWidget);
  });

  testWidgets('chat sends a durable encrypted WebSocket frame', (tester) async {
    await tester.runAsync(() async {
      final harness = await _RelayHarness.start();
      addTearDown(harness.close);

      await harness.relay.sendMessage('retain this message');
      await harness.waitForFrameCount(2);

      final chatFrame = harness.frames
          .map((frame) => jsonDecode(frame) as Map<String, dynamic>)
          .lastWhere((frame) => frame['persist'] == true);

      expect(chatFrame['kind'], 'cipher');
      expect(chatFrame['persist'], isTrue);
    });
  });

  testWidgets('direct messages are not retained by the relay', (tester) async {
    await tester.runAsync(() async {
      final harness = await _RelayHarness.start();
      addTearDown(harness.close);

      harness.frames.clear();
      harness.relay.switchConversation('@remote-node');
      await harness.relay.sendMessage('private direct message');
      await harness.waitForFrameCount(1);

      final dmFrame = jsonDecode(harness.frames.last) as Map<String, dynamic>;

      expect(dmFrame['kind'], 'cipher');
      expect(dmFrame.containsKey('persist'), isFalse);
    });
  });

  testWidgets('retained and live messages render in chronological order', (
    tester,
  ) async {
    await tester.runAsync(() async {
      final harness = await _RelayHarness.start();
      addTearDown(harness.close);

      await harness.sendPlain({
        'type': 'chat',
        'id': 'remote-newer',
        'senderId': 'remote-node',
        'sender': 'Remote',
        'ts': 2000,
        'channel': '#general',
        'text': 'newer remote message',
      });
      await harness.sendPlain({
        'type': 'chat',
        'id': 'remote-older',
        'senderId': 'remote-node',
        'sender': 'Remote',
        'ts': 1000,
        'channel': '#general',
        'text': 'older remote message',
      });
      await _waitUntil(() => harness.relay.messages('#general').length == 2);

      expect(
        harness.relay.messages('#general').map((message) => message.text),
        ['older remote message', 'newer remote message'],
      );
    });
  });
}

Future<void> _waitUntil(bool Function() condition) async {
  final deadline = DateTime.now().add(const Duration(seconds: 2));
  while (!condition() && DateTime.now().isBefore(deadline)) {
    await Future<void>.delayed(const Duration(milliseconds: 10));
  }
}

class _RelayHarness {
  _RelayHarness._({
    required this.server,
    required this.relay,
    required this.crypto,
    required this.frames,
    required WebSocket socket,
    required Future<void> serverLoop,
  }) : _socket = socket,
       _serverLoop = serverLoop;

  final HttpServer server;
  final RelayService relay;
  final RoomCrypto crypto;
  final WebSocket _socket;
  final Future<void> _serverLoop;
  final List<String> frames;

  static Future<_RelayHarness> start() async {
    SharedPreferences.setMockInitialValues({});
    final server = await HttpServer.bind(InternetAddress.loopbackIPv4, 0);
    final socketCompleter = Completer<WebSocket>();
    final frames = <String>[];

    final serverLoop = () async {
      await for (final request in server) {
        if (!WebSocketTransformer.isUpgradeRequest(request)) {
          request.response.statusCode = HttpStatus.notFound;
          await request.response.close();
          continue;
        }

        final socket = await WebSocketTransformer.upgrade(request);
        if (!socketCompleter.isCompleted) {
          socketCompleter.complete(socket);
        }
        socket.listen((data) {
          if (data is String) {
            frames.add(data);
          }
        });
      }
    }();

    final settings = await SettingsService.create();
    await settings.setDisplayName('Mobile Tester');
    await settings.setServerUrl(
      'ws://${server.address.host}:${server.port}/ws',
    );
    final identity = await Identity.loadOrCreate();
    final relay = RelayService(settings, identity);
    await relay.connect().timeout(
      const Duration(seconds: 5),
      onTimeout: () => throw TimeoutException('relay.connect timed out'),
    );
    final socket = await socketCompleter.future.timeout(
      const Duration(seconds: 5),
      onTimeout: () =>
          throw TimeoutException('test WebSocket was not accepted'),
    );
    final crypto = await RoomCrypto.shared(settings.room);

    final harness = _RelayHarness._(
      server: server,
      relay: relay,
      crypto: crypto,
      frames: frames,
      socket: socket,
      serverLoop: serverLoop,
    );
    return harness;
  }

  Future<void> sendPlain(Map<String, dynamic> plain) async {
    final envelope = await crypto.encrypt(plain);
    _socket.add(jsonEncode(envelope));
  }

  Future<void> waitForFrameCount(int count) async {
    final deadline = DateTime.now().add(const Duration(seconds: 2));
    while (frames.length < count && DateTime.now().isBefore(deadline)) {
      await Future<void>.delayed(const Duration(milliseconds: 10));
    }
  }

  Future<void> close() async {
    relay.dispose();
    await _socket.close();
    await server.close(force: true);
    try {
      await _serverLoop;
    } catch (_) {}
  }
}
