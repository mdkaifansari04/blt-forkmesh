// Cross-region free helpers shared by several MainWindow feature translation
// units. These were forward-declared at the top of MainWindow.cpp and defined
// deep in its feature code; lifted here (with external linkage, in namespace
// forkmesh::ui) so the per-feature MainWindow*.cpp files can all call them.
// Declarations live in MainWindowInternal.h.

#include "ActionStore.h"

#include <QColor>
#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QString>
#include <QStringList>
#include <QUrl>

namespace forkmesh::ui {

QString openAiAuthHeader(const QString &apiKey)
{
    return QStringLiteral("Bearer ") + apiKey.trimmed();
}

QNetworkRequest openAiRequest(const QUrl &url, const QString &apiKey)
{
    QNetworkRequest request(url);
    request.setRawHeader("Authorization", openAiAuthHeader(apiKey).toUtf8());
    request.setRawHeader("Accept", "application/json");
    return request;
}

QString apiErrorSummary(QNetworkReply *reply, const QByteArray &body)
{
    const int status =
        reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    QString message = reply->errorString();
    const QJsonDocument doc = QJsonDocument::fromJson(body);
    const QString apiMessage =
        doc.object().value("error").toObject().value("message").toString();
    if (!apiMessage.isEmpty())
        message = apiMessage;
    if (status > 0)
        return QStringLiteral("HTTP %1: %2").arg(status).arg(message);
    return message;
}

QString openAiResponseText(const QJsonObject &obj)
{
    const QString direct = obj.value(QStringLiteral("output_text")).toString().trimmed();
    if (!direct.isEmpty())
        return direct;

    QStringList parts;
    const QJsonArray output = obj.value(QStringLiteral("output")).toArray();
    for (const QJsonValue &outputValue : output) {
        const QJsonArray content =
            outputValue.toObject().value(QStringLiteral("content")).toArray();
        for (const QJsonValue &contentValue : content) {
            const QJsonObject contentObj = contentValue.toObject();
            const QString text = contentObj.value(QStringLiteral("text")).toString();
            if (!text.trimmed().isEmpty())
                parts << text.trimmed();
        }
    }
    return parts.join(QStringLiteral("\n\n")).trimmed();
}

// USD cost of an "Ask AI" Responses call from its usage tokens. Prices are the
// published OpenAI rates for gpt-4.1-nano (input $0.10 / output $0.40 per 1M
// tokens); bump these if the model or its pricing changes.
double openAiAskCostUsd(const QJsonObject &response, qint64 *inTokens,
                        qint64 *outTokens)
{
    const QJsonObject usage = response.value(QStringLiteral("usage")).toObject();
    const qint64 input = static_cast<qint64>(
        usage.value(QStringLiteral("input_tokens")).toDouble());
    const qint64 output = static_cast<qint64>(
        usage.value(QStringLiteral("output_tokens")).toDouble());
    if (inTokens)
        *inTokens = input;
    if (outTokens)
        *outTokens = output;
    constexpr double kInputPerMillion = 0.10;
    constexpr double kOutputPerMillion = 0.40;
    return (input * kInputPerMillion + output * kOutputPerMillion) / 1000000.0;
}

QString mirrorHeadBranch(const QString &mirrorPath)
{
    if (!QDir(mirrorPath).exists())
        return QString();
    QProcess p;
    p.start("git", {"-C", mirrorPath, "symbolic-ref", "--short", "HEAD"});
    if (!p.waitForFinished(5000) || p.exitCode() != 0)
        return QString();
    return QString::fromUtf8(p.readAllStandardOutput()).trimmed();
}

QString mirrorBranchCommit(const QString &mirrorPath, const QString &branch)
{
    if (!QDir(mirrorPath).exists() || branch.isEmpty())
        return QString();
    QProcess p;
    p.start("git", {"-C", mirrorPath, "rev-parse", "--verify",
                    "refs/heads/" + branch});
    if (!p.waitForFinished(5000) || p.exitCode() != 0)
        return QString();
    return QString::fromUtf8(p.readAllStandardOutput()).trimmed();
}

QString actionStatusText(const QString &status)
{
    if (status == ActionStatus::AwaitingApproval) return QStringLiteral("Awaiting approval");
    if (status == ActionStatus::Queued) return QStringLiteral("Queued");
    if (status == ActionStatus::Running) return QStringLiteral("Running");
    if (status == ActionStatus::Success) return QStringLiteral("Success");
    if (status == ActionStatus::Failed) return QStringLiteral("Failed");
    if (status == ActionStatus::Rejected) return QStringLiteral("Rejected");
    if (status == ActionStatus::Cancelled) return QStringLiteral("Cancelled");
    return status;
}

QColor actionStatusColor(const QString &status)
{
    if (status == ActionStatus::Success) return QColor("#3fb950");
    if (status == ActionStatus::Failed) return QColor("#f85149");
    if (status == ActionStatus::Running) return QColor("#58a6ff");
    if (status == ActionStatus::AwaitingApproval) return QColor("#d29922");
    if (status == ActionStatus::Rejected) return QColor("#8b949e");
    if (status == ActionStatus::Cancelled) return QColor("#8b949e");
    return QColor("#8b949e");
}

} // namespace forkmesh::ui
