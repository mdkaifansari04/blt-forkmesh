import 'dart:io';

import 'package:forkmesh_git/forkmesh_git.dart';
import 'package:test/test.dart';

void main() {
  test(
    'rejection and recovery remove operation quarantine without changing HEAD',
    () async {
      final root = await Directory.systemTemp.createTemp(
        'forkmesh-quarantine-',
      );
      addTearDown(() => root.delete(recursive: true));
      final repository = await GitRepository.init(GitRepositoryInit(root));
      final headBefore = await File(
        '${repository.gitDirectory.path}${Platform.pathSeparator}HEAD',
      ).readAsString();
      final quarantine = await GitObjectQuarantine.begin(
        repository.gitDirectory,
      );
      await quarantine.stage('incoming.pack', <int>[1, 2, 3]);

      await quarantine.reject();

      expect(await quarantine.directory.exists(), isFalse);
      expect(
        await File(
          '${repository.gitDirectory.path}${Platform.pathSeparator}HEAD',
        ).readAsString(),
        headBefore,
      );
    },
  );

  test(
    'cancellation and restart recovery leave no valid quarantine directory',
    () async {
      final root = await Directory.systemTemp.createTemp(
        'forkmesh-quarantine-cancel-',
      );
      addTearDown(() => root.delete(recursive: true));
      final repository = await GitRepository.init(GitRepositoryInit(root));
      final cancellation = GitCancellationToken()..cancel();
      final quarantine = await GitObjectQuarantine.begin(
        repository.gitDirectory,
        cancellation: cancellation,
      );

      await expectLater(
        () => quarantine.stage('incoming.pack', <int>[1]),
        throwsA(
          isA<GitException>().having(
            (error) => error.code,
            'code',
            GitErrorCode.cancelled,
          ),
        ),
      );
      await GitObjectQuarantine.recover(repository.gitDirectory);

      expect(await quarantine.directory.exists(), isFalse);
    },
  );

  test(
    'over-limit staging is rejected and leaves no operation directory',
    () async {
      final root = await Directory.systemTemp.createTemp(
        'forkmesh-quarantine-limit-',
      );
      addTearDown(() => root.delete(recursive: true));
      final repository = await GitRepository.init(GitRepositoryInit(root));
      final quarantine = await GitObjectQuarantine.begin(
        repository.gitDirectory,
        limits: GitResourceLimits(maxInflatedBytes: 1),
      );

      await expectLater(
        () => quarantine.stage('incoming.pack', <int>[1, 2]),
        throwsA(
          isA<GitException>().having(
            (error) => error.code,
            'code',
            GitErrorCode.resourceLimitExceeded,
          ),
        ),
      );

      expect(await quarantine.directory.exists(), isFalse);
    },
  );

  test(
    'repository open clears an interrupted quarantine before exposing state',
    () async {
      final root = await Directory.systemTemp.createTemp(
        'forkmesh-quarantine-open-',
      );
      addTearDown(() => root.delete(recursive: true));
      final repository = await GitRepository.init(GitRepositoryInit(root));
      final quarantine = await GitObjectQuarantine.begin(
        repository.gitDirectory,
      );
      await quarantine.stage('interrupted.pack', <int>[1, 2, 3]);

      await GitRepository.open(GitRepositoryOpen(root));

      expect(await quarantine.directory.exists(), isFalse);
    },
  );
}
