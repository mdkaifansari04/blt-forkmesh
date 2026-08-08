#pragma once

#include <QByteArray>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QString>
#include <QVariant>

namespace forkmesh {

inline QString networkFailureText(int httpStatus, const QString &reasonPhrase,
                                  const QString &qtErrorString)
{
    if (httpStatus <= 0)
        return qtErrorString.simplified();
    const QString reason = reasonPhrase.simplified();
    return reason.isEmpty()
               ? QString::number(httpStatus)
               : QStringLiteral("%1 %2").arg(QString::number(httpStatus), reason);
}

inline QString networkFailureText(const QNetworkReply *reply)
{
    if (!reply)
        return QString();
    const QVariant code =
        reply->attribute(QNetworkRequest::HttpStatusCodeAttribute);
    return networkFailureText(
        code.isValid() ? code.toInt() : 0,
        reply->attribute(QNetworkRequest::HttpReasonPhraseAttribute).toString(),
        reply->errorString());
}

inline QString networkResponseSnippet(const QByteArray &body,
                                      int maxChars = 200)
{
    const QString text = QString::fromUtf8(body).simplified();
    if (maxChars > 0 && text.size() > maxChars)
        return text.left(maxChars) + QString::fromUtf8("\xE2\x80\xA6");
    return text;
}

} // namespace forkmesh
