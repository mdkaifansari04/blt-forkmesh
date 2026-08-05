import 'dart:typed_data';

import '../errors/git_error.dart';

int findHeaderBodySeparator(Uint8List bytes) {
  for (var index = 0; index + 1 < bytes.length; index += 1) {
    if (bytes[index] == 0x0a && bytes[index + 1] == 0x0a) return index;
  }
  return -1;
}

String decodeAsciiHeaders(Uint8List bytes) {
  for (final byte in bytes) {
    if (byte == 0 || byte > 0x7f || byte == 0x0d) {
      throw const GitException(
        GitErrorCode.invalidObject,
        'object headers are not ASCII',
      );
    }
  }
  return String.fromCharCodes(bytes);
}
