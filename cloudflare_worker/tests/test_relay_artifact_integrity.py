"""Tamper-proofing served bytes (adhoc #86).

Two halves of the same guarantee:

1. Gateway-side: a content-addressed release artifact is hashed before the
   direct-HTTPS gateway opens its bounded file stream. A mismatch is rejected
   before response construction, so the retired repository socket is not part
   of the integrity boundary.

2. Client-side: the desktop Mirror-nodes table underlines any content column
   whose value doesn't match the source of truth, so a node quietly serving
   divergent data is visible at a glance.
"""

from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
ENTRY = ROOT / "cloudflare_worker" / "src" / "entry.py"
GATEWAY = ROOT / "tools" / "mirror_gateway.py"
RELEASES = ROOT / "qt_client" / "src" / "MainWindowReleases.cpp"


def test_release_blob_gateway_verifies_content_hash_before_streaming():
    worker = ENTRY.read_text(encoding="utf-8")
    gateway = GATEWAY.read_text(encoding="utf-8")

    route = worker[
        worker.index("release_blob_match = RELEASE_BLOB_RE.match"):
        worker.index("host_match = REPO_HOST_RE.match")
    ]
    assert 'operation="release-blob"' not in route
    assert '"release-blob"' in route
    assert "release_sha=sha256" in route
    release_spec = gateway[
        gateway.index("def release_spec("):
        gateway.index("\n    def ", gateway.index("def release_spec(") + 1)
    ]
    assert "SHA256_RE.fullmatch(digest)" in release_spec
    assert "(root / \"sha256\" / digest[:2] / digest / \"data\").resolve()" in release_spec
    assert "hmac.compare_digest(_sha256_file(path), digest)" in release_spec
    assert 'raise GatewayError("release blob integrity check failed")' in release_spec
    assert "return StreamSpec(" in release_spec


def test_mirror_nodes_table_underlines_mismatched_columns():
    source = RELEASES.read_text(encoding="utf-8")
    body = source[source.index("void MainWindow::loadMirrorNodesPanel()"):]


    assert "const MirrorAdvert *referenceAdvert =" in body
    assert "const int refIssues = referenceAdvert ? referenceAdvert->issueCount : -1;" in body


    assert "auto markMismatch = [](QTableWidgetItem *item, bool mismatch," in body
    assert "f.setUnderline(true);" in body
    assert "Doesn't match the source of truth" in body


    assert "markMismatch(commitItem," in body
    assert "markMismatch(issuesItem, countMismatch(nodeIssues, refIssues)," in body
    assert "markMismatch(artifactsItem, countMismatch(nodeArtifacts, refArtifacts)," in body
    assert "markMismatch(catCommitItem," in body
    assert "markMismatch(catIssuesItem, countMismatch(catIssues, refIssues)," in body
