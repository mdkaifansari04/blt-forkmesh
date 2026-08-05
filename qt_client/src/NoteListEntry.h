#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QStringList>

// The label lines behind each row of the Notes sidebar.
//
// The sidebar used to say only "<title> / <storage> · v<n>", which left the
// two questions people actually ask of a note list unanswered: is this one
// published, and who else can see it? A note the user had asked to keep in
// the cloud but that had never reached it looked identical to one that had —
// which is how a "published" note could be nowhere on the web with nothing
// in the UI saying so.
//
// Every function here is pure so the wording is testable without a window.
namespace NoteListEntry {

// Where the note is stored, in the words the storage combo uses.
inline QString storageLabel(const QString &mode)
{
    if (mode == QLatin1String("cloud"))
        return QStringLiteral("Cloud");
    if (mode == QLatin1String("both"))
        return QStringLiteral("Local + cloud");
    return QStringLiteral("Local only");
}

// Who can reach the note, in one word. `cloud` is the note's cloud copy (or
// an empty object when there is none to read); `synced` says whether a cloud
// copy exists at all, which is the difference between "Private" and a note
// that never made it off this computer despite being asked to.
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

// "bob (editor), acme (viewer)", or "" when the note is shared with nobody.
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

// "12 views", or "12 views from 4 readers" once more than one person has
// read it. Empty for an unread note: a draft labelled "0 views" is noise.
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

// The full row: title, then a compact status line, then the sharing line.
// `stamp` is the caller's already-formatted "edited …" text (empty to omit it)
// so this stays independent of the local time zone.
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
