import 'dart:convert';

import 'package:forkmesh_git/forkmesh_git.dart';
import 'package:test/test.dart';

void main() {
  test('encodes data and decodes data plus control pkt-lines', () {
    final bytes = <int>[
      ...GitPktLine.data(utf8.encode('hello\n')).encode(),
      ...GitPktLine.flush().encode(),
      ...GitPktLine.delimiter().encode(),
      ...GitPktLine.responseEnd().encode(),
    ];

    expect(utf8.decode(bytes.sublist(0, 10)), '000ahello\n');
    expect(
      GitPktLine.decodeAll(bytes).map((packet) => packet.kind),
      <GitPktLineKind>[
        GitPktLineKind.data,
        GitPktLineKind.flush,
        GitPktLineKind.delimiter,
        GitPktLineKind.responseEnd,
      ],
    );
  });

  test('rejects truncated and oversized pkt-lines before allocation', () {
    expect(
      () => GitPktLine.decodeAll(utf8.encode('000aabc')),
      throwsA(isA<GitException>()),
    );
    expect(
      () => GitPktLine.data(List<int>.filled(0xfff0, 1)).encode(),
      throwsA(isA<GitException>()),
    );
  });
}
