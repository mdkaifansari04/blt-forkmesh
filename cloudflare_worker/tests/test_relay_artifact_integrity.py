"""Tamper-proofing served bytes (adhoc #86).

Two halves of the same guarantee:

1. Relay-side: a content-addressed release artifact is hashed as it streams
   through the Durable Object and the transfer is ABORTED at git-end if the
   bytes don't match the requested sha256 — so a tampered mirror can't serve
   forged bytes for a hash even to a client that never re-verifies. The pin is
   enforced at the relay, not delegated to the downloader.

2. Client-side: the desktop Mirror-nodes table underlines any content column
   whose value doesn't match the source of truth, so a node quietly serving
   divergent data is visible at a glance.
"""

from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
ENTRY = ROOT / "cloudflare_worker" / "src" / "entry.py"
RELEASES = ROOT / "qt_client" / "src" / "MainWindowReleases.cpp"


def test_release_blob_relay_verifies_content_hash_server_side():
    source = ENTRY.read_text(encoding="utf-8")

    # _stream_request takes an optional expected hash and seeds a running hasher
    # only when it's set (clone packs stay unhashed — they aren't content-addressed).
    assert (
        "async def _stream_request(self, host, message, headers, verify_sha256=None):"
        in source
    )
    assert '"hasher": hashlib.sha256() if verify_sha256 else None,' in source

    # Each chunk is folded into the hash before it leaves for the client.
    assert 'stream["hasher"].update(data)' in source

    # git-end aborts (not close) when the digest doesn't match, so the forged
    # bytes never land as a complete or edge-cacheable download.
    assert 'hasher.hexdigest() != stream.get("verify")' in source
    assert 'await stream["writer"].abort("integrity check failed")' in source

    # The release-blob endpoint is the caller that opts into verification, keyed
    # on the content-addressed sha256 from the URL.
    blob = source[
        source.index("async def _release_blob(")
        : source.index("async def _raw_blob(")
    ]
    assert "verify_sha256=sha256.lower()," in blob


def test_mirror_nodes_table_underlines_mismatched_columns():
    source = RELEASES.read_text(encoding="utf-8")
    body = source[source.index("void MainWindow::loadMirrorNodesPanel()"):]

    # A reference row (source of truth, else freshest) supplies the canonical values.
    assert "const MirrorAdvert *referenceAdvert =" in body
    assert "const int refIssues = referenceAdvert ? referenceAdvert->issueCount : -1;" in body

    # The underline helper flips the cell font and notes the mismatch in the tooltip.
    assert "auto markMismatch = [](QTableWidgetItem *item, bool mismatch," in body
    assert "f.setUnderline(true);" in body
    assert "Doesn't match the source of truth" in body

    # Applied to the content columns in BOTH the live-roster and catalog-backed rows.
    assert "markMismatch(commitItem," in body
    assert "markMismatch(issuesItem, countMismatch(nodeIssues, refIssues)," in body
    assert "markMismatch(artifactsItem, countMismatch(nodeArtifacts, refArtifacts)," in body
    assert "markMismatch(catCommitItem," in body
    assert "markMismatch(catIssuesItem, countMismatch(catIssues, refIssues)," in body
