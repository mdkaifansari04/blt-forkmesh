#pragma once

#include <QHash>
#include <QJsonObject>
#include <QList>
#include <QString>
#include <QtGlobal>

// Client-side policy for the error reports this node sends to the relay so that
// a failure the app only ever showed to itself still reaches somebody (adhoc
// #1538). A warning dialog on a machine nobody is sitting in front of, or a red
// toast on a headless mirror, used to be the whole record of an outage: nothing
// was written to the shared operational log, so nothing pinged. Every
// warning/critical modal and every error toast now becomes one POST to
// /api/desktop-errors, which lands in the relay's error_log and pings the
// platform administrators the first time a distinct failure appears.
//
// This class is deliberately pure (no widgets, no network): it decides *whether*
// a report goes out and *what text* leaves the machine, so both are testable
// without a relay or a screen. MainWindow owns one instance and does the sending.
//
// Three bounds matter, because the reporter rides the error path:
//   * redact()  — a private path, URL, address or token never leaves the machine
//                 in the first place. The relay redacts again on arrival; this is
//                 the copy that decides what is put on the wire at all.
//   * accept()  — an identical failure reports once per hour, and no node sends
//                 more than kMaxPerWindow distinct reports an hour. The relay's
//                 error groups ping only on their first sighting, so a repeat
//                 costs a request and buys nothing.
//   * defer()   — the relay being rate-limited is exactly the failure most worth
//                 reporting, and exactly when a POST would be swallowed by the
//                 host-wide 429 backoff. Those reports are parked and flushed
//                 once the cooldown lifts, then dropped after kMaxAttempts.
namespace forkmesh {

// Off-switch for a node whose operator does not want its failures reported.
// Absent (the default) means reports are on: an unreported error on a headless
// node is invisible, which is the bug this exists to fix.
inline const QString kReportUserVisibleErrorsSetting =
    QStringLiteral("diagnostics/reportErrors");

class ClientErrorReports
{
public:
    // One report, already redacted and ready to send.
    struct Report {
        QString kind;     // "dialog" (modal) or "toast" (in-app error pill)
        QString surface;  // "app" or "headless"; becomes the relay's error_log path
        QString title;    // the dialog's own name for the operation, may be empty
        QString message;  // the failure text; empty means "nothing to report"
        qint64 tsMs = 0;  // when the operator saw it
        int attempts = 0; // sends tried so far (a deferred report keeps counting)
        // The Pings row this failure was filed as (adhoc #1629). Carried so the
        // report's fate can be written back onto that row — "Synced" once the
        // relay takes it, "Not synced" when it never does. Local bookkeeping
        // only: payload() never puts it on the wire, and dedupeKey() ignores it,
        // so two sightings of one failure still count as one report.
        qint64 pingId = 0;
    };

    static constexpr int kMaxMessageChars = 700;
    static constexpr int kMaxTitleChars = 120;
    // One hour is both the dedupe window and the window the send cap counts in.
    static constexpr qint64 kWindowMs = 60LL * 60 * 1000;
    static constexpr int kMaxPerWindow = 8;
    // Distinct messages remembered for the dedupe window. A storm of unique
    // texts must not grow this map without bound.
    static constexpr int kMaxTrackedKeys = 200;
    static constexpr int kMaxDeferred = 8;
    static constexpr int kMaxAttempts = 3;
    // A report older than this has lost its context; drop it rather than keep
    // retrying into a relay that has been unreachable all day.
    static constexpr qint64 kMaxDeferralMs = 6LL * 60 * 60 * 1000;

    // Strip anything private and collapse the text to one line. Returns an empty
    // string when nothing reportable is left.
    static QString redact(const QString &text, int maxChars);

    // Build a ready-to-send report. An empty surface becomes "app"; an empty
    // message (or one that redacts away to nothing) yields an empty Report,
    // which accept() refuses.
    static Report build(const QString &kind, const QString &surface,
                        const QString &title, const QString &message,
                        qint64 nowMs);

    // The JSON body POST /api/desktop-errors expects.
    static QJsonObject payload(const Report &report);

    // True when this report should go out now — and it is counted against the
    // dedupe window and the hourly cap, so call it exactly once per report.
    bool accept(const Report &report);

    // Park a report the relay cannot take yet. Refused (and dropped) once it has
    // been tried kMaxAttempts times or the queue is full of newer reports.
    bool defer(const Report &report);

    // Everything still worth sending, oldest first; the queue is left empty.
    // `expiredOut` collects the reports dropped for having waited longer than
    // kMaxDeferralMs — they are never sent, and the Pings row each one came from
    // has to be told that rather than waiting forever (adhoc #1629).
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
