import 'dart:typed_data';

import '../errors/git_error.dart';
import 'digest.dart';

final class ForkMeshSha1 implements GitDigest {
  ForkMeshSha1({int maxInputBytes = maximumInputBytes})
    : _maxInputBytes = maxInputBytes {
    if (maxInputBytes < 0 || maxInputBytes > maximumInputBytes) {
      throw ArgumentError.value(maxInputBytes, 'maxInputBytes');
    }
  }

  static const maximumInputBytes = 0x1fffffffffffffff;

  final BytesBuilder _pending = BytesBuilder(copy: false);
  final int _maxInputBytes;
  var _lengthBytes = 0;
  var _h0 = 0x67452301;
  var _h1 = 0xefcdab89;
  var _h2 = 0x98badcfe;
  var _h3 = 0x10325476;
  var _h4 = 0xc3d2e1f0;
  var _closed = false;

  @override
  void add(List<int> bytes) {
    if (_closed) throw StateError('digest is closed');
    if (bytes.length > _maxInputBytes - _lengthBytes) {
      throw const GitException(
        GitErrorCode.hashImplementationFailure,
        'SHA-1 input length exceeds supported accounting range',
      );
    }
    _lengthBytes += bytes.length;
    _pending.add(bytes);
    _drainFullBlocks();
  }

  void _drainFullBlocks() {
    final data = _pending.takeBytes();
    final complete = data.length - data.length % 64;
    for (var offset = 0; offset < complete; offset += 64) {
      _compress(Uint8List.sublistView(data, offset, offset + 64));
    }
    _pending.add(data.sublist(complete));
  }

  void _compress(Uint8List block) {
    final words = Uint32List(80);
    for (var index = 0; index < 16; index += 1) {
      final offset = index * 4;
      words[index] =
          (block[offset] << 24) |
          (block[offset + 1] << 16) |
          (block[offset + 2] << 8) |
          block[offset + 3];
    }
    for (var index = 16; index < 80; index += 1) {
      words[index] = rotateLeft32(
        words[index - 3] ^
            words[index - 8] ^
            words[index - 14] ^
            words[index - 16],
        1,
      );
    }
    var a = _h0;
    var b = _h1;
    var c = _h2;
    var d = _h3;
    var e = _h4;
    for (var index = 0; index < 80; index += 1) {
      final f = index < 20
          ? (b & c) | (~b & d)
          : index < 40
          ? b ^ c ^ d
          : index < 60
          ? (b & c) | (b & d) | (c & d)
          : b ^ c ^ d;
      final k = index < 20
          ? 0x5a827999
          : index < 40
          ? 0x6ed9eba1
          : index < 60
          ? 0x8f1bbcdc
          : 0xca62c1d6;
      final temp = (rotateLeft32(a, 5) + f + e + k + words[index]) & 0xffffffff;
      e = d;
      d = c;
      c = rotateLeft32(b, 30);
      b = a;
      a = temp;
    }
    _h0 = (_h0 + a) & 0xffffffff;
    _h1 = (_h1 + b) & 0xffffffff;
    _h2 = (_h2 + c) & 0xffffffff;
    _h3 = (_h3 + d) & 0xffffffff;
    _h4 = (_h4 + e) & 0xffffffff;
  }

  @override
  Uint8List close() {
    if (_closed) throw StateError('digest is closed');
    _closed = true;
    final padded = BytesBuilder(copy: false)
      ..add(_pending.toBytes())
      ..addByte(0x80);
    while ((padded.length + 8) % 64 != 0) {
      padded.addByte(0);
    }
    final length = ByteData(8)..setUint64(0, _lengthBytes * 8, Endian.big);
    padded.add(length.buffer.asUint8List());
    final finalBytes = padded.takeBytes();
    for (var offset = 0; offset < finalBytes.length; offset += 64) {
      _compress(Uint8List.sublistView(finalBytes, offset, offset + 64));
    }
    final output = ByteData(20);
    output.setUint32(0, _h0, Endian.big);
    output.setUint32(4, _h1, Endian.big);
    output.setUint32(8, _h2, Endian.big);
    output.setUint32(12, _h3, Endian.big);
    output.setUint32(16, _h4, Endian.big);
    return output.buffer.asUint8List();
  }
}
