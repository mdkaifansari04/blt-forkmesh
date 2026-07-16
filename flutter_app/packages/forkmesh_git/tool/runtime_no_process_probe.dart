import 'dart:io';

import 'package:forkmesh_git/forkmesh_git.dart';

Future<void> main() async {
  final sentinel = Platform.environment['FORKMESH_G1_GIT_SENTINEL'];
  if (sentinel == null || sentinel.isEmpty) {
    throw StateError('FORKMESH_G1_GIT_SENTINEL is required');
  }
  final root = await Directory.systemTemp.createTemp('forkmesh-g1-runtime-');
  try {
    final repository = await GitRepository.init(GitRepositoryInit(root));
    final object = await repository.writeBlob(<int>[111, 107, 10]);
    await repository.readBlob(object);
    if (await File(sentinel).exists()) {
      throw StateError('Git executable sentinel was invoked');
    }
    stdout.writeln('runtime-no-git-fallback=PROVED');
  } finally {
    await root.delete(recursive: true);
  }
}
