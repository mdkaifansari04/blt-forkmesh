import 'dart:convert';
import 'dart:io';
import 'dart:typed_data';

import '../errors/git_error.dart';

final class GitWorktreePath {
  GitWorktreePath(String value)
    : value = value,
      bytes = Uint8List.fromList(utf8.encode(value)),
      components = List<String>.unmodifiable(value.split('/')) {
    if (value.isEmpty ||
        value.startsWith('/') ||
        value.startsWith('\\') ||
        bytes.length > 4096 ||
        value.contains('\\') ||
        value.codeUnits.any(
          (unit) => unit == 0 || unit < 0x20 || unit == 0x7f,
        )) {
      throw const GitException(
        GitErrorCode.pathEscapesWorktree,
        'path is not portable',
      );
    }
    for (final component in components) {
      if (component.isEmpty ||
          component == '.' ||
          component == '..' ||
          utf8.encode(component).length > 255 ||
          component.endsWith('.') ||
          component.endsWith(' ') ||
          component.contains(':') ||
          _isReservedDeviceName(component)) {
        throw const GitException(
          GitErrorCode.pathEscapesWorktree,
          'path is not portable',
        );
      }
    }
  }

  final String value;
  final Uint8List bytes;
  final List<String> components;

  @override
  bool operator ==(Object other) =>
      other is GitWorktreePath && other.value == value;

  @override
  int get hashCode => value.hashCode;
}

final class GitWorktree {
  GitWorktree(this.root);

  final Directory root;

  Future<void> writeFile(GitWorktreePath path, List<int> bytes) async {
    final file = await _safeFile(path, createParents: true);
    await file.writeAsBytes(bytes, flush: true);
  }

  Future<Uint8List> readFile(GitWorktreePath path) async {
    final file = await _safeFile(path, createParents: false);
    if (!await file.exists()) {
      throw const GitException(
        GitErrorCode.invalidRepository,
        'worktree file is missing',
      );
    }
    return Uint8List.fromList(await file.readAsBytes());
  }

  Future<String> readSymlink(GitWorktreePath path) async {
    final file = await _safeFile(
      path,
      createParents: false,
      allowFinalSymlink: true,
    );
    if (await FileSystemEntity.type(file.path, followLinks: false) !=
        FileSystemEntityType.link) {
      throw const GitException(
        GitErrorCode.invalidRepository,
        'worktree path is not a symlink',
      );
    }
    final target = await Link(file.path).target();
    _validateSymlinkTarget(target);
    return target;
  }

  Future<void> deleteFile(GitWorktreePath path) async {
    final file = await _safeFile(path, createParents: false);
    if (await file.exists()) await file.delete();
  }

  Future<void> writeSymlink(GitWorktreePath path, String target) async {
    _validateSymlinkTarget(target);
    final file = await _safeFile(
      path,
      createParents: true,
      allowFinalSymlink: true,
    );
    final type = await FileSystemEntity.type(file.path, followLinks: false);
    if (type == FileSystemEntityType.directory) {
      throw const GitException(
        GitErrorCode.pathEscapesWorktree,
        'cannot replace a directory with a symlink',
      );
    }
    if (type != FileSystemEntityType.notFound) await file.delete();
    await Link(file.path).create(target);
  }

  Future<FileSystemEntityType> typeOf(GitWorktreePath path) async {
    final file = await _safeFile(
      path,
      createParents: false,
      allowFinalSymlink: true,
    );
    return FileSystemEntity.type(file.path, followLinks: false);
  }

  Future<File> _safeFile(
    GitWorktreePath path, {
    required bool createParents,
    bool allowFinalSymlink = false,
  }) async {
    if (await FileSystemEntity.type(root.path, followLinks: false) ==
        FileSystemEntityType.link) {
      throw const GitException(
        GitErrorCode.symlinkEscape,
        'worktree root must not be a symlink',
      );
    }
    var current = root;
    for (final component in path.components.take(path.components.length - 1)) {
      final next = Directory(
        '${current.path}${Platform.pathSeparator}$component',
      );
      var type = await FileSystemEntity.type(next.path, followLinks: false);
      if (type == FileSystemEntityType.link) {
        throw const GitException(
          GitErrorCode.symlinkEscape,
          'worktree parent is a symlink',
        );
      }
      if (type == FileSystemEntityType.notFound && createParents) {
        await next.create();
        type = await FileSystemEntity.type(next.path, followLinks: false);
      }
      if (type != FileSystemEntityType.directory) {
        throw const GitException(
          GitErrorCode.pathEscapesWorktree,
          'worktree parent is not a directory',
        );
      }
      current = next;
    }
    final file = File(
      '${current.path}${Platform.pathSeparator}${path.components.last}',
    );
    if (!allowFinalSymlink &&
        await FileSystemEntity.type(file.path, followLinks: false) ==
            FileSystemEntityType.link) {
      throw const GitException(
        GitErrorCode.symlinkEscape,
        'worktree destination is a symlink',
      );
    }
    return file;
  }
}

bool _isReservedDeviceName(String component) {
  final stem = component.split('.').first.toUpperCase();
  return stem == 'CON' ||
      stem == 'PRN' ||
      stem == 'AUX' ||
      stem == 'NUL' ||
      RegExp(r'^(COM|LPT)[1-9]$').hasMatch(stem);
}

void _validateSymlinkTarget(String target) {
  if (target.isEmpty ||
      target.startsWith('/') ||
      target.startsWith('\\') ||
      target.contains('\\') ||
      target
          .split('/')
          .any((part) => part.isEmpty || part == '.' || part == '..')) {
    throw const GitException(
      GitErrorCode.symlinkEscape,
      'symlink target escapes worktree',
    );
  }
}
