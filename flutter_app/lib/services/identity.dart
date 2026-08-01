import 'dart:convert';

import 'package:cryptography/cryptography.dart';
import 'package:shared_preferences/shared_preferences.dart';






class Identity {
  Identity._(this.keyPair, this.publicKeyBytes);

  final SimpleKeyPair keyPair;
  final List<int> publicKeyBytes;

  static const _seedKey = 'identity/ed25519Seed';
  static final _ed25519 = Ed25519();


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



  Future<String> sign(List<int> data) async {
    final sig = await _ed25519.sign(data, keyPair: keyPair);
    return _b64url(sig.bytes);
  }

  static String _b64url(List<int> bytes) =>
      base64Url.encode(bytes).replaceAll('=', '');
}
