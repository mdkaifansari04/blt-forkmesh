import 'package:flutter/material.dart';
import 'package:provider/provider.dart';

import '../models/models.dart';
import '../services/api_service.dart';
import '../theme.dart';
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
          padding: const EdgeInsets.all(12),
          child: Row(children: [
            Expanded(
              child: TextField(
                decoration: const InputDecoration(
                  hintText: 'Find a repository…',
                  prefixIcon: Icon(Icons.search, size: 18),
                ),
                onChanged: (v) => setState(() => _query = v.toLowerCase()),
              ),
            ),
            const SizedBox(width: 8),
            IconButton(onPressed: _reload, icon: const Icon(Icons.refresh)),
          ]),
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
                  .where((r) => _query.isEmpty || r.fullName.toLowerCase().contains(_query))
                  .toList();
              if (repos.isEmpty) {
                return const Center(
                    child: Text('No repositories.', style: TextStyle(color: FmColors.textMuted)));
              }
              return RefreshIndicator(
                onRefresh: () async => _reload(),
                child: ListView.separated(
                  itemCount: repos.length,
                  separatorBuilder: (_, _) => const Divider(height: 1),
                  itemBuilder: (_, i) => _RepoTile(repo: repos[i]),
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
    return ListTile(
      leading: const Icon(Icons.book_outlined, color: FmColors.accent),
      title: Row(children: [
        Flexible(
          child: Text(repo.fullName,
              style: const TextStyle(color: FmColors.accent, fontWeight: FontWeight.w600),
              overflow: TextOverflow.ellipsis),
        ),
        if (repo.isPrivate) ...[
          const SizedBox(width: 8),
          _Pill(text: 'Private'),
        ],
      ]),
      subtitle: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          if (repo.description.isNotEmpty)
            Text(repo.description, maxLines: 2, overflow: TextOverflow.ellipsis),
          const SizedBox(height: 4),
          Row(children: [
            if (repo.language.isNotEmpty) ...[
              const Icon(Icons.circle, size: 10, color: FmColors.accentEdge),
              const SizedBox(width: 4),
              Text(repo.language, style: const TextStyle(fontSize: 12, color: FmColors.textMuted)),
              const SizedBox(width: 12),
            ],
            const Icon(Icons.star_border, size: 14, color: FmColors.textMuted),
            Text(' ${repo.stars}',
                style: const TextStyle(fontSize: 12, color: FmColors.textMuted)),
            const SizedBox(width: 12),
            const Icon(Icons.call_split, size: 14, color: FmColors.textMuted),
            Text(' ${repo.forks}',
                style: const TextStyle(fontSize: 12, color: FmColors.textMuted)),
            const SizedBox(width: 12),
            const Icon(Icons.dns_outlined, size: 14, color: FmColors.textMuted),
            Text(' ${repo.mirrors} mirror${repo.mirrors == 1 ? "" : "s"}',
                style: const TextStyle(fontSize: 12, color: FmColors.textMuted)),
          ]),
        ],
      ),
      onTap: () => Navigator.of(context).push(
        MaterialPageRoute(builder: (_) => RepoDetailScreen(repo: repo)),
      ),
    );
  }
}

class _Pill extends StatelessWidget {
  const _Pill({required this.text});
  final String text;
  @override
  Widget build(BuildContext context) => Container(
        padding: const EdgeInsets.symmetric(horizontal: 8, vertical: 1),
        decoration: BoxDecoration(
          border: Border.all(color: FmColors.border),
          borderRadius: BorderRadius.circular(10),
        ),
        child: Text(text, style: const TextStyle(fontSize: 11, color: FmColors.textMuted)),
      );
}

class _ErrorView extends StatelessWidget {
  const _ErrorView({required this.message, required this.onRetry});
  final String message;
  final VoidCallback onRetry;
  @override
  Widget build(BuildContext context) => Center(
        child: Column(mainAxisSize: MainAxisSize.min, children: [
          const Icon(Icons.cloud_off, color: FmColors.textMuted, size: 32),
          const SizedBox(height: 8),
          Padding(
            padding: const EdgeInsets.symmetric(horizontal: 24),
            child: Text(message,
                textAlign: TextAlign.center, style: const TextStyle(color: FmColors.textMuted)),
          ),
          const SizedBox(height: 8),
          OutlinedButton(onPressed: onRetry, child: const Text('Retry')),
        ]),
      );
}
