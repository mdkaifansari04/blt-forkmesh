import 'dart:io';

import '../crypto/collision_screen.dart';
import '../errors/git_error.dart';
import '../objects/git_object.dart';
import '../security/resource_limits.dart';
import 'pack_index_v2.dart';
import 'pack_reader.dart';

final class GitPackObjectStore {
  GitPackObjectStore(this.gitDirectory, this.limits, {this.detector});

  final Directory gitDirectory;
  final GitResourceLimits limits;
  final GitSha1CollisionDetector? detector;

  Future<GitObjectFrame> read(GitObjectId objectId) async {
    final directory = Directory(
      '${gitDirectory.path}${Platform.pathSeparator}objects'
      '${Platform.pathSeparator}pack',
    );
    if (!await directory.exists()) {
      throw const GitException(GitErrorCode.invalidObject, 'object is absent');
    }
    await for (final entity in directory.list(followLinks: false)) {
      if (entity is! File || !entity.path.endsWith('.idx')) continue;
      final index = GitPackIndexV2.parse(await entity.readAsBytes(), limits);
      final indexEntry = index.lookup(objectId);
      if (indexEntry == null) continue;
      final packPath =
          '${entity.path.substring(0, entity.path.length - 4)}.pack';
      final packFile = File(packPath);
      if (!await packFile.exists()) {
        throw const GitException(
          GitErrorCode.packCorrupt,
          'pack file is missing',
        );
      }
      final bytes = await packFile.readAsBytes();
      if (bytes.length < 20 ||
          !_equalBytes(index.packChecksum, bytes.sublist(bytes.length - 20))) {
        throw const GitException(
          GitErrorCode.packChecksumMismatch,
          'pack and index checksums do not match',
        );
      }
      final object = GitPackReader.parse(
        bytes,
        limits,
        detector: detector,
      ).lookup(objectId);
      if (object == null || object.offset != indexEntry.offset) {
        throw const GitException(
          GitErrorCode.packCorrupt,
          'pack index object offset does not match pack',
        );
      }
      return GitObjectFrame(object.type, object.body);
    }
    throw const GitException(GitErrorCode.invalidObject, 'object is absent');
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
