import 'package:forkmesh_git/forkmesh_git.dart';
import 'package:test/test.dart';

void main() {
  test('accepts only credential-free HTTPS Git remote URLs', () {
    final remote = GitRemoteUrl.parse('https://example.test/group/repo.git');

    expect(remote.uri.scheme, 'https');
    expect(remote.redactedOrigin, 'https://example.test');
  });

  test('rejects non-HTTPS schemes URL credentials and fragments', () {
    for (final value in <String>[
      'http://example.test/repo.git',
      'file:///tmp/repo.git',
      'ssh://example.test/repo.git',
      'https://user:secret@example.test/repo.git',
      'https://example.test/repo.git#fragment',
    ]) {
      expect(() => GitRemoteUrl.parse(value), throwsA(isA<GitException>()));
    }
  });
}
