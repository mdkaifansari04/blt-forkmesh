#!/usr/bin/env python3
"""Generate the measured Worker/source footprint used by the World chart."""

import json
import re
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / "src"
PUBLIC = ROOT / "public"
VENDORED = ROOT / "python_modules"
OUTPUT = PUBLIC / "world" / "worker-footprint.js"
LAZY_MODULES = {"repository_imports.py", "world_infrastructure.py"}
WORKER_LIMITS = {
    "memoryBytes": 128_000_000,
    "compressedBundleFreeBytes": 3_000_000,
    "compressedBundlePaidBytes": 10_000_000,
    "uncompressedBundleBytes": 64_000_000,
    "startupTimeMs": 1000,
    "dynamicRequestsFreeDaily": 100_000,
}
STATIC_LIMITS = {
    "assetCount": 20_000,
    "maxAssetBytes": 25 * 1024 * 1024,
    # This is an intentionally stricter project budget, not a Cloudflare cap.
    # It leaves headroom for future districts without letting the first visit
    # silently inherit every optional feature module.
    "initialWorldModuleBytesSoft": 2_500_000,
}
STATIC_IMPORT_FROM_RE = re.compile(
    r"""\bfrom\s+["'](?P<path>\.{1,2}/[^"']+)["']"""
)
STATIC_IMPORT_SIDE_EFFECT_RE = re.compile(
    r"""^\s*import\s+["'](?P<path>\.{1,2}/[^"']+)["']""",
    re.MULTILINE,
)


def _component_for(name):
    """Group source modules into stable architectural concepts."""
    if name == "entry.py":
        return "Worker routing + runtime"
    if name.startswith("world"):
        return "World + Office"
    if name.startswith(("activitypub", "fediverse")):
        return "Fediverse"
    if name.startswith(("organization", "chat_")):
        return "Organizations + chat"
    if name in {
        "catalog.py",
        "edge_routing.py",
        "git_http.py",
        "mirrors.py",
        "pull_badge.py",
        "releases.py",
        "repository_imports.py",
    }:
        return "Repositories + mirrors"
    if name in {
        "badges.py",
        "reward_policy.py",
        "security_controls.py",
        "security_scan_ingest.py",
        "solana.py",
        "ssh_keys.py",
    }:
        return "Identity + security"
    if name.startswith("community_") or name in {
        "blog_feed.py",
        "contributions.py",
        "og_card.py",
    }:
        return "Community + publishing"
    return "Platform + build"


def _files(root, pattern="*"):
    return [path for path in root.rglob(pattern) if path.is_file()]


def _static_javascript_graph(roots):
    """Return local modules fetched before any dynamic import boundary."""
    pending = [path.resolve() for path in roots]
    found = {}
    while pending:
        path = pending.pop()
        if path in found or not path.is_file():
            continue
        source = path.read_text(encoding="utf-8")
        found[path] = path.stat().st_size
        imports = {
            match.group("path")
            for pattern in (STATIC_IMPORT_FROM_RE, STATIC_IMPORT_SIDE_EFFECT_RE)
            for match in pattern.finditer(source)
        }
        for imported in imports:
            target = (path.parent / imported).resolve()
            if target.suffix == "":
                target = target.with_suffix(".js")
            if target.is_relative_to(PUBLIC.resolve()):
                pending.append(target)
    return found


def footprint():
    source_files = _files(SRC, "*.py")
    vendored_files = _files(VENDORED)
    generated = OUTPUT.resolve()
    public_files = [
        path for path in _files(PUBLIC) if path.resolve() != generated
    ]
    initial_world_graph = _static_javascript_graph(
        [
            PUBLIC / "world" / "world.js",
            PUBLIC / "world" / "world-discord.js",
        ]
    )
    largest_asset = max(public_files, key=lambda path: path.stat().st_size)
    modules = [
        {
            "name": path.name,
            "bytes": path.stat().st_size,
            "phase": "on-demand" if path.name in LAZY_MODULES else "startup",
        }
        for path in source_files
    ]
    modules.sort(key=lambda item: (-item["bytes"], item["name"]))
    source_bytes = sum(item["bytes"] for item in modules)
    lazy_bytes = sum(
        item["bytes"] for item in modules if item["phase"] == "on-demand"
    )
    vendored_bytes = sum(path.stat().st_size for path in vendored_files)
    component_totals = {}
    for item in modules:
        component = _component_for(item["name"])
        component_totals[component] = (
            component_totals.get(component, 0) + item["bytes"]
        )
    components = [
        {"name": name, "bytes": size}
        for name, size in component_totals.items()
    ]
    components.append({"name": "Vendored Python runtime", "bytes": vendored_bytes})
    components.sort(key=lambda item: (-item["bytes"], item["name"]))
    return {
        "measurement": "uncompressed source bytes on disk",
        "attachedPythonBytes": source_bytes + vendored_bytes,
        "estimatedStartupSourceBytes": source_bytes - lazy_bytes + vendored_bytes,
        "onDemandSourceBytes": lazy_bytes,
        "vendoredBytes": vendored_bytes,
        "moduleCount": len(modules) + len(vendored_files),
        "staticAssetBytes": sum(path.stat().st_size for path in public_files),
        "staticAssetCount": len(public_files),
        "largestStaticAsset": {
            "name": largest_asset.relative_to(PUBLIC).as_posix(),
            "bytes": largest_asset.stat().st_size,
        },
        "initialWorldModuleBytes": sum(initial_world_graph.values()),
        "initialWorldModuleCount": len(initial_world_graph),
        "initialWorldModules": sorted(
            path.relative_to(PUBLIC).as_posix() for path in initial_world_graph
        ),
        # Keep the complete generated inventory in the asset; the Three.js
        # chart chooses its largest visible rows, while tests and future views
        # can still inspect every clearly named Python module.
        "modules": modules,
        "components": components,
        "workerLimits": WORKER_LIMITS,
        "staticLimits": STATIC_LIMITS,
        "note": (
            "Source bytes are a reproducible startup-footprint proxy, not heap. "
            "Cloudflare does not expose per-module Python heap measurements. "
            "Bundle limits apply after compression, so source-byte bars are "
            "not presented as bundle-limit utilization."
        ),
    }


def validate_budgets(data=None):
    """Fail builds before a new asset crosses a Free-plan growth boundary."""
    snapshot = data or footprint()
    errors = []
    if snapshot["staticAssetCount"] > STATIC_LIMITS["assetCount"]:
        errors.append(
            "static asset count "
            f"{snapshot['staticAssetCount']:,} exceeds "
            f"{STATIC_LIMITS['assetCount']:,}"
        )
    largest = snapshot["largestStaticAsset"]
    if largest["bytes"] > STATIC_LIMITS["maxAssetBytes"]:
        errors.append(
            f"{largest['name']} is {largest['bytes']:,} bytes; "
            f"the per-asset limit is {STATIC_LIMITS['maxAssetBytes']:,}"
        )
    if (
        snapshot["initialWorldModuleBytes"]
        > STATIC_LIMITS["initialWorldModuleBytesSoft"]
    ):
        errors.append(
            "initial World module graph is "
            f"{snapshot['initialWorldModuleBytes']:,} bytes; "
            "the project soft budget is "
            f"{STATIC_LIMITS['initialWorldModuleBytesSoft']:,}"
        )
    if errors:
        raise RuntimeError("Free-plan growth budget failed: " + "; ".join(errors))
    return snapshot


def rendered():
    payload = json.dumps(validate_budgets(), indent=2, separators=(",", ": "))
    return (
        "// Generated by tools/build_worker_footprint.py; do not hand-edit.\n"
        f"export const WORKER_FOOTPRINT = Object.freeze({payload});\n"
    )


def build():
    content = rendered()
    old = OUTPUT.read_text(encoding="utf-8") if OUTPUT.exists() else ""
    if old == content:
        return False
    OUTPUT.parent.mkdir(parents=True, exist_ok=True)
    OUTPUT.write_text(content, encoding="utf-8")
    return True


if __name__ == "__main__":
    print(
        "Built Worker footprint chart data."
        if build()
        else "Worker footprint chart data is up to date."
    )
