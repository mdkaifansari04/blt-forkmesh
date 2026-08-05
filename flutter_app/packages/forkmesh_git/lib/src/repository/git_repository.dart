import 'dart:convert';
import 'dart:io';
import 'dart:typed_data';

import '../crypto/collision_screen.dart';
import '../errors/git_error.dart';
import '../index/index_store.dart';
import '../index/index_v2.dart';
import '../objects/commit_object.dart';
import '../objects/git_object.dart';
import '../objects/loose_object_store.dart';
import '../objects/tag_object.dart';
import '../objects/tree_object.dart';
import '../pack/pack_object_store.dart';
import '../pack/pack_writer.dart';
import '../refs/ref_store.dart';
import '../security/operation_journal.dart';
import '../security/quarantine.dart';
import '../security/resource_limits.dart';
import '../worktree/worktree_path.dart';
import 'repository_config.dart';

final class GitRepositoryInit {
  const GitRepositoryInit(this.worktree, {this.limits, this.detector});

  final Directory worktree;
  final GitResourceLimits? limits;
  final GitSha1CollisionDetector? detector;
}

final class GitRepositoryOpen {
  const GitRepositoryOpen(this.worktree, {this.limits, this.detector});

  final Directory worktree;
  final GitResourceLimits? limits;
  final GitSha1CollisionDetector? detector;
}

enum GitChangeKind { added, modified, deleted, untracked }

final class GitStatusEntry {
  const GitStatusEntry(this.path, this.kind);

  final GitWorktreePath path;
  final GitChangeKind kind;
}

final class GitStatus {
  const GitStatus({required this.staged, required this.worktree});

  final List<GitStatusEntry> staged;
  final List<GitStatusEntry> worktree;

  bool get isClean => staged.isEmpty && worktree.isEmpty;
}

final class GitIdentity {
  GitIdentity({
    required this.name,
    required this.email,
    required this.timestampSeconds,
    required this.timezoneOffsetMinutes,
  }) {
    if (name.isEmpty ||
        email.isEmpty ||
        !_isSafeIdentityField(name) ||
        !_isSafeIdentityField(email) ||
        email.contains('<') ||
        email.contains('>') ||
        timezoneOffsetMinutes < -14 * 60 ||
        timezoneOffsetMinutes > 14 * 60) {
      throw const GitException(
        GitErrorCode.invalidObject,
        'commit identity is invalid',
      );
    }
  }

  final String name;
  final String email;
  final int timestampSeconds;
  final int timezoneOffsetMinutes;

  String get gitText {
    final absoluteMinutes = timezoneOffsetMinutes.abs();
    final sign = timezoneOffsetMinutes < 0 ? '-' : '+';
    final hours = absoluteMinutes ~/ 60;
    final minutes = absoluteMinutes % 60;
    return '$name <$email> $timestampSeconds '
        '$sign${hours.toString().padLeft(2, '0')}${minutes.toString().padLeft(2, '0')}';
  }
}

final class GitCommitRequest {
  GitCommitRequest({
    required this.message,
    required this.author,
    required this.committer,
  }) {
    if (message.contains('\u0000')) {
      throw const GitException(
        GitErrorCode.invalidObject,
        'commit message contains NUL',
      );
    }
  }

  final String message;
  final GitIdentity author;
  final GitIdentity committer;
}

final class GitBranchName {
  GitBranchName(this.value) {
    if (value.isEmpty || value.startsWith('/') || value.endsWith('/')) {
      throw const GitException(
        GitErrorCode.invalidRef,
        'branch name is invalid',
      );
    }
    GitRefName.branch('refs/heads/$value');
  }

  final String value;

  GitRefName get ref => GitRefName.branch('refs/heads/$value');
}

final class GitCheckoutRequest {
  const GitCheckoutRequest.branch(this.branch);

  final GitBranchName branch;
}

final class GitRepository {
  GitRepository._(this.worktree, this.gitDirectory, this.limits, this.detector)
    : _objects = LooseObjectStore(gitDirectory, limits, detector: detector),
      _index = GitIndexStore(gitDirectory, limits),
      _packedObjects = GitPackObjectStore(
        gitDirectory,
        limits,
        detector: detector,
      );

  final Directory worktree;
  final Directory gitDirectory;
  final GitResourceLimits limits;
  final GitSha1CollisionDetector? detector;
  final LooseObjectStore _objects;
  final GitIndexStore _index;
  final GitPackObjectStore _packedObjects;

  static Future<GitRepository> init(GitRepositoryInit request) async {
    await request.worktree.create(recursive: true);
    final gitDirectory = Directory(
      '${request.worktree.path}${Platform.pathSeparator}.git',
    );
    if (await gitDirectory.exists()) {
      throw const GitException(
        GitErrorCode.invalidRepository,
        'repository already exists',
      );
    }
    await Directory(
      '${gitDirectory.path}${Platform.pathSeparator}objects',
    ).create(recursive: true);
    await Directory(
      '${gitDirectory.path}${Platform.pathSeparator}refs${Platform.pathSeparator}heads',
    ).create(recursive: true);
    await Directory(
      '${gitDirectory.path}${Platform.pathSeparator}refs${Platform.pathSeparator}tags',
    ).create(recursive: true);
    await Directory(
      '${gitDirectory.path}${Platform.pathSeparator}refs${Platform.pathSeparator}remotes',
    ).create(recursive: true);
    await File(
      '${gitDirectory.path}${Platform.pathSeparator}HEAD',
    ).writeAsString('ref: refs/heads/main\n', flush: true);
    await File(
      '${gitDirectory.path}${Platform.pathSeparator}config',
    ).writeAsString(
      '[core]\n\trepositoryformatversion = 0\n\tbare = false\n',
      flush: true,
    );
    return open(
      GitRepositoryOpen(
        request.worktree,
        limits: request.limits,
        detector: request.detector,
      ),
    );
  }

  static Future<GitRepository> open(GitRepositoryOpen request) async {
    final gitDirectory = Directory(
      '${request.worktree.path}${Platform.pathSeparator}.git',
    );
    if (!await gitDirectory.exists()) {
      throw const GitException(
        GitErrorCode.invalidRepository,
        'missing .git directory',
      );
    }
    final config = File('${gitDirectory.path}${Platform.pathSeparator}config');
    if (!await config.exists()) {
      throw const GitException(
        GitErrorCode.invalidRepository,
        'missing config',
      );
    }
    await RepositoryConfig.load(config);
    await GitOperationJournal.recover(gitDirectory);
    await GitObjectQuarantine.recover(gitDirectory);
    final head = File('${gitDirectory.path}${Platform.pathSeparator}HEAD');
    if (!await head.exists()) {
      throw const GitException(
        GitErrorCode.invalidRepository,
        'HEAD is invalid',
      );
    }
    final headValue = (await head.readAsString()).trim();
    try {
      if (headValue.startsWith('ref: ')) {
        GitRefName.branch(headValue.substring(5));
      } else {
        GitObjectId.parseSha1(headValue);
      }
    } on GitException {
      throw const GitException(
        GitErrorCode.invalidRepository,
        'HEAD is invalid',
      );
    }
    return GitRepository._(
      request.worktree,
      gitDirectory,
      request.limits ?? GitResourceLimits(),
      request.detector,
    );
  }

  Future<GitObjectId> writeBlob(List<int> bytes) =>
      _objects.write(GitObjectType.blob, bytes);

  Future<Uint8List> readBlob(GitObjectId id) async {
    final object = await _readObject(id);
    if (object.type != GitObjectType.blob) {
      throw const GitException(
        GitErrorCode.invalidObject,
        'expected blob object',
      );
    }
    return object.body;
  }

  Future<GitSymbolicRef> readHead() async {
    final value = (await File(
      '${gitDirectory.path}${Platform.pathSeparator}HEAD',
    ).readAsString()).trim();
    if (!value.startsWith('ref: ')) {
      throw const GitException(GitErrorCode.invalidRef, 'HEAD is detached');
    }
    final target = value.substring(5);
    GitRefName.branch(target);
    return GitSymbolicRef(target);
  }

  Future<GitObjectId?> readHeadObjectId() async {
    final head = await _headTarget();
    if (head.ref == null) return head.direct;
    return RefStore(gitDirectory).readDirect(head.ref!);
  }

  Future<GitTree> readTree(GitObjectId id) async {
    final object = await _readObject(id);
    if (object.type != GitObjectType.tree) {
      throw const GitException(
        GitErrorCode.invalidObject,
        'expected tree object',
      );
    }
    return GitTree.parse(object.body);
  }

  Future<GitCommit> readCommit(GitObjectId id) async {
    final object = await _readObject(id);
    if (object.type != GitObjectType.commit) {
      throw const GitException(
        GitErrorCode.invalidObject,
        'expected commit object',
      );
    }
    return GitCommit.parse(object.body);
  }

  Future<GitObjectFrame> _readObject(GitObjectId id) async {
    try {
      return await _objects.read(id);
    } on GitException catch (error) {
      if (error.code != GitErrorCode.invalidObject) rethrow;
      return _packedObjects.read(id);
    }
  }

  Future<GitWrittenPack> writePack(Iterable<GitObjectId> roots) async {
    final visited = <GitObjectId>{};
    final objects = <GitPackWriteObject>[];
    for (final root in roots) {
      await _collectPackObject(root, visited, objects);
    }
    return GitPackWriter.write(objects);
  }

  Future<void> _collectPackObject(
    GitObjectId id,
    Set<GitObjectId> visited,
    List<GitPackWriteObject> output,
  ) async {
    if (!visited.add(id)) return;
    final object = await _readObject(id);
    output.add(GitPackWriteObject(object.type, object.body));
    switch (object.type) {
      case GitObjectType.blob:
        return;
      case GitObjectType.tree:
        for (final entry in GitTree.parse(object.body).entries) {
          await _collectPackObject(entry.objectId, visited, output);
        }
      case GitObjectType.commit:
        final commit = GitCommit.parse(object.body);
        await _collectPackObject(commit.tree, visited, output);
        for (final parent in commit.parents) {
          await _collectPackObject(parent, visited, output);
        }
      case GitObjectType.tag:
        await _collectPackObject(
          GitAnnotatedTag.parse(object.body).target,
          visited,
          output,
        );
    }
  }

  Future<void> add(Iterable<GitWorktreePath> paths) async {
    final journal = await GitOperationJournal.begin(gitDirectory, 'add');
    try {
      final existing = await _index.read();
      final entries = <String, GitIndexEntry>{
        for (final entry in existing.entries) utf8.decode(entry.path): entry,
      };
      for (final path in paths) {
        if (path.components.first == '.git') {
          throw const GitException(
            GitErrorCode.pathEscapesWorktree,
            'cannot stage repository metadata',
          );
        }
        await _stagePath(path, entries);
      }
      await _index.write(GitIndexV2(entries.values));
    } finally {
      await journal.complete();
    }
  }

  Future<void> remove(Iterable<GitWorktreePath> paths) async {
    final journal = await GitOperationJournal.begin(gitDirectory, 'remove');
    try {
      final existing = await _index.read();
      final entries = <String, GitIndexEntry>{
        for (final entry in existing.entries) utf8.decode(entry.path): entry,
      };
      for (final path in paths) {
        entries.remove(path.value);
      }
      await _index.write(GitIndexV2(entries.values));
    } finally {
      await journal.complete();
    }
  }

  Future<GitStatus> status() async {
    final index = await _index.read();
    final indexEntries = <String, GitIndexEntry>{
      for (final entry in index.entries) utf8.decode(entry.path): entry,
    };
    final headEntries = await _headEntries();
    final staged = <GitStatusEntry>[];
    final allPaths = <String>{
      ...indexEntries.keys,
      ...headEntries.keys,
    }.toList()..sort();
    for (final path in allPaths) {
      final indexEntry = indexEntries[path];
      final headEntry = headEntries[path];
      if (indexEntry == null) {
        staged.add(
          GitStatusEntry(GitWorktreePath(path), GitChangeKind.deleted),
        );
      } else if (headEntry == null) {
        staged.add(GitStatusEntry(GitWorktreePath(path), GitChangeKind.added));
      } else if (indexEntry.objectId != headEntry.objectId ||
          indexEntry.mode != headEntry.mode) {
        staged.add(
          GitStatusEntry(GitWorktreePath(path), GitChangeKind.modified),
        );
      }
    }
    final worktree = <GitStatusEntry>[];
    for (final entry in indexEntries.values) {
      final change = await _worktreeChange(entry);
      if (change != null) {
        worktree.add(
          GitStatusEntry(GitWorktreePath(utf8.decode(entry.path)), change),
        );
      }
    }
    await _collectUntracked(this.worktree, <String>[], indexEntries, worktree);
    worktree.sort((left, right) => left.path.value.compareTo(right.path.value));
    return GitStatus(
      staged: List<GitStatusEntry>.unmodifiable(staged),
      worktree: List<GitStatusEntry>.unmodifiable(worktree),
    );
  }

  Future<GitObjectId> commit(GitCommitRequest request) async {
    final journal = await GitOperationJournal.begin(gitDirectory, 'commit');
    try {
      final index = await _index.read();
      final tree = await _writeTree(index.entries);
      final parent = await readHeadObjectId();
      final body = _encodeCommit(request, tree, parent);
      final commit = await _objects.write(GitObjectType.commit, body);
      final head = await _headTarget();
      if (head.ref == null) {
        throw const GitException(
          GitErrorCode.unsupportedRepositoryFormat,
          'committing from detached HEAD is unsupported',
        );
      }
      final result = await RefStore(
        gitDirectory,
      ).update(head.ref!, expectedOld: parent, next: commit);
      if (result != GitRefUpdateResult.updated) {
        throw const GitException(
          GitErrorCode.nonFastForward,
          'HEAD changed while creating commit',
        );
      }
      return commit;
    } finally {
      await journal.complete();
    }
  }

  Future<void> createBranch(GitBranchName branch) async {
    final head = await readHeadObjectId();
    if (head == null) {
      throw const GitException(
        GitErrorCode.invalidRef,
        'cannot create a branch from an unborn HEAD',
      );
    }
    final result = await RefStore(
      gitDirectory,
    ).update(branch.ref, expectedOld: null, next: head);
    if (result != GitRefUpdateResult.updated) {
      throw const GitException(GitErrorCode.refLocked, 'branch already exists');
    }
  }

  Future<void> checkout(GitCheckoutRequest request) async {
    final journal = await GitOperationJournal.begin(gitDirectory, 'checkout');
    try {
      final before = await status();
      if (!before.isClean) {
        throw const GitException(
          GitErrorCode.operationInterrupted,
          'cannot checkout with local changes',
        );
      }
      final target = await RefStore(
        gitDirectory,
      ).readDirect(request.branch.ref);
      if (target == null) {
        throw const GitException(
          GitErrorCode.invalidRef,
          'branch does not exist',
        );
      }
      final currentEntries = await _headEntries();
      final targetEntries = await _entriesForCommit(target);
      await _materializeCheckout(currentEntries, targetEntries);
      await _index.write(GitIndexV2(targetEntries.values));
      await _writeHead('ref: ${request.branch.ref.value}\n');
    } finally {
      await journal.complete();
    }
  }

  Future<void> _stagePath(
    GitWorktreePath path,
    Map<String, GitIndexEntry> entries,
  ) async {
    final tree = GitWorktree(worktree);
    final type = await tree.typeOf(path);
    switch (type) {
      case FileSystemEntityType.file:
        final bytes = await tree.readFile(path);
        final stat = await File(
          '${worktree.path}${Platform.pathSeparator}${path.components.join(Platform.pathSeparator)}',
        ).stat();
        final config = await RepositoryConfig.load(
          File('${gitDirectory.path}${Platform.pathSeparator}config'),
        );
        final mode = config.fileMode && stat.mode & 73 != 0
            ? GitIndexMode.executable
            : GitIndexMode.regular;
        entries[path.value] = GitIndexEntry(
          path: path.bytes,
          objectId: await writeBlob(bytes),
          mode: mode,
          size: bytes.length,
        );
      case FileSystemEntityType.link:
        final target = await tree.readSymlink(path);
        final bytes = utf8.encode(target);
        entries[path.value] = GitIndexEntry(
          path: path.bytes,
          objectId: await writeBlob(bytes),
          mode: GitIndexMode.symlink,
          size: bytes.length,
        );
      case FileSystemEntityType.directory:
        final directory = Directory(
          '${worktree.path}${Platform.pathSeparator}${path.components.join(Platform.pathSeparator)}',
        );
        await for (final entity in directory.list(followLinks: false)) {
          final name = entity.uri.pathSegments.last;
          await _stagePath(GitWorktreePath('${path.value}/$name'), entries);
        }
      case FileSystemEntityType.notFound:
        throw const GitException(
          GitErrorCode.invalidRepository,
          'worktree path is missing',
        );
      default:
        throw const GitException(
          GitErrorCode.unsupportedRepositoryFormat,
          'worktree path has unsupported type',
        );
    }
  }

  Future<GitChangeKind?> _worktreeChange(GitIndexEntry entry) async {
    final path = GitWorktreePath(utf8.decode(entry.path));
    final tree = GitWorktree(worktree);
    final type = await tree.typeOf(path);
    if (type == FileSystemEntityType.notFound) return GitChangeKind.deleted;
    Uint8List bytes;
    if (type == FileSystemEntityType.file) {
      bytes = await tree.readFile(path);
    } else if (type == FileSystemEntityType.link) {
      bytes = Uint8List.fromList(utf8.encode(await tree.readSymlink(path)));
    } else {
      return GitChangeKind.modified;
    }
    final mode = type == FileSystemEntityType.link
        ? GitIndexMode.symlink
        : entry.mode == GitIndexMode.executable
        ? GitIndexMode.executable
        : GitIndexMode.regular;
    final id = GitObjectId.sha1Of(
      GitObjectFrame.encode(GitObjectType.blob, bytes),
    );
    if (id != entry.objectId || mode != entry.mode) {
      return GitChangeKind.modified;
    }
    return null;
  }

  Future<void> _collectUntracked(
    Directory directory,
    List<String> prefix,
    Map<String, GitIndexEntry> tracked,
    List<GitStatusEntry> output,
  ) async {
    await for (final entity in directory.list(followLinks: false)) {
      final name = entity.uri.pathSegments.lastWhere(
        (segment) => segment.isNotEmpty,
      );
      if (prefix.isEmpty && name == '.git') continue;
      final components = <String>[...prefix, name];
      final path = GitWorktreePath(components.join('/'));
      final type = await FileSystemEntity.type(entity.path, followLinks: false);
      if (type == FileSystemEntityType.directory) {
        await _collectUntracked(
          Directory(entity.path),
          components,
          tracked,
          output,
        );
      } else if (!tracked.containsKey(path.value)) {
        output.add(GitStatusEntry(path, GitChangeKind.untracked));
      }
    }
  }

  Future<Map<String, GitIndexEntry>> _headEntries() async {
    final head = await readHeadObjectId();
    if (head == null) return <String, GitIndexEntry>{};
    return _entriesForCommit(head);
  }

  Future<Map<String, GitIndexEntry>> _entriesForCommit(
    GitObjectId commitId,
  ) async {
    final commit = await readCommit(commitId);
    final entries = <String, GitIndexEntry>{};
    await _collectTreeEntries(commit.tree, <String>[], entries);
    return entries;
  }

  Future<void> _materializeCheckout(
    Map<String, GitIndexEntry> current,
    Map<String, GitIndexEntry> target,
  ) async {
    final tree = GitWorktree(worktree);
    final removedPaths =
        current.keys.where((path) => !target.containsKey(path)).toList()
          ..sort((left, right) => right.compareTo(left));
    for (final path in removedPaths) {
      await tree.deleteFile(GitWorktreePath(path));
    }
    final targetPaths = target.keys.toList()..sort();
    for (final path in targetPaths) {
      final entry = target[path]!;
      final destination = GitWorktreePath(path);
      final bytes = await readBlob(entry.objectId);
      switch (entry.mode) {
        case GitIndexMode.regular:
        case GitIndexMode.executable:
          await tree.writeFile(destination, bytes);
        case GitIndexMode.symlink:
          String targetText;
          try {
            targetText = utf8.decode(bytes, allowMalformed: false);
          } on FormatException {
            throw const GitException(
              GitErrorCode.symlinkEscape,
              'symlink payload is not valid UTF-8',
            );
          }
          await tree.writeSymlink(destination, targetText);
      }
    }
  }

  Future<void> _writeHead(String value) async {
    final destination = File(
      '${gitDirectory.path}${Platform.pathSeparator}HEAD',
    );
    final lock = File('${destination.path}.lock');
    try {
      await lock.create(exclusive: true);
    } on FileSystemException {
      throw const GitException(
        GitErrorCode.refLocked,
        'HEAD lock already exists',
      );
    }
    try {
      await lock.writeAsString(value, flush: true);
      await lock.rename(destination.path);
    } finally {
      if (await lock.exists()) await lock.delete();
    }
  }

  Future<void> _collectTreeEntries(
    GitObjectId treeId,
    List<String> prefix,
    Map<String, GitIndexEntry> output,
  ) async {
    final tree = await readTree(treeId);
    for (final entry in tree.entries) {
      final name = utf8.decode(entry.name);
      final components = <String>[...prefix, name];
      if (entry.mode == GitTreeMode.directory) {
        await _collectTreeEntries(entry.objectId, components, output);
      } else {
        final path = GitWorktreePath(components.join('/'));
        output[path.value] = GitIndexEntry(
          path: path.bytes,
          objectId: entry.objectId,
          mode: switch (entry.mode) {
            GitTreeMode.regular => GitIndexMode.regular,
            GitTreeMode.executable => GitIndexMode.executable,
            GitTreeMode.symlink => GitIndexMode.symlink,
            GitTreeMode.directory => throw StateError('handled above'),
          },
        );
      }
    }
  }

  Future<GitObjectId> _writeTree(List<GitIndexEntry> entries) async {
    final root = _TreeNode();
    for (final entry in entries) {
      final path = GitWorktreePath(utf8.decode(entry.path));
      var node = root;
      for (final component in path.components.take(
        path.components.length - 1,
      )) {
        if (node.files.containsKey(component)) {
          throw const GitException(
            GitErrorCode.invalidTree,
            'file and directory paths collide',
          );
        }
        node = node.directories.putIfAbsent(component, _TreeNode.new);
      }
      final name = path.components.last;
      if (node.directories.containsKey(name) || node.files.containsKey(name)) {
        throw const GitException(
          GitErrorCode.invalidTree,
          'duplicate tree path',
        );
      }
      node.files[name] = entry;
    }
    return _writeTreeNode(root);
  }

  Future<GitObjectId> _writeTreeNode(_TreeNode node) async {
    final entries = <GitTreeEntry>[];
    for (final file in node.files.entries) {
      final entry = file.value;
      final name = utf8.encode(file.key);
      entries.add(switch (entry.mode) {
        GitIndexMode.regular => GitTreeEntry.regular(name, entry.objectId),
        GitIndexMode.executable => GitTreeEntry.executable(
          name,
          entry.objectId,
        ),
        GitIndexMode.symlink => GitTreeEntry.symlink(name, entry.objectId),
      });
    }
    for (final directory in node.directories.entries) {
      entries.add(
        GitTreeEntry.directory(
          utf8.encode(directory.key),
          await _writeTreeNode(directory.value),
        ),
      );
    }
    return _objects.write(GitObjectType.tree, GitTree(entries).encode());
  }

  Future<_HeadTarget> _headTarget() async {
    final value = (await File(
      '${gitDirectory.path}${Platform.pathSeparator}HEAD',
    ).readAsString()).trim();
    if (value.startsWith('ref: ')) {
      return _HeadTarget.ref(GitRefName.branch(value.substring(5)));
    }
    return _HeadTarget.direct(GitObjectId.parseSha1(value));
  }

  Future<void> dispose() async {}
}

final class _TreeNode {
  final Map<String, _TreeNode> directories = <String, _TreeNode>{};
  final Map<String, GitIndexEntry> files = <String, GitIndexEntry>{};
}

final class _HeadTarget {
  const _HeadTarget.ref(this.ref) : direct = null;
  const _HeadTarget.direct(this.direct) : ref = null;

  final GitRefName? ref;
  final GitObjectId? direct;
}

Uint8List _encodeCommit(
  GitCommitRequest request,
  GitObjectId tree,
  GitObjectId? parent,
) {
  final headers = StringBuffer('tree ${tree.hex}\n');
  if (parent != null) headers.write('parent ${parent.hex}\n');
  headers
    ..write('author ${request.author.gitText}\n')
    ..write('committer ${request.committer.gitText}\n')
    ..write('\n');
  return Uint8List.fromList(<int>[
    ...ascii.encode(headers.toString()),
    ...utf8.encode(request.message),
  ]);
}

bool _isSafeIdentityField(String value) =>
    !value.contains(RegExp(r'[\u0000\r\n]')) &&
    ascii.encode(value).length == value.length;
