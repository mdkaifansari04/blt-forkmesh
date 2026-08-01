#pragma once

#include <QtGlobal>

#include <QString>

// Small local iCalendar export for agent usage-window reset reminders.  It has
// no calendar-account integration: the desktop opens the standard .ics file in
// the user's configured calendar application, which owns the actual event and
// its alert.
namespace UsageLimitCalendar {

// Build a VEVENT with a display alarm at resetMs (UTC). Empty when resetMs is
// not a valid wall-clock instant. providerKey/windowKey are stable internal
// identifiers; their human-readable counterparts are shown in the event.
QString eventText(const QString &providerKey, const QString &windowKey,
                  const QString &providerName, const QString &windowName,
                  qint64 resetMs, qint64 createdMs = 0);

// Atomically write the reminder under the app-data directory and return its
// absolute path. The same provider/window file is updated for later windows.
QString writeEvent(const QString &providerKey, const QString &windowKey,
                   const QString &providerName, const QString &windowName,
                   qint64 resetMs);

} // namespace UsageLimitCalendar
