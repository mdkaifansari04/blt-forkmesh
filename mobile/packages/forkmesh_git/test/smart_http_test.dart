import 'dart:convert';

import 'package:forkmesh_git/forkmesh_git.dart';
import 'package:test/test.dart';

void main() {
  test(
    'splits a smart HTTP upload-pack advertisement from v2 capabilities',
    () {
      final body = <int>[
        ...GitPktLine.data(utf8.encode('# service=git-upload-pack\n')).encode(),
        ...GitPktLine.flush().encode(),
        ...GitPktLine.data(utf8.encode('version 2\n')).encode(),
        ...GitPktLine.data(utf8.encode('ls-refs\n')).encode(),
        ...GitPktLine.flush().encode(),
      ];

      final advertisement = GitSmartHttp.parseUploadPackAdvertisement(body);

      expect(advertisement.supports('ls-refs'), isTrue);
    },
  );

  test('rejects a smart HTTP service banner for the wrong endpoint', () {
    final body = <int>[
      ...GitPktLine.data(utf8.encode('# service=git-receive-pack\n')).encode(),
      ...GitPktLine.flush().encode(),
    ];

    expect(
      () => GitSmartHttp.parseUploadPackAdvertisement(body),
      throwsA(isA<GitException>()),
    );
  });
}
