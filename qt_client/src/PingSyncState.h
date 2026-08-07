#pragma once

#include <QString>

// Where a desktop ping stands with the cloud (adhoc #1629).
//
// Every alert this app shows — an in-app toast, a modal, an OS notification —
// is filed on the Pings page, because the page is the *record* of what this
// machine told its operator. Some of those events also have a destination off
// the machine: a failure becomes a signed report to the relay's error log
// (ClientErrorReports / POST /api/desktop-errors), which is how an error on a
// headless node is ever seen by anybody.
//
// That second half only works when the node can reach the relay, so the page
// has to be honest about which of its rows made it out:
//
//   * a ping raised while this node was offline is deliberately local-only —
//     nothing was sent, nothing is queued, and the row says so rather than
//     pretending the mesh knows about it;
//   * a ping that *should* have synced and didn't (the relay refused it, or the
//     app closed before it was acknowledged) says that too, instead of reading
//     the same as one that landed.
//
// This header is deliberately pure (no widgets, no network): the labels, the
// persisted tokens and the restart resolution are all decided here so they can
// be tested without a relay or a screen. MainWindow owns the state machine.
namespace forkmesh {

enum class PingSync {
    // The event never had a cloud destination: an informational desktop event
    // (an action started, a node connected) is history for this machine only.
    LocalOnly,
    // Raised while this node could not reach the relay — or had no signed-in
    // account to sign a report with. Kept here, never sent.
    Offline,
    // Handed to the relay reporter and not yet acknowledged (in flight, or
    // parked behind the host's rate-limit cooldown waiting for a retry).
    Pending,
    // The relay took it: this failure exists off the machine as well.
    Synced,
    // The relay was reachable and the report did not land — the row is the only
    // copy that exists, and it says so.
    Failed,
};

// The Status column's word for a state. Short enough to sort and scan.
QString pingSyncLabel(PingSync state);

// The one-line "why" shown under the Status cell when the caller has no more
// specific reason of its own.
QString pingSyncDescription(PingSync state);

// Stable spelling for the on-disk ping journal. Unknown/absent tokens read back
// as LocalOnly, which is the state that claims the least.
QString pingSyncToken(PingSync state);
PingSync pingSyncFromToken(const QString &token);

// True while the mesh does not have this ping and something still could: the
// Pings page counts these as the "not in the cloud" rows.
bool pingSyncIsUnsynced(PingSync state);

// A ping still in flight when the app closed can never be acknowledged now —
// the reply it was waiting on died with the process, and nothing re-queues a
// report that only ever existed in memory. Loading the journal resolves those
// to Failed rather than leaving a "Syncing…" row that will never move.
PingSync pingSyncAfterRestart(PingSync stored);

// The reason string paired with pingSyncAfterRestart's promotion.
QString pingSyncRestartReason();

} // namespace forkmesh
