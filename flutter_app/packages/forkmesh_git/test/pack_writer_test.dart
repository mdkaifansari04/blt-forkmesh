import 'dart:convert';

import 'package:forkmesh_git/forkmesh_git.dart';
import 'package:test/test.dart';

void main() {
  test(
    'writes a non-delta pack and matching index that ForkMesh can reopen',
    () {
      final written = GitPackWriter.write(<GitPackWriteObject>[
        GitPackWriteObject(GitObjectType.blob, utf8.encode('hello\n')),
        GitPackWriteObject(GitObjectType.blob, <int>[0, 1, 0xff]),
      ]);

      final pack = GitPackReader.parse(written.packBytes, GitResourceLimits());
      final index = GitPackIndexV2.parse(
        written.indexBytes,
        GitResourceLimits(),
      );

      expect(pack.objects, hasLength(2));
      for (final object in pack.objects) {
        expect(index.lookup(object.objectId), isNotNull);
      }
    },
  );
}
