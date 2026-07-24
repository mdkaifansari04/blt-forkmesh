import importlib.util
import json
import subprocess
import sys
from pathlib import Path

import pytest


ROOT = Path(__file__).resolve().parents[2]
TOOL = ROOT / "tools" / "quality_gate.py"
CONFIG = ROOT / "docs" / "engineering" / "quality-gates.json"


def _module():
    spec = importlib.util.spec_from_file_location("forkmesh_quality_gate", TOOL)
    assert spec is not None and spec.loader is not None
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def test_quality_gate_config_validates_from_repository_root():
    result = subprocess.run(
        [sys.executable, str(TOOL), "validate"],
        cwd=ROOT,
        check=False,
        capture_output=True,
        text=True,
    )
    assert result.returncode == 0, result.stderr
    assert "configuration is valid" in result.stdout


def test_pull_request_evidence_rejects_placeholders_and_accepts_explanations():
    module = _module()
    config = json.loads(CONFIG.read_text(encoding="utf-8"))
    placeholder = "\n".join(
        f"### {item['heading']}\n\n<!-- still empty -->"
        for item in config["requiredPullRequestEvidence"]
    )
    assert module.pull_request_evidence_errors(placeholder, config) == [
        item["heading"] for item in config["requiredPullRequestEvidence"]
    ]

    complete = "\n".join(
        f"### {item['heading']}\n\nNot applicable — documentation-only change."
        for item in config["requiredPullRequestEvidence"]
    )
    assert module.pull_request_evidence_errors(complete, config) == []


def test_budget_increase_requires_a_current_matching_waiver(monkeypatch, tmp_path):
    module = _module()
    current = json.loads(CONFIG.read_text(encoding="utf-8"))
    prior = json.loads(CONFIG.read_text(encoding="utf-8"))
    current["performanceBudgets"][0]["maximum"] = 0.75
    monkeypatch.setattr(module, "ROOT", tmp_path)
    enforcing_path = (
        tmp_path
        / prior["performanceBudgets"][0]["enforcedBy"].split("::", 1)[0]
    )
    enforcing_path.parent.mkdir(parents=True)
    enforcing_path.write_text("# test fixture\n", encoding="utf-8")

    with pytest.raises(module.GateError, match="weakening requires"):
        module.compare_budgets(prior, current)


def test_all_gate_commands_are_argument_arrays_without_shell_execution():
    config = json.loads(CONFIG.read_text(encoding="utf-8"))
    for gate in config["gates"]:
        assert isinstance(gate["command"], list)
        assert gate["command"]
        assert all(isinstance(part, str) and part for part in gate["command"])
