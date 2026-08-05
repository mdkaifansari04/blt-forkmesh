import 'dart:convert';
import 'dart:io';

import 'package:forkmesh_git/forkmesh_git.dart';
import 'package:test/test.dart';

void main() {
  test(
    'stages a nested file, writes a commit, and returns to clean status',
    () async {
      final root = await Directory.systemTemp.createTemp('forkmesh-authoring-');
      addTearDown(() => root.delete(recursive: true));
      final repository = await GitRepository.init(GitRepositoryInit(root));
      final docs = Directory('${root.path}${Platform.pathSeparator}docs');
      await docs.create();
      await File(
        '${docs.path}${Platform.pathSeparator}readme.txt',
      ).writeAsString('hello\n');

      await repository.add(<GitWorktreePath>[
        GitWorktreePath('docs/readme.txt'),
      ]);
      final staged = await repository.status();

      expect(staged.staged.single.path.value, 'docs/readme.txt');
      expect(staged.staged.single.kind, GitChangeKind.added);
      expect(staged.worktree, isEmpty);

      final identity = GitIdentity(
        name: 'Ada Lovelace',
        email: 'ada@example.test',
        timestampSeconds: 0,
        timezoneOffsetMinutes: 0,
      );
      final commitId = await repository.commit(
        GitCommitRequest(
          message: 'Add a nested README\n',
          author: identity,
          committer: identity,
        ),
      );

      final commit = await repository.readCommit(commitId);
      final tree = await repository.readTree(commit.tree);
      expect(tree.entries.single.mode, GitTreeMode.directory);
      final nested = await repository.readTree(tree.entries.single.objectId);
      expect(
        utf8.decode(await repository.readBlob(nested.entries.single.objectId)),
        'hello\n',
      );
      expect(await repository.readHeadObjectId(), commitId);
      expect((await repository.status()).isClean, isTrue);
    },
  );
}
