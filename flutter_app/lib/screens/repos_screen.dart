import 'package:flutter/material.dart';
import 'package:provider/provider.dart';

import '../models/models.dart';
import '../services/api_service.dart';
import '../theme.dart';
import '../widgets/fm_ui.dart';
import 'repo_detail_screen.dart';

enum _RepoFilter { all, online, offline, public, private }

enum _RepoSort { recentlyUpdated, name, mirrors }

extension _RepoFilterLabel on _RepoFilter {
  String get label {
    switch (this) {
      case _RepoFilter.all:
        return 'All';
      case _RepoFilter.online:
        return 'Online';
      case _RepoFilter.offline:
        return 'Offline';
      case _RepoFilter.public:
        return 'Public';
      case _RepoFilter.private:
        return 'Private';
    }
  }
}

extension _RepoSortLabel on _RepoSort {
  String get buttonLabel {
    switch (this) {
      case _RepoSort.recentlyUpdated:
        return 'Updated';
      case _RepoSort.name:
        return 'Name';
      case _RepoSort.mirrors:
        return 'Mirrors';
    }
  }

  String get menuLabel {
    switch (this) {
      case _RepoSort.recentlyUpdated:
        return 'Recently updated first';
      case _RepoSort.name:
        return 'Name A-Z';
      case _RepoSort.mirrors:
        return 'Mirrors high-low';
    }
  }
}


class ReposScreen extends StatefulWidget {
  const ReposScreen({super.key});

  @override
  State<ReposScreen> createState() => _ReposScreenState();
}

class _ReposScreenState extends State<ReposScreen> {
  late Future<List<Repository>> _future;
  String _query = '';
  _RepoFilter _filter = _RepoFilter.all;
  _RepoSort _sort = _RepoSort.recentlyUpdated;

  @override
  void initState() {
    super.initState();
    _future = context.read<ApiService>().repositories();
  }

  void _reload() {
    setState(() {
      _future = context.read<ApiService>().repositories();
    });
  }

  @override
  Widget build(BuildContext context) {
    return Column(
      children: [
        Padding(
          padding: const EdgeInsets.all(FmSpace.x4),
          child: Column(
            crossAxisAlignment: CrossAxisAlignment.stretch,
            children: [
              Row(
                children: [
                  Expanded(
                    child: SizedBox(
                      height: 48,
                      child: TextField(
                        decoration: InputDecoration(
                          hintText: 'Find a repository...',
                          prefixIcon: Icon(
                            Icons.search,
                            size: 18,
                            color: FmTheme.textTertiary(context),
                          ),
                          filled: true,
                          fillColor: FmTheme.bgRaised(context),
                          contentPadding: const EdgeInsets.symmetric(
                            horizontal: FmSpace.x4,
                            vertical: FmSpace.x3,
                          ),
                          enabledBorder: OutlineInputBorder(
                            borderRadius: BorderRadius.circular(FmRadius.lg),
                            borderSide: BorderSide.none,
                          ),
                          focusedBorder: OutlineInputBorder(
                            borderRadius: BorderRadius.circular(FmRadius.lg),
                            borderSide: BorderSide(
                              color: FmTheme.accent(context),
                              width: 1.2,
                            ),
                          ),
                        ),
                        onChanged: (v) =>
                            setState(() => _query = v.trim().toLowerCase()),
                      ),
                    ),
                  ),
                  const SizedBox(width: FmSpace.x2),
                  SizedBox(
                    width: 48,
                    height: 48,
                    child: IconButton(
                      onPressed: _reload,
                      tooltip: 'Refresh repositories',
                      style: IconButton.styleFrom(
                        backgroundColor: FmTheme.bgRaised(context),
                        foregroundColor: FmTheme.textSecondary(context),
                        shape: RoundedRectangleBorder(
                          borderRadius: BorderRadius.circular(FmRadius.lg),
                        ),
                      ),
                      icon: const Icon(Icons.refresh),
                    ),
                  ),
                ],
              ),
              const SizedBox(height: FmSpace.x3),
              _RepoCatalogControls(
                filter: _filter,
                sort: _sort,
                onFilterChanged: (filter) => setState(() => _filter = filter),
                onSortChanged: (sort) => setState(() => _sort = sort),
              ),
            ],
          ),
        ),
        Expanded(
          child: FutureBuilder<List<Repository>>(
            future: _future,
            builder: (context, snap) {
              if (snap.connectionState == ConnectionState.waiting) {
                return const Center(child: CircularProgressIndicator());
              }
              if (snap.hasError) {
                return _ErrorView(message: '${snap.error}', onRetry: _reload);
              }
              final repos = _filteredAndSortedRepos(snap.data ?? []);
              if (repos.isEmpty) {
                final hasActiveControls =
                    _query.isNotEmpty || _filter != _RepoFilter.all;
                return FmEmptyState(
                  icon: Icons.book_outlined,
                  title: hasActiveControls
                      ? 'No matching repositories'
                      : 'No repositories',
                  message: hasActiveControls
                      ? 'Try another search term or filter.'
                      : 'Repository mirrors will appear here when available.',
                );
              }
              return RefreshIndicator(
                onRefresh: () async => _reload(),
                child: ListView.builder(
                  padding: const EdgeInsets.fromLTRB(
                    FmSpace.x4,
                    FmSpace.x0,
                    FmSpace.x4,
                    FmSpace.x4,
                  ),
                  itemCount: repos.length,
                  itemBuilder: (_, i) => Padding(
                    padding: EdgeInsets.only(
                      bottom: i == repos.length - 1 ? FmSpace.x0 : FmSpace.x3,
                    ),
                    child: _RepoTile(repo: repos[i]),
                  ),
                ),
              );
            },
          ),
        ),
      ],
    );
  }

  List<Repository> _filteredAndSortedRepos(List<Repository> repos) {
    final filtered = repos
        .where((repo) => _matchesQuery(repo) && _matchesFilter(repo))
        .toList();
    filtered.sort(_compareRepos);
    return filtered;
  }

  bool _matchesQuery(Repository repo) {
    if (_query.isEmpty) return true;
    return repo.fullName.toLowerCase().contains(_query) ||
        repo.description.toLowerCase().contains(_query) ||
        repo.language.toLowerCase().contains(_query);
  }

  bool _matchesFilter(Repository repo) {
    final online = _isRepoOnline(repo);
    switch (_filter) {
      case _RepoFilter.all:
        return true;
      case _RepoFilter.online:
        return online;
      case _RepoFilter.offline:
        return !online;
      case _RepoFilter.public:
        return !repo.isPrivate;
      case _RepoFilter.private:
        return repo.isPrivate;
    }
  }

  int _compareRepos(Repository a, Repository b) {
    switch (_sort) {
      case _RepoSort.recentlyUpdated:
        final updated = b.updatedMs.compareTo(a.updatedMs);
        return updated == 0 ? _compareNames(a, b) : updated;
      case _RepoSort.name:
        return _compareNames(a, b);
      case _RepoSort.mirrors:
        final mirrors = b.mirrors.compareTo(a.mirrors);
        return mirrors == 0 ? _compareNames(a, b) : mirrors;
    }
  }

  int _compareNames(Repository a, Repository b) {
    return a.fullName.toLowerCase().compareTo(b.fullName.toLowerCase());
  }
}

class _RepoCatalogControls extends StatelessWidget {
  const _RepoCatalogControls({
    required this.filter,
    required this.sort,
    required this.onFilterChanged,
    required this.onSortChanged,
  });

  final _RepoFilter filter;
  final _RepoSort sort;
  final ValueChanged<_RepoFilter> onFilterChanged;
  final ValueChanged<_RepoSort> onSortChanged;

  @override
  Widget build(BuildContext context) {
    return Row(
      children: [
        Expanded(
          child: SizedBox(
            height: 36,
            child: ListView.separated(
              scrollDirection: Axis.horizontal,
              itemCount: _RepoFilter.values.length,
              separatorBuilder: (_, _) => const SizedBox(width: FmSpace.x2),
              itemBuilder: (context, i) {
                final option = _RepoFilter.values[i];
                final selected = option == filter;
                return ChoiceChip(
                  label: Text(option.label),
                  selected: selected,
                  showCheckmark: false,
                  visualDensity: VisualDensity.compact,
                  materialTapTargetSize: MaterialTapTargetSize.shrinkWrap,
                  onSelected: (_) => onFilterChanged(option),
                  backgroundColor: FmTheme.bgRaised(context),
                  selectedColor: FmTheme.accentSubtle(context),
                  side: BorderSide(
                    color: selected
                        ? FmTheme.accent(context)
                        : FmTheme.border(context),
                  ),
                  labelStyle: TextStyle(
                    color: selected
                        ? FmTheme.accent(context)
                        : FmTheme.textSecondary(context),
                    fontSize: 12,
                    fontWeight: FontWeight.w700,
                  ),
                );
              },
            ),
          ),
        ),
        const SizedBox(width: FmSpace.x2),
        PopupMenuButton<_RepoSort>(
          tooltip: 'Sort repositories',
          initialValue: sort,
          onSelected: onSortChanged,
          itemBuilder: (context) => _RepoSort.values
              .map(
                (option) => PopupMenuItem<_RepoSort>(
                  value: option,
                  child: Text(option.menuLabel),
                ),
              )
              .toList(),
          child: Container(
            height: 36,
            padding: const EdgeInsets.symmetric(horizontal: FmSpace.x3),
            decoration: BoxDecoration(
              color: FmTheme.bgRaised(context),
              borderRadius: BorderRadius.circular(FmRadius.full),
              border: Border.all(color: FmTheme.border(context)),
            ),
            child: Row(
              mainAxisSize: MainAxisSize.min,
              children: [
                Icon(
                  Icons.sort,
                  size: 16,
                  color: FmTheme.textSecondary(context),
                ),
                const SizedBox(width: FmSpace.x1),
                Text(
                  sort.buttonLabel,
                  style: TextStyle(
                    color: FmTheme.textSecondary(context),
                    fontSize: 12,
                    fontWeight: FontWeight.w700,
                  ),
                ),
              ],
            ),
          ),
        ),
      ],
    );
  }
}

class _RepoTile extends StatelessWidget {
  const _RepoTile({required this.repo});
  final Repository repo;

  @override
  Widget build(BuildContext context) {
    final online = _isRepoOnline(repo);
    return FmCard(
      onTap: () => Navigator.of(
        context,
      ).push(MaterialPageRoute(builder: (_) => RepoDetailScreen(repo: repo))),
      radius: FmRadius.lg,
      padding: const EdgeInsets.all(FmSpace.x4),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          Row(
            crossAxisAlignment: CrossAxisAlignment.start,
            children: [
              Container(
                width: 36,
                height: 36,
                decoration: BoxDecoration(
                  color: FmTheme.accentSubtle(context),
                  borderRadius: BorderRadius.circular(FmRadius.md),
                ),
                child: Icon(
                  Icons.book_outlined,
                  color: FmTheme.accent(context),
                  size: 20,
                ),
              ),
              const SizedBox(width: FmSpace.x3),
              Expanded(
                child: Text(
                  repo.fullName,
                  maxLines: 2,
                  overflow: TextOverflow.ellipsis,
                  style: TextStyle(
                    color: FmTheme.accent(context),
                    fontSize: 16,
                    fontWeight: FontWeight.w700,
                    height: 1.2,
                  ),
                ),
              ),
            ],
          ),
          if (repo.description.isNotEmpty)
            Padding(
              padding: const EdgeInsets.only(top: FmSpace.x3),
              child: Text(
                repo.description,
                maxLines: 2,
                overflow: TextOverflow.ellipsis,
                style: TextStyle(
                  color: FmTheme.textSecondary(context),
                  fontSize: 14,
                  height: 1.35,
                ),
              ),
            ),
          const SizedBox(height: FmSpace.x3),
          Wrap(
            spacing: FmSpace.x3,
            runSpacing: FmSpace.x2,
            crossAxisAlignment: WrapCrossAlignment.center,
            children: [
              if (repo.language.isNotEmpty)
                _RepoMetaItem(
                  icon: Icons.circle,
                  text: repo.language,
                  iconColor: FmColors.accentEdge,
                  iconSize: 10,
                ),
              _RepoMetaItem(icon: Icons.star_border, text: '${repo.stars}'),
              _RepoMetaItem(icon: Icons.call_split, text: '${repo.forks}'),
              _RepoMetaItem(
                icon: Icons.dns_outlined,
                text: '${repo.mirrors} mirror${repo.mirrors == 1 ? "" : "s"}',
              ),
              if (repo.updatedMs > 0)
                _RepoMetaItem(
                  icon: Icons.update,
                  text: 'Updated ${_formatRepoDate(repo.updatedMs)}',
                ),
            ],
          ),
          const SizedBox(height: FmSpace.x3),
          Wrap(
            spacing: FmSpace.x2,
            runSpacing: FmSpace.x2,
            children: [
              FmStatusBadge(
                label: online ? 'Online' : 'Offline',
                color: online ? FmTheme.success(context) : FmColors.offline,
              ),
              FmStatusBadge(
                label: repo.isPrivate ? 'Private' : 'Public',
                color: repo.isPrivate
                    ? FmTheme.warning(context)
                    : FmTheme.success(context),
              ),
            ],
          ),
        ],
      ),
    );
  }
}

bool _isRepoOnline(Repository repo) => repo.cloneOnline || repo.liveHost;

String _formatRepoDate(int ms) {
  final d = DateTime.fromMillisecondsSinceEpoch(ms, isUtc: true);
  final month = d.month.toString().padLeft(2, '0');
  final day = d.day.toString().padLeft(2, '0');
  return '${d.year}-$month-$day';
}

class _RepoMetaItem extends StatelessWidget {
  const _RepoMetaItem({
    required this.icon,
    required this.text,
    this.iconColor,
    this.iconSize = 14,
  });

  final IconData icon;
  final String text;
  final Color? iconColor;
  final double iconSize;

  @override
  Widget build(BuildContext context) {
    final metaColor = FmTheme.textTertiary(context);
    return Row(
      mainAxisSize: MainAxisSize.min,
      children: [
        Icon(icon, size: iconSize, color: iconColor ?? metaColor),
        const SizedBox(width: 4),
        Text(text, style: TextStyle(fontSize: 12, color: metaColor)),
      ],
    );
  }
}

class _ErrorView extends StatelessWidget {
  const _ErrorView({required this.message, required this.onRetry});
  final String message;
  final VoidCallback onRetry;
  @override
  Widget build(BuildContext context) => Center(
    child: Padding(
      padding: const EdgeInsets.all(FmSpace.x4),
      child: FmCard(
        radius: FmRadius.lg,
        child: Column(
          mainAxisSize: MainAxisSize.min,
          children: [
            FmEmptyState(
              icon: Icons.cloud_off,
              title: 'Could not load repositories',
              message: message,
            ),
            const SizedBox(height: FmSpace.x3),
            OutlinedButton(onPressed: onRetry, child: const Text('Retry')),
          ],
        ),
      ),
    ),
  );
}
