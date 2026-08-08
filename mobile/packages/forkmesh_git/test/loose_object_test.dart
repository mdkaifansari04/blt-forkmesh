import 'dart:convert';
import 'dart:io';

import 'package:forkmesh_git/forkmesh_git.dart';
import 'package:test/test.dart';

void main() {
  test('frames hello blob with Git canonical bytes and object ID', () {
    final frame = GitObjectFrame.encode(
      GitObjectType.blob,
      utf8.encode('hello\n'),
    );

    expect(utf8.decode(frame), 'blob 6\u0000hello\n');
    expect(
      GitObjectId.sha1Of(frame).hex,
      'ce013625030ba8dba906f756967f9e9ca394464a',
    );
  });

  test('rejects object body whose declared length is wrong', () {
    expect(
      () => GitObjectFrame.decode(
        utf8.encode('blob 7\u0000hello\n'),
        GitResourceLimits(),
      ),
      throwsA(isA<GitException>()),
    );
  });

  test('writes and reads a verified loose object', () async {
    final root = await Directory.systemTemp.createTemp(
      'forkmesh-loose-object-',
    );
    addTearDown(() => root.delete(recursive: true));
    final store = LooseObjectStore(root, GitResourceLimits());

    final id = await store.write(GitObjectType.blob, utf8.encode('hello\n'));
    final object = await store.read(id);

    expect(id.hex, 'ce013625030ba8dba906f756967f9e9ca394464a');
    expect(object.type, GitObjectType.blob);
    expect(utf8.decode(object.body), 'hello\n');
  });

  test('rejects a loose object stored at the wrong object ID', () async {
    final root = await Directory.systemTemp.createTemp(
      'forkmesh-object-mismatch-',
    );
    addTearDown(() => root.delete(recursive: true));
    final store = LooseObjectStore(root, GitResourceLimits());
    final id = await store.write(GitObjectType.blob, utf8.encode('hello\n'));
    final source = store.fileFor(id);
    final wrong = store.fileFor(
      GitObjectId.parseSha1('0000000000000000000000000000000000000001'),
    );
    await wrong.parent.create(recursive: true);
    await source.copy(wrong.path);

    expect(
      () => store.read(
        GitObjectId.parseSha1('0000000000000000000000000000000000000001'),
      ),
      throwsA(isA<GitException>()),
    );
  });

  test(
    'fails closed before exposing an untrusted loose SHA-1 object',
    () async {
      final root = await Directory.systemTemp.createTemp(
        'forkmesh-untrusted-loose-',
      );
      addTearDown(() => root.delete(recursive: true));
      final store = LooseObjectStore(root, GitResourceLimits());
      final id = await store.write(GitObjectType.blob, utf8.encode('hello\n'));

      await expectLater(
        () => store.read(id, origin: GitSha1ObjectOrigin.untrustedTransport),
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

  test('admits an untrusted loose object only with a clear detector', () async {
    final root = await Directory.systemTemp.createTemp(
      'forkmesh-screened-loose-',
    );
    addTearDown(() => root.delete(recursive: true));
    final store = LooseObjectStore(
      root,
      GitResourceLimits(),
      detector: _ClearDetector(),
    );
    final id = await store.write(GitObjectType.blob, utf8.encode('hello\n'));

    final object = await store.read(
      id,
      origin: GitSha1ObjectOrigin.untrustedTransport,
    );

    expect(utf8.decode(object.body), 'hello\n');
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
