import 'dart:io';

import 'package:forkmesh_git/forkmesh_git.dart';
import 'package:test/test.dart';

void main() {
  test(
    'untrusted pack rejection leaves repository refs and object store unchanged',
    () async {
      final root = await Directory.systemTemp.createTemp(
        'forkmesh-fail-closed-',
      );
      addTearDown(() => root.delete(recursive: true));
      final repository = await GitRepository.init(GitRepositoryInit(root));
      final head = File(
        '${repository.gitDirectory.path}${Platform.pathSeparator}HEAD',
      );
      final headBefore = await head.readAsString();
      final objects = Directory(
        '${repository.gitDirectory.path}${Platform.pathSeparator}objects',
      );
      final objectEntriesBefore = await objects.list().toList();
      final pack = GitPackWriter.write(<GitPackWriteObject>[
        GitPackWriteObject(GitObjectType.blob, <int>[1]),
        GitPackWriteObject(GitObjectType.blob, <int>[2]),
      ]);

      expect(
        () => GitFetchResponse.validateUntrustedPack(
          pack.packBytes,
          GitResourceLimits(),
        ),
        throwsA(
          isA<GitException>().having(
            (error) => error.code,
            'code',
            GitErrorCode.hashScreeningUnavailable,
          ),
        ),
      );

      expect(await head.readAsString(), headBefore);
      expect(
        await objects.list().toList(),
        hasLength(objectEntriesBefore.length),
      );
    },
  );
}
