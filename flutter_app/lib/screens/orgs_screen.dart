import 'package:flutter/material.dart';
import 'package:provider/provider.dart';

import '../models/models.dart';
import '../services/api_service.dart';
import '../services/auth_service.dart';
import '../theme.dart';
import '../widgets/fm_ui.dart';
import 'org_detail_screen.dart';

/// Lists the organizations the signed-in account belongs to and lets it create
/// a new one. Mirrors the Worker's `/api/orgs` collection; tapping an org opens
/// [OrgDetailScreen] for member/team/repo management.
class OrgsScreen extends StatefulWidget {
  const OrgsScreen({super.key});

  @override
  State<OrgsScreen> createState() => _OrgsScreenState();
}

class _OrgsScreenState extends State<OrgsScreen> {
  late final ApiService _api = context.read<ApiService>();
  Future<List<OrgSummary>>? _future;

  @override
  void initState() {
    super.initState();
    if (_signedIn) _reload();
  }

  void _reload() {
    setState(() {
      _future = _api.myOrgs();
    });
  }

  bool get _signedIn {
    final session = context.read<AuthService>().session;
    return session != null && session.sessionKind != 'preview';
  }

  Future<void> _createOrg() async {
    final created = await showModalBottomSheet<OrgSummary>(
      context: context,
      isScrollControlled: true,
      backgroundColor: Colors.transparent,
      builder: (_) => const _CreateOrgSheet(),
    );
    if (created == null || !mounted) return;
    _reload();
    _openOrg(created.name);
  }

  void _openOrg(String name) {
    Navigator.of(context).push(
      MaterialPageRoute<void>(
        builder: (_) => OrgDetailScreen(orgName: name),
      ),
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
              title: 'Organizations',
              subtitle: 'Shared /<org>/<repo> namespaces',
              trailing: _signedIn
                  ? IconButton(
                      key: const ValueKey('org-create-button'),
                      tooltip: 'New organization',
                      onPressed: _createOrg,
                      icon: Icon(Icons.add, color: FmTheme.textPrimary(context)),
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
    if (!_signedIn) {
      return const FmEmptyState(
        icon: Icons.apartment_outlined,
        title: 'Sign in to manage organizations',
        message:
            'Organizations are tied to your Worker account. Sign in, then create '
            'or manage orgs and the repos they serve.',
      );
    }
    return RefreshIndicator(
      onRefresh: () async => _reload(),
      child: FutureBuilder<List<OrgSummary>>(
        future: _future,
        builder: (context, snapshot) {
          if (snapshot.connectionState == ConnectionState.waiting) {
            return const Center(child: CircularProgressIndicator());
          }
          if (snapshot.hasError) {
            return ListView(
              children: [
                const SizedBox(height: FmSpace.x7),
                FmEmptyState(
                  icon: Icons.cloud_off_outlined,
                  title: 'Could not load organizations',
                  message: _messageFor(snapshot.error),
                ),
                Center(
                  child: TextButton.icon(
                    onPressed: _reload,
                    icon: const Icon(Icons.refresh, size: 18),
                    label: const Text('Retry'),
                  ),
                ),
              ],
            );
          }
          final orgs = snapshot.data ?? const <OrgSummary>[];
          if (orgs.isEmpty) {
            return ListView(
              children: [
                const SizedBox(height: FmSpace.x7),
                const FmEmptyState(
                  icon: Icons.apartment_outlined,
                  title: 'No organizations yet',
                  message:
                      'Create one to serve your repos at /<org>/<repo> and share '
                      'management with teams.',
                ),
                Center(
                  child: FilledButton.icon(
                    onPressed: _createOrg,
                    icon: const Icon(Icons.add, size: 18),
                    label: const Text('New organization'),
                  ),
                ),
              ],
            );
          }
          return ListView.separated(
            padding: const EdgeInsets.all(FmSpace.x4),
            itemCount: orgs.length,
            separatorBuilder: (_, _) => const SizedBox(height: FmSpace.x2),
            itemBuilder: (context, index) {
              final org = orgs[index];
              return _OrgRow(org: org, onTap: () => _openOrg(org.name));
            },
          );
        },
      ),
    );
  }
}

class _OrgRow extends StatelessWidget {
  const _OrgRow({required this.org, required this.onTap});

  final OrgSummary org;
  final VoidCallback onTap;

  @override
  Widget build(BuildContext context) {
    return FmCard(
      radius: FmRadius.lg,
      onTap: onTap,
      child: Row(
        children: [
          Container(
            width: 42,
            height: 42,
            decoration: BoxDecoration(
              color: FmTheme.accentSubtle(context),
              borderRadius: BorderRadius.circular(FmRadius.md),
            ),
            child: Icon(Icons.apartment_outlined, color: FmTheme.accent(context)),
          ),
          const SizedBox(width: FmSpace.x3),
          Expanded(
            child: Text(
              org.name,
              maxLines: 1,
              overflow: TextOverflow.ellipsis,
              style: TextStyle(
                color: FmTheme.textPrimary(context),
                fontWeight: FontWeight.w800,
              ),
            ),
          ),
          const SizedBox(width: FmSpace.x2),
          OrgRoleBadge(role: org.role),
          const SizedBox(width: FmSpace.x2),
          Icon(Icons.chevron_right, color: FmTheme.textDisabled(context)),
        ],
      ),
    );
  }
}

class _CreateOrgSheet extends StatefulWidget {
  const _CreateOrgSheet();

  @override
  State<_CreateOrgSheet> createState() => _CreateOrgSheetState();
}

class _CreateOrgSheetState extends State<_CreateOrgSheet> {
  final _name = TextEditingController();
  final _displayName = TextEditingController();
  final _description = TextEditingController();
  bool _busy = false;
  String _error = '';

  @override
  void dispose() {
    _name.dispose();
    _displayName.dispose();
    _description.dispose();
    super.dispose();
  }

  Future<void> _submit() async {
    final name = _name.text.trim().toLowerCase();
    if (name.isEmpty) {
      setState(() => _error = 'Enter an organization name.');
      return;
    }
    setState(() {
      _busy = true;
      _error = '';
    });
    try {
      final created = await context.read<ApiService>().createOrg(
        name: name,
        displayName: _displayName.text,
        description: _description.text,
      );
      if (mounted) Navigator.of(context).pop(created);
    } catch (error) {
      if (mounted) {
        setState(() {
          _busy = false;
          _error = _messageFor(error);
        });
      }
    }
  }

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
                'New organization',
                style: TextStyle(
                  color: FmTheme.textPrimary(context),
                  fontSize: 20,
                  fontWeight: FontWeight.w700,
                ),
              ),
              const SizedBox(height: FmSpace.x2),
              Text(
                'The name shares the /<name>/<repo> namespace with accounts, so it '
                'must be free. You become its first owner.',
                style: TextStyle(
                  color: FmTheme.textSecondary(context),
                  fontSize: 12,
                  height: 1.35,
                ),
              ),
              const SizedBox(height: FmSpace.x4),
              OrgTextField(
                key: const ValueKey('org-name-field'),
                controller: _name,
                label: 'Name (lowercase, dashes)',
                autofocus: true,
              ),
              OrgTextField(
                controller: _displayName,
                label: 'Display name (optional)',
              ),
              OrgTextField(
                controller: _description,
                label: 'Description (optional)',
              ),
              if (_error.isNotEmpty) ...[
                const SizedBox(height: FmSpace.x1),
                Text(
                  _error,
                  style: TextStyle(color: FmTheme.danger(context), fontSize: 13),
                ),
              ],
              const SizedBox(height: FmSpace.x3),
              FilledButton(
                key: const ValueKey('org-create-submit'),
                onPressed: _busy ? null : _submit,
                child: _busy
                    ? const SizedBox(
                        width: 18,
                        height: 18,
                        child: CircularProgressIndicator(strokeWidth: 2),
                      )
                    : const Text('Create organization'),
              ),
            ],
          ),
        ),
      ),
    );
  }
}

/// A lightweight header used by the org screens: a back button, title, optional
/// subtitle, and a trailing action slot.
class OrgScreenHeader extends StatelessWidget {
  const OrgScreenHeader({
    super.key,
    required this.title,
    this.subtitle,
    this.trailing,
  });

  final String title;
  final String? subtitle;
  final Widget? trailing;

  @override
  Widget build(BuildContext context) {
    return Container(
      color: FmTheme.bgRaised(context),
      child: FmPanelHeader(
        title: title,
        subtitle: subtitle,
        leading: IconButton(
          tooltip: 'Back',
          onPressed: () => Navigator.of(context).maybePop(),
          icon: Icon(Icons.arrow_back, color: FmTheme.textPrimary(context)),
        ),
        trailing: trailing,
      ),
    );
  }
}

/// A pill for an org role (owner/admin/member) coloured by privilege.
class OrgRoleBadge extends StatelessWidget {
  const OrgRoleBadge({super.key, required this.role});

  final String role;

  @override
  Widget build(BuildContext context) {
    final color = switch (role) {
      'owner' => FmTheme.accent(context),
      'admin' => FmTheme.warning(context),
      _ => FmTheme.textTertiary(context),
    };
    return FmStatusBadge(label: role.isEmpty ? 'member' : role, color: color);
  }
}

/// The compact filled text field shared by the org create/edit forms.
class OrgTextField extends StatelessWidget {
  const OrgTextField({
    super.key,
    required this.controller,
    required this.label,
    this.autofocus = false,
  });

  final TextEditingController controller;
  final String label;
  final bool autofocus;

  @override
  Widget build(BuildContext context) {
    return Padding(
      padding: const EdgeInsets.only(bottom: FmSpace.x3),
      child: TextField(
        controller: controller,
        autofocus: autofocus,
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
          focusedBorder: OutlineInputBorder(
            borderRadius: BorderRadius.circular(FmRadius.md),
            borderSide: BorderSide(color: FmTheme.accent(context), width: 1.2),
          ),
        ),
      ),
    );
  }
}

/// Human-readable text for an error thrown by an [ApiService] org call.
String _messageFor(Object? error) {
  if (error is OrgApiException) return error.message;
  return 'Something went wrong. Check your connection and try again.';
}
