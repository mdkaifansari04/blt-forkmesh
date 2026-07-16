import 'dart:convert';
import 'dart:typed_data';

import '../bytes/encoding.dart';
import '../crypto/sha1.dart';
import '../errors/git_error.dart';
import '../security/resource_limits.dart';

enum GitObjectType { blob, tree, commit, tag }

final class GitObjectId {
  GitObjectId._(this._bytes);

  final Uint8List _bytes;

  factory GitObjectId.parseSha1(String value) {
    final bytes = decodeLowerHex(value);
    if (bytes.length != 20) {
      throw const GitException(
        GitErrorCode.invalidObject,
        'expected SHA-1 object ID',
      );
    }
    return GitObjectId._(bytes);
  }

  factory GitObjectId.fromSha1Bytes(List<int> value) {
    if (value.length != 20) {
      throw const GitException(
        GitErrorCode.invalidObject,
        'expected SHA-1 object ID',
      );
    }
    return GitObjectId._(Uint8List.fromList(value));
  }

  factory GitObjectId.sha1Of(List<int> canonicalBytes) {
    final digest = ForkMeshSha1()..add(canonicalBytes);
    return GitObjectId._(digest.close());
  }

  Uint8List get bytes => Uint8List.fromList(_bytes);
  String get hex => encodeLowerHex(_bytes);

  @override
  bool operator ==(Object other) =>
      other is GitObjectId && _constantTimeEquals(_bytes, other._bytes);

  @override
  int get hashCode => Object.hashAll(_bytes);

  @override
  String toString() => hex;
}

bool _constantTimeEquals(Uint8List left, Uint8List right) {
  if (left.length != right.length) return false;
  var different = 0;
  for (var index = 0; index < left.length; index += 1) {
    different |= left[index] ^ right[index];
  }
  return different == 0;
}

final class GitObjectFrame {
  const GitObjectFrame(this.type, this.body);

  final GitObjectType type;
  final Uint8List body;

  static Uint8List encode(GitObjectType type, List<int> body) =>
      Uint8List.fromList(<int>[
        ...ascii.encode('${type.name} ${body.length}\u0000'),
        ...body,
      ]);

  static GitObjectFrame decode(List<int> bytes, GitResourceLimits limits) {
    final separator = bytes.indexOf(0);
    if (separator <= 0) {
      throw const GitException(
        GitErrorCode.invalidObject,
        'object header is missing NUL',
      );
    }
    String header;
    try {
      header = ascii.decode(bytes.sublist(0, separator));
    } on FormatException {
      throw const GitException(
        GitErrorCode.invalidObject,
        'object header is not ASCII',
      );
    }
    final separatorIndex = header.indexOf(' ');
    if (separatorIndex <= 0 || separatorIndex != header.lastIndexOf(' ')) {
      throw const GitException(
        GitErrorCode.invalidObject,
        'object header is not canonical',
      );
    }
    final typeText = header.substring(0, separatorIndex);
    final sizeText = header.substring(separatorIndex + 1);
    if (!RegExp(r'^(0|[1-9][0-9]*)$').hasMatch(sizeText)) {
      throw const GitException(
        GitErrorCode.invalidObject,
        'object size is not canonical',
      );
    }
    final declaredSize = int.parse(sizeText);
    final actualSize = bytes.length - separator - 1;
    if (declaredSize > limits.maxObjectBytes ||
        actualSize > limits.maxObjectBytes) {
      throw const GitException(
        GitErrorCode.resourceLimitExceeded,
        'object exceeds configured limit',
      );
    }
    if (declaredSize != actualSize) {
      throw const GitException(
        GitErrorCode.invalidObject,
        'object body length does not match header',
      );
    }
    final type = switch (typeText) {
      'blob' => GitObjectType.blob,
      'tree' => GitObjectType.tree,
      'commit' => GitObjectType.commit,
      'tag' => GitObjectType.tag,
      _ => throw const GitException(
        GitErrorCode.invalidObject,
        'unsupported object type',
      ),
    };
    return GitObjectFrame(
      type,
      Uint8List.fromList(bytes.sublist(separator + 1)),
    );
  }
}
