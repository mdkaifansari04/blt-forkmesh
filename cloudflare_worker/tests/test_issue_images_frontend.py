#!/usr/bin/env python3
"""Web issue compose form: image attachments (adhoc #85).

Attached images travel as base64 data: URLs embedded in the issue body — no
separate upload channel — so they share the body's MAX_ISSUE_BYTES cap in the
worker. These tests pin the client-side guards (count/size limits, image-only
input), the placeholder flow that keeps the textarea readable, and the
swap-back right before signing so the signed hash covers what is sent.
"""

import re
from pathlib import Path

from _dashboard_bundle import assembled_dashboard_js


PUBLIC = Path(__file__).resolve().parents[1] / "public"
ENTRY = Path(__file__).resolve().parents[1] / "src" / "entry.py"


DASHBOARD_JS = assembled_dashboard_js()
ENTRY_TEXT = ENTRY.read_text(encoding="utf-8")


def _js_const(name):
    match = re.search(r"const %s = ([0-9*\s]+);" % name, DASHBOARD_JS)
    assert match, "missing constant %s in dashboard.js" % name
    return eval(match.group(1))


def test_compose_form_has_attach_controls_and_image_only_input():
    assert "data-repo-issue-attach-image" in DASHBOARD_JS
    assert "data-repo-issue-attach-hint" in DASHBOARD_JS
    assert "data-repo-issue-attachments" in DASHBOARD_JS


    assert 'accept="image/png,image/jpeg,image/gif,image/webp"' in DASHBOARD_JS
    assert 'if (!file.type.startsWith("image/"))' in DASHBOARD_JS
    assert "not an image" in DASHBOARD_JS


def test_attachment_budget_fits_inside_the_worker_issue_cap():
    max_count = _js_const("ISSUE_IMAGE_MAX_COUNT")
    max_bytes = _js_const("ISSUE_IMAGE_MAX_BYTES")
    max_total = _js_const("ISSUE_IMAGE_MAX_TOTAL_BYTES")
    assert 0 < max_bytes <= max_total
    assert max_count >= 1


    entry_match = re.search(r"MAX_ISSUE_BYTES = ([0-9*\s]+)", ENTRY_TEXT)
    assert entry_match
    max_issue_bytes = eval(entry_match.group(1))




    assert max_issue_bytes - max_total * 4 / 3 >= 4 * 1024




    assert "file.size > ISSUE_IMAGE_RAW_MAX_BYTES" in DASHBOARD_JS
    assert ("const budget = Math.min(ISSUE_IMAGE_MAX_BYTES, "
            "ISSUE_IMAGE_MAX_TOTAL_BYTES - total);") in DASHBOARD_JS
    assert "images.length >= ISSUE_IMAGE_MAX_COUNT" in DASHBOARD_JS


def test_body_carries_placeholders_until_signing():


    assert "forkmesh-pending-image:" in DASHBOARD_JS
    assert "![${name}](${id})" in DASHBOARD_JS



    submit = DASHBOARD_JS[
        DASHBOARD_JS.index("async function handleIssueComposeSubmit"):
        DASHBOARD_JS.index("function renderRepoDetail")
    ]
    assert "body = body.split(img.id).join(img.dataUrl);" in submit


    assert "await submitWebIssue(repo, title, body" in submit

    assert "images.length = 0;" in submit


def test_removing_a_chip_also_strips_its_placeholder_from_the_body():
    assert "data-repo-issue-attachment-remove" in DASHBOARD_JS
    assert ("bodyInput.value.split(`\\n![${removed.name}](${removed.id})\\n`)"
            ".join(\"\\n\")") in DASHBOARD_JS


def test_oversize_images_get_the_resize_flow_not_a_flat_rejection():




    assert "openImageResizeModal(file, budget)" in DASHBOARD_JS
    assert "crop or compress it to fit under" in DASHBOARD_JS
    assert ("Attached images already use up the issue's size limit"
            in DASHBOARD_JS)
    assert "is too large to attach (max" in DASHBOARD_JS
    assert "please shorten it or attach smaller images" in DASHBOARD_JS
