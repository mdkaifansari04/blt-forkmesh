import 'dart:convert';
import 'dart:typed_data';

import 'package:forkmesh_git/forkmesh_git.dart';
import 'package:test/test.dart';

void main() {
  group('checked bytes', () {
    test('rejects a declared length beyond remaining input', () {
      final cursor = CheckedBytesReader(Uint8List.fromList(<int>[0, 1, 2]));

      expect(() => cursor.readBytes(4), throwsA(isA<GitException>()));
    });

    test('writes and reads integers in network byte order', () {
      final writer = CheckedBytesWriter()..writeUint32(0x5041434b);
      final bytes = writer.takeBytes();

      expect(bytes, Uint8List.fromList(<int>[0x50, 0x41, 0x43, 0x4b]));
      expect(CheckedBytesReader(bytes).readUint32(), 0x5041434b);
    });

    test('rejects noncanonical lowercase hexadecimal', () {
      expect(() => decodeLowerHex('CE013625'), throwsA(isA<GitException>()));
      expect(() => decodeLowerHex('abc'), throwsA(isA<GitException>()));
      expect(encodeLowerHex(decodeLowerHex('ce013625')), 'ce013625');
    });
  });

  group('owned digests', () {
    test('SHA-1 and SHA-256 match empty-input FIPS vectors', () {
      expect(
        encodeLowerHex(ForkMeshSha1().close()),
        'da39a3ee5e6b4b0d3255bfef95601890afd80709',
      );
      expect(
        encodeLowerHex(ForkMeshSha256().close()),
        'e3b0c44298fc1c149afbf4c8996fb924'
        '27ae41e4649b934ca495991b7852b855',
      );
    });

    test('SHA-1 matches FIPS vectors across chunk boundaries', () {
      final digest = ForkMeshSha1();
      digest.add(utf8.encode('a'));
      digest.add(utf8.encode('bc'));

      expect(
        encodeLowerHex(digest.close()),
        'a9993e364706816aba3e25717850c26c9cd0d89d',
      );
    });

    test('SHA-256 matches FIPS vectors across chunk boundaries', () {
      final digest = ForkMeshSha256();
      digest.add(utf8.encode('a'));
      digest.add(utf8.encode('bc'));

      expect(
        encodeLowerHex(digest.close()),
        'ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad',
      );
    });

    test('match multi-block FIPS vectors', () {
      final input = utf8.encode(
        'abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq',
      );

      expect(
        encodeLowerHex((ForkMeshSha1()..add(input)).close()),
        '84983e441c3bd26ebaae4aa1f95129e5e54670f1',
      );
      expect(
        encodeLowerHex((ForkMeshSha256()..add(input)).close()),
        '248d6a61d20638b8e5c026930c3e6039'
        'a33ce45964ff2167f6ecedd419db06c1',
      );
    });

    test('handle one million bytes across boundary-sized chunks', () {
      final sha1 = ForkMeshSha1();
      final sha256 = ForkMeshSha256();
      const chunkSizes = <int>[1, 55, 56, 63, 64, 65, 17];
      var written = 0;
      var chunk = 0;
      while (written < 1000000) {
        final candidate = chunkSizes[chunk % chunkSizes.length];
        final remaining = 1000000 - written;
        final length = candidate < remaining ? candidate : remaining;
        final bytes = List<int>.filled(length, 0x61);
        sha1.add(bytes);
        sha256.add(bytes);
        written += length;
        chunk += 1;
      }

      expect(
        encodeLowerHex(sha1.close()),
        '34aa973cd4c4daa4f61eeb2bdbad27316534016f',
      );
      expect(
        encodeLowerHex(sha256.close()),
        'cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0',
      );
    });

    test('reject input beyond configured length accounting', () {
      expect(
        () => ForkMeshSha1(maxInputBytes: 2).add(<int>[1, 2, 3]),
        throwsA(
          isA<GitException>().having(
            (error) => error.code,
            'code',
            GitErrorCode.hashImplementationFailure,
          ),
        ),
      );
      expect(
        () => ForkMeshSha256(maxInputBytes: 2).add(<int>[1, 2, 3]),
        throwsA(
          isA<GitException>().having(
            (error) => error.code,
            'code',
            GitErrorCode.hashImplementationFailure,
          ),
        ),
      );
    });

    test('collision screen keeps untrusted SHA-1 transport fail closed', () {
      final screen = Sha1CollisionScreen();
      final frame = GitObjectFrame.encode(GitObjectType.blob, <int>[1]);

      expect(
        screen
            .screenFrame(
              frame: frame,
              objectType: GitObjectType.blob,
              declaredBodySize: 1,
              origin: GitSha1ObjectOrigin.untrustedTransport,
              limits: GitResourceLimits(),
            )
            .decision,
        GitSha1ScreeningDecision.screeningUnavailable,
      );
    });
  });
}
