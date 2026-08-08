import 'package:forkmesh_git/forkmesh_git.dart';
import 'package:test/test.dart';

void main() {
  test('exposes stable typed error codes and immutable hard caps', () {
    expect(GitErrorCode.invalidRepository.wireName, 'invalidRepository');
    expect(GitErrorCode.cancelled.wireName, 'cancelled');
    expect(
      () => GitResourceLimits(
        maxObjectBytes: GitResourceLimits.hardMaxObjectBytes + 1,
      ),
      throwsA(isA<ArgumentError>()),
    );
  });
}
