import 'dart:convert';
import 'dart:io';

import 'package:flutter_test/flutter_test.dart';
import 'package:forkmesh/services/api_service.dart';
import 'package:forkmesh/services/settings_service.dart';
import 'package:shared_preferences/shared_preferences.dart';

void main() {
  test('repositories retries after a failed catalog load', () async {
    var requestCount = 0;
    final server = await HttpServer.bind(InternetAddress.loopbackIPv4, 0);
    final subscription = server.listen((request) async {
      requestCount += 1;
      expect(request.uri.path, '/api/repositories');

      if (requestCount <= 2) {
        request.response.statusCode = HttpStatus.internalServerError;
        request.response.write('temporary failure');
      } else {
        request.response.headers.contentType = ContentType.json;
        request.response.write(
          jsonEncode({
            'ok': true,
            'repositories': [
              {'owner': 'owner', 'name': 'forkmesh'},
            ],
          }),
        );
      }

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

    await expectLater(api.repositories(), throwsException);
    expect(requestCount, 2);

    final repos = await api.repositories();

    expect(requestCount, 3);
    expect(repos.single.fullName, 'owner/forkmesh');
  });
}
