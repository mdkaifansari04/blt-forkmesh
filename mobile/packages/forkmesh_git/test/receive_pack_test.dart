import 'dart:convert';

import 'package:forkmesh_git/forkmesh_git.dart';
import 'package:test/test.dart';

GitObjectId _id(int byte) =>
    GitObjectId.parseSha1(byte.toRadixString(16).padLeft(2, '0') * 20);

void main() {
  test('builds compare-and-swap receive-pack commands without force', () {
    final commands = GitReceivePack.commandPackets(<GitPushRefUpdate>[
      GitPushRefUpdate(
        refName: 'refs/heads/main',
        expectedOld: _id(1),
        next: _id(2),
      ),
    ]);

    expect(
      utf8.decode(commands.first.payload),
      '${_id(1).hex} ${_id(2).hex} refs/heads/main\u0000report-status-v2 side-band-64k\n',
    );
    expect(commands.last.kind, GitPktLineKind.flush);
  });

  test('parses per-ref rejected status as typed non-fast-forward', () {
    final status = GitReceivePack.parseReportStatus(<GitPktLine>[
      GitPktLine.data(utf8.encode('unpack ok\n')),
      GitPktLine.data(utf8.encode('ng refs/heads/main non-fast-forward\n')),
      GitPktLine.flush(),
    ]);

    expect(status.unpackOk, isTrue);
    expect(status.refs.single.code, GitErrorCode.nonFastForward);
  });
}
