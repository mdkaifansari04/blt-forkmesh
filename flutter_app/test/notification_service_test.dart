import 'package:flutter_test/flutter_test.dart';
import 'package:forkmesh/models/models.dart';
import 'package:forkmesh/services/api_service.dart';
import 'package:forkmesh/services/notification_service.dart';
import 'package:forkmesh/services/settings_service.dart';
import 'package:shared_preferences/shared_preferences.dart';

class NodeSwitchApiService extends ApiService {
  NodeSwitchApiService(super.settings);

  @override
  Future<NotificationPage> notifications(String node, {int limit = 40}) async {
    return NotificationPage(
      unread: 1,
      notifications: [
        ForkNotification(
          id: 'n-$node',
          kind: 'mention',
          title: 'Notification for $node',
        ),
      ],
    );
  }

  @override
  Future<void> markNotificationsRead(
    String node, {
    List<String> ids = const [],
    bool all = false,
  }) async {}
}

void main() {
  test(
    'service hides stale notifications immediately when node changes',
    () async {
      SharedPreferences.setMockInitialValues({});
      final settings = await SettingsService.create();
      final api = NodeSwitchApiService(settings);
      var node = 'alice';
      final service = NotificationService(api, nodeName: () => node);
      addTearDown(service.dispose);

      await service.refresh();
      expect(service.unreadCount, 1);
      expect(service.notifications.single.title, 'Notification for alice');

      node = 'bob';

      expect(service.unreadCount, 0);
      expect(service.notifications, isEmpty);

      await service.refresh();
      expect(service.unreadCount, 1);
      expect(service.notifications.single.title, 'Notification for bob');
    },
  );
}
