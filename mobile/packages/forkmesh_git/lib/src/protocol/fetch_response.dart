import 'dart:convert';
import 'dart:typed_data';

import '../crypto/collision_screen.dart';
import '../errors/git_error.dart';
import '../pack/pack_reader.dart';
import '../security/resource_limits.dart';
import 'pkt_line.dart';

final class GitFetchResponse {
  GitFetchResponse._(List<int> packBytes)
    : packBytes = Uint8List.fromList(packBytes);

  final Uint8List packBytes;

  static GitFetchResponse parse(List<GitPktLine> packets) {
    if (packets.length < 2 ||
        packets.first.kind != GitPktLineKind.data ||
        !_isPackfileMarker(packets.first.payload) ||
        packets.last.kind != GitPktLineKind.flush) {
      throw const GitException(
        GitErrorCode.protocolUnsupported,
        'fetch response framing is invalid',
      );
    }
    final output = BytesBuilder(copy: false);
    for (final packet in packets.sublist(1, packets.length - 1)) {
      if (packet.kind != GitPktLineKind.data || packet.payload.isEmpty) {
        throw const GitException(
          GitErrorCode.protocolUnsupported,
          'fetch sideband packet is invalid',
        );
      }
      switch (packet.payload.first) {
        case 1:
          output.add(packet.payload.sublist(1));
        case 2:
          continue;
        case 3:
          throw const GitException(
            GitErrorCode.remoteRejected,
            'remote rejected fetch request',
          );
        default:
          throw const GitException(
            GitErrorCode.protocolUnsupported,
            'fetch sideband channel is invalid',
          );
      }
    }
    return GitFetchResponse._(output.takeBytes());
  }

  static void validateUntrustedPack(
    List<int> bytes,
    GitResourceLimits limits, {
    GitSha1CollisionDetector? detector,
  }) {
    GitPackReader.parse(
      bytes,
      limits,
      origin: GitSha1ObjectOrigin.untrustedTransport,
      detector: detector,
    );
  }

  static bool _isPackfileMarker(List<int> bytes) {
    try {
      return ascii.decode(bytes) == 'packfile\n';
    } on FormatException {
      return false;
    }
  }
}
