import 'dart:convert';

import 'package:forkmesh_git/forkmesh_git.dart';
import 'package:test/test.dart';

void main() {
  test('an injected clear detector permits a canonical untrusted frame', () {
    final frame = GitObjectFrame.encode(GitObjectType.blob, <int>[1]);
    final result = Sha1CollisionScreen(detector: _FakeDetector.clear())
        .screenFrame(
          frame: frame,
          objectType: GitObjectType.blob,
          declaredBodySize: 1,
          origin: GitSha1ObjectOrigin.untrustedTransport,
          limits: GitResourceLimits(),
        );

    expect(result.decision, GitSha1ScreeningDecision.approved);
  });

  test(
    'screens complete Git frame bytes with typed trusted and untrusted policy',
    () {
      final frame = GitObjectFrame.encode(
        GitObjectType.blob,
        utf8.encode('hello\n'),
      );
      final screen = Sha1CollisionScreen();

      final trusted = screen.screenFrame(
        frame: frame,
        objectType: GitObjectType.blob,
        declaredBodySize: 6,
        origin: GitSha1ObjectOrigin.trustedLocal,
        limits: GitResourceLimits(),
      );
      final untrusted = screen.screenFrame(
        frame: frame,
        objectType: GitObjectType.blob,
        declaredBodySize: 6,
        origin: GitSha1ObjectOrigin.untrustedTransport,
        limits: GitResourceLimits(),
      );

      expect(trusted.decision, GitSha1ScreeningDecision.approved);
      expect(trusted.digest, GitObjectId.sha1Of(frame).bytes);
      expect(trusted.inspectedBytes, frame.length);
      expect(untrusted.decision, GitSha1ScreeningDecision.screeningUnavailable);
    },
  );

  test('reports malformed frames and cancellation as distinct decisions', () {
    final cancellation = GitCancellationToken()..cancel();
    final screen = Sha1CollisionScreen();

    final malformed = screen.screenFrame(
      frame: utf8.encode('blob 7\u0000hello\n'),
      objectType: GitObjectType.blob,
      declaredBodySize: 7,
      origin: GitSha1ObjectOrigin.trustedLocal,
      limits: GitResourceLimits(),
    );
    final cancelled = screen.screenFrame(
      frame: GitObjectFrame.encode(GitObjectType.blob, <int>[1]),
      objectType: GitObjectType.blob,
      declaredBodySize: 1,
      origin: GitSha1ObjectOrigin.trustedLocal,
      limits: GitResourceLimits(),
      cancellation: cancellation,
    );

    expect(malformed.decision, GitSha1ScreeningDecision.malformedFrame);
    expect(cancelled.decision, GitSha1ScreeningDecision.cancelled);
  });

  test('cancellation between streaming chunks prevents approval', () {
    final frame = GitObjectFrame.encode(GitObjectType.blob, <int>[1, 2]);
    final cancellation = GitCancellationToken();
    final session = Sha1CollisionScreen().start(
      objectType: GitObjectType.blob,
      declaredBodySize: 2,
      origin: GitSha1ObjectOrigin.trustedLocal,
      limits: GitResourceLimits(),
      cancellation: cancellation,
    );

    session.add(frame.sublist(0, 4));
    cancellation.cancel();
    session.add(frame.sublist(4));

    expect(session.close().decision, GitSha1ScreeningDecision.cancelled);
  });

  test('declared bodies beyond the object limit do not receive approval', () {
    final frame = GitObjectFrame.encode(GitObjectType.blob, <int>[1, 2]);

    final result = Sha1CollisionScreen().screenFrame(
      frame: frame,
      objectType: GitObjectType.blob,
      declaredBodySize: 2,
      origin: GitSha1ObjectOrigin.trustedLocal,
      limits: GitResourceLimits(maxObjectBytes: 1),
    );

    expect(result.decision, GitSha1ScreeningDecision.resourceLimitExceeded);
  });

  test(
    'preserves the full-frame decision across irregular streaming chunks',
    () {
      final body = utf8.encode(
        'tree 0000000000000000000000000000000000000000\n\nmessage\n',
      );
      final frame = GitObjectFrame.encode(GitObjectType.commit, body);
      final session = Sha1CollisionScreen().start(
        objectType: GitObjectType.commit,
        declaredBodySize: body.length,
        origin: GitSha1ObjectOrigin.trustedLocal,
        limits: GitResourceLimits(),
      );
      for (var offset = 0; offset < frame.length;) {
        final length = (frame.length - offset) < 5 ? frame.length - offset : 5;
        session.add(frame.sublist(offset, offset + length));
        offset += length;
      }

      final result = session.close();

      expect(result.decision, GitSha1ScreeningDecision.approved);
      expect(result.digest, GitObjectId.sha1Of(frame).bytes);
    },
  );
}

final class _FakeDetector implements GitSha1CollisionDetector {
  _FakeDetector.clear();

  @override
  GitSha1CollisionDetectorSession start() => _FakeDetectorSession();
}

final class _FakeDetectorSession implements GitSha1CollisionDetectorSession {
  @override
  void add(List<int> bytes) {}

  @override
  GitSha1CollisionDetectorStatus close() =>
      GitSha1CollisionDetectorStatus.clear;

  @override
  void dispose() {}
}
