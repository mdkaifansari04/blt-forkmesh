import 'dart:typed_data';

import '../errors/git_error.dart';
import 'header_bytes.dart';
import 'git_object.dart';

final class GitCommit {
  const GitCommit._({
    required this.tree,
    required this.parents,
    required this.headers,
    required this.message,
  });

  final GitObjectId tree;
  final List<GitObjectId> parents;
  final Map<String, List<String>> headers;
  final Uint8List message;

  static GitCommit parse(List<int> body) {
    final bytes = Uint8List.fromList(body);
    final separator = findHeaderBodySeparator(bytes);
    final headerBytes = separator == -1 ? bytes : bytes.sublist(0, separator);
    final message = separator == -1
        ? Uint8List(0)
        : Uint8List.fromList(bytes.sublist(separator + 2));
    final headerText = decodeAsciiHeaders(headerBytes);
    final headers = <String, List<String>>{};
    String? previousKey;
    for (final line in headerText.split('\n')) {
      if (line.startsWith(' ')) {
        if (previousKey == null) {
          throw const GitException(
            GitErrorCode.invalidObject,
            'orphan header continuation',
          );
        }
        headers[previousKey]![headers[previousKey]!.length - 1] +=
            '\n${line.substring(1)}';
        continue;
      }
      final separatorIndex = line.indexOf(' ');
      if (separatorIndex <= 0) {
        throw const GitException(
          GitErrorCode.invalidObject,
          'commit header is invalid',
        );
      }
      final key = line.substring(0, separatorIndex);
      final value = line.substring(separatorIndex + 1);
      headers.putIfAbsent(key, () => <String>[]).add(value);
      previousKey = key;
    }
    final treeValues = headers['tree'];
    if (treeValues == null || treeValues.length != 1) {
      throw const GitException(
        GitErrorCode.invalidObject,
        'commit must have one tree',
      );
    }
    if (headers['author']?.length != 1 || headers['committer']?.length != 1) {
      throw const GitException(
        GitErrorCode.invalidObject,
        'commit identity headers are invalid',
      );
    }
    final parents = List<GitObjectId>.unmodifiable(<GitObjectId>[
      for (final value in headers['parent'] ?? <String>[])
        GitObjectId.parseSha1(value),
    ]);
    return GitCommit._(
      tree: GitObjectId.parseSha1(treeValues.single),
      parents: parents,
      headers: Map<String, List<String>>.unmodifiable(
        headers.map(
          (key, value) => MapEntry(key, List<String>.unmodifiable(value)),
        ),
      ),
      message: message,
    );
  }
}
