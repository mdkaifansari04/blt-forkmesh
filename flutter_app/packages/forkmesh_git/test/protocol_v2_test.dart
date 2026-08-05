import 'dart:convert';

import 'package:forkmesh_git/forkmesh_git.dart';
import 'package:test/test.dart';

GitObjectId get _id =>
    GitObjectId.parseSha1('ce013625030ba8dba906f756967f9e9ca394464a');

void main() {
  test('parses v2 capabilities and builds an ls-refs request', () {
    final advertisement = GitProtocolV2Advertisement.parse(<GitPktLine>[
      GitPktLine.data(utf8.encode('version 2\n')),
      GitPktLine.data(utf8.encode('agent=forkmesh-test\n')),
      GitPktLine.data(utf8.encode('ls-refs=unborn\n')),
      GitPktLine.data(utf8.encode('fetch=shallow wait-for-done\n')),
      GitPktLine.flush(),
    ]);

    expect(advertisement.supports('ls-refs'), isTrue);
    expect(advertisement.supports('fetch'), isTrue);
    expect(
      utf8.decode(GitProtocolV2.lsRefsRequest().first.payload),
      'command=ls-refs\n',
    );
  });

  test('parses typed ls-refs records and rejects malformed attributes', () {
    final refs = GitProtocolV2.parseLsRefs(<GitPktLine>[
      GitPktLine.data(
        utf8.encode('${_id.hex} HEAD symref-target:refs/heads/main\n'),
      ),
      GitPktLine.data(utf8.encode('${_id.hex} refs/heads/main\n')),
      GitPktLine.flush(),
    ]);

    expect(refs, hasLength(2));
    expect(refs.first.symrefTarget, 'refs/heads/main');
    expect(
      () => GitProtocolV2.parseLsRefs(<GitPktLine>[
        GitPktLine.data(
          utf8.encode('${_id.hex} refs/heads/main unknown:value\n'),
        ),
        GitPktLine.flush(),
      ]),
      throwsA(isA<GitException>()),
    );
  });
}
