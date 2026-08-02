import 'dart:convert';
import 'dart:io';

import 'package:flutter_test/flutter_test.dart';
import 'package:forkmesh/models/models.dart';
import 'package:forkmesh/services/api_service.dart';
import 'package:forkmesh/services/settings_service.dart';
import 'package:shared_preferences/shared_preferences.dart';

/// Serves canned JSON for the org endpoints and records each request so the
/// tests can assert method + path + body, mirroring api_service_test.dart.
class _OrgServer {
  _OrgServer(this.server);

  final HttpServer server;
  final List<({String method, String path, dynamic body})> requests = [];

  ({int status, Object body}) Function(
    ({String method, String path, dynamic body}) req,
  )?
  handler;

  static Future<_OrgServer> start(
    ({int status, Object body}) Function(
      ({String method, String path, dynamic body}) req,
    )
    handler,
  ) async {
    final http = await HttpServer.bind(InternetAddress.loopbackIPv4, 0);
    final server = _OrgServer(http)..handler = handler;
    http.listen((request) async {
      final raw = await utf8.decoder.bind(request).join();
      final body = raw.isEmpty ? null : jsonDecode(raw);
      final req = (method: request.method, path: request.uri.path, body: body);
      server.requests.add(req);
      final result = server.handler!(req);
      request.response.statusCode = result.status;
      request.response.headers.contentType = ContentType.json;
      request.response.write(jsonEncode(result.body));
      await request.response.close();
    });
    return server;
  }

  Future<ApiService> api() async {
    SharedPreferences.setMockInitialValues({});
    final settings = await SettingsService.create();
    await settings.setServerUrl(
      'ws://${server.address.host}:${server.port}/ws',
    );
    return ApiService(settings);
  }

  Future<void> close() => server.close(force: true);
}

void main() {
  test('OrgSummary/OrgProfile/OrgTeam/OrgRepo parse from JSON', () {
    final summary = OrgSummary.fromJson({'name': 'acme', 'role': 'OWNER'});
    expect(summary.name, 'acme');
    expect(summary.role, 'owner');
    expect(summary.canManage, isTrue);

    final profile = OrgProfile.fromJson({
      'org': 'acme',
      'displayName': 'Acme Inc',
      'description': 'We make things',
      'createdAt': 1700000000000,
      'members': 3,
      'teams': 2,
      'viewerRole': 'admin',
      'repos': [
        {'repo': 'widget', 'node': 'alice'},
        {'repo': '', 'node': 'ignored'},
      ],
    });
    expect(profile.title, 'Acme Inc');
    expect(profile.canManage, isTrue);
    expect(profile.isOwner, isFalse);
    expect(profile.repos.single.repo, 'widget');
    expect(profile.repos.single.node, 'alice');

    final team = OrgTeam.fromJson({
      'team': 'Core',
      'permission': 'Write',
      'members': 4,
    });
    expect(team.team, 'core');
    expect(team.permission, 'write');
    expect(team.members, 4);

    // node_owner is the wire name used inside the org profile repo list.
    final repo = OrgRepo.fromJson({'repo': 'API', 'node_owner': 'Bob'});
    expect(repo.repo, 'api');
    expect(repo.node, 'Bob');
  });

  test('orgErrorMessage maps known codes and falls back for unknown', () {
    expect(orgErrorMessage('org_name_taken'), 'That name is already taken.');
    expect(
      orgErrorMessage('last_owner'),
      'An organization must keep at least one owner.',
    );
    expect(orgErrorMessage('mystery'), 'Request failed (mystery).');
    expect(orgErrorMessage(''), 'Request failed.');
  });

  test('myOrgs unwraps the orgs array', () async {
    final server = await _OrgServer.start((req) {
      expect(req.method, 'GET');
      expect(req.path, '/api/orgs');
      return (
        status: 200,
        body: {
          'ok': true,
          'orgs': [
            {'name': 'acme', 'role': 'owner'},
            {'name': 'globex', 'role': 'member'},
          ],
        },
      );
    });
    addTearDown(server.close);

    final api = await server.api();
    final orgs = await api.myOrgs();

    expect(orgs.map((o) => o.name), ['acme', 'globex']);
    expect(orgs.first.canManage, isTrue);
    expect(orgs.last.canManage, isFalse);
  });

  test('createOrg POSTs the payload and returns the new org', () async {
    final server = await _OrgServer.start((req) {
      expect(req.method, 'POST');
      expect(req.path, '/api/orgs');
      expect(req.body, {
        'name': 'acme',
        'displayName': 'Acme Inc',
        'description': 'Things',
      });
      return (status: 201, body: {'ok': true, 'org': 'acme', 'role': 'owner'});
    });
    addTearDown(server.close);

    final api = await server.api();
    final created = await api.createOrg(
      name: 'ACME',
      displayName: 'Acme Inc',
      description: 'Things',
    );

    expect(created.name, 'acme');
    expect(created.role, 'owner');
  });

  test('createOrg surfaces the Worker error code as a message', () async {
    final server = await _OrgServer.start((req) {
      return (status: 409, body: {'error': 'org_name_taken'});
    });
    addTearDown(server.close);

    final api = await server.api();

    await expectLater(
      api.createOrg(name: 'taken'),
      throwsA(
        isA<OrgApiException>()
            .having((e) => e.code, 'code', 'org_name_taken')
            .having((e) => e.message, 'message', 'That name is already taken.'),
      ),
    );
  });

  test('member/team/repo writes use the right method, path and body', () async {
    final server = await _OrgServer.start((req) {
      if (req.path.endsWith('/members') && req.method == 'GET') {
        return (
          status: 200,
          body: {
            'ok': true,
            'members': [
              {'name': 'alice', 'role': 'owner', 'since': 1},
              {'name': 'bob', 'role': 'member', 'since': 2},
            ],
          },
        );
      }
      return (status: 200, body: {'ok': true});
    });
    addTearDown(server.close);

    final api = await server.api();

    final members = await api.orgMembers('acme');
    expect(members.map((m) => m.name), ['alice', 'bob']);

    await api.setOrgMember('acme', 'Carol', role: 'admin');
    await api.removeOrgMember('acme', 'Bob');
    await api.setOrgTeam('acme', 'Core', permission: 'write');
    await api.deleteOrgTeam('acme', 'Core');
    await api.linkOrgRepo('acme', 'Widget', node: 'Alice');
    await api.unlinkOrgRepo('acme', 'Widget');
    await api.addOrgTeamMember('acme', 'core', 'Alice');
    await api.removeOrgTeamMember('acme', 'core', 'Alice');
    await api.deleteOrg('acme');

    // Skip the initial GET /members; assert the writes. Records compare maps
    // by identity, so check each field (body via the equals matcher) instead.
    final writes = server.requests.where((r) => r.method != 'GET').toList();
    void expectWrite(
      int i,
      String method,
      String path,
      Map<String, dynamic> body,
    ) {
      expect(writes[i].method, method);
      expect(writes[i].path, path);
      expect(writes[i].body, equals(body));
    }

    expectWrite(0, 'POST', '/api/orgs/acme/members', {
      'member': 'carol',
      'role': 'admin',
    });
    expectWrite(1, 'DELETE', '/api/orgs/acme/members', {'member': 'bob'});
    expectWrite(2, 'POST', '/api/orgs/acme/teams', {
      'team': 'core',
      'permission': 'write',
    });
    expectWrite(3, 'DELETE', '/api/orgs/acme/teams', {'team': 'core'});
    expectWrite(4, 'POST', '/api/orgs/acme/repos', {
      'repo': 'widget',
      'node': 'alice',
    });
    expectWrite(5, 'DELETE', '/api/orgs/acme/repos', {'repo': 'widget'});
    expectWrite(6, 'POST', '/api/orgs/acme/teams/core/members', {
      'member': 'alice',
    });
    expectWrite(7, 'DELETE', '/api/orgs/acme/teams/core/members', {
      'member': 'alice',
    });
    expectWrite(8, 'DELETE', '/api/orgs/acme', <String, dynamic>{});
  });
}
