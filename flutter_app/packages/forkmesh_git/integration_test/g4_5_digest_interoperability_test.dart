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

Future<String> _oracleObjectId(GitObjectType type, List<int> body) async {
  final process = await Process.start(_systemGit, <String>[
    'hash-object',
    '-t',
    type.name,
    '--stdin',
  ]);
  process.stdin.add(body);
  await process.stdin.close();
  final stdout = utf8.decoder.bind(process.stdout).join();
  final stderr = utf8.decoder.bind(process.stderr).join();
  expect(await process.exitCode, 0, reason: await stderr);
  return (await stdout).trim();
}

String _ownedFrameId(List<int> frame) {
  final digest = ForkMeshSha1();
  const chunkSizes = <int>[1, 55, 56, 63, 64, 65, 7, 3];
  for (var offset = 0, chunk = 0; offset < frame.length; chunk += 1) {
    final requested = chunkSizes[chunk % chunkSizes.length];
    final length = requested < frame.length - offset
        ? requested
        : frame.length - offset;
    digest.add(frame.sublist(offset, offset + length));
    offset += length;
  }
  return encodeLowerHex(digest.close());
}

void main() {
  test(
    'Git object frames match system Git across types and irregular chunks',
    () async {
      const emptyTree = '4b825dc642cb6eb9a060e54bf8d69288fbee4904';
      final frames = <({GitObjectType type, List<int> body})>[
        (type: GitObjectType.blob, body: utf8.encode('blob body\n')),
        (type: GitObjectType.tree, body: <int>[]),
        (
          type: GitObjectType.commit,
          body: utf8.encode(
            'tree $emptyTree\n'
            'author Oracle <oracle@example.test> 0 +0000\n'
            'committer Oracle <oracle@example.test> 0 +0000\n'
            '\n'
            'commit body\n',
          ),
        ),
        (
          type: GitObjectType.tag,
          body: utf8.encode(
            'object $emptyTree\n'
            'type tree\n'
            'tag v1\n'
            'tagger Oracle <oracle@example.test> 0 +0000\n'
            '\n'
            'tag body\n',
          ),
        ),
      ];

      for (final entry in frames) {
        final frame = GitObjectFrame.encode(entry.type, entry.body);
        final expected = await _oracleObjectId(entry.type, entry.body);

        expect(GitObjectId.sha1Of(frame).hex, expected);
        expect(_ownedFrameId(frame), expected);
      }
    },
  );
}
