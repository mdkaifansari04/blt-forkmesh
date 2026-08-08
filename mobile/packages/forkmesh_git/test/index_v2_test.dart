import 'dart:convert';

import 'package:forkmesh_git/forkmesh_git.dart';
import 'package:test/test.dart';

GitObjectId fixtureId(int byte) =>
    GitObjectId.parseSha1(byte.toRadixString(16).padLeft(2, '0') * 20);

void main() {
  test('writes and reads a checksummed version 2 index', () {
    final bytes = GitIndexV2(<GitIndexEntry>[
      GitIndexEntry(
        path: utf8.encode('bin/run'),
        objectId: fixtureId(1),
        mode: GitIndexMode.executable,
        size: 12,
      ),
      GitIndexEntry(
        path: utf8.encode('hello.txt'),
        objectId: fixtureId(2),
        mode: GitIndexMode.regular,
        size: 6,
      ),
    ]).encode();

    final index = GitIndexV2.parse(bytes, GitResourceLimits());

    expect(index.entries.map((entry) => utf8.decode(entry.path)), <String>[
      'bin/run',
      'hello.txt',
    ]);
    expect(index.entries.first.mode, GitIndexMode.executable);
    expect(index.entries.last.objectId, fixtureId(2));
  });

  test('rejects an index with a changed checksum byte', () {
    final bytes = GitIndexV2(<GitIndexEntry>[
      GitIndexEntry(
        path: utf8.encode('hello.txt'),
        objectId: fixtureId(2),
        mode: GitIndexMode.regular,
      ),
    ]).encode();
    bytes[bytes.length - 1] ^= 0x01;

    expect(
      () => GitIndexV2.parse(bytes, GitResourceLimits()),
      throwsA(isA<GitException>()),
    );
  });

  test('rejects a traversal path and non-zero stage entry', () {
    expect(
      () => GitIndexEntry(
        path: utf8.encode('../outside'),
        objectId: fixtureId(3),
        mode: GitIndexMode.regular,
      ),
      throwsA(isA<GitException>()),
    );
    expect(
      () => GitIndexEntry(
        path: utf8.encode('conflict.txt'),
        objectId: fixtureId(3),
        mode: GitIndexMode.regular,
        stage: 1,
      ),
      throwsA(isA<GitException>()),
    );
  });
}
