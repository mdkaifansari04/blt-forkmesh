#!/usr/bin/env python3
"""Truthful live-data and scene contracts for World mirror cabinets."""

import json
from pathlib import Path
import subprocess


ROOT = Path(__file__).resolve().parents[1]
WORLD = ROOT / "public" / "world"
APP = (WORLD / "world.js").read_text(encoding="utf-8")
SCENE = (WORLD / "world-scene.js").read_text(encoding="utf-8")
MODULE = WORLD / "world-mirror-nodes.js"


def test_live_node_builder_preserves_signed_repo_facts_and_unknown_telemetry():
    script = f"""
      import {{ buildLiveMirrorNodes }} from {json.dumps(MODULE.as_uri())};
      const commit = "a".repeat(40);
      const network = {{
        stats: {{ onlineNodes: ["mirror2", "mirror3"] }},
        leaderboards: {{ nodes: [
          {{ name: "mirror2", sizeBytes: 12, pullCount: 9, cpuPercent: null }},
          {{ name: "mirror3", sizeBytes: 18, pullCount: 4 }}
        ] }}
      }};
      const mirrors = {{
        owner: "mirror2",
        repo: "forkmesh",
        requestedOwner: "forkmesh",
        requestedRepo: "forkmesh",
        mirrors: [
          {{
            node: "mirror2", status: "online", integrity: "ok",
            cloneAvailable: true, commit, branch: "main", sizeBytes: 44,
            issueCount: 7, commitCount: 80, branchCount: 3, pullCount: 5,
            discussionCount: 2, artifactCount: 1, platform: "linux",
            version: "0.7.0", cpuPercent: 32,
            memUsedBytes: 25, memTotalBytes: 100,
            diskUsedBytes: -1, diskTotalBytes: -1
          }},
          {{
            node: "mirror3", status: "online", integrity: "ok",
            cloneAvailable: true, commit, branch: "main",
            issueCount: -1, pullCount: -1
          }}
        ]
      }};
      process.stdout.write(JSON.stringify(buildLiveMirrorNodes(network, mirrors)));
    """
    result = subprocess.run(
        [
            "node",
            "--experimental-default-type=module",
            "--input-type=module",
            "-e",
            script,
        ],
        check=True,
        text=True,
        capture_output=True,
    )
    nodes = json.loads(result.stdout)
    assert [node["name"] for node in nodes] == ["mirror2", "mirror3"]
    mirror2 = nodes[0]
    assert mirror2["commit"] == "a" * 40
    assert mirror2["branch"] == "main"
    assert mirror2["pullCount"] == 5
    assert mirror2["sizeBytes"] == 44
    assert mirror2["version"] == "0.7.0"
    assert mirror2["cpuPercent"] == 32
    assert mirror2["memoryUsedBytes"] == 25
    assert mirror2["memoryTotalBytes"] == 100
    assert "diskUsedBytes" not in mirror2
    assert "diskTotalBytes" not in mirror2
    assert mirror2["repositories"][0]["owner"] == "forkmesh"
    assert mirror2["repositories"][0]["name"] == "forkmesh"
    mirror3 = nodes[1]
    # A signed -1 means "not advertised"; a cached aggregate zero/positive must
    # not replace that explicit unknown state for this exact mirror record.
    assert "pullCount" not in mirror3
    assert "issueCount" not in mirror3
    assert "cpuPercent" not in mirror3
    assert "memoryUsedBytes" not in mirror3
    assert "diskUsedBytes" not in mirror3


def test_live_node_builder_keeps_long_names_distinct_and_rejected_routes_blocked():
    script = f"""
      import {{ buildLiveMirrorNodes }} from {json.dumps(MODULE.as_uri())};
      const prefix = "mirror-" + "x".repeat(50);
      const names = [prefix + "-one", prefix + "-two"];
      const payload = {{
        requestedOwner: "forkmesh", requestedRepo: "forkmesh",
        mirrors: names.map((node) => ({{
          node, status: "online", integrity: "rejected",
          cloneAvailable: true, commit: "b".repeat(40), branch: "main"
        }}))
      }};
      process.stdout.write(JSON.stringify(buildLiveMirrorNodes({{
        stats: {{ onlineNodes: names }}
      }}, payload)));
    """
    result = subprocess.run(
        [
            "node",
            "--experimental-default-type=module",
            "--input-type=module",
            "-e",
            script,
        ],
        check=True,
        text=True,
        capture_output=True,
    )
    nodes = json.loads(result.stdout)
    assert len(nodes) == 2
    assert nodes[0]["name"] != nodes[1]["name"]
    assert all(node["healthy"] is False for node in nodes)
    assert all(node["cloneAvailable"] is False for node in nodes)


def test_world_fetches_the_flagship_mirror_snapshot_once_and_uses_cabinets():
    # One bootstrap fetch plus one bounded visible-page HTTPS poller. This data
    # never travels on the multiplayer socket.
    assert APP.count('this.fetchJSON("/api/repo/forkmesh/forkmesh/mirrors"') == 1
    assert '"/api/repo/forkmesh/forkmesh/mirrors",' in APP
    assert "const MIRROR_STATUS_POLL_MS = 30 * 1000" in APP
    assert "document.visibilityState !== \"visible\"" in APP
    assert "startMirrorPolling()" in APP
    assert "buildLiveMirrorNodes" in APP
    assert "this.mirrorCatalogs" in APP
    assert "function createMirrorServerCabinet" in SCENE
    assert "serverPanelTexture" in SCENE
    assert "`mirror-server-cabinet:${id}`" in SCENE
    assert '"CPU"' in SCENE
    assert '"MEM"' in SCENE
    assert '"DISK"' in SCENE
    assert '"NOT SHARED"' in SCENE
    assert '"LAST COMMIT"' in SCENE
    assert "mirrorCommitSnapshot" in SCENE
    assert "COMMIT SUBJECT NOT REPORTED" in SCENE
    assert "AUTHOR NOT REPORTED" in SCENE
    assert "mirrorCommitAgeLabel" in SCENE
    assert "nodeDataKey" in SCENE
    assert ".slice(0, 64)" in SCENE
    assert "mirror-server-front-panel" in SCENE
    assert "nodeCabinet" in SCENE
    assert "data-world-mirror-node" in APP
    assert "data-world-mirror-node-detail" in APP
    assert "operator-reported" in APP
    assert "createMirrorNodePylon" not in SCENE
