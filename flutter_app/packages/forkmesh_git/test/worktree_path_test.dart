import 'dart:convert';
import 'dart:io';

import 'package:forkmesh_git/forkmesh_git.dart';
import 'package:test/test.dart';

void main() {
  test('accepts a portable UTF-8 repository-relative path', () {
    final path = GitWorktreePath('nested/caf\u00e9.txt');

    expect(path.components, <String>['nested', 'caf\u00e9.txt']);
    expect(utf8.decode(path.bytes), 'nested/caf\u00e9.txt');
  });

  test('rejects traversal, device, and platform-ambiguous path names', () {
    for (final value in <String>[
      '../outside',
      '/absolute',
      'nested/../outside',
      'CON',
      'name.',
      'name ',
      'name:stream',
      'nested\\backslash',
    ]) {
      expect(() => GitWorktreePath(value), throwsA(isA<GitException>()));
    }
  });

  test('refuses to traverse an existing symlink while writing', () async {
    final root = await Directory.systemTemp.createTemp('forkmesh-worktree-');
    final outside = await Directory.systemTemp.createTemp('forkmesh-outside-');
    addTearDown(() async {
      await root.delete(recursive: true);
      await outside.delete(recursive: true);
    });
    await Link(
      '${root.path}${Platform.pathSeparator}escape',
    ).create(outside.path);
    final worktree = GitWorktree(root);

    await expectLater(
      () => worktree.writeFile(GitWorktreePath('escape/owned.txt'), <int>[1]),
      throwsA(
        isA<GitException>().having(
          (error) => error.code,
          'code',
          GitErrorCode.symlinkEscape,
        ),
      ),
    );
    expect(
      await File('${outside.path}${Platform.pathSeparator}owned.txt').exists(),
      isFalse,
    );
  });
}
