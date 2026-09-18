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
WWW = REPOSITORY / "www"
WORLD = REPOSITORY / "world"

SHARED_FILES = (
    "404.html",
    "api-client.js",
    "auth-page-theme.css",
    "home-header-auth.js",
    "mesh-page-theme.css",
    "noisy-background.avif",
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

BACKEND_ASSETS = (
    "blog.html",
    "index.html",
    "install.sh",
    "uninstall.sh",
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


def shared_assets(destination: Path, *, include_video: bool = False) -> None:
    for name in SHARED_FILES:
        copy_file(WWW / "public" / name, destination / name)
    copy_tree(WWW / "public" / "favicon", destination / "favicon")
    asset_source = WWW / "public" / "assets"
    asset_destination = destination / "assets"
    asset_destination.mkdir(parents=True, exist_ok=True)
    for path in sorted(asset_source.iterdir()):
        if path.name == "blog" or (path.name == "video" and not include_video):
            continue
        target = asset_destination / path.name
        if path.is_dir():
            copy_tree(path, target)
        else:
            copy_file(path, target)


def write_headers(destination: Path, worker: str) -> None:
    source = (WWW / "public" / "_headers").read_text(encoding="utf-8")
    source += f"\n/*\n  x-forkmesh-worker: {worker}\n"
    (destination / "_headers").write_text(source, encoding="utf-8")


def _populate_www(destination: Path) -> None:
    # Fresh staging dir: copy public contents into it (not onto a wiped path).
    for path in sorted((WWW / "public").iterdir()):
        target = destination / path.name
        if path.is_dir():
            copy_tree(path, target)
        else:
            copy_file(path, target)
    write_headers(destination, "www")


def _populate_app(destination: Path) -> None:
    for path in sorted((APP / "public").iterdir()):
        target = destination / path.name
        if path.is_dir():
            copy_tree(path, target)
        else:
            copy_file(path, target)
    shared_assets(destination, include_video=True)
    app_homepage = APP / "public" / "index.html"
    for name in BACKEND_ASSETS:
        # Prefer the BLT app landing when present; keep www homepage only as fallback.
        if name == "index.html" and app_homepage.is_file():
            continue
        copy_file(WWW / "public" / name, destination / name)
    homepage = destination / "index.html"
    if homepage.is_file():
        text = homepage.read_text(encoding="utf-8")
        if 'src="https://cdn.tailwindcss.com"' in text or \
                "src='https://cdn.tailwindcss.com'" in text:
            raise SystemExit(
                "refusing to stage a homepage that loads cdn.tailwindcss.com "
                "(blocked by CSP; use /blt-home.css)"
            )
        if "Report bugs." not in text:
            homepage.write_text(
                text.replace(
                    "/assets/blog/intro-to-forkmesh.png", "/assets/logo.png"
                ),
                encoding="utf-8",
            )
        # Worker prefers blt-home.html for `/`; keep it identical to index.html
        # so landing session UI and branding never drift between the two.
        (destination / "blt-home.html").write_text(
            homepage.read_text(encoding="utf-8"), encoding="utf-8")
    write_headers(destination, "app")


def _populate_world(destination: Path) -> None:
    for path in sorted((WORLD / "public").iterdir()):
        target = destination / path.name
        if path.is_dir():
            copy_tree(path, target)
        else:
            copy_file(path, target)
    shared_assets(destination, include_video=True)
    for name in WORLD_APP_MODULES:
        copy_file(APP / "public" / name, destination / name)
    write_headers(destination, "world")


def stage_www() -> Path:
    return stage_tree(WWW / "dist", _populate_www)


def stage_app() -> Path:
    return stage_tree(APP / "dist", _populate_app)


def stage_cutover() -> Path:
    def populate(destination: Path) -> None:
        _populate_app(destination)
        shutil.copytree(WWW / "public", destination, dirs_exist_ok=True)
        shutil.copytree(WORLD / "public", destination, dirs_exist_ok=True)
        homepage = destination / "index.html"
        homepage.write_text(
            homepage.read_text(encoding="utf-8").replace(
                "/assets/blog/intro-to-forkmesh.png", "/assets/logo.png"
            ),
            encoding="utf-8",
        )
        write_headers(destination, "app")

    return stage_tree(APP / "dist", populate)


def stage_world() -> Path:
    return stage_tree(WORLD / "dist", _populate_world)


def stage_single() -> Path:
    return stage_app()


def main(arguments: list[str]) -> int:
    stagers = {
        "app": stage_app,
        "cutover": stage_cutover,
        "single": stage_single,
        "world": stage_world,
        "www": stage_www,
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
