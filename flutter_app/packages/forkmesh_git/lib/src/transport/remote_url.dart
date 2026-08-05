import '../errors/git_error.dart';

final class GitRemoteUrl {
  GitRemoteUrl._(this.uri);

  final Uri uri;

  factory GitRemoteUrl.parse(String value) {
    final uri = Uri.tryParse(value);
    if (uri == null ||
        uri.scheme != 'https' ||
        uri.host.isEmpty ||
        uri.userInfo.isNotEmpty ||
        uri.fragment.isNotEmpty ||
        uri.query.isNotEmpty) {
      throw const GitException(
        GitErrorCode.protocolUnsupported,
        'remote URL must be a credential-free HTTPS URL',
      );
    }
    return GitRemoteUrl._(uri);
  }

  String get redactedOrigin => uri.origin;
}
