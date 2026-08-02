import importlib.util
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SCENE = (ROOT / "public" / "world" / "world-scene.js").read_text(
    encoding="utf-8"
)
ENTRY = (ROOT / "src" / "entry.py").read_text(encoding="utf-8")


def _generator():
    path = ROOT / "tools" / "build_worker_footprint.py"
    spec = importlib.util.spec_from_file_location("build_worker_footprint", path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def test_worker_footprint_asset_matches_current_source_tree():
    generator = _generator()
    assert not generator.needs_refresh(), (
        "world/worker-footprint.js drifted past the refresh deadband — "
        "run tools/build_worker_footprint.py")
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
        "dynamicRequestsFreeDaily": 100_000,
    }
    assert data["staticLimits"] == {
        "assetCount": 20_000,
        "maxAssetBytes": 25 * 1024 * 1024,
        "initialWorldModuleBytesSoft": 2_500_000,
    }


def test_ordinary_source_edits_do_not_rewrite_the_committed_chart():
    """wrangler.toml rebuilds this on every deploy; it must stay put."""
    generator = _generator()
    nudged = generator.footprint()
    nudged["attachedPythonBytes"] += 6_000
    nudged["estimatedStartupSourceBytes"] += 6_000
    nudged["staticAssetCount"] += 1
    nudged["modules"][0]["bytes"] += 6_000
    nudged["components"][0]["bytes"] += 6_000
    assert not generator.needs_refresh(nudged)
    assert generator.build() is False
    assert generator.committed() is not None


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
    world = (ROOT / "public" / "world" / "world.js").read_text(encoding="utf-8")
    assert 'import("./world-office.js")' in world
    assert 'import("./world-office-meeting.js")' in world
    assert 'import("./world-office-tasks.js")' in world
    assert 'from "./world-office.js"' not in world
    assert 'from "./world-office-meeting.js"' not in world
    assert 'from "./world-office-tasks.js"' not in world
    assert 'from "./world-office.js"' not in SCENE


def test_idle_deploy_watch_does_not_burn_the_free_request_budget():
    world = (ROOT / "public" / "world" / "world.js").read_text(encoding="utf-8")
    assert "const WORLD_DEPLOY_STATUS_IDLE_MS = 60 * 1000;" in world
    assert 'state === "deploying"' in world
    assert "? WORLD_DEPLOY_STATUS_POLL_MS" in world
    assert ": WORLD_DEPLOY_STATUS_IDLE_MS" in world
    assert "this.deployStatusTimer = window.setTimeout(" in world
