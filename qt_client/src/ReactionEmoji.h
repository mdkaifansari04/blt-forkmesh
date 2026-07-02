#pragma once

#include <QColor>
#include <QPixmap>
#include <QString>
#include <QVector>

// Vector-painted emoji for chat reactions. The app cannot rely on a color
// emoji font being installed, so every glyph is drawn with QPainter and looks
// identical (and crisp at any DPI) on all platforms.
namespace reactions {

struct Choice {
    QString value; // wire value stored in reaction payloads ("like", ...)
    QString label; // human-readable name ("Like")
};

// Ordered set of reactions offered by the picker.
const QVector<Choice> &choices();

// Human label for a stored reaction value; falls back to the raw value.
QString displayName(const QString &value);

// Canonical wire value for a stored payload. Legacy payloads carried literal
// unicode emoji; those map onto today's named values so old reactions render
// with the same painted icons.
QString canonicalValue(const QString &value);

// Whether a painted icon exists for this (canonicalized) value.
bool hasEmoji(const QString &value);

// The painted emoji at `size` device-independent pixels (empty if unknown).
QPixmap emojiPixmap(const QString &value, int size, qreal dpr);

// Monochrome "add reaction" glyph (smiley with a small plus) for the bar.
QPixmap addGlyph(int size, qreal dpr, const QColor &color);

} // namespace reactions
