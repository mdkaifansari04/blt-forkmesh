#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QStringList>

namespace NoteListEntry {

inline QString storageLabel(const QString &mode)
{
    if (mode == QLatin1String("cloud"))
        return QStringLiteral("Cloud");
    if (mode == QLatin1String("both"))
        return QStringLiteral("Local + cloud");
    return QStringLiteral("Local only");
}

inline QString visibilityLabel(const QJsonObject &note,
                               const QJsonObject &cloud, const QString &mode,
                               bool synced)
{
    if (mode == QLatin1String("local"))
        return QStringLiteral("On this computer");
    if (!synced)
        return QStringLiteral("Not published yet");
    // The local mirror carries the last known visibility, so the row still
    // reads correctly while the cloud listing is unavailable (e.g. signed
    // out) and only the sharing and view details go missing.
    const QJsonObject &status = cloud.isEmpty() ? note : cloud;
    if (status.value(QLatin1String("visibility")).toString() ==
        QLatin1String("public"))
        return QStringLiteral("Public");
    if (!cloud.value(QLatin1String("shares")).toArray().isEmpty())
        return QStringLiteral("Shared");
    return QStringLiteral("Private");
}

inline QString sharedWithLabel(const QJsonObject &cloud)
{
    QStringList names;
    const QJsonArray shares = cloud.value(QLatin1String("shares")).toArray();
    for (const QJsonValue &value : shares) {
        const QJsonObject share = value.toObject();
        const QString name = share.value(QLatin1String("name")).toString();
        if (name.isEmpty())
            continue;
        names << QStringLiteral("%1 (%2)").arg(
            name, share.value(QLatin1String("role")).toString(
                      QStringLiteral("viewer")));
    }
    return names.join(QStringLiteral(", "));
}

inline QString viewsLabel(const QJsonObject &cloud)
{
    const int views = cloud.value(QLatin1String("views")).toInt();
    const int readers = cloud.value(QLatin1String("readers")).toInt();
    if (views <= 0)
        return QString();
    const QString counted = views == 1 ? QStringLiteral("1 view")
                                       : QStringLiteral("%1 views").arg(views);
    return readers > 1 ? QStringLiteral("%1 from %2 readers")
                             .arg(counted).arg(readers)
                       : counted;
}

inline QStringList lines(const QJsonObject &note, const QJsonObject &cloud,
                         const QString &mode, bool synced,
                         const QString &stamp = QString())
{
    QStringList status{
        QStringLiteral("Storage: %1").arg(storageLabel(mode)),
        QStringLiteral("Visibility: %1").arg(
            visibilityLabel(note, cloud, mode, synced))};
    const QString views = viewsLabel(cloud);
    if (!views.isEmpty())
        status << QStringLiteral("Views: %1").arg(views);
    status << QStringLiteral("Rev %1").arg(
        qMax(1, note.value(QLatin1String("version")).toInt(1)));
    if (!stamp.isEmpty())
        status << stamp;

    QStringList result{note.value(QLatin1String("title")).toString(
                           QStringLiteral("Untitled note")),
                       status.join(QStringLiteral(" · "))};
    const QString shared = sharedWithLabel(cloud);
    if (!shared.isEmpty())
        result << QStringLiteral("Shared with %1").arg(shared);
    else if (synced)
        result << QStringLiteral("Not shared with anyone");
    return result;
}

} // namespace NoteListEntry
