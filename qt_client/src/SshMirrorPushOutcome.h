#pragma once

#include <QString>
#include <QStringList>

// Classify the result of the unattended SSH mirror fan-out
// (MainWindow::pushToSshMirrorRemotes) from git's --porcelain status lines.
//
// The fan-out pushes every stable ref at once — refs/heads/* and refs/tags/* —
// and deliberately keeps the update non-atomic and non-forcing: a mirror can
// legitimately advance a collaboration branch (forkmesh/pulls, an agent branch)
// before this checkout consumes it, and an unattended desktop must never rewind
// the gateway. Git then rejects exactly that one divergent ref, advances every
// other ref normally, and still exits non-zero:
//
//     To ssh://gateway/…/forkmesh-forkmesh.git
//      \trefs/heads/main:refs/heads/main\t349f2db..b0f0298
//     !\trefs/heads/pulls:refs/heads/pulls\t[rejected] (non-fast-forward)
//     Done
//     error: failed to push some refs to '…'
//     hint: Updates were rejected because a pushed branch tip is behind its
//     hint: remote counterpart. …
//
// Reading only the exit code turns that healthy steady state into a recurring
// "SSH mirror push failed" in the network log and a false "N failed" on a
// release push, while main in fact reached the whole headless fleet. So judge
// the push by its per-ref status lines instead: a rejection that only means
// "the mirror is ahead here" is benign and self-heals on the next sync once the
// source converges, whereas an unreachable gateway, a refused key, or a hook
// denial is a real failure that must stay loud.
struct SshMirrorPushOutcome {
    // Refs that moved on the gateway this push (created, fast-forwarded,
    // forced, or deleted). Refs already up to date do not count.
    int advancedRefs = 0;
    // Refs left alone because the mirror is ahead of this checkout. These are
    // not errors; the next sync retries them after the source converges.
    QStringList divergedRefs;
    // Refs rejected for a reason that is not mere divergence — a pre-receive
    // hook denial, for example.
    QStringList rejectedRefs;
    // Nothing usable came back: the gateway was unreachable, the key was
    // refused, or git died before reporting any ref status.
    bool fatal = false;

    // Did anything at all move? Drives the "pushed X to mirror Y" log line, so
    // a quiet no-op sync cadence doesn't spam it.
    bool updated() const { return advancedRefs > 0; }
    // Worth counting as a failed remote (loud log, "N failed" flash)?
    bool failed() const { return fatal || !rejectedRefs.isEmpty(); }
};

namespace SshMirrorPush {

// A '!' rejection whose reason means "the mirror already has this, or has
// moved past it" — the expected outcome for a branch another node advanced,
// or a tag the gateway already carries. Everything else ('[remote rejected]'
// from a hook, a missing ref, a permission denial) is a genuine failure.
inline bool isDivergenceRejection(const QString &summary)
{
    return summary.contains(QLatin1String("non-fast-forward")) ||
           summary.contains(QLatin1String("fetch first")) ||
           summary.contains(QLatin1String("stale info")) ||
           summary.contains(QLatin1String("already exists"));
}

// Parse `git push --porcelain` output. Each status line is
// "<flag>\t<from>:<to>\t<summary>", wrapped by a leading "To <url>" line and a
// trailing "Done"; flags are ' ' fast-forward, '+' forced, '-' deleted,
// '*' new ref, '=' already up to date, '!' rejected.
inline SshMirrorPushOutcome parsePorcelain(const QString &output, int exitCode)
{
    SshMirrorPushOutcome outcome;
    bool sawStatusLine = false;
    for (const QString &raw : output.split(QLatin1Char('\n'))) {
        const QString line =
            raw.endsWith(QLatin1Char('\r')) ? raw.chopped(1) : raw;
        // Only the tab-delimited status lines carry per-ref results; "To …",
        // "Done" and any stray chatter do not.
        const int firstTab = line.indexOf(QLatin1Char('\t'));
        if (line.isEmpty() || firstTab != 1)
            continue;
        const QChar flag = line.at(0);
        const int secondTab = line.indexOf(QLatin1Char('\t'), firstTab + 1);
        const QString refs = line.mid(firstTab + 1,
                                      secondTab < 0
                                          ? -1
                                          : secondTab - firstTab - 1);
        const QString summary =
            secondTab < 0 ? QString() : line.mid(secondTab + 1);
        // "local:remote" — name the remote side, which is what the gateway
        // and the operator's `git log` on the mirror actually show.
        const int colon = refs.indexOf(QLatin1Char(':'));
        const QString ref = colon < 0 ? refs : refs.mid(colon + 1);
        sawStatusLine = true;
        if (flag == QLatin1Char('!')) {
            if (isDivergenceRejection(summary))
                outcome.divergedRefs.append(ref);
            else
                outcome.rejectedRefs.append(ref);
        } else if (flag != QLatin1Char('=')) {
            ++outcome.advancedRefs;
        }
    }
    // A non-zero exit with no ref status at all never reached the point of
    // negotiating refs: connection refused, host key rejected, no such repo.
    outcome.fatal = exitCode != 0 && !sawStatusLine;
    return outcome;
}

// One-line, log-ready ref list: a fan-out over refs/heads/* can name dozens,
// and the network log wants the shape, not the transcript.
inline QString summariseRefs(const QStringList &refs, int maxNamed = 4)
{
    if (refs.isEmpty())
        return QString();
    if (refs.size() <= maxNamed)
        return refs.join(QStringLiteral(", "));
    return refs.mid(0, maxNamed).join(QStringLiteral(", ")) +
           QStringLiteral(" and %1 more").arg(refs.size() - maxNamed);
}

} // namespace SshMirrorPush
