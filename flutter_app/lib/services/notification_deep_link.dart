import '../models/models.dart';

enum RepoDetailTab {
  code,
  issues,
  pulls,
  discussions,
  commits,
  mirrors,
  releases,
  about,
  actions,
  agents,
  worktrees,
}

class NotificationDeepLink {
  const NotificationDeepLink({
    required this.repo,
    required this.initialTab,
    this.number = 0,
    this.reference = '',
  });

  final Repository repo;
  final RepoDetailTab initialTab;
  final int number;
  final String reference;

  static final _safeSegment = RegExp(r'^[A-Za-z0-9._-]+$');

  static NotificationDeepLink? parse(String href) {
    final trimmed = href.trim();
    if (trimmed.isEmpty) return null;
    late final Uri uri;
    try {
      uri = Uri.parse(trimmed);
    } catch (_) {
      return null;
    }
    if (uri.hasScheme || uri.hasAuthority) return null;
    final parts = uri.pathSegments.where((part) => part.isNotEmpty).toList();
    if (parts.length < 2) return null;
    final owner = Uri.decodeComponent(parts[0]);
    final name = Uri.decodeComponent(parts[1]);
    if (!_safeSegment.hasMatch(owner) || !_safeSegment.hasMatch(name)) {
      return null;
    }
    if (owner == 'api' || owner == 'dashboard') return null;

    var tab = RepoDetailTab.code;
    var number = 0;
    var reference = '';
    if (parts.length >= 3) {
      switch (parts[2]) {
        case 'issues':
          tab = RepoDetailTab.issues;
          number = _number(parts);
        case 'pulls':
          tab = RepoDetailTab.pulls;
          number = _number(parts);
        case 'discussions':
          tab = RepoDetailTab.discussions;
          number = _number(parts);
        case 'commit':
        case 'commits':
          tab = RepoDetailTab.commits;
          reference = parts.length >= 4 ? Uri.decodeComponent(parts[3]) : '';
        case 'mirrors':
          tab = RepoDetailTab.mirrors;
        case 'releases':
          tab = RepoDetailTab.releases;
        case 'about':
          tab = RepoDetailTab.about;
        case 'actions':
          tab = RepoDetailTab.actions;
        case 'agents':
          tab = RepoDetailTab.agents;
        case 'worktrees':
          tab = RepoDetailTab.worktrees;
        case 'tree':
        case 'blob':
        case 'raw':
        case 'code':
          tab = RepoDetailTab.code;
      }
    }

    return NotificationDeepLink(
      repo: Repository(owner: owner, name: name),
      initialTab: tab,
      number: number,
      reference: reference,
    );
  }

  static int _number(List<String> parts) {
    if (parts.length < 4) return 0;
    return int.tryParse(parts[3]) ?? 0;
  }
}
