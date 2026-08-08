import 'dart:typed_data';

import 'package:forkmesh_git/forkmesh_git.dart';
import 'package:test/test.dart';

Uint8List _uint32(int value) =>
    Uint8List.fromList(<int>[value >> 24, value >> 16, value >> 8, value]);

Uint8List _singleObjectIndex(GitObjectId id) {
  final body = BytesBuilder(copy: false)
    ..add(<int>[0xff, 0x74, 0x4f, 0x63])
    ..add(_uint32(2));
  for (var index = 0; index < 256; index += 1) {
    body.add(_uint32(index < id.bytes.first ? 0 : 1));
  }
  body
    ..add(id.bytes)
    ..add(_uint32(0x12345678))
    ..add(_uint32(12));
  final withoutChecksums = body.takeBytes();
  final packChecksum = List<int>.filled(20, 0xaa);
  final withPackChecksum = Uint8List.fromList(<int>[
    ...withoutChecksums,
    ...packChecksum,
  ]);
  final indexDigest = ForkMeshSha1()..add(withPackChecksum);
  return Uint8List.fromList(<int>[...withPackChecksum, ...indexDigest.close()]);
}

void main() {
  test('parses an index v2 fanout lookup and checksums', () {
    final id = GitObjectId.parseSha1(
      'ce013625030ba8dba906f756967f9e9ca394464a',
    );
    final index = GitPackIndexV2.parse(
      _singleObjectIndex(id),
      GitResourceLimits(),
    );

    expect(index.packChecksum, List<int>.filled(20, 0xaa));
    expect(index.lookup(id)!.offset, 12);
    expect(index.lookup(id)!.crc32, 0x12345678);
  });

  test('rejects an index checksum mismatch and a nonmonotonic fanout', () {
    final id = GitObjectId.parseSha1(
      'ce013625030ba8dba906f756967f9e9ca394464a',
    );
    final checksumChanged = _singleObjectIndex(id)..[0] ^= 1;
    final fanoutChanged = _singleObjectIndex(id)..[8 + 4] = 1;

    expect(
      () => GitPackIndexV2.parse(checksumChanged, GitResourceLimits()),
      throwsA(isA<GitException>()),
    );
    expect(
      () => GitPackIndexV2.parse(fanoutChanged, GitResourceLimits()),
      throwsA(isA<GitException>()),
    );
  });
}
