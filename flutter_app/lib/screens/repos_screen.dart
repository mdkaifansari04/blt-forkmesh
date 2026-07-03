import 'package:flutter/material.dart';
import 'package:provider/provider.dart';

import '../models/models.dart';
import '../services/api_service.dart';
import '../theme.dart';
import '../widgets/fm_ui.dart';
import 'repo_detail_screen.dart';

/// The public catalog of repositories across the network (Code section).
class ReposScreen extends StatefulWidget {
  const ReposScreen({super.key});

  @override
  State<ReposScreen> createState() => _ReposScreenState();
}

class _ReposScreenState extends State<ReposScreen> {
  late Future<List<Repository>> _future;
  String _query = '';

  @override
  void initState() {
    super.initState();
    _future = context.read<ApiService>().repositories();
  }

  void _reload() {
    setState(() => _future = context.read<ApiService>().repositories());
  }

  @override
  Widget build(BuildContext context) {
    return Column(
      children: [
        Padding(
          padding: const EdgeInsets.all(FmSpace.x4),
          child: Row(
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
                    onChanged: (v) => setState(() => _query = v.toLowerCase()),
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
              final repos = (snap.data ?? [])
                  .where(
                    (r) =>
                        _query.isEmpty ||
                        r.fullName.toLowerCase().contains(_query),
                  )
                  .toList();
              if (repos.isEmpty) {
                return const FmEmptyState(
                  icon: Icons.book_outlined,
                  title: 'No repositories',
                  message:
                      'Repository mirrors will appear here when available.',
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
}

class _RepoTile extends StatelessWidget {
  const _RepoTile({required this.repo});
  final Repository repo;

  @override
  Widget build(BuildContext context) {
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
              const SizedBox(width: FmSpace.x2),
              FmStatusBadge(
                label: repo.cloneOnline || repo.liveHost ? 'Online' : 'Offline',
                color: repo.cloneOnline || repo.liveHost
                    ? FmTheme.success(context)
                    : FmColors.offline,
              ),
              FmStatusBadge(
                label: repo.isPrivate ? 'Private' : 'Public',
                color: repo.isPrivate
                    ? FmTheme.warning(context)
                    : FmTheme.success(context),
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
            ],
          ),
        ],
      ),
    );
  }
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
