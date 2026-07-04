"""Server-side composition of the dashboard SPA shell from HTML partials.

The dashboard's static chrome used to be one ~1800-line ``dashboard/index.html``.
It is now split into individual partial ``.html`` files under
``public/dashboard/partials/`` (header, sidebar, main repos view, network rail,
modals). ``dashboard/index.html`` keeps only the ``<head>`` and the outer body
scaffold, pulling each section back in with an

    <!--#include partial="name"-->

placeholder. The Worker stitches them together at request time (see
``Default._route``'s ``/dashboard`` branch in ``entry.py``).

This module is intentionally js-free — no ``js``/``workers`` imports — so both
the Worker (on Cloudflare) and the test suite can import it directly, the same
way they import ``urls.py``. The composition is a pure string substitution, so
it is trivially unit-testable against the on-disk partials.
"""

import re

# Placeholder the shell uses to pull in a partial: <!--#include partial="name"-->
INCLUDE_RE = re.compile(r'<!--#include partial="([a-z0-9-]+)"-->')


def partial_path(name):
    """public/-relative path to a partial by its include name."""
    return "dashboard/partials/%s.html" % name


def included_partials(shell):
    """Ordered, de-duplicated list of partial names the shell pulls in."""
    seen = []
    for name in INCLUDE_RE.findall(shell):
        if name not in seen:
            seen.append(name)
    return seen


def assemble_shell(shell, partials):
    """Substitute each ``<!--#include-->`` with ``partials[name]``.

    ``partials`` maps a partial name to its raw HTML. A missing name raises
    ``KeyError`` rather than silently leaving the placeholder in the page, so a
    composition bug surfaces loudly instead of shipping a broken shell.
    """
    return INCLUDE_RE.sub(lambda m: partials[m.group(1)], shell)


def compose_from_reader(read):
    """Assemble the dashboard shell using ``read(public_relative_path) -> str``.

    Shared by the Worker (``read`` fetches from the ASSETS binding) and the test
    suite (``read`` loads files off disk). Reads the shell, then each partial it
    references, and returns the fully composed HTML.
    """
    shell = read("dashboard/index.html")
    partials = {name: read(partial_path(name)) for name in included_partials(shell)}
    return assemble_shell(shell, partials)
