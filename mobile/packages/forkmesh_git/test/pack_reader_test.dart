import 'dart:convert';
import 'dart:io';
import 'dart:typed_data';

import 'package:forkmesh_git/forkmesh_git.dart';
import 'package:test/test.dart';

Uint8List _uint32(int value) =>
    Uint8List.fromList(<int>[value >> 24, value >> 16, value >> 8, value]);

Uint8List _singleBlobPack(List<int> body) {
  final objectHeader = 0x30 | body.length;
  final bytes = BytesBuilder(copy: false)
    ..add(ascii.encode('PACK'))
    ..add(_uint32(2))
    ..add(_uint32(1))
    ..addByte(objectHeader)
    ..add(ZLibEncoder().convert(body));
  final withoutTrailer = bytes.takeBytes();
  final trailer = ForkMeshSha1()..add(withoutTrailer);
  return Uint8List.fromList(<int>[...withoutTrailer, ...trailer.close()]);
}

Uint8List _finishPack(BytesBuilder bytes) {
  final withoutTrailer = bytes.takeBytes();
  final trailer = ForkMeshSha1()..add(withoutTrailer);
  return Uint8List.fromList(<int>[...withoutTrailer, ...trailer.close()]);
}

Uint8List _refDeltaPack() {
  final base = utf8.encode('hello\n');
  final baseId = GitObjectId.sha1Of(
    GitObjectFrame.encode(GitObjectType.blob, base),
  );
  final delta = <int>[6, 12, 0x90, 6, 6, ...utf8.encode('world\n')];
  final bytes = BytesBuilder(copy: false)
    ..add(ascii.encode('PACK'))
    ..add(_uint32(2))
    ..add(_uint32(2))
    ..addByte(0x36)
    ..add(ZLibEncoder().convert(base))
    ..addByte(0x70 | delta.length)
    ..add(baseId.bytes)
    ..add(ZLibEncoder().convert(delta));
  return _finishPack(bytes);
}

Uint8List _ofsDeltaPack() {
  final base = utf8.encode('hello\n');
  final delta = <int>[6, 12, 0x90, 6, 6, ...utf8.encode('world\n')];
  final prefix = BytesBuilder(copy: false)
    ..add(ascii.encode('PACK'))
    ..add(_uint32(2))
    ..add(_uint32(2))
    ..addByte(0x36)
    ..add(ZLibEncoder().convert(base));
  final baseDistance = prefix.length - 12;
  expect(baseDistance, lessThan(128));
  prefix
    ..addByte(0x60 | delta.length)
    ..addByte(baseDistance)
    ..add(ZLibEncoder().convert(delta));
  return _finishPack(prefix);
}

void main() {
  test('reads a checksummed version 2 pack with a blob object', () {
    final pack = GitPackReader.parse(
      _singleBlobPack(utf8.encode('hello\n')),
      GitResourceLimits(),
    );

    expect(pack.objects, hasLength(1));
    expect(pack.objects.single.type, GitObjectType.blob);
    expect(utf8.decode(pack.objects.single.body), 'hello\n');
    expect(
      pack.objects.single.objectId.hex,
      'ce013625030ba8dba906f756967f9e9ca394464a',
    );
  });

  test('rejects a pack whose trailer checksum has changed', () {
    final bytes = _singleBlobPack(utf8.encode('hello\n'));
    bytes[bytes.length - 1] ^= 1;

    expect(
      () => GitPackReader.parse(bytes, GitResourceLimits()),
      throwsA(isA<GitException>()),
    );
  });

  test('resolves reference and offset deltas with checked copy ranges', () {
    for (final bytes in <Uint8List>[_refDeltaPack(), _ofsDeltaPack()]) {
      final pack = GitPackReader.parse(bytes, GitResourceLimits());

      expect(pack.objects, hasLength(2));
      expect(utf8.decode(pack.objects.last.body), 'hello\nworld\n');
      expect(pack.objects.last.type, GitObjectType.blob);
    }
  });

  test('fails closed after reconstructing an untrusted pack object frame', () {
    expect(
      () => GitPackReader.parse(
        _refDeltaPack(),
        GitResourceLimits(),
        origin: GitSha1ObjectOrigin.untrustedTransport,
      ),
      throwsA(
        isA<GitException>().having(
          (error) => error.code,
          'code',
          GitErrorCode.hashScreeningUnavailable,
        ),
      ),
    );
  });

  test(
    'screens each reconstructed untrusted delta frame with the detector',
    () {
      final pack = GitPackReader.parse(
        _refDeltaPack(),
        GitResourceLimits(),
        origin: GitSha1ObjectOrigin.untrustedTransport,
        detector: _ClearDetector(),
      );

      expect(pack.objects, hasLength(2));
    },
  );
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
