import 'dart:io';

import 'package:forkmesh_git/forkmesh_git.dart';
import 'package:test/test.dart';

GitIdentity get _identity => GitIdentity(
  name: 'Ada Lovelace',
  email: 'ada@example.test',
  timestampSeconds: 0,
  timezoneOffsetMinutes: 0,
);

Future<void> _commitFile(
  GitRepository repository,
  Directory root,
  String content,
  String message,
) async {
  await File(
    '${root.path}${Platform.pathSeparator}note.txt',
  ).writeAsString(content);
  await repository.add(<GitWorktreePath>[GitWorktreePath('note.txt')]);
  await repository.commit(
    GitCommitRequest(message: message, author: _identity, committer: _identity),
  );
}

void main() {
  test(
    'creates a branch and safely materializes its committed files on checkout',
    () async {
      final root = await Directory.systemTemp.createTemp('forkmesh-checkout-');
      addTearDown(() => root.delete(recursive: true));
      final repository = await GitRepository.init(GitRepositoryInit(root));

      await _commitFile(repository, root, 'main\n', 'main commit\n');
      await repository.createBranch(GitBranchName('feature'));
      await repository.checkout(
        GitCheckoutRequest.branch(GitBranchName('feature')),
      );
      await _commitFile(repository, root, 'feature\n', 'feature commit\n');

      await repository.checkout(
        GitCheckoutRequest.branch(GitBranchName('main')),
      );

      expect(
        await File(
          '${root.path}${Platform.pathSeparator}note.txt',
        ).readAsString(),
        'main\n',
      );
      expect(
        await repository.readHead(),
        const GitSymbolicRef('refs/heads/main'),
      );
    },
  );
}
