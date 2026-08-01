#pragma once

#include <QHash>
#include <QRandomGenerator>
#include <QString>
#include <QtGlobal>















class NetworkBackoff {
public:
    static constexpr qint64 kDefaultBaseMs = 60LL * 1000;
    static constexpr qint64 kDefaultCapMs = 10LL * 60 * 1000;



    bool ready(const QString &channel, qint64 nowMs) const
    {
        return m_channels.value(channel).nextAllowedMs <= nowMs;
    }



    void noteSuccess(const QString &channel)
    {
        if (!channel.isEmpty())
            m_channels.remove(channel);
    }




    void noteFailure(const QString &channel, qint64 nowMs, qint64 baseMs,
                     qint64 capMs)
    {
        if (channel.isEmpty())
            return;
        State &s = m_channels[channel];
        if (s.failures < 30)
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
