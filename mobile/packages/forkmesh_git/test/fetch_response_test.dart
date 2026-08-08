import 'dart:convert';

import 'package:forkmesh_git/forkmesh_git.dart';
import 'package:test/test.dart';

void main() {
  test('extracts sideband channel 1 pack bytes without retaining progress', () {
    final pack = GitPackWriter.write(<GitPackWriteObject>[
      GitPackWriteObject(GitObjectType.blob, utf8.encode('hello\n')),
    ]).packBytes;
    final response = GitFetchResponse.parse(<GitPktLine>[
      GitPktLine.data(utf8.encode('packfile\n')),
      GitPktLine.data(<int>[1, ...pack.sublist(0, 8)]),
      GitPktLine.data(<int>[1, ...pack.sublist(8)]),
      GitPktLine.flush(),
    ]);

    expect(response.packBytes, pack);
  });

  test(
    'blocks untrusted fetch packs until collision screening is implemented',
    () {
      final pack = GitPackWriter.write(<GitPackWriteObject>[
        GitPackWriteObject(GitObjectType.blob, utf8.encode('hello\n')),
      ]).packBytes;

      expect(
        () => GitFetchResponse.validateUntrustedPack(pack, GitResourceLimits()),
        throwsA(
          isA<GitException>().having(
            (error) => error.code,
            'code',
            GitErrorCode.hashScreeningUnavailable,
          ),
        ),
      );
    },
  );

  test('validates an untrusted pack only through a clear detector', () {
    final pack = GitPackWriter.write(<GitPackWriteObject>[
      GitPackWriteObject(GitObjectType.blob, utf8.encode('hello\n')),
    ]).packBytes;

    expect(
      () => GitFetchResponse.validateUntrustedPack(
        pack,
        GitResourceLimits(),
        detector: _ClearDetector(),
      ),
      returnsNormally,
    );
  });
}

final class _ClearDetector implements GitSha1CollisionDetector {
  @override
  GitSha1CollisionDetectorSession start() => _ClearDetectorSession();
}

final class _ClearDetectorSession implements GitSha1CollisionDetectorSession {
  @override
  void add(List<int> bytes) {}

  @override
  GitSha1CollisionDetectorStatus close() =>
      GitSha1CollisionDetectorStatus.clear;

  @override
  void dispose() {}
}
