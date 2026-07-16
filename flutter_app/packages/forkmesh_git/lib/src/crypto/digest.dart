import 'dart:typed_data';

abstract interface class GitDigest {
  void add(List<int> bytes);
  Uint8List close();
}

int rotateLeft32(int value, int bits) =>
    ((value << bits) | (value >>> (32 - bits))) & 0xffffffff;

int rotateRight32(int value, int bits) =>
    ((value >>> bits) | (value << (32 - bits))) & 0xffffffff;
