import 'dart:convert';
import 'dart:io';

import '../errors/git_error.dart';

final class RepositoryConfig {
  const RepositoryConfig({required this.fileMode});

  final bool fileMode;

  static Future<RepositoryConfig> load(File file) async =>
      parse(await file.readAsBytes());

  static RepositoryConfig parse(List<int> bytes) {
    if (bytes.length > 1024 * 1024) {
      throw const GitException(
        GitErrorCode.resourceLimitExceeded,
        'config exceeds one MiB',
      );
    }
    String text;
    try {
      text = utf8.decode(bytes, allowMalformed: false);
    } on FormatException {
      throw const GitException(
        GitErrorCode.unsupportedRepositoryFormat,
        'config is not valid UTF-8',
      );
    }
    var fileMode = true;
    String? section;
    for (final line in const LineSplitter().convert(text)) {
      final trimmed = line.trimLeft();
      if (trimmed.isEmpty ||
          trimmed.startsWith('#') ||
          trimmed.startsWith(';')) {
        continue;
      }
      final sectionMatch = RegExp(
        r'^\[([A-Za-z][A-Za-z0-9-]*)\]$',
      ).firstMatch(trimmed);
      if (sectionMatch != null) {
        section = sectionMatch.group(1)!.toLowerCase();
        if (section == 'include' || section == 'includeif') {
          throw const GitException(
            GitErrorCode.unsupportedRepositoryFormat,
            'config includes are unsupported',
          );
        }
        continue;
      }
      final keyMatch = RegExp(
        r'^([A-Za-z][A-Za-z0-9-]*)[ \t]*=[ \t]*(.*)$',
      ).firstMatch(trimmed);
      if (section == null || keyMatch == null) {
        throw const GitException(
          GitErrorCode.unsupportedRepositoryFormat,
          'config line is invalid',
        );
      }
      final key = keyMatch.group(1)!.toLowerCase();
      final value = keyMatch.group(2)!;
      if (section == 'core' &&
          key == 'repositoryformatversion' &&
          value != '0') {
        throw const GitException(
          GitErrorCode.unsupportedRepositoryFormat,
          'repository format is unsupported',
        );
      }
      if (section == 'core' && key == 'bare' && value != 'false') {
        throw const GitException(
          GitErrorCode.unsupportedRepositoryFormat,
          'bare repository is unsupported',
        );
      }
      if (section == 'core' && key == 'worktree') {
        throw const GitException(
          GitErrorCode.unsupportedRepositoryFormat,
          'external worktree is unsupported',
        );
      }
      if (section == 'extensions' && key == 'objectformat' && value != 'sha1') {
        throw const GitException(
          GitErrorCode.unsupportedRepositoryFormat,
          'object format is unsupported',
        );
      }
      if (section == 'core' && key == 'filemode') {
        if (value != 'true' && value != 'false') {
          throw const GitException(
            GitErrorCode.unsupportedRepositoryFormat,
            'core.filemode is invalid',
          );
        }
        fileMode = value == 'true';
      }
      if (key.contains('hook') ||
          key.contains('helper') ||
          key.contains('command')) {
        throw const GitException(
          GitErrorCode.unsupportedRepositoryFormat,
          'executable config is unsupported',
        );
      }
    }
    return RepositoryConfig(fileMode: fileMode);
  }
}
