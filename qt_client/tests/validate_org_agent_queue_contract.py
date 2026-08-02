#!/usr/bin/env python3
"""Static guardrails for the native organization-agent FIFO/restart contract."""

from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
AGENTS = (ROOT / "src/MainWindowAgents.cpp").read_text(encoding="utf-8")
PULLS = (ROOT / "src/MainWindowPulls.cpp").read_text(encoding="utf-8")
STORE = (ROOT / "src/AgentStore.cpp").read_text(encoding="utf-8")


def between(source: str, start: str, end: str | None = None) -> str:
    begin = source.index(start)
    finish = source.index(end, begin) if end is not None else len(source)
    return source[begin:finish]


def main() -> None:
    sync = between(
        PULLS,
        "void MainWindow::performRelaySync()",
    )
    assert "drainOrgAgentJobsFor(repo);" in sync
    assert "reportCompletedOrgAgentJobs();" in sync

    drain = between(
        AGENTS,
        "void MainWindow::drainOrgAgentJobsFor(RepositoryRecord repo)",
        "void MainWindow::applyOrgAgentJobsPayload",
    )
    assert "m_orgAgentJobDrainsInFlight.contains(drainKey)" in drain
    assert "m_orgAgentJobDrainsPending.insert(drainKey)" in drain
    assert "m_orgAgentJobDrainsInFlight.remove(drainKey)" in drain

    result = between(
        AGENTS,
        "void MainWindow::reportOrgAgentJob(",
        "void MainWindow::reportCompletedOrgAgentJobs()",
    )
    success_gate = result.index("if (!ok)")
    clear_binding = result.index("clearOrgAgentBinding(localAgentId)")
    next_claim = result.rindex("drainOrgAgentJobsFor(repo);")
    assert success_gate < clear_binding < next_claim

    safety = between(
        AGENTS,
        "void MainWindow::runOrgAgentSafetyCheck(",
        "void MainWindow::persistOrgAgentBinding(",
    )
    assert safety.index("if (repoIndex < 0)") < safety.index(
        "m_repositories.at(repoIndex)"
    )

    assert 'obj["orgAgentJob"] = orgAgentJob;' in STORE
    assert 'obj.value("orgAgentJob").toObject()' in STORE
    assert "queuedSessionIdsOldestFirst" in STORE


if __name__ == "__main__":
    main()
