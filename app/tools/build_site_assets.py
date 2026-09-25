#!/usr/bin/env python3

from __future__ import annotations

import fcntl
import os
import shutil
import sys
from contextlib import contextmanager
from pathlib import Path


REPOSITORY = Path(__file__).resolve().parents[2]
APP = REPOSITORY / "app"
WORLD = REPOSITORY / "world"

# App-owned site chrome the World Worker ships beside its own pages.
SHARED_FILES = (
    "404.html",
    "api-client.js",
    "auth-page-theme.css",
    "mesh-page-theme.css",
    "posthog.js",
    "site-footer.css",
    "site-footer.js",
    "site-header.css",
    "site-header.js",
    "static-page.js",
    "styles.css",
    "tailwind.css",
)

WORLD_APP_MODULES = (
    "account-events.js",
    "chat-attachments.js",
    "chat-crypto.js",
    "chat-moderation.js",
    "chat-room-transport.js",
)


def reset(path: Path) -> None:
    shutil.rmtree(path, ignore_errors=True)
    path.mkdir(parents=True)


def copy_file(source: Path, destination: Path) -> None:
    destination.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(source, destination)


def copy_tree(source: Path, destination: Path) -> None:
    """Copy a directory tree into ``destination`` (replaced if present)."""
    if not source.is_dir():
        raise FileNotFoundError(f"source tree missing: {source}")
    if destination.exists():
        shutil.rmtree(destination)
    destination.parent.mkdir(parents=True, exist_ok=True)
    shutil.copytree(source, destination)


@contextmanager
def _stage_lock(destination: Path):
    """Serialize staging for a dist dir so wrangler rebuilds cannot race."""
    destination.parent.mkdir(parents=True, exist_ok=True)
    lock_path = destination.parent / f".{destination.name}.build.lock"
    with open(lock_path, "a+", encoding="utf-8") as handle:
        fcntl.flock(handle.fileno(), fcntl.LOCK_EX)
        try:
            yield
        finally:
            fcntl.flock(handle.fileno(), fcntl.LOCK_UN)


def _publish_staged(staging: Path, destination: Path) -> None:
    """Replace ``destination`` with ``staging`` via rename (near-atomic)."""
    previous = destination.parent / f".{destination.name}.prev.{os.getpid()}"
    shutil.rmtree(previous, ignore_errors=True)
    if destination.exists():
        # Rename aside first so readers never see a half-deleted tree.
        destination.rename(previous)
    staging.rename(destination)
    shutil.rmtree(previous, ignore_errors=True)


def stage_tree(destination: Path, populate) -> Path:
    """Build into a private staging dir, then publish under a process lock."""
    with _stage_lock(destination):
        staging = destination.parent / f".{destination.name}.staging.{os.getpid()}"
        shutil.rmtree(staging, ignore_errors=True)
        staging.mkdir(parents=True)
        try:
            populate(staging)
            _publish_staged(staging, destination)
        except Exception:
            shutil.rmtree(staging, ignore_errors=True)
            raise
    return destination


def shared_assets(destination: Path) -> None:
    public = APP / "public"
    for name in SHARED_FILES:
        copy_file(public / name, destination / name)
    copy_tree(public / "favicon", destination / "favicon")
    copy_tree(public / "assets", destination / "assets")


def write_headers(destination: Path, worker: str) -> None:
    source = (APP / "public" / "_headers").read_text(encoding="utf-8")
    source += f"\n/*\n  x-forkmesh-worker: {worker}\n"
    (destination / "_headers").write_text(source, encoding="utf-8")


def _populate_app(destination: Path) -> None:
    # Fresh staging dir: copy public contents into it (not onto a wiped path).
    for path in sorted((APP / "public").iterdir()):
        target = destination / path.name
        if path.is_dir():
            copy_tree(path, target)
        else:
            copy_file(path, target)
    write_headers(destination, "app")


def _populate_world(destination: Path) -> None:
    for path in sorted((WORLD / "public").iterdir()):
        target = destination / path.name
        if path.is_dir():
            copy_tree(path, target)
        else:
            copy_file(path, target)
    shared_assets(destination)
    for name in WORLD_APP_MODULES:
        copy_file(APP / "public" / name, destination / name)
    write_headers(destination, "world")


def stage_app() -> Path:
    return stage_tree(APP / "dist", _populate_app)


def stage_world() -> Path:
    return stage_tree(WORLD / "dist", _populate_world)


def stage_single() -> Path:
    return stage_app()


def main(arguments: list[str]) -> int:
    stagers = {
        "app": stage_app,
        "single": stage_single,
        "world": stage_world,
    }
    targets = arguments[1:] or list(stagers)
    for target in targets:
        stager = stagers.get(target)
        if stager is None:
            print(f"unknown site target: {target}", file=sys.stderr)
            return 2
        destination = stager()
        files = sum(path.is_file() for path in destination.rglob("*"))
        print(f"staged {target}: {files} files in {destination.relative_to(REPOSITORY)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
