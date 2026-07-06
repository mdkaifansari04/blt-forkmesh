import 'package:flutter/material.dart';
import 'package:provider/provider.dart';

import '../fm_icons.dart';
import '../models/models.dart';
import '../services/notification_deep_link.dart';
import '../services/notification_service.dart';
import '../theme.dart';
import '../widgets/fm_ui.dart';
import 'repo_detail_screen.dart';

class NotificationsScreen extends StatefulWidget {
  const NotificationsScreen({super.key});

  @override
  State<NotificationsScreen> createState() => _NotificationsScreenState();
}

class _NotificationsScreenState extends State<NotificationsScreen> {
  static const _filters = ['All', 'Unread', 'Mentions', 'Repo', 'System'];
  String _selectedFilter = 'All';
  bool _started = false;

  @override
  void didChangeDependencies() {
    super.didChangeDependencies();
    if (_started) return;
    _started = true;
    WidgetsBinding.instance.addPostFrameCallback((_) {
      if (mounted) context.read<NotificationService>().refresh();
    });
  }

  @override
  Widget build(BuildContext context) {
    final notifications = context.watch<NotificationService>();
    final visible = _filterNotifications(notifications.notifications).toList();

    return Scaffold(
      backgroundColor: FmTheme.bgBase(context),
      body: SafeArea(
        child: Column(
          children: [
            _NotificationsHeader(
              unreadCount: notifications.unreadCount,
              onRefresh: notifications.refresh,
              onMarkAllRead: notifications.unreadCount > 0
                  ? notifications.markAllRead
                  : null,
            ),
            SizedBox(
              height: 56,
              child: ListView.separated(
                padding: const EdgeInsets.symmetric(horizontal: FmSpace.x4),
                scrollDirection: Axis.horizontal,
                itemBuilder: (context, index) {
                  final filter = _filters[index];
                  return _NotificationFilterChip(
                    label: filter,
                    selected: filter == _selectedFilter,
                    onSelected: () => setState(() {
                      _selectedFilter = filter;
                    }),
                  );
                },
                separatorBuilder: (_, _) => const SizedBox(width: FmSpace.x2),
                itemCount: _filters.length,
              ),
            ),
            Expanded(child: _buildBody(context, notifications, visible)),
          ],
        ),
      ),
    );
  }

  Widget _buildBody(
    BuildContext context,
    NotificationService notifications,
    List<ForkNotification> visible,
  ) {
    if (notifications.loading && notifications.notifications.isEmpty) {
      return const Center(child: CircularProgressIndicator());
    }
    if (notifications.error.isNotEmpty && notifications.notifications.isEmpty) {
      return _NotificationsEmptyState(
        icon: Icons.cloud_off_outlined,
        title: 'Could not load notifications',
        message: notifications.error,
        action: TextButton.icon(
          onPressed: notifications.refresh,
          icon: const Icon(Icons.refresh),
          label: const Text('Retry'),
        ),
      );
    }
    if (visible.isEmpty) {
      return _NotificationsEmptyState(
        icon: _selectedFilter == 'Unread'
            ? Icons.mark_email_read_outlined
            : FmIcons.notificationLine,
        title: _selectedFilter == 'Unread'
            ? 'All caught up'
            : 'No ${_selectedFilter.toLowerCase()} notifications',
        message: _selectedFilter == 'Unread'
            ? 'Unread issue, PR, discussion, host, release, and mention updates will appear here.'
            : 'Mainnode-backed ForkMesh updates will appear here when something needs your attention.',
      );
    }

    return RefreshIndicator(
      onRefresh: notifications.refresh,
      child: ListView(
        padding: const EdgeInsets.fromLTRB(
          FmSpace.x4,
          FmSpace.x2,
          FmSpace.x4,
          FmSpace.x5,
        ),
        children: [
          _NotificationSectionLabel(
            label:
                '${visible.length} ${visible.length == 1 ? "update" : "updates"}',
          ),
          const SizedBox(height: FmSpace.x2),
          for (final notification in visible)
            _NotificationRow(
              notification: notification,
              onTap: () =>
                  _openNotification(context, notifications, notification),
            ),
        ],
      ),
    );
  }

  Iterable<ForkNotification> _filterNotifications(
    List<ForkNotification> notifications,
  ) {
    return notifications.where((notification) {
      return switch (_selectedFilter) {
        'Unread' => notification.isUnread,
        'Mentions' => notification.filterGroup == 'Mentions',
        'Repo' => notification.filterGroup == 'Repo',
        'System' => notification.filterGroup == 'System',
        _ => true,
      };
    });
  }

  Future<void> _openNotification(
    BuildContext context,
    NotificationService notifications,
    ForkNotification notification,
  ) async {
    try {
      await notifications.markRead(notification);
    } catch (_) {}
    if (!context.mounted) return;
    final link = NotificationDeepLink.parse(notification.href);
    if (link == null) return;
    Navigator.of(context).push(
      MaterialPageRoute<void>(
        builder: (_) => RepoDetailScreen(
          repo: link.repo,
          initialTab: link.initialTab,
          initialTarget: link,
        ),
      ),
    );
  }
}

class _NotificationsHeader extends StatelessWidget {
  const _NotificationsHeader({
    required this.unreadCount,
    required this.onRefresh,
    required this.onMarkAllRead,
  });

  final int unreadCount;
  final Future<void> Function() onRefresh;
  final Future<void> Function()? onMarkAllRead;

  @override
  Widget build(BuildContext context) {
    final canPop = Navigator.of(context).canPop();

    return SizedBox(
      height: 72,
      child: Padding(
        padding: const EdgeInsets.symmetric(horizontal: FmSpace.x2),
        child: Row(
          children: [
            IconButton(
              tooltip: 'Back',
              onPressed: canPop ? () => Navigator.of(context).pop() : null,
              icon: const Icon(Icons.arrow_back_ios_new_rounded, size: 20),
              color: FmTheme.textPrimary(context),
              disabledColor: FmTheme.textDisabled(context),
            ),
            Expanded(
              child: Column(
                mainAxisAlignment: MainAxisAlignment.center,
                crossAxisAlignment: CrossAxisAlignment.start,
                children: [
                  Text(
                    'Notifications',
                    maxLines: 1,
                    overflow: TextOverflow.ellipsis,
                    style: TextStyle(
                      color: FmTheme.textPrimary(context),
                      fontSize: 22,
                      fontWeight: FontWeight.w800,
                      height: 1.1,
                    ),
                  ),
                  const SizedBox(height: FmSpace.x1),
                  Text(
                    '$unreadCount unread',
                    style: TextStyle(
                      color: FmTheme.textTertiary(context),
                      fontSize: 12,
                      fontWeight: FontWeight.w700,
                    ),
                  ),
                ],
              ),
            ),
            if (onMarkAllRead != null)
              TextButton(
                onPressed: onMarkAllRead,
                child: const Text('Mark all read'),
              ),
            IconButton(
              tooltip: 'Refresh notifications',
              onPressed: onRefresh,
              icon: const Icon(Icons.refresh_rounded),
              color: FmTheme.textPrimary(context),
            ),
          ],
        ),
      ),
    );
  }
}

class _NotificationFilterChip extends StatelessWidget {
  const _NotificationFilterChip({
    required this.label,
    required this.selected,
    required this.onSelected,
  });

  final String label;
  final bool selected;
  final VoidCallback onSelected;

  @override
  Widget build(BuildContext context) {
    return ChoiceChip(
      label: Text(label),
      selected: selected,
      showCheckmark: false,
      onSelected: (_) => onSelected(),
      labelStyle: TextStyle(
        color: selected
            ? FmTheme.textPrimary(context)
            : FmTheme.textSecondary(context),
        fontSize: 14,
        fontWeight: selected ? FontWeight.w700 : FontWeight.w600,
      ),
      padding: const EdgeInsets.symmetric(horizontal: FmSpace.x2),
      side: BorderSide(
        color: selected
            ? FmTheme.accent(context).withValues(alpha: 0.22)
            : FmTheme.border(context),
      ),
      selectedColor: FmTheme.accentSubtle(context),
      backgroundColor: FmTheme.bgRaised(context),
      shape: RoundedRectangleBorder(
        borderRadius: BorderRadius.circular(FmRadius.sm),
      ),
    );
  }
}

class _NotificationSectionLabel extends StatelessWidget {
  const _NotificationSectionLabel({required this.label});

  final String label;

  @override
  Widget build(BuildContext context) {
    return Text(
      label,
      style: TextStyle(
        color: FmTheme.textPrimary(context),
        fontSize: 16,
        fontWeight: FontWeight.w800,
      ),
    );
  }
}

class _NotificationRow extends StatelessWidget {
  const _NotificationRow({required this.notification, required this.onTap});

  final ForkNotification notification;
  final VoidCallback onTap;

  @override
  Widget build(BuildContext context) {
    final time = _timeLabel(notification.ts);
    final preview = notification.body.isEmpty
        ? 'Open context in ForkMesh to review this update.'
        : notification.body;
    final accent = notification.isUnread
        ? FmTheme.accent(context)
        : FmTheme.textTertiary(context);

    return Material(
      color: Colors.transparent,
      child: InkWell(
        onTap: onTap,
        borderRadius: BorderRadius.circular(FmRadius.md),
        child: Container(
          padding: const EdgeInsets.symmetric(vertical: FmSpace.x3),
          decoration: BoxDecoration(
            border: Border(
              bottom: BorderSide(color: FmTheme.border(context), width: 0.8),
            ),
          ),
          child: Row(
            crossAxisAlignment: CrossAxisAlignment.start,
            children: [
              Container(
                width: 52,
                height: 52,
                decoration: BoxDecoration(
                  color: notification.isUnread
                      ? FmTheme.accentSubtle(context)
                      : FmTheme.bgRaised(context),
                  shape: BoxShape.circle,
                ),
                child: Icon(
                  _iconFor(notification.kind),
                  color: accent,
                  size: 24,
                ),
              ),
              const SizedBox(width: FmSpace.x3),
              Expanded(
                child: Column(
                  crossAxisAlignment: CrossAxisAlignment.start,
                  children: [
                    Row(
                      crossAxisAlignment: CrossAxisAlignment.start,
                      children: [
                        Expanded(
                          child: Text(
                            notification.title,
                            maxLines: 2,
                            overflow: TextOverflow.ellipsis,
                            style: TextStyle(
                              color: FmTheme.textPrimary(context),
                              fontSize: 15,
                              fontWeight: notification.isUnread
                                  ? FontWeight.w900
                                  : FontWeight.w700,
                              height: 1.18,
                            ),
                          ),
                        ),
                        const SizedBox(width: FmSpace.x2),
                        if (time.isNotEmpty)
                          Text(
                            time,
                            style: TextStyle(
                              color: FmTheme.textTertiary(context),
                              fontSize: 13,
                              fontWeight: FontWeight.w500,
                            ),
                          ),
                      ],
                    ),
                    const SizedBox(height: FmSpace.x2),
                    Wrap(
                      spacing: FmSpace.x2,
                      runSpacing: FmSpace.x1,
                      children: [
                        _MiniPill(label: notification.kindLabel),
                        if (notification.repo.isNotEmpty)
                          _MiniPill(label: notification.repo),
                        if (notification.actor.isNotEmpty)
                          _MiniPill(label: '@${notification.actor}'),
                      ],
                    ),
                    const SizedBox(height: FmSpace.x2),
                    Text(
                      preview,
                      maxLines: 3,
                      overflow: TextOverflow.ellipsis,
                      style: TextStyle(
                        color: FmTheme.textSecondary(context),
                        fontSize: 13,
                        height: 1.45,
                      ),
                    ),
                    if (notification.href.isNotEmpty) ...[
                      const SizedBox(height: FmSpace.x2),
                      Text(
                        'Open context: ${notification.href}',
                        maxLines: 1,
                        overflow: TextOverflow.ellipsis,
                        style: TextStyle(
                          color: FmTheme.accent(context),
                          fontSize: 12,
                          fontWeight: FontWeight.w700,
                        ),
                      ),
                    ],
                  ],
                ),
              ),
            ],
          ),
        ),
      ),
    );
  }

  IconData _iconFor(String kind) => switch (kind) {
    'mention' => Icons.alternate_email_rounded,
    'pull_submitted' => Icons.call_merge_rounded,
    'issue_assigned' => Icons.assignment_ind_outlined,
    'host_online' => Icons.cloud_done_outlined,
    'host_offline' => Icons.cloud_off_outlined,
    'release_published' => Icons.new_releases_outlined,
    'bounty_funded' || 'bounty_paid' => Icons.payments_outlined,
    _ => FmIcons.notificationLine,
  };

  String _timeLabel(int ms) {
    if (ms <= 0) return '';
    final ts = DateTime.fromMillisecondsSinceEpoch(ms);
    return '${ts.hour.toString().padLeft(2, '0')}:${ts.minute.toString().padLeft(2, '0')}';
  }
}

class _MiniPill extends StatelessWidget {
  const _MiniPill({required this.label});

  final String label;

  @override
  Widget build(BuildContext context) => Container(
    padding: const EdgeInsets.symmetric(
      horizontal: FmSpace.x2,
      vertical: FmSpace.x1,
    ),
    decoration: BoxDecoration(
      color: FmTheme.bgRaised(context),
      borderRadius: BorderRadius.circular(FmRadius.sm),
      border: Border.all(color: FmTheme.border(context)),
    ),
    child: Text(
      label,
      style: TextStyle(
        color: FmTheme.textSecondary(context),
        fontSize: 11,
        fontWeight: FontWeight.w700,
      ),
    ),
  );
}

class _NotificationsEmptyState extends StatelessWidget {
  const _NotificationsEmptyState({
    required this.icon,
    required this.title,
    required this.message,
    this.action,
  });

  final IconData icon;
  final String title;
  final String message;
  final Widget? action;

  @override
  Widget build(BuildContext context) {
    return Center(
      child: Padding(
        padding: const EdgeInsets.all(FmSpace.x5),
        child: Column(
          mainAxisSize: MainAxisSize.min,
          children: [
            FmEmptyState(icon: icon, title: title, message: message),
            if (action != null) ...[
              const SizedBox(height: FmSpace.x3),
              action!,
            ],
          ],
        ),
      ),
    );
  }
}
