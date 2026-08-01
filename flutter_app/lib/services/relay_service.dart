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





class RelayService extends ChangeNotifier with WidgetsBindingObserver {
  RelayService(
    this._settings,
    this._identity, {
    PerformanceMonitorService? performanceMonitor,
  }) : _performanceMonitor = performanceMonitor;

  final SettingsService _settings;
  final Identity _identity;
  final PerformanceMonitorService? _performanceMonitor;




  Future<String> Function()? roomPassphraseProvider;
  String _sharedPassphrase = '';



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


  final Map<String, _Peer> _peers = {};

  final Map<String, List<ChatMessage>> _history = {};
  final Set<String> _seenIds = {};
  final Set<String> _seenNotificationIds = {};



  final Map<String, int> _unread = {};


  Iterable<String> get unreadConversations => _unread.keys;


  bool hasUnread(String conversation) => (_unread[conversation] ?? 0) > 0;


  int unreadCountFor(String conversation) => _unread[conversation] ?? 0;


  int get totalUnread => _unread.values.fold(0, (sum, n) => sum + n);

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
          owner: p.owner,
          nodeName: p.nodeName,
        ),
      );
    }
    return list;
  }

  int get onlineCount => roster().where((m) => m.online).length;






  List<MemberGroup> rosterGroups() {
    final byKey = <String, List<Member>>{};
    for (final m in roster()) {
      byKey.putIfAbsent(_identityKey(m), () => <Member>[]).add(m);
    }


    final nameCounts = <String, int>{};
    for (final nodes in byKey.values) {
      final name = _displayName(nodes.first).toLowerCase();
      nameCounts[name] = (nameCounts[name] ?? 0) + 1;
    }
    final groups = <MemberGroup>[];
    for (final nodes in byKey.values) {
      nodes.sort(_compareNodes);
      final primary = nodes.first;
      final name = _displayName(primary);
      final collides = (nameCounts[name.toLowerCase()] ?? 0) > 1;
      groups.add(
        MemberGroup(
          name: name,
          members: nodes,
          self: nodes.any((m) => m.self),
          disambiguator: collides ? _shortIdentity(primary) : '',
        ),
      );
    }
    return groups;
  }



  String _displayName(Member m) {
    if (m.owner.trim().isNotEmpty) return m.owner.trim();
    if (m.name.trim().isNotEmpty) return m.name.trim();
    if (m.nodeName.trim().isNotEmpty) return m.nodeName.trim();
    return m.id;
  }




  String _identityKey(Member m) {
    if (m.self) return 'self';
    final owner = m.owner.trim().toLowerCase();
    if (owner.isNotEmpty) return 'owner:$owner';
    final wallet = m.solanaAddress.trim();
    if (wallet.isNotEmpty) return 'wallet:$wallet';
    final name = _displayName(m).toLowerCase();
    if (name.isNotEmpty) return 'name:$name';
    return 'node:${m.id}';
  }


  String _shortIdentity(Member m) {
    final raw = m.solanaAddress.trim().isNotEmpty
        ? m.solanaAddress.trim()
        : m.id;
    if (raw.length <= 10) return raw;
    return '${raw.substring(0, 4)}…${raw.substring(raw.length - 4)}';
  }


  int _compareNodes(Member a, Member b) {
    if (a.self != b.self) return a.self ? -1 : 1;
    if (a.online != b.online) return a.online ? -1 : 1;
    return a.id.compareTo(b.id);
  }

  List<ChatMessage> messages(String conversation) =>
      _history[conversation] ?? const [];

  List<ChatMessage> get recentMessages {
    final items = _history.values.expand((m) => m).toList()
      ..sort((a, b) => b.timestamp.compareTo(a.timestamp));
    return items;
  }

  List<ChatMessage> get unreadMessages => recentMessages
      .where((m) => !m.self && _unread.containsKey(m.conversation))
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



  Future<void> connect() async {
    _userStopped = false;
    _reconnectTimer?.cancel();





    if (!_observing) {
      WidgetsBinding.instance.addObserver(this);
      _observing = true;
    }
    await _open();
  }

  @override
  void didChangeAppLifecycleState(AppLifecycleState state) {
    if (state != AppLifecycleState.resumed || _userStopped) return;


    _reconnectTimer?.cancel();
    _reconnectAttempt = 0;
    if (_state == RelayConnectionState.connected) {



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
    if (_settings.passphrase.isNotEmpty) {

      _crypto = await RoomCrypto.withPassphrase(
        _settings.room,
        _settings.passphrase,
      );
    } else {


      if (_sharedPassphrase.isEmpty && roomPassphraseProvider != null) {
        _sharedPassphrase = await roomPassphraseProvider!();
      }
      if (_sharedPassphrase.isEmpty) {
        throw Exception('Sign in to join chat — room key unavailable.');
      }
      _crypto = await RoomCrypto.withPassphrase(
        _settings.room,
        _sharedPassphrase,
      );
    }
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
        notifyListeners();
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
    _unread.remove(conversation);
    notifyListeners();
  }


  void markConversationRead(String conversation) {
    if (_unread.remove(conversation) != null) notifyListeners();
  }


  void markAllRead() {
    if (_unread.isEmpty) return;
    _unread.clear();
    notifyListeners();
  }



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
      _unread[msg.conversation] = (_unread[msg.conversation] ?? 0) + 1;
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
    final owner = (m['ownerUser'] ?? m['owner'] ?? '').toString();
    if (owner.isNotEmpty) p.owner = owner;
    final nodeName = (m['nodeName'] ?? '').toString();
    if (nodeName.isNotEmpty) p.nodeName = nodeName;
    final mirrors = m['mirrors'];
    if (mirrors is List) p.mirrors = mirrors.map((e) => e.toString()).toList();
    p.online = true;
    p.lastSeenMs = DateTime.now().millisecondsSinceEpoch;
  }



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
  String owner = '';
  String nodeName = '';
  List<String> mirrors = const [];
  bool online = true;
  int lastSeenMs = 0;
}
