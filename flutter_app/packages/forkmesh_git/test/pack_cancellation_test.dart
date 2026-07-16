import 'package:forkmesh_git/forkmesh_git.dart';
import 'package:test/test.dart';

void main() {
  test('refuses to return a pack when cancellation was requested', () {
    final cancellation = GitCancellationToken()..cancel();

    expect(
      () => GitPackWriter.write(<GitPackWriteObject>[
        GitPackWriteObject(GitObjectType.blob, <int>[1]),
      ], cancellation: cancellation),
      throwsA(
        isA<GitException>().having(
          (error) => error.code,
          'code',
          GitErrorCode.cancelled,
        ),
      ),
    );
  });
}
