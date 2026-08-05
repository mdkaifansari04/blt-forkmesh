import 'dart:convert';
import 'dart:typed_data';

import 'package:forkmesh_git/forkmesh_git.dart';
import 'package:test/test.dart';

GitObjectId fixtureId(int byte) =>
    GitObjectId.parseSha1(byte.toRadixString(16).padLeft(2, '0') * 20);

void main() {
  test('tree writer uses Git raw-byte directory ordering', () {
    final tree = GitTree(<GitTreeEntry>[
      GitTreeEntry.regular(utf8.encode('z'), fixtureId(1)),
      GitTreeEntry.directory(utf8.encode('a'), fixtureId(2)),
    ]);

    expect(tree.entries.map((entry) => utf8.decode(entry.name)), <String>[
      'a',
      'z',
    ]);
    expect(GitTree.parse(tree.encode()).entries, hasLength(2));
  });

  test('tree parser rejects unsorted or gitlink entries', () {
    final unsorted = BytesBuilder(copy: false)
      ..add(ascii.encode('100644 z\u0000'))
      ..add(fixtureId(1).bytes)
      ..add(ascii.encode('100644 a\u0000'))
      ..add(fixtureId(2).bytes);
    final gitlink = BytesBuilder(copy: false)
      ..add(ascii.encode('160000 module\u0000'))
      ..add(fixtureId(1).bytes);

    expect(
      () => GitTree.parse(unsorted.takeBytes()),
      throwsA(isA<GitException>()),
    );
    expect(
      () => GitTree.parse(gitlink.takeBytes()),
      throwsA(isA<GitException>()),
    );
  });

  test('commit parser preserves parent order and message bytes', () {
    final commit = GitCommit.parse(
      utf8.encode(
        'tree ${fixtureId(1).hex}\n'
        'parent ${fixtureId(2).hex}\n'
        'parent ${fixtureId(3).hex}\n'
        'author Ada <ada@example.test> 0 +0000\n'
        'committer Ada <ada@example.test> 0 +0000\n'
        '\n'
        'message\n',
      ),
    );

    expect(commit.tree, fixtureId(1));
    expect(commit.parents, <GitObjectId>[fixtureId(2), fixtureId(3)]);
    expect(utf8.decode(commit.message), 'message\n');
  });

  test('annotated tag parser rejects a malformed target object ID', () {
    expect(
      () => GitAnnotatedTag.parse(
        utf8.encode('object not-an-oid\ntype commit\ntag v1\n\nmessage\n'),
      ),
      throwsA(isA<GitException>()),
    );
  });
}
