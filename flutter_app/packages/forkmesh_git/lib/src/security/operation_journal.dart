import 'dart:io';

import '../errors/git_error.dart';

final class GitOperationJournal {
  GitOperationJournal._(this._file);

  final File _file;

  static Future<GitOperationJournal> begin(
    Directory gitDirectory,
    String operation,
  ) async {
    if (!RegExp(r'^[a-z][a-z-]{0,63}$').hasMatch(operation)) {
      throw ArgumentError.value(operation, 'operation');
    }
    final directory = Directory(
      '${gitDirectory.path}${Platform.pathSeparator}forkmesh-journal',
    );
    await directory.create(recursive: true);
    for (var attempt = 0; attempt < 32; attempt += 1) {
      final file = File(
        '${directory.path}${Platform.pathSeparator}'
        '${DateTime.now().microsecondsSinceEpoch}-$attempt.journal',
      );
      try {
        await file.create(exclusive: true);
      } on FileSystemException {
        continue;
      }
      await file.writeAsString(
        'forkmesh-journal-v1\noperation=$operation\nstate=prepared\n',
        flush: true,
      );
      return GitOperationJournal._(file);
    }
    throw const GitException(
      GitErrorCode.refLocked,
      'could not allocate operation journal',
    );
  }

  Future<void> complete() async {
    if (await _file.exists()) await _file.delete();
  }

  static Future<void> recover(Directory gitDirectory) async {
    final directory = Directory(
      '${gitDirectory.path}${Platform.pathSeparator}forkmesh-journal',
    );
    if (!await directory.exists()) return;
    await for (final entity in directory.list(followLinks: false)) {
      if (entity is! File || !entity.path.endsWith('.journal')) {
        throw const GitException(
          GitErrorCode.operationInterrupted,
          'operation journal directory is malformed',
        );
      }
      final bytes = await entity.readAsBytes();
      if (bytes.length > 1024 || !_isValid(bytes)) {
        throw const GitException(
          GitErrorCode.operationInterrupted,
          'operation journal entry is malformed',
        );
      }
      await entity.delete();
    }
  }

  static bool _isValid(List<int> bytes) {
    final text = String.fromCharCodes(bytes);
    return RegExp(
      r'^forkmesh-journal-v1\noperation=[a-z][a-z-]{0,63}\nstate=prepared\n$',
    ).hasMatch(text);
  }
}
