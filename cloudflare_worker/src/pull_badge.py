# Pull request "badge" generator (adhoc #44): a shareable SVG fingerprint of a
# PR. One tile per changed file (a file-type glyph with a green/red bar showing
# that file's additions:deletions ratio), tiles grouped by directory with a
# labeled connector line, and a header carrying the title, author and totals.
#
# Pure and js-free (like activitypub.py/urls.py) so tests import it directly.
# The SVG is self-contained — glyphs are text labels, no external images — so
# it renders anywhere the ActivityPub attachment lands.

_BADGE_WIDTH = 1200
_MARGIN = 40
_TILE = 56          # tile square edge
_TILE_PITCH = 72    # tile + horizontal gap
_ROW_PITCH = 104    # tile + bar + group line/label + vertical gap
_HEADER_H = 108
_COLS = (_BADGE_WIDTH - 2 * _MARGIN + (_TILE_PITCH - _TILE)) // _TILE_PITCH
# Tile cap keeps the SVG comfortably under the 64 KB note-media budget; the
# footer says how many files were dropped.
MAX_BADGE_FILES = 96

_GREEN = "#3fb950"
_RED = "#f85149"
_MUTED = "#8b949e"
_PURPLE = "#a371f7"

# extension -> (tile label, label color). Fallback is the uppercased extension
# in muted gray, so unknown types still read as "a file of some kind".
_EXT_STYLES = {
    "ts": ("TS", "#3178c6"), "tsx": ("TSX", "#3178c6"),
    "js": ("JS", "#f1e05a"), "mjs": ("JS", "#f1e05a"), "cjs": ("JS", "#f1e05a"),
    "jsx": ("JSX", "#61dafb"),
    "py": ("PY", "#4b8bbe"), "pyw": ("PY", "#4b8bbe"),
    "rb": ("RB", "#cc342d"), "rs": ("RS", "#dea584"), "go": ("GO", "#00add8"),
    "java": ("JAVA", "#b07219"), "kt": ("KT", "#a97bff"),
    "swift": ("SWFT", "#f05138"), "cs": ("C#", "#178600"),
    "c": ("C", "#9cdcfe"), "h": ("H", "#9cdcfe"),
    "cpp": ("C++", "#f34b7d"), "cc": ("C++", "#f34b7d"),
    "cxx": ("C++", "#f34b7d"), "hpp": ("H++", "#f34b7d"),
    "php": ("PHP", "#777bb3"), "dart": ("DART", "#00b4ab"),
    "vue": ("VUE", "#41b883"), "svelte": ("SVLT", "#ff3e00"),
    "html": ("</>", "#e34c26"), "htm": ("</>", "#e34c26"),
    "css": ("CSS", "#563d7c"), "scss": ("SCSS", "#c6538c"),
    "less": ("LESS", "#1d365d"),
    "md": ("MD", "#519aba"), "markdown": ("MD", "#519aba"),
    "json": ("{ }", "#cbcb41"), "yaml": ("YAML", "#cb4b4b"),
    "yml": ("YAML", "#cb4b4b"), "toml": ("TOML", "#9c4221"),
    "xml": ("XML", "#e37933"), "ini": ("CFG", "#6d8086"),
    "cfg": ("CFG", "#6d8086"), "conf": ("CFG", "#6d8086"),
    "env": ("ENV", "#6d8086"),
    "sh": (">_", "#89e051"), "bash": (">_", "#89e051"),
    "zsh": (">_", "#89e051"), "bat": (">_", "#c1f12e"),
    "ps1": (">_", "#012456"),
    "sql": ("SQL", "#e38c00"),
    "svg": ("SVG", "#ffb13b"),
    "png": ("IMG", "#a074c4"), "jpg": ("IMG", "#a074c4"),
    "jpeg": ("IMG", "#a074c4"), "gif": ("IMG", "#a074c4"),
    "webp": ("IMG", "#a074c4"), "avif": ("IMG", "#a074c4"),
    "ico": ("IMG", "#a074c4"),
    "pdf": ("PDF", "#f40f02"), "txt": ("TXT", "#8b949e"),
    "lock": ("LOCK", "#8b949e"), "qrc": ("QRC", "#41cd52"),
    "pro": ("QT", "#41cd52"), "ui": ("UI", "#41cd52"),
    "cmake": ("CMK", "#649ad2"),
}

_NAME_STYLES = {
    "dockerfile": ("DOCK", "#2496ed"),
    "makefile": ("MAKE", "#6d8086"),
    "cmakelists.txt": ("CMK", "#649ad2"),
    "license": ("LIC", "#d0b44c"),
    "readme.md": ("MD", "#519aba"),
    ".gitignore": ("GIT", "#f14e32"),
    ".gitattributes": ("GIT", "#f14e32"),
    ".gitmodules": ("GIT", "#f14e32"),
    "package.json": ("NPM", "#cb3837"),
    "package-lock.json": ("NPM", "#cb3837"),
}


def file_glyph(path):
    """(label, color) tile glyph for one changed file's basename."""
    name = (path or "").rsplit("/", 1)[-1].lower()
    if name in _NAME_STYLES:
        return _NAME_STYLES[name]
    ext = name.rsplit(".", 1)[-1] if "." in name.lstrip(".") else ""
    if ext in _EXT_STYLES:
        return _EXT_STYLES[ext]
    label = (ext.upper()[:4] or "FILE")
    return (label, _MUTED)


def patch_file_stats(patch):
    """[{path, adds, dels}] per file from a unified diff, in patch order."""
    files = []
    cur = None
    for line in (patch or "").split("\n"):
        if line.startswith("diff --git "):
            # "diff --git a/<old> b/<new>" — take the b/ side (rename target).
            rest = line[len("diff --git "):]
            path = rest.split(" b/", 1)[1] if " b/" in rest else rest
            cur = {"path": path, "adds": 0, "dels": 0}
            files.append(cur)
        elif cur is None:
            continue
        elif line.startswith("+++") or line.startswith("---"):
            continue
        elif line.startswith("+"):
            cur["adds"] += 1
        elif line.startswith("-"):
            cur["dels"] += 1
    return files


def _esc(text):
    return (str(text).replace("&", "&amp;").replace("<", "&lt;")
            .replace(">", "&gt;").replace('"', "&quot;"))


def _ellipsize(text, limit):
    text = str(text or "")
    return text if len(text) <= limit else text[:limit - 1].rstrip() + "…"


def _dir_label(directory):
    """Last path segment of a directory, capped to 8 chars.

    Directory connector labels show just the folder name, not the whole path
    ("one/two/three" -> "three"), so they stay readable under a narrow tile
    cluster.
    """
    name = directory if directory == "/" else directory.rsplit("/", 1)[-1]
    return name if len(name) <= 8 else name[:8] + "…"


def _tile_svg(x, y, file):
    label, color = file_glyph(file["path"])
    adds, dels = file["adds"], file["dels"]
    total = adds + dels
    parts = [
        '<rect x="%d" y="%d" width="%d" height="%d" rx="8" fill="#161b22" '
        'stroke="#30363d"/>' % (x, y, _TILE, _TILE - 12),
        '<text x="%d" y="%d" font-size="%d" font-weight="700" fill="%s" '
        'text-anchor="middle">%s</text>' % (
            x + _TILE // 2, y + (_TILE - 12) // 2 + 5,
            15 if len(label) <= 3 else 11, color, _esc(label)),
    ]
    # Ratio bar: green additions share on the left, red deletions share on the
    # right; a file with no counted lines (binary) gets a neutral bar.
    bar_y = y + _TILE - 4
    if total:
        green_w = round(_TILE * adds / total)
        if green_w:
            parts.append('<rect x="%d" y="%d" width="%d" height="6" rx="3" '
                         'fill="%s"/>' % (x, bar_y, green_w, _GREEN))
        if _TILE - green_w:
            parts.append('<rect x="%d" y="%d" width="%d" height="6" rx="3" '
                         'fill="%s"/>' % (x + green_w, bar_y, _TILE - green_w,
                                          _RED))
    else:
        parts.append('<rect x="%d" y="%d" width="%d" height="6" rx="3" '
                     'fill="#30363d"/>' % (x, bar_y, _TILE))
    return "".join(parts)


def _group_files(files):
    """[(directory, [file, ...])] with files clustered by their directory."""
    ordered = sorted(files, key=lambda f: (f["path"] or "").lower())
    groups = []
    for file in ordered:
        path = file["path"] or "file"
        directory = path.rsplit("/", 1)[0] if "/" in path else "/"
        if groups and groups[-1][0] == directory:
            groups[-1][1].append(file)
        else:
            groups.append((directory, [file]))
    return groups


def pull_badge_svg(title, author, files, number=0):
    """Self-contained SVG badge for a pull request.

    files: [{path, adds, dels}] (patch_file_stats output). number may be 0 for
    a PR still pending a maintainer-assigned number.
    """
    files = list(files or [])
    shown = files[:MAX_BADGE_FILES]
    dropped = len(files) - len(shown)
    additions = sum(f["adds"] for f in files)
    deletions = sum(f["dels"] for f in files)

    body = []
    # Tile grid: groups flow left-to-right; a group that would split across a
    # row boundary but could fit on a fresh row starts one, so directories
    # mostly stay visually contiguous like the concept mock.
    row, col = 0, 0
    for directory, group in _group_files(shown):
        if col and col + len(group) > _COLS and len(group) <= _COLS:
            row, col = row + 1, 0
        segments = []  # (row, first col, last col) runs this group occupies
        for file in group:
            if col >= _COLS:
                row, col = row + 1, 0
            if segments and segments[-1][0] == row:
                segments[-1][2] = col
            else:
                segments.append([row, col, col])
            x = _MARGIN + col * _TILE_PITCH
            y = _HEADER_H + row * _ROW_PITCH
            body.append(_tile_svg(x, y, file))
            col += 1
        # Connector line under every run of the group; the directory label
        # sits under the last run.
        for seg_row, first, last in segments:
            x1 = _MARGIN + first * _TILE_PITCH
            x2 = _MARGIN + last * _TILE_PITCH + _TILE
            line_y = _HEADER_H + seg_row * _ROW_PITCH + _TILE + 12
            body.append('<line x1="%d" y1="%d" x2="%d" y2="%d" '
                        'stroke="#30363d" stroke-width="1"/>' % (
                            x1, line_y, x2, line_y))
            body.append('<circle cx="%d" cy="%d" r="2.5" fill="#484f58"/>'
                        '<circle cx="%d" cy="%d" r="2.5" fill="#484f58"/>' % (
                            x1, line_y, x2, line_y))
        seg_row, first, last = segments[-1]
        label = "%s  (%d file%s)" % (
            _dir_label(directory), len(group),
            "" if len(group) == 1 else "s")
        body.append('<text x="%d" y="%d" font-size="13" fill="%s" '
                    'text-anchor="middle">%s</text>' % (
                        (_MARGIN + first * _TILE_PITCH +
                         _MARGIN + last * _TILE_PITCH + _TILE) // 2,
                        _HEADER_H + seg_row * _ROW_PITCH + _TILE + 32,
                        _MUTED, _esc(label)))

    height = _HEADER_H + (row + 1) * _ROW_PITCH + (24 if dropped else 0)
    if dropped:
        body.append('<text x="%d" y="%d" font-size="13" fill="%s">'
                    '+%d more file%s not shown</text>' % (
                        _MARGIN, height - 14, _MUTED, dropped,
                        "" if dropped == 1 else "s"))

    number_label = ("#%d" % number) if number else "pull request"
    byline = ("%s · by %s" % (number_label, author)) if author \
        else number_label
    stats = [("+%d" % additions, "Additions", _GREEN),
             ("-%d" % deletions, "Deletions", _RED),
             ("%d" % len(files), "Files changed", "#e6edf3")]
    header = [
        '<text x="%d" y="46" font-size="26" font-weight="700" '
        'fill="#e6edf3">%s</text>' % (_MARGIN, _esc(_ellipsize(title, 52))),
        '<text x="%d" y="76" font-size="17" font-weight="600" '
        'fill="%s">%s</text>' % (_MARGIN, _PURPLE, _esc(byline)),
    ]
    for i, (value, caption, color) in enumerate(stats):
        x = _BADGE_WIDTH - _MARGIN - (2 - i) * 150
        header.append('<text x="%d" y="50" font-size="24" font-weight="700" '
                      'fill="%s" text-anchor="end">%s</text>' % (
                          x, color, _esc(value)))
        header.append('<text x="%d" y="72" font-size="13" fill="%s" '
                      'text-anchor="end">%s</text>' % (x, _MUTED, caption))

    return (
        '<svg xmlns="http://www.w3.org/2000/svg" width="%d" height="%d" '
        'viewBox="0 0 %d %d" font-family="-apple-system,\'Segoe UI\','
        'Ubuntu,Helvetica,Arial,sans-serif">'
        '<rect width="%d" height="%d" rx="14" fill="#0d1117"/>%s%s</svg>' % (
            _BADGE_WIDTH, height, _BADGE_WIDTH, height, _BADGE_WIDTH, height,
            "".join(header), "".join(body)))
