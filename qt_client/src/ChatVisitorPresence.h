#pragma once

#include <QLatin1String>
#include <QRegularExpression>
#include <QString>

// Guests and World visitors are throwaway browser sessions, not nodes or
// accounts: a tab opened on /chat or /world joins the room under an anonymous
// "Guest 1667" / "World Guest f49ab8" name (or, for a signed-in visitor, an
// older "World visitor · jett" one) and is gone the moment the tab closes.
//
// Real peers are deliberately remembered after they drop off the roster — an
// offline node stays selectable in the Node dropdown, and a registered account
// stays listed in the users column. Applying that retention to visitors made
// the users column fill up with dozens of dead "Guest ####" rows that could
// never come back (adhoc #404), so visitors are forgotten outright once they
// have been silent for kVisitorIdleMs.
namespace ChatVisitorPresence {

// Idle window after which a guest/World-visitor peer is dropped. Long enough
// to survive a handful of missed presence beats (the 60s heartbeat, 3 of which
// already mark a peer offline), short enough that a closed tab clears quickly.
constexpr qint64 kVisitorIdleMs = 600000; // 10 minutes

// True for a peer that joined as an anonymous guest or a World visitor.
// `accountKind` is the peer's advertised kind ("guest" for an anonymous browser
// in the public World room); the name check catches visitors whose client is
// older than that field, or that still prefixes the legacy "World visitor · ".
inline bool isTransientVisitor(const QString &accountKind, const QString &name)
{
    if (accountKind.trimmed().compare(QLatin1String("guest"),
                                      Qt::CaseInsensitive) == 0)
        return true;
    static const QRegularExpression visitorName(
        QStringLiteral("^\\s*(guest\\b|world\\s+guest\\b|world\\s+visitor\\b)"),
        QRegularExpression::CaseInsensitiveOption);
    return visitorName.match(name).hasMatch();
}

// True when a transient visitor last seen at `lastSeenMs` has been silent long
// enough to forget. A peer we have never actually seen (lastSeenMs <= 0) is
// left alone: the caller has no idea how long it has been idle.
inline bool visitorIsIdle(qint64 lastSeenMs, qint64 nowMs)
{
    return lastSeenMs > 0 && nowMs - lastSeenMs > kVisitorIdleMs;
}

} // namespace ChatVisitorPresence
