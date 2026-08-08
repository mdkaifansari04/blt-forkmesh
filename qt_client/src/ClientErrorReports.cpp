#include "ClientErrorReports.h"

#include <QRegularExpression>

#include <algorithm>

namespace forkmesh {

QString ClientErrorReports::redact(const QString &text, int maxChars)
{
    QString out = text.simplified();
    if (out.isEmpty())
        return out;

    // Order matters: credentials and addresses first, then whole URLs, then the
    // long opaque ids (key material, node ids, hashes), then filesystem paths.
    // Each replacement is deliberately coarse — a report is triage material, not
    // a forensic record, and the shape of the failure survives all of it.
    static const QRegularExpression secret(
        QStringLiteral("\\b(?:authorization|bearer|password|passwd|secret|"
                       "token|api[_-]?key)\\b\\s*[:=]?\\s*[^\\s,;]+"),
        QRegularExpression::CaseInsensitiveOption);
    out.replace(secret, QStringLiteral("<secret>"));
    static const QRegularExpression email(
        QStringLiteral("\\b[A-Za-z0-9._%+-]+@[A-Za-z0-9.-]+\\.[A-Za-z]{2,}\\b"));
    out.replace(email, QStringLiteral("<email>"));
    static const QRegularExpression url(
        QStringLiteral("\\b[A-Za-z][A-Za-z0-9+.-]*://\\S+"));
    out.replace(url, QStringLiteral("<url>"));
    static const QRegularExpression opaqueId(
        QStringLiteral("\\b[A-Za-z0-9_-]{32,}\\b"));
    out.replace(opaqueId, QStringLiteral("<id>"));
    static const QRegularExpression windowsPath(
        QStringLiteral("[A-Za-z]:\\\\[^\\s\"']+"));
    out.replace(windowsPath, QStringLiteral("<path>"));
    // Two or more segments, so a bare "/status" or an "owner/repo" slug (both
    // real signal) survives while "/home/<user>/…" and "/api/repo/x/y" do not.
    static const QRegularExpression unixPath(
        QStringLiteral("(?:/[A-Za-z0-9._~%+-]+){2,}/?"));
    out.replace(unixPath, QStringLiteral("<path>"));

    out = out.simplified();
    if (maxChars > 1 && out.size() > maxChars)
        out = out.left(maxChars - 1) + QChar(0x2026);
    return out;
}

ClientErrorReports::Report ClientErrorReports::build(const QString &kind,
                                                     const QString &surface,
                                                     const QString &title,
                                                     const QString &message,
                                                     qint64 nowMs)
{
    Report report;
    report.kind = kind.trimmed().toLower();
    report.surface = surface.trimmed().toLower();
    if (report.surface.isEmpty())
        report.surface = QStringLiteral("app");
    report.title = redact(title, kMaxTitleChars);
    report.message = redact(message, kMaxMessageChars);
    report.tsMs = nowMs;
    return report;
}

QJsonObject ClientErrorReports::payload(const Report &report)
{
    return QJsonObject{{QStringLiteral("kind"), report.kind},
                       {QStringLiteral("surface"), report.surface},
                       {QStringLiteral("title"), report.title},
                       {QStringLiteral("message"), report.message}};
}

QString ClientErrorReports::dedupeKey(const Report &report)
{
    return report.kind + QLatin1Char('|') + report.surface + QLatin1Char('|')
           + report.title + QLatin1Char('|') + report.message;
}

void ClientErrorReports::prune(qint64 nowMs)
{
    auto expired = [nowMs](qint64 stamp) { return nowMs - stamp >= kWindowMs; };
    m_acceptedMs.erase(
        std::remove_if(m_acceptedMs.begin(), m_acceptedMs.end(), expired),
        m_acceptedMs.end());
    for (auto it = m_lastSentMs.begin(); it != m_lastSentMs.end();) {
        if (expired(it.value()))
            it = m_lastSentMs.erase(it);
        else
            ++it;
    }
    // A flood of distinct messages inside one window can still outgrow the map;
    // forget the oldest half rather than the whole history.
    if (m_lastSentMs.size() > kMaxTrackedKeys) {
        QList<qint64> stamps = m_lastSentMs.values();
        std::sort(stamps.begin(), stamps.end());
        const qint64 cutoff = stamps.at(stamps.size() / 2);
        for (auto it = m_lastSentMs.begin(); it != m_lastSentMs.end();) {
            if (it.value() < cutoff)
                it = m_lastSentMs.erase(it);
            else
                ++it;
        }
    }
}

bool ClientErrorReports::accept(const Report &report)
{
    if (report.kind.isEmpty() || report.surface.isEmpty()
        || report.message.isEmpty())
        return false;
    prune(report.tsMs);
    const QString key = dedupeKey(report);
    const auto sent = m_lastSentMs.constFind(key);
    if (sent != m_lastSentMs.constEnd()
        && report.tsMs - sent.value() < kWindowMs)
        return false;
    if (m_acceptedMs.size() >= kMaxPerWindow)
        return false;
    m_lastSentMs.insert(key, report.tsMs);
    m_acceptedMs.append(report.tsMs);
    return true;
}

bool ClientErrorReports::defer(const Report &report)
{
    if (report.message.isEmpty() || report.attempts >= kMaxAttempts)
        return false;
    Report parked = report;
    ++parked.attempts;
    // The newest reports describe the state the node is in now, so an overfull
    // queue drops its oldest entry rather than refusing the arrival.
    while (m_deferred.size() >= kMaxDeferred)
        m_deferred.removeFirst();
    m_deferred.append(parked);
    return true;
}

QList<ClientErrorReports::Report> ClientErrorReports::takeDeferred(
    qint64 nowMs, QList<Report> *expiredOut)
{
    QList<Report> ready;
    for (const Report &report : m_deferred) {
        if (nowMs - report.tsMs <= kMaxDeferralMs)
            ready.append(report);
        else if (expiredOut)
            expiredOut->append(report);
    }
    m_deferred.clear();
    return ready;
}

} // namespace forkmesh
