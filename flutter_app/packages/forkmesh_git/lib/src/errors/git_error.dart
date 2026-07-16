enum GitErrorCode {
  invalidRepository,
  unsupportedRepositoryFormat,
  invalidObject,
  objectHashMismatch,
  invalidTree,
  invalidRef,
  refLocked,
  indexCorrupt,
  packCorrupt,
  packChecksumMismatch,
  deltaDepthExceeded,
  resourceLimitExceeded,
  pathEscapesWorktree,
  symlinkEscape,
  networkUnavailable,
  tlsRejected,
  authenticationFailed,
  protocolUnsupported,
  remoteRejected,
  nonFastForward,
  cancelled,
  operationInterrupted,
  sha1CollisionDetected,
  hashScreeningUnavailable,
  hashImplementationFailure,
  quarantineRejected;

  String get wireName => name;
}

final class GitException implements Exception {
  const GitException(this.code, this.message);

  final GitErrorCode code;
  final String message;

  @override
  String toString() =>
      'GitException(code: ${code.wireName}, message: $message)';
}
