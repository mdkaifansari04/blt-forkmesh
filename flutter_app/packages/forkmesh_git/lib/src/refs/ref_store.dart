import 'dart:convert';
import 'dart:io';

import '../errors/git_error.dart';
import '../objects/git_object.dart';
import '../security/resource_limits.dart';

final class GitRefName {
  GitRefName._(this.value);

  final String value;

  factory GitRefName.branch(String value) {
    final ref = GitRefName._(value);
    if (!value.startsWith('refs/heads/')) {
      throw const GitException(GitErrorCode.invalidRef, 'expected branch ref');
    }
    ref._validate();
    return ref;
  }

  factory GitRefName.parse(String value) {
    final ref = GitRefName._(value);
    if (!value.startsWith('refs/heads/') &&
        !value.startsWith('refs/tags/') &&
        !value.startsWith('refs/remotes/')) {
      throw const GitException(
        GitErrorCode.invalidRef,
        'unsupported ref namespace',
      );
    }
    ref._validate();
    return ref;
  }

  void _validate() {
    if (utf8.encode(value).length > GitResourceLimits.hardMaxRefNameBytes ||
        value.endsWith('/') ||
        value.endsWith('.') ||
        value.endsWith('.lock') ||
        value.contains('..') ||
        value.contains('//') ||
        RegExp(r'[\x00-\x20~^:?*\\[\\]').hasMatch(value)) {
      throw const GitException(GitErrorCode.invalidRef, 'invalid ref spelling');
    }
    for (final component in value.split('/')) {
      if (component.isEmpty || component == '.' || component == '..') {
        throw const GitException(
          GitErrorCode.invalidRef,
          'invalid ref component',
        );
      }
    }
  }

  @override
  bool operator ==(Object other) => other is GitRefName && other.value == value;

  @override
  int get hashCode => value.hashCode;
}

final class GitSymbolicRef {
  const GitSymbolicRef(this.target);

  final String target;

  @override
  bool operator ==(Object other) =>
      other is GitSymbolicRef && other.target == target;

  @override
  int get hashCode => target.hashCode;
}

enum GitRefUpdateResult { updated, nonFastForward }

final class RefStore {
  RefStore(this.gitDirectory);

  final Directory gitDirectory;

  File _fileFor(GitRefName ref) =>
      File('${gitDirectory.path}${Platform.pathSeparator}${ref.value}');

  Future<GitObjectId?> readDirect(GitRefName ref) =>
      _readOptional(_fileFor(ref));

  Future<GitRefUpdateResult> update(
    GitRefName ref, {
    required GitObjectId? expectedOld,
    required GitObjectId next,
  }) async {
    final destination = _fileFor(ref);
    final lock = File('${destination.path}.lock');
    await lock.parent.create(recursive: true);
    try {
      await lock.create(exclusive: true);
    } on FileSystemException {
      throw const GitException(
        GitErrorCode.refLocked,
        'ref lock already exists',
      );
    }
    try {
      final current = await _readOptional(destination);
      if (current != expectedOld) return GitRefUpdateResult.nonFastForward;
      await lock.writeAsString('${next.hex}\n', flush: true);
      await lock.rename(destination.path);
      return GitRefUpdateResult.updated;
    } finally {
      if (await lock.exists()) await lock.delete();
    }
  }

  Future<GitObjectId?> _readOptional(File file) async {
    if (!await file.exists()) return null;
    final text = await file.readAsString();
    final value = text.trim();
    if (value.startsWith('ref: ')) {
      throw const GitException(GitErrorCode.invalidRef, 'expected direct ref');
    }
    return GitObjectId.parseSha1(value);
  }
}

final class PackedRef {
  const PackedRef(this.objectId, this.peeled);

  final GitObjectId objectId;
  final GitObjectId? peeled;
}

final class PackedRefs {
  const PackedRefs._(this._entries);

  final Map<String, PackedRef> _entries;

  static PackedRefs parse(List<int> bytes, GitResourceLimits limits) {
    final text = ascii.decode(bytes);
    final entries = <String, PackedRef>{};
    String? previousName;
    for (final line in const LineSplitter().convert(text)) {
      if (line.isEmpty || line.startsWith('#')) continue;
      if (line.startsWith('^')) {
        if (previousName == null || entries[previousName]!.peeled != null) {
          throw const GitException(
            GitErrorCode.invalidRef,
            'invalid peeled ref',
          );
        }
        final current = entries[previousName]!;
        entries[previousName] = PackedRef(
          current.objectId,
          GitObjectId.parseSha1(line.substring(1)),
        );
        continue;
      }
      final separator = line.indexOf(' ');
      if (separator != 40 ||
          separator == line.length - 1 ||
          entries.length >= limits.maxRefCount) {
        throw const GitException(
          GitErrorCode.invalidRef,
          'packed ref record is invalid',
        );
      }
      final name = line.substring(separator + 1);
      GitRefName.parse(name);
      if (previousName != null && previousName.compareTo(name) >= 0) {
        throw const GitException(
          GitErrorCode.invalidRef,
          'packed refs are not sorted',
        );
      }
      entries[name] = PackedRef(
        GitObjectId.parseSha1(line.substring(0, separator)),
        null,
      );
      previousName = name;
    }
    return PackedRefs._(Map.unmodifiable(entries));
  }

  PackedRef? lookup(String name) => _entries[name];
}
