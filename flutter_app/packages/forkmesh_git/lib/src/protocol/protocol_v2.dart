import 'dart:convert';

import '../errors/git_error.dart';
import '../objects/git_object.dart';
import '../refs/ref_store.dart';
import 'pkt_line.dart';

final class GitProtocolV2Advertisement {
  GitProtocolV2Advertisement._(this._capabilities);

  final Map<String, String?> _capabilities;

  bool supports(String command) => _capabilities.containsKey(command);

  String? valueOf(String capability) => _capabilities[capability];

  static GitProtocolV2Advertisement parse(List<GitPktLine> packets) {
    if (packets.isEmpty || packets.last.kind != GitPktLineKind.flush) {
      throw const GitException(
        GitErrorCode.protocolUnsupported,
        'protocol v2 advertisement is not flush terminated',
      );
    }
    final lines = <String>[];
    for (final packet in packets.take(packets.length - 1)) {
      if (packet.kind != GitPktLineKind.data) {
        throw const GitException(
          GitErrorCode.protocolUnsupported,
          'protocol v2 advertisement has unexpected control packet',
        );
      }
      lines.add(_asciiLine(packet.payload));
    }
    if (lines.isEmpty || lines.first != 'version 2') {
      throw const GitException(
        GitErrorCode.protocolUnsupported,
        'protocol v2 version marker is missing',
      );
    }
    final capabilities = <String, String?>{};
    for (final line in lines.skip(1)) {
      final separator = line.indexOf('=');
      final name = separator == -1 ? line : line.substring(0, separator);
      if (!RegExp(r'^[a-z][a-z0-9-]*$').hasMatch(name) ||
          capabilities.containsKey(name)) {
        throw const GitException(
          GitErrorCode.protocolUnsupported,
          'protocol v2 capability is invalid',
        );
      }
      capabilities[name] = separator == -1
          ? null
          : line.substring(separator + 1);
    }
    return GitProtocolV2Advertisement._(
      Map<String, String?>.unmodifiable(capabilities),
    );
  }
}

final class GitRemoteRef {
  const GitRemoteRef({
    required this.name,
    required this.objectId,
    this.peeled,
    this.symrefTarget,
  });

  final String name;
  final GitObjectId objectId;
  final GitObjectId? peeled;
  final String? symrefTarget;
}

final class GitProtocolV2 {
  static List<GitPktLine> lsRefsRequest() => <GitPktLine>[
    GitPktLine.data(ascii.encode('command=ls-refs\n')),
    GitPktLine.delimiter(),
    GitPktLine.data(ascii.encode('peel\n')),
    GitPktLine.data(ascii.encode('symrefs\n')),
    GitPktLine.flush(),
  ];

  static List<GitRemoteRef> parseLsRefs(List<GitPktLine> packets) {
    if (packets.isEmpty || packets.last.kind != GitPktLineKind.flush) {
      throw const GitException(
        GitErrorCode.protocolUnsupported,
        'ls-refs response is not flush terminated',
      );
    }
    final refs = <GitRemoteRef>[];
    for (final packet in packets.take(packets.length - 1)) {
      if (packet.kind != GitPktLineKind.data) {
        throw const GitException(
          GitErrorCode.protocolUnsupported,
          'ls-refs response has unexpected control packet',
        );
      }
      final parts = _asciiLine(packet.payload).split(' ');
      if (parts.length < 2 || parts.any((part) => part.isEmpty)) {
        throw const GitException(
          GitErrorCode.protocolUnsupported,
          'ls-refs record is invalid',
        );
      }
      final name = parts[1];
      if (name != 'HEAD') GitRefName.parse(name);
      GitObjectId? peeled;
      String? symrefTarget;
      for (final attribute in parts.skip(2)) {
        if (attribute.startsWith('peeled:') && peeled == null) {
          peeled = GitObjectId.parseSha1(attribute.substring(7));
        } else if (attribute.startsWith('symref-target:') &&
            symrefTarget == null) {
          symrefTarget = attribute.substring(14);
          GitRefName.branch(symrefTarget);
        } else {
          throw const GitException(
            GitErrorCode.protocolUnsupported,
            'ls-refs attribute is unsupported',
          );
        }
      }
      refs.add(
        GitRemoteRef(
          name: name,
          objectId: GitObjectId.parseSha1(parts.first),
          peeled: peeled,
          symrefTarget: symrefTarget,
        ),
      );
    }
    return List<GitRemoteRef>.unmodifiable(refs);
  }
}

String _asciiLine(List<int> bytes) {
  String line;
  try {
    line = ascii.decode(bytes);
  } on FormatException {
    throw const GitException(
      GitErrorCode.protocolUnsupported,
      'protocol field is not ASCII',
    );
  }
  if (!line.endsWith('\n') || line.contains('\r') || line.length == 1) {
    throw const GitException(
      GitErrorCode.protocolUnsupported,
      'protocol field is not newline terminated',
    );
  }
  return line.substring(0, line.length - 1);
}
