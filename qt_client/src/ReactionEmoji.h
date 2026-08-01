#pragma once

#include <QColor>
#include <QPixmap>
#include <QString>
#include <QVector>




namespace reactions {

struct Choice {
    QString value;
    QString label;
};


const QVector<Choice> &choices();


QString displayName(const QString &value);




QString canonicalValue(const QString &value);


bool hasEmoji(const QString &value);


QPixmap emojiPixmap(const QString &value, int size, qreal dpr);


QPixmap addGlyph(int size, qreal dpr, const QColor &color);

}
