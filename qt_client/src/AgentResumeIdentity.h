#pragma once

// Which prior conversation a resumed agent session can actually pick up.
//
// Claude Code and the Codex app-server both stamp their own conversation
// identity onto every normalized transcript event, and both land in the same
// per-session event list. A session handed from one CLI to the other — the
// composer's agent dropdown switches provider between runs (adhoc #76) — ends
// up with a transcript holding *both* identities, so "the newest id in this
// transcript" is the wrong answer for whichever CLI is starting now.
//
// Handing one CLI the other's id is not a harmless no-op: `claude --resume
// <codex-thread-uuid>` names a conversation Claude Code has never heard of and
// fails the whole turn. That is what turned Continue on a Codex session that
// had been switched to Claude Code into a bare "the CLI hit an error while
// running the turn", with the agent never seeing the task at all.
//
// Codex marks its events with `thread_id` (and `provider: "codex"` on the init
// event it synthesizes); Claude Code's stream-json carries `session_id` only.
// Each lookup below therefore scans backwards for the newest event *its own*
// transport produced and steps over the other's — which also means a session
// bounced back to a provider it ran under before resumes that earlier
// conversation instead of starting from nothing.

#include <QJsonObject>
#include <QList>
#include <QString>

namespace forkmesh::agents {

inline bool transcriptEventIsCodex(const QJsonObject &ev)
{
    return !ev.value(QStringLiteral("thread_id")).toString().isEmpty() ||
           ev.value(QStringLiteral("provider")).toString() ==
               QLatin1String("codex");
}

// Codex app-server threads are the native conversation identity used by the
// official IDE extension. The transport records it on every normalized event,
// so a ForkMesh restart can resume the real thread rather than replaying a
// clipped plain-text transcript into a new process.
inline QString codexResumeThreadId(const QList<QJsonObject> &events)
{
    for (auto it = events.crbegin(); it != events.crend(); ++it) {
        if (!transcriptEventIsCodex(*it))
            continue;
        QString id = it->value(QStringLiteral("thread_id")).toString();
        if (id.isEmpty())
            id = it->value(QStringLiteral("session_id")).toString();
        if (!id.isEmpty())
            return id;
    }
    return QString();
}

inline QString claudeResumeSessionId(const QList<QJsonObject> &events)
{
    for (auto it = events.crbegin(); it != events.crend(); ++it) {
        if (transcriptEventIsCodex(*it))
            continue;
        const QString id = it->value(QStringLiteral("session_id")).toString();
        if (!id.isEmpty())
            return id;
    }
    return QString();
}

} // namespace forkmesh::agents
