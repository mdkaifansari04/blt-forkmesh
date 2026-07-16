import 'dart:typed_data';

import '../crypto/sha1.dart';
import '../errors/git_error.dart';
import '../objects/git_object.dart';
import '../security/resource_limits.dart';

final class GitPackIndexEntry {
  const GitPackIndexEntry(this.objectId, this.crc32, this.offset);

  final GitObjectId objectId;
  final int crc32;
  final int offset;
}

final class GitPackIndexV2 {
  GitPackIndexV2._(this._entries, List<int> packChecksum)
    : packChecksum = Uint8List.fromList(packChecksum);

  final List<GitPackIndexEntry> _entries;
  final Uint8List packChecksum;

  List<GitPackIndexEntry> get entries =>
      List<GitPackIndexEntry>.unmodifiable(_entries);

  static GitPackIndexV2 parse(List<int> input, GitResourceLimits limits) {
    final bytes = Uint8List.fromList(input);
    const headerLength = 8 + 256 * 4;
    if (bytes.length < headerLength + 40) {
      throw const GitException(
        GitErrorCode.packCorrupt,
        'pack index is truncated',
      );
    }
    if (bytes[0] != 0xff ||
        bytes[1] != 0x74 ||
        bytes[2] != 0x4f ||
        bytes[3] != 0x63 ||
        _readUint32(bytes, 4) != 2) {
      throw const GitException(
        GitErrorCode.unsupportedRepositoryFormat,
        'only pack index version 2 is supported',
      );
    }
    _verifyIndexChecksum(bytes);
    var previous = 0;
    for (var index = 0; index < 256; index += 1) {
      final value = _readUint32(bytes, 8 + index * 4);
      if (value < previous) {
        throw const GitException(
          GitErrorCode.packCorrupt,
          'pack index fanout is not monotonic',
        );
      }
      previous = value;
    }
    final count = previous;
    if (count > limits.maxRefCount ||
        count > GitResourceLimits.hardMaxRefCount) {
      throw const GitException(
        GitErrorCode.resourceLimitExceeded,
        'pack index object count exceeds configured limit',
      );
    }
    final fixedLength = headerLength + count * 28 + 40;
    if (fixedLength > bytes.length || (bytes.length - fixedLength) % 8 != 0) {
      throw const GitException(
        GitErrorCode.packCorrupt,
        'pack index size is invalid',
      );
    }
    final nameOffset = headerLength;
    final crcOffset = nameOffset + count * 20;
    final offsetOffset = crcOffset + count * 4;
    final largeOffsetOffset = offsetOffset + count * 4;
    final largeOffsetCount = (bytes.length - fixedLength) ~/ 8;
    final entries = <GitPackIndexEntry>[];
    GitObjectId? previousId;
    for (var index = 0; index < count; index += 1) {
      final id = GitObjectId.parseSha1(
        bytes
            .sublist(nameOffset + index * 20, nameOffset + (index + 1) * 20)
            .map((byte) => byte.toRadixString(16).padLeft(2, '0'))
            .join(),
      );
      if (previousId != null && previousId.hex.compareTo(id.hex) >= 0) {
        throw const GitException(
          GitErrorCode.packCorrupt,
          'pack index object IDs are not strictly sorted',
        );
      }
      final packedOffset = _readUint32(bytes, offsetOffset + index * 4);
      final offset = (packedOffset & 0x80000000) == 0
          ? packedOffset
          : _readLargeOffset(
              bytes,
              largeOffsetOffset,
              packedOffset & 0x7fffffff,
              largeOffsetCount,
            );
      entries.add(
        GitPackIndexEntry(
          id,
          _readUint32(bytes, crcOffset + index * 4),
          offset,
        ),
      );
      previousId = id;
    }
    return GitPackIndexV2._(
      entries,
      bytes.sublist(bytes.length - 40, bytes.length - 20),
    );
  }

  GitPackIndexEntry? lookup(GitObjectId objectId) {
    var low = 0;
    var high = _entries.length - 1;
    while (low <= high) {
      final middle = (low + high) ~/ 2;
      final comparison = _entries[middle].objectId.hex.compareTo(objectId.hex);
      if (comparison == 0) return _entries[middle];
      if (comparison < 0) {
        low = middle + 1;
      } else {
        high = middle - 1;
      }
    }
    return null;
  }
}

int _readLargeOffset(Uint8List bytes, int start, int index, int count) {
  if (index >= count) {
    throw const GitException(
      GitErrorCode.packCorrupt,
      'pack index large offset is invalid',
    );
  }
  final offset = start + index * 8;
  return (_readUint32(bytes, offset) << 32) | _readUint32(bytes, offset + 4);
}

void _verifyIndexChecksum(Uint8List bytes) {
  final checksumOffset = bytes.length - 20;
  final digest = ForkMeshSha1()..add(bytes.sublist(0, checksumOffset));
  var different = 0;
  final actual = digest.close();
  for (var index = 0; index < 20; index += 1) {
    different |= actual[index] ^ bytes[checksumOffset + index];
  }
  if (different != 0) {
    throw const GitException(
      GitErrorCode.packChecksumMismatch,
      'pack index checksum does not match',
    );
  }
}

int _readUint32(Uint8List bytes, int offset) =>
    (bytes[offset] << 24) |
    (bytes[offset + 1] << 16) |
    (bytes[offset + 2] << 8) |
    bytes[offset + 3];
