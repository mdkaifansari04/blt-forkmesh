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

/// Top-level shell with a persistent status bar and primary app navigation.
class HomeShell extends StatefulWidget {
  const HomeShell({super.key});

  @override
  State<HomeShell> createState() => _HomeShellState();
}

class _HomeShellState extends State<HomeShell> {
  int _index = 0;

  static const _destinations = [
    (icon: Icons.code, label: 'Code'),
    (icon: Icons.chat_bubble_outline_rounded, label: 'Chat'),
    (icon: Icons.bolt_outlined, label: 'Activity'),
    (icon: Icons.person_outline, label: 'Profile'),
  ];

  @override
  Widget build(BuildContext context) {
    final relay = context.watch<RelayService>();
    final settings = context.watch<SettingsService>();
    final identity = context.read<Identity>();
    final wide = MediaQuery.of(context).size.width >= 760;
    final name = settings.displayName.isEmpty
        ? identity.shortKey
        : settings.displayName;

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
        Expanded(
          child: IndexedStack(index: _index, children: pages),
        ),
      ],
    );

    if (wide) {
      return Scaffold(
        body: Row(
          children: [
            NavigationRail(
              backgroundColor: FmColors.rail,
              indicatorColor: FmColors.text,
              selectedIconTheme: const IconThemeData(color: Colors.white),
              unselectedIconTheme: const IconThemeData(
                color: FmColors.textMuted,
              ),
              selectedLabelTextStyle: const TextStyle(
                color: FmColors.text,
                fontWeight: FontWeight.w800,
              ),
              unselectedLabelTextStyle: const TextStyle(
                color: FmColors.textMuted,
              ),
              selectedIndex: _index,
              onDestinationSelected: (i) => setState(() => _index = i),
              labelType: NavigationRailLabelType.all,
              destinations: [
                for (final d in _destinations)
                  NavigationRailDestination(
                    icon: _RailIcon(
                      icon: d.icon,
                      badge: d.label == 'Chat' ? relay.unread.length : 0,
                    ),
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
      bottomNavigationBar: _CompactBottomMenu(
        destinations: _destinations,
        selectedIndex: _index,
        chatBadge: relay.unread.length,
        onSelected: (i) => setState(() => _index = i),
      ),
    );
  }
}

class _CompactBottomMenu extends StatelessWidget {
  const _CompactBottomMenu({
    required this.destinations,
    required this.selectedIndex,
    required this.chatBadge,
    required this.onSelected,
  });

  final List<({IconData icon, String label})> destinations;
  final int selectedIndex;
  final int chatBadge;
  final ValueChanged<int> onSelected;

  @override
  Widget build(BuildContext context) {
    final bottomPadding = MediaQuery.paddingOf(context).bottom;
    final contentBottomInset = (bottomPadding - 20).clamp(0.0, bottomPadding);

    return Material(
      color: Colors.transparent,
      child: Container(
        key: const ValueKey('compact-bottom-menu'),
        decoration: const BoxDecoration(
          color: Colors.white,
          borderRadius: BorderRadius.vertical(top: Radius.circular(28)),
          border: Border(top: BorderSide(color: Color(0xFFE8E6E2))),
          boxShadow: [
            BoxShadow(
              color: Color(0x14000000),
              blurRadius: 18,
              offset: Offset(0, -4),
            ),
          ],
        ),
        child: Padding(
          padding: EdgeInsets.only(bottom: contentBottomInset),
          child: SizedBox(
            key: const ValueKey('compact-bottom-menu-content'),
            height: 58,
            child: Row(
              children: [
                for (var i = 0; i < destinations.length; i++)
                  Expanded(
                    child: _CompactMenuItem(
                      icon: destinations[i].icon,
                      label: destinations[i].label,
                      selected: selectedIndex == i,
                      badge: destinations[i].label == 'Chat' ? chatBadge : 0,
                      onTap: () => onSelected(i),
                    ),
                  ),
              ],
            ),
          ),
        ),
      ),
    );
  }
}

class _CompactMenuItem extends StatelessWidget {
  const _CompactMenuItem({
    required this.icon,
    required this.label,
    required this.selected,
    required this.badge,
    required this.onTap,
  });

  final IconData icon;
  final String label;
  final bool selected;
  final int badge;
  final VoidCallback onTap;

  @override
  Widget build(BuildContext context) {
    final color = selected ? FmColors.text : const Color(0xFFAAA8A3);
    return Semantics(
      button: true,
      selected: selected,
      label: label,
      child: InkWell(
        onTap: onTap,
        borderRadius: BorderRadius.circular(24),
        splashColor: Colors.black12,
        highlightColor: Colors.black12,
        child: SizedBox(
          height: 58,
          child: Column(
            mainAxisAlignment: MainAxisAlignment.center,
            children: [
              _RailIcon(icon: icon, badge: badge, color: color, size: 24),
              const SizedBox(height: 3),
              Text(
                label,
                maxLines: 1,
                overflow: TextOverflow.ellipsis,
                style: TextStyle(
                  color: color,
                  fontSize: 11,
                  height: 1,
                  fontWeight: selected ? FontWeight.w700 : FontWeight.w600,
                ),
              ),
            ],
          ),
        ),
      ),
    );
  }
}

class _RailIcon extends StatelessWidget {
  const _RailIcon({required this.icon, this.badge = 0, this.color, this.size});
  final IconData icon;
  final int badge;
  final Color? color;
  final double? size;

  @override
  Widget build(BuildContext context) {
    final iconWidget = Icon(icon, color: color, size: size);
    if (badge <= 0) return iconWidget;
    return Badge(
      label: Text('$badge'),
      backgroundColor: FmColors.danger,
      child: iconWidget,
    );
  }
}

class _TopBar extends StatelessWidget {
  const _TopBar({required this.relay, required this.name});
  final RelayService relay;
  final String name;

  String get _statusText => switch (relay.state) {
    RelayConnectionState.connected =>
      'Connected - ${relay.onlineCount} ${relay.onlineCount == 1 ? "node" : "nodes"} online',
    RelayConnectionState.connecting => 'Connecting...',
    RelayConnectionState.offline => 'Offline',
  };

  @override
  Widget build(BuildContext context) {
    return SafeArea(
      bottom: false,
      child: Container(
        key: const ValueKey('home-top-bar'),
        color: FmColors.surface,
        padding: const EdgeInsets.symmetric(horizontal: 16, vertical: 10),
        child: Row(
          children: [
            const Text(
              'ForkMesh',
              style: TextStyle(fontSize: 18, fontWeight: FontWeight.w800),
            ),
            const SizedBox(width: 16),
            Flexible(
              child: Text(
                _statusText,
                overflow: TextOverflow.ellipsis,
                style: const TextStyle(color: FmColors.textMuted, fontSize: 13),
              ),
            ),
            const SizedBox(width: 12),
            Tooltip(
              message: '$_statusText - your node profile',
              child: AvatarWithDot(state: relay.state, label: name),
            ),
          ],
        ),
      ),
    );
  }
}
