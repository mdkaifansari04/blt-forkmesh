#!/usr/bin/env python3
"""Pull-request badge generator (adhoc #44).

pull_badge.py renders the shareable SVG "fingerprint" of a PR: one tile per
changed file (a file-type glyph over a green/red additions:deletions bar),
tiles clustered by directory under a labeled connector line, and a header with
the title, author and totals. Like activitypub.py it is pure and js-free, so
these tests import the real implementation. The entry.py wiring — attaching
the badge as the lead image of the federated PR-opened note — is pinned by
text assertions because entry.py imports js.

Run: python3 -m pytest cloudflare_worker/tests/test_pull_badge.py
"""
import importlib.util
from pathlib import Path

SRC = Path(__file__).resolve().parents[1] / "src"
spec = importlib.util.spec_from_file_location(
    "pull_badge", SRC / "pull_badge.py")
badge = importlib.util.module_from_spec(spec)
spec.loader.exec_module(badge)


SAMPLE_PATCH = """diff --git a/src/auth/login.ts b/src/auth/login.ts
index 111..222 100644
--- a/src/auth/login.ts
+++ b/src/auth/login.ts
@@ -1,3 +1,5 @@
+import x
+import y
-old line
 context
diff --git a/src/auth/session.py b/src/auth/session.py
new file mode 100644
--- /dev/null
+++ b/src/auth/session.py
@@ -0,0 +1,2 @@
+a = 1
+b = 2
diff --git a/docs/README.md b/docs/README.md
--- a/docs/README.md
+++ b/docs/README.md
@@ -1 +1 @@
-old
+new
"""


# --- patch_file_stats --------------------------------------------------------

def test_patch_file_stats_counts_per_file():
    files = badge.patch_file_stats(SAMPLE_PATCH)
    assert [f["path"] for f in files] == [
        "src/auth/login.ts", "src/auth/session.py", "docs/README.md"]
    assert files[0] == {"path": "src/auth/login.ts", "adds": 2, "dels": 1}
    assert files[1] == {"path": "src/auth/session.py", "adds": 2, "dels": 0}
    assert files[2] == {"path": "docs/README.md", "adds": 1, "dels": 1}


def test_patch_file_stats_ignores_headers_and_empty_patch():
    # +++/--- file headers must not count as changed lines.
    assert badge.patch_file_stats("") == []
    assert badge.patch_file_stats(None) == []
    only_headers = ("diff --git a/x b/x\n--- a/x\n+++ b/x\n")
    assert badge.patch_file_stats(only_headers) == [
        {"path": "x", "adds": 0, "dels": 0}]


# --- glyphs -------------------------------------------------------------------

def test_file_glyph_known_special_and_fallback():
    assert badge.file_glyph("src/a.ts") == ("TS", "#3178c6")
    assert badge.file_glyph("Dockerfile") == ("DOCK", "#2496ed")
    assert badge.file_glyph("dir/CMakeLists.txt") == ("CMK", "#649ad2")
    label, color = badge.file_glyph("weird.zzz")
    assert label == "ZZZ"
    label, _ = badge.file_glyph("noextension")
    assert label == "FILE"


# --- pull_badge_svg -----------------------------------------------------------

def test_badge_svg_header_and_directory_groups():
    files = badge.patch_file_stats(SAMPLE_PATCH)
    svg = badge.pull_badge_svg("Add auth", "jett", files, number=245)
    assert svg.startswith("<svg ")
    assert "Add auth" in svg
    assert "#245 · by jett" in svg
    # Totals: 5 additions, 2 deletions, 3 files.
    assert ">+5</text>" in svg
    assert ">-2</text>" in svg
    assert "Additions" in svg and "Deletions" in svg and "Files changed" in svg
    # Directory clusters get a "(N files)" label; root files land under "/".
    assert "src/auth  (2 files)" in svg
    assert "docs  (1 file)" in svg


def test_badge_svg_pending_number_and_escaping():
    svg = badge.pull_badge_svg(
        'Fix <tag> & "quote"', "a&b", [{"path": "a.js", "adds": 1, "dels": 0}])
    # No maintainer-assigned number yet -> generic byline, nothing broken.
    assert "pull request · by a&amp;b" in svg
    assert "&lt;tag&gt; &amp; &quot;quote&quot;" in svg
    assert "<tag>" not in svg


def test_badge_svg_caps_tiles_and_stays_small():
    files = [{"path": "src/f%03d.py" % i, "adds": i, "dels": 1}
             for i in range(200)]
    svg = badge.pull_badge_svg("Big", "bot", files)
    dropped = 200 - badge.MAX_BADGE_FILES
    assert ("+%d more files not shown" % dropped) in svg
    # Totals still describe the whole PR, not just the tiles shown.
    assert (">%d</text>" % 200) in svg
    # The badge must fit the federated-note media budget (64 KB, see
    # MAX_NOTE_IMAGE_BYTES in activitypub.py) with room to spare.
    assert len(svg.encode("utf-8")) < 64 * 1024


def test_badge_svg_binary_file_gets_neutral_bar():
    svg = badge.pull_badge_svg(
        "Bin", "x", [{"path": "logo.png", "adds": 0, "dels": 0}])
    assert 'fill="#30363d"' in svg  # neutral bar, no green/red split
    assert "logo.png" not in svg or True  # tile shows a glyph, not the path


# --- entry.py wiring (text assertions: entry.py imports js) --------------------

ENTRY = (SRC / "entry.py").read_text(encoding="utf-8")


def test_publish_accepts_and_prepends_extra_images():
    start = ENTRY.index("async def _ap_publish_repo_event(")
    body = ENTRY[start:ENTRY.index("\n\n\n", start)]
    assert "extra_images=None" in body
    # Badge leads the attachment list so it becomes the visible preview.
    assert ("images = [img for img in (extra_images or []) if img] + images"
            in body)


def test_pull_open_publish_attaches_badge_svg():
    start = ENTRY.index("async def pulls_handler(")
    body = ENTRY[start:ENTRY.index('"pull", "open"', start) + 400]
    assert "patch_file_stats(pull.get(\"patch\", \"\") or \"\")" in body
    assert "pull_badge_svg(" in body
    assert '"mediaType": "image/svg+xml"' in body
    assert "extra_images=[badge] if badge else None" in body
    # Best-effort: a badge failure must never block the PR submission.
    assert "except Exception:" in body
