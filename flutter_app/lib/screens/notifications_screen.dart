import 'package:flutter/material.dart';
import 'package:provider/provider.dart';

import '../fm_icons.dart';
import '../models/models.dart';
import '../services/relay_service.dart';
import '../theme.dart';
import '../widgets/fm_ui.dart';

class NotificationsScreen extends StatefulWidget {
  const NotificationsScreen({super.key});

  @override
  State<NotificationsScreen> createState() => _NotificationsScreenState();
}

class _NotificationsScreenState extends State<NotificationsScreen> {
  static const _filters = ['Inbox', 'Channels', 'Direct', 'Attachments'];
  String _selectedFilter = 'Inbox';
  bool _markedSeen = false;

  @override
  void didChangeDependencies() {
    super.didChangeDependencies();
    if (_markedSeen) return;
    _markedSeen = true;
    WidgetsBinding.instance.addPostFrameCallback((_) {
      if (mounted) {
        context.read<RelayService>().markNotificationsSeen();
      }
    });
  }

  @override
  Widget build(BuildContext context) {
    final relay = context.watch<RelayService>();
    final messages = relay.notificationMessages.take(50).toList();
    final visibleMessages = _filterMessages(messages).toList();

    return Scaffold(
      backgroundColor: FmTheme.bgBase(context),
      body: SafeArea(
        child: Column(
          children: [
            const _NotificationsHeader(),
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
            Expanded(
              child: messages.isEmpty || visibleMessages.isEmpty
                  ? _NotificationsEmptyState(
                      title: messages.isEmpty
                          ? 'No notifications yet'
                          : 'No ${_selectedFilter.toLowerCase()} notifications',
                      message: messages.isEmpty
                          ? 'ForkMesh relay updates from channels and direct conversations will appear here.'
                          : 'Try another filter to see more updates.',
                    )
                  : ListView(
                      padding: const EdgeInsets.fromLTRB(
                        FmSpace.x4,
                        FmSpace.x2,
                        FmSpace.x4,
                        FmSpace.x5,
                      ),
                      children: [
                        const _NotificationSectionLabel(label: 'Today,'),
                        const SizedBox(height: FmSpace.x2),
                        for (final message in visibleMessages)
                          _NotificationRow(message: message),
                      ],
                    ),
            ),
          ],
        ),
      ),
    );
  }

  Iterable<ChatMessage> _filterMessages(List<ChatMessage> messages) {
    if (_selectedFilter == 'Inbox') return messages;

    return messages.where((message) {
      return switch (_selectedFilter) {
        // Peers may send channel names without the '#' prefix, so classify a
        // channel as any conversation that is not a direct ('@...') chat.
        'Channels' => !message.conversation.startsWith('@'),
        'Direct' => message.conversation.startsWith('@'),
        'Attachments' => message.fileName.isNotEmpty,
        _ => true,
      };
    });
  }
}

class _NotificationsHeader extends StatelessWidget {
  const _NotificationsHeader();

  @override
  Widget build(BuildContext context) {
    final canPop = Navigator.of(context).canPop();

    return SizedBox(
      height: 64,
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
              child: Text(
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
            ),
            const SizedBox(width: 48),
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
  const _NotificationRow({required this.message});

  final ChatMessage message;

  @override
  Widget build(BuildContext context) {
    final ts = message.timestamp;
    final time =
        '${ts.hour.toString().padLeft(2, '0')}:${ts.minute.toString().padLeft(2, '0')}';
    final preview = message.text.isEmpty ? 'Sent an attachment' : message.text;

    return Container(
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
              color: FmTheme.accentSubtle(context),
              shape: BoxShape.circle,
            ),
            child: Icon(
              FmIcons.notificationLine,
              color: FmTheme.textPrimary(context),
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
                        '${message.senderName} in ${message.conversation}',
                        maxLines: 2,
                        overflow: TextOverflow.ellipsis,
                        style: TextStyle(
                          color: FmTheme.textPrimary(context),
                          fontSize: 15,
                          fontWeight: FontWeight.w800,
                          height: 1.18,
                        ),
                      ),
                    ),
                    const SizedBox(width: FmSpace.x2),
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
                const SizedBox(height: FmSpace.x1),
                Text.rich(
                  TextSpan(
                    children: [
                      TextSpan(text: preview),
                      if (message.fileName.isNotEmpty)
                        TextSpan(
                          text: ' - ${message.fileName}',
                          style: TextStyle(
                            color: FmTheme.textPrimary(context),
                            fontWeight: FontWeight.w700,
                          ),
                        ),
                    ],
                  ),
                  maxLines: 3,
                  overflow: TextOverflow.ellipsis,
                  style: TextStyle(
                    color: FmTheme.textSecondary(context),
                    fontSize: 13,
                    height: 1.45,
                  ),
                ),
              ],
            ),
          ),
        ],
      ),
    );
  }
}

class _NotificationsEmptyState extends StatelessWidget {
  const _NotificationsEmptyState({required this.title, required this.message});

  final String title;
  final String message;

  @override
  Widget build(BuildContext context) {
    return Center(
      child: Padding(
        padding: const EdgeInsets.all(FmSpace.x5),
        child: FmEmptyState(
          icon: FmIcons.notificationLine,
          title: title,
          message: message,
        ),
      ),
    );
  }
}
