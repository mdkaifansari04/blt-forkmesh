#!/usr/bin/env python3
"""Regression checks for public chat, bounty, and mobile capability claims."""

from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
PROJECT = ROOT.parent
APP_PUBLIC = ROOT / "public"


def _public_corpus():
    paths = [PROJECT / "README.md"]
    paths.extend(APP_PUBLIC.rglob("*.html"))
    paths.extend(APP_PUBLIC.rglob("*.js"))
    return "\n".join(
        path.read_text(encoding="utf-8", errors="replace")
        for path in paths
    )


def test_public_room_copy_does_not_claim_blind_or_end_to_end_default_rooms():
    corpus = _public_corpus().lower()
    forbidden = (
        "the relay is a <strong>blind</strong> relay",
        "the server never sees plaintext",
        "end-to-end encrypted before it ever touches the relay",
        "the relay only ever sees ciphertext",
        "persists no message bodies, ever",
        "room content is not meant to be readable by the relay",
        "chat the relay can't read",
        "conversations the relay can't read",
    )
    for claim in forbidden:
        assert claim not in corpus
    assert "500 persisted frames" in corpus


def test_public_durability_and_private_metadata_claims_are_bounded():
    corpus = _public_corpus().lower()
    forbidden = (
        "code that can't be taken down",
        "never lose the code that matters",
        "the worker and unauthorized mirror operators receive no private repository name or plaintext",
        "without packfile or message-body custody",
        "private repository names and private-agent content use separate owner-only encryption boundaries",
        "secure by default",
        "keep working even when the original host is offline",
        "authorized private repositories appear only to their owners and are never broadcast",
        "only owner, name, description, channel, sync time, maintainer key, signature",
    )
    for claim in forbidden:
        assert claim not in corpus


def test_provider_import_token_copy_discloses_worker_transit():
    corpus = _public_corpus().lower()
    assert "the token is sent only to the provider" not in corpus
    assert (
        "the token passes through forkmesh’s worker only for this request "
        "and is forwarded to the provider; it is not persisted"
    ) in corpus


def test_marketing_copy_matches_guest_registration_and_bounded_availability():
    corpus = _public_corpus().lower()
    for stale_claim in (
        "sign up with nothing but a key and a password",
        "no email, no phone",
        "every node has a public profile",
        "shows every online node",
        "the mesh always knows which nodes carry copies",
        "host offline? any mirror node serves",
        "failover kicks in the moment a host actually drops",
        "every repository gets its own encrypted room",
        "every repository would still exist",
        "always cloneable",
        "stay fresh automatically",
        "zero-custody relay",
    ):
        assert stale_claim not in corpus
    readme = (PROJECT / "README.md").read_text(encoding="utf-8").lower()
    normalized_readme = " ".join(readme.split())
    assert "public mirror eligibility requires a fresh account-bound signature" in normalized_readme
    assert "healthy direct https endpoint" in normalized_readme
    assert "an integrity-matching repository proof" in normalized_readme


def test_retired_bounty_and_flutter_mirror_advertising_stays_removed():
    readme = (PROJECT / "README.md").read_text(encoding="utf-8").lower()
    assert "fund any issue with one click" not in readme
    assert "pays out to the contributor when their pr is merged" not in readme
    assert "on-chain bounties" not in readme
    assert "mirror on your phone" not in readme
    assert "a flutter mobile client" in " ".join(readme.split())
    assert "legacy worker-custodied bounty deposits and automatic payouts are frozen" in readme
