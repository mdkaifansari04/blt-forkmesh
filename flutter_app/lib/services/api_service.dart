import 'dart:convert';

import 'package:http/http.dart' as http;

import '../models/models.dart';
import 'settings_service.dart';

/// Read access to the relay/worker REST API (the same endpoints the Qt client
/// and the website use): the public catalog, per-repo issues/pulls/commits, and
/// network stats. Derives the https host from the configured relay URL.
class ApiService {
  ApiService(this._settings);

  final SettingsService _settings;

  Uri _base(String path) {
    final ws = Uri.parse(_settings.serverUrl);
    final scheme = ws.scheme == 'ws' ? 'http' : 'https';
    return Uri(scheme: scheme, host: ws.host, port: ws.hasPort ? ws.port : null, path: path);
  }

  Future<List<Repository>> repositories() async {
    final data = await _getJson(_base('/api/repositories'));
    final list = _asList(data);
    return list
        .whereType<Map<String, dynamic>>()
        .map(Repository.fromJson)
        .where((r) => r.owner.isNotEmpty && r.name.isNotEmpty)
        .toList();
  }

  Future<NetworkStats> networkStats() async {
    try {
      final data = await _getJson(_base('/api/network/stats'));
      if (data is Map<String, dynamic>) return NetworkStats.fromJson(data);
    } catch (_) {}
    return NetworkStats();
  }

  Future<List<Issue>> issues(String owner, String name) async {
    final data = await _getJson(_base('/api/repo/$owner/$name/issues'));
    return _asList(data)
        .whereType<Map<String, dynamic>>()
        .map(Issue.fromJson)
        .toList();
  }

  Future<List<PullRequest>> pulls(String owner, String name) async {
    final data = await _getJson(_base('/api/repo/$owner/$name/pulls'));
    return _asList(data)
        .whereType<Map<String, dynamic>>()
        .map(PullRequest.fromJson)
        .toList();
  }

  Future<List<Map<String, dynamic>>> commits(String owner, String name) async {
    final data = await _getJson(_base('/api/repo/$owner/$name/commits'));
    return _asList(data).whereType<Map<String, dynamic>>().toList();
  }

  // Catalog endpoints sometimes wrap the array in {repositories:[...]} / {data:[...]}.
  List<dynamic> _asList(dynamic data) {
    if (data is List) return data;
    if (data is Map<String, dynamic>) {
      for (final key in ['repositories', 'issues', 'pulls', 'commits', 'data', 'items']) {
        if (data[key] is List) return data[key] as List;
      }
    }
    return const [];
  }

  Future<dynamic> _getJson(Uri uri) async {
    final resp = await http
        .get(uri, headers: {'Accept': 'application/json'})
        .timeout(const Duration(seconds: 20));
    if (resp.statusCode >= 200 && resp.statusCode < 300) {
      if (resp.body.isEmpty) return const [];
      return jsonDecode(resp.body);
    }
    throw Exception('HTTP ${resp.statusCode} for ${uri.path}');
  }
}
