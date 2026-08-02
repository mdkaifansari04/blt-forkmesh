import 'dart:convert';
import 'dart:typed_data';

import 'package:crypto/crypto.dart' as crypto;
import 'package:cryptography/cryptography.dart';

/// Dart port of the Qt client's RoomCrypto (RoomCrypto.cpp), byte-compatible so
/// this client interoperates with existing nodes on the same relay room.
///
/// AES-256-GCM with a 12-byte nonce and 16-byte tag. The key is
/// PBKDF2-HMAC-SHA256(passphrase-or-baked-app-key, salt, 210000, 32 bytes),
/// where salt = SHA256("ForkMesh room:" + room).first(16).
///
/// Wire envelope: {"kind":"cipher","v":1,"nonce":b64,"tag":b64,"body":b64}.
class RoomCrypto {
  RoomCrypto._(this._key);

  static const _appRoomKey = 'forkmesh-shared-room-key-v1';
  static const _rounds = 210000;
  static const _nonceBytes = 12;
  static const _tagBytes = 16;

  final SecretKey _key;
  final _aes = AesGcm.with256bits(nonceLength: _nonceBytes);

  /// Passphrase-free shared room (uses the baked-in app key).
  static Future<RoomCrypto> shared(String roomName) =>
      _derive(_appRoomKey, roomName);

  /// Passphrase-protected room.
  static Future<RoomCrypto> withPassphrase(
    String roomName,
    String passphrase,
  ) => _derive(passphrase, roomName);

  static Future<RoomCrypto> _derive(String secret, String roomName) async {
    final salt = _saltForRoom(roomName.trim());
    final pbkdf2 = Pbkdf2(
      macAlgorithm: Hmac.sha256(),
      iterations: _rounds,
      bits: 256,
    );
    final key = await pbkdf2.deriveKey(
      secretKey: SecretKey(utf8.encode(secret)),
      nonce: salt,
    );
    return RoomCrypto._(key);
  }

  static List<int> _saltForRoom(String roomName) {
    final digest = crypto.sha256.convert(
      utf8.encode('ForkMesh room:$roomName'),
    );
    return digest.bytes.sublist(0, 16);
  }

  /// Encrypt a JSON object into the wire envelope.
  Future<Map<String, dynamic>> encrypt(Map<String, dynamic> plain) async {
    final input = utf8.encode(jsonEncode(plain));
    final box = await _aes.encrypt(input, secretKey: _key);
    return {
      'kind': 'cipher',
      'v': 1,
      'nonce': base64.encode(box.nonce),
      'tag': base64.encode(box.mac.bytes),
      'body': base64.encode(box.cipherText),
    };
  }

  /// Decrypt a wire envelope back into a JSON object, or null on failure.
  Future<Map<String, dynamic>?> decrypt(Map<String, dynamic> envelope) async {
    if (envelope['kind'] != 'cipher') return null;
    try {
      final nonce = base64.decode(envelope['nonce'] as String);
      final tag = base64.decode(envelope['tag'] as String);
      final body = base64.decode(envelope['body'] as String);
      if (nonce.length != _nonceBytes || tag.length != _tagBytes) return null;
      final clear = await _aes.decrypt(
        SecretBox(body, nonce: nonce, mac: Mac(tag)),
        secretKey: _key,
      );
      final decoded = jsonDecode(utf8.decode(clear));
      return decoded is Map<String, dynamic> ? decoded : null;
    } catch (_) {
      return null;
    }
  }
}

/// Helper: lowercase hex of bytes (used for node ids).
String hex(List<int> bytes) {
  final sb = StringBuffer();
  for (final b in bytes) {
    sb.write(b.toRadixString(16).padLeft(2, '0'));
  }
  return sb.toString();
}

Uint8List bytesFromHex(String s) {
  final out = Uint8List(s.length ~/ 2);
  for (var i = 0; i < out.length; i++) {
    out[i] = int.parse(s.substring(i * 2, i * 2 + 2), radix: 16);
  }
  return out;
}
