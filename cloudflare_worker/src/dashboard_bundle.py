"""Server-side composition of the dashboard behaviour script from JS fragments.

The dashboard's client script used to be one ~5450-line ``public/dashboard.js``.
It is now split into ordered fragment files under ``public/dashboard/js/`` that
the Worker concatenates back into a single ``/dashboard.js`` response at request
time (see ``Default._serve_dashboard_bundle`` in ``entry.py``). This mirrors how
the HTML chrome is split into partials and composed by ``dashboard_shell.py``.

Every fragment is a *contiguous slice* of the original single closure
``(() => { ... })();`` — the fragments are NOT independently valid modules; they
share one lexical scope and one ``state`` object. So the concatenation must be
byte-exact and in exactly ``FRAGMENTS`` order to reproduce the shipped script.

This module is intentionally js-free — no ``js``/``workers`` imports — so both
the Worker (on Cloudflare) and the test suite can import it directly, the same
way they import ``urls.py`` and ``dashboard_shell.py``. The composition is a pure
string concatenation, so it is trivially unit-testable against the on-disk
fragments.
"""

# Ordered fragments of the single dashboard IIFE. Concatenated verbatim (with no
# separator) they reproduce the original dashboard.js byte-for-byte. Keep this in
# lexical/source order; renaming or reordering changes the shipped script.
FRAGMENTS = (
    "01-state.js",
    "02-helpers.js",
    "03-issues-profile-io.js",
    "04-account.js",
    "05-repo-list-explorer.js",
    "06-repo-content.js",
    "07-repo-compose-branch.js",
    "08-repo-detail-network.js",
)


def fragment_path(name):
    """public/-relative path to a bundle fragment by its file name."""
    return "dashboard/js/%s" % name


def assemble_bundle(fragments):
    """Concatenate fragment sources, in order, into the single dashboard script.

    ``fragments`` is an iterable of raw JS strings in ``FRAGMENTS`` order. The
    join has no separator so the result is byte-identical to the pre-split file.
    """
    return "".join(fragments)


def compose_from_reader(read):
    """Assemble ``/dashboard.js`` using ``read(public_relative_path) -> str``.

    Shared by the Worker (``read`` fetches from the ASSETS binding) and the test
    suite (``read`` loads files off disk). Reads each fragment in ``FRAGMENTS``
    order and returns the fully composed script.
    """
    return assemble_bundle([read(fragment_path(name)) for name in FRAGMENTS])
