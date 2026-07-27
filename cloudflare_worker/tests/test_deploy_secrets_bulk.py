"""Focused contract tests for atomic Worker-secret publication."""

import json
import os
from pathlib import Path
import shutil
import stat
import subprocess


ROOT = Path(__file__).resolve().parents[1]
DEPLOY = ROOT / "deploy.sh"
PYWRANGLER = ROOT / "pywrangler.sh"

REQUIRED = {
    "ADMIN_PATH": "/admin-test",
    "MAILTRAP_API_TOKEN": "mail-token",
    "DATA_KEY": 'quotes-" slash-\\ tab-\t unicode-☃',
    "TREASURY_SOLANA_ADDRESS": "solana-address",
    "MIRROR_ROUTER_PUBLIC_KEY": "router-public",
    "MIRROR_ROUTER_SIGNING_SEED": "router-seed",
}


def _sandbox(tmp_path: Path) -> tuple[Path, Path, Path]:
    worker = tmp_path / "worker"
    worker.mkdir()
    shutil.copy2(DEPLOY, worker / "deploy.sh")
    shutil.copy2(PYWRANGLER, worker / "pywrangler.sh")

    env_lines = [
        "CLOUDFLARE_ACCOUNT_ID=test-account",
        "CLOUDFLARE_API_TOKEN=cloudflare-token-must-not-be-published",
        *(f"{key}={value}" for key, value in REQUIRED.items()),
        "OPTIONAL_SECRET=optional=value with spaces",
        "EMPTY_SECRET=",
    ]
    (worker / ".env.production").write_text(
        "\n".join(env_lines) + "\n", encoding="utf-8"
    )

    fake_venv = tmp_path / "fake-pywrangler"
    fake_bin = fake_venv / "bin"
    fake_bin.mkdir(parents=True)
    log = tmp_path / "calls.log"
    captured = tmp_path / "captured.json"
    fake = fake_bin / "pywrangler"
    fake.write_text(
        """#!/usr/bin/env bash
set -euo pipefail
printf '<%s>' "$@" >> "$FAKE_WRANGLER_LOG"
printf '\\n' >> "$FAKE_WRANGLER_LOG"
if [ "$1" = "secret" ] && [ "$2" = "bulk" ]; then
    payload="${@: -1}"
    python3 - "$payload" "$FAKE_WRANGLER_CAPTURE" <<'PY'
import json
import os
import shutil
import stat
import sys

mode = stat.S_IMODE(os.stat(sys.argv[1]).st_mode)
if mode != 0o600:
    raise SystemExit(f"secret payload mode is {mode:o}, expected 600")
with open(sys.argv[1], encoding="utf-8") as stream:
    json.load(stream)
shutil.copyfile(sys.argv[1], sys.argv[2])
PY
    printf '%s' "$payload" > "$FAKE_WRANGLER_CAPTURE.path"
    [ "${FAKE_WRANGLER_FAIL_BULK:-0}" != "1" ] || exit 23
elif [ "$1" = "secret" ] && [ "$2" = "list" ]; then
    python3 - "$FAKE_WRANGLER_CAPTURE" <<'PY'
import json
import sys

with open(sys.argv[1], encoding="utf-8") as stream:
    payload = json.load(stream)
print(json.dumps([{"name": key} for key in payload]))
PY
else
    echo "unexpected fake pywrangler invocation" >&2
    exit 97
fi
""",
        encoding="utf-8",
    )
    fake.chmod(fake.stat().st_mode | stat.S_IXUSR)
    return worker, log, captured


def _run_secrets(
    tmp_path: Path, *, fail_bulk: bool = False
) -> tuple[subprocess.CompletedProcess[str], Path, Path]:
    worker, log, captured = _sandbox(tmp_path)
    env = os.environ.copy()
    env.update(
        {
            "PYWRANGLER_VENV": str(tmp_path / "fake-pywrangler"),
            "FAKE_WRANGLER_LOG": str(log),
            "FAKE_WRANGLER_CAPTURE": str(captured),
            "FAKE_WRANGLER_FAIL_BULK": "1" if fail_bulk else "0",
        }
    )
    result = subprocess.run(
        ["bash", "deploy.sh", "secrets"],
        cwd=worker,
        env=env,
        text=True,
        capture_output=True,
        check=False,
    )
    return result, log, captured


def test_secrets_command_uses_one_secure_bulk_update_and_verifies_names(tmp_path):
    result, log, captured = _run_secrets(tmp_path)

    assert result.returncode == 0, result.stderr
    calls = log.read_text(encoding="utf-8").splitlines()
    assert len([line for line in calls if "<secret><bulk>" in line]) == 1
    assert len([line for line in calls if "<secret><list>" in line]) == 1
    assert all("<secret><put>" not in line for line in calls)

    payload = json.loads(captured.read_text(encoding="utf-8"))
    assert payload == {
        **REQUIRED,
        "OPTIONAL_SECRET": "optional=value with spaces",
    }
    assert "CLOUDFLARE_ACCOUNT_ID" not in payload
    assert "CLOUDFLARE_API_TOKEN" not in payload
    assert "EMPTY_SECRET" not in payload
    assert "Pushed 7 secret(s) from .env.production in one bulk update." in result.stdout
    combined_output = result.stdout + result.stderr
    for value in (*payload.values(), "cloudflare-token-must-not-be-published"):
        assert value not in combined_output

    temporary_payload = Path(
        (captured.with_suffix(captured.suffix + ".path")).read_text()
    )
    assert not temporary_payload.exists()


def test_failed_bulk_update_is_not_retried_per_secret_and_cleans_payload(tmp_path):
    result, log, captured = _run_secrets(tmp_path, fail_bulk=True)

    assert result.returncode != 0
    calls = log.read_text(encoding="utf-8").splitlines()
    assert len([line for line in calls if "<secret><bulk>" in line]) == 1
    assert all("<secret><put>" not in line for line in calls)
    assert all("<secret><list>" not in line for line in calls)
    assert "no per-secret retry was attempted" in result.stderr

    temporary_payload = Path(
        (captured.with_suffix(captured.suffix + ".path")).read_text()
    )
    assert not temporary_payload.exists()


def test_missing_required_secret_fails_before_any_bulk_mutation(tmp_path):
    worker, log, _captured = _sandbox(tmp_path)
    env_file = worker / ".env.production"
    lines = [
        line
        for line in env_file.read_text(encoding="utf-8").splitlines()
        if not line.startswith("DATA_KEY=")
    ]
    env_file.write_text("\n".join(lines) + "\n", encoding="utf-8")
    env = os.environ.copy()
    env.update(
        {
            "PYWRANGLER_VENV": str(tmp_path / "fake-pywrangler"),
            "FAKE_WRANGLER_LOG": str(log),
            "FAKE_WRANGLER_CAPTURE": str(tmp_path / "captured.json"),
        }
    )

    result = subprocess.run(
        ["bash", "deploy.sh", "secrets"],
        cwd=worker,
        env=env,
        text=True,
        capture_output=True,
        check=False,
    )

    assert result.returncode != 0
    assert "required secret(s) empty or missing" in result.stderr
    assert not log.exists()


def test_push_secrets_contains_bulk_contract_without_put_loop():
    source = DEPLOY.read_text(encoding="utf-8")
    start = source.index("push_secrets() {")
    end = source.index("\n}\n\n# Build and publish", start)
    function = source[start:end]

    assert function.count("pywrangler secret bulk") == 1
    assert "pywrangler secret put" not in function
    assert 'chmod 0600 "$secret_bulk_file"' in function
    assert "trap 'rm -f -- \"$secret_bulk_file\"' EXIT" in function
