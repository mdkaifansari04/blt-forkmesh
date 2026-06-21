#pragma once

#include <QByteArray>
#include <QImage>
#include <QString>
#include <vector>

// A small, self-contained QR Code generator (byte mode). No third-party
// dependency: implements the QR spec directly — version auto-selection,
// Reed–Solomon error correction, the standard function patterns, and data-mask
// selection by penalty score. Sufficient for short payloads like wallet addresses.
namespace QrCode {

enum class Ecl { Low, Medium, Quartile, High };

// Encode bytes into a QR matrix; modules[y][x] == true means a dark module.
// Returns an empty matrix if the data does not fit in any version.
std::vector<std::vector<bool>> encode(const QByteArray &data,
                                      Ecl ecl = Ecl::Medium);

// Render text (UTF-8) to a black-on-white image: each module is `scale` pixels
// with a `margin`-module quiet zone. Returns a null image on failure.
QImage encodeToImage(const QString &text, int scale = 4, int margin = 4,
                     Ecl ecl = Ecl::Medium);

} // namespace QrCode
