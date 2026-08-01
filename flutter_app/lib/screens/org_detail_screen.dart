import 'package:flutter/material.dart';
import 'package:provider/provider.dart';

import '../models/models.dart';
import '../services/api_service.dart';
import '../services/auth_service.dart';
import '../theme.dart';
import '../widgets/fm_ui.dart';
import 'orgs_screen.dart';





class OrgDetailScreen extends StatefulWidget {
  const OrgDetailScreen({super.key, required this.orgName});

  final String orgName;

  @override
  State<OrgDetailScreen> createState() => _OrgDetailScreenState();
}

class _OrgDetailScreenState extends State<OrgDetailScreen> {
  late final ApiService _api = context.read<ApiService>();

  bool _loading = true;
  String _error = '';
  OrgProfile? _profile;
  List<OrgMember> _members = const [];
  List<OrgTeam> _teams = const [];
  List<OrgRepo> _repos = const [];

  @override
  void initState() {
    super.initState();
    _load();
  }

  Future<void> _load() async {
    setState(() {
      _loading = true;
      _error = '';
    });
    try {
      final results = await Future.wait([
        _api.orgProfile(widget.orgName),
        _api.orgMembers(widget.orgName),
        _api.orgTeams(widget.orgName),
        _api.orgRepos(widget.orgName),
      ]);
      if (!mounted) return;
      setState(() {
        _profile = results[0] as OrgProfile;
        _members = results[1] as List<OrgMember>;
        _teams = results[2] as List<OrgTeam>;
        _repos = results[3] as List<OrgRepo>;
        _loading = false;
      });
    } catch (error) {
      if (!mounted) return;
      setState(() {
        _loading = false;
        _error = _messageFor(error);
      });
    }
  }

  bool get _canManage => _profile?.canManage == true;

  Future<void> _run(Future<void> Function() action) async {
    try {
      await action();
      await _load();
    } catch (error) {
      if (mounted) _toast(_messageFor(error));
    }
  }

  void _toast(String message) {
    if (!mounted) return;
    ScaffoldMessenger.of(context)
      ..hideCurrentSnackBar()
      ..showSnackBar(SnackBar(content: Text(message)));
  }

  @override
  Widget build(BuildContext context) {
    final profile = _profile;
    return Scaffold(
      backgroundColor: FmTheme.bgBase(context),
      body: SafeArea(
        child: Column(
          children: [
            OrgScreenHeader(
              title: profile?.title ?? widget.orgName,
              subtitle: '/${widget.orgName}',
              trailing: profile?.isOwner == true
                  ? IconButton(
                      key: const ValueKey('org-delete-button'),
                      tooltip: 'Delete organization',
                      onPressed: _confirmDelete,
                      icon: Icon(
                        Icons.delete_outline,
                        color: FmTheme.danger(context),
                      ),
                    )
                  : null,
            ),
            Expanded(child: _body()),
          ],
        ),
      ),
    );
  }

  Widget _body() {
    if (_loading) return const Center(child: CircularProgressIndicator());
    final profile = _profile;
    if (profile == null) {
      return ListView(
        children: [
          const SizedBox(height: FmSpace.x7),
          FmEmptyState(
            icon: Icons.cloud_off_outlined,
            title: 'Could not load organization',
            message: _error,
          ),
          Center(
            child: TextButton.icon(
              onPressed: _load,
              icon: const Icon(Icons.refresh, size: 18),
              label: const Text('Retry'),
            ),
          ),
        ],
      );
    }
    return RefreshIndicator(
      onRefresh: _load,
      child: ListView(
        padding: const EdgeInsets.all(FmSpace.x4),
        children: [
          _overview(profile),
          const SizedBox(height: FmSpace.x5),
          _membersSection(),
          const SizedBox(height: FmSpace.x5),
          _teamsSection(),
          const SizedBox(height: FmSpace.x5),
          _reposSection(),
        ],
      ),
    );
  }

  Widget _overview(OrgProfile profile) {
    return FmCard(
      radius: FmRadius.lg,
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          Row(
            children: [
              Expanded(
                child: Text(
                  profile.title,
                  style: TextStyle(
                    color: FmTheme.textPrimary(context),
                    fontSize: 18,
                    fontWeight: FontWeight.w800,
                  ),
                ),
              ),
              if (profile.viewerRole.isNotEmpty)
                OrgRoleBadge(role: profile.viewerRole),
            ],
          ),
          if (profile.description.isNotEmpty) ...[
            const SizedBox(height: FmSpace.x2),
            Text(
              profile.description,
              style: TextStyle(
                color: FmTheme.textSecondary(context),
                fontSize: 13,
                height: 1.35,
              ),
            ),
          ],
          const SizedBox(height: FmSpace.x3),
          Row(
            children: [
              _stat('Members', profile.members),
              _stat('Teams', profile.teams),
              _stat('Repos', profile.repos.length),
            ],
          ),
        ],
      ),
    );
  }

  Widget _stat(String label, int value) {
    return Expanded(
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          Text(
            '$value',
            style: TextStyle(
              color: FmTheme.textPrimary(context),
              fontSize: 20,
              fontWeight: FontWeight.w800,
            ),
          ),
          Text(
            label,
            style: TextStyle(
              color: FmTheme.textTertiary(context),
              fontSize: 12,
            ),
          ),
        ],
      ),
    );
  }



  Widget _membersSection() {
    return _Section(
      title: 'Members',
      count: _members.length,
      onAdd: _canManage ? _addMember : null,
      children: [
        for (final member in _members)
          _MemberRow(
            member: member,
            canManage: _canManage,
            onChangeRole: (role) => _run(
              () => _api.setOrgMember(widget.orgName, member.name, role: role),
            ),
            onRemove: () =>
                _run(() => _api.removeOrgMember(widget.orgName, member.name)),
          ),
      ],
    );
  }

  Future<void> _addMember() async {
    final result = await _promptMember(context);
    if (result == null) return;
    await _run(
      () => _api.setOrgMember(widget.orgName, result.name, role: result.role),
    );
  }



  Widget _teamsSection() {
    return _Section(
      title: 'Teams',
      count: _teams.length,
      onAdd: _canManage ? _addTeam : null,
      emptyLabel: 'No teams yet',
      children: [
        for (final team in _teams)
          _TeamRow(
            team: team,
            canManage: _canManage,
            onOpen: () => _openTeam(team),
            onChangePermission: (permission) => _run(
              () => _api.setOrgTeam(
                widget.orgName,
                team.team,
                permission: permission,
              ),
            ),
            onDelete: () =>
                _run(() => _api.deleteOrgTeam(widget.orgName, team.team)),
          ),
      ],
    );
  }

  Future<void> _addTeam() async {
    final result = await _promptTeam(context);
    if (result == null) return;
    await _run(
      () => _api.setOrgTeam(
        widget.orgName,
        result.name,
        permission: result.permission,
      ),
    );
  }

  Future<void> _openTeam(OrgTeam team) async {
    await Navigator.of(context).push(
      MaterialPageRoute<void>(
        builder: (_) => _TeamDetailScreen(
          orgName: widget.orgName,
          team: team,
          orgMembers: _members,
          canManage: _canManage,
        ),
      ),
    );
    if (mounted) _load();
  }



  Widget _reposSection() {
    return _Section(
      title: 'Linked repos',
      count: _repos.length,
      onAdd: _canManage ? _linkRepo : null,
      emptyLabel: 'No repos linked',
      children: [
        for (final repo in _repos)
          _RepoRow(
            repo: repo,
            orgName: widget.orgName,
            canManage: _canManage,
            onUnlink: () =>
                _run(() => _api.unlinkOrgRepo(widget.orgName, repo.repo)),
          ),
      ],
    );
  }

  Future<void> _linkRepo() async {
    final account =
        context.read<AuthService>().session?.nodeName.trim().toLowerCase() ??
        '';
    final result = await _promptRepo(context, defaultNode: account);
    if (result == null) return;
    await _run(
      () => _api.linkOrgRepo(widget.orgName, result.repo, node: result.node),
    );
  }

  Future<void> _confirmDelete() async {
    final ok = await showDialog<bool>(
      context: context,
      builder: (context) => AlertDialog(
        backgroundColor: FmTheme.bgOverlay(context),
        surfaceTintColor: Colors.transparent,
        title: Text('Delete ${widget.orgName}?'),
        content: const Text(
          'This dissolves the organization, its members, teams, and repo links. '
          'The linked repos themselves are not deleted. This cannot be undone.',
        ),
        actions: [
          TextButton(
            onPressed: () => Navigator.of(context).pop(false),
            child: const Text('Cancel'),
          ),
          FilledButton(
            style: FilledButton.styleFrom(
              backgroundColor: FmTheme.danger(context),
            ),
            onPressed: () => Navigator.of(context).pop(true),
            child: const Text('Delete'),
          ),
        ],
      ),
    );
    if (ok != true) return;
    try {
      await _api.deleteOrg(widget.orgName);
      if (mounted) Navigator.of(context).pop();
    } catch (error) {
      if (mounted) _toast(_messageFor(error));
    }
  }
}






class _TeamDetailScreen extends StatefulWidget {
  const _TeamDetailScreen({
    required this.orgName,
    required this.team,
    required this.orgMembers,
    required this.canManage,
  });

  final String orgName;
  final OrgTeam team;
  final List<OrgMember> orgMembers;
  final bool canManage;

  @override
  State<_TeamDetailScreen> createState() => _TeamDetailScreenState();
}

class _TeamDetailScreenState extends State<_TeamDetailScreen> {
  late final ApiService _api = context.read<ApiService>();
  bool _loading = true;
  String _error = '';
  List<OrgMember> _members = const [];

  @override
  void initState() {
    super.initState();
    _load();
  }

  Future<void> _load() async {
    setState(() {
      _loading = true;
      _error = '';
    });
    try {
      final members = await _api.orgTeamMembers(
        widget.orgName,
        widget.team.team,
      );
      if (!mounted) return;
      setState(() {
        _members = members;
        _loading = false;
      });
    } catch (error) {
      if (!mounted) return;
      setState(() {
        _loading = false;
        _error = _messageFor(error);
      });
    }
  }

  Future<void> _run(Future<void> Function() action) async {
    try {
      await action();
      await _load();
    } catch (error) {
      if (mounted) {
        ScaffoldMessenger.of(context)
          ..hideCurrentSnackBar()
          ..showSnackBar(SnackBar(content: Text(_messageFor(error))));
      }
    }
  }

  Future<void> _addToTeam() async {
    final onTeam = _members.map((m) => m.name).toSet();
    final candidates = widget.orgMembers
        .where((m) => !onTeam.contains(m.name))
        .toList();
    if (candidates.isEmpty) {
      ScaffoldMessenger.of(context).showSnackBar(
        const SnackBar(
          content: Text('Every org member is already on this team.'),
        ),
      );
      return;
    }
    final picked = await showModalBottomSheet<String>(
      context: context,
      backgroundColor: Colors.transparent,
      builder: (_) => _PickMemberSheet(candidates: candidates),
    );
    if (picked == null) return;
    await _run(
      () => _api.addOrgTeamMember(widget.orgName, widget.team.team, picked),
    );
  }

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      backgroundColor: FmTheme.bgBase(context),
      body: SafeArea(
        child: Column(
          children: [
            OrgScreenHeader(
              title: widget.team.team,
              subtitle: '${widget.team.permission} permission',
              trailing: widget.canManage
                  ? IconButton(
                      tooltip: 'Add member to team',
                      onPressed: _addToTeam,
                      icon: Icon(
                        Icons.add,
                        color: FmTheme.textPrimary(context),
                      ),
                    )
                  : null,
            ),
            Expanded(child: _body()),
          ],
        ),
      ),
    );
  }

  Widget _body() {
    if (_loading) return const Center(child: CircularProgressIndicator());
    if (_members.isEmpty) {
      return FmEmptyState(
        icon: Icons.group_outlined,
        title: 'No members on this team',
        message: _error.isNotEmpty
            ? _error
            : 'Add org members to grant them the ${widget.team.permission} '
                  'permission over linked repos.',
      );
    }
    return ListView.separated(
      padding: const EdgeInsets.all(FmSpace.x4),
      itemCount: _members.length,
      separatorBuilder: (_, _) => const SizedBox(height: FmSpace.x2),
      itemBuilder: (context, index) {
        final member = _members[index];
        return FmCard(
          radius: FmRadius.lg,
          child: Row(
            children: [
              Expanded(
                child: Text(
                  member.name,
                  style: TextStyle(
                    color: FmTheme.textPrimary(context),
                    fontWeight: FontWeight.w700,
                  ),
                ),
              ),
              if (widget.canManage)
                IconButton(
                  tooltip: 'Remove from team',
                  onPressed: () => _run(
                    () => _api.removeOrgTeamMember(
                      widget.orgName,
                      widget.team.team,
                      member.name,
                    ),
                  ),
                  icon: Icon(
                    Icons.close,
                    color: FmTheme.textTertiary(context),
                    size: 20,
                  ),
                ),
            ],
          ),
        );
      },
    );
  }
}



class _Section extends StatelessWidget {
  const _Section({
    required this.title,
    required this.count,
    required this.children,
    this.onAdd,
    this.emptyLabel,
  });

  final String title;
  final int count;
  final List<Widget> children;
  final VoidCallback? onAdd;
  final String? emptyLabel;

  @override
  Widget build(BuildContext context) {
    return Column(
      crossAxisAlignment: CrossAxisAlignment.stretch,
      children: [
        FmSectionHeader(
          title: title,
          count: count,
          trailing: onAdd == null
              ? null
              : IconButton(
                  visualDensity: VisualDensity.compact,
                  padding: EdgeInsets.zero,
                  constraints: const BoxConstraints(),
                  tooltip: 'Add',
                  onPressed: onAdd,
                  icon: Icon(
                    Icons.add_circle_outline,
                    color: FmTheme.accent(context),
                    size: 20,
                  ),
                ),
        ),
        const SizedBox(height: FmSpace.x2),
        if (children.isEmpty)
          FmCard(
            radius: FmRadius.lg,
            child: Text(
              emptyLabel ?? 'Nothing here yet',
              style: TextStyle(
                color: FmTheme.textTertiary(context),
                fontSize: 13,
              ),
            ),
          )
        else
          for (var i = 0; i < children.length; i++) ...[
            children[i],
            if (i != children.length - 1) const SizedBox(height: FmSpace.x2),
          ],
      ],
    );
  }
}

class _MemberRow extends StatelessWidget {
  const _MemberRow({
    required this.member,
    required this.canManage,
    required this.onChangeRole,
    required this.onRemove,
  });

  final OrgMember member;
  final bool canManage;
  final ValueChanged<String> onChangeRole;
  final VoidCallback onRemove;

  @override
  Widget build(BuildContext context) {
    return FmCard(
      radius: FmRadius.lg,
      child: Row(
        children: [
          Expanded(
            child: Text(
              member.name,
              maxLines: 1,
              overflow: TextOverflow.ellipsis,
              style: TextStyle(
                color: FmTheme.textPrimary(context),
                fontWeight: FontWeight.w700,
              ),
            ),
          ),
          const SizedBox(width: FmSpace.x2),
          OrgRoleBadge(role: member.role),
          if (canManage)
            PopupMenuButton<String>(
              icon: Icon(Icons.more_vert, color: FmTheme.textTertiary(context)),
              onSelected: (value) {
                if (value == 'remove') {
                  onRemove();
                } else {
                  onChangeRole(value);
                }
              },
              itemBuilder: (context) => [
                for (final role in orgRoles)
                  PopupMenuItem(
                    value: role,
                    enabled: role != member.role,
                    child: Text('Set role: $role'),
                  ),
                const PopupMenuDivider(),
                const PopupMenuItem(
                  value: 'remove',
                  child: Text('Remove from org'),
                ),
              ],
            ),
        ],
      ),
    );
  }
}

class _TeamRow extends StatelessWidget {
  const _TeamRow({
    required this.team,
    required this.canManage,
    required this.onOpen,
    required this.onChangePermission,
    required this.onDelete,
  });

  final OrgTeam team;
  final bool canManage;
  final VoidCallback onOpen;
  final ValueChanged<String> onChangePermission;
  final VoidCallback onDelete;

  @override
  Widget build(BuildContext context) {
    return FmCard(
      radius: FmRadius.lg,
      onTap: onOpen,
      child: Row(
        children: [
          Expanded(
            child: Column(
              crossAxisAlignment: CrossAxisAlignment.start,
              children: [
                Text(
                  team.team,
                  maxLines: 1,
                  overflow: TextOverflow.ellipsis,
                  style: TextStyle(
                    color: FmTheme.textPrimary(context),
                    fontWeight: FontWeight.w700,
                  ),
                ),
                const SizedBox(height: FmSpace.x1),
                Text(
                  '${team.members} member${team.members == 1 ? '' : 's'}',
                  style: TextStyle(
                    color: FmTheme.textTertiary(context),
                    fontSize: 12,
                  ),
                ),
              ],
            ),
          ),
          const SizedBox(width: FmSpace.x2),
          FmStatusBadge(
            label: team.permission,
            color: FmTheme.textSecondary(context),
          ),
          if (canManage)
            PopupMenuButton<String>(
              icon: Icon(Icons.more_vert, color: FmTheme.textTertiary(context)),
              onSelected: (value) {
                if (value == 'delete') {
                  onDelete();
                } else if (value.startsWith('perm:')) {
                  onChangePermission(value.substring(5));
                }
              },
              itemBuilder: (context) => [
                for (final permission in orgTeamPermissions)
                  PopupMenuItem(
                    value: 'perm:$permission',
                    enabled: permission != team.permission,
                    child: Text('Permission: $permission'),
                  ),
                const PopupMenuDivider(),
                const PopupMenuItem(
                  value: 'delete',
                  child: Text('Delete team'),
                ),
              ],
            ),
        ],
      ),
    );
  }
}

class _RepoRow extends StatelessWidget {
  const _RepoRow({
    required this.repo,
    required this.orgName,
    required this.canManage,
    required this.onUnlink,
  });

  final OrgRepo repo;
  final String orgName;
  final bool canManage;
  final VoidCallback onUnlink;

  @override
  Widget build(BuildContext context) {
    return FmCard(
      radius: FmRadius.lg,
      child: Row(
        children: [
          Expanded(
            child: Column(
              crossAxisAlignment: CrossAxisAlignment.start,
              children: [
                Text(
                  '$orgName/${repo.repo}',
                  maxLines: 1,
                  overflow: TextOverflow.ellipsis,
                  style: TextStyle(
                    color: FmTheme.textPrimary(context),
                    fontWeight: FontWeight.w700,
                  ),
                ),
                if (repo.node.isNotEmpty) ...[
                  const SizedBox(height: FmSpace.x1),
                  Text(
                    'serves ${repo.node}/${repo.repo}',
                    maxLines: 1,
                    overflow: TextOverflow.ellipsis,
                    style: TextStyle(
                      color: FmTheme.textTertiary(context),
                      fontSize: 12,
                    ),
                  ),
                ],
              ],
            ),
          ),
          if (canManage)
            IconButton(
              tooltip: 'Unlink repo',
              onPressed: onUnlink,
              icon: Icon(
                Icons.link_off,
                color: FmTheme.textTertiary(context),
                size: 20,
              ),
            ),
        ],
      ),
    );
  }
}

class _PickMemberSheet extends StatelessWidget {
  const _PickMemberSheet({required this.candidates});

  final List<OrgMember> candidates;

  @override
  Widget build(BuildContext context) {
    return Container(
      decoration: BoxDecoration(
        color: FmTheme.bgRaised(context),
        borderRadius: const BorderRadius.vertical(
          top: Radius.circular(FmRadius.lg),
        ),
      ),
      child: SafeArea(
        top: false,
        child: Column(
          mainAxisSize: MainAxisSize.min,
          children: [
            Padding(
              padding: const EdgeInsets.all(FmSpace.x4),
              child: Align(
                alignment: Alignment.centerLeft,
                child: Text(
                  'Add member to team',
                  style: TextStyle(
                    color: FmTheme.textPrimary(context),
                    fontSize: 18,
                    fontWeight: FontWeight.w700,
                  ),
                ),
              ),
            ),
            Flexible(
              child: ListView.builder(
                shrinkWrap: true,
                itemCount: candidates.length,
                itemBuilder: (context, index) {
                  final member = candidates[index];
                  return ListTile(
                    title: Text(
                      member.name,
                      style: TextStyle(color: FmTheme.textPrimary(context)),
                    ),
                    trailing: OrgRoleBadge(role: member.role),
                    onTap: () => Navigator.of(context).pop(member.name),
                  );
                },
              ),
            ),
          ],
        ),
      ),
    );
  }
}



class _MemberInput {
  const _MemberInput(this.name, this.role);
  final String name;
  final String role;
}

Future<_MemberInput?> _promptMember(BuildContext context) {
  return showModalBottomSheet<_MemberInput>(
    context: context,
    isScrollControlled: true,
    backgroundColor: Colors.transparent,
    builder: (_) => const _MemberPromptSheet(),
  );
}

class _MemberPromptSheet extends StatefulWidget {
  const _MemberPromptSheet();

  @override
  State<_MemberPromptSheet> createState() => _MemberPromptSheetState();
}

class _MemberPromptSheetState extends State<_MemberPromptSheet> {
  final _name = TextEditingController();
  String _role = 'member';

  @override
  void dispose() {
    _name.dispose();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    return _PromptScaffold(
      title: 'Add or update member',
      helper: 'Enter an existing account name. New members are notified.',
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.stretch,
        children: [
          OrgTextField(
            key: const ValueKey('org-member-field'),
            controller: _name,
            label: 'Account name',
            autofocus: true,
          ),
          _RoleDropdown(
            value: _role,
            options: orgRoles,
            label: 'Role',
            onChanged: (v) => setState(() => _role = v),
          ),
        ],
      ),
      onSubmit: () {
        final name = _name.text.trim().toLowerCase();
        if (name.isEmpty) return;
        Navigator.of(context).pop(_MemberInput(name, _role));
      },
    );
  }
}

class _TeamInput {
  const _TeamInput(this.name, this.permission);
  final String name;
  final String permission;
}

Future<_TeamInput?> _promptTeam(BuildContext context) {
  return showModalBottomSheet<_TeamInput>(
    context: context,
    isScrollControlled: true,
    backgroundColor: Colors.transparent,
    builder: (_) => const _TeamPromptSheet(),
  );
}

class _TeamPromptSheet extends StatefulWidget {
  const _TeamPromptSheet();

  @override
  State<_TeamPromptSheet> createState() => _TeamPromptSheetState();
}

class _TeamPromptSheetState extends State<_TeamPromptSheet> {
  final _name = TextEditingController();
  String _permission = 'read';

  @override
  void dispose() {
    _name.dispose();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    return _PromptScaffold(
      title: 'New or updated team',
      helper: 'A team grants one permission over the org\'s linked repos.',
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.stretch,
        children: [
          OrgTextField(
            key: const ValueKey('org-team-field'),
            controller: _name,
            label: 'Team name (lowercase, dashes)',
            autofocus: true,
          ),
          _RoleDropdown(
            value: _permission,
            options: orgTeamPermissions,
            label: 'Permission',
            onChanged: (v) => setState(() => _permission = v),
          ),
        ],
      ),
      onSubmit: () {
        final name = _name.text.trim().toLowerCase();
        if (name.isEmpty) return;
        Navigator.of(context).pop(_TeamInput(name, _permission));
      },
    );
  }
}

class _RepoInput {
  const _RepoInput(this.repo, this.node);
  final String repo;
  final String node;
}

Future<_RepoInput?> _promptRepo(
  BuildContext context, {
  required String defaultNode,
}) {
  return showModalBottomSheet<_RepoInput>(
    context: context,
    isScrollControlled: true,
    backgroundColor: Colors.transparent,
    builder: (_) => _RepoPromptSheet(defaultNode: defaultNode),
  );
}

class _RepoPromptSheet extends StatefulWidget {
  const _RepoPromptSheet({required this.defaultNode});

  final String defaultNode;

  @override
  State<_RepoPromptSheet> createState() => _RepoPromptSheetState();
}

class _RepoPromptSheetState extends State<_RepoPromptSheet> {
  final _repo = TextEditingController();
  late final _node = TextEditingController(text: widget.defaultNode);

  @override
  void dispose() {
    _repo.dispose();
    _node.dispose();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    return _PromptScaffold(
      title: 'Link a repo',
      helper:
          'Link one of your own node\'s published repos to serve it at '
          '/<org>/<repo>.',
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.stretch,
        children: [
          OrgTextField(
            key: const ValueKey('org-repo-field'),
            controller: _repo,
            label: 'Repo name',
            autofocus: true,
          ),
          OrgTextField(
            controller: _node,
            label: 'Your node (defaults to your account)',
          ),
        ],
      ),
      onSubmit: () {
        final repo = _repo.text.trim().toLowerCase();
        if (repo.isEmpty) return;
        Navigator.of(context).pop(_RepoInput(repo, _node.text.trim()));
      },
    );
  }
}

class _RoleDropdown extends StatelessWidget {
  const _RoleDropdown({
    required this.value,
    required this.options,
    required this.label,
    required this.onChanged,
  });

  final String value;
  final List<String> options;
  final String label;
  final ValueChanged<String> onChanged;

  @override
  Widget build(BuildContext context) {
    return Padding(
      padding: const EdgeInsets.only(bottom: FmSpace.x3),
      child: DropdownButtonFormField<String>(
        initialValue: value,
        decoration: InputDecoration(
          labelText: label,
          filled: true,
          fillColor: FmTheme.bgBase(context),
          contentPadding: const EdgeInsets.symmetric(
            horizontal: FmSpace.x4,
            vertical: FmSpace.x3,
          ),
          enabledBorder: OutlineInputBorder(
            borderRadius: BorderRadius.circular(FmRadius.md),
            borderSide: BorderSide.none,
          ),
        ),
        items: [
          for (final option in options)
            DropdownMenuItem(value: option, child: Text(option)),
        ],
        onChanged: (v) => onChanged(v ?? value),
      ),
    );
  }
}

class _PromptScaffold extends StatelessWidget {
  const _PromptScaffold({
    required this.title,
    required this.helper,
    required this.child,
    required this.onSubmit,
  });

  final String title;
  final String helper;
  final Widget child;
  final VoidCallback onSubmit;

  @override
  Widget build(BuildContext context) {
    final insets = MediaQuery.viewInsetsOf(context).bottom;
    return Padding(
      padding: EdgeInsets.only(bottom: insets),
      child: Container(
        decoration: BoxDecoration(
          color: FmTheme.bgRaised(context),
          borderRadius: const BorderRadius.vertical(
            top: Radius.circular(FmRadius.lg),
          ),
        ),
        padding: const EdgeInsets.all(FmSpace.x4),
        child: SafeArea(
          top: false,
          child: Column(
            mainAxisSize: MainAxisSize.min,
            crossAxisAlignment: CrossAxisAlignment.stretch,
            children: [
              Text(
                title,
                style: TextStyle(
                  color: FmTheme.textPrimary(context),
                  fontSize: 20,
                  fontWeight: FontWeight.w700,
                ),
              ),
              const SizedBox(height: FmSpace.x2),
              Text(
                helper,
                style: TextStyle(
                  color: FmTheme.textSecondary(context),
                  fontSize: 12,
                  height: 1.35,
                ),
              ),
              const SizedBox(height: FmSpace.x4),
              child,
              const SizedBox(height: FmSpace.x2),
              FilledButton(
                key: const ValueKey('org-prompt-submit'),
                onPressed: onSubmit,
                child: const Text('Save'),
              ),
            ],
          ),
        ),
      ),
    );
  }
}

String _messageFor(Object? error) {
  if (error is OrgApiException) return error.message;
  return 'Something went wrong. Check your connection and try again.';
}
