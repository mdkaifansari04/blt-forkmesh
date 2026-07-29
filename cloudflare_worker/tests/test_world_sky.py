#!/usr/bin/env python3
"""Focused deterministic/runtime contracts for the isolated World sky."""

import hashlib
import json
from pathlib import Path
import subprocess


ROOT = Path(__file__).resolve().parents[1]
SKY_MODULE = ROOT / "public" / "world" / "world-sky.js"
WORLD_APP = ROOT / "public" / "world" / "world.js"
SGP4_MODULE = (
    ROOT
    / "public"
    / "world"
    / "vendor"
    / "satellite-js-7.1.0.esm.js"
)
SGP4_LICENSE = SGP4_MODULE.with_name(
    "satellite-js-7.1.0.LICENSE.md"
)


def run_sky_script(body):
    script = f"""
      import * as sky from {json.dumps(SKY_MODULE.as_uri())};
      import * as sgp4Engine from {json.dumps(SGP4_MODULE.as_uri())};
      {body}
    """
    result = subprocess.run(
        ["node", "--input-type=module", "-e", script],
        check=True,
        text=True,
        capture_output=True,
    )
    return json.loads(result.stdout)


def test_seeded_star_field_is_deterministic_bounded_and_above_the_horizon():
    result = run_sky_script(
        """
        const first = sky.generateWorldStarField(64, 12345, 760);
        const repeated = sky.generateWorldStarField(64, 12345, 760);
        const changed = sky.generateWorldStarField(64, 54321, 760);
        const capped = sky.generateWorldStarField(999999, 12345, 760);
        let minimumRadius = Infinity;
        let maximumRadius = 0;
        let minimumY = Infinity;
        for (let index = 0; index < first.count; index += 1) {
          const offset = index * 3;
          const x = first.positions[offset];
          const y = first.positions[offset + 1];
          const z = first.positions[offset + 2];
          minimumRadius = Math.min(minimumRadius, Math.hypot(x, y, z));
          maximumRadius = Math.max(maximumRadius, Math.hypot(x, y, z));
          minimumY = Math.min(minimumY, y);
        }
        process.stdout.write(JSON.stringify({
          count: first.count,
          cappedCount: capped.count,
          repeated:
            Buffer.from(first.positions.buffer).equals(
              Buffer.from(repeated.positions.buffer),
            ) &&
            Buffer.from(first.colors.buffer).equals(
              Buffer.from(repeated.colors.buffer),
            ),
          changed:
            !Buffer.from(first.positions.buffer).equals(
              Buffer.from(changed.positions.buffer),
            ),
          typed:
            first.positions instanceof Float32Array &&
            first.colors instanceof Float32Array,
          minimumRadius,
          maximumRadius,
          minimumY,
        }));
        """
    )

    assert result["count"] == 64
    assert result["cappedCount"] == 1600
    assert result["repeated"] is True
    assert result["changed"] is True
    assert result["typed"] is True
    assert 760 * 0.964 < result["minimumRadius"] <= 760
    assert 760 * 0.964 < result["maximumRadius"] <= 760.001
    assert result["minimumY"] > 0


def test_omm_snapshot_validation_deduplicates_caps_and_fails_closed():
    result = run_sky_script(
        """
        const base = {
          OBJECT_NAME: "ISS (ZARYA)",
          OBJECT_ID: "1998-067A",
          EPOCH: "2026-07-26T12:00:00Z",
          MEAN_MOTION: 15.5,
          ECCENTRICITY: 0.0004,
          INCLINATION: 51.64,
          RA_OF_ASC_NODE: 123.4,
          ARG_OF_PERICENTER: 24.5,
          MEAN_ANOMALY: 335.5,
          NORAD_CAT_ID: "25544",
          BSTAR: 0.0002,
          MEAN_MOTION_DOT: 0.00001,
          MEAN_MOTION_DDOT: 0,
        };
        const records = [
          { ...base, NORAD_CAT_ID: "bad" },
          base,
          { ...base, OBJECT_NAME: "duplicate" },
        ];
        for (let id = 30000; id < 30180; id += 1) {
          records.push({
            ...base,
            OBJECT_NAME: `SAT ${id}`,
            NORAD_CAT_ID: String(id),
          });
        }
        const normalized = sky.normalizeWorldSatelliteSnapshot({
          ok: true,
          schemaVersion: 1,
          fetchedAt: 1800000000000,
          satellites: records,
        }, sky.WORLD_SKY_MAX_SATELLITES, sgp4Engine);
        process.stdout.write(JSON.stringify({
          count: normalized.length,
          firstId: normalized[0]?.catalogId,
          duplicateCount:
            normalized.filter((record) => record.catalogId === "25544").length,
          invalidTopLevel:
            sky.normalizeWorldSatelliteSnapshot(
              { satellites: records },
              sky.WORLD_SKY_MAX_SATELLITES,
              sgp4Engine,
            ).length,
          invalidRecord: sky.prepareWorldSatelliteOmm({
            ...base,
            ECCENTRICITY: 1,
          }, sgp4Engine),
          invalidInclination: sky.prepareWorldSatelliteOmm({
            ...base,
            INCLINATION: 181,
          }, sgp4Engine),
          invalidBstar: sky.prepareWorldSatelliteOmm({
            ...base,
            BSTAR: Number.NaN,
          }, sgp4Engine),
          missingEngine:
            sky.normalizeWorldSatelliteSnapshot({
              ok: true,
              schemaVersion: 1,
              satellites: [base],
            }).length,
          max: sky.WORLD_SKY_MAX_SATELLITES,
        }));
        """
    )

    assert result["count"] == result["max"] == 160
    assert result["firstId"] == "25544"
    assert result["duplicateCount"] == 1
    assert result["invalidTopLevel"] == 0
    assert result["invalidRecord"] is None
    assert result["invalidInclination"] is None
    assert result["invalidBstar"] is None
    assert result["missingEngine"] == 0


def test_sgp4_matches_published_celestrak_vanguard_reference_vectors():
    result = run_sky_script(
        """
        // CelesTrak AIAA-2006-6753 Rev 2, Appendix D/E, satellite 00005.
        // https://celestrak.org/publications/AIAA/2006-6753/
        let initializationCalls = 0;
        let propagationCalls = 0;
        const countedEngine = {
          json2satrec(record) {
            initializationCalls += 1;
            return sgp4Engine.json2satrec(record);
          },
          sgp4(satrec, minutesSinceEpoch) {
            propagationCalls += 1;
            return sgp4Engine.sgp4(satrec, minutesSinceEpoch);
          },
        };
        const prepared = sky.prepareWorldSatelliteOmm({
          OBJECT_NAME: "VANGUARD 1",
          OBJECT_ID: "1958-002B",
          EPOCH: "2000-06-27T18:50:19.733568Z",
          MEAN_MOTION: 10.82419157,
          ECCENTRICITY: 0.1859667,
          INCLINATION: 34.2682,
          RA_OF_ASC_NODE: 348.7242,
          ARG_OF_PERICENTER: 331.7664,
          MEAN_ANOMALY: 19.3264,
          NORAD_CAT_ID: "5",
          BSTAR: 0.000028098,
          MEAN_MOTION_DOT: 0.00000023,
          MEAN_MOTION_DDOT: 0,
        }, countedEngine);
        const epoch = Date.parse("2000-06-27T18:50:19.733568Z");
        const output = { x: -1, y: -1, z: -1 };
        const atEpoch = sky.propagateWorldSatelliteOmm(
          prepared,
          epoch,
          output,
        );
        const epochPosition = { ...output };
        const afterSixHours = sky.propagateWorldSatelliteOmm(
          prepared,
          epoch + 360 * 60 * 1000,
          output,
        );
        const sixHourPosition = { ...output };
        const stale = sky.propagateWorldSatelliteOmm(
          prepared,
          epoch + 15 * 24 * 60 * 60 * 1000,
          output,
        );
        process.stdout.write(JSON.stringify({
          atEpoch,
          afterSixHours,
          stale,
          epochPosition,
          sixHourPosition,
          initializationCalls,
          propagationCalls,
        }));
        """
    )

    assert result["atEpoch"] is True
    assert result["afterSixHours"] is True
    assert result["stale"] is False
    assert result["initializationCalls"] == 1
    assert result["propagationCalls"] == 2
    expected_epoch = {
        "x": 7022.46529266,
        "y": -1400.08296755,
        "z": 0.03995155,
    }
    expected_six_hours = {
        "x": -7154.03120202,
        "y": -3783.17682504,
        "z": -3536.19412294,
    }
    for axis, expected in expected_epoch.items():
        assert abs(result["epochPosition"][axis] - expected) < 1e-5
    for axis, expected in expected_six_hours.items():
        assert abs(result["sixHourPosition"][axis] - expected) < 1e-5


def test_sky_module_keeps_rendering_and_lifecycle_work_bounded():
    source = SKY_MODULE.read_text(encoding="utf-8")
    assert "export function createWorldSky(" in source
    assert "export function generateWorldStarField(" in source
    assert "export function normalizeWorldSatelliteSnapshot(" in source
    assert "export function propagateWorldSatelliteOmm(" in source
    assert "const stars = new THREE.Points(" in source
    assert source.count("new THREE.InstancedMesh(") == 3
    assert "satellites.instanceMatrix.setUsage?.(THREE.DynamicDrawUsage)" in source
    assert "now - lastSatelliteTick < safeTickInterval" in source
    assert "const propagated = { x: 0, y: 0, z: 0 };" in source
    assert "sgp4Engine.json2satrec(" in source
    assert "prepared.sgp4(" in source
    assert "solveEccentricAnomaly" not in source
    assert "EARTH_MU_KM3_S2" not in source
    assert "transform.updateMatrix();" in source
    assert "const sunInstanceIndex = WORLD_SKY_PLANETS.length;" in source
    assert "const moonInstanceIndex = sunInstanceIndex + 1;" in source
    assert "function setDaylightMinute(" in source
    assert "sunAndMoon: 2" in source
    assert "drawCalls: 4" in source
    assert "forkmesh-world-sun-moon-glows" in source
    assert "new THREE.RingGeometry(1.08, 1.62, 28)" in source
    assert "THREE.AdditiveBlending" in source
    for method in (
        "setDaylightMinute,",
        "update,",
        "tick,",
        "dispose,",
        "getState,",
    ):
        assert method in source

    # Fetch ownership stays in world.js. This rendering module has no timers,
    # animation loop, or third-party request path of its own.
    for forbidden in (
        "fetch(",
        "XMLHttpRequest",
        "setInterval(",
        "setTimeout(",
        "requestAnimationFrame(",
        "Math.random(",
    ):
        assert forbidden not in source


def test_pinned_local_sgp4_bundle_and_license_are_integrity_locked():
    bundle = SGP4_MODULE.read_bytes()
    license_text = SGP4_LICENSE.read_text(encoding="utf-8")
    assert len(bundle) < 25_000
    assert hashlib.sha256(bundle).hexdigest() == (
        "e9b6d7c5b01611701fe567d97b498794"
        "1714e3476d38a2901bd9b43eba70ba23"
    )
    assert "satellite.js 7.1.0" in license_text
    assert "MIT License" in license_text
    assert "Copyright (C) 2013 Shashwat Kandadai" in license_text
    assert "sourceMappingURL" not in bundle.decode("utf-8")
    assert "http://" not in bundle.decode("utf-8")
    assert "https://" not in bundle.decode("utf-8")


def test_sgp4_bundle_load_is_local_deferred_and_not_world_boot_blocking():
    source = WORLD_APP.read_text(encoding="utf-8")
    index_source = (
        ROOT / "public" / "world" / "index.html"
    ).read_text(encoding="utf-8")
    scene_source = (
        ROOT / "public" / "world" / "world-scene.js"
    ).read_text(encoding="utf-8")

    assert (
        'SATELLITE_SGP4_MODULE_URL =\n'
        '  "./vendor/satellite-js-7.1.0.esm.js";'
    ) in source
    assert "import(SATELLITE_SGP4_MODULE_URL)" in source
    assert 'from "./vendor/satellite-js' not in source
    assert "satellite-js-7.1.0.esm.js" not in index_source

    load_start = source.index("  async loadSatelliteSky() {")
    load_end = source.index("\n  async loadWorldData(", load_start)
    load_body = source[load_start:load_end]
    assert load_body.index(
        'await this.fetchJSON("/api/world/satellites"'
    ) < load_body.index("await loadSatelliteSgp4Module()")
    assert "snapshot?.ok !== true" in load_body
    assert "snapshot.satellites.length === 0" in load_body
    assert "updateSatelliteSky?.(snapshot, sgp4Engine)" in load_body
    assert (
        "updateSatelliteSky: (snapshot, sgp4Engine) =>"
        in scene_source
    )
    assert "worldSky.update(snapshot, sgp4Engine)" in scene_source
