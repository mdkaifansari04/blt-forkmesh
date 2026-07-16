import 'dart:typed_data';

import '../errors/git_error.dart';

final class CheckedBytesReader {
  CheckedBytesReader(List<int> bytes) : _bytes = Uint8List.fromList(bytes);

  final Uint8List _bytes;
  var _offset = 0;

  int get offset => _offset;
  int get remaining => _bytes.length - _offset;
  bool get isAtEnd => remaining == 0;

  int readByte() {
    if (remaining == 0) {
      throw const GitException(
        GitErrorCode.invalidObject,
        'unexpected end of input',
      );
    }
    return _bytes[_offset++];
  }

  Uint8List readBytes(int length) {
    if (length < 0 || length > remaining) {
      throw const GitException(
        GitErrorCode.resourceLimitExceeded,
        'declared byte range exceeds remaining input',
      );
    }
    final result = Uint8List.sublistView(_bytes, _offset, _offset + length);
    _offset += length;
    return result;
  }

  int readUint32() {
    final bytes = readBytes(4);
    return (bytes[0] << 24) | (bytes[1] << 16) | (bytes[2] << 8) | bytes[3];
  }
}

final class CheckedBytesWriter {
  final BytesBuilder _builder = BytesBuilder(copy: false);

  int get length => _builder.length;

  void writeByte(int value) {
    if (value < 0 || value > 0xff) {
      throw ArgumentError.value(value, 'value', 'must be a byte');
    }
    _builder.addByte(value);
  }

  void writeBytes(List<int> values) => _builder.add(values);

  void writeUint32(int value) {
    if (value < 0 || value > 0xffffffff) {
      throw ArgumentError.value(
        value,
        'value',
        'must be an unsigned 32-bit integer',
      );
    }
    _builder.add(<int>[value >> 24, value >> 16, value >> 8, value]);
  }

  Uint8List takeBytes() => _builder.takeBytes();
}
