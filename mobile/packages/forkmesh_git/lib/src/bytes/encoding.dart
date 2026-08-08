import 'dart:convert';
import 'dart:typed_data';

import '../errors/git_error.dart';

Uint8List decodeLowerHex(String value) {
  if (value.length.isOdd || !RegExp(r'^[0-9a-f]*$').hasMatch(value)) {
    throw const GitException(
      GitErrorCode.invalidObject,
      'expected canonical lowercase hexadecimal',
    );
  }
  return Uint8List.fromList(<int>[
    for (var index = 0; index < value.length; index += 2)
      int.parse(value.substring(index, index + 2), radix: 16),
  ]);
}

String encodeLowerHex(List<int> value) =>
    value.map((int byte) => byte.toRadixString(16).padLeft(2, '0')).join();

String encodeCanonicalBase64Url(List<int> value) =>
    base64UrlEncode(value).replaceAll('=', '');

Uint8List decodeCanonicalBase64Url(String value) {
  if (value.contains('=') || !RegExp(r'^[A-Za-z0-9_-]*$').hasMatch(value)) {
    throw const GitException(
      GitErrorCode.invalidObject,
      'expected unpadded canonical base64url',
    );
  }
  final padding = '=' * ((4 - value.length % 4) % 4);
  final decoded = Uint8List.fromList(base64Url.decode('$value$padding'));
  if (encodeCanonicalBase64Url(decoded) != value) {
    throw const GitException(
      GitErrorCode.invalidObject,
      'base64url spelling is not canonical',
    );
  }
  return decoded;
}
