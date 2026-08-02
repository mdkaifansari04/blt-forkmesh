import 'dart:convert';
import 'dart:io';

import 'package:flutter_test/flutter_test.dart';
import 'package:forkmesh/models/models.dart';
import 'package:forkmesh/services/api_service.dart';
import 'package:forkmesh/services/settings_service.dart';
import 'package:shared_preferences/shared_preferences.dart';

void main() {
  test('FederatedReply preserves lifecycle and fails closed as non-native', () {
    final reply = FederatedReply.fromJson({
      'remoteId': 'https://lemmy.example/comment/7',
      'parentRemoteId': 'https://lemmy.example/comment/6',
      'author': 'alice@lemmy.example',
      'body': 'remote body',
      'url': 'https://lemmy.example/comment/7',
      'sourceInstance': 'lemmy.example',
      'sourceSoftware': 'lemmy',
      'lifecycle': 'edited',
      'depth': 2,
      // A compromised or old server response cannot promote this projection.
      'nativeEvent': true,
    });

    expect(reply.remoteId, 'https://lemmy.example/comment/7');
    expect(reply.parentRemoteId, 'https://lemmy.example/comment/6');
    expect(reply.edited, isTrue);
    expect(reply.depth, 2);
    expect(reply.nativeEvent, isFalse);
  });

  test('tombstones and moderation never expose a projected body', () {
    final tombstone = FederatedReply.fromJson({
      'remoteId': 'https://lemmy.example/comment/8',
      'body': '',
      'lifecycle': 'tombstoned',
    });
    final moderated = FederatedReply.fromJson({
      'remoteId': 'https://lemmy.example/comment/9',
      'body': '',
      'lifecycle': 'moderated',
    });

    expect(tombstone.displayBody, 'Deleted on the remote instance');
    expect(moderated.displayBody, 'Hidden by remote moderation');
  });

  test('ApiService requests the dedicated federated projection', () async {
    late Uri requested;
    final server = await HttpServer.bind(InternetAddress.loopbackIPv4, 0);
    final subscription = server.listen((request) async {
      requested = request.uri;
      request.response.headers.contentType = ContentType.json;
      request.response.write(
        jsonEncode({
          'ok': true,
          'comments': [
            {
              'remoteId': 'https://lemmy.example/comment/10',
              'author': 'bob@lemmy.example',
              'body': 'hello from Lemmy',
              'url': 'https://lemmy.example/comment/10',
              'sourceInstance': 'lemmy.example',
              'sourceSoftware': 'lemmy',
              'lifecycle': 'active',
              'nativeEvent': false,
            },
          ],
        }),
      );
      await request.response.close();
    });
    addTearDown(() async {
      await subscription.cancel();
      await server.close(force: true);
    });

    SharedPreferences.setMockInitialValues({});
    final settings = await SettingsService.create();
    await settings.setServerUrl(
      'ws://${server.address.host}:${server.port}/ws',
    );
    final api = ApiService(settings);

    final replies = await api.federatedReplies(
      'forkmesh',
      'forkmesh',
      kind: 'discussion',
      number: 9,
    );

    expect(requested.path, '/api/repo/forkmesh/forkmesh/fedi-comments');
    expect(requested.queryParameters['kind'], 'discussion');
    expect(requested.queryParameters['number'], '9');
    expect(replies.single.body, 'hello from Lemmy');
    expect(replies.single.nativeEvent, isFalse);
  });
}
