import 'dart:typed_data';

import '../errors/git_error.dart';
import 'header_bytes.dart';
import 'git_object.dart';

final class GitAnnotatedTag {
  const GitAnnotatedTag._({
    required this.target,
    required this.targetType,
    required this.name,
    required this.message,
  });

  final GitObjectId target;
  final GitObjectType targetType;
  final String name;
  final Uint8List message;

  static GitAnnotatedTag parse(List<int> body) {
    final bytes = Uint8List.fromList(body);
    final separator = findHeaderBodySeparator(bytes);
    final headerBytes = separator == -1 ? bytes : bytes.sublist(0, separator);
    final message = separator == -1
        ? Uint8List(0)
        : Uint8List.fromList(bytes.sublist(separator + 2));
    final values = <String, String>{};
    for (final line in decodeAsciiHeaders(headerBytes).split('\n')) {
      final index = line.indexOf(' ');
      if (index <= 0 || values.containsKey(line.substring(0, index))) {
        throw const GitException(
          GitErrorCode.invalidObject,
          'tag header is invalid',
        );
      }
      values[line.substring(0, index)] = line.substring(index + 1);
    }
    final target = values['object'];
    final type = values['type'];
    final name = values['tag'];
    if (target == null ||
        type == null ||
        name == null ||
        name.isEmpty ||
        name.contains(RegExp(r'[\x00\r\n]'))) {
      throw const GitException(
        GitErrorCode.invalidObject,
        'tag headers are incomplete',
      );
    }
    return GitAnnotatedTag._(
      target: GitObjectId.parseSha1(target),
      targetType: switch (type) {
        'blob' => GitObjectType.blob,
        'tree' => GitObjectType.tree,
        'commit' => GitObjectType.commit,
        'tag' => GitObjectType.tag,
        _ => throw const GitException(
          GitErrorCode.invalidObject,
          'tag target type is invalid',
        ),
      },
      name: name,
      message: message,
    );
  }
}
