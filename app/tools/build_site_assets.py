#!/usr/bin/env python3

from __future__ import annotations

import shutil
import sys
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
    if destination.exists():
        shutil.rmtree(destination)
    shutil.copytree(source, destination)


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


def stage_www() -> Path:
    destination = WWW / "dist"
    reset(destination)
    copy_tree(WWW / "public", destination)
    write_headers(destination, "www")
    return destination


def stage_app() -> Path:
    destination = APP / "dist"
    reset(destination)
    copy_tree(APP / "public", destination)
    shared_assets(destination, include_video=True)
    for name in BACKEND_ASSETS:
        copy_file(WWW / "public" / name, destination / name)
    homepage = destination / "index.html"
    homepage.write_text(
        homepage.read_text(encoding="utf-8").replace(
            "/assets/blog/intro-to-forkmesh.png", "/assets/logo.png"
        ),
        encoding="utf-8",
    )
    write_headers(destination, "app")
    return destination


def stage_cutover() -> Path:
    destination = stage_app()
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
    return destination


def stage_world() -> Path:
    destination = WORLD / "dist"
    reset(destination)
    copy_tree(WORLD / "public", destination)
    shared_assets(destination, include_video=True)
    for name in WORLD_APP_MODULES:
        copy_file(APP / "public" / name, destination / name)
    write_headers(destination, "world")
    return destination


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
