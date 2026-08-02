import 'package:flutter/material.dart';
import 'package:provider/provider.dart';

import '../services/auth_service.dart';
import '../services/identity.dart';
import '../services/relay_service.dart';
import '../services/settings_service.dart';
import '../theme.dart';
import '../widgets/fm_ui.dart';
import 'orgs_screen.dart';

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

        const FmSectionHeader(title: 'Funding and payouts'),
        const SizedBox(height: FmSpace.x2),
        FmCard(
          radius: FmRadius.lg,
          child: Column(
            crossAxisAlignment: CrossAxisAlignment.stretch,
            children: [
              _FundingPayoutStatus(session: session),
              const SizedBox(height: FmSpace.x3),
              _field(
                'Public Solana address (payouts/donations, optional)',
                _solana,
                onSaved: _settings.setSolanaAddress,
              ),
              Text(
                'Non-custodial: ForkMesh saves only this public payout address. Wallet keys and recovery phrases remain on your device. User-owned funds, the public community pool, pending allocations, and finalized transfers are separate states. Legacy Worker bounty wallets are frozen for offline migration; never fund one or paste a private key or seed phrase.',
                style: TextStyle(
                  color: FmTheme.textSecondary(context),
                  fontSize: 12,
                  height: 1.35,
                ),
              ),
            ],
          ),
        ),
        const SizedBox(height: FmSpace.x5),

        const FmSectionHeader(title: 'Organizations'),
        const SizedBox(height: FmSpace.x2),
        FmCard(
          radius: FmRadius.lg,
          padding: EdgeInsets.zero,
          child: _OrganizationsRow(
            onOpen: () => Navigator.of(
              context,
            ).push(MaterialPageRoute<void>(builder: (_) => const OrgsScreen())),
          ),
        ),
        const SizedBox(height: FmSpace.x5),

        const FmSectionHeader(title: 'Desktop nodes'),
        const SizedBox(height: FmSpace.x2),
        FmCard(
          radius: FmRadius.lg,
          child: _DesktopNodesCard(session: session),
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

class _OrganizationsRow extends StatelessWidget {
  const _OrganizationsRow({required this.onOpen});

  final VoidCallback onOpen;

  @override
  Widget build(BuildContext context) {
    return InkWell(
      key: const ValueKey('settings-organizations-row'),
      onTap: onOpen,
      borderRadius: BorderRadius.circular(FmRadius.lg),
      child: Padding(
        padding: const EdgeInsets.all(FmSpace.x4),
        child: Row(
          children: [
            Icon(
              Icons.apartment_outlined,
              color: FmTheme.textSecondary(context),
            ),
            const SizedBox(width: FmSpace.x3),
            Expanded(
              child: Column(
                crossAxisAlignment: CrossAxisAlignment.start,
                children: [
                  Text(
                    'Manage organizations',
                    style: TextStyle(
                      color: FmTheme.textPrimary(context),
                      fontWeight: FontWeight.w800,
                    ),
                  ),
                  const SizedBox(height: FmSpace.x1),
                  Text(
                    'Create orgs, invite members, set up teams, and link repos.',
                    style: TextStyle(
                      color: FmTheme.textSecondary(context),
                      fontSize: 12,
                      height: 1.35,
                    ),
                  ),
                ],
              ),
            ),
            Icon(Icons.chevron_right, color: FmTheme.textDisabled(context)),
          ],
        ),
      ),
    );
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

class _FundingPayoutStatus extends StatelessWidget {
  const _FundingPayoutStatus({required this.session});

  final AuthSession? session;

  @override
  Widget build(BuildContext context) {
    final connected = session?.hasPayoutAddress == true;
    final address = session?.solana.trim() ?? '';
    return Row(
      crossAxisAlignment: CrossAxisAlignment.start,
      children: [
        Icon(
          connected ? Icons.verified_outlined : Icons.info_outline,
          color: connected
              ? FmTheme.success(context)
              : FmTheme.textTertiary(context),
          size: 22,
        ),
        const SizedBox(width: FmSpace.x3),
        Expanded(
          child: Column(
            crossAxisAlignment: CrossAxisAlignment.start,
            children: [
              Text(
                connected
                    ? 'Payout address connected'
                    : 'No payout address connected',
                style: TextStyle(
                  color: FmTheme.textPrimary(context),
                  fontWeight: FontWeight.w800,
                ),
              ),
              const SizedBox(height: FmSpace.x1),
              Text(
                address.isNotEmpty
                    ? address
                    : 'Add a public Solana address only if you want optional mirror-reward or direct-donation metadata. Legacy bounty funding is frozen.',
                overflow: TextOverflow.ellipsis,
                style: TextStyle(
                  color: FmTheme.textSecondary(context),
                  fontSize: 12,
                ),
              ),
            ],
          ),
        ),
      ],
    );
  }
}

class _DesktopNodesCard extends StatelessWidget {
  const _DesktopNodesCard({required this.session});

  final AuthSession? session;

  @override
  Widget build(BuildContext context) {
    final devices = session?.devices ?? const <AccountDevice>[];
    final desktopDevices = session?.desktopDevices ?? const <AccountDevice>[];
    if (session == null || session?.sessionKind == 'preview') {
      return _DesktopNodeEmptyState(
        title: 'Sign in to inspect desktop nodes',
        body:
            'Desktop pairing uses your Worker account device inventory. Log in to see which Qt desktop nodes can later receive signed mobile commands.',
      );
    }
    if (devices.isEmpty) {
      return _DesktopNodeEmptyState(
        title: 'No desktop node inventory yet',
        body:
            'Log in from the Qt desktop app with this account to register a desktop node. Mobile controls stay locked until a desktop node is available.',
      );
    }
    return Column(
      crossAxisAlignment: CrossAxisAlignment.start,
      children: [
        Text(
          desktopDevices.isEmpty
              ? 'No owner-capable desktop node registered'
              : '${desktopDevices.length} desktop node${desktopDevices.length == 1 ? '' : 's'} ready',
          style: TextStyle(
            color: FmTheme.textPrimary(context),
            fontWeight: FontWeight.w800,
          ),
        ),
        const SizedBox(height: FmSpace.x2),
        Text(
          'Remote commands stay locked until a desktop approval flow is added. This inventory only shows which nodes could later run trusted work.',
          style: TextStyle(
            color: FmTheme.textSecondary(context),
            fontSize: 12,
            height: 1.35,
          ),
        ),
        const SizedBox(height: FmSpace.x3),
        for (final device in devices) ...[
          _DesktopDeviceRow(device: device),
          if (device != devices.last) const SizedBox(height: FmSpace.x2),
        ],
      ],
    );
  }
}

class _DesktopNodeEmptyState extends StatelessWidget {
  const _DesktopNodeEmptyState({required this.title, required this.body});

  final String title;
  final String body;

  @override
  Widget build(BuildContext context) => Column(
    crossAxisAlignment: CrossAxisAlignment.start,
    children: [
      Text(
        title,
        style: TextStyle(
          color: FmTheme.textPrimary(context),
          fontWeight: FontWeight.w800,
        ),
      ),
      const SizedBox(height: FmSpace.x2),
      Text(
        body,
        style: TextStyle(
          color: FmTheme.textSecondary(context),
          fontSize: 12,
          height: 1.35,
        ),
      ),
    ],
  );
}

class _DesktopDeviceRow extends StatelessWidget {
  const _DesktopDeviceRow({required this.device});

  final AccountDevice device;

  @override
  Widget build(BuildContext context) {
    final isDesktop = device.kind == 'desktop_node';
    final color = !device.enabled
        ? FmColors.offline
        : device.canOwnerSign
        ? FmTheme.success(context)
        : FmTheme.warning(context);
    return Container(
      padding: const EdgeInsets.all(FmSpace.x3),
      decoration: BoxDecoration(
        color: FmTheme.bgBase(context),
        borderRadius: BorderRadius.circular(FmRadius.md),
      ),
      child: Row(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          Container(
            width: 36,
            height: 36,
            decoration: BoxDecoration(
              color: color.withAlpha(28),
              borderRadius: BorderRadius.circular(FmRadius.md),
            ),
            child: Icon(
              isDesktop ? Icons.desktop_mac_outlined : Icons.phone_iphone,
              color: color,
              size: 20,
            ),
          ),
          const SizedBox(width: FmSpace.x3),
          Expanded(
            child: Column(
              crossAxisAlignment: CrossAxisAlignment.start,
              children: [
                Text(
                  device.displayName,
                  maxLines: 1,
                  overflow: TextOverflow.ellipsis,
                  style: TextStyle(
                    color: FmTheme.textPrimary(context),
                    fontWeight: FontWeight.w800,
                  ),
                ),
                const SizedBox(height: FmSpace.x1),
                Align(
                  alignment: Alignment.centerLeft,
                  child: FmStatusBadge(label: device.statusLabel, color: color),
                ),
                const SizedBox(height: FmSpace.x1),
                Text(
                  '${device.kindLabel} · ${device.capabilityLabel}',
                  maxLines: 2,
                  overflow: TextOverflow.ellipsis,
                  style: TextStyle(
                    color: FmTheme.textSecondary(context),
                    fontSize: 12,
                  ),
                ),
                if (device.pubkey.isNotEmpty) ...[
                  const SizedBox(height: FmSpace.x1),
                  Text(
                    'Key: ${_shortKey(device.pubkey)}',
                    style: TextStyle(
                      color: FmTheme.textTertiary(context),
                      fontSize: 11,
                    ),
                  ),
                ],
              ],
            ),
          ),
        ],
      ),
    );
  }

  static String _shortKey(String key) {
    if (key.length <= 18) return key;
    return '${key.substring(0, 9)}...${key.substring(key.length - 6)}';
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
