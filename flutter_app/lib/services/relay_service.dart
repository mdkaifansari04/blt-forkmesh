import 'dart:async';
import 'dart:convert';
import 'dart:io' show Platform;

import 'package:flutter/foundation.dart';
import 'package:flutter/widgets.dart'
    show WidgetsBinding, WidgetsBindingObserver, AppLifecycleState;
import 'package:web_socket_channel/web_socket_channel.dart';
import 'package:web_socket_channel/status.dart' as ws_status;

import '../models/models.dart';
import 'identity.dart';
import 'performance_monitor_service.dart';
import 'room_crypto.dart';
import 'settings_service.dart';

enum RelayConnectionState { offline, connecting, connected }

/// Live relay connection: WebSocket transport + RoomCrypto, mirroring the Qt
/// client's ServerNode. Speaks the same encrypted JSON envelope protocol, so it
/// shares rooms with existing nodes. Exposes roster, channels, and per-channel
/// message history as a ChangeNotifier for the UI.
class RelayService extends ChangeNotifier with WidgetsBindingObserver {
  RelayService(
    this._settings,
    this._identity, {
    PerformanceMonitorService? performanceMonitor,
  }) : _performanceMonitor = performanceMonitor;

  final SettingsService _settings;
  final Identity _identity;
  final PerformanceMonitorService? _performanceMonitor;

  // Presence cadence + staleness window (matches the ServerNode fix: peers not
  // heard from within the window are dropped so stale nodes don't show online).
  static const _presenceInterval = Duration(seconds: 60);
  static const _staleMs = 180000;

  WebSocketChannel? _channel;
  RoomCrypto? _crypto;
  StreamSubscription? _sub;
  Timer? _presenceTimer;
  Timer? _reconnectTimer;
  int _reconnectAttempt = 0;
  bool _userStopped = false;
  bool _observing = false;

  RelayConnectionState _state = RelayConnectionState.offline;
  RelayConnectionState get state => _state;

  final List<String> channels = ['#general', '#random'];
  String currentConversation = '#general';

  final List<String> log = [];

  // peerId -> peer record
  final Map<String, _Peer> _peers = {};
  // conversation -> messages
  final Map<String, List<ChatMessage>> _history = {};
  final Set<String> _seenIds = {};
  final Set<String> _seenNotificationIds = {};
  final Set<String> unread = {};

  String get _nodeId => _identity.nodeId;
  String get _name => _settings.displayName.isEmpty
      ? _identity.shortKey
      : _settings.displayName;

  String _platform() {
    if (kIsWeb) return 'web';
    try {
      if (Platform.isLinux) return 'linux';
      if (Platform.isMacOS) return 'macos';
      if (Platform.isWindows) return 'windows';
      if (Platform.isAndroid) return 'android';
      if (Platform.isIOS) return 'ios';
    } catch (_) {}
    return 'unknown';
  }

  List<Member> roster() {
    final now = DateTime.now().millisecondsSinceEpoch;
    final me = Member(
      id: _nodeId,
      name: _name,
      self: true,
      online: _state == RelayConnectionState.connected,
      platform: _platform(),
    );
    final list = <Member>[me];
    for (final entry in _peers.entries) {
      final p = entry.value;
      if (!p.online || now - p.lastSeenMs > _staleMs) continue;
      list.add(
        Member(
          id: entry.key,
          name: p.name,
          online: true,
          platform: p.platform,
          version: p.version,
          solanaAddress: p.solana,
          mirrors: p.mirrors,
        ),
      );
    }
    return list;
  }

  int get onlineCount => roster().where((m) => m.online).length;

  List<ChatMessage> messages(String conversation) =>
      _history[conversation] ?? const [];

  List<ChatMessage> get recentMessages {
    final items = _history.values.expand((m) => m).toList()
      ..sort((a, b) => b.timestamp.compareTo(a.timestamp));
    return items;
  }

  List<ChatMessage> get unreadMessages => recentMessages
      .where((m) => !m.self && unread.contains(m.conversation))
      .toList();

  List<ChatMessage> get notificationMessages =>
      recentMessages.where((m) => !m.self).toList();

  int get unseenNotificationCount => notificationMessages
      .where((m) => !_seenNotificationIds.contains(m.id))
      .length;

  void markNotificationsSeen() {
    final before = _seenNotificationIds.length;
    for (final message in notificationMessages) {
      if (message.id.isNotEmpty) {
        _seenNotificationIds.add(message.id);
      }
    }
    if (_seenNotificationIds.length != before) {
      notifyListeners();
    }
  }

  // ---- lifecycle ----------------------------------------------------------

  Future<void> connect() async {
    _userStopped = false;
    _reconnectTimer?.cancel();
    // Watch the app lifecycle so the relay stays reachable across
    // foreground/background transitions: mobile OSes freeze our timers and tear
    // down the WebSocket while backgrounded, so on resume we reconnect at once
    // (instead of waiting out the exponential backoff) and re-announce presence
    // so this node reappears online to peers without the usual 60s delay.
    if (!_observing) {
      WidgetsBinding.instance.addObserver(this);
      _observing = true;
    }
    await _open();
  }

  @override
  void didChangeAppLifecycleState(AppLifecycleState state) {
    if (state != AppLifecycleState.resumed || _userStopped) return;
    // Coming back to the foreground. Reset the backoff so the next attempt is
    // immediate rather than delayed.
    _reconnectTimer?.cancel();
    _reconnectAttempt = 0;
    if (_state == RelayConnectionState.connected) {
      // The socket may have been silently killed by the OS while paused; a
      // presence send both refreshes our online status and surfaces a dead
      // transport (its failure routes through _onClosed -> reconnect).
      _sendPresence();
    } else {
      _open();
    }
  }

  Future<void> _open() async {
    final monitor = _performanceMonitor;
    if (monitor != null) {
      return monitor.track(
        'relay.open',
        _openUntracked,
        details: {
          'server': Uri.parse(_settings.serverUrl).host,
          'room': _settings.room,
        },
      );
    }
    return _openUntracked();
  }

  Future<void> _openUntracked() async {
    await _teardownSocket();
    _setState(RelayConnectionState.connecting);
    _crypto = _settings.passphrase.isEmpty
        ? await RoomCrypto.shared(_settings.room)
        : await RoomCrypto.withPassphrase(_settings.room, _settings.passphrase);
    try {
      final uri = Uri.parse(_settings.serverUrl);
      final channel = WebSocketChannel.connect(uri);
      _channel = channel;
      await channel.ready;
      _setState(RelayConnectionState.connected);
      _reconnectAttempt = 0;
      _logLine('Connected to ${uri.host} · room ${_settings.room}');
      _sub = channel.stream.listen(
        _onFrame,
        onDone: _onClosed,
        onError: (_) => _onClosed(),
        cancelOnError: false,
      );
      await _sendHello();
      _presenceTimer?.cancel();
      _presenceTimer = Timer.periodic(_presenceInterval, (_) {
        _sendPresence();
        notifyListeners(); // re-evaluate staleness even with no inbound traffic
      });
    } catch (e) {
      _logLine('Connect failed: $e');
      _onClosed();
    }
  }

  Future<void> disconnect() async {
    _userStopped = true;
    _reconnectTimer?.cancel();
    _presenceTimer?.cancel();
    try {
      await _send({'type': 'bye'});
    } catch (_) {}
    await _teardownSocket();
    _peers.clear();
    _setState(RelayConnectionState.offline);
  }

  Future<void> _teardownSocket() async {
    _presenceTimer?.cancel();
    await _sub?.cancel();
    _sub = null;
    try {
      await _channel?.sink.close(ws_status.normalClosure);
    } catch (_) {}
    _channel = null;
  }

  void _onClosed() {
    _presenceTimer?.cancel();
    _channel = null;
    if (_userStopped) {
      _setState(RelayConnectionState.offline);
      return;
    }
    _setState(RelayConnectionState.connecting);
    _scheduleReconnect();
  }

  void _scheduleReconnect() {
    if (_userStopped || (_reconnectTimer?.isActive ?? false)) return;
    final delaySec = (1 << (_reconnectAttempt.clamp(0, 5))).clamp(1, 30);
    _reconnectAttempt++;
    _reconnectTimer = Timer(Duration(seconds: delaySec), () {
      if (!_userStopped) _open();
    });
  }

  // ---- send ---------------------------------------------------------------

  Map<String, dynamic> _envelopeBase(String type) {
    final m = <String, dynamic>{
      'type': type,
      'id': _uuid(),
      'senderId': _nodeId,
      'sender': _name,
      'ts': DateTime.now().millisecondsSinceEpoch,
      'platform': _platform(),
      'version': 'flutter-1.0',
    };
    final solana = _settings.solanaAddress;
    if (solana.isNotEmpty) m['solana'] = solana;
    return m;
  }

  Future<bool> _send(Map<String, dynamic> plain) async {
    final monitor = _performanceMonitor;
    if (monitor != null) {
      return monitor.track(
        'relay.send.${plain['type'] ?? 'unknown'}',
        () => _sendUntracked(plain),
        details: {
          'type': '${plain['type'] ?? ''}',
          if (plain['conversation'] != null)
            'conversation': '${plain['conversation']}',
        },
      );
    }
    return _sendUntracked(plain);
  }

  Future<bool> _sendUntracked(Map<String, dynamic> plain) async {
    final crypto = _crypto;
    final channel = _channel;
    if (crypto == null ||
        channel == null ||
        _state != RelayConnectionState.connected) {
      return false;
    }
    try {
      final persist = plain['persist'] == true;
      final payload = persist
          ? (Map<String, dynamic>.from(plain)..remove('persist'))
          : plain;
      final env = await crypto.encrypt(payload);
      if (persist) {
        env['persist'] = true;
      }
      channel.sink.add(jsonEncode(env));
      return true;
    } catch (e) {
      _logLine('Send failed: $e');
      _onClosed();
      return false;
    }
  }

  Future<void> _sendHello() async {
    final hello = _envelopeBase('hello')..['channels'] = channels;
    await _send(hello);
  }

  Future<void> _sendPresence() async {
    if (_state != RelayConnectionState.connected) return;
    await _send(_envelopeBase('presence'));
  }

  Future<bool> sendMessage(String text) async {
    final trimmed = text.trim();
    if (trimmed.isEmpty) return false;
    final conversation = currentConversation;
    final msg = _envelopeBase(conversation.startsWith('@') ? 'dm' : 'chat')
      ..['text'] = trimmed
      ..['conversation'] = conversation;
    if (conversation.startsWith('#')) {
      msg['channel'] = conversation;
      msg['persist'] = true;
    } else {
      msg['to'] = conversation.substring(1);
    }
    final ts = DateTime.fromMillisecondsSinceEpoch(msg['ts'] as int);
    // Optimistically show our own message.
    final appended = _appendMessage(
      ChatMessage(
        id: msg['id'] as String,
        conversation: conversation,
        senderId: _nodeId,
        senderName: _name,
        text: trimmed,
        timestamp: ts,
        self: true,
      ),
    );
    _seenIds.add(msg['id'] as String);
    if (appended) {
      notifyListeners();
    }
    final sent = await _send(msg);
    if (!sent) {
      _removeMessage(conversation, msg['id'] as String);
      _seenIds.remove(msg['id']);
      notifyListeners();
    }
    return sent;
  }

  void switchConversation(String conversation) {
    currentConversation = conversation;
    unread.remove(conversation);
    notifyListeners();
  }

  // ---- receive ------------------------------------------------------------

  Future<void> _onFrame(dynamic raw) async {
    final crypto = _crypto;
    if (crypto == null) return;
    Map<String, dynamic>? env;
    try {
      final decoded = jsonDecode(raw is String ? raw : utf8.decode(raw));
      if (decoded is Map<String, dynamic>) env = decoded;
    } catch (_) {
      return;
    }
    if (env == null) return;
    final plain = await crypto.decrypt(env);
    if (plain == null) return;
    _handle(plain);
  }

  void _handle(Map<String, dynamic> m) {
    final type = (m['type'] ?? '').toString();
    final senderId = (m['senderId'] ?? '').toString();
    final sender = (m['sender'] ?? '').toString();
    if (type != 'typing') {
      final id = (m['id'] ?? '').toString();
      if (id.isNotEmpty) {
        if (_seenIds.contains(id)) {
          // already handled (e.g. our own echo) but still refresh presence
          if (senderId.isNotEmpty && senderId != _nodeId) {
            _rememberPeer(senderId, sender, m);
          }
          return;
        }
        _seenIds.add(id);
      }
    }
    if (senderId.isNotEmpty && senderId != _nodeId) {
      _rememberPeer(senderId, sender, m);
    }

    switch (type) {
      case 'hello':
        final incoming =
            (m['channels'] as List?)?.map((e) => e.toString()) ?? const [];
        for (final c in incoming) {
          if (c.isNotEmpty && !channels.contains(c)) channels.add(c);
        }
        // Reply to a broadcast hello so the newcomer sees us.
        if ((m['to'] ?? '').toString().isEmpty && senderId != _nodeId) {
          final reply = _envelopeBase('hello')
            ..['channels'] = channels
            ..['to'] = senderId;
          _send(reply);
        }
        break;
      case 'chat':
        final channel = (m['channel'] ?? '#general').toString();
        if (!channels.contains(channel)) channels.add(channel);
        _appendMessage(_toMessage(m, channel));
        break;
      case 'dm':
        if ((m['to'] ?? '').toString() == _nodeId) {
          _appendMessage(_toMessage(m, '@$senderId'));
        }
        break;
      case 'bye':
        if (_peers.remove(senderId) != null) {
          _logLine('$sender left');
        }
        break;
      // presence / typing / mirror-update / avatar: peer already refreshed.
    }
    notifyListeners();
  }

  ChatMessage _toMessage(Map<String, dynamic> m, String conversation) {
    final senderId = (m['senderId'] ?? '').toString();
    final tsRaw = m['ts'];
    final ts = tsRaw is num
        ? DateTime.fromMillisecondsSinceEpoch(tsRaw.toInt())
        : DateTime.now();
    return ChatMessage(
      id: (m['id'] ?? '').toString(),
      conversation: conversation,
      senderId: senderId,
      senderName: (m['sender'] ?? '').toString(),
      text: (m['text'] ?? '').toString(),
      timestamp: ts,
      self: senderId == _nodeId,
      fileName: (m['fileName'] ?? '').toString(),
    );
  }

  bool _appendMessage(ChatMessage msg) {
    final list = _history[msg.conversation] ?? const <ChatMessage>[];
    if (list.any((m) => m.id == msg.id)) return false;
    final next = <ChatMessage>[...list, msg]..sort(_compareMessages);
    _history[msg.conversation] = List.unmodifiable(next);
    if (!msg.self && msg.conversation != currentConversation) {
      unread.add(msg.conversation);
    }
    return true;
  }

  void _removeMessage(String conversation, String id) {
    final list = _history[conversation];
    if (list == null) return;
    final next = list.where((m) => m.id != id).toList(growable: false);
    if (next.length == list.length) return;
    if (next.isEmpty) {
      _history.remove(conversation);
    } else {
      _history[conversation] = List.unmodifiable(next);
    }
    _seenNotificationIds.remove(id);
  }

  int _compareMessages(ChatMessage a, ChatMessage b) {
    final byTimestamp = a.timestamp.compareTo(b.timestamp);
    if (byTimestamp != 0) return byTimestamp;
    return a.id.compareTo(b.id);
  }

  void _rememberPeer(String id, String name, Map<String, dynamic> m) {
    final p = _peers.putIfAbsent(id, () => _Peer());
    if (name.isNotEmpty) p.name = name;
    final platform = (m['platform'] ?? '').toString();
    if (platform.isNotEmpty) p.platform = platform;
    final version = (m['version'] ?? '').toString();
    if (version.isNotEmpty) p.version = version;
    final solana = (m['solana'] ?? '').toString();
    if (solana.isNotEmpty) p.solana = solana;
    final mirrors = m['mirrors'];
    if (mirrors is List) p.mirrors = mirrors.map((e) => e.toString()).toList();
    p.online = true;
    p.lastSeenMs = DateTime.now().millisecondsSinceEpoch;
  }

  // ---- misc ---------------------------------------------------------------

  void _setState(RelayConnectionState s) {
    if (_state == s) return;
    _state = s;
    notifyListeners();
  }

  void _logLine(String line) {
    final stamp = DateTime.now().toIso8601String().substring(11, 19);
    log.add('$stamp  $line');
    if (log.length > 500) log.removeAt(0);
    notifyListeners();
  }

  static int _seq = 0;
  String _uuid() {
    _seq++;
    final r = DateTime.now().microsecondsSinceEpoch;
    return '$_nodeId-$r-$_seq';
  }

  @override
  void dispose() {
    _userStopped = true;
    if (_observing) {
      WidgetsBinding.instance.removeObserver(this);
      _observing = false;
    }
    _presenceTimer?.cancel();
    _reconnectTimer?.cancel();
    _sub?.cancel();
    _channel?.sink.close();
    super.dispose();
  }
}

class _Peer {
  String name = '';
  String platform = '';
  String version = '';
  String solana = '';
  List<String> mirrors = const [];
  bool online = true;
  int lastSeenMs = 0;
}
