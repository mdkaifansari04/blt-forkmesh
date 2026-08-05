import 'dart:io';

Future<void> main() async {
  final forbiddenProcessApi = RegExp(
    r'\bProcess\b|\bProcessStartMode\b|\bProcessSignal\b',
  );
  final forbiddenGitPackage = RegExp(
    r'package:(?:git2dart|libgit2dart|git_on_dart|git2|dart_git|gix)',
  );
  final violations = <String>[];
  await for (final entity in Directory('lib').list(recursive: true)) {
    if (entity is! File || !entity.path.endsWith('.dart')) continue;
    final content = await entity.readAsString();
    if (forbiddenProcessApi.hasMatch(content) ||
        forbiddenGitPackage.hasMatch(content)) {
      violations.add(entity.path);
    }
  }
  if (violations.isNotEmpty) {
    stderr.writeln(
      'production-process-api=FAILED files=${violations.join(',')}',
    );
    exitCode = 99;
    return;
  }
  stdout.writeln('production-process-api=PROVED');
}
