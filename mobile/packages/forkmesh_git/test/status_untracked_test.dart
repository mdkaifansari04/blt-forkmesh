import 'dart:io';

import 'package:forkmesh_git/forkmesh_git.dart';
import 'package:test/test.dart';

void main() {
  test(
    'reports an untracked file without treating .git metadata as worktree content',
    () async {
      final root = await Directory.systemTemp.createTemp('forkmesh-status-');
      addTearDown(() => root.delete(recursive: true));
      final repository = await GitRepository.init(GitRepositoryInit(root));
      await File(
        '${root.path}${Platform.pathSeparator}draft.txt',
      ).writeAsString('draft\n');

      final status = await repository.status();

      expect(status.staged, isEmpty);
      expect(status.worktree, hasLength(1));
      expect(status.worktree.single.path.value, 'draft.txt');
      expect(status.worktree.single.kind, GitChangeKind.untracked);
    },
  );
}
