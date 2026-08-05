import 'dart:convert';
import 'dart:typed_data';

import '../errors/git_error.dart';
import 'git_object.dart';

enum GitTreeMode {
  regular('100644'),
  executable('100755'),
  symlink('120000'),
  directory('40000');

  const GitTreeMode(this.wireValue);

  final String wireValue;

  static GitTreeMode parse(String value) => switch (value) {
    '100644' => GitTreeMode.regular,
    '100755' => GitTreeMode.executable,
    '120000' => GitTreeMode.symlink,
    '40000' => GitTreeMode.directory,
    '160000' => throw const GitException(
      GitErrorCode.unsupportedRepositoryFormat,
      'gitlinks are unsupported',
    ),
    _ => throw const GitException(
      GitErrorCode.invalidTree,
      'unsupported tree mode',
    ),
  };
}

final class GitTreeEntry {
  GitTreeEntry._(this.mode, List<int> name, this.objectId)
    : name = Uint8List.fromList(name) {
    if (this.name.isEmpty ||
        this.name.contains(0) ||
        this.name.contains(0x2f)) {
      throw const GitException(
        GitErrorCode.invalidTree,
        'tree entry name is invalid',
      );
    }
  }

  final GitTreeMode mode;
  final Uint8List name;
  final GitObjectId objectId;

  factory GitTreeEntry.regular(List<int> name, GitObjectId objectId) =>
      GitTreeEntry._(GitTreeMode.regular, name, objectId);

  factory GitTreeEntry.executable(List<int> name, GitObjectId objectId) =>
      GitTreeEntry._(GitTreeMode.executable, name, objectId);

  factory GitTreeEntry.symlink(List<int> name, GitObjectId objectId) =>
      GitTreeEntry._(GitTreeMode.symlink, name, objectId);

  factory GitTreeEntry.directory(List<int> name, GitObjectId objectId) =>
      GitTreeEntry._(GitTreeMode.directory, name, objectId);
}

int compareGitTreeEntries(GitTreeEntry left, GitTreeEntry right) {
  final leftName = <int>[
    ...left.name,
    if (left.mode == GitTreeMode.directory) 0x2f,
  ];
  final rightName = <int>[
    ...right.name,
    if (right.mode == GitTreeMode.directory) 0x2f,
  ];
  for (
    var index = 0;
    index < leftName.length && index < rightName.length;
    index += 1
  ) {
    final comparison = leftName[index] - rightName[index];
    if (comparison != 0) return comparison;
  }
  return leftName.length - rightName.length;
}

final class GitTree {
  GitTree(Iterable<GitTreeEntry> entries)
    : entries = List<GitTreeEntry>.unmodifiable(
        List<GitTreeEntry>.of(entries)..sort(compareGitTreeEntries),
      ) {
    _validateUnique(this.entries);
  }

  final List<GitTreeEntry> entries;

  static GitTree parse(List<int> body) {
    final bytes = Uint8List.fromList(body);
    final parsed = <GitTreeEntry>[];
    var offset = 0;
    while (offset < bytes.length) {
      final modeEnd = bytes.indexOf(0x20, offset);
      if (modeEnd <= offset) {
        throw const GitException(
          GitErrorCode.invalidTree,
          'tree mode is missing',
        );
      }
      final nameEnd = bytes.indexOf(0, modeEnd + 1);
      if (nameEnd < modeEnd + 1 || nameEnd + 21 > bytes.length) {
        throw const GitException(
          GitErrorCode.invalidTree,
          'tree entry is truncated',
        );
      }
      final modeText = ascii.decode(bytes.sublist(offset, modeEnd));
      final entry = GitTreeEntry._(
        GitTreeMode.parse(modeText),
        bytes.sublist(modeEnd + 1, nameEnd),
        GitObjectId.parseSha1(
          bytes
              .sublist(nameEnd + 1, nameEnd + 21)
              .map((int byte) => byte.toRadixString(16).padLeft(2, '0'))
              .join(),
        ),
      );
      if (parsed.isNotEmpty && compareGitTreeEntries(parsed.last, entry) >= 0) {
        throw const GitException(
          GitErrorCode.invalidTree,
          'tree entries are not strictly sorted',
        );
      }
      parsed.add(entry);
      offset = nameEnd + 21;
    }
    return GitTree._parsed(parsed);
  }

  GitTree._parsed(List<GitTreeEntry> entries)
    : entries = List<GitTreeEntry>.unmodifiable(entries);

  Uint8List encode() {
    final output = BytesBuilder(copy: false);
    for (final entry in entries) {
      output.add(ascii.encode('${entry.mode.wireValue} '));
      output.add(entry.name);
      output.addByte(0);
      output.add(entry.objectId.bytes);
    }
    return output.takeBytes();
  }

  static void _validateUnique(List<GitTreeEntry> entries) {
    for (var index = 1; index < entries.length; index += 1) {
      if (compareGitTreeEntries(entries[index - 1], entries[index]) == 0) {
        throw const GitException(
          GitErrorCode.invalidTree,
          'duplicate tree entry',
        );
      }
    }
  }
}
