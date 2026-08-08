#pragma once

#include "NetworkBackoff.h"

#include <QHash>
#include <QLatin1String>
#include <QString>
#include <QStringList>
#include <QtGlobal>

// Did this mirror push fail because the gateway never answered, rather than
// because the repository on it refused a ref?
//
// The two failures need opposite handling. A rejected ref is about *this*
// push and clears when the source converges, so retrying on the next safety
// pass is exactly right. A gateway that does not answer — a retired node whose
// DNS record still resolves, a host behind a dead route, a box that is down —
// costs a full TCP connect timeout (~2 minutes of a hung `git push`) and
// produces the identical red log line every five-second pass, forever. Those
// are the failures worth rotating past.
//
// Matched on ssh's own wording, so a gateway that authenticates and then
// refuses the push (a hook denial, a revoked key) is deliberately NOT counted
// here: it answered, and a human needs to see that every time.
inline bool mirrorGatewayUnreachable(const QString &pushStderr)
{
    static const QLatin1String kPhrases[] = {
        QLatin1String("connection timed out"),
        QLatin1String("operation timed out"),
        QLatin1String("connection refused"),
        QLatin1String("no route to host"),
        QLatin1String("network is unreachable"),
        QLatin1String("could not resolve hostname"),
        QLatin1String("name or service not known"),
        QLatin1String("nodename nor servname provided"),
        QLatin1String("temporary failure in name resolution"),
        QLatin1String("connection closed by remote host"),
        QLatin1String("connection reset by peer"),
    };
    const QString text = pushStderr.toLower();
    for (const QLatin1String &phrase : kPhrases) {
        if (text.contains(phrase))
            return true;
    }
    return false;
}

// Which SSH gateways the unattended fan-out should try on this pass.
//
// The fan-out is a broadcast: every configured ssh:// remote gets the same
// refs. One unreachable gateway must therefore never define the cadence for
// the rest of the fleet. This rotates it out instead — a gateway that does not
// answer sits out an exponentially growing cooldown (one minute, then two,
// four, … up to a quarter of an hour) while every healthy gateway keeps
// syncing on the normal pass, and rotates back in when its cooldown expires.
// The first success clears the streak.
//
// The cooldown itself is NetworkBackoff, the same curve the periodic pollers
// use. What lives here is the part a poller has no need for: how long the
// gateway has been down, so the log can say it once an hour rather than every
// pass, and so a node that has been silent for a full day can be named as
// probably retired — the fan-out cannot drop a remote the operator configured,
// but it can stop pretending the outage is news and say what to do about it.
class MirrorGatewayHealth {
public:
    static constexpr qint64 kBaseCooldownMs = 60LL * 1000;         // one pass
    static constexpr qint64 kMaxCooldownMs = 15LL * 60 * 1000;     // 15 minutes
    static constexpr qint64 kRepeatNoticeMs = 60LL * 60 * 1000;    // hourly
    static constexpr qint64 kRetiredAfterMs = 24LL * 60 * 60 * 1000;

    // What the caller should say about one unreachable gateway.
    struct Notice {
        qint64 cooldownMs = 0;  // how long it now sits out of the fan-out
        qint64 downForMs = 0;   // length of the current unreachable streak
        bool announce = false;  // log the failure this round, or stay quiet?
        bool retired = false;   // down long enough to suggest dropping it
    };

    // Should this pass push to the gateway, or is it still cooling down?
    bool readyToPush(const QString &host, qint64 nowMs) const
    {
        return m_backoff.ready(host, nowMs);
    }

    qint64 cooldownRemainingMs(const QString &host, qint64 nowMs) const
    {
        return m_backoff.msUntilReady(host, nowMs);
    }

    // The gateway did not answer: extend its cooldown and report what to log.
    Notice noteUnreachable(const QString &host, qint64 nowMs)
    {
        Streak &streak = m_streaks[host];
        if (streak.sinceMs == 0)
            streak.sinceMs = nowMs;
        m_backoff.noteFailure(host, nowMs, kBaseCooldownMs, kMaxCooldownMs);
        Notice notice;
        notice.cooldownMs = m_backoff.msUntilReady(host, nowMs);
        notice.downForMs = nowMs - streak.sinceMs;
        // A fresh streak has announcedMs 0, so the first failure always speaks.
        notice.announce = nowMs - streak.announcedMs >= kRepeatNoticeMs;
        if (notice.announce)
            streak.announcedMs = nowMs;
        if (!streak.retiredNoticed && notice.downForMs >= kRetiredAfterMs) {
            streak.retiredNoticed = true;
            notice.retired = true;
        }
        return notice;
    }

    // The gateway answered. Returns true when it had been rotated out, so the
    // caller can report the recovery it already reported the loss of.
    bool noteReachable(const QString &host)
    {
        m_backoff.noteSuccess(host);
        return m_streaks.remove(host) > 0;
    }

private:
    struct Streak {
        qint64 sinceMs = 0;       // first failure of the current streak
        qint64 announcedMs = 0;   // last time the log mentioned it
        bool retiredNoticed = false;
    };
    NetworkBackoff m_backoff;
    QHash<QString, Streak> m_streaks;
};
