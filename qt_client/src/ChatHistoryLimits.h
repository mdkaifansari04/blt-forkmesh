#pragma once

#include <QJsonObject>
#include <QList>
#include <QString>
#include <QStringList>

#include <utility>








namespace ChatHistoryLimits {


constexpr int kMaxEntriesPerChannel = 200;



constexpr qsizetype kMaxCharsPerChannel = 2 * 1024 * 1024;




inline qsizetype entryCost(const QJsonObject &entry)
{
    return entry.value(QLatin1String("file")).toString().size() +
           entry.value(QLatin1String("text")).toString().size() + 64;
}















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

}
