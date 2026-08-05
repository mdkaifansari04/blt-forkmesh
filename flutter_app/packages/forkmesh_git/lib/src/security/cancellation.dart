import '../errors/git_error.dart';

final class GitCancellationToken {
  var _cancelled = false;

  bool get isCancelled => _cancelled;

  void cancel() => _cancelled = true;

  void throwIfCancelled() {
    if (_cancelled) {
      throw const GitException(
        GitErrorCode.cancelled,
        'operation was cancelled',
      );
    }
  }
}
