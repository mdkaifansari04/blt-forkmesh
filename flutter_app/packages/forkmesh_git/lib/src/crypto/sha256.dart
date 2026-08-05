import 'dart:typed_data';

import '../errors/git_error.dart';
import 'digest.dart';

const _roundConstants = <int>[
  0x428a2f98,
  0x71374491,
  0xb5c0fbcf,
  0xe9b5dba5,
  0x3956c25b,
  0x59f111f1,
  0x923f82a4,
  0xab1c5ed5,
  0xd807aa98,
  0x12835b01,
  0x243185be,
  0x550c7dc3,
  0x72be5d74,
  0x80deb1fe,
  0x9bdc06a7,
  0xc19bf174,
  0xe49b69c1,
  0xefbe4786,
  0x0fc19dc6,
  0x240ca1cc,
  0x2de92c6f,
  0x4a7484aa,
  0x5cb0a9dc,
  0x76f988da,
  0x983e5152,
  0xa831c66d,
  0xb00327c8,
  0xbf597fc7,
  0xc6e00bf3,
  0xd5a79147,
  0x06ca6351,
  0x14292967,
  0x27b70a85,
  0x2e1b2138,
  0x4d2c6dfc,
  0x53380d13,
  0x650a7354,
  0x766a0abb,
  0x81c2c92e,
  0x92722c85,
  0xa2bfe8a1,
  0xa81a664b,
  0xc24b8b70,
  0xc76c51a3,
  0xd192e819,
  0xd6990624,
  0xf40e3585,
  0x106aa070,
  0x19a4c116,
  0x1e376c08,
  0x2748774c,
  0x34b0bcb5,
  0x391c0cb3,
  0x4ed8aa4a,
  0x5b9cca4f,
  0x682e6ff3,
  0x748f82ee,
  0x78a5636f,
  0x84c87814,
  0x8cc70208,
  0x90befffa,
  0xa4506ceb,
  0xbef9a3f7,
  0xc67178f2,
];

final class ForkMeshSha256 implements GitDigest {
  ForkMeshSha256({int maxInputBytes = maximumInputBytes})
    : _maxInputBytes = maxInputBytes {
    if (maxInputBytes < 0 || maxInputBytes > maximumInputBytes) {
      throw ArgumentError.value(maxInputBytes, 'maxInputBytes');
    }
  }

  static const maximumInputBytes = 0x1fffffffffffffff;

  final Uint32List _state = Uint32List.fromList(<int>[
    0x6a09e667,
    0xbb67ae85,
    0x3c6ef372,
    0xa54ff53a,
    0x510e527f,
    0x9b05688c,
    0x1f83d9ab,
    0x5be0cd19,
  ]);
  final BytesBuilder _pending = BytesBuilder(copy: false);
  final int _maxInputBytes;
  var _lengthBytes = 0;
  var _closed = false;

  @override
  void add(List<int> bytes) {
    if (_closed) throw StateError('digest is closed');
    if (bytes.length > _maxInputBytes - _lengthBytes) {
      throw const GitException(
        GitErrorCode.hashImplementationFailure,
        'SHA-256 input length exceeds supported accounting range',
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
    final words = Uint32List(64);
    for (var index = 0; index < 16; index += 1) {
      final offset = index * 4;
      words[index] =
          (block[offset] << 24) |
          (block[offset + 1] << 16) |
          (block[offset + 2] << 8) |
          block[offset + 3];
    }
    for (var index = 16; index < 64; index += 1) {
      final sigma0 =
          rotateRight32(words[index - 15], 7) ^
          rotateRight32(words[index - 15], 18) ^
          (words[index - 15] >>> 3);
      final sigma1 =
          rotateRight32(words[index - 2], 17) ^
          rotateRight32(words[index - 2], 19) ^
          (words[index - 2] >>> 10);
      words[index] =
          (words[index - 16] + sigma0 + words[index - 7] + sigma1) & 0xffffffff;
    }
    var a = _state[0];
    var b = _state[1];
    var c = _state[2];
    var d = _state[3];
    var e = _state[4];
    var f = _state[5];
    var g = _state[6];
    var h = _state[7];
    for (var index = 0; index < 64; index += 1) {
      final sum1 =
          rotateRight32(e, 6) ^ rotateRight32(e, 11) ^ rotateRight32(e, 25);
      final choice = (e & f) ^ (~e & g);
      final temp1 =
          (h + sum1 + choice + _roundConstants[index] + words[index]) &
          0xffffffff;
      final sum0 =
          rotateRight32(a, 2) ^ rotateRight32(a, 13) ^ rotateRight32(a, 22);
      final majority = (a & b) ^ (a & c) ^ (b & c);
      final temp2 = (sum0 + majority) & 0xffffffff;
      h = g;
      g = f;
      f = e;
      e = (d + temp1) & 0xffffffff;
      d = c;
      c = b;
      b = a;
      a = (temp1 + temp2) & 0xffffffff;
    }
    _state[0] = (_state[0] + a) & 0xffffffff;
    _state[1] = (_state[1] + b) & 0xffffffff;
    _state[2] = (_state[2] + c) & 0xffffffff;
    _state[3] = (_state[3] + d) & 0xffffffff;
    _state[4] = (_state[4] + e) & 0xffffffff;
    _state[5] = (_state[5] + f) & 0xffffffff;
    _state[6] = (_state[6] + g) & 0xffffffff;
    _state[7] = (_state[7] + h) & 0xffffffff;
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
    final output = ByteData(32);
    for (var index = 0; index < _state.length; index += 1) {
      output.setUint32(index * 4, _state[index], Endian.big);
    }
    return output.buffer.asUint8List();
  }
}
