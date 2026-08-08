import 'dart:convert';
import 'dart:typed_data';

import '../crypto/sha1.dart';
import '../errors/git_error.dart';
import '../objects/git_object.dart';
import '../security/resource_limits.dart';

enum GitIndexMode {
  regular(33188),
  executable(33261),
  symlink(40960);

  const GitIndexMode(this.fileMode);

  final int fileMode;

  static GitIndexMode parse(int fileMode) => switch (fileMode) {
    33188 => GitIndexMode.regular,
    33261 => GitIndexMode.executable,
    40960 => GitIndexMode.symlink,
    57344 => throw const GitException(
      GitErrorCode.unsupportedRepositoryFormat,
      'gitlinks are unsupported',
    ),
    _ => throw const GitException(
      GitErrorCode.indexCorrupt,
      'unsupported index file mode',
    ),
  };
}

final class GitIndexEntry {
  GitIndexEntry({
    required List<int> path,
    required this.objectId,
    required this.mode,
    this.size = 0,
    this.stage = 0,
  }) : path = Uint8List.fromList(path) {
    if (size < 0 || size > 0xffffffff) {
      throw ArgumentError.value(size, 'size', 'must fit in uint32');
    }
    if (stage != 0) {
      throw const GitException(
        GitErrorCode.unsupportedRepositoryFormat,
        'conflict index stages are unsupported',
      );
    }
    _validatePath(this.path);
  }

  final Uint8List path;
  final GitObjectId objectId;
  final GitIndexMode mode;
  final int size;
  final int stage;
}

final class GitIndexV2 {
  GitIndexV2(Iterable<GitIndexEntry> entries)
    : entries = List<GitIndexEntry>.unmodifiable(
        List<GitIndexEntry>.of(entries)..sort(_comparePaths),
      ) {
    for (var index = 1; index < this.entries.length; index += 1) {
      if (_comparePaths(this.entries[index - 1], this.entries[index]) == 0) {
        throw const GitException(
          GitErrorCode.indexCorrupt,
          'duplicate index path',
        );
      }
    }
  }

  final List<GitIndexEntry> entries;

  static GitIndexV2 parse(List<int> input, GitResourceLimits limits) {
    final bytes = Uint8List.fromList(input);
    if (bytes.length < 32) {
      throw const GitException(GitErrorCode.indexCorrupt, 'index is truncated');
    }
    final checksumOffset = bytes.length - 20;
    _requireDigest(bytes, checksumOffset);
    if (!_equalBytes(bytes.sublist(0, 4), ascii.encode('DIRC'))) {
      throw const GitException(
        GitErrorCode.indexCorrupt,
        'index signature is invalid',
      );
    }
    if (_readUint32(bytes, 4) != 2) {
      throw const GitException(
        GitErrorCode.unsupportedRepositoryFormat,
        'only index version 2 is supported',
      );
    }
    final count = _readUint32(bytes, 8);
    if (count > GitResourceLimits.hardMaxRefCount ||
        count > limits.maxRefCount) {
      throw const GitException(
        GitErrorCode.resourceLimitExceeded,
        'index entry count exceeds configured limit',
      );
    }
    var offset = 12;
    final entries = <GitIndexEntry>[];
    for (var item = 0; item < count; item += 1) {
      final entryOffset = offset;
      if (checksumOffset - offset < 62) {
        throw const GitException(
          GitErrorCode.indexCorrupt,
          'index entry is truncated',
        );
      }
      final mode = GitIndexMode.parse(_readUint32(bytes, offset + 24));
      final size = _readUint32(bytes, offset + 36);
      final id = GitObjectId.parseSha1(
        bytes
            .sublist(offset + 40, offset + 60)
            .map((byte) => byte.toRadixString(16).padLeft(2, '0'))
            .join(),
      );
      final flags = _readUint16(bytes, offset + 60);
      final stage = (flags >> 12) & 0x3;
      if (stage != 0 || flags & 0xc000 != 0) {
        throw const GitException(
          GitErrorCode.unsupportedRepositoryFormat,
          'conflict or extended index entry is unsupported',
        );
      }
      final pathStart = offset + 62;
      final nul = bytes.indexOf(0, pathStart);
      if (nul < 0 || nul >= checksumOffset) {
        throw const GitException(
          GitErrorCode.indexCorrupt,
          'index path is unterminated',
        );
      }
      final path = bytes.sublist(pathStart, nul);
      final declaredLength = flags & 0x0fff;
      if (declaredLength != 0x0fff && declaredLength != path.length) {
        throw const GitException(
          GitErrorCode.indexCorrupt,
          'index path length flag is invalid',
        );
      }
      final entryLength = nul + 1 - entryOffset;
      offset = nul + 1 + ((8 - entryLength % 8) % 8);
      if (offset > checksumOffset) {
        throw const GitException(
          GitErrorCode.indexCorrupt,
          'index entry padding is truncated',
        );
      }
      entries.add(
        GitIndexEntry(
          path: path,
          objectId: id,
          mode: mode,
          size: size,
          stage: stage,
        ),
      );
    }
    _skipExtensions(bytes, offset, checksumOffset, limits);
    final parsed = GitIndexV2._parsed(entries);
    for (var index = 1; index < parsed.entries.length; index += 1) {
      if (_comparePaths(parsed.entries[index - 1], parsed.entries[index]) >=
          0) {
        throw const GitException(
          GitErrorCode.indexCorrupt,
          'index entries are not strictly sorted',
        );
      }
    }
    return parsed;
  }

  GitIndexV2._parsed(List<GitIndexEntry> entries)
    : entries = List<GitIndexEntry>.unmodifiable(entries);

  Uint8List encode() {
    final output = BytesBuilder(copy: false)
      ..add(ascii.encode('DIRC'))
      ..add(_uint32(2))
      ..add(_uint32(entries.length));
    for (final entry in entries) {
      final entryOffset = output.length;
      for (var field = 0; field < 6; field += 1) {
        output.add(_uint32(0));
      }
      output
        ..add(_uint32(entry.mode.fileMode))
        ..add(_uint32(0))
        ..add(_uint32(0))
        ..add(_uint32(entry.size))
        ..add(entry.objectId.bytes);
      final pathLength = entry.path.length > 0xffe ? 0xfff : entry.path.length;
      output
        ..add(_uint16(pathLength))
        ..add(entry.path)
        ..addByte(0);
      while ((output.length - entryOffset) % 8 != 0) {
        output.addByte(0);
      }
    }
    final withoutChecksum = output.takeBytes();
    final digest = ForkMeshSha1()..add(withoutChecksum);
    return Uint8List.fromList(<int>[...withoutChecksum, ...digest.close()]);
  }
}

void _skipExtensions(
  Uint8List bytes,
  int offset,
  int checksumOffset,
  GitResourceLimits limits,
) {
  while (offset < checksumOffset) {
    if (checksumOffset - offset < 8) {
      throw const GitException(
        GitErrorCode.indexCorrupt,
        'index extension header is truncated',
      );
    }
    final signature = bytes.sublist(offset, offset + 4);
    final length = _readUint32(bytes, offset + 4);
    if (length > limits.maxInflatedBytes ||
        length > checksumOffset - offset - 8) {
      throw const GitException(
        GitErrorCode.resourceLimitExceeded,
        'index extension length is invalid',
      );
    }
    if (signature.first < 0x41 || signature.first > 0x5a) {
      throw const GitException(
        GitErrorCode.unsupportedRepositoryFormat,
        'unknown mandatory index extension',
      );
    }
    offset += 8 + length;
  }
}

void _validatePath(Uint8List path) {
  if (path.isEmpty || path.length > 4096 || path.contains(0)) {
    throw const GitException(GitErrorCode.pathEscapesWorktree, 'invalid path');
  }
  String value;
  try {
    value = utf8.decode(path);
  } on FormatException {
    throw const GitException(
      GitErrorCode.pathEscapesWorktree,
      'path is not valid UTF-8',
    );
  }
  if (value.startsWith('/') ||
      value.startsWith('\\') ||
      value.contains('\\') ||
      value
          .split('/')
          .any((part) => part.isEmpty || part == '.' || part == '..')) {
    throw const GitException(
      GitErrorCode.pathEscapesWorktree,
      'path escapes worktree',
    );
  }
}

int _comparePaths(GitIndexEntry left, GitIndexEntry right) {
  for (
    var index = 0;
    index < left.path.length && index < right.path.length;
    index += 1
  ) {
    final comparison = left.path[index] - right.path[index];
    if (comparison != 0) return comparison;
  }
  return left.path.length - right.path.length;
}

int _readUint16(Uint8List bytes, int offset) =>
    (bytes[offset] << 8) | bytes[offset + 1];

int _readUint32(Uint8List bytes, int offset) =>
    (bytes[offset] << 24) |
    (bytes[offset + 1] << 16) |
    (bytes[offset + 2] << 8) |
    bytes[offset + 3];

Uint8List _uint16(int value) => Uint8List.fromList(<int>[value >> 8, value]);

Uint8List _uint32(int value) =>
    Uint8List.fromList(<int>[value >> 24, value >> 16, value >> 8, value]);

void _requireDigest(Uint8List bytes, int checksumOffset) {
  final digest = ForkMeshSha1()..add(bytes.sublist(0, checksumOffset));
  if (!_equalBytes(digest.close(), bytes.sublist(checksumOffset))) {
    throw const GitException(
      GitErrorCode.indexCorrupt,
      'index checksum does not match',
    );
  }
}

bool _equalBytes(List<int> left, List<int> right) {
  if (left.length != right.length) return false;
  var different = 0;
  for (var index = 0; index < left.length; index += 1) {
    different |= left[index] ^ right[index];
  }
  return different == 0;
}
