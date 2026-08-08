import 'dart:convert';
import 'dart:io';
import 'dart:typed_data';

import '../crypto/collision_screen.dart';
import '../errors/git_error.dart';
import '../security/cancellation.dart';
import '../security/resource_limits.dart';
import 'git_object.dart';

final class LooseObjectStore {
  LooseObjectStore(this.gitDirectory, this.limits, {this.detector});

  final Directory gitDirectory;
  final GitResourceLimits limits;
  final GitSha1CollisionDetector? detector;

  File fileFor(GitObjectId id) => File(
    '${gitDirectory.path}${Platform.pathSeparator}objects'
    '${Platform.pathSeparator}${id.hex.substring(0, 2)}'
    '${Platform.pathSeparator}${id.hex.substring(2)}',
  );

  Future<GitObjectId> write(GitObjectType type, List<int> body) async {
    if (body.length > limits.maxObjectBytes) {
      throw const GitException(
        GitErrorCode.resourceLimitExceeded,
        'object exceeds configured limit',
      );
    }
    final frame = GitObjectFrame.encode(type, body);
    final id = GitObjectId.sha1Of(frame);
    final destination = fileFor(id);
    await destination.parent.create(recursive: true);
    if (await destination.exists()) return id;
    final temporary = File(
      '${destination.path}.tmp-${DateTime.now().microsecondsSinceEpoch}',
    );
    try {
      await temporary.writeAsBytes(ZLibEncoder().convert(frame), flush: true);
      try {
        await temporary.rename(destination.path);
      } on FileSystemException {
        if (!await destination.exists()) rethrow;
      }
    } finally {
      if (await temporary.exists()) await temporary.delete();
    }
    return id;
  }

  Future<GitObjectFrame> read(
    GitObjectId expectedId, {
    GitSha1ObjectOrigin origin = GitSha1ObjectOrigin.trustedLocal,
    GitCancellationToken? cancellation,
  }) async {
    final source = fileFor(expectedId);
    if (!await source.exists()) {
      throw const GitException(GitErrorCode.invalidObject, 'object is absent');
    }
    final compressed = await source.readAsBytes();
    final frameBytes = _inflateBounded(compressed, limits.maxObjectBytes + 128);
    final frame = GitObjectFrame.decode(frameBytes, limits);
    final screening = Sha1CollisionScreen(detector: detector).screenFrame(
      frame: frameBytes,
      objectType: frame.type,
      declaredBodySize: frame.body.length,
      origin: origin,
      limits: limits,
      cancellation: cancellation,
    );
    if (!screening.approved) throw screening.toException();
    final actualId = GitObjectId.fromSha1Bytes(screening.digest);
    if (actualId != expectedId) {
      throw const GitException(
        GitErrorCode.objectHashMismatch,
        'loose object path does not match its contents',
      );
    }
    return frame;
  }
}

final class _BoundedOutputSink implements ChunkedConversionSink<List<int>> {
  _BoundedOutputSink(this.maximumBytes);

  final int maximumBytes;
  final BytesBuilder _output = BytesBuilder(copy: false);
  var _closed = false;

  @override
  void add(List<int> chunk) {
    if (_closed || _output.length + chunk.length > maximumBytes) {
      throw const GitException(
        GitErrorCode.resourceLimitExceeded,
        'inflated object exceeds configured limit',
      );
    }
    _output.add(chunk);
  }

  @override
  void close() => _closed = true;

  Uint8List takeBytes() {
    if (!_closed) {
      throw const GitException(
        GitErrorCode.invalidObject,
        'zlib stream did not close',
      );
    }
    return _output.takeBytes();
  }
}

Uint8List _inflateBounded(Uint8List compressed, int maximumBytes) {
  final output = _BoundedOutputSink(maximumBytes);
  final input = ZLibDecoder().startChunkedConversion(output);
  try {
    input.add(compressed);
    input.close();
  } on FormatException {
    throw const GitException(
      GitErrorCode.invalidObject,
      'object zlib stream is invalid',
    );
  }
  return output.takeBytes();
}
