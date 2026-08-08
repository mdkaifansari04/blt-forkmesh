import 'package:flutter_test/flutter_test.dart';
import 'package:forkmesh/services/settings_service.dart';

void main() {
  test('world URL keeps only the configured relay origin', () {
    expect(
      SettingsService.worldUriForServerUrl(
        'wss://relay.example.test:9443/api/repo/private/name/rooms/main/ws'
        '?ticket=never-copy',
      ).toString(),
      'https://relay.example.test:9443/',
    );
    expect(
      SettingsService.worldUriForServerUrl(
        'ws://127.0.0.1:8787/api/repo/mainnode/forkmesh/rooms/general/ws',
      ).toString(),
      'http://127.0.0.1:8787/',
    );
  });

  test('credential-bearing and invalid relay values fail closed', () {
    expect(
      SettingsService.worldUriForServerUrl(
        'wss://user:secret@relay.example.test/api/world/ws',
      ).toString(),
      'https://world.forkmesh.com/',
    );
    expect(
      SettingsService.worldUriForServerUrl('file:///tmp/private').toString(),
      'https://world.forkmesh.com/',
    );
  });

  test('canonical app and retired room defaults migrate', () {
    expect(
      SettingsService.canonicalServerUrl(
        'wss://forkmesh.com/api/repo/mainnode/forkmesh/rooms/general/ws',
      ),
      SettingsService.productionServerUrl,
    );
    expect(
      SettingsService.canonicalServerUrl(
        'wss://api.forkmesh.com/api/repo/mainnode/forkmesh/rooms/world-general/ws',
      ),
      SettingsService.productionServerUrl,
    );
    expect(
      SettingsService.worldUriForServerUrl(
        SettingsService.productionServerUrl,
      ).toString(),
      'https://world.forkmesh.com/',
    );
  });
}
