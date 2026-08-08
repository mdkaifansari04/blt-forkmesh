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
//
// The same argument applies one level down, between two accounts of the SAME
// CLI. Each provider account is a separate config root (CLAUDE_CONFIG_DIR /
// CODEX_HOME) and every CLI keeps its conversation history inside that root, so
// a conversation started under one account simply does not exist for another —
// the usual reason to switch being that the account which was running hit its
// usage limit. `--resume` on an id the newly selected account has never seen
// fails the turn exactly like the cross-CLI mix-up above. Runs therefore stamp
// the account that produced each event (resumeAccountKey()) and the lookups skip
// conversations belonging to a different one. Events written before this stamp
// existed carry none and are treated as the current account's, which keeps a
// long-lived single-account session resuming the way it always did.

#include <QJsonObject>
#include <QList>
#include <QString>

namespace forkmesh::agents {

// Which ForkMesh provider-account profile id ran the turn this event came from.
inline QString resumeAccountKey()
{
    return QStringLiteral("_forkmesh_account");
}

inline bool transcriptEventIsCodex(const QJsonObject &ev)
{
    return !ev.value(QStringLiteral("thread_id")).toString().isEmpty() ||
           ev.value(QStringLiteral("provider")).toString() ==
               QLatin1String("codex");
}

// An empty `accountId` asks for "whichever account", which is what the hand-off
// notice needs to tell "no conversation at all" apart from "one this account
// cannot reach". An unstamped event matches every account (see above).
inline bool transcriptEventMatchesAccount(const QJsonObject &ev,
                                          const QString &accountId)
{
    if (accountId.isEmpty())
        return true;
    const QString stamped = ev.value(resumeAccountKey()).toString();
    return stamped.isEmpty() || stamped == accountId;
}

// Codex app-server threads are the native conversation identity used by the
// official IDE extension. The transport records it on every normalized event,
// so a ForkMesh restart can resume the real thread rather than replaying a
// clipped plain-text transcript into a new process.
inline QString codexResumeThreadId(const QList<QJsonObject> &events,
                                   const QString &accountId = QString())
{
    for (auto it = events.crbegin(); it != events.crend(); ++it) {
        if (!transcriptEventIsCodex(*it) ||
            !transcriptEventMatchesAccount(*it, accountId))
            continue;
        QString id = it->value(QStringLiteral("thread_id")).toString();
        if (id.isEmpty())
            id = it->value(QStringLiteral("session_id")).toString();
        if (!id.isEmpty())
            return id;
    }
    return QString();
}

inline QString claudeResumeSessionId(const QList<QJsonObject> &events,
                                     const QString &accountId = QString())
{
    for (auto it = events.crbegin(); it != events.crend(); ++it) {
        if (transcriptEventIsCodex(*it) ||
            !transcriptEventMatchesAccount(*it, accountId))
            continue;
        const QString id = it->value(QStringLiteral("session_id")).toString();
        if (!id.isEmpty())
            return id;
    }
    return QString();
}

// Which account owns the newest conversation `codexTransport`'s CLI left in this
// transcript, ignoring which account is selected now. Empty when that CLI never
// ran here, or when it ran before conversations carried an account stamp — in
// both cases there is no other account to hand the work over from, since an
// unstamped conversation is resumed as the current account's.
inline QString resumeConversationAccountId(const QList<QJsonObject> &events,
                                           bool codexTransport)
{
    for (auto it = events.crbegin(); it != events.crend(); ++it) {
        if (transcriptEventIsCodex(*it) != codexTransport)
            continue;
        const bool hasId =
            !it->value(QStringLiteral("session_id")).toString().isEmpty() ||
            !it->value(QStringLiteral("thread_id")).toString().isEmpty();
        if (hasId)
            return it->value(resumeAccountKey()).toString();
    }
    return QString();
}

} // namespace forkmesh::agents
