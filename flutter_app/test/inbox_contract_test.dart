import 'dart:convert';
import 'dart:io';

import 'package:crypto/crypto.dart' as crypto;
import 'package:flutter_test/flutter_test.dart';
import 'package:forkmesh/services/identity.dart';
import 'package:forkmesh/services/inbox_service.dart';
import 'package:forkmesh/services/settings_service.dart';
import 'package:shared_preferences/shared_preferences.dart';

// Signed-inbox wire contract (pull #4). Every content layout and canonical
// string InboxService signs must match the Qt client (IssueStore/PullStore)
// and the worker's verify_* functions byte-for-byte, or the relay rejects the
// mobile submission with 401 bad_signature. The expected hashes below are the
// SAME cross-language vectors qt_client/tests/test_crypto.cpp pins, so a
// drift on either side breaks a test in that client's own suite.
void main() {
  String sha256Hex(String content) =>
      crypto.sha256.convert(utf8.encode(content)).toString();

  late InboxService inbox;

  setUp(() async {
    SharedPreferences.setMockInitialValues({});
    final settings = await SettingsService.create();
    final identity = await Identity.loadOrCreate();
    inbox = InboxService(settings, identity);
  });

  test('issue-event content hashes match the Qt cross-language vectors', () {
    // qt: IssueStore::canonicalString "open" vector.
    expect(
      sha256Hex(
        inbox.issueContent('open', {
          'title': 'Hello',
          'body': 'World',
          'attachments': ['attachments/x.png'],
        }),
      ),
      '7b66aa5f19006b73598497bef2f5d6ed94700c48d479279df3ff5eb147fb0513',
    );
    // qt: "title" (rename) vector — content is just the title.
    expect(
      sha256Hex(inbox.issueContent('title', {'title': 'Renamed issue'})),
      '37f2b6516c608aafe45958eabd5bb4c20b9c6765d1eb39a5c27b4588e566ec97',
    );
  });

  test('pull-comment content hashes match the Qt cross-language vectors', () {
    // qt: PullStore::canonicalString "comment" vector.
    expect(
      sha256Hex(inbox.pullCommentContent('comment', {'body': 'Looks good'})),
      '5fc87d339144090b0ad2e192e6a6fe58e98d3a5a062467c7e62049c7d8c3db01',
    );
    // qt: "review" vector — state and body, NUL-joined.
    expect(
      sha256Hex(
        inbox.pullCommentContent('review', {
          'state': 'approved',
          'body': 'LGTM',
        }),
      ),
      'b863bbc11dd8fea92da94a7da47f815aceeaa9418483992d0a952273894a0731',
    );
    // qt: "line-comment" vector — path, side, line, body.
    expect(
      sha256Hex(
        inbox.pullCommentContent('line-comment', {
          'path': 'src/x.cpp',
          'side': 'new',
          'line': 42,
          'body': 'needs a guard',
        }),
      ),
      'a2574c2b392fd0db6b7e4b0d025d4094bb410691dca7686ddf095d63881f4985',
    );
  });

  test('canonical string templates bind the same fields as Qt and the worker',
      () {
    // The composition around the content hash lives in private methods; pin
    // the interpolation templates at the source level (the same way the
    // worker suite pins entry.py) so a field can't be silently dropped or
    // reordered on the mobile side only.
    final src = File(
      'lib/services/inbox_service.dart',
    ).readAsStringSync();
    expect(
      src,
      contains(
        r"'forkmesh-issue-event-v1\n$type\n$number\n$_author\n$ts\n$contentHash'",
      ),
    );
    expect(
      src,
      contains(r"'forkmesh-pull-event-v1\n$_author\n$ts\n$contentHash'"),
    );
    expect(
      src,
      contains(
        r"'forkmesh-pull-comment-v1\n$type\n$number\n$_author\n$ts\n$contentHash'",
      ),
    );
    expect(
      src,
      contains(
        r"'forkmesh-commit-comment-v1\n$sha\n$_author\n$ts\n$contentHash'",
      ),
    );
    // A new pull's content is title/base/head/patch, NUL-joined.
    expect(src, contains(r"[title, base, head, patch].join('\x00')"));
  });
}
