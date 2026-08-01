"""Pure catalog + validation for ForkMesh achievement badges.

Badges are public recognition marks shown on a profile. Each one is either
awarded automatically at a real, verifiable platform event or granted by a
platform administrator; the catalog below is the fixed set of badges that can
exist. The Worker imports this module, but it has no Workers/JavaScript or D1
dependency so the catalog can be unit-tested with normal CPython. D1 reads,
writes, and the event hooks that call into this module live in entry.py.
"""

import re

_SLUG_RE = re.compile(r"^[a-z][a-z0-9_]{1,39}$")



BADGE_DEFINITIONS = {
    "first_100_users": {
        "name": "Founding Member",
        "description": "One of the first 100 people to create a ForkMesh account.",
        "icon": "\U0001F947",
    },
    "world_first_hour": {
        "name": "World Explorer",
        "description": "Spent a full hour in the ForkMesh World.",
        "icon": "\U0001F30D",
    },
    "first_referral": {
        "name": "Connector",
        "description": "Brought in the first new member through a referral link.",
        "icon": "\U0001F517",
    },
    "mirror_operator": {
        "name": "Mirror Operator",
        "description": "Runs a node that mirrors and helps keep repositories available.",
        "icon": "\U0001FA9E",
    },
}


def normalize_badge_slug(value):
    """A known catalog slug, lowercased, or "" for anything else."""
    slug = str(value or "").strip().lower()
    return slug if _SLUG_RE.match(slug) and slug in BADGE_DEFINITIONS else ""


def badge_definition(slug):
    return BADGE_DEFINITIONS.get(normalize_badge_slug(slug))


def public_catalog():
    return [
        {"slug": slug, "name": info["name"], "description": info["description"],
         "icon": info["icon"]}
        for slug, info in BADGE_DEFINITIONS.items()
    ]
