import 'package:flutter/material.dart';
import 'package:provider/provider.dart';

import '../services/identity.dart';
import '../services/relay_service.dart';
import '../services/settings_service.dart';
import '../theme.dart';
import '../widgets/connection_dot.dart';
import 'chat_screen.dart';
import 'notifications_screen.dart';
import 'repos_screen.dart';
import 'settings_screen.dart';

/// Top-level shell: a persistent top bar (relay status + avatar/connection dot)
/// and the Code / Chat / Notifications / Settings sections, matching the Qt nav.
class HomeShell extends StatefulWidget {
  const HomeShell({super.key});

  @override
  State<HomeShell> createState() => _HomeShellState();
}

class _HomeShellState extends State<HomeShell> {
  int _index = 0;

  static const _destinations = [
    (icon: Icons.code, label: 'Code'),
    (icon: Icons.chat_bubble_outline, label: 'Chat'),
    (icon: Icons.notifications_none, label: 'Notifications'),
    (icon: Icons.settings_outlined, label: 'Settings'),
  ];

  @override
  Widget build(BuildContext context) {
    final relay = context.watch<RelayService>();
    final settings = context.watch<SettingsService>();
    final identity = context.read<Identity>();
    final wide = MediaQuery.of(context).size.width >= 760;
    final name = settings.displayName.isEmpty ? identity.shortKey : settings.displayName;

    final pages = const [
      ReposScreen(),
      ChatScreen(),
      NotificationsScreen(),
      SettingsScreen(),
    ];

    final body = Column(
      children: [
        _TopBar(relay: relay, name: name),
        const Divider(height: 1),
        Expanded(child: IndexedStack(index: _index, children: pages)),
      ],
    );

    if (wide) {
      return Scaffold(
        body: Row(
          children: [
            NavigationRail(
              backgroundColor: FmColors.rail,
              selectedIndex: _index,
              onDestinationSelected: (i) => setState(() => _index = i),
              labelType: NavigationRailLabelType.all,
              destinations: [
                for (final d in _destinations)
                  NavigationRailDestination(
                    icon: _RailIcon(icon: d.icon, badge: d.label == 'Chat' ? relay.unread.length : 0),
                    label: Text(d.label),
                  ),
              ],
            ),
            const VerticalDivider(width: 1),
            Expanded(child: body),
          ],
        ),
      );
    }

    return Scaffold(
      body: body,
      bottomNavigationBar: NavigationBar(
        backgroundColor: FmColors.rail,
        selectedIndex: _index,
        onDestinationSelected: (i) => setState(() => _index = i),
        destinations: [
          for (final d in _destinations)
            NavigationDestination(
              icon: _RailIcon(icon: d.icon, badge: d.label == 'Chat' ? relay.unread.length : 0),
              label: d.label,
            ),
        ],
      ),
    );
  }
}

class _RailIcon extends StatelessWidget {
  const _RailIcon({required this.icon, this.badge = 0});
  final IconData icon;
  final int badge;

  @override
  Widget build(BuildContext context) {
    if (badge <= 0) return Icon(icon);
    return Badge(
      label: Text('$badge'),
      backgroundColor: FmColors.danger,
      child: Icon(icon),
    );
  }
}

class _TopBar extends StatelessWidget {
  const _TopBar({required this.relay, required this.name});
  final RelayService relay;
  final String name;

  String get _statusText => switch (relay.state) {
        RelayConnectionState.connected =>
          'Connected · ${relay.onlineCount} ${relay.onlineCount == 1 ? "node" : "nodes"} online',
        RelayConnectionState.connecting => 'Connecting…',
        RelayConnectionState.offline => 'Offline',
      };

  @override
  Widget build(BuildContext context) {
    return Container(
      color: FmColors.canvas,
      padding: const EdgeInsets.symmetric(horizontal: 16, vertical: 10),
      child: Row(
        children: [
          const Text('ForkMesh',
              style: TextStyle(fontSize: 18, fontWeight: FontWeight.w800)),
          const SizedBox(width: 16),
          Text(_statusText, style: const TextStyle(color: FmColors.textMuted, fontSize: 13)),
          const Spacer(),
          Tooltip(
            message: '$_statusText · your node profile',
            child: AvatarWithDot(state: relay.state, label: name),
          ),
        ],
      ),
    );
  }
}
