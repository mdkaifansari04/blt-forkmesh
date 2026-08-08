import 'dart:convert';
import 'dart:io';

import 'package:forkmesh_git/forkmesh_git.dart';
import 'package:test/test.dart';

void main() {
  test('system Git and ForkMesh read each other\'s loose blobs', () async {
    final systemGit = Platform.environment['FORKMESH_TEST_SYSTEM_GIT'];
    if (systemGit == null || !systemGit.startsWith('/')) {
      throw StateError(
        'FORKMESH_TEST_SYSTEM_GIT must be an absolute test-only path',
      );
    }
    final root = await Directory.systemTemp.createTemp('forkmesh-g1-interop-');
    addTearDown(() => root.delete(recursive: true));
    final repository = await GitRepository.init(GitRepositoryInit(root));

    final ownedId = await repository.writeBlob(utf8.encode('owned\n'));
    final catFile = await Process.run(systemGit, <String>[
      '-C',
      root.path,
      'cat-file',
      '-p',
      ownedId.hex,
    ]);
    expect(catFile.exitCode, 0, reason: catFile.stderr.toString());
    expect(catFile.stdout, 'owned\n');

    final oracle = await Process.start(systemGit, <String>[
      '-C',
      root.path,
      'hash-object',
      '-w',
      '--stdin',
    ]);
    oracle.stdin.write('oracle\n');
    await oracle.stdin.close();
    final oracleStdout = await utf8.decoder.bind(oracle.stdout).join();
    final oracleStderr = await utf8.decoder.bind(oracle.stderr).join();
    expect(await oracle.exitCode, 0, reason: oracleStderr);
    final oracleId = GitObjectId.parseSha1(oracleStdout.trim());
    expect(utf8.decode(await repository.readBlob(oracleId)), 'oracle\n');
  });
}
