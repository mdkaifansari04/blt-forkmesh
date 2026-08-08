#pragma once

#include <QByteArray>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QString>
#include <QVariant>

namespace forkmesh {

// How a failed QNetworkReply should read in a log line that already names the
// URL somewhere else on the same line.
//
// Qt's QNetworkReply::errorString() is the wrong text for that job whenever the
// server actually answered. It reads "Error transferring <url> - server
// replied: <reason phrase>", so it repeats a URL the caller is about to print
// anyway, and it trails off into nothing when the response carried no reason
// phrase — which Cloudflare Workers never send. A relay 503 on a release blob
// therefore logged (adhoc #1613):
//
//   net GET ERR 503 Error transferring https://forkmesh.com/api/repo/…/blob/
//   sha256/c832… - server replied: https://forkmesh.com/api/repo/…/blob/
//   sha256/c832… · release fetch
//
// — the URL twice, and not one word about what went wrong. Quote the reason
// phrase instead when there is one, say only the status code when there isn't,
// and keep Qt's string solely for a transport failure (offline, DNS, TLS
// refusal) that has no HTTP status to report at all.
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

// The server's own words about a failure, flattened onto one line. A relay
// error body is a short JSON object ({"error":"mirror_unavailable"}) and is
// usually the only thing that explains a bare status code, but a failing
// upstream can also answer with a full HTML page — so cap it rather than let a
// log entry swallow the pane.
inline QString networkResponseSnippet(const QByteArray &body,
                                      int maxChars = 200)
{
    const QString text = QString::fromUtf8(body).simplified();
    if (maxChars > 0 && text.size() > maxChars)
        return text.left(maxChars) + QString::fromUtf8("\xE2\x80\xA6");
    return text;
}

} // namespace forkmesh
