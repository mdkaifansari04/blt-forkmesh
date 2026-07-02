#pragma once

#include <QHash>
#include <QRandomGenerator>
#include <QString>
#include <QtGlobal>

// Per-channel exponential backoff for the app's periodic network pollers.
//
// A poller names a channel — usually its request URL, or a fixed label like
// "heartbeat" — calls ready() before firing, and reports the outcome with
// noteSuccess()/noteFailure() from the reply handler. A run of failures (a
// relay that is offline or returning HTTP 429 "Too Many Requests") holds that
// channel off for an exponentially growing delay: base, 2·base, 4·base, …
// capped. So instead of hammering an endpoint that is already failing on every
// timer tick, the app spaces its retries out. The first success clears the
// streak and the poller returns to its normal cadence.
//
// This is deliberately separate from DiscussionInboxBackoff, which is a fixed
// cooldown for endpoints a relay does not support at all (a 404): that means
// "don't bother for a while", this means "back off, but keep probing".
class NetworkBackoff {
public:
    static constexpr qint64 kDefaultBaseMs = 60LL * 1000;      // one poll cycle
    static constexpr qint64 kDefaultCapMs = 10LL * 60 * 1000;  // 10 minutes

    // May this channel fire now? True unless it is inside a post-failure
    // cooldown window (or has never failed).
    bool ready(const QString &channel, qint64 nowMs) const
    {
        return m_channels.value(channel).nextAllowedMs <= nowMs;
    }

    // A request succeeded: clear the failure streak so the next poll fires on
    // the normal schedule.
    void noteSuccess(const QString &channel)
    {
        if (!channel.isEmpty())
            m_channels.remove(channel);
    }

    // A request failed: extend the cooldown. The nth consecutive failure holds
    // the channel off for min(baseMs · 2^(n-1), capMs), plus a little jitter so
    // independently-failing channels don't all retry on the exact same tick.
    void noteFailure(const QString &channel, qint64 nowMs, qint64 baseMs,
                     qint64 capMs)
    {
        if (channel.isEmpty())
            return;
        State &s = m_channels[channel];
        if (s.failures < 30) // guard the shift below against overflow
            ++s.failures;
        qint64 delay = baseMs;
        for (int i = 1; i < s.failures && delay < capMs; ++i)
            delay <<= 1;
        delay = qMin(delay, capMs);
        const quint32 span = static_cast<quint32>(qMax<qint64>(1, delay / 8));
        s.nextAllowedMs =
            nowMs + delay + QRandomGenerator::global()->bounded(span);
    }

    void noteFailure(const QString &channel, qint64 nowMs)
    {
        noteFailure(channel, nowMs, kDefaultBaseMs, kDefaultCapMs);
    }

private:
    struct State {
        int failures = 0;
        qint64 nextAllowedMs = 0;
    };
    QHash<QString, State> m_channels;
};
