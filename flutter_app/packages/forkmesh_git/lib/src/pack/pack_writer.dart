import 'dart:convert';
import 'dart:io';
import 'dart:typed_data';

import '../crypto/sha1.dart';
import '../errors/git_error.dart';
import '../objects/git_object.dart';
import '../security/cancellation.dart';

final class GitPackWriteObject {
  GitPackWriteObject(this.type, List<int> body)
    : body = Uint8List.fromList(body),
      objectId = GitObjectId.sha1Of(GitObjectFrame.encode(type, body)) {
    if (this.body.length > 256 * 1024 * 1024) {
      throw const GitException(
        GitErrorCode.resourceLimitExceeded,
        'pack object exceeds hard limit',
      );
    }
  }

  final GitObjectType type;
  final Uint8List body;
  final GitObjectId objectId;
}

final class GitWrittenPack {
  const GitWrittenPack._(this.packBytes, this.indexBytes);

  final Uint8List packBytes;
  final Uint8List indexBytes;
}

final class GitPackWriter {
  static GitWrittenPack write(
    Iterable<GitPackWriteObject> objects, {
    GitCancellationToken? cancellation,
  }) {
    cancellation?.throwIfCancelled();
    final values = List<GitPackWriteObject>.of(objects);
    final knownIds = <String>{};
    for (final object in values) {
      if (!knownIds.add(object.objectId.hex)) {
        throw const GitException(
          GitErrorCode.packCorrupt,
          'pack write objects contain a duplicate object ID',
        );
      }
    }
    final pack = BytesBuilder(copy: false)
      ..add(ascii.encode('PACK'))
      ..add(_uint32(2))
      ..add(_uint32(values.length));
    final indexed = <_WrittenObject>[];
    for (final object in values) {
      cancellation?.throwIfCancelled();
      final offset = pack.length;
      final encoded = BytesBuilder(copy: false)
        ..add(_objectHeader(object.type, object.body.length))
        ..add(ZLibEncoder().convert(object.body));
      final objectBytes = encoded.takeBytes();
      pack.add(objectBytes);
      indexed.add(
        _WrittenObject(
          object: object,
          offset: offset,
          crc32: _crc32(objectBytes),
        ),
      );
    }
    cancellation?.throwIfCancelled();
    final beforeTrailer = pack.takeBytes();
    final digest = ForkMeshSha1()..add(beforeTrailer);
    final packChecksum = digest.close();
    final packBytes = Uint8List.fromList(<int>[
      ...beforeTrailer,
      ...packChecksum,
    ]);
    final indexBytes = _writeIndex(indexed, packChecksum);
    return GitWrittenPack._(packBytes, indexBytes);
  }
}

final class _WrittenObject {
  const _WrittenObject({
    required this.object,
    required this.offset,
    required this.crc32,
  });

  final GitPackWriteObject object;
  final int offset;
  final int crc32;
}

Uint8List _writeIndex(List<_WrittenObject> input, Uint8List packChecksum) {
  final entries = List<_WrittenObject>.of(input)
    ..sort(
      (left, right) =>
          left.object.objectId.hex.compareTo(right.object.objectId.hex),
    );
  final fanout = List<int>.filled(256, 0);
  for (final entry in entries) {
    fanout[entry.object.objectId.bytes.first] += 1;
  }
  for (var index = 1; index < fanout.length; index += 1) {
    fanout[index] += fanout[index - 1];
  }
  final output = BytesBuilder(copy: false)
    ..add(<int>[0xff, 0x74, 0x4f, 0x63])
    ..add(_uint32(2));
  for (final value in fanout) {
    output.add(_uint32(value));
  }
  for (final entry in entries) {
    output.add(entry.object.objectId.bytes);
  }
  for (final entry in entries) {
    output.add(_uint32(entry.crc32));
  }
  for (final entry in entries) {
    if (entry.offset >= 0x80000000) {
      throw const GitException(
        GitErrorCode.resourceLimitExceeded,
        'large pack offsets are unsupported by the initial writer',
      );
    }
    output.add(_uint32(entry.offset));
  }
  output.add(packChecksum);
  final beforeChecksum = output.takeBytes();
  final indexDigest = ForkMeshSha1()..add(beforeChecksum);
  return Uint8List.fromList(<int>[...beforeChecksum, ...indexDigest.close()]);
}

Uint8List _objectHeader(GitObjectType type, int size) {
  final typeCode = switch (type) {
    GitObjectType.commit => 1,
    GitObjectType.tree => 2,
    GitObjectType.blob => 3,
    GitObjectType.tag => 4,
  };
  final output = BytesBuilder(copy: false);
  var remaining = size;
  var first = (typeCode << 4) | (remaining & 0x0f);
  remaining >>= 4;
  if (remaining != 0) first |= 0x80;
  output.addByte(first);
  while (remaining != 0) {
    var byte = remaining & 0x7f;
    remaining >>= 7;
    if (remaining != 0) byte |= 0x80;
    output.addByte(byte);
  }
  return output.takeBytes();
}

int _crc32(List<int> bytes) {
  var crc = 0xffffffff;
  for (final byte in bytes) {
    crc ^= byte;
    for (var bit = 0; bit < 8; bit += 1) {
      crc = crc & 1 == 0 ? crc >>> 1 : (crc >>> 1) ^ 0xedb88320;
    }
  }
  return crc ^ 0xffffffff;
}

Uint8List _uint32(int value) =>
    Uint8List.fromList(<int>[value >> 24, value >> 16, value >> 8, value]);
