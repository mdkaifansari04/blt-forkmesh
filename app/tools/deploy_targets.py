#!/usr/bin/env python3

import argparse
import hashlib
import json
import os
import time
from pathlib import Path


REPOSITORY = Path(__file__).resolve().parents[2]
APP = REPOSITORY / "app"
STATE_PATH = APP / ".wrangler" / "deploy-targets.json"
TARGETS = ("app", "world", "www")


def _iter_tree(relative):
    root = REPOSITORY / relative
    if root.is_file():
        yield root
        return
    if not root.is_dir():
        return
    for path in sorted(root.rglob("*")):
        if path.is_file() and not any(
            part in {"__pycache__", "dist", "node_modules", "public_api", "src_build", ".wrangler", ".pywrangler"}
            for part in path.relative_to(REPOSITORY).parts
        ):
            yield path


def target_inputs(target):
    if target == "app":
        return (
            "app/wrangler.toml",
            "app/migrations",
            "app/public",
            "app/pyproject.toml",
            "app/pywrangler.sh",
            "app/src",
            "app/tools/build_site_assets.py",
            "app/tools/build_dashboard_assets.py",
            "app/tools/build_worker_footprint.py",
            "app/tools/build_worker_python.py",
            "app/tailwind.dashboard.config.js",
            "app/tailwind.input.css",
            "app/uv.lock",
            "www/public/404.html",
            "www/public/_headers",
            "www/public/api-client.js",
            "www/public/auth-page-theme.css",
            "www/public/home-header-auth.js",
            "www/public/mesh-page-theme.css",
            "www/public/blog.html",
            "www/public/favicon",
            "www/public/assets/fediverse-avatar.png",
            "www/public/assets/fediverse-banner.png",
            "www/public/assets/fonts",
            "www/public/assets/video",
            "www/public/assets/forkmesh-mark.svg",
            "www/public/assets/hero.mp4",
            "www/public/assets/jett.webp",
            "www/public/assets/kaif.webp",
            "www/public/assets/logo.png",
            "www/public/assets/repo-network.svg",
            "www/public/assets/sol.png",
            "www/public/noisy-background.avif",
            "www/public/index.html",
            "www/public/install.sh",
            "www/public/posthog.js",
            "www/public/site-footer.css",
            "www/public/site-footer.js",
            "www/public/site-header.css",
            "www/public/site-header.js",
            "www/public/static-page.js",
            "www/public/styles.css",
            "www/public/tailwind.css",
            "www/public/uninstall.sh",
        )
    if target == "world":
        return (
            "world/wrangler.toml",
            "world/worker.js",
            "world/public",
            "app/tools/build_site_assets.py",
            "app/public/account-events.js",
            "app/public/chat-attachments.js",
            "app/public/chat-crypto.js",
            "app/public/chat-moderation.js",
            "app/public/chat-room-transport.js",
            "www/public/404.html",
            "www/public/api-client.js",
            "www/public/auth-page-theme.css",
            "www/public/home-header-auth.js",
            "www/public/mesh-page-theme.css",
            "www/public/_headers",
            "www/public/favicon",
            "www/public/assets/fediverse-avatar.png",
            "www/public/assets/fediverse-banner.png",
            "www/public/assets/fonts",
            "www/public/assets/video",
            "www/public/assets/forkmesh-mark.svg",
            "www/public/assets/hero.mp4",
            "www/public/assets/jett.webp",
            "www/public/assets/kaif.webp",
            "www/public/assets/logo.png",
            "www/public/assets/repo-network.svg",
            "www/public/assets/sol.png",
            "www/public/noisy-background.avif",
            "www/public/posthog.js",
            "www/public/site-footer.css",
            "www/public/site-footer.js",
            "www/public/site-header.css",
            "www/public/site-header.js",
            "www/public/static-page.js",
            "www/public/styles.css",
            "www/public/tailwind.css",
        )
    if target == "www":
        return (
            "www/wrangler.toml",
            "www/worker.js",
            "www/public",
            "app/tools/build_site_assets.py",
        )
    raise ValueError(f"unknown deploy target: {target}")


def target_files(target):
    files = {}
    for relative in target_inputs(target):
        for path in _iter_tree(relative):
            files[path.relative_to(REPOSITORY).as_posix()] = path
    return [files[name] for name in sorted(files)]


def fingerprint(target):
    digest = hashlib.sha256()
    for path in target_files(target):
        relative = path.relative_to(REPOSITORY).as_posix().encode()
        data = path.read_bytes()
        digest.update(len(relative).to_bytes(4, "big"))
        digest.update(relative)
        digest.update(len(data).to_bytes(8, "big"))
        digest.update(data)
    return digest.hexdigest()


def read_state(path=None):
    path = STATE_PATH if path is None else path
    try:
        data = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, ValueError, TypeError):
        return {}
    return data if isinstance(data, dict) else {}


def write_state(state, path=None):
    path = STATE_PATH if path is None else path
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_suffix(f".tmp-{os.getpid()}")
    temporary.write_text(json.dumps(state, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    os.replace(temporary, path)


def target_status(target, state=None):
    state = read_state() if state is None else state
    current = fingerprint(target)
    deployed = state.get(target, {})
    previous = deployed.get("fingerprint") if isinstance(deployed, dict) else None
    return {
        "target": target,
        "changed": current != previous,
        "fingerprint": current,
        "deployedFingerprint": previous or "",
        "deployedRevision": deployed.get("revision", "") if isinstance(deployed, dict) else "",
        "deployedAt": deployed.get("deployedAt", 0) if isinstance(deployed, dict) else 0,
    }


def mark_deployed(target, revision="", path=STATE_PATH):
    state = read_state(path)
    state[target] = {
        "fingerprint": fingerprint(target),
        "revision": revision,
        "deployedAt": int(time.time() * 1000),
    }
    write_state(state, path)
    return state[target]


def main(argv=None):
    parser = argparse.ArgumentParser()
    commands = parser.add_subparsers(dest="command", required=True)
    status = commands.add_parser("status")
    status.add_argument("targets", nargs="*", choices=TARGETS)
    status.add_argument("--json", action="store_true")
    changed = commands.add_parser("changed")
    changed.add_argument("target", choices=TARGETS)
    fingerprint_command = commands.add_parser("fingerprint")
    fingerprint_command.add_argument("target", choices=TARGETS)
    mark = commands.add_parser("mark")
    mark.add_argument("target", choices=TARGETS)
    mark.add_argument("revision", nargs="?", default="")
    args = parser.parse_args(argv)

    if args.command == "changed":
        result = target_status(args.target)
        print("changed" if result["changed"] else "unchanged")
        return 0 if result["changed"] else 3
    if args.command == "fingerprint":
        print(fingerprint(args.target))
        return 0
    if args.command == "mark":
        print(json.dumps(mark_deployed(args.target, args.revision), sort_keys=True))
        return 0

    targets = args.targets or TARGETS
    results = [target_status(target) for target in targets]
    if args.json:
        print(json.dumps({"targets": results}, sort_keys=True))
    else:
        for result in results:
            state = "changed" if result["changed"] else "unchanged"
            print(f"{result['target']}\t{state}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
