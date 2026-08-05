import 'dart:io';

import 'package:forkmesh_git/forkmesh_git.dart';
import 'package:test/test.dart';

void main() {
  test('refuses to open when a recovery journal is malformed', () async {
    final root = await Directory.systemTemp.createTemp('forkmesh-journal-');
    addTearDown(() => root.delete(recursive: true));
    await GitRepository.init(GitRepositoryInit(root));
    final journal = Directory(
      '${root.path}${Platform.pathSeparator}.git'
      '${Platform.pathSeparator}forkmesh-journal',
    );
    await journal.create();
    await File(
      '${journal.path}${Platform.pathSeparator}broken',
    ).writeAsString('not a ForkMesh journal');

    await expectLater(
      () => GitRepository.open(GitRepositoryOpen(root)),
      throwsA(
        isA<GitException>().having(
          (error) => error.code,
          'code',
          GitErrorCode.operationInterrupted,
        ),
      ),
    );
  });
}
