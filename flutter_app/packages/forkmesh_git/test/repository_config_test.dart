import 'dart:convert';

import 'package:forkmesh_git/src/repository/repository_config.dart';
import 'package:test/test.dart';

void main() {
  test('accepts only the supported non-bare SHA-1 core configuration', () {
    final config = RepositoryConfig.parse(
      utf8.encode(
        '[core]\n'
        'repositoryformatversion = 0\n'
        'bare = false\n'
        'filemode = true\n'
        '[extensions]\n'
        'objectformat = sha1\n',
      ),
    );

    expect(config.fileMode, isTrue);
  });

  test('rejects include directives and external worktrees', () {
    expect(
      () => RepositoryConfig.parse(utf8.encode('[include]\npath = /tmp/x\n')),
      throwsA(isA<Exception>()),
    );
    expect(
      () => RepositoryConfig.parse(utf8.encode('[core]\nworktree = /tmp/x\n')),
      throwsA(isA<Exception>()),
    );
  });
}
