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
    assert generator.OUTPUT.read_text(encoding="utf-8") == generator.rendered()
    data = generator.footprint()
    assert data["attachedPythonBytes"] > data["estimatedStartupSourceBytes"]
    assert data["onDemandSourceBytes"] > 100_000
    assert data["staticAssetBytes"] > data["attachedPythonBytes"]
    assert any(
        item["name"] == "repository_imports.py"
        and item["phase"] == "on-demand"
        for item in data["modules"]
    )


def test_provider_import_module_is_loaded_only_when_its_routes_need_it():
    eager_imports = ENTRY[: ENTRY.index("def _repository_import_module():")]
    assert "import repository_imports" not in eager_imports
    assert "def _repository_import_module():" in ENTRY
    assert "repository_import = _repository_import_module()" in ENTRY


def test_infrastructure_room_renders_detailed_honest_footprint_chart():
    assert 'from "./worker-footprint.js"' in SCENE
    assert "WORKER STARTUP FOOTPRINT" in SCENE
    assert "EDGE STATIC (NOT HEAP)" in SCENE
    assert "SOURCE BYTES ≠ HEAP" in SCENE
    assert 'display.name = "forkmesh-infrastructure-worker-footprint"' in SCENE
    assert "INFRASTRUCTURE_FOOTPRINT_POSITION" in SCENE
