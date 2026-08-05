import 'dart:convert';

import '../errors/git_error.dart';
import 'pkt_line.dart';
import 'protocol_v2.dart';

final class GitSmartHttp {
  static GitProtocolV2Advertisement parseUploadPackAdvertisement(
    List<int> body,
  ) {
    final packets = GitPktLine.decodeAll(body);
    if (packets.length < 3 ||
        packets.first.kind != GitPktLineKind.data ||
        !_matchesService(packets.first.payload, 'git-upload-pack') ||
        packets[1].kind != GitPktLineKind.flush) {
      throw const GitException(
        GitErrorCode.protocolUnsupported,
        'upload-pack advertisement banner is invalid',
      );
    }
    return GitProtocolV2Advertisement.parse(packets.sublist(2));
  }

  static bool _matchesService(List<int> payload, String service) {
    try {
      return ascii.decode(payload) == '# service=$service\n';
    } on FormatException {
      return false;
    }
  }
}
