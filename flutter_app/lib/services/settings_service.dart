import 'package:flutter/foundation.dart';
import 'package:shared_preferences/shared_preferences.dart';

/// Persisted user/app settings, mirroring the Qt client's QSettings keys we care
/// about: display name, relay URL/room/passphrase, and import tokens.
class SettingsService extends ChangeNotifier {
  SettingsService(this._prefs);

  final SharedPreferences _prefs;

  static const productionServerUrl =
      'wss://forkmesh.com/api/repo/mainnode/forkmesh/rooms/general/ws';
  static const localWorkerServerUrl =
      'ws://localhost:8787/api/repo/mainnode/forkmesh/rooms/general/ws';
  static const androidEmulatorLocalWorkerServerUrl =
      'ws://10.0.2.2:8787/api/repo/mainnode/forkmesh/rooms/general/ws';
  static const configuredServerUrl = String.fromEnvironment(
    'FORKMESH_SERVER_URL',
    defaultValue: '',
  );

  static String get defaultServerUrl {
    if (configuredServerUrl.isNotEmpty) return configuredServerUrl;
    if (kReleaseMode) return productionServerUrl;
    if (defaultTargetPlatform == TargetPlatform.android) {
      return androidEmulatorLocalWorkerServerUrl;
    }
    return localWorkerServerUrl;
  }

  static const defaultRoom = 'general';
  static const defaultPassphrase = '';
  static const legacyPublicRoomPassphrase = 'forkmesh-public-room';

  static Future<SettingsService> create() async =>
      SettingsService(await SharedPreferences.getInstance());

  String get displayName => _prefs.getString('profile/name') ?? '';
  String get serverUrl => _prefs.getString('server/url') ?? defaultServerUrl;
  String get room => _prefs.getString('server/room') ?? defaultRoom;
  String get passphrase {
    final stored = _prefs.getString('server/passphrase');
    // Earlier Flutter builds saved this legacy value, which made mobile derive a
    // different room key from Qt's passphrase-free public room. Treat it as empty
    // so chat interoperates with desktop nodes and retained Worker history.
    if (stored == legacyPublicRoomPassphrase) return '';
    return stored ?? defaultPassphrase;
  }

  String get solanaAddress => _prefs.getString('profile/solana') ?? '';
  String get githubToken => _prefs.getString('import/githubToken') ?? '';
  String get gitlabToken => _prefs.getString('import/gitlabToken') ?? '';

  Future<void> setDisplayName(String v) => _set('profile/name', v);
  Future<void> setServerUrl(String v) => _set('server/url', v);
  Future<void> setRoom(String v) => _set('server/room', v);
  Future<void> setPassphrase(String v) async {
    final clean = v == legacyPublicRoomPassphrase ? '' : v;
    await _set('server/passphrase', clean);
  }

  Future<void> setSolanaAddress(String v) => _set('profile/solana', v);
  Future<void> setGithubToken(String v) => _set('import/githubToken', v);
  Future<void> setGitlabToken(String v) => _set('import/gitlabToken', v);

  Future<void> _set(String key, String value) async {
    await _prefs.setString(key, value);
    notifyListeners();
  }
}
