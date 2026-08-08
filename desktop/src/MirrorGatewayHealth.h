#pragma once

#include "NetworkBackoff.h"

#include <QHash>
#include <QLatin1String>
#include <QString>
#include <QStringList>
#include <QtGlobal>

// Only transport failures are unreachable; authenticated ref rejects are not.
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

class MirrorGatewayHealth {
public:
    static constexpr qint64 kBaseCooldownMs = 60LL * 1000;         // one pass
    static constexpr qint64 kMaxCooldownMs = 15LL * 60 * 1000;     // 15 minutes
    static constexpr qint64 kRepeatNoticeMs = 60LL * 60 * 1000;    // hourly
    static constexpr qint64 kRetiredAfterMs = 24LL * 60 * 60 * 1000;

    struct Notice {
        qint64 cooldownMs = 0;
        qint64 downForMs = 0;
        bool announce = false;
        bool retired = false;
    };

    bool readyToPush(const QString &host, qint64 nowMs) const
    {
        return m_backoff.ready(host, nowMs);
    }

    qint64 cooldownRemainingMs(const QString &host, qint64 nowMs) const
    {
        return m_backoff.msUntilReady(host, nowMs);
    }

    Notice noteUnreachable(const QString &host, qint64 nowMs)
    {
        Streak &streak = m_streaks[host];
        if (streak.sinceMs == 0)
            streak.sinceMs = nowMs;
        m_backoff.noteFailure(host, nowMs, kBaseCooldownMs, kMaxCooldownMs);
        Notice notice;
        notice.cooldownMs = m_backoff.msUntilReady(host, nowMs);
        notice.downForMs = nowMs - streak.sinceMs;
        notice.announce = nowMs - streak.announcedMs >= kRepeatNoticeMs;
        if (notice.announce)
            streak.announcedMs = nowMs;
        if (!streak.retiredNoticed && notice.downForMs >= kRetiredAfterMs) {
            streak.retiredNoticed = true;
            notice.retired = true;
        }
        return notice;
    }

    bool noteReachable(const QString &host)
    {
        m_backoff.noteSuccess(host);
        return m_streaks.remove(host) > 0;
    }

private:
    struct Streak {
        qint64 sinceMs = 0;
        qint64 announcedMs = 0;
        bool retiredNoticed = false;
    };
    NetworkBackoff m_backoff;
    QHash<QString, Streak> m_streaks;
};
