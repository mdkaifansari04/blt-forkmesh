import 'package:flutter/material.dart';
import 'package:provider/provider.dart';

import '../services/auth_service.dart';
import '../services/identity.dart';
import '../services/relay_service.dart';
import '../services/settings_service.dart';
import '../theme.dart';

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
    return ListView(
      padding: const EdgeInsets.all(16),
      children: [
        const _SectionLabel('PROFILE'),
        _ProfileAccountCard(session: session),
        const SizedBox(height: 12),
        _field('Display name', _name, onSaved: _settings.setDisplayName),
        _field(
          'Solana address (payouts/donations, optional)',
          _solana,
          onSaved: _settings.setSolanaAddress,
        ),
        const SizedBox(height: 6),
        Text(
          'Node id: ${identity.publicKeyB64url}',
          overflow: TextOverflow.ellipsis,
          style: const TextStyle(fontSize: 12, color: FmColors.textMuted),
        ),
        const SizedBox(height: 20),

        const _SectionLabel('RELAY'),
        _field('Relay WebSocket URL', _server, onSaved: _settings.setServerUrl),
        _field('Room', _room, onSaved: _settings.setRoom),
        _field(
          'Passphrase',
          _passphrase,
          onSaved: _settings.setPassphrase,
          obscure: true,
        ),
        const SizedBox(height: 10),
        Wrap(
          spacing: 10,
          runSpacing: 8,
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
            Text(switch (relay.state) {
              RelayConnectionState.connected => 'Connected',
              RelayConnectionState.connecting => 'Connecting...',
              RelayConnectionState.offline => 'Offline',
            }, style: const TextStyle(color: FmColors.textMuted)),
          ],
        ),
        const SizedBox(height: 20),

        const _SectionLabel('IMPORT TOKENS'),
        const Text(
          'Personal access tokens used to authenticate clones when importing a '
          'repo from GitHub/GitLab (raises rate limits). Stored locally only.',
          style: TextStyle(color: FmColors.textMuted, fontSize: 12),
        ),
        const SizedBox(height: 8),
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
        const SizedBox(height: 20),
        const _SectionLabel('ACCOUNT'),
        _SignOutCard(
          signedInAs: session?.nodeName.isNotEmpty == true
              ? session!.nodeName
              : session?.email ?? 'Current account',
          onSignOut: () => _confirmSignOut(context, auth),
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
      padding: const EdgeInsets.symmetric(vertical: 6),
      child: TextField(
        controller: c,
        obscureText: obscure,
        decoration: InputDecoration(labelText: label),
        onChanged: (v) => onSaved(v.trim()),
      ),
    );
  }

  Future<void> _confirmSignOut(BuildContext context, AuthService auth) async {
    final ok = await showDialog<bool>(
      context: context,
      builder: (context) => AlertDialog(
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
    final title = session?.nodeName.isNotEmpty == true
        ? session!.nodeName
        : session?.email.isNotEmpty == true
        ? session!.email
        : 'Signed in';
    final subtitle = session?.email.isNotEmpty == true
        ? session!.email
        : 'Worker account session saved on this device';
    return Container(
      padding: const EdgeInsets.all(14),
      decoration: BoxDecoration(
        color: FmColors.surface,
        border: Border.all(color: FmColors.border),
        borderRadius: BorderRadius.circular(18),
      ),
      child: Row(
        children: [
          Container(
            width: 42,
            height: 42,
            decoration: const BoxDecoration(
              color: FmColors.text,
              shape: BoxShape.circle,
            ),
            child: const Icon(Icons.person_outline, color: Colors.white),
          ),
          const SizedBox(width: 12),
          Expanded(
            child: Column(
              crossAxisAlignment: CrossAxisAlignment.start,
              children: [
                Text(
                  title,
                  maxLines: 1,
                  overflow: TextOverflow.ellipsis,
                  style: const TextStyle(fontWeight: FontWeight.w800),
                ),
                const SizedBox(height: 3),
                Text(
                  subtitle,
                  maxLines: 1,
                  overflow: TextOverflow.ellipsis,
                  style: const TextStyle(
                    color: FmColors.textMuted,
                    fontSize: 12,
                  ),
                ),
              ],
            ),
          ),
          const _SessionBadge(),
        ],
      ),
    );
  }
}

class _SessionBadge extends StatelessWidget {
  const _SessionBadge();

  @override
  Widget build(BuildContext context) => Container(
    padding: const EdgeInsets.symmetric(horizontal: 9, vertical: 5),
    decoration: BoxDecoration(
      color: const Color(0xFFEFF8F2),
      borderRadius: BorderRadius.circular(999),
      border: Border.all(color: const Color(0xFFCFEBD8)),
    ),
    child: const Text(
      'Active',
      style: TextStyle(
        color: FmColors.success,
        fontSize: 11,
        fontWeight: FontWeight.w800,
      ),
    ),
  );
}

class _SignOutCard extends StatelessWidget {
  const _SignOutCard({required this.signedInAs, required this.onSignOut});

  final String signedInAs;
  final VoidCallback onSignOut;

  @override
  Widget build(BuildContext context) => Container(
    padding: const EdgeInsets.all(14),
    decoration: BoxDecoration(
      color: FmColors.surface,
      border: Border.all(color: FmColors.border),
      borderRadius: BorderRadius.circular(18),
    ),
    child: Column(
      crossAxisAlignment: CrossAxisAlignment.start,
      children: [
        Text(
          'Signed in as $signedInAs',
          maxLines: 1,
          overflow: TextOverflow.ellipsis,
          style: const TextStyle(fontWeight: FontWeight.w800),
        ),
        const SizedBox(height: 6),
        const Text(
          'Sign out to return to the login screen and test with another Cloudflare Worker account.',
          style: TextStyle(
            color: FmColors.textMuted,
            fontSize: 12,
            height: 1.35,
          ),
        ),
        const SizedBox(height: 12),
        OutlinedButton.icon(
          onPressed: onSignOut,
          icon: const Icon(Icons.logout, size: 18),
          label: const Text('Sign out'),
          style: OutlinedButton.styleFrom(
            foregroundColor: FmColors.danger,
            side: const BorderSide(color: Color(0xFFF1C9C7)),
          ),
        ),
      ],
    ),
  );
}

class _SectionLabel extends StatelessWidget {
  const _SectionLabel(this.text);
  final String text;
  @override
  Widget build(BuildContext context) => Padding(
    padding: const EdgeInsets.only(bottom: 6, top: 4),
    child: Text(
      text,
      style: const TextStyle(
        fontSize: 12,
        fontWeight: FontWeight.w700,
        color: FmColors.textMuted,
        letterSpacing: 0.5,
      ),
    ),
  );
}
