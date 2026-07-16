import 'dart:typed_data';

import '../errors/git_error.dart';

enum GitPktLineKind { data, flush, delimiter, responseEnd }

final class GitPktLine {
  GitPktLine._(this.kind, List<int> payload)
    : payload = Uint8List.fromList(payload);

  final GitPktLineKind kind;
  final Uint8List payload;

  factory GitPktLine.data(List<int> payload) =>
      GitPktLine._(GitPktLineKind.data, payload);

  factory GitPktLine.flush() => GitPktLine._(GitPktLineKind.flush, <int>[]);

  factory GitPktLine.delimiter() =>
      GitPktLine._(GitPktLineKind.delimiter, <int>[]);

  factory GitPktLine.responseEnd() =>
      GitPktLine._(GitPktLineKind.responseEnd, <int>[]);

  Uint8List encode() {
    final length = switch (kind) {
      GitPktLineKind.flush => 0,
      GitPktLineKind.delimiter => 1,
      GitPktLineKind.responseEnd => 2,
      GitPktLineKind.data => payload.length + 4,
    };
    if (length > 0xfff0) {
      throw const GitException(
        GitErrorCode.resourceLimitExceeded,
        'pkt-line exceeds hard length limit',
      );
    }
    final prefix = length.toRadixString(16).padLeft(4, '0');
    return Uint8List.fromList(<int>[
      prefix.codeUnitAt(0),
      prefix.codeUnitAt(1),
      prefix.codeUnitAt(2),
      prefix.codeUnitAt(3),
      ...payload,
    ]);
  }

  static List<GitPktLine> decodeAll(
    List<int> input, {
    int maximumPackets = 50000,
  }) {
    if (maximumPackets <= 0 || maximumPackets > 200000) {
      throw ArgumentError.value(maximumPackets, 'maximumPackets');
    }
    final bytes = Uint8List.fromList(input);
    final packets = <GitPktLine>[];
    var offset = 0;
    while (offset < bytes.length) {
      if (bytes.length - offset < 4 || packets.length >= maximumPackets) {
        throw const GitException(
          GitErrorCode.resourceLimitExceeded,
          'pkt-line stream is truncated or exceeds packet limit',
        );
      }
      final length = _parseLength(bytes, offset);
      offset += 4;
      switch (length) {
        case 0:
          packets.add(GitPktLine.flush());
        case 1:
          packets.add(GitPktLine.delimiter());
        case 2:
          packets.add(GitPktLine.responseEnd());
        case 3:
          throw const GitException(
            GitErrorCode.protocolUnsupported,
            'pkt-line length 3 is invalid',
          );
        default:
          if (length > 0xfff0 || length - 4 > bytes.length - offset) {
            throw const GitException(
              GitErrorCode.protocolUnsupported,
              'pkt-line payload is truncated or oversized',
            );
          }
          packets.add(
            GitPktLine.data(bytes.sublist(offset, offset + length - 4)),
          );
          offset += length - 4;
      }
    }
    return List<GitPktLine>.unmodifiable(packets);
  }
}

int _parseLength(Uint8List bytes, int offset) {
  var length = 0;
  for (var index = 0; index < 4; index += 1) {
    final byte = bytes[offset + index];
    final value = byte >= 0x30 && byte <= 0x39
        ? byte - 0x30
        : byte >= 0x61 && byte <= 0x66
        ? byte - 0x61 + 10
        : byte >= 0x41 && byte <= 0x46
        ? byte - 0x41 + 10
        : -1;
    if (value < 0) {
      throw const GitException(
        GitErrorCode.protocolUnsupported,
        'pkt-line prefix is not hexadecimal',
      );
    }
    length = length * 16 + value;
  }
  return length;
}
