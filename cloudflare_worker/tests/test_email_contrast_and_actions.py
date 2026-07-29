"""All outbound mail has one contrast mode and a site action."""

from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
ENTRY = (ROOT / "src" / "entry.py").read_text(encoding="utf-8")


def test_shared_email_card_is_light_only_and_high_contrast():
    card = ENTRY[
        ENTRY.index("def _light_email_fragment"):
        ENTRY.index("def _format_email_ts")
    ]
    assert 'name=\\"color-scheme\\" content=\\"light\\"' in card
    assert "prefers-color-scheme:dark" not in card
    assert '"#f5f5f5", "#24292f"' in card
    assert '"#d4d4d8", "#24292f"' in card
    assert '"#0f0f11", "#f6f8fa"' in card


def test_sender_adds_html_and_site_action_even_for_plain_text_mail():
    sender = ENTRY[
        ENTRY.index("async def _send_email"):
        ENTRY.index("# --- Account email activity")
    ]
    assert "Open ForkMesh: " in sender
    assert "if not html:" in sender
    assert "_forkmesh_email_card_html(" in sender
    assert 'if "data-forkmesh-site-action" not in html:' in sender
    assert "_forkmesh_email_action_html(" in sender


def test_how_are_we_doing_links_to_lobby_feedback():
    feedback = ENTRY[
        ENTRY.index("def _feedback_email_content"):
        ENTRY.index("async def _send_feedback_emails")
    ]
    assert "world/?landmark=office&feedback=1" in feedback
    assert "Add feedback in the lobby" in feedback

