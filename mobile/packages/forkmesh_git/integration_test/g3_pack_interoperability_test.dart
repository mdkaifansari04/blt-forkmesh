import 'dart:convert';
import 'dart:io';
import 'dart:typed_data';

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

Future<void> _git(Directory root, List<String> arguments) async {
  final result = await Process.run(_systemGit, <String>[
    '-C',
    root.path,
    ...arguments,
  ]);
  expect(result.exitCode, 0, reason: result.stderr.toString());
}

Future<Uint8List> _createPack(Directory root) async {
  final process = await Process.start(_systemGit, <String>[
    '-C',
    root.path,
    'pack-objects',
    '--stdout',
    '--revs',
    '--window=10',
    '--depth=10',
    '--no-reuse-delta',
  ]);
  process.stdin.write('HEAD\n');
  await process.stdin.close();
  final stdout = await process.stdout.fold<BytesBuilder>(
    BytesBuilder(copy: false),
    (output, chunk) => output..add(chunk),
  );
  final stderr = await utf8.decoder.bind(process.stderr).join();
  expect(await process.exitCode, 0, reason: stderr);
  return stdout.takeBytes();
}

void main() {
  test('reads every object from a system-Git generated pack', () async {
    final root = await Directory.systemTemp.createTemp('forkmesh-g3-pack-');
    addTearDown(() => root.delete(recursive: true));
    await _git(root, <String>['init', '--initial-branch=main']);
    await _git(root, <String>['config', 'user.name', 'Oracle']);
    await _git(root, <String>['config', 'user.email', 'oracle@example.test']);
    final file = File('${root.path}${Platform.pathSeparator}data.txt');
    await file.writeAsString(
      List<String>.filled(2048, 'base content').join('\n'),
    );
    await _git(root, <String>['add', 'data.txt']);
    await _git(root, <String>['commit', '-m', 'base']);
    await file.writeAsString(
      List<String>.filled(
        2047,
        'base content',
      ).followedBy(<String>['changed']).join('\n'),
    );
    await _git(root, <String>['add', 'data.txt']);
    await _git(root, <String>['commit', '-m', 'changed']);

    final pack = GitPackReader.parse(
      await _createPack(root),
      GitResourceLimits(),
    );

    expect(pack.objects, hasLength(greaterThan(4)));
    for (final object in pack.objects) {
      final type = await Process.run(_systemGit, <String>[
        '-C',
        root.path,
        'cat-file',
        '-t',
        object.objectId.hex,
      ]);
      expect(type.exitCode, 0, reason: type.stderr.toString());
      expect(type.stdout.toString().trim(), object.type.name);
      final body = await Process.run(_systemGit, <String>[
        '-C',
        root.path,
        'cat-file',
        object.type.name,
        object.objectId.hex,
      ], stdoutEncoding: null);
      expect(body.exitCode, 0, reason: body.stderr.toString());
      expect(body.stdout, object.body);
    }
  });

  test('parses a system-Git generated version 2 pack index', () async {
    final root = await Directory.systemTemp.createTemp('forkmesh-g3-index-');
    addTearDown(() => root.delete(recursive: true));
    await _git(root, <String>['init', '--initial-branch=main']);
    await _git(root, <String>['config', 'user.name', 'Oracle']);
    await _git(root, <String>['config', 'user.email', 'oracle@example.test']);
    await File(
      '${root.path}${Platform.pathSeparator}file.txt',
    ).writeAsString('content\n');
    await _git(root, <String>['add', '.']);
    await _git(root, <String>['commit', '-m', 'fixture']);
    await _git(root, <String>['repack', '-ad']);
    final packDirectory = Directory(
      '${root.path}${Platform.pathSeparator}.git'
      '${Platform.pathSeparator}objects${Platform.pathSeparator}pack',
    );
    final files = await packDirectory
        .list()
        .where((entity) => entity.path.endsWith('.idx'))
        .toList();
    expect(files, hasLength(1));

    final index = GitPackIndexV2.parse(
      await File(files.single.path).readAsBytes(),
      GitResourceLimits(),
    );
    final head = (await Process.run(_systemGit, <String>[
      '-C',
      root.path,
      'rev-parse',
      'HEAD',
    ])).stdout.toString().trim();

    expect(index.lookup(GitObjectId.parseSha1(head)), isNotNull);
  });

  test(
    'opens a packed system-Git repository through the repository API',
    () async {
      final root = await Directory.systemTemp.createTemp(
        'forkmesh-g3-repository-',
      );
      addTearDown(() => root.delete(recursive: true));
      await _git(root, <String>['init', '--initial-branch=main']);
      await _git(root, <String>['config', 'user.name', 'Oracle']);
      await _git(root, <String>['config', 'user.email', 'oracle@example.test']);
      await File(
        '${root.path}${Platform.pathSeparator}packed.txt',
      ).writeAsString('packed\n');
      await _git(root, <String>['add', '.']);
      await _git(root, <String>['commit', '-m', 'packed fixture']);
      final blob = await Process.run(_systemGit, <String>[
        '-C',
        root.path,
        'rev-parse',
        'HEAD:packed.txt',
      ]);
      await _git(root, <String>['repack', '-ad']);
      final repository = await GitRepository.open(GitRepositoryOpen(root));

      expect(
        utf8.decode(
          await repository.readBlob(
            GitObjectId.parseSha1(blob.stdout.toString().trim()),
          ),
        ),
        'packed\n',
      );
    },
  );
}
