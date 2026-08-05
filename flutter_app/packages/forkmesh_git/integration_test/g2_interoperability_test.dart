import 'dart:io';

import 'package:forkmesh_git/forkmesh_git.dart';
import 'package:test/test.dart';

String get _systemGit {
  final value = Platform.environment['FORKMESH_TEST_SYSTEM_GIT'];
  if (value == null || !value.startsWith('/')) {
    throw StateError(
      'FORKMESH_TEST_SYSTEM_GIT must be an absolute test-only path',
    );
  }
  return value;
}

GitIdentity get _identity => GitIdentity(
  name: 'Ada Lovelace',
  email: 'ada@example.test',
  timestampSeconds: 0,
  timezoneOffsetMinutes: 0,
);

Future<void> _git(Directory root, List<String> arguments) async {
  final result = await Process.run(_systemGit, <String>[
    '-C',
    root.path,
    ...arguments,
  ]);
  expect(result.exitCode, 0, reason: result.stderr.toString());
}

void main() {
  test(
    'system Git reads a ForkMesh G2 commit with core path and mode variants',
    () async {
      final root = await Directory.systemTemp.createTemp('forkmesh-g2-owned-');
      addTearDown(() => root.delete(recursive: true));
      final repository = await GitRepository.init(GitRepositoryInit(root));
      final fixture = Directory(
        '${root.path}${Platform.pathSeparator}fixtures',
      );
      await fixture.create();
      await File(
        '${fixture.path}${Platform.pathSeparator}plain.txt',
      ).writeAsString('plain\n');
      await File(
        '${fixture.path}${Platform.pathSeparator}binary.bin',
      ).writeAsBytes(<int>[0, 1, 2, 0xff]);
      await File(
        '${fixture.path}${Platform.pathSeparator}run.sh',
      ).writeAsString('#!/bin/sh\n');
      final chmod = await Process.run('/bin/chmod', <String>[
        '755',
        '${fixture.path}${Platform.pathSeparator}run.sh',
      ]);
      expect(chmod.exitCode, 0, reason: chmod.stderr.toString());
      await Link(
        '${fixture.path}${Platform.pathSeparator}link',
      ).create('plain.txt');
      await File(
        '${fixture.path}${Platform.pathSeparator}caf\u00e9.txt',
      ).writeAsString('accent\n');

      await repository.add(<GitWorktreePath>[GitWorktreePath('fixtures')]);
      final commit = await repository.commit(
        GitCommitRequest(
          message: 'ForkMesh G2 fixture\n',
          author: _identity,
          committer: _identity,
        ),
      );

      await _git(root, <String>['fsck', '--no-dangling']);
      final tree = await Process.run(_systemGit, <String>[
        '-C',
        root.path,
        '-c',
        'core.quotePath=false',
        'ls-tree',
        '-r',
        '--full-tree',
        commit.hex,
      ]);
      expect(tree.exitCode, 0, reason: tree.stderr.toString());
      final output = tree.stdout.toString();
      expect(output, contains('100755 blob'));
      expect(output, contains('120000 blob'));
      expect(output, contains('fixtures/caf\u00e9.txt'));
    },
  );

  test(
    'ForkMesh reads a system Git index and clean committed worktree',
    () async {
      final root = await Directory.systemTemp.createTemp('forkmesh-g2-oracle-');
      addTearDown(() => root.delete(recursive: true));
      await _git(root, <String>['init', '--initial-branch=main']);
      await _git(root, <String>['config', 'user.name', 'Oracle']);
      await _git(root, <String>['config', 'user.email', 'oracle@example.test']);
      final nested = Directory('${root.path}${Platform.pathSeparator}nested');
      await nested.create();
      await File(
        '${nested.path}${Platform.pathSeparator}binary.bin',
      ).writeAsBytes(<int>[0, 0xff, 1]);
      await File(
        '${nested.path}${Platform.pathSeparator}caf\u00e9.txt',
      ).writeAsString('accent\n');
      await Link(
        '${nested.path}${Platform.pathSeparator}link',
      ).create('caf\u00e9.txt');
      await _git(root, <String>['add', '.']);
      await _git(root, <String>['commit', '-m', 'oracle fixture']);

      final repository = await GitRepository.open(GitRepositoryOpen(root));
      final status = await repository.status();

      expect(status.isClean, isTrue);
      final head = await repository.readHeadObjectId();
      expect(head, isNotNull);
      expect((await repository.readCommit(head!)).parents, isEmpty);
    },
  );
}
