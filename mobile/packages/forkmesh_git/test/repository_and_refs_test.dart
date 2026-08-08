import 'dart:convert';
import 'dart:io';

import 'package:forkmesh_git/forkmesh_git.dart';
import 'package:test/test.dart';

GitObjectId fixtureId(int byte) =>
    GitObjectId.parseSha1(byte.toRadixString(16).padLeft(2, '0') * 20);

void main() {
  test('rejects traversal and lock-suffix refs', () {
    for (final value in <String>[
      'refs/heads/../main',
      'refs/heads/main.lock',
      'refs/heads/a..b',
      'refs/tags/v1',
    ]) {
      expect(() => GitRefName.branch(value), throwsA(isA<GitException>()));
    }
  });

  test('packed refs reads a peeled tag', () {
    final refs = PackedRefs.parse(
      utf8.encode(
        '# pack-refs with: peeled\n'
        '${fixtureId(1).hex} refs/tags/v1\n'
        '^${fixtureId(2).hex}\n',
      ),
      GitResourceLimits(),
    );

    expect(refs.lookup('refs/tags/v1')!.peeled, fixtureId(2));
  });

  test('stale ref update leaves original object ID unchanged', () async {
    final root = await Directory.systemTemp.createTemp('forkmesh-ref-');
    addTearDown(() => root.delete(recursive: true));
    final refs = RefStore(root);
    final ref = GitRefName.branch('refs/heads/main');
    await refs.update(ref, expectedOld: null, next: fixtureId(3));

    final result = await refs.update(
      ref,
      expectedOld: fixtureId(1),
      next: fixtureId(2),
    );

    expect(result, GitRefUpdateResult.nonFastForward);
    expect(await refs.readDirect(ref), fixtureId(3));
  });

  test(
    'initializes and reopens a SHA-1 repository with symbolic main HEAD',
    () async {
      final root = await Directory.systemTemp.createTemp(
        'forkmesh-repository-',
      );
      addTearDown(() => root.delete(recursive: true));

      final created = await GitRepository.init(GitRepositoryInit(root));
      final id = await created.writeBlob(utf8.encode('hello\n'));
      final reopened = await GitRepository.open(GitRepositoryOpen(root));

      expect(
        await reopened.readHead(),
        const GitSymbolicRef('refs/heads/main'),
      );
      expect(utf8.decode(await reopened.readBlob(id)), 'hello\n');
    },
  );
}
