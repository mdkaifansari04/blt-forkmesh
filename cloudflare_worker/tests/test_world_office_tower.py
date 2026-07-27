#!/usr/bin/env python3
"""Runtime contracts for the unified ten-storey World office campus."""

import json
from pathlib import Path
import subprocess


ROOT = Path(__file__).resolve().parents[1]
TOWER_MODULE = ROOT / "public" / "world" / "world-office-tower.js"


def run_tower_script(body):
    """Run a small assertion probe against the browser-independent ES module."""

    script = f"""
      import * as tower from {json.dumps(TOWER_MODULE.as_uri())};
      {body}
    """
    result = subprocess.run(
        ["node", "--input-type=module", "-e", script],
        check=True,
        text=True,
        capture_output=True,
    )
    return json.loads(result.stdout)


def test_tower_has_ten_floors_and_is_about_ten_times_the_old_width():
    assert TOWER_MODULE.exists()
    result = run_tower_script(
        """
        process.stdout.write(JSON.stringify({
          count: tower.OFFICE_FLOOR_COUNT,
          width: tower.OFFICE_WIDTH,
          floorHeight: tower.OFFICE_FLOOR_HEIGHT,
          oldWidthRatio: tower.OFFICE_WIDTH / 17,
          towerHeight: tower.OFFICE_TOWER_HEIGHT,
          floors: tower.OFFICE_FLOORS.map((floor) => ({
            id: floor.id,
            level: floor.level,
            team: floor.team,
            publicForMembers: floor.publicForMembers === true,
            y: tower.officeFloorY(floor.id),
          })),
        }));
        """
    )

    assert result["count"] == 10
    assert len(result["floors"]) == 10
    assert [floor["level"] for floor in result["floors"]] == list(range(10))
    assert len({floor["id"] for floor in result["floors"]}) == 10
    assert 9 <= result["oldWidthRatio"] <= 11
    assert result["floorHeight"] == 16
    assert result["towerHeight"] == 10 * 16
    assert [floor["y"] for floor in result["floors"]] == [
        level * 16 for level in range(10)
    ]

    public_floors = {
        floor["id"] for floor in result["floors"] if floor["publicForMembers"]
    }
    assert public_floors == {"lobby", "marketing", "rooftop"}
    restricted = [
        floor for floor in result["floors"] if not floor["publicForMembers"]
    ]
    assert len(restricted) == 7
    assert all(floor["team"] for floor in restricted)


def test_authenticated_members_get_three_default_floors_but_team_floors_stay_restricted():
    result = run_tower_script(
        """
        const guest = tower.normalizeOfficeFloorAccess({
          authenticated: false,
          allowedFloorIds: ["lobby", "engineering"],
        });
        const member = tower.normalizeOfficeFloorAccess({
          authenticated: true,
          account: "Alice",
        });
        const teamHintOnly = tower.normalizeOfficeFloorAccess({
          authenticated: true,
          account: "Alice",
          teams: ["engineering"],
        });
        const serverAuthorized = tower.normalizeOfficeFloorAccess({
          authenticated: true,
          account: "Alice",
          teams: ["engineering"],
          allowedFloorIds: ["engineering"],
        });
        process.stdout.write(JSON.stringify({
          guestCanLobby: tower.canAccessOfficeFloor(guest, "lobby"),
          guestCanEngineering:
            tower.canAccessOfficeFloor(guest, "engineering"),
          defaults: tower.OFFICE_FLOORS
            .filter((floor) => tower.canAccessOfficeFloor(member, floor.id))
            .map((floor) => floor.id),
          teamHintCanEngineering:
            tower.canAccessOfficeFloor(teamHintOnly, "engineering"),
          authorizedCanEngineering:
            tower.canAccessOfficeFloor(serverAuthorized, "engineering"),
          authorizedStillCannotSecurity:
            tower.canAccessOfficeFloor(serverAuthorized, "security"),
          unknownFloor: tower.canAccessOfficeFloor(serverAuthorized, "unknown"),
          account: serverAuthorized.account,
        }));
        """
    )

    assert result["guestCanLobby"] is False
    assert result["guestCanEngineering"] is False
    assert set(result["defaults"]) == {"lobby", "marketing", "rooftop"}
    # Team names are display/context data. Only the server-provided floor
    # allowlist may unlock a restricted elevator button.
    assert result["teamHintCanEngineering"] is False
    assert result["authorizedCanEngineering"] is True
    assert result["authorizedStillCannotSecurity"] is False
    assert result["unknownFloor"] is False
    assert result["account"] == "alice"


def test_office_campus_surface_contains_the_island_and_bridge_but_not_water():
    result = run_tower_script(
        """
        const avatarRadius = tower.OFFICE_AVATAR_RADIUS;
        process.stdout.write(JSON.stringify({
          islandCenter: tower.officeCampusSurfaceContains(
            tower.OFFICE_ISLAND_CENTER[0],
            tower.OFFICE_ISLAND_CENTER[2],
            avatarRadius,
          ),
          islandSafeEdge: tower.officeCampusSurfaceContains(
            tower.OFFICE_ISLAND_CENTER[0] +
              tower.OFFICE_ISLAND_RADIUS - avatarRadius,
            tower.OFFICE_ISLAND_CENTER[2],
            avatarRadius,
          ),
          beyondIslandEdge: tower.officeCampusSurfaceContains(
            tower.OFFICE_ISLAND_CENTER[0] +
              tower.OFFICE_ISLAND_RADIUS - avatarRadius + 0.01,
            tower.OFFICE_ISLAND_CENTER[2],
            avatarRadius,
          ),
          bridgeCenter: tower.officeCampusSurfaceContains(0, -97, avatarRadius),
          bridgeSafeEdge: tower.officeCampusSurfaceContains(
            tower.OFFICE_BRIDGE_WIDTH / 2 - avatarRadius,
            -97,
            avatarRadius,
          ),
          beyondBridgeRail: tower.officeCampusSurfaceContains(
            tower.OFFICE_BRIDGE_WIDTH / 2 - avatarRadius + 0.01,
            -97,
            avatarRadius,
          ),
          mainShoreOverlap: tower.officeCampusSurfaceContains(0, -87, 0),
          waterBesideBridge: tower.officeCampusSurfaceContains(25, -97, 0),
          beforeBridge: tower.officeCampusSurfaceContains(0, -84, 0),
          invalid: tower.officeCampusSurfaceContains(Number.NaN, -97, 0),
        }));
        """
    )

    assert result == {
        "islandCenter": True,
        "islandSafeEdge": True,
        "beyondIslandEdge": False,
        "bridgeCenter": True,
        "bridgeSafeEdge": True,
        "beyondBridgeRail": False,
        "mainShoreOverlap": True,
        "waterBesideBridge": False,
        "beforeBridge": False,
        "invalid": False,
    }


def test_office_interior_bounds_reserve_avatar_clearance_from_every_wall():
    result = run_tower_script(
        """
        const radius = tower.OFFICE_AVATAR_RADIUS;
        const maxX = tower.OFFICE_WIDTH / 2 - radius - 0.65;
        const maxZ = tower.OFFICE_DEPTH / 2 - radius - 0.65;
        process.stdout.write(JSON.stringify({
          center: tower.officeInteriorContains(0, 0, radius),
          positiveXEdge: tower.officeInteriorContains(maxX, 0, radius),
          negativeXEdge: tower.officeInteriorContains(-maxX, 0, radius),
          positiveZEdge: tower.officeInteriorContains(0, maxZ, radius),
          negativeZEdge: tower.officeInteriorContains(0, -maxZ, radius),
          outsideX: tower.officeInteriorContains(maxX + 0.01, 0, radius),
          outsideZ: tower.officeInteriorContains(0, maxZ + 0.01, radius),
          invalid: tower.officeInteriorContains(Number.NaN, 0, radius),
        }));
        """
    )

    assert result["center"] is True
    assert result["positiveXEdge"] is True
    assert result["negativeXEdge"] is True
    assert result["positiveZEdge"] is True
    assert result["negativeZEdge"] is True
    assert result["outsideX"] is False
    assert result["outsideZ"] is False
    assert result["invalid"] is False


def test_front_elevator_cabin_and_approach_are_walkable_but_glass_sides_are_solid():
    result = run_tower_script(
        """
        const radius = tower.OFFICE_AVATAR_RADIUS;
        const centerX = tower.OFFICE_ELEVATOR_CENTER_X;
        const centerZ = tower.OFFICE_ELEVATOR_CENTER_Z;
        process.stdout.write(JSON.stringify({
          centerX,
          centerZ,
          frontZ: tower.OFFICE_FRONT_Z,
          cabinCenter: tower.officeElevatorCabinContains(
            centerX,
            centerZ,
            radius,
          ),
          cabinCenterWalkable: tower.officeInteriorPointIsWalkable(
            "lobby",
            centerX,
            centerZ,
            radius,
          ),
          interiorApproachWalkable: tower.officeInteriorPointIsWalkable(
            "lobby",
            centerX,
            centerZ - 5,
            radius,
          ),
          rearDoorOpen: !tower.officePointHitsObstacle(
            "lobby",
            centerX,
            centerZ - 4,
            radius,
          ),
          leftGlassBlocked: tower.officePointHitsObstacle(
            "lobby",
            centerX - 5,
            centerZ,
            radius,
          ),
          rightGlassBlocked: tower.officePointHitsObstacle(
            "lobby",
            centerX + 5,
            centerZ,
            radius,
          ),
          outwardGlassBlocked: tower.officePointHitsObstacle(
            "lobby",
            centerX,
            centerZ + 4,
            radius,
          ),
          pastCabin: tower.officeElevatorCabinContains(
            centerX,
            centerZ + 5,
            radius,
          ),
        }));
        """
    )

    assert result["centerX"] == 70
    # Centering the eight-unit-deep cabin on the facade leaves half of it
    # outdoors while its rear door opens onto the office floor.
    assert result["centerZ"] >= result["frontZ"] - 0.5
    assert result["cabinCenter"] is True
    assert result["cabinCenterWalkable"] is True
    assert result["interiorApproachWalkable"] is True
    assert result["rearDoorOpen"] is True
    assert result["leftGlassBlocked"] is True
    assert result["rightGlassBlocked"] is True
    assert result["outwardGlassBlocked"] is True
    assert result["pastCabin"] is False


def test_every_floor_has_collidable_obstacles_and_reachable_open_space():
    result = run_tower_script(
        """
        const obstacleSamples = {
          lobby: [-18, -2],
          marketing: [0, 0],
          engineering: [0, 0],
              "product-design": [-12, 0],
          security: [0, -4],
          infrastructure: [-50, 0],
          community: [0, -3],
          partnerships: [0, 0],
          operations: [0, 0],
              rooftop: [0, 8],
        };
        const floors = tower.OFFICE_FLOORS.map((floor) => {
          const blocked = obstacleSamples[floor.id];
          return {
            id: floor.id,
            obstacleHit: tower.officePointHitsObstacle(
              floor.id,
              blocked[0],
              blocked[1],
            ),
            obstacleNotWalkable: !tower.officeInteriorPointIsWalkable(
              floor.id,
              blocked[0],
              blocked[1],
            ),
            openSpaceWalkable: tower.officeInteriorPointIsWalkable(
              floor.id,
              70,
              35,
            ),
          };
        });
        process.stdout.write(JSON.stringify({
          floors,
          expandedMarketingObstacle: tower.officePointHitsObstacle(
            "marketing",
            9 + tower.OFFICE_AVATAR_RADIUS - 0.01,
            0,
          ),
          pastMarketingObstacle: tower.officePointHitsObstacle(
            "marketing",
            9 + tower.OFFICE_AVATAR_RADIUS + 0.01,
            0,
          ),
        }));
        """
    )

    assert len(result["floors"]) == 10
    assert all(floor["obstacleHit"] for floor in result["floors"])
    assert all(floor["obstacleNotWalkable"] for floor in result["floors"])
    assert all(floor["openSpaceWalkable"] for floor in result["floors"])
    assert result["expandedMarketingObstacle"] is True
    assert result["pastMarketingObstacle"] is False
