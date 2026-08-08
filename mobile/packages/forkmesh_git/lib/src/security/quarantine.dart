import 'dart:io';
import 'dart:math';

import '../errors/git_error.dart';
import 'cancellation.dart';
import 'resource_limits.dart';

/// Private operation storage for future untrusted transport input.
///
/// G4.5 deliberately exposes no installation method. A later reviewed phase
/// must validate every staged object and perform a separately audited rename.
final class GitObjectQuarantine {
  GitObjectQuarantine._(
    this.directory,
    this._cancellation,
    this._maximumStagedBytes,
  );

  final Directory directory;
  final GitCancellationToken? _cancellation;
  final int _maximumStagedBytes;

  static Future<GitObjectQuarantine> begin(
    Directory gitDirectory, {
    GitCancellationToken? cancellation,
    GitResourceLimits? limits,
  }) async {
    final root = Directory(
      '${gitDirectory.path}${Platform.pathSeparator}forkmesh-quarantine',
    );
    await root.create(recursive: true);
    for (var attempt = 0; attempt < 32; attempt += 1) {
      final name =
          '${DateTime.now().microsecondsSinceEpoch}-'
          '${Random.secure().nextInt(1 << 32).toRadixString(16)}-$attempt';
      final directory = Directory('${root.path}${Platform.pathSeparator}$name');
      if (await directory.exists()) continue;
      await directory.create();
      return GitObjectQuarantine._(
        directory,
        cancellation,
        limits?.maxInflatedBytes ?? GitResourceLimits.defaultMaxInflatedBytes,
      );
    }
    throw const GitException(
      GitErrorCode.quarantineRejected,
      'could not allocate operation quarantine',
    );
  }

  Future<File> stage(String name, List<int> bytes) async {
    if (!RegExp(r'^[A-Za-z0-9][A-Za-z0-9._-]{0,127}$').hasMatch(name)) {
      throw const GitException(
        GitErrorCode.quarantineRejected,
        'quarantine entry name is invalid',
      );
    }
    try {
      _cancellation?.throwIfCancelled();
      if (bytes.length > _maximumStagedBytes) {
        throw const GitException(
          GitErrorCode.resourceLimitExceeded,
          'quarantine entry exceeds the operation limit',
        );
      }
      final destination = File(
        '${directory.path}${Platform.pathSeparator}$name',
      );
      if (await destination.exists()) {
        throw const GitException(
          GitErrorCode.quarantineRejected,
          'quarantine entry already exists',
        );
      }
      await destination.writeAsBytes(bytes, flush: true);
      _cancellation?.throwIfCancelled();
      return destination;
    } on Object {
      await reject();
      rethrow;
    }
  }

  Future<void> reject() async {
    if (await directory.exists()) await directory.delete(recursive: true);
  }

  static Future<void> recover(Directory gitDirectory) async {
    final root = Directory(
      '${gitDirectory.path}${Platform.pathSeparator}forkmesh-quarantine',
    );
    if (!await root.exists()) return;
    await for (final entity in root.list(followLinks: false)) {
      await entity.delete(recursive: true);
    }
  }
}
