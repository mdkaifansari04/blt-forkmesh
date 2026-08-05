import 'dart:io';

import '../errors/git_error.dart';
import '../security/resource_limits.dart';
import 'index_v2.dart';

final class GitIndexStore {
  GitIndexStore(this.gitDirectory, this.limits);

  final Directory gitDirectory;
  final GitResourceLimits limits;

  File get file => File('${gitDirectory.path}${Platform.pathSeparator}index');

  Future<GitIndexV2> read() async {
    if (!await file.exists()) return GitIndexV2(<GitIndexEntry>[]);
    return GitIndexV2.parse(await file.readAsBytes(), limits);
  }

  Future<void> write(GitIndexV2 index) async {
    final destination = file;
    final lock = File('${destination.path}.lock');
    try {
      await lock.create(exclusive: true);
    } on FileSystemException {
      throw const GitException(
        GitErrorCode.refLocked,
        'index lock already exists',
      );
    }
    try {
      await lock.writeAsBytes(index.encode(), flush: true);
      await lock.rename(destination.path);
    } finally {
      if (await lock.exists()) await lock.delete();
    }
  }
}
