import importlib.util
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SCENE = (ROOT.parent / "world" / "public" / "world" / "world-scene.js").read_text(
    encoding="utf-8"
)
ENTRY = (ROOT / "src" / "entry.py").read_text(encoding="utf-8")


def _generator():
    path = ROOT / "tools" / "build_worker_footprint.py"
    spec = importlib.util.spec_from_file_location("build_worker_footprint", path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def test_worker_footprint_budgets_match_current_source_tree():
    generator = _generator()
    data = generator.footprint()
    assert data["attachedPythonBytes"] > data["estimatedStartupSourceBytes"]
    assert data["onDemandSourceBytes"] > 100_000
    assert data["staticAssetBytes"] > data["attachedPythonBytes"]
    assert data["staticAssetCount"] < data["staticLimits"]["assetCount"]
    assert (
        data["largestStaticAsset"]["bytes"]
        < data["staticLimits"]["maxAssetBytes"]
    )
    assert (
        data["initialWorldModuleBytes"]
        < data["staticLimits"]["initialWorldModuleBytesSoft"]
    )
    assert "world/world-office.js" not in data["initialWorldModules"]
    assert "world/world-office-meeting.js" not in data["initialWorldModules"]
    assert "world/world-office-tasks.js" not in data["initialWorldModules"]
    assert "world/world-discord-panel.js" not in data["initialWorldModules"]
    assert "qr.js" not in data["initialWorldModules"]
    assert "world/world-scene.js" not in data["initialWorldModules"]
    # Reachable only through the scene builder, so they leave with it.
    assert "world/world-sky.js" not in data["initialWorldModules"]
    assert "world/worker-footprint.js" not in data["initialWorldModules"]
    assert any(
        item["name"] == "repository_imports.py"
        and item["phase"] == "on-demand"
        for item in data["modules"]
    )
    assert any(
        item["name"] == "world_infrastructure.py"
        and item["phase"] == "on-demand"
        for item in data["modules"]
    )
    assert sum(item["bytes"] for item in data["components"]) == (
        data["attachedPythonBytes"]
    )
    assert data["workerLimits"] == {
        "memoryBytes": 128_000_000,
        "compressedBundleFreeBytes": 3_000_000,
        "compressedBundlePaidBytes": 10_000_000,
        "uncompressedBundleBytes": 64_000_000,
        "startupTimeMs": 1000,
        "startupSourceBytesSoft": 2_150_000,
        "dynamicRequestsFreeDaily": 100_000,
    }
    assert data["staticLimits"] == {
        "assetCount": 20_000,
        "maxAssetBytes": 25 * 1024 * 1024,
        "initialWorldModuleBytesSoft": 2_650_000,
    }


def test_worker_footprint_tool_is_read_only():
    """Deploy validation must never rewrite the committed chart asset."""
    generator = _generator()
    before = generator.OUTPUT.read_bytes()
    generator.validate_budgets()
    assert generator.OUTPUT.read_bytes() == before
    assert not hasattr(generator, "build")


def test_material_growth_still_refreshes_the_committed_chart():
    generator = _generator()
    doubled = generator.footprint()
    doubled["attachedPythonBytes"] *= 2
    assert generator.needs_refresh(doubled)
    renamed = generator.footprint()
    renamed["modules"][0]["name"] = "brand_new_module.py"
    assert generator.needs_refresh(renamed)
    rebar = generator.footprint()
    rebar["components"][0]["bytes"] *= 2
    assert generator.needs_refresh(rebar)


def test_provider_import_module_is_loaded_only_when_its_routes_need_it():
    eager_imports = ENTRY[: ENTRY.index("def _repository_import_module():")]
    assert "import repository_imports" not in eager_imports
    assert "def _repository_import_module():" in ENTRY
    assert "repository_import = _repository_import_module()" in ENTRY
    assert "import world_infrastructure" not in eager_imports
    assert "def _world_infrastructure_module():" in ENTRY


def test_optional_python_route_modules_are_deferred_from_global_scope():
    """Keep Cloudflare's Python startup validation below its memory ceiling."""
    eager_imports = ENTRY[: ENTRY.index("def _repository_import_module():")]
    deferred = (
        "activitypub",
        "activitypub_threads",
        "badges",
        "blog_feed",
        "catalog",
        "chat_channels_api",
        "chat_direct_messages_api",
        "community_ads_api",
        "contributions",
        "edge_routing",
        "events",
        "fediverse_digest",
        "fediverse_mentions_api",
        "git_http",
        "notes",
        "og_card",
        "organization_discord",
        "organization_succession_api",
        "polar_integration",
        "releases",
        "reward_policy",
        "security_controls",
        "security_scan_ingest",
        "solana",
        "static_routes",
        "urls",
        "schema",
        "world",
        "world_build_board",
        "world_community_api",
        "world_element_store",
        "world_events_api",
        "world_link_kiosk",
        "world_office_tasks",
        "world_satellites",
        "world_social_feeds",
        "world_visitors",
        "world_workshops",
    )
    for module in deferred:
        assert f'_LazyModule("{module}")' in eager_imports
        assert f"import {module}" not in eager_imports
    assert '_LazyModule("admin_console")' in ENTRY
    assert "import admin_console" not in ENTRY


def test_heavy_stdlib_modules_are_deferred_from_global_scope():
    """Pyodide must not allocate optional stdlib modules during validation."""
    eager_imports = ENTRY[: ENTRY.index("MAX_ROOM_NAME =")]
    for module in (
        "base64", "gzip", "io", "ipaddress", "math", "struct", "time",
        "asyncio", "hashlib", "hmac", "json", "traceback", "urllib.parse",
    ):
        assert f'_LazyModule("{module}")' in eager_imports
    for statement in (
        "import asyncio", "import base64", "import gzip", "import hashlib",
        "import hmac", "import io", "import ipaddress", "import json",
        "import math", "import struct", "import time", "import traceback",
        "from urllib.parse import",
    ):
        assert statement not in eager_imports


def test_infrastructure_room_renders_detailed_honest_footprint_chart():
    assert 'from "./worker-footprint.js"' in SCENE
    assert "WORKER STARTUP FOOTPRINT" in SCENE
    assert "EDGE STATIC (NOT HEAP)" in SCENE
    assert "SOURCE BYTES ≠ HEAP" in SCENE
    assert 'display.name = "forkmesh-infrastructure-worker-footprint"' in SCENE
    assert "INFRASTRUCTURE_FOOTPRINT_POSITION" in SCENE
    assert "WORKER COMPONENT + FREE PLAN MAP" in SCENE
    assert "COMPRESSED BUNDLE" in SCENE
    assert "MEMORY" in SCENE
    assert "DYNAMIC REQUESTS" in SCENE
    assert "initialWorldModuleBytes" in SCENE
    assert 'display.name = "forkmesh-infrastructure-worker-components"' in SCENE
    assert "INFRASTRUCTURE_COMPONENTS_POSITION" in SCENE


def test_office_runtime_is_outside_the_initial_world_module_graph():
    world = (ROOT.parent / "world" / "public" / "world" / "world.js").read_text(encoding="utf-8")
    assert 'import("./world-office.js")' in world
    assert 'import("./world-office-meeting.js")' in world
    assert 'import("./world-office-tasks.js")' in world
    assert 'from "./world-office.js"' not in world
    assert 'from "./world-office-meeting.js"' not in world
    assert 'from "./world-office-tasks.js"' not in world
    assert 'from "./world-office.js"' not in SCENE


def test_scene_builder_streams_beside_three_instead_of_blocking_the_shell():
    """The renderer cannot run before three.js, so it must not block first paint."""
    world = (ROOT.parent / "world" / "public" / "world" / "world.js").read_text(encoding="utf-8")
    assert 'const WORLD_SCENE_MODULE = import("./world-scene.js");' in world
    assert 'from "./world-scene.js"' not in world
    # Started on the same tick as three.js, and awaited together with it.
    assert world.index("const WORLD_SCENE_MODULE") > world.index(
        "const THREE_MODULE = import(THREE_MODULE_URL);"
    )
    assert "Promise.all([THREE_MODULE, WORLD_SCENE_MODULE])" in world
    assert "this.world = scene.createWorldScene({" in world
    # The account badge paints behind the loading curtain, so its painter stays
    # in the shell's own graph rather than dragging the scene back in with it.
    assert (
        'import { proceduralAvatarFaceDataURL } from "./world-avatar-face.js";'
        in world
    )


def test_wallet_qr_encoder_is_outside_the_initial_world_module_graph():
    world = (ROOT.parent / "world" / "public" / "world" / "world.js").read_text(encoding="utf-8")
    assert 'import("../qr.js")' in SCENE
    assert 'import "../qr.js"' not in SCENE
    assert "QR_MODULE_READY" in world


def test_idle_deploy_watch_does_not_burn_the_free_request_budget():
    world = (ROOT.parent / "world" / "public" / "world" / "world.js").read_text(encoding="utf-8")
    assert "const WORLD_DEPLOY_STATUS_IDLE_MS = 60 * 1000;" in world
    assert 'state === "deploying"' in world
    assert "? WORLD_DEPLOY_STATUS_POLL_MS" in world
    assert ": WORLD_DEPLOY_STATUS_IDLE_MS" in world
    assert "this.deployStatusTimer = window.setTimeout(" in world
