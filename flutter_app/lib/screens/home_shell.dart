import 'package:flutter/material.dart';
import 'package:forkmesh/fm_icons.dart';
import 'package:provider/provider.dart';

import '../services/auth_service.dart';
import '../services/identity.dart';
import '../services/relay_service.dart';
import '../services/settings_service.dart';
import '../theme.dart';
import '../widgets/connection_dot.dart';
import '../widgets/fm_ui.dart';
import 'activity_screen.dart';
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
    (icon: Icons.settings_outlined, label: 'Settings'),
    (icon: Icons.grid_view_rounded, label: 'Tools'),
  ];

  @override
  Widget build(BuildContext context) {
    final relay = context.watch<RelayService>();
    final auth = context.watch<AuthService>();
    final settings = context.watch<SettingsService>();
    final identity = context.read<Identity>();
    final wide = MediaQuery.of(context).size.width >= 760;
    final session = auth.session;
    final accountName = session?.nodeName.isNotEmpty == true
        ? session!.nodeName
        : session?.email ?? '';
    final name = accountName.isNotEmpty
        ? accountName
        : settings.displayName.isEmpty
        ? identity.shortKey
        : settings.displayName;

    final pages = const [
      ReposScreen(),
      ChatScreen(),
      ActivityScreen(),
      SettingsScreen(),
    ];

    final body = Column(
      children: [
        _TopBar(relay: relay, name: name),
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
              backgroundColor: FmTheme.bgRaised(context),
              useIndicator: false,
              selectedIconTheme: IconThemeData(
                color: FmTheme.textPrimary(context),
              ),
              unselectedIconTheme: IconThemeData(
                color: FmTheme.textTertiary(context),
              ),
              selectedLabelTextStyle: TextStyle(
                color: FmTheme.textPrimary(context),
                fontWeight: FontWeight.w700,
              ),
              unselectedLabelTextStyle: TextStyle(
                color: FmTheme.textTertiary(context),
              ),
              selectedIndex: _index,
              onDestinationSelected: (i) {
                if (i == _destinations.length - 1) {
                  _showToolsSheet(context);
                  return;
                }
                setState(() => _index = i);
              },
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
        onSelected: (i) {
          if (i == _destinations.length - 1) {
            _showToolsSheet(context);
            return;
          }
          setState(() => _index = i);
        },
      ),
    );
  }

  void _showToolsSheet(BuildContext context) {
    showModalBottomSheet<void>(
      context: context,
      useSafeArea: true,
      isScrollControlled: true,
      showDragHandle: false,
      backgroundColor: Colors.transparent,
      builder: (_) => const _ToolsSheet(),
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
        decoration: BoxDecoration(
          color: FmTheme.bgRaised(context),
          borderRadius: const BorderRadius.vertical(
            top: Radius.circular(FmRadius.lg),
          ),
          boxShadow: const [
            BoxShadow(
              color: Color(0x0F000000),
              blurRadius: 16,
              offset: Offset(0, -2),
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
    final iconColor = selected
        ? FmTheme.textPrimary(context)
        : FmTheme.textTertiary(context);
    final textColor = selected
        ? FmTheme.textPrimary(context)
        : FmTheme.textTertiary(context);

    return Semantics(
      button: true,
      selected: selected,
      label: label,
      child: InkWell(
        onTap: onTap,
        splashColor: FmTheme.accentSubtle(context),
        highlightColor: FmTheme.accentSubtle(context),
        child: SizedBox(
          height: 58,
          child: Center(
            child: AnimatedScale(
              key: selected ? ValueKey('bottom-nav-selected-$label') : null,
              duration: FmMotion.fast,
              curve: Curves.easeOut,
              scale: selected ? 1.04 : 1,
              child: Column(
                mainAxisSize: MainAxisSize.min,
                mainAxisAlignment: MainAxisAlignment.center,
                children: [
                  _RailIcon(
                    icon: icon,
                    badge: badge,
                    color: iconColor,
                    size: 23,
                  ),
                  const SizedBox(height: FmSpace.x1),
                  Text(
                    label,
                    maxLines: 1,
                    overflow: TextOverflow.ellipsis,
                    style: TextStyle(
                      color: textColor,
                      fontSize: 11,
                      height: 1,
                      fontWeight: selected ? FontWeight.w700 : FontWeight.w600,
                    ),
                  ),
                ],
              ),
            ),
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
      backgroundColor: FmTheme.danger(context),
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
        color: FmTheme.bgRaised(context),
        child: FmPanelHeader(
          title: 'ForkMesh',
          subtitle: _statusText,
          trailing: Row(
            mainAxisSize: MainAxisSize.min,
            children: [
              _TopNotificationButton(
                unreadCount: relay.unseenNotificationCount,
              ),
              const SizedBox(width: FmSpace.x2),
              Tooltip(
                message: '$_statusText - your node profile',
                child: AvatarWithDot(state: relay.state, label: name),
              ),
            ],
          ),
        ),
      ),
    );
  }
}

class _TopNotificationButton extends StatelessWidget {
  const _TopNotificationButton({required this.unreadCount});

  final int unreadCount;

  @override
  Widget build(BuildContext context) {
    final icon = Icon(
      FmIcons.notificationLine,
      color: FmTheme.textPrimary(context),
      size: 22,
    );

    return IconButton(
      key: const ValueKey('top-notifications-button'),
      tooltip: 'Notifications',
      visualDensity: VisualDensity.compact,
      onPressed: () {
        Navigator.of(context).push(
          MaterialPageRoute<void>(builder: (_) => const NotificationsScreen()),
        );
      },
      icon: unreadCount > 0
          ? Badge(
              label: Text('$unreadCount'),
              backgroundColor: FmTheme.danger(context),
              child: icon,
            )
          : icon,
    );
  }
}

class _ToolsSheet extends StatelessWidget {
  const _ToolsSheet();

  static const _tools = [
    (icon: Icons.folder_outlined, label: 'Explorer'),
    (icon: Icons.search, label: 'Search'),
    (icon: Icons.power_outlined, label: 'Ports'),
    (icon: Icons.bar_chart_rounded, label: 'Processes'),
    (icon: Icons.shield_outlined, label: 'API Client'),
    (icon: Icons.monitor_heart_outlined, label: 'Monitor'),
  ];

  @override
  Widget build(BuildContext context) {
    final availableHeight = MediaQuery.sizeOf(context).height;
    final bottomInset = MediaQuery.paddingOf(context).bottom;
    // Hug the content (the Column is min-sized and the list shrink-wraps),
    // capped at half the screen plus the bottom inset the list pads for.
    final maxSheetHeight = availableHeight * 0.5 + bottomInset;
    return SafeArea(
      top: false,
      bottom: false,
      child: Container(
        key: const ValueKey('tools-sheet-panel'),
        constraints: BoxConstraints(maxHeight: maxSheetHeight),
        decoration: BoxDecoration(
          color: FmTheme.bgRaised(context),
          borderRadius: const BorderRadius.vertical(
            top: Radius.circular(FmRadius.lg),
          ),
          border: Border(top: BorderSide(color: FmTheme.border(context))),
        ),
        child: Column(
          mainAxisSize: MainAxisSize.min,
          children: [
            Padding(
              padding: const EdgeInsets.fromLTRB(
                FmSpace.x4,
                FmSpace.x4,
                FmSpace.x3,
                FmSpace.x2,
              ),
              child: Row(
                children: [
                  Expanded(
                    child: Text(
                      'Tools',
                      style: TextStyle(
                        color: FmTheme.textPrimary(context),
                        fontSize: 20,
                        fontWeight: FontWeight.w700,
                        height: 1.1,
                      ),
                    ),
                  ),
                  IconButton(
                    tooltip: 'Close tools',
                    onPressed: () => Navigator.of(context).pop(),
                    style: IconButton.styleFrom(
                      backgroundColor: FmTheme.bgOverlay(context),
                      foregroundColor: FmTheme.textPrimary(context),
                      shape: const CircleBorder(),
                    ),
                    icon: const Icon(Icons.close),
                  ),
                ],
              ),
            ),
            Flexible(
              child: ListView.separated(
                shrinkWrap: true,
                padding: EdgeInsets.fromLTRB(
                  FmSpace.x4,
                  FmSpace.x1,
                  FmSpace.x4,
                  FmSpace.x5 + bottomInset,
                ),
                itemCount: _tools.length,
                separatorBuilder: (_, _) => const SizedBox(height: FmSpace.x1),
                itemBuilder: (context, index) {
                  final tool = _tools[index];
                  return _ToolMenuRow(icon: tool.icon, label: tool.label);
                },
              ),
            ),
          ],
        ),
      ),
    );
  }
}

class _ToolMenuRow extends StatelessWidget {
  const _ToolMenuRow({required this.icon, required this.label});

  final IconData icon;
  final String label;

  @override
  Widget build(BuildContext context) {
    return Material(
      color: Colors.transparent,
      child: InkWell(
        onTap: () {},
        borderRadius: BorderRadius.circular(FmRadius.md),
        child: Padding(
          padding: const EdgeInsets.symmetric(
            horizontal: FmSpace.x2,
            vertical: FmSpace.x3,
          ),
          child: Row(
            children: [
              Icon(icon, color: FmTheme.textTertiary(context), size: 28),
              const SizedBox(width: FmSpace.x3),
              Expanded(
                child: Text(
                  label,
                  maxLines: 1,
                  overflow: TextOverflow.ellipsis,
                  style: TextStyle(
                    color: FmTheme.textSecondary(context),
                    fontSize: 16,
                    fontWeight: FontWeight.w600,
                    height: 1.2,
                  ),
                ),
              ),
              Icon(
                Icons.chevron_right,
                color: FmTheme.textDisabled(context),
                size: 20,
              ),
            ],
          ),
        ),
      ),
    );
  }
}
