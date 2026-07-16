import 'dart:convert';
import 'dart:io';

import 'package:forkmesh_git/forkmesh_git.dart';
import 'package:test/test.dart';

void main() {
  test('walks a commit and its nested objects into a non-delta pack', () async {
    final root = await Directory.systemTemp.createTemp(
      'forkmesh-reachability-',
    );
    addTearDown(() => root.delete(recursive: true));
    final repository = await GitRepository.init(GitRepositoryInit(root));
    await File(
      '${root.path}${Platform.pathSeparator}hello.txt',
    ).writeAsString('hello\n');
    await repository.add(<GitWorktreePath>[GitWorktreePath('hello.txt')]);
    final identity = GitIdentity(
      name: 'Ada',
      email: 'ada@example.test',
      timestampSeconds: 0,
      timezoneOffsetMinutes: 0,
    );
    final commit = await repository.commit(
      GitCommitRequest(
        message: 'fixture\n',
        author: identity,
        committer: identity,
      ),
    );

    final pack = await repository.writePack(<GitObjectId>[commit]);
    final objects = GitPackReader.parse(
      pack.packBytes,
      GitResourceLimits(),
    ).objects;

    expect(objects.map((object) => object.objectId), contains(commit));
    expect(
      objects.map((object) => object.type),
      containsAll(<GitObjectType>[
        GitObjectType.commit,
        GitObjectType.tree,
        GitObjectType.blob,
      ]),
    );
    expect(
      utf8.decode(
        await repository.readBlob(
          objects
              .firstWhere((object) => object.type == GitObjectType.blob)
              .objectId,
        ),
      ),
      'hello\n',
    );
  });
}
