import 'package:flutter/material.dart';
import 'package:provider/provider.dart';

import '../services/auth_service.dart';
import '../services/identity.dart';
import '../services/relay_service.dart';
import '../services/settings_service.dart';
import '../theme.dart';
import '../widgets/fm_ui.dart';

/// Profile, relay connection, and import-token settings. Mirrors the Qt client's
/// Settings section (the subset the Flutter app currently uses).
class SettingsScreen extends StatefulWidget {
  const SettingsScreen({super.key});

  @override
  State<SettingsScreen> createState() => _SettingsScreenState();
}

class _SettingsScreenState extends State<SettingsScreen> {
  late final SettingsService _settings = context.read<SettingsService>();
  late final _name = TextEditingController(text: _settings.displayName);
  late final _solana = TextEditingController(text: _settings.solanaAddress);
  late final _server = TextEditingController(text: _settings.serverUrl);
  late final _room = TextEditingController(text: _settings.room);
  late final _passphrase = TextEditingController(text: _settings.passphrase);
  late final _github = TextEditingController(text: _settings.githubToken);
  late final _gitlab = TextEditingController(text: _settings.gitlabToken);

  @override
  void dispose() {
    for (final c in [
      _name,
      _solana,
      _server,
      _room,
      _passphrase,
      _github,
      _gitlab,
    ]) {
      c.dispose();
    }
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    final identity = context.read<Identity>();
    final relay = context.watch<RelayService>();
    final auth = context.watch<AuthService>();
    final session = auth.session;
    final accountName = session?.nodeName.isNotEmpty == true
        ? session!.nodeName
        : session?.email ?? '';
    if (accountName.isNotEmpty &&
        (_name.text.isEmpty || _name.text == 'preview-node')) {
      _name.text = accountName;
    }
    return ListView(
      padding: const EdgeInsets.all(FmSpace.x4),
      children: [
        const FmSectionHeader(title: 'Profile'),
        const SizedBox(height: FmSpace.x2),
        FmCard(
          radius: FmRadius.lg,
          child: Column(
            crossAxisAlignment: CrossAxisAlignment.stretch,
            children: [
              _ProfileAccountCard(session: session),
              const SizedBox(height: FmSpace.x4),
              _field('Display name', _name, onSaved: _settings.setDisplayName),
              _field(
                'Solana address (payouts/donations, optional)',
                _solana,
                onSaved: _settings.setSolanaAddress,
              ),
              const SizedBox(height: FmSpace.x2),
              Text(
                'Node id: ${identity.publicKeyB64url}',
                overflow: TextOverflow.ellipsis,
                style: TextStyle(
                  fontSize: 12,
                  color: FmTheme.textTertiary(context),
                ),
              ),
            ],
          ),
        ),
        const SizedBox(height: FmSpace.x5),

        const FmSectionHeader(title: 'Relay'),
        const SizedBox(height: FmSpace.x2),
        FmCard(
          radius: FmRadius.lg,
          child: Column(
            crossAxisAlignment: CrossAxisAlignment.stretch,
            children: [
              _field(
                'Relay WebSocket URL',
                _server,
                onSaved: _settings.setServerUrl,
              ),
              _field('Room', _room, onSaved: _settings.setRoom),
              _field(
                'Passphrase',
                _passphrase,
                onSaved: _settings.setPassphrase,
                obscure: true,
              ),
              const SizedBox(height: FmSpace.x2),
              Wrap(
                spacing: FmSpace.x3,
                runSpacing: FmSpace.x2,
                crossAxisAlignment: WrapCrossAlignment.center,
                children: [
                  FilledButton.icon(
                    onPressed: () => relay.connect(),
                    icon: const Icon(Icons.refresh, size: 18),
                    label: const Text('Reconnect'),
                  ),
                  OutlinedButton.icon(
                    onPressed: relay.state == RelayConnectionState.offline
                        ? null
                        : () => relay.disconnect(),
                    icon: const Icon(Icons.logout, size: 18),
                    label: const Text('Disconnect'),
                  ),
                  _RelayStateBadge(state: relay.state),
                ],
              ),
            ],
          ),
        ),
        const SizedBox(height: FmSpace.x5),

        const FmSectionHeader(title: 'Import tokens'),
        const SizedBox(height: FmSpace.x2),
        FmCard(
          radius: FmRadius.lg,
          child: Column(
            crossAxisAlignment: CrossAxisAlignment.stretch,
            children: [
              Text(
                'Personal access tokens used to authenticate clones when importing a '
                'repo from GitHub/GitLab (raises rate limits). Stored locally only.',
                style: TextStyle(
                  color: FmTheme.textSecondary(context),
                  fontSize: 12,
                  height: 1.35,
                ),
              ),
              const SizedBox(height: FmSpace.x3),
              _field(
                'GitHub token',
                _github,
                onSaved: _settings.setGithubToken,
                obscure: true,
              ),
              _field(
                'GitLab token',
                _gitlab,
                onSaved: _settings.setGitlabToken,
                obscure: true,
              ),
            ],
          ),
        ),
        const SizedBox(height: FmSpace.x5),
        const FmSectionHeader(title: 'Account'),
        const SizedBox(height: FmSpace.x2),
        FmCard(
          radius: FmRadius.lg,
          child: _SignOutCard(
            signedInAs: session?.nodeName.isNotEmpty == true
                ? session!.nodeName
                : session?.email ?? 'Current account',
            onSignOut: () => _confirmSignOut(context, auth),
          ),
        ),
      ],
    );
  }

  Widget _field(
    String label,
    TextEditingController c, {
    required Future<void> Function(String) onSaved,
    bool obscure = false,
  }) {
    return Padding(
      padding: const EdgeInsets.only(bottom: FmSpace.x3),
      child: TextField(
        controller: c,
        obscureText: obscure,
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
        onChanged: (v) => onSaved(v.trim()),
      ),
    );
  }

  Future<void> _confirmSignOut(BuildContext context, AuthService auth) async {
    final ok = await showDialog<bool>(
      context: context,
      builder: (context) => AlertDialog(
        backgroundColor: FmTheme.bgOverlay(context),
        surfaceTintColor: Colors.transparent,
        shape: RoundedRectangleBorder(
          borderRadius: BorderRadius.circular(FmRadius.lg),
        ),
        title: const Text('Sign out?'),
        content: const Text(
          'This clears the saved mobile session on this device. Your identity key, relay settings, and tokens stay local.',
        ),
        actions: [
          TextButton(
            onPressed: () => Navigator.of(context).pop(false),
            child: const Text('Cancel'),
          ),
          FilledButton(
            onPressed: () => Navigator.of(context).pop(true),
            child: const Text('Sign out'),
          ),
        ],
      ),
    );
    if (ok != true) return;
    await auth.logout();
    if (context.mounted) {
      ScaffoldMessenger.of(context).showSnackBar(
        const SnackBar(
          content: Text('Signed out. Sign in with another account.'),
        ),
      );
    }
  }
}

class _ProfileAccountCard extends StatelessWidget {
  const _ProfileAccountCard({required this.session});

  final AuthSession? session;

  @override
  Widget build(BuildContext context) {
    final title = session?.sessionKind == 'preview'
        ? 'Preview mode'
        : session?.nodeName.isNotEmpty == true
        ? session!.nodeName
        : session?.email.isNotEmpty == true
        ? session!.email
        : 'Not signed in';
    final subtitle = session?.sessionKind == 'preview'
        ? 'This is local preview data. Sign out and log in to show your Worker account.'
        : session?.email.isNotEmpty == true
        ? session!.email
        : 'No Worker account session found on this device';
    return Row(
      children: [
        Container(
          width: 42,
          height: 42,
          decoration: BoxDecoration(
            color: FmTheme.textPrimary(context),
            borderRadius: BorderRadius.circular(FmRadius.full),
          ),
          child: Icon(
            Icons.person_outline,
            color: FmTheme.isDark(context)
                ? FmColors.darkBgBase
                : FmColors.bgRaised,
          ),
        ),
        const SizedBox(width: FmSpace.x3),
        Expanded(
          child: Column(
            crossAxisAlignment: CrossAxisAlignment.start,
            children: [
              Text(
                title,
                maxLines: 1,
                overflow: TextOverflow.ellipsis,
                style: TextStyle(
                  color: FmTheme.textPrimary(context),
                  fontWeight: FontWeight.w800,
                ),
              ),
              const SizedBox(height: FmSpace.x1),
              Text(
                subtitle,
                maxLines: 1,
                overflow: TextOverflow.ellipsis,
                style: TextStyle(
                  color: FmTheme.textSecondary(context),
                  fontSize: 12,
                ),
              ),
            ],
          ),
        ),
        const SizedBox(width: FmSpace.x2),
        const _SessionBadge(),
      ],
    );
  }
}

class _SessionBadge extends StatelessWidget {
  const _SessionBadge();

  @override
  Widget build(BuildContext context) =>
      FmStatusBadge(label: 'Active', color: FmTheme.success(context));
}

class _RelayStateBadge extends StatelessWidget {
  const _RelayStateBadge({required this.state});

  final RelayConnectionState state;

  @override
  Widget build(BuildContext context) {
    final label = switch (state) {
      RelayConnectionState.connected => 'Connected',
      RelayConnectionState.connecting => 'Connecting...',
      RelayConnectionState.offline => 'Offline',
    };
    final color = switch (state) {
      RelayConnectionState.connected => FmTheme.success(context),
      RelayConnectionState.connecting => FmTheme.warning(context),
      RelayConnectionState.offline => FmColors.offline,
    };
    return FmStatusBadge(label: label, color: color);
  }
}

class _SignOutCard extends StatelessWidget {
  const _SignOutCard({required this.signedInAs, required this.onSignOut});

  final String signedInAs;
  final VoidCallback onSignOut;

  @override
  Widget build(BuildContext context) => Column(
    crossAxisAlignment: CrossAxisAlignment.start,
    children: [
      Text(
        'Signed in as $signedInAs',
        maxLines: 1,
        overflow: TextOverflow.ellipsis,
        style: TextStyle(
          color: FmTheme.textPrimary(context),
          fontWeight: FontWeight.w800,
        ),
      ),
      const SizedBox(height: FmSpace.x2),
      Text(
        'Sign out to return to the login screen and test with another Cloudflare Worker account.',
        style: TextStyle(
          color: FmTheme.textSecondary(context),
          fontSize: 12,
          height: 1.35,
        ),
      ),
      const SizedBox(height: FmSpace.x3),
      TextButton.icon(
        onPressed: onSignOut,
        icon: const Icon(Icons.logout, size: 18),
        label: const Text('Sign out'),
        style: TextButton.styleFrom(
          foregroundColor: FmTheme.danger(context),
          backgroundColor: FmTheme.danger(context).withAlpha(22),
          minimumSize: const Size(0, 44),
          padding: const EdgeInsets.symmetric(
            horizontal: FmSpace.x4,
            vertical: FmSpace.x3,
          ),
          shape: RoundedRectangleBorder(
            borderRadius: BorderRadius.circular(FmRadius.md),
          ),
        ),
      ),
    ],
  );
}
