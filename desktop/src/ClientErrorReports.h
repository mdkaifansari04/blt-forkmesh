#pragma once

#include <QHash>
#include <QJsonObject>
#include <QList>
#include <QString>
#include <QtGlobal>

namespace forkmesh {

inline const QString kReportUserVisibleErrorsSetting =
    QStringLiteral("diagnostics/reportErrors");

class ClientErrorReports
{
public:
    struct Report {
        QString kind;     // "dialog" (modal) or "toast" (in-app error pill)
        QString surface;  // "app" or "headless"; becomes the relay's error_log path
        QString title;    // the dialog's own name for the operation, may be empty
        QString message;  // the failure text; empty means "nothing to report"
        qint64 tsMs = 0;  // when the operator saw it
        int attempts = 0; // sends tried so far (a deferred report keeps counting)
        qint64 pingId = 0;
    };

    static constexpr int kMaxMessageChars = 700;
    static constexpr int kMaxTitleChars = 120;
    static constexpr qint64 kWindowMs = 60LL * 60 * 1000;
    static constexpr int kMaxPerWindow = 8;
    static constexpr int kMaxTrackedKeys = 200;
    static constexpr int kMaxDeferred = 8;
    static constexpr int kMaxAttempts = 3;
    static constexpr qint64 kMaxDeferralMs = 6LL * 60 * 60 * 1000;

    static QString redact(const QString &text, int maxChars);

    static Report build(const QString &kind, const QString &surface,
                        const QString &title, const QString &message,
                        qint64 nowMs);

    static QJsonObject payload(const Report &report);

    bool accept(const Report &report);

    bool defer(const Report &report);

    QList<Report> takeDeferred(qint64 nowMs, QList<Report> *expiredOut = nullptr);
    int deferredCount() const { return m_deferred.size(); }

private:
    static QString dedupeKey(const Report &report);
    void prune(qint64 nowMs);

    QHash<QString, qint64> m_lastSentMs; // dedupe key -> when it last went out
    QList<qint64> m_acceptedMs;          // send stamps inside the current window
    QList<Report> m_deferred;
};

} // namespace forkmesh
