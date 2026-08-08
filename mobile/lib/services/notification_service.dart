import 'dart:async';

import 'package:flutter/foundation.dart';

import '../models/models.dart';
import 'api_service.dart';

class NotificationService extends ChangeNotifier {
  NotificationService(
    this._api, {
    required String Function() nodeName,
    Duration pollInterval = const Duration(minutes: 1),
  }) : _nodeName = nodeName,
       _pollInterval = pollInterval;

  final ApiService _api;
  final String Function() _nodeName;
  final Duration _pollInterval;
  Timer? _pollTimer;

  List<ForkNotification> _notifications = const [];
  int _unreadCount = 0;
  bool _loading = false;
  String _error = '';
  String _activeNode = '';

  List<ForkNotification> get notifications =>
      _hasCurrentNode ? _notifications : const <ForkNotification>[];
  int get unreadCount => _hasCurrentNode ? _unreadCount : 0;
  bool get loading => _hasCurrentNode && _loading;
  String get error => _hasCurrentNode ? _error : '';
  bool get hasLoaded =>
      _hasCurrentNode &&
      (_loading || _notifications.isNotEmpty || _error.isNotEmpty);

  String get _node => _nodeName().trim().toLowerCase();
  bool get _hasCurrentNode => _node == _activeNode;

  void startPolling() {
    _pollTimer ??= Timer.periodic(_pollInterval, (_) => unawaited(refresh()));
    final nodeChanged = _node != _activeNode;
    if (nodeChanged || !hasLoaded) unawaited(refresh());
  }

  Future<void> refresh() async {
    final node = _node;
    if (node != _activeNode) {
      _activeNode = node;
      _notifications = const [];
      _unreadCount = 0;
      _error = '';
      _loading = false;
      notifyListeners();
    }
    if (node.isEmpty) return;
    _loading = true;
    _error = '';
    notifyListeners();
    try {
      final page = await _api.notifications(node);
      if (node != _activeNode) return;
      _notifications = page.notifications;
      _unreadCount = page.unread;
    } catch (error) {
      if (node == _activeNode) _error = error.toString();
    } finally {
      if (node == _activeNode) {
        _loading = false;
        notifyListeners();
      }
    }
  }

  Future<void> markAllRead() async {
    final node = _node;
    if (node.isEmpty || node != _activeNode || _notifications.isEmpty) return;
    await _api.markNotificationsRead(node, all: true);
    _notifications = [
      for (final notification in _notifications)
        notification.copyWith(readAt: DateTime.now().millisecondsSinceEpoch),
    ];
    _unreadCount = 0;
    notifyListeners();
  }

  Future<void> markRead(ForkNotification notification) async {
    if (!notification.isUnread || notification.id.isEmpty) return;
    final node = _node;
    if (node.isEmpty || node != _activeNode) return;
    await _api.markNotificationsRead(node, ids: [notification.id]);
    _notifications = [
      for (final item in _notifications)
        item.id == notification.id
            ? item.copyWith(readAt: DateTime.now().millisecondsSinceEpoch)
            : item,
    ];
    _unreadCount = _notifications.where((item) => item.isUnread).length;
    notifyListeners();
  }

  @override
  void dispose() {
    _pollTimer?.cancel();
    super.dispose();
  }
}
