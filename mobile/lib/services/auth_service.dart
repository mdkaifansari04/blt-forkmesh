import 'dart:async';
import 'dart:convert';

import 'package:flutter/foundation.dart';
import 'package:http/http.dart' as http;
import 'package:shared_preferences/shared_preferences.dart';

import 'identity.dart';
import 'performance_monitor_service.dart';
import 'settings_service.dart';

class AuthSession {
  const AuthSession({
    required this.nodeName,
    required this.email,
    required this.status,
    required this.pubkey,
    required this.emailVerified,
    required this.isAdmin,
    required this.solana,
    required this.hasPayoutAddress,
    required this.avatarPng,
    required this.avatarUpdatedAt,
    required this.createdAt,
    required this.savedAt,
    this.sessionKind = 'account',
    this.deviceKind = '',
    this.desktopCapable = false,
    this.capabilities = const [],
    this.devices = const [],
    this.sessionToken = '',
  });

  final String nodeName;
  final String email;
  final String status;
  final String pubkey;
  final bool emailVerified;
  final bool isAdmin;
  final String solana;
  final bool hasPayoutAddress;
  final String avatarPng;
  final int avatarUpdatedAt;
  final int createdAt;
  final int savedAt;
  final String sessionKind;
  final String deviceKind;
  final bool desktopCapable;
  final List<String> capabilities;
  final List<AccountDevice> devices;
  // Signed account session token returned by the Worker on login/signup. Sent
  // as an `Authorization: Bearer` header so per-account endpoints (notifications,
  // agent list/prompt, repo metadata) authorize this account rather than a
  // self-asserted node name. Persisted with the rest of the session.
  final String sessionToken;

  bool get canSubmitIssue => capabilities.contains('submit_issue');
  bool get canSubmitPr => capabilities.contains('submit_pr');
  bool get canHost => capabilities.contains('host_repo');
  bool get canPublish => capabilities.contains('publish_repo');
  List<AccountDevice> get desktopDevices => devices
      .where((device) => device.kind == 'desktop_node' && device.enabled)
      .toList();

  factory AuthSession.fromJson(Map<String, dynamic> json) => AuthSession(
    nodeName: '${json['nodeName'] ?? json['name'] ?? ''}',
    email: '${json['email'] ?? ''}',
    status: '${json['status'] ?? ''}',
    pubkey: '${json['pubkey'] ?? ''}',
    emailVerified: json['emailVerified'] == true,
    isAdmin: json['isAdmin'] == true,
    solana: '${json['solana'] ?? ''}',
    hasPayoutAddress: json['hasPayoutAddress'] == true,
    avatarPng: '${json['avatarPng'] ?? ''}',
    avatarUpdatedAt: _asInt(json['avatarUpdatedAt']),
    createdAt: _asInt(json['createdAt']),
    savedAt: _asInt(json['at'] ?? DateTime.now().millisecondsSinceEpoch),
    sessionKind: '${json['sessionKind'] ?? 'account'}',
    deviceKind: '${json['deviceKind'] ?? ''}',
    desktopCapable: json['desktopCapable'] == true,
    capabilities: (json['capabilities'] is List)
        ? (json['capabilities'] as List).map((e) => '$e').toList()
        : const [],
    devices: (json['devices'] is List)
        ? (json['devices'] as List)
              .whereType<Map>()
              .map(
                (item) =>
                    AccountDevice.fromJson(Map<String, dynamic>.from(item)),
              )
              .toList()
        : const [],
    sessionToken: '${json['sessionToken'] ?? ''}',
  );

  Map<String, dynamic> toJson() => {
    'nodeName': nodeName,
    'email': email,
    'status': status,
    'pubkey': pubkey,
    'emailVerified': emailVerified,
    'isAdmin': isAdmin,
    'solana': solana,
    'hasPayoutAddress': hasPayoutAddress,
    'avatarPng': avatarPng,
    'avatarUpdatedAt': avatarUpdatedAt,
    'createdAt': createdAt,
    'at': savedAt,
    'sessionKind': sessionKind,
    'deviceKind': deviceKind,
    'desktopCapable': desktopCapable,
    'capabilities': capabilities,
    'devices': devices.map((device) => device.toJson()).toList(),
    'sessionToken': sessionToken,
  };

  static int _asInt(dynamic value) {
    if (value is int) return value;
    if (value is num) return value.toInt();
    return int.tryParse('$value') ?? 0;
  }
}

class AccountDevice {
  const AccountDevice({
    required this.id,
    required this.pubkey,
    required this.kind,
    required this.label,
    required this.capabilities,
    required this.enabled,
    required this.lastSeen,
  });

  final String id;
  final String pubkey;
  final String kind;
  final String label;
  final List<String> capabilities;
  final bool enabled;
  final int lastSeen;

  bool get canOwnerSign => capabilities.contains('owner_sign');
  bool get canHost => capabilities.contains('host_repo');
  bool get canPublish => capabilities.contains('publish_repo');

  String get displayName => label.trim().isNotEmpty
      ? label.trim()
      : kind == 'desktop_node'
      ? 'Desktop node'
      : 'Mobile client';

  String get kindLabel => switch (kind) {
    'desktop_node' => 'Desktop node',
    'mobile_client' => 'Mobile client',
    _ => 'Device',
  };

  String get statusLabel => !enabled
      ? 'Disabled'
      : canOwnerSign
      ? 'Ready for signed controls'
      : 'Browse only';

  String get capabilityLabel {
    final labels = <String>[];
    if (canHost) labels.add('Host repo');
    if (canPublish) labels.add('Publish repo');
    if (canOwnerSign) labels.add('Owner signing');
    if (labels.isEmpty) labels.addAll(capabilities.map(_capabilityTitle));
    return labels.isEmpty ? 'No capabilities' : labels.join(' · ');
  }

  Map<String, dynamic> toJson() => {
    'id': id,
    'pubkey': pubkey,
    'kind': kind,
    'label': label,
    'capabilities': capabilities,
    'enabled': enabled,
    'lastSeen': lastSeen,
  };

  factory AccountDevice.fromJson(Map<String, dynamic> json) => AccountDevice(
    id: '${json['id'] ?? ''}',
    pubkey: '${json['pubkey'] ?? ''}',
    kind: '${json['kind'] ?? ''}',
    label: '${json['label'] ?? ''}',
    capabilities: (json['capabilities'] is List)
        ? (json['capabilities'] as List).map((e) => '$e').toList()
        : const [],
    enabled: json['enabled'] != false,
    lastSeen: AuthSession._asInt(json['lastSeen'] ?? json['last_seen']),
  );

  static String _capabilityTitle(String raw) {
    final words = raw.split('_').where((word) => word.isNotEmpty).toList();
    if (words.isEmpty) return raw;
    return words
        .map((word) => word[0].toUpperCase() + word.substring(1))
        .join(' ');
  }
}

class AccountAvailability {
  const AccountAvailability({
    required this.ok,
    required this.exists,
    required this.available,
    required this.name,
    this.message = '',
  });

  final bool ok;
  final bool exists;
  final bool available;
  final String name;
  final String message;
}

class AuthException implements Exception {
  const AuthException(this.code, this.message);
  final String code;
  final String message;

  @override
  String toString() => message;
}

class AuthService extends ChangeNotifier {
  AuthService(
    this._settings,
    this._identity,
    this._prefs, {
    PerformanceMonitorService? performanceMonitor,
  }) : _performanceMonitor = performanceMonitor {
    _session = _loadSession();
  }

  final SettingsService _settings;
  final Identity _identity;
  final SharedPreferences _prefs;
  final PerformanceMonitorService? _performanceMonitor;

  static const _sessionKey = 'auth/session';
  static final nodeNamePattern = RegExp(r'^[a-z](?:[a-z0-9-]{0,61}[a-z0-9])?$');

  AuthSession? _session;
  AuthSession? get session => _session;
  bool get isAuthenticated => _session != null;

  static Future<AuthService> create(
    SettingsService settings,
    Identity identity, {
    PerformanceMonitorService? performanceMonitor,
  }) async => AuthService(
    settings,
    identity,
    await SharedPreferences.getInstance(),
    performanceMonitor: performanceMonitor,
  );

  Uri _base(String path) {
    final ws = Uri.parse(_settings.serverUrl);
    final scheme = ws.scheme == 'ws' ? 'http' : 'https';
    return Uri(
      scheme: scheme,
      host: ws.host,
      port: ws.hasPort ? ws.port : null,
      path: path,
    );
  }

  Future<AuthSession> login({
    required String identifier,
    required String password,
    String totp = '',
  }) async {
    final body = await _postJson('/api/accounts/login', {
      'identifier': identifier.trim().toLowerCase(),
      'email': identifier.trim().toLowerCase(),
      'password': password,
      'totp': totp.trim(),
      // Do not send this mobile device's local Ed25519 key here. The Worker
      // treats a pubkey on password login as the desktop/node key and binds it
      // to the account if one is not already present. Mobile is a web-style
      // client for the central Worker, not a repo-hosting desktop node, so
      // binding this key would block the real Qt client from logging in later.
    });
    return _saveSession(AuthSession.fromJson(body));
  }

  Future<AuthSession> signup({
    required String nodeName,
    required String email,
    required String password,
  }) async {
    final body = await _postJson('/api/accounts/signup', {
      'nodeName': nodeName.trim().toLowerCase(),
      'email': email.trim().toLowerCase(),
      'password': password,
    });
    return _saveSession(AuthSession.fromJson(body));
  }

  Future<AccountAvailability> checkNodeName(String nodeName) async {
    final clean = nodeName.trim().toLowerCase();
    if (!nodeNamePattern.hasMatch(clean)) {
      return AccountAvailability(
        ok: false,
        exists: false,
        available: false,
        name: clean,
        message:
            'Use lowercase letters, numbers and hyphens. Start with a letter and end with a letter or number.',
      );
    }
    try {
      final data = await _getJson('/api/accounts/$clean');
      return AccountAvailability(
        ok: true,
        exists: data['exists'] == true,
        available: data['available'] != false,
        name: '${data['name'] ?? clean}',
        message: data['available'] == false
            ? 'That name is already taken.'
            : '“$clean” is available.',
      );
    } catch (_) {
      return AccountAvailability(
        ok: false,
        exists: false,
        available: true,
        name: clean,
        message:
            'Could not check availability. You can still try creating the account.',
      );
    }
  }

  Future<void> authenticatePreview() async {
    if (_session != null && _session!.sessionKind != 'preview') return;
    await _saveSession(
      AuthSession(
        nodeName: 'preview-node',
        email: 'preview@forkmesh.local',
        status: 'active',
        pubkey: _identity.publicKeyB64url,
        emailVerified: true,
        isAdmin: false,
        solana: '',
        hasPayoutAddress: false,
        avatarPng: '',
        avatarUpdatedAt: 0,
        createdAt: DateTime.now().millisecondsSinceEpoch,
        savedAt: DateTime.now().millisecondsSinceEpoch,
        sessionKind: 'preview',
        deviceKind: 'mobile',
        capabilities: const ['browse', 'comment', 'submit_issue', 'submit_pr'],
      ),
    );
  }

  Future<void> logout() async {
    // Fire-and-forget server-side logout — the same /api/accounts/logout every
    // web client uses. On web builds the browser holds the HttpOnly
    // forkmesh_admin cookie, and only the Worker can clear it; without this an
    // admin stayed able to open the admin page after logging out here.
    unawaited(
      _postJson(
        '/api/accounts/logout',
        {},
      ).catchError((_) => const <String, dynamic>{}),
    );
    await _prefs.remove(_sessionKey);
    _session = null;
    notifyListeners();
  }

  AuthSession? _loadSession() {
    final raw = _prefs.getString(_sessionKey);
    if (raw == null || raw.isEmpty) return null;
    try {
      final json = jsonDecode(raw);
      if (json is Map<String, dynamic>) {
        final session = AuthSession.fromJson(json);
        if (session.sessionKind == 'preview') {
          unawaited(_prefs.remove(_sessionKey));
          return null;
        }
        if (session.nodeName.isNotEmpty || session.email.isNotEmpty) {
          return session;
        }
      }
    } catch (_) {}
    return null;
  }

  Future<AuthSession> _saveSession(AuthSession session) async {
    await _prefs.setString(_sessionKey, jsonEncode(session.toJson()));
    if (session.nodeName.isNotEmpty) {
      await _settings.setDisplayName(session.nodeName);
    }
    if (session.solana.isNotEmpty) {
      await _settings.setSolanaAddress(session.solana);
    }
    _session = session;
    notifyListeners();
    return session;
  }

  Future<Map<String, dynamic>> _getJson(String path) async {
    Future<Map<String, dynamic>> load() async {
      final resp = await http
          .get(_base(path), headers: {'accept': 'application/json'})
          .timeout(const Duration(seconds: 20));
      return _decodeResponse(resp);
    }

    final monitor = _performanceMonitor;
    if (monitor == null) return load();
    return monitor.track('auth.GET $path', load, details: {'path': path});
  }

  Future<Map<String, dynamic>> _postJson(
    String path,
    Map<String, dynamic> payload,
  ) async {
    Future<Map<String, dynamic>> load() async {
      final resp = await http
          .post(
            _base(path),
            headers: {
              'content-type': 'application/json',
              'accept': 'application/json',
            },
            body: jsonEncode(payload),
          )
          .timeout(const Duration(seconds: 20));
      return _decodeResponse(resp);
    }

    final monitor = _performanceMonitor;
    if (monitor == null) return load();
    return monitor.track('auth.POST $path', load, details: {'path': path});
  }

  Map<String, dynamic> _decodeResponse(http.Response resp) {
    Map<String, dynamic> body = {};
    try {
      final decoded = jsonDecode(resp.body);
      if (decoded is Map<String, dynamic>) body = decoded;
    } catch (_) {}
    if (resp.statusCode >= 200 && resp.statusCode < 300) return body;
    final code = '${body['error'] ?? 'http_${resp.statusCode}'}';
    throw AuthException(code, _messageFor(code));
  }

  String _messageFor(String code) => switch (code) {
    'bad_totp' => 'Enter your authenticator code.',
    'too_many_attempts' =>
      'Too many failed attempts. Wait a few minutes and try again.',
    'account_disabled' => 'This account has been disabled.',
    'invalid_credentials' => 'Incorrect email, username, or password.',
    'pubkey_mismatch' => 'This account is linked to a different device key.',
    'invalid_node_name' => 'Choose a valid username.',
    'valid_email_required' => 'Enter a valid email address.',
    'password_too_short' => 'Password must be at least 8 characters.',
    'node_name_taken' => 'That username is already taken.',
    'email_taken' => 'That email is already registered.',
    'invalid_json' => 'Could not send the request. Please try again.',
    _ => 'Could not complete the request. Please try again.',
  };
}
