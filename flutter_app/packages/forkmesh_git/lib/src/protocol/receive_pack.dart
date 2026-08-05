import 'dart:convert';

import '../errors/git_error.dart';
import '../objects/git_object.dart';
import '../refs/ref_store.dart';
import 'pkt_line.dart';

final class GitPushRefUpdate {
  GitPushRefUpdate({
    required this.refName,
    required this.expectedOld,
    required this.next,
  }) {
    GitRefName.parse(refName);
  }

  final String refName;
  final GitObjectId expectedOld;
  final GitObjectId next;
}

final class GitPushRefStatus {
  const GitPushRefStatus(this.refName, this.code, {this.message});

  final String refName;
  final GitErrorCode? code;
  final String? message;

  bool get accepted => code == null;
}

final class GitReceivePackStatus {
  const GitReceivePackStatus(this.unpackOk, this.refs);

  final bool unpackOk;
  final List<GitPushRefStatus> refs;
}

final class GitReceivePack {
  static List<GitPktLine> commandPackets(Iterable<GitPushRefUpdate> updates) {
    final values = List<GitPushRefUpdate>.of(updates);
    if (values.isEmpty || values.length > 100000) {
      throw const GitException(
        GitErrorCode.invalidRef,
        'push must contain a bounded non-empty set of updates',
      );
    }
    final names = <String>{};
    final packets = <GitPktLine>[];
    for (var index = 0; index < values.length; index += 1) {
      final update = values[index];
      if (!names.add(update.refName)) {
        throw const GitException(GitErrorCode.invalidRef, 'duplicate push ref');
      }
      final capabilities = index == 0
          ? '\u0000report-status-v2 side-band-64k'
          : '';
      packets.add(
        GitPktLine.data(
          ascii.encode(
            '${update.expectedOld.hex} ${update.next.hex} '
            '${update.refName}$capabilities\n',
          ),
        ),
      );
    }
    packets.add(GitPktLine.flush());
    return List<GitPktLine>.unmodifiable(packets);
  }

  static GitReceivePackStatus parseReportStatus(List<GitPktLine> packets) {
    if (packets.length < 2 || packets.last.kind != GitPktLineKind.flush) {
      throw const GitException(
        GitErrorCode.protocolUnsupported,
        'receive-pack status framing is invalid',
      );
    }
    final lines = <String>[];
    for (final packet in packets.take(packets.length - 1)) {
      if (packet.kind != GitPktLineKind.data) {
        throw const GitException(
          GitErrorCode.protocolUnsupported,
          'receive-pack status has unexpected control packet',
        );
      }
      lines.add(_line(packet.payload));
    }
    final unpackOk = lines.first == 'unpack ok';
    if (!unpackOk && !lines.first.startsWith('unpack ')) {
      throw const GitException(
        GitErrorCode.protocolUnsupported,
        'receive-pack unpack status is invalid',
      );
    }
    final refs = <GitPushRefStatus>[];
    for (final line in lines.skip(1)) {
      if (line.startsWith('ok ')) {
        final ref = line.substring(3);
        GitRefName.parse(ref);
        refs.add(GitPushRefStatus(ref, null));
        continue;
      }
      if (!line.startsWith('ng ')) {
        throw const GitException(
          GitErrorCode.protocolUnsupported,
          'receive-pack ref status is invalid',
        );
      }
      final separator = line.indexOf(' ', 3);
      if (separator == -1 || separator == line.length - 1) {
        throw const GitException(
          GitErrorCode.protocolUnsupported,
          'receive-pack rejected ref status is invalid',
        );
      }
      final ref = line.substring(3, separator);
      final message = line.substring(separator + 1);
      GitRefName.parse(ref);
      refs.add(
        GitPushRefStatus(
          ref,
          message.toLowerCase().contains('non-fast-forward')
              ? GitErrorCode.nonFastForward
              : GitErrorCode.remoteRejected,
          message: message,
        ),
      );
    }
    return GitReceivePackStatus(
      unpackOk,
      List<GitPushRefStatus>.unmodifiable(refs),
    );
  }
}

String _line(List<int> bytes) {
  String value;
  try {
    value = ascii.decode(bytes);
  } on FormatException {
    throw const GitException(
      GitErrorCode.protocolUnsupported,
      'receive-pack status is not ASCII',
    );
  }
  if (!value.endsWith('\n') || value.contains('\r') || value.length == 1) {
    throw const GitException(
      GitErrorCode.protocolUnsupported,
      'receive-pack status is malformed',
    );
  }
  return value.substring(0, value.length - 1);
}
