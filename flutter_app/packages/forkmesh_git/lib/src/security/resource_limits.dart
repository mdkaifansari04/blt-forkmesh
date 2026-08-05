final class GitResourceLimits {
  GitResourceLimits({
    this.maxObjectBytes = defaultMaxObjectBytes,
    this.maxInflatedBytes = defaultMaxInflatedBytes,
    this.maxRefNameBytes = defaultMaxRefNameBytes,
    this.maxRefCount = defaultMaxRefCount,
  }) {
    _validateRange(maxObjectBytes, 'maxObjectBytes', hardMaxObjectBytes);
    _validateRange(maxInflatedBytes, 'maxInflatedBytes', hardMaxInflatedBytes);
    _validateRange(maxRefNameBytes, 'maxRefNameBytes', hardMaxRefNameBytes);
    _validateRange(maxRefCount, 'maxRefCount', hardMaxRefCount);
  }

  static const defaultMaxObjectBytes = 64 * 1024 * 1024;
  static const hardMaxObjectBytes = 256 * 1024 * 1024;
  static const defaultMaxInflatedBytes = 768 * 1024 * 1024;
  static const hardMaxInflatedBytes = 1024 * 1024 * 1024;
  static const defaultMaxRefNameBytes = 255;
  static const hardMaxRefNameBytes = 1024;
  static const defaultMaxRefCount = 25000;
  static const hardMaxRefCount = 100000;

  final int maxObjectBytes;
  final int maxInflatedBytes;
  final int maxRefNameBytes;
  final int maxRefCount;

  static void _validateRange(int value, String name, int hardMaximum) {
    if (value <= 0 || value > hardMaximum) {
      throw ArgumentError.value(value, name, 'must be in 1..$hardMaximum');
    }
  }
}
