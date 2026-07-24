#!/usr/bin/env python3
"""Validate and run ForkMesh's auditable completion gates."""

from __future__ import annotations

import argparse
import datetime as dt
import json
import subprocess
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
CONFIG_PATH = ROOT / "docs" / "engineering" / "quality-gates.json"


class GateError(RuntimeError):
    """A quality-gate definition or invocation is invalid."""


def load_config(path: Path = CONFIG_PATH) -> dict:
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        raise GateError(f"cannot read quality-gate config: {exc}") from exc
    if not isinstance(value, dict):
        raise GateError("quality-gate config must be a JSON object")
    return value


def _budget_map(config: dict) -> dict[str, dict]:
    budgets = config.get("performanceBudgets")
    if not isinstance(budgets, list):
        raise GateError("performanceBudgets must be a list")
    result: dict[str, dict] = {}
    for budget in budgets:
        if not isinstance(budget, dict) or not isinstance(budget.get("id"), str):
            raise GateError("every performance budget needs a string id")
        if budget["id"] in result:
            raise GateError(f"duplicate performance budget: {budget['id']}")
        maximum = budget.get("maximum")
        if not isinstance(maximum, (int, float)) or maximum <= 0:
            raise GateError(f"{budget['id']}: maximum must be positive")
        enforced_by = budget.get("enforcedBy")
        if not isinstance(enforced_by, str) or "::" not in enforced_by:
            raise GateError(f"{budget['id']}: enforcedBy must name a test")
        if not (ROOT / enforced_by.split("::", 1)[0]).is_file():
            raise GateError(f"{budget['id']}: enforcing test does not exist")
        result[budget["id"]] = budget
    return result


def validate_config(config: dict) -> None:
    if config.get("schemaVersion") != 1:
        raise GateError("unsupported quality-gate schemaVersion")
    definition = config.get("definitionOfDone")
    if not isinstance(definition, str) or not (ROOT / definition).is_file():
        raise GateError("definitionOfDone must point to a repository file")

    evidence = config.get("requiredPullRequestEvidence")
    if not isinstance(evidence, list) or len(evidence) < 5:
        raise GateError("all five pull-request evidence sections are required")
    headings = []
    for item in evidence:
        if not isinstance(item, dict) or not isinstance(item.get("heading"), str):
            raise GateError("each evidence requirement needs a heading")
        headings.append(item["heading"])
    if len(set(headings)) != len(headings):
        raise GateError("pull-request evidence headings must be unique")

    gates = config.get("gates")
    if not isinstance(gates, list) or not gates:
        raise GateError("gates must be a non-empty list")
    gate_ids: set[str] = set()
    for gate in gates:
        if not isinstance(gate, dict) or not isinstance(gate.get("id"), str):
            raise GateError("every gate needs a string id")
        gate_id = gate["id"]
        if gate_id in gate_ids:
            raise GateError(f"duplicate gate: {gate_id}")
        gate_ids.add(gate_id)
        command = gate.get("command")
        if not isinstance(command, list) or not command:
            raise GateError(f"{gate_id}: command must be a non-empty list")
        if not all(isinstance(part, str) and part for part in command):
            raise GateError(f"{gate_id}: command arguments must be strings")

    profiles = config.get("profiles")
    if not isinstance(profiles, dict) or not profiles:
        raise GateError("profiles must be a non-empty object")
    for name, profile_gates in profiles.items():
        if not isinstance(profile_gates, list) or not profile_gates:
            raise GateError(f"profile {name} must contain gates")
        unknown = set(profile_gates) - gate_ids
        if unknown:
            raise GateError(f"profile {name} has unknown gates: {sorted(unknown)}")

    _budget_map(config)
    policy = config.get("budgetChangePolicy")
    if not isinstance(policy, dict):
        raise GateError("budgetChangePolicy is required")
    waiver_dir = policy.get("waiverDirectory")
    if not isinstance(waiver_dir, str) or not (ROOT / waiver_dir).is_dir():
        raise GateError("budgetChangePolicy.waiverDirectory must exist")
    required = policy.get("requiredFields")
    if not isinstance(required, list) or not required:
        raise GateError("budgetChangePolicy.requiredFields is required")


def pull_request_evidence_errors(body: str, config: dict) -> list[str]:
    """Return missing/placeholder-only pull-request evidence headings."""
    errors: list[str] = []
    lines = body.replace("\r\n", "\n").splitlines()
    sections: dict[str, list[str]] = {}
    active: str | None = None
    required = {
        str(item["heading"]).strip().casefold(): str(item["heading"])
        for item in config["requiredPullRequestEvidence"]
    }
    for line in lines:
        if line.startswith("### "):
            normalized = line[4:].strip().casefold()
            active = normalized if normalized in required else None
            if active is not None:
                sections.setdefault(active, [])
        elif active is not None:
            sections[active].append(line)
    for key, display in required.items():
        visible = "\n".join(sections.get(key, []))
        visible = _without_html_comments(visible).strip()
        normalized = visible.casefold()
        if not visible or normalized in {"todo", "tbd", "n/a", "not applicable"}:
            errors.append(display)
    return errors


def _without_html_comments(value: str) -> str:
    while "<!--" in value:
        start = value.find("<!--")
        end = value.find("-->", start + 4)
        if end < 0:
            return value[:start]
        value = value[:start] + value[end + 3 :]
    return value


def _read_base_config(base_ref: str) -> dict | None:
    relative = CONFIG_PATH.relative_to(ROOT).as_posix()
    result = subprocess.run(
        ["git", "show", f"{base_ref}:{relative}"],
        cwd=ROOT,
        check=False,
        capture_output=True,
        text=True,
    )
    if result.returncode:
        if "does not exist" in result.stderr or "exists on disk, but not in" in result.stderr:
            return None
        raise GateError(f"cannot read base quality-gate config: {result.stderr.strip()}")
    try:
        value = json.loads(result.stdout)
    except json.JSONDecodeError as exc:
        raise GateError(f"base quality-gate config is invalid: {exc}") from exc
    return value


def _valid_waiver(budget_id: str, previous: float, requested: float, config: dict) -> None:
    policy = config["budgetChangePolicy"]
    waiver_path = ROOT / policy["waiverDirectory"] / f"{budget_id}.json"
    try:
        waiver = json.loads(waiver_path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        raise GateError(f"{budget_id}: weakening requires {waiver_path.relative_to(ROOT)}") from exc
    missing = [
        field for field in policy["requiredFields"]
        if field not in waiver or waiver[field] in ("", None, [])
    ]
    if missing:
        raise GateError(f"{budget_id}: waiver is missing {', '.join(missing)}")
    if waiver["budgetId"] != budget_id:
        raise GateError(f"{budget_id}: waiver budgetId does not match")
    if float(waiver["previousValue"]) != float(previous):
        raise GateError(f"{budget_id}: waiver previousValue does not match")
    if float(waiver["requestedValue"]) != float(requested):
        raise GateError(f"{budget_id}: waiver requestedValue does not match")
    try:
        expiry = dt.datetime.fromisoformat(str(waiver["expiresAt"]).replace("Z", "+00:00"))
    except ValueError as exc:
        raise GateError(f"{budget_id}: waiver expiresAt is not ISO-8601") from exc
    if expiry.tzinfo is None:
        raise GateError(f"{budget_id}: waiver expiresAt must include a timezone")
    if expiry <= dt.datetime.now(dt.timezone.utc):
        raise GateError(f"{budget_id}: waiver has expired")


def compare_budgets(base: dict | None, current: dict) -> None:
    if base is None:
        return
    old = _budget_map(base)
    new = _budget_map(current)
    for budget_id, prior in old.items():
        if budget_id not in new:
            _valid_waiver(budget_id, float(prior["maximum"]), float("inf"), current)
            continue
        before = float(prior["maximum"])
        after = float(new[budget_id]["maximum"])
        if after > before:
            _valid_waiver(budget_id, before, after, current)


def run_profile(config: dict, profile: str) -> None:
    try:
        selected = config["profiles"][profile]
    except KeyError as exc:
        raise GateError(f"unknown profile: {profile}") from exc
    gates = {gate["id"]: gate for gate in config["gates"]}
    for gate_id in selected:
        command = [
            sys.executable if part == "{python}" else part
            for part in gates[gate_id]["command"]
        ]
        print(f"[quality-gate] {gate_id}: {' '.join(command)}", flush=True)
        result = subprocess.run(command, cwd=ROOT, check=False)
        if result.returncode:
            raise GateError(f"{gate_id} failed with exit code {result.returncode}")


def _parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    subparsers = parser.add_subparsers(dest="command", required=True)
    subparsers.add_parser("validate")
    run = subparsers.add_parser("run")
    run.add_argument("--profile", default="core")
    compare = subparsers.add_parser("compare")
    compare.add_argument("--base-ref", required=True)
    evidence = subparsers.add_parser("pr-evidence")
    evidence.add_argument("--event", type=Path, required=True)
    return parser


def main() -> int:
    args = _parser().parse_args()
    try:
        config = load_config()
        validate_config(config)
        if args.command == "validate":
            print("quality-gate configuration is valid")
        elif args.command == "run":
            run_profile(config, args.profile)
        elif args.command == "compare":
            compare_budgets(_read_base_config(args.base_ref), config)
            print("performance-budget changes are acceptable")
        elif args.command == "pr-evidence":
            event = json.loads(args.event.read_text(encoding="utf-8"))
            body = str(event.get("pull_request", {}).get("body") or "")
            missing = pull_request_evidence_errors(body, config)
            if missing:
                raise GateError(
                    "pull-request evidence is missing or placeholder-only: "
                    + ", ".join(missing)
                )
            print("pull-request completion evidence is present")
    except (GateError, OSError, json.JSONDecodeError) as exc:
        print(f"quality-gate error: {exc}", file=sys.stderr)
        return 2
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
