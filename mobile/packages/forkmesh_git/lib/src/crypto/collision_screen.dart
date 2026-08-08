import 'dart:convert';
import 'dart:typed_data';

import '../errors/git_error.dart';
import '../objects/git_object.dart';
import '../security/cancellation.dart';
import '../security/resource_limits.dart';
import 'sha1.dart';

enum GitSha1ObjectOrigin { trustedLocal, untrustedTransport }

enum GitSha1CollisionDetectorStatus { clear, detected, unavailable, failed }

abstract interface class GitSha1CollisionDetector {
  GitSha1CollisionDetectorSession start();
}

abstract interface class GitSha1CollisionDetectorSession {
  void add(List<int> bytes);

  GitSha1CollisionDetectorStatus close();

  void dispose();
}

enum GitSha1ScreeningDecision {
  approved,
  collisionDetected,
  malformedFrame,
  resourceLimitExceeded,
  screeningUnavailable,
  cancelled,
}

final class GitSha1ScreeningResult {
  GitSha1ScreeningResult._(
    this.decision, {
    required List<int> digest,
    required this.inspectedBytes,
  }) : digest = Uint8List.fromList(digest);

  final GitSha1ScreeningDecision decision;
  final Uint8List digest;
  final int inspectedBytes;

  bool get approved => decision == GitSha1ScreeningDecision.approved;

  GitException toException() => switch (decision) {
    GitSha1ScreeningDecision.approved => throw StateError(
      'approved screening result has no error',
    ),
    GitSha1ScreeningDecision.collisionDetected => const GitException(
      GitErrorCode.sha1CollisionDetected,
      'SHA-1 collision screening rejected the object',
    ),
    GitSha1ScreeningDecision.malformedFrame => const GitException(
      GitErrorCode.invalidObject,
      'object frame does not match screening metadata',
    ),
    GitSha1ScreeningDecision.resourceLimitExceeded => const GitException(
      GitErrorCode.resourceLimitExceeded,
      'object frame exceeds screening limit',
    ),
    GitSha1ScreeningDecision.screeningUnavailable => const GitException(
      GitErrorCode.hashScreeningUnavailable,
      'untrusted SHA-1 screening is unavailable',
    ),
    GitSha1ScreeningDecision.cancelled => const GitException(
      GitErrorCode.cancelled,
      'SHA-1 screening was cancelled',
    ),
  };
}

/// Bounded full-frame SHA-1 screening boundary.
///
/// Untrusted SHA-1 data returns
/// [GitSha1ScreeningDecision.screeningUnavailable] unless an injected detector
/// reports a clear result for the complete canonical frame.
final class Sha1CollisionScreen {
  Sha1CollisionScreen({this.detector});

  static const _screeningChunkBytes = 64 * 1024;

  final GitSha1CollisionDetector? detector;

  bool get hasGeneralizedDetector => detector != null;

  GitSha1ScreeningSession start({
    required GitObjectType objectType,
    required int declaredBodySize,
    required GitSha1ObjectOrigin origin,
    required GitResourceLimits limits,
    GitCancellationToken? cancellation,
  }) => GitSha1ScreeningSession._(
    objectType: objectType,
    declaredBodySize: declaredBodySize,
    origin: origin,
    limits: limits,
    cancellation: cancellation,
    detector: detector,
  );

  GitSha1ScreeningResult screenFrame({
    required List<int> frame,
    required GitObjectType objectType,
    required int declaredBodySize,
    required GitSha1ObjectOrigin origin,
    required GitResourceLimits limits,
    GitCancellationToken? cancellation,
  }) {
    final session = start(
      objectType: objectType,
      declaredBodySize: declaredBodySize,
      origin: origin,
      limits: limits,
      cancellation: cancellation,
    );
    for (var offset = 0; offset < frame.length;) {
      final end = (offset + _screeningChunkBytes) < frame.length
          ? offset + _screeningChunkBytes
          : frame.length;
      session.add(frame.sublist(offset, end));
      offset = end;
      if (cancellation?.isCancelled ?? false) break;
    }
    return session.close();
  }
}

final class GitSha1ScreeningSession {
  GitSha1ScreeningSession._({
    required this.objectType,
    required this.declaredBodySize,
    required this.origin,
    required this.limits,
    required this.cancellation,
    required this.detector,
  }) {
    try {
      _detectorSession = detector?.start();
    } on Object {
      _detectorFailed = true;
    }
  }

  final GitObjectType objectType;
  final int declaredBodySize;
  final GitSha1ObjectOrigin origin;
  final GitResourceLimits limits;
  final GitCancellationToken? cancellation;
  final GitSha1CollisionDetector? detector;
  final ForkMeshSha1 _digest = ForkMeshSha1();
  final BytesBuilder _frame = BytesBuilder(copy: false);
  GitSha1CollisionDetectorSession? _detectorSession;
  var _inspectedBytes = 0;
  var _limitExceeded = false;
  var _detectorFailed = false;
  var _closed = false;

  void add(List<int> bytes) {
    if (_closed) throw StateError('screening session is closed');
    if (cancellation?.isCancelled ?? false) return;
    final maximumFrameBytes = limits.maxObjectBytes + 128;
    if (bytes.length > maximumFrameBytes - _inspectedBytes) {
      _limitExceeded = true;
      return;
    }
    _digest.add(bytes);
    _frame.add(bytes);
    try {
      _detectorSession?.add(bytes);
    } on Object {
      _detectorFailed = true;
    }
    _inspectedBytes += bytes.length;
  }

  GitSha1ScreeningResult close() {
    if (_closed) throw StateError('screening session is closed');
    _closed = true;
    try {
      if (cancellation?.isCancelled ?? false) {
        return GitSha1ScreeningResult._(
          GitSha1ScreeningDecision.cancelled,
          digest: <int>[],
          inspectedBytes: _inspectedBytes,
        );
      }
      if (_limitExceeded || declaredBodySize > limits.maxObjectBytes) {
        return GitSha1ScreeningResult._(
          GitSha1ScreeningDecision.resourceLimitExceeded,
          digest: <int>[],
          inspectedBytes: _inspectedBytes,
        );
      }
      final digest = _digest.close();
      final frame = _frame.takeBytes();
      final header = ascii.encode('${objectType.name} $declaredBodySize\u0000');
      if (declaredBodySize < 0 ||
          frame.length != header.length + declaredBodySize ||
          !_sameBytes(frame, header, header.length)) {
        return GitSha1ScreeningResult._(
          GitSha1ScreeningDecision.malformedFrame,
          digest: digest,
          inspectedBytes: _inspectedBytes,
        );
      }
      final detectorStatus = _finishDetector();
      if (detectorStatus == GitSha1CollisionDetectorStatus.detected) {
        return GitSha1ScreeningResult._(
          GitSha1ScreeningDecision.collisionDetected,
          digest: digest,
          inspectedBytes: _inspectedBytes,
        );
      }
      if (origin == GitSha1ObjectOrigin.untrustedTransport &&
          detectorStatus != GitSha1CollisionDetectorStatus.clear) {
        return GitSha1ScreeningResult._(
          GitSha1ScreeningDecision.screeningUnavailable,
          digest: digest,
          inspectedBytes: _inspectedBytes,
        );
      }
      return GitSha1ScreeningResult._(
        GitSha1ScreeningDecision.approved,
        digest: digest,
        inspectedBytes: _inspectedBytes,
      );
    } finally {
      try {
        _detectorSession?.dispose();
      } on Object {
        // A failed native cleanup cannot turn a rejected object into approval.
      }
    }
  }

  GitSha1CollisionDetectorStatus _finishDetector() {
    if (_detectorFailed) return GitSha1CollisionDetectorStatus.failed;
    final session = _detectorSession;
    if (session == null) return GitSha1CollisionDetectorStatus.unavailable;
    try {
      return session.close();
    } on Object {
      return GitSha1CollisionDetectorStatus.failed;
    }
  }
}

bool _sameBytes(List<int> frame, List<int> header, int headerLength) {
  if (frame.length < headerLength) return false;
  var different = 0;
  for (var index = 0; index < headerLength; index += 1) {
    different |= frame[index] ^ header[index];
  }
  return different == 0;
}
