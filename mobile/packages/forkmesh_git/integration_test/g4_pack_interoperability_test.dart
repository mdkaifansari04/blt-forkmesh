import 'dart:convert';
import 'dart:io';

import 'package:forkmesh_git/forkmesh_git.dart';
import 'package:test/test.dart';

String get _systemGit {
  final value = Platform.environment['FORKMESH_TEST_SYSTEM_GIT'];
  if (value == null || !value.startsWith('/')) {
    throw StateError(
      'FORKMESH_TEST_SYSTEM_GIT must be an absolute test-only path',
    );
  }
  return value;
}

void main() {
  test(
    'system Git verifies and reads a ForkMesh generated non-delta pack',
    () async {
      final root = await Directory.systemTemp.createTemp('forkmesh-g4-pack-');
      addTearDown(() => root.delete(recursive: true));
      final initialized = await Process.run(_systemGit, <String>[
        'init',
        '--initial-branch=main',
        root.path,
      ]);
      expect(initialized.exitCode, 0, reason: initialized.stderr.toString());
      final written = GitPackWriter.write(<GitPackWriteObject>[
        GitPackWriteObject(GitObjectType.blob, utf8.encode('owned\n')),
        GitPackWriteObject(GitObjectType.blob, <int>[0, 1, 0xff]),
      ]);
      final packDirectory = Directory(
        '${root.path}${Platform.pathSeparator}.git'
        '${Platform.pathSeparator}objects${Platform.pathSeparator}pack',
      );
      final base =
          '${packDirectory.path}${Platform.pathSeparator}pack-forkmesh';
      await File('$base.pack').writeAsBytes(written.packBytes, flush: true);
      await File('$base.idx').writeAsBytes(written.indexBytes, flush: true);

      final verified = await Process.run(_systemGit, <String>[
        'verify-pack',
        '-v',
        '$base.idx',
      ]);
      expect(verified.exitCode, 0, reason: verified.stderr.toString());
      final first = GitObjectId.sha1Of(
        GitObjectFrame.encode(GitObjectType.blob, utf8.encode('owned\n')),
      );
      final body = await Process.run(_systemGit, <String>[
        '-C',
        root.path,
        'cat-file',
        '-p',
        first.hex,
      ]);

      expect(body.exitCode, 0, reason: body.stderr.toString());
      expect(body.stdout, 'owned\n');
    },
  );
}
