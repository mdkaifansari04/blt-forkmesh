#!/usr/bin/env python3
"""World catalog truth and selectable repository-entity graph regressions."""

import json
from pathlib import Path
import subprocess


ROOT = Path(__file__).resolve().parents[1]
WORLD = ROOT / "public" / "world"
APP = (WORLD / "world.js").read_text(encoding="utf-8")
DATA = (WORLD / "world-data.js").read_text(encoding="utf-8")
SCENE = (WORLD / "world-scene.js").read_text(encoding="utf-8")
GRAPH_MODULE = WORLD / "world-repository-graph.js"


def test_production_world_has_no_sample_repository_fallback_or_runnable_fake():
    assert "SAMPLE_REPOSITORIES" not in APP
    assert "SAMPLE_REPOSITORIES" not in DATA
    assert "sample portals are shown" not in APP
    assert "repositoryCatalogState" in APP
    assert "Repository catalog unavailable" in APP
    assert "No authorized repositories listed" in APP
    assert "will not run a workshop against sample or guessed" in APP
    assert 'runnable ? "" : "disabled"' in APP
    assert "if (!this.repositories.length)" in APP
    assert "this.world.updateRepositoryGraph?.([], [])" in APP


def test_graph_fixture_builds_distinct_commit_matched_contributor_issue_pr_nodes():
    script = f"""
      import {{ buildRepositoryGraphEntities }} from {json.dumps(GRAPH_MODULE.as_uri())};
      const commit = "a".repeat(40);
      const active = {{
        owner: "alice",
        repo: "widget",
        commit,
        entries: [
          {{ path: "src/app.js", contributor: "Alice" }},
          {{ path: ".forkmesh", contributor: "Bob" }},
          {{ path: "pulls", contributor: "Bob" }}
        ],
        stats: {{
          commit,
          contributors: [
            {{ name: "Alice", commits: 3 }},
            {{ name: "Bob", commits: 2 }}
          ]
        }},
        counts: {{ issues: 1, pulls: 1 }},
        entityRecords: {{
          commit,
          issues: [{{ number: 12, path: ".forkmesh/issues/open/12", state: "open" }}],
          pulls: [{{ number: 7, path: "pulls/7" }}]
        }}
      }};
      const nodes = buildRepositoryGraphEntities(active);
      const kinds = nodes.map((node) => node.kind);
      if (!kinds.includes("contributor") || !kinds.includes("issue") ||
          !kinds.includes("pull-request")) process.exit(2);
      if (new Set(nodes.map((node) => node.id)).size !== nodes.length) process.exit(3);
      const alice = nodes.find((node) => node.label === "Alice");
      const issue = nodes.find((node) => node.kind === "issue");
      const pull = nodes.find((node) => node.kind === "pull-request");
      if (!alice.targetPaths.includes("src/app.js")) process.exit(4);
      if (issue.targetPaths[0] !== ".forkmesh/issues/open/12") process.exit(5);
      if (pull.targetPaths[0] !== "pulls/7") process.exit(6);
      process.stdout.write(JSON.stringify(nodes));
    """
    result = subprocess.run(
        ["node", "--experimental-default-type=module", "--input-type=module",
         "-e", script],
        check=True,
        text=True,
        capture_output=True,
    )
    nodes = json.loads(result.stdout)
    assert len(nodes) == 4
    assert len({node["id"] for node in nodes}) == 4


def test_entity_records_fail_closed_on_commit_mismatch_and_remain_bounded():
    script = f"""
      import {{ buildRepositoryGraphEntities }} from {json.dumps(GRAPH_MODULE.as_uri())};
      const active = {{
        owner: "alice", repo: "widget", commit: "a".repeat(40),
        entries: [], stats: {{ commit: "b".repeat(40), contributors: [{{name:"Leak"}}] }},
        counts: {{ issues: 2, pulls: 3 }},
        entityRecords: {{
          commit: "b".repeat(40),
          issues: Array.from({{length: 100}}, (_, index) => ({{
            number: index + 1, path: `.forkmesh/issues/open/${{index + 1}}`
          }})),
          pulls: [{{ number: 1, path: "pulls/1" }}]
        }}
      }};
      process.stdout.write(JSON.stringify(buildRepositoryGraphEntities(active)));
    """
    result = subprocess.run(
        ["node", "--experimental-default-type=module", "--input-type=module",
         "-e", script],
        check=True,
        text=True,
        capture_output=True,
    )
    nodes = json.loads(result.stdout)
    assert {node["kind"] for node in nodes} == {
        "issue-collection", "pull-request-collection"}
    assert all("Leak" not in json.dumps(node) for node in nodes)


def test_three_scene_creates_selectable_distinct_entity_meshes_and_edges():
    assert "function updateRepositoryGraph(entries = [], entities = [])" in SCENE
    assert "repository-entity-layer" in SCENE
    assert "new THREE.SphereGeometry" in SCENE
    assert "new THREE.OctahedronGeometry" in SCENE
    assert "new THREE.TorusGeometry" in SCENE
    assert "mesh.userData.graphNode" in SCENE
    assert "interactive.push(mesh)" in SCENE
    assert "entityRelationships.push([mesh, meshes[targetIndex]])" in SCENE
    assert "repositoryEntityMeshes" in SCENE
    assert "meta.graphNode" in APP
    assert "selectRepositoryGraphNode" in APP
