import 'package:flutter/material.dart';
import 'package:provider/provider.dart';

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
    return ListView(
      padding: const EdgeInsets.all(16),
      children: [
        const _SectionLabel('PROFILE'),
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
