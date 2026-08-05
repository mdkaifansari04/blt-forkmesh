import 'dart:convert';
import 'dart:io';
import 'dart:typed_data';

import '../crypto/collision_screen.dart';
import '../crypto/sha1.dart';
import '../errors/git_error.dart';
import '../objects/git_object.dart';
import '../security/cancellation.dart';
import '../security/resource_limits.dart';

final class GitPackObject {
  const GitPackObject._({
    required this.offset,
    required this.type,
    required this.body,
    required this.objectId,
  });

  final int offset;
  final GitObjectType type;
  final Uint8List body;
  final GitObjectId objectId;
}

final class GitPack {
  const GitPack._(this.objects);

  final List<GitPackObject> objects;

  GitPackObject? lookup(GitObjectId objectId) {
    for (final object in objects) {
      if (object.objectId == objectId) return object;
    }
    return null;
  }
}

final class GitPackReader {
  static GitPack parse(
    List<int> input,
    GitResourceLimits limits, {
    GitSha1ObjectOrigin origin = GitSha1ObjectOrigin.trustedLocal,
    GitCancellationToken? cancellation,
    GitSha1CollisionDetector? detector,
  }) {
    cancellation?.throwIfCancelled();
    final bytes = Uint8List.fromList(input);
    if (bytes.length < 32) {
      throw const GitException(GitErrorCode.packCorrupt, 'pack is truncated');
    }
    final trailerOffset = bytes.length - 20;
    _verifyTrailer(bytes, trailerOffset);
    if (ascii.decode(bytes.sublist(0, 4), allowInvalid: true) != 'PACK') {
      throw const GitException(
        GitErrorCode.packCorrupt,
        'pack signature is invalid',
      );
    }
    if (_readUint32(bytes, 4) != 2) {
      throw const GitException(
        GitErrorCode.unsupportedRepositoryFormat,
        'only pack version 2 is supported',
      );
    }
    final count = _readUint32(bytes, 8);
    if (count > GitResourceLimits.hardMaxRefCount ||
        count > limits.maxRefCount) {
      throw const GitException(
        GitErrorCode.resourceLimitExceeded,
        'pack object count exceeds configured limit',
      );
    }
    var offset = 12;
    final entries = <_PackEntry>[];
    for (var index = 0; index < count; index += 1) {
      if (offset >= trailerOffset) {
        throw const GitException(
          GitErrorCode.packCorrupt,
          'pack object header is truncated',
        );
      }
      final objectOffset = offset;
      final header = _readObjectHeader(bytes, offset, trailerOffset, limits);
      offset = header.nextOffset;
      int? baseOffset;
      GitObjectId? baseId;
      if (header.kind == _PackObjectKind.ofsDelta) {
        final parsed = _readOffsetDeltaBase(bytes, offset, trailerOffset);
        offset = parsed.nextOffset;
        baseOffset = objectOffset - parsed.distance;
        if (baseOffset < 12) {
          throw const GitException(
            GitErrorCode.packCorrupt,
            'offset delta base is before pack data',
          );
        }
      } else if (header.kind == _PackObjectKind.refDelta) {
        if (trailerOffset - offset < 20) {
          throw const GitException(
            GitErrorCode.packCorrupt,
            'reference delta base is truncated',
          );
        }
        baseId = GitObjectId.parseSha1(
          bytes
              .sublist(offset, offset + 20)
              .map((byte) => byte.toRadixString(16).padLeft(2, '0'))
              .join(),
        );
        offset += 20;
      }
      final compressedEnd = _zlibEnd(bytes, offset, trailerOffset);
      final body = _inflate(
        bytes.sublist(offset, compressedEnd),
        header.size,
        limits,
      );
      entries.add(
        _PackEntry(
          offset: objectOffset,
          kind: header.kind,
          declaredSize: header.size,
          body: body,
          baseOffset: baseOffset,
          baseId: baseId,
        ),
      );
      offset = compressedEnd;
    }
    if (offset != trailerOffset) {
      throw const GitException(
        GitErrorCode.packCorrupt,
        'pack has trailing object data',
      );
    }
    final byOffset = <int, _PackEntry>{
      for (final entry in entries) entry.offset: entry,
    };
    final state = _ResolutionState(
      limits,
      byOffset,
      entries,
      origin,
      cancellation,
      detector,
    );
    final objects = <GitPackObject>[];
    for (final entry in entries) {
      objects.add(state.resolve(entry, 0));
    }
    return GitPack._(List<GitPackObject>.unmodifiable(objects));
  }
}

enum _PackObjectKind { commit, tree, blob, tag, ofsDelta, refDelta }

final class _ObjectHeader {
  const _ObjectHeader(this.kind, this.size, this.nextOffset);

  final _PackObjectKind kind;
  final int size;
  final int nextOffset;
}

_ObjectHeader _readObjectHeader(
  Uint8List bytes,
  int offset,
  int limit,
  GitResourceLimits limits,
) {
  var current = _readByte(bytes, offset++, limit);
  final kind = switch ((current >> 4) & 7) {
    1 => _PackObjectKind.commit,
    2 => _PackObjectKind.tree,
    3 => _PackObjectKind.blob,
    4 => _PackObjectKind.tag,
    6 => _PackObjectKind.ofsDelta,
    7 => _PackObjectKind.refDelta,
    _ => throw const GitException(
      GitErrorCode.packCorrupt,
      'pack object type is invalid',
    ),
  };
  var size = current & 0x0f;
  var shift = 4;
  while (current & 0x80 != 0) {
    if (shift > 56 || offset >= limit) {
      throw const GitException(
        GitErrorCode.packCorrupt,
        'pack object size is invalid',
      );
    }
    current = _readByte(bytes, offset++, limit);
    size |= (current & 0x7f) << shift;
    shift += 7;
  }
  if (size > limits.maxObjectBytes) {
    throw const GitException(
      GitErrorCode.resourceLimitExceeded,
      'pack object exceeds configured limit',
    );
  }
  return _ObjectHeader(kind, size, offset);
}

final class _OffsetDeltaBase {
  const _OffsetDeltaBase(this.distance, this.nextOffset);

  final int distance;
  final int nextOffset;
}

_OffsetDeltaBase _readOffsetDeltaBase(Uint8List bytes, int offset, int limit) {
  var current = _readByte(bytes, offset++, limit);
  var distance = current & 0x7f;
  while (current & 0x80 != 0) {
    if (offset >= limit || distance > 0x00ffffffffffffff) {
      throw const GitException(
        GitErrorCode.packCorrupt,
        'offset delta distance is invalid',
      );
    }
    current = _readByte(bytes, offset++, limit);
    distance = ((distance + 1) << 7) | (current & 0x7f);
  }
  if (distance == 0) {
    throw const GitException(
      GitErrorCode.packCorrupt,
      'offset delta distance is zero',
    );
  }
  return _OffsetDeltaBase(distance, offset);
}

final class _PackEntry {
  _PackEntry({
    required this.offset,
    required this.kind,
    required this.declaredSize,
    required this.body,
    this.baseOffset,
    this.baseId,
  });

  final int offset;
  final _PackObjectKind kind;
  final int declaredSize;
  final Uint8List body;
  final int? baseOffset;
  final GitObjectId? baseId;
  GitPackObject? resolved;
  var resolving = false;
}

final class _ResolutionState {
  _ResolutionState(
    this.limits,
    this.byOffset,
    this.entries,
    this.origin,
    this.cancellation,
    this.detector,
  );

  final GitResourceLimits limits;
  final Map<int, _PackEntry> byOffset;
  final List<_PackEntry> entries;
  final GitSha1ObjectOrigin origin;
  final GitCancellationToken? cancellation;
  final GitSha1CollisionDetector? detector;
  final Map<GitObjectId, _PackEntry> byId = <GitObjectId, _PackEntry>{};
  var totalBytes = 0;

  GitPackObject resolve(_PackEntry entry, int depth) {
    cancellation?.throwIfCancelled();
    final cached = entry.resolved;
    if (cached != null) return cached;
    if (depth >= 32) {
      throw const GitException(
        GitErrorCode.deltaDepthExceeded,
        'pack delta chain exceeds depth limit',
      );
    }
    if (entry.resolving) {
      throw const GitException(GitErrorCode.packCorrupt, 'pack delta cycle');
    }
    entry.resolving = true;
    try {
      final result = switch (entry.kind) {
        _PackObjectKind.commit => _base(entry, GitObjectType.commit),
        _PackObjectKind.tree => _base(entry, GitObjectType.tree),
        _PackObjectKind.blob => _base(entry, GitObjectType.blob),
        _PackObjectKind.tag => _base(entry, GitObjectType.tag),
        _PackObjectKind.ofsDelta ||
        _PackObjectKind.refDelta => _delta(entry, depth),
      };
      final screening = Sha1CollisionScreen(detector: detector).screenFrame(
        frame: GitObjectFrame.encode(result.type, result.body),
        objectType: result.type,
        declaredBodySize: result.body.length,
        origin: origin,
        limits: limits,
        cancellation: cancellation,
      );
      if (!screening.approved) throw screening.toException();
      entry.resolved = result;
      byId[result.objectId] = entry;
      _addToTotal(result.body.length);
      return result;
    } finally {
      entry.resolving = false;
    }
  }

  GitPackObject _base(_PackEntry entry, GitObjectType type) => GitPackObject._(
    offset: entry.offset,
    type: type,
    body: entry.body,
    objectId: GitObjectId.sha1Of(GitObjectFrame.encode(type, entry.body)),
  );

  GitPackObject _delta(_PackEntry entry, int depth) {
    _PackEntry? baseEntry;
    if (entry.baseOffset != null) {
      baseEntry = byOffset[entry.baseOffset!];
    } else if (entry.baseId != null) {
      baseEntry = byId[entry.baseId!];
      baseEntry ??= _findAndResolve(entry.baseId!, depth + 1);
    }
    if (baseEntry == null) {
      throw const GitException(
        GitErrorCode.unsupportedRepositoryFormat,
        'thin packs are unsupported',
      );
    }
    final base = resolve(baseEntry, depth + 1);
    final body = _applyDelta(base.body, entry.body, limits);
    return GitPackObject._(
      offset: entry.offset,
      type: base.type,
      body: body,
      objectId: GitObjectId.sha1Of(GitObjectFrame.encode(base.type, body)),
    );
  }

  _PackEntry? _findAndResolve(GitObjectId id, int depth) {
    for (final candidate in entries) {
      final object = resolve(candidate, depth);
      if (object.objectId == id) return candidate;
    }
    return null;
  }

  void _addToTotal(int byteLength) {
    totalBytes += byteLength;
    if (totalBytes > limits.maxInflatedBytes) {
      throw const GitException(
        GitErrorCode.resourceLimitExceeded,
        'resolved pack bytes exceed configured limit',
      );
    }
  }
}

Uint8List _applyDelta(
  Uint8List base,
  Uint8List delta,
  GitResourceLimits limits,
) {
  final reader = _DeltaReader(delta);
  final sourceSize = reader.readVariableInteger();
  final targetSize = reader.readVariableInteger();
  if (sourceSize != base.length || targetSize > limits.maxObjectBytes) {
    throw const GitException(
      GitErrorCode.packCorrupt,
      'delta header is invalid',
    );
  }
  final output = BytesBuilder(copy: false);
  while (!reader.isAtEnd) {
    final instruction = reader.readByte();
    if (instruction == 0) {
      throw const GitException(
        GitErrorCode.packCorrupt,
        'delta instruction is zero',
      );
    }
    if (instruction & 0x80 == 0) {
      final inserted = reader.readBytes(instruction);
      if (output.length + inserted.length > targetSize) {
        throw const GitException(
          GitErrorCode.packCorrupt,
          'delta insert exceeds target size',
        );
      }
      output.add(inserted);
      continue;
    }
    var sourceOffset = 0;
    var size = 0;
    if (instruction & 0x01 != 0) sourceOffset |= reader.readByte();
    if (instruction & 0x02 != 0) sourceOffset |= reader.readByte() << 8;
    if (instruction & 0x04 != 0) sourceOffset |= reader.readByte() << 16;
    if (instruction & 0x08 != 0) sourceOffset |= reader.readByte() << 24;
    if (instruction & 0x10 != 0) size |= reader.readByte();
    if (instruction & 0x20 != 0) size |= reader.readByte() << 8;
    if (instruction & 0x40 != 0) size |= reader.readByte() << 16;
    if (size == 0) size = 0x10000;
    if (sourceOffset < 0 ||
        size < 0 ||
        sourceOffset > base.length ||
        size > base.length - sourceOffset ||
        output.length + size > targetSize) {
      throw const GitException(
        GitErrorCode.packCorrupt,
        'delta copy is invalid',
      );
    }
    output.add(base.sublist(sourceOffset, sourceOffset + size));
  }
  if (output.length != targetSize) {
    throw const GitException(
      GitErrorCode.packCorrupt,
      'delta result size does not match header',
    );
  }
  return output.takeBytes();
}

final class _DeltaReader {
  _DeltaReader(this.bytes);

  final Uint8List bytes;
  var offset = 0;

  bool get isAtEnd => offset == bytes.length;

  int readByte() {
    if (isAtEnd) {
      throw const GitException(GitErrorCode.packCorrupt, 'delta is truncated');
    }
    return bytes[offset++];
  }

  Uint8List readBytes(int length) {
    if (length < 0 || length > bytes.length - offset) {
      throw const GitException(GitErrorCode.packCorrupt, 'delta is truncated');
    }
    final result = bytes.sublist(offset, offset + length);
    offset += length;
    return result;
  }

  int readVariableInteger() {
    var value = 0;
    var shift = 0;
    while (true) {
      if (shift > 56) {
        throw const GitException(
          GitErrorCode.packCorrupt,
          'delta variable integer is invalid',
        );
      }
      final byte = readByte();
      value |= (byte & 0x7f) << shift;
      if (byte & 0x80 == 0) return value;
      shift += 7;
    }
  }
}

Uint8List _inflate(
  Uint8List compressed,
  int declaredSize,
  GitResourceLimits limits,
) {
  Uint8List output;
  try {
    output = Uint8List.fromList(ZLibDecoder().convert(compressed));
  } on FormatException {
    throw const GitException(
      GitErrorCode.packCorrupt,
      'pack zlib stream is invalid',
    );
  }
  if (output.length != declaredSize || output.length > limits.maxObjectBytes) {
    throw const GitException(
      GitErrorCode.packCorrupt,
      'pack object size does not match header',
    );
  }
  return output;
}

void _verifyTrailer(Uint8List bytes, int trailerOffset) {
  final digest = ForkMeshSha1()..add(bytes.sublist(0, trailerOffset));
  final actual = digest.close();
  var different = 0;
  for (var index = 0; index < 20; index += 1) {
    different |= actual[index] ^ bytes[trailerOffset + index];
  }
  if (different != 0) {
    throw const GitException(
      GitErrorCode.packChecksumMismatch,
      'pack trailer checksum does not match',
    );
  }
}

int _zlibEnd(Uint8List bytes, int start, int limit) {
  if (limit - start < 6) {
    throw const GitException(
      GitErrorCode.packCorrupt,
      'zlib stream is truncated',
    );
  }
  final cmf = bytes[start];
  final flg = bytes[start + 1];
  if (cmf & 0x0f != 8 ||
      cmf >> 4 > 7 ||
      (cmf << 8 | flg) % 31 != 0 ||
      flg & 0x20 != 0) {
    throw const GitException(
      GitErrorCode.packCorrupt,
      'zlib header is invalid',
    );
  }
  final reader = _DeflateBitReader(bytes, start + 2, limit);
  var finalBlock = false;
  while (!finalBlock) {
    finalBlock = reader.readBits(1) == 1;
    final type = reader.readBits(2);
    switch (type) {
      case 0:
        reader.alignToByte();
        final length = reader.readBits(16);
        final complement = reader.readBits(16);
        if ((length ^ complement) != 0xffff) {
          throw const GitException(
            GitErrorCode.packCorrupt,
            'stored deflate block length is invalid',
          );
        }
        reader.skipBytes(length);
      case 1:
        _skipCompressedBlock(reader, _fixedLiteralTree, _fixedDistanceTree);
      case 2:
        final trees = _readDynamicTrees(reader);
        _skipCompressedBlock(reader, trees.literal, trees.distance);
      default:
        throw const GitException(
          GitErrorCode.packCorrupt,
          'deflate block type is invalid',
        );
    }
  }
  reader.alignToByte();
  if (reader.byteOffset + 4 > limit) {
    throw const GitException(
      GitErrorCode.packCorrupt,
      'zlib checksum is truncated',
    );
  }
  return reader.byteOffset + 4;
}

void _skipCompressedBlock(
  _DeflateBitReader reader,
  _Huffman literal,
  _Huffman? distance,
) {
  while (true) {
    final symbol = literal.decode(reader);
    if (symbol < 256) continue;
    if (symbol == 256) return;
    if (symbol < 257 || symbol > 285 || distance == null) {
      throw const GitException(
        GitErrorCode.packCorrupt,
        'deflate length symbol is invalid',
      );
    }
    final lengthIndex = symbol - 257;
    reader.readBits(_lengthExtraBits[lengthIndex]);
    final distanceSymbol = distance.decode(reader);
    if (distanceSymbol >= _distanceExtraBits.length) {
      throw const GitException(
        GitErrorCode.packCorrupt,
        'deflate distance symbol is invalid',
      );
    }
    reader.readBits(_distanceExtraBits[distanceSymbol]);
  }
}

final class _DynamicTrees {
  const _DynamicTrees(this.literal, this.distance);

  final _Huffman literal;
  final _Huffman? distance;
}

_DynamicTrees _readDynamicTrees(_DeflateBitReader reader) {
  final literalCount = reader.readBits(5) + 257;
  final distanceCount = reader.readBits(5) + 1;
  final codeLengthCount = reader.readBits(4) + 4;
  final order = <int>[
    16,
    17,
    18,
    0,
    8,
    7,
    9,
    6,
    10,
    5,
    11,
    4,
    12,
    3,
    13,
    2,
    14,
    1,
    15,
  ];
  final codeLengths = List<int>.filled(19, 0);
  for (var index = 0; index < codeLengthCount; index += 1) {
    codeLengths[order[index]] = reader.readBits(3);
  }
  final codeTree = _Huffman(codeLengths);
  final lengths = <int>[];
  final total = literalCount + distanceCount;
  while (lengths.length < total) {
    final symbol = codeTree.decode(reader);
    if (symbol <= 15) {
      lengths.add(symbol);
    } else if (symbol == 16) {
      if (lengths.isEmpty) {
        throw const GitException(
          GitErrorCode.packCorrupt,
          'deflate repeat has no previous code length',
        );
      }
      final count = reader.readBits(2) + 3;
      if (lengths.length + count > total) _invalidDeflateLengths();
      lengths.addAll(List<int>.filled(count, lengths.last));
    } else if (symbol == 17 || symbol == 18) {
      final count =
          reader.readBits(symbol == 17 ? 3 : 7) + (symbol == 17 ? 3 : 11);
      if (lengths.length + count > total) _invalidDeflateLengths();
      lengths.addAll(List<int>.filled(count, 0));
    } else {
      _invalidDeflateLengths();
    }
  }
  final literal = _Huffman(lengths.sublist(0, literalCount));
  if (lengths[256] == 0) {
    throw const GitException(
      GitErrorCode.packCorrupt,
      'deflate end code is missing',
    );
  }
  final distanceLengths = lengths.sublist(literalCount);
  final distance = distanceLengths.any((length) => length != 0)
      ? _Huffman(distanceLengths)
      : null;
  return _DynamicTrees(literal, distance);
}

Never _invalidDeflateLengths() => throw const GitException(
  GitErrorCode.packCorrupt,
  'deflate code lengths are invalid',
);

final class _Huffman {
  _Huffman(List<int> lengths) {
    if (lengths.any((length) => length < 0 || length > 15)) {
      _invalidDeflateLengths();
    }
    final counts = List<int>.filled(16, 0);
    for (final length in lengths) {
      if (length != 0) counts[length] += 1;
    }
    if (counts.every((count) => count == 0)) _invalidDeflateLengths();
    final nextCode = List<int>.filled(16, 0);
    var code = 0;
    for (var bits = 1; bits <= 15; bits += 1) {
      code = (code + counts[bits - 1]) << 1;
      nextCode[bits] = code;
    }
    for (var symbol = 0; symbol < lengths.length; symbol += 1) {
      final length = lengths[symbol];
      if (length == 0) continue;
      final symbolCode = nextCode[length]++;
      if (symbolCode >= 1 << length) _invalidDeflateLengths();
      _symbols[_huffmanKey(length, _reverseBits(symbolCode, length))] = symbol;
      if (length > _maximumLength) _maximumLength = length;
    }
  }

  final Map<int, int> _symbols = <int, int>{};
  var _maximumLength = 0;

  int decode(_DeflateBitReader reader) {
    var code = 0;
    for (var length = 1; length <= _maximumLength; length += 1) {
      code |= reader.readBits(1) << (length - 1);
      final symbol = _symbols[_huffmanKey(length, code)];
      if (symbol != null) return symbol;
    }
    throw const GitException(
      GitErrorCode.packCorrupt,
      'deflate code is invalid',
    );
  }
}

int _huffmanKey(int length, int code) => (length << 16) | code;

int _reverseBits(int value, int length) {
  var reversed = 0;
  for (var index = 0; index < length; index += 1) {
    reversed = (reversed << 1) | (value & 1);
    value >>= 1;
  }
  return reversed;
}

final class _DeflateBitReader {
  _DeflateBitReader(this.bytes, this.byteOffset, this.limit);

  final Uint8List bytes;
  final int limit;
  int byteOffset;
  var bitOffset = 0;

  int readBits(int count) {
    var result = 0;
    for (var bit = 0; bit < count; bit += 1) {
      if (byteOffset >= limit) {
        throw const GitException(
          GitErrorCode.packCorrupt,
          'deflate stream is truncated',
        );
      }
      result |= ((bytes[byteOffset] >> bitOffset) & 1) << bit;
      bitOffset += 1;
      if (bitOffset == 8) {
        bitOffset = 0;
        byteOffset += 1;
      }
    }
    return result;
  }

  void alignToByte() {
    if (bitOffset != 0) {
      bitOffset = 0;
      byteOffset += 1;
    }
  }

  void skipBytes(int count) {
    alignToByte();
    if (count < 0 || count > limit - byteOffset) {
      throw const GitException(
        GitErrorCode.packCorrupt,
        'stored deflate block is truncated',
      );
    }
    byteOffset += count;
  }
}

final _Huffman _fixedLiteralTree = _Huffman(<int>[
  ...List<int>.filled(144, 8),
  ...List<int>.filled(112, 9),
  ...List<int>.filled(24, 7),
  ...List<int>.filled(8, 8),
]);
final _Huffman _fixedDistanceTree = _Huffman(List<int>.filled(32, 5));

const List<int> _lengthExtraBits = <int>[
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  1,
  1,
  1,
  1,
  2,
  2,
  2,
  2,
  3,
  3,
  3,
  3,
  4,
  4,
  4,
  4,
  5,
  5,
  5,
  5,
  0,
];

const List<int> _distanceExtraBits = <int>[
  0,
  0,
  0,
  0,
  1,
  1,
  2,
  2,
  3,
  3,
  4,
  4,
  5,
  5,
  6,
  6,
  7,
  7,
  8,
  8,
  9,
  9,
  10,
  10,
  11,
  11,
  12,
  12,
  13,
  13,
];

int _readUint32(Uint8List bytes, int offset) =>
    (bytes[offset] << 24) |
    (bytes[offset + 1] << 16) |
    (bytes[offset + 2] << 8) |
    bytes[offset + 3];

int _readByte(Uint8List bytes, int offset, int limit) {
  if (offset >= limit) {
    throw const GitException(GitErrorCode.packCorrupt, 'pack is truncated');
  }
  return bytes[offset];
}
