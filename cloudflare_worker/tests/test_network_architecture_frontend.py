#!/usr/bin/env python3
"""Static contracts for the Network command-center page."""

import re
from pathlib import Path


PUBLIC = Path(__file__).resolve().parents[1] / "public"
NETWORK_PAGES = (
    PUBLIC / "network.html",
)
STYLES = (PUBLIC / "styles.css").read_text(encoding="utf-8")
STATIC_PAGE = (PUBLIC / "static-page.js").read_text(encoding="utf-8")


def _read(page: Path) -> str:
    return page.read_text(encoding="utf-8")


def test_network_page_uses_command_center_surface():
    for page in NETWORK_PAGES:
        html = _read(page)

        assert "network-command-page" in html
        assert 'class="network-command-hero"' in html
        assert 'class="network-console"' in html
        assert 'class="network-console-metrics"' in html
        assert 'id="network-clients"' in html
        assert 'data-network-hosts' in html
        assert 'data-network-repos' in html
        assert "Storage boundary" in html
        assert 'class="network-bento-section"' not in html
        assert 'class="stats-grid network-stats"' not in html


def test_network_page_groups_operational_data_into_single_grid():
    for page in NETWORK_PAGES:
        html = _read(page)

        assert 'class="network-ops-grid"' in html
        assert 'class="network-ops-card network-ops-card-activity"' in html
        assert 'class="network-ops-card network-ops-card-wallets"' in html
        assert 'class="network-ops-card network-ops-card-leaderboards"' in html
        assert 'id="online-graph" class="online-graph network-activity-graph"' in html
        assert 'id="network-wallets" class="wallet-table network-wallet-table"' in html
        assert 'id="leaderboards" class="leaderboards network-leaderboards"' in html
        assert 'class="panel doc-panel"' not in html
        assert 'class="docs-grid"' not in html


def test_network_page_uses_compact_architecture_steps():
    for page in NETWORK_PAGES:
        html = _read(page)

        assert 'class="network-architecture-panel"' in html
        assert 'class="network-architecture-steps"' in html
        assert html.count('class="network-architecture-step"') == 6
        assert "Cloudflare coordinates the route" in html
        assert "Desktop hosts keep the code" in html
        assert 'class="control-plane-card"' not in html


def test_network_command_center_styles_exist():
    for marker in (
        ".network-command-page",
        ".network-command-hero",
        ".network-console",
        ".network-console-metrics",
        ".network-route-map",
        ".network-ops-grid",
        ".network-ops-card",
        ".network-architecture-panel",
        ".network-architecture-steps",
        ".network-architecture-step",
        "grid-template-columns: minmax(0, 1.05fr) minmax(360px, 0.95fr);",
        "grid-template-columns: minmax(0, 1.12fr) minmax(320px, 0.88fr);",
    ):
        assert marker in STYLES


def test_network_command_center_surfaces_use_theme_tokens():
    network_block = STYLES[
        STYLES.index("/* ===== Network command center ===== */"):
        STYLES.index("/* ===== Doc content ===== */")
    ]

    assert "#111212" not in network_block
    assert "rgba(9, 9, 9" not in network_block
    assert "var(--card)" in network_block
    assert "var(--card-soft)" in network_block
    assert "var(--border)" in network_block
    assert "color-mix(in srgb, var(--foreground)" in network_block


def test_network_command_center_decorative_accents_use_theme_tokens():
    network_block = STYLES[
        STYLES.index("/* ===== Network command center ===== */"):
        STYLES.index("/* ===== Doc content ===== */")
    ]

    assert "rgba(74, 222, 128" not in network_block
    assert "rgba(163, 113, 247" not in network_block
    assert "color-mix(in srgb, var(--primary)" in network_block
    assert "color-mix(in srgb, var(--purple)" in network_block


def test_network_decorative_accent_selectors_keep_private_theme_tokens():
    expected = {
        ".network-console::before": (
            "color-mix(in srgb, var(--primary) 16%, transparent)",
            "color-mix(in srgb, var(--purple) 12%, transparent)",
        ),
        ".network-console-led": (
            "background: var(--primary);",
            "color-mix(in srgb, var(--primary) 62%, transparent)",
        ),
        ".network-route-step.is-relay": (
            "color-mix(in srgb, var(--primary) 42%, var(--border))",
            "color-mix(in srgb, var(--primary) 10%, var(--card))",
        ),
        ".network-architecture-boundary": (
            "color-mix(in srgb, var(--purple) 24%, var(--border))",
            "color-mix(in srgb, var(--purple) 20%, transparent)",
        ),
    }

    for selector, markers in expected.items():
        match = re.search(
            rf"(?ms)^{re.escape(selector)}\s*\{{(?P<body>[^}}]*)\}}",
            STYLES,
        )
        assert match, f"styles.css missing {selector} rule"
        body = match.group("body")
        for marker in markers:
            assert marker in body, f"{selector} must use {marker}"


def test_network_stats_bindings_update_all_metric_instances():
    for marker in (
        'setText("#network-clients, [data-network-clients]"',
        'setText("#network-hosts, [data-network-hosts]"',
        'setText("#network-repos, [data-network-repos]"',
        "[data-network-clients]",
        "[data-network-hosts]",
        "[data-network-repos]",
    ):
        assert marker in STATIC_PAGE


def test_network_wallets_resolve_when_stats_api_is_unavailable():
    poll_stats = STATIC_PAGE[
        STATIC_PAGE.index("async function pollStats()"):
        STATIC_PAGE.index("function primeCachedStats(")
    ]
    catch_block = poll_stats[poll_stats.index("} catch (error) {"):]

    assert "renderPayoutNodes([]);" in catch_block
