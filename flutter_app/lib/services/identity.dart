import 'dart:convert';

import 'package:cryptography/cryptography.dart';
import 'package:shared_preferences/shared_preferences.dart';

/// This node's Ed25519 identity. The public key, base64url-encoded without
/// padding, is the canonical node id — used as the chat sender id, the repo
/// owner namespace, and the `author` on signed inbox events. This matches
/// ForkMeshIdentity in the Qt client (raw 32-byte key, base64url) and the
/// worker's ed25519_verify, so events this app signs are accepted on the relay.
class Identity {
  Identity._(this.keyPair, this.publicKeyBytes);

  final SimpleKeyPair keyPair;
  final List<int> publicKeyBytes;

  static const _seedKey = 'identity/ed25519Seed';
  static final _ed25519 = Ed25519();

  /// base64url(raw 32-byte public key) with no padding.
  String get publicKeyB64url => _b64url(publicKeyBytes);
  String get nodeId => publicKeyB64url;
  String get shortKey {
    final k = publicKeyB64url;
    return k.length <= 16
        ? k
        : '${k.substring(0, 8)}...${k.substring(k.length - 8)}';
  }

  static Future<Identity> loadOrCreate() async {
    final prefs = await SharedPreferences.getInstance();
    final stored = prefs.getString(_seedKey);
    SimpleKeyPair pair;
    if (stored != null) {
      pair = await _ed25519.newKeyPairFromSeed(base64.decode(stored));
    } else {
      pair = await _ed25519.newKeyPair();
      final seed = await pair.extractPrivateKeyBytes();
      await prefs.setString(_seedKey, base64.encode(seed));
    }
    final pub = await pair.extractPublicKey();
    return Identity._(pair, pub.bytes);
  }

  /// Detached Ed25519 signature over [data], base64url with no padding —
  /// the `sig` field on inbox events.
  Future<String> sign(List<int> data) async {
    final sig = await _ed25519.sign(data, keyPair: keyPair);
    return _b64url(sig.bytes);
  }

  static String _b64url(List<int> bytes) =>
      base64Url.encode(bytes).replaceAll('=', '');
}
