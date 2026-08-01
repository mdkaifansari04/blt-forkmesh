#pragma once

#include <QJsonObject>
#include <QList>
#include <QString>
#include <QStringList>

#include <utility>

// Bounds for the in-RAM chat history a node keeps per channel (issue #428).
// A headless mirror node runs for months; without a cap every chat frame it
// ever saw stayed in ServerNode::m_channelHistory forever — including file
// messages, whose base64 payload alone can be tens of MB of QString chars.
// History only exists so a newcomer gets recent context replayed (and that
// replay goes out as a single WebSocket frame, so an unbounded backlog could
// not even be delivered), which recent-N with a byte budget preserves.
namespace ChatHistoryLimits {

// Newest entries kept per channel.
constexpr int kMaxEntriesPerChannel = 200;
// Char budget per channel (QString chars ≈ 2 bytes each, so ~4 MB of RAM).
// The base64 "file" payload of a single large transfer dwarfs everything
// else, so this is what actually bounds a channel's footprint.
constexpr qsizetype kMaxCharsPerChannel = 2 * 1024 * 1024;

// Approximate in-RAM cost of one stored entry, in QString chars. The "file"
// payload dominates file messages, "text" the rest; the constant covers the
// small fixed fields (id/sender/ts/...).
inline qsizetype entryCost(const QJsonObject &entry)
{
    return entry.value(QLatin1String("file")).toString().size() +
           entry.value(QLatin1String("text")).toString().size() + 64;
}

// Append `message` to `history`, then evict oldest entries until both the
// count and char budgets hold. A message whose payload alone exceeds the char
// budget (a huge file transfer) is stored without its "file" body — the
// transfer still reaches live peers, it just isn't replayed to newcomers —
// so the newest message always fits and history is never left empty.
// Returns the ids of the evicted entries so the caller can drop any
// per-message bookkeeping (sender/conversation/reactions) keyed on them.
//
// `runningTotal`, when given, holds the channel's current total cost and is
// updated in place. Without it every append re-derives the total by walking
// the whole list — and entryCost() materializes each entry's "file"/"text"
// payload out of QJsonObject's compact storage, so on a busy channel that
// walk re-decoded up to kMaxCharsPerChannel chars per incoming message and
// showed up as >500 ms GUI stalls in ServerNode::storeHistory.
inline QStringList appendBounded(QList<QJsonObject> &history, QJsonObject message,
                                 qsizetype *runningTotal = nullptr)
{
    if (entryCost(message) > kMaxCharsPerChannel)
        message.remove(QLatin1String("file"));
    history.append(message);
    qsizetype totalChars = 0;
    if (runningTotal) {
        totalChars = *runningTotal + entryCost(message);
    } else {
        for (const QJsonObject &entry : std::as_const(history))
            totalChars += entryCost(entry);
    }
    QStringList evicted;
    while (history.size() > 1 && (history.size() > kMaxEntriesPerChannel ||
                                  totalChars > kMaxCharsPerChannel)) {
        totalChars -= entryCost(history.first());
        const QString id = history.first().value(QLatin1String("id")).toString();
        if (!id.isEmpty())
            evicted.append(id);
        history.removeFirst();
    }
    if (runningTotal)
        *runningTotal = totalChars;
    return evicted;
}

} // namespace ChatHistoryLimits
