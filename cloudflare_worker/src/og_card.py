"""Social-preview (OpenGraph) card renderer for repo and referral pages.

Pure (builtin-only, no ``js``/``workers`` runtime, no ``env``/DB access): a
Mastodon/Slack/Twitter unfurl of a repo URL used to show only the ForkMesh
logo (issue adhoc #46); now it gets a 1200x630 PNG info card - repo name,
description, and the catalog stats - with the logo small in the top-right.

Everything is rendered in-process with the stdlib (Pyodide workers have no
Pillow): pixels go into a flat RGB bytearray, text comes from an embedded
classic 5x7 bitmap font scaled up per use, the logo is a pre-scaled RGBA
blob baked in at authoring time (tools/embed_og_logo.py regenerates it from
public/assets/logo.png), and the PNG is assembled by hand with zlib. Drawing
sticks to bytearray row-slice writes so a card costs only a few thousand
Python-level ops - cheap enough for a worker request, and each colo renders
a given repo's card at most once per edge-cache TTL anyway.

Colors mirror the dashboard's dark theme variables in
public/dashboard/repo.html so the card looks like the page it links to.
"""

import base64
import struct
import zlib
from datetime import datetime, timezone

CARD_W = 1200
CARD_H = 630

_BG = (1, 4, 9)
_CARD = (13, 17, 23)
_BORDER = (48, 54, 61)
_FG = (240, 246, 252)
_TEXT = (201, 209, 217)
_MUTED = (139, 148, 158)
_ACCENT = (88, 166, 255)




_FONT = bytes.fromhex(
    "0000000000"
    "00005f0000"
    "0007000700"
    "147f147f14"
    "242a7f2a12"
    "2313086462"
    "3649552250"
    "0005030000"
    "001c224100"
    "0041221c00"
    "082a1c2a08"
    "08083e0808"
    "0050300000"
    "0808080808"
    "0060600000"
    "2010080402"
    "3e5149453e"
    "00427f4000"
    "4261514946"
    "2141454b31"
    "1814127f10"
    "2745454539"
    "3c4a494930"
    "0171090503"
    "3649494936"
    "064949291e"
    "0036360000"
    "0056360000"
    "0008142241"
    "1414141414"
    "4122140800"
    "0201510906"
    "324979413e"
    "7e1111117e"
    "7f49494936"
    "3e41414122"
    "7f4141221c"
    "7f49494941"
    "7f09090901"
    "3e4149497a"
    "7f0808087f"
    "00417f4100"
    "2040413f01"
    "7f08142241"
    "7f40404040"
    "7f020c027f"
    "7f0408107f"
    "3e4141413e"
    "7f09090906"
    "3e4151215e"
    "7f09192946"
    "4649494931"
    "01017f0101"
    "3f4040403f"
    "1f2040201f"
    "3f4038403f"
    "6314081463"
    "0708700807"
    "6151494543"
    "007f414100"
    "0204081020"
    "0041417f00"
    "0402010204"
    "4040404040"
    "0001020400"
    "2054545478"
    "7f48444438"
    "3844444420"
    "384444487f"
    "3854545418"
    "087e090102"
    "0c5252523e"
    "7f08040478"
    "00447d4000"
    "2040443d00"
    "7f10284400"
    "00417f4000"
    "7c04180478"
    "7c08040478"
    "3844444438"
    "7c14141408"
    "081414187c"
    "7c08040408"
    "4854545420"
    "043f444020"
    "3c4040207c"
    "1c2040201c"
    "3c4030403c"
    "4428102844"
    "0c5050503c"
    "4464544c44"
    "0008364100"
    "00007f0000"
    "0041360800"
    "0201020402"
)



_LOGO_W = 93
_LOGO_H = 112
_LOGO_B64 = (
    "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"
    "p07HzJr147FPPv2sx7/7+f4fE/JD4g=="
)


def _logo_rgba():
    return zlib.decompress(base64.b64decode(_LOGO_B64))


class _Canvas:
    """Flat RGB byte canvas with just the ops the card needs."""

    def __init__(self, w, h, color):
        self.w = w
        self.h = h
        self.buf = bytearray(bytes(color) * (w * h))

    def fill_rect(self, x, y, w, h, color):
        x0 = max(0, x)
        y0 = max(0, y)
        x1 = min(self.w, x + w)
        y1 = min(self.h, y + h)
        if x1 <= x0 or y1 <= y0:
            return
        row = bytes(color) * (x1 - x0)
        stride = self.w * 3
        for yy in range(y0, y1):
            off = yy * stride + x0 * 3
            self.buf[off:off + len(row)] = row

    def frame_rect(self, x, y, w, h, thickness, color):
        self.fill_rect(x, y, w, thickness, color)
        self.fill_rect(x, y + h - thickness, w, thickness, color)
        self.fill_rect(x, y, thickness, h, color)
        self.fill_rect(x + w - thickness, y, thickness, h, color)

    def text(self, x, y, s, scale, color):
        """Draw s at (x, y) top-left, each font pixel a scale x scale block."""
        pen = x
        for ch in s:
            code = ord(ch)
            if not 32 <= code <= 126:
                code = 63
            base = (code - 32) * 5
            for col in range(5):
                bits = _FONT[base + col]
                if not bits:
                    continue
                for row in range(7):
                    if bits & (1 << row):
                        self.fill_rect(pen + col * scale, y + row * scale,
                                       scale, scale, color)
            pen += 6 * scale
        return pen

    def blit_rgba(self, x, y, w, h, rgba):
        """Alpha-blend a raw RGBA blob onto the canvas."""
        stride = self.w * 3
        buf = self.buf
        for yy in range(h):
            ty = y + yy
            if ty < 0 or ty >= self.h:
                continue
            src = yy * w * 4
            dst = ty * stride + x * 3
            for xx in range(w):
                a = rgba[src + 3]
                if a == 255:
                    buf[dst:dst + 3] = rgba[src:src + 3]
                elif a:
                    inv = 255 - a
                    buf[dst] = (rgba[src] * a + buf[dst] * inv) // 255
                    buf[dst + 1] = (rgba[src + 1] * a + buf[dst + 1] * inv) // 255
                    buf[dst + 2] = (rgba[src + 2] * a + buf[dst + 2] * inv) // 255
                src += 4
                dst += 3

    def png(self):
        stride = self.w * 3
        raw = bytearray()
        for yy in range(self.h):
            raw.append(0)
            raw += self.buf[yy * stride:(yy + 1) * stride]
        out = bytearray(b"\x89PNG\r\n\x1a\n")
        for tag, body in (
                (b"IHDR", struct.pack(">IIBBBBB", self.w, self.h, 8, 2, 0, 0, 0)),
                (b"IDAT", zlib.compress(bytes(raw), 6)),
                (b"IEND", b"")):
            out += struct.pack(">I", len(body)) + tag + body
            out += struct.pack(">I", zlib.crc32(tag + body) & 0xFFFFFFFF)
        return bytes(out)


def text_width(s, scale):
    return max(0, len(s) * 6 * scale - scale)


def _fit(s, scale, max_w):
    """Truncate s with an ellipsis so it fits max_w at scale."""
    if text_width(s, scale) <= max_w:
        return s
    while s and text_width(s + "...", scale) > max_w:
        s = s[:-1]
    return s + "..."


def _wrap(s, scale, max_w, max_lines):
    words = s.split()
    lines = []
    cur = ""
    for word in words:
        cand = (cur + " " + word) if cur else word
        if cur and text_width(cand, scale) > max_w:
            lines.append(cur)
            cur = word
        else:
            cur = cand
    if cur:
        lines.append(cur)
    if len(lines) > max_lines:
        lines = lines[:max_lines]
        lines[-1] += "..."
    return [_fit(line, scale, max_w) for line in lines]


def format_count(value):
    """Catalog counts arrive as strings ('', '12'); render compactly."""
    try:
        n = int(str(value).strip() or 0)
    except (TypeError, ValueError):
        n = 0
    if n >= 1000000:
        text = "%.1fM" % (n / 1000000.0)
    elif n >= 1000:
        text = "%.1fk" % (n / 1000.0)
    else:
        return str(n)
    return text.replace(".0", "")


def format_exact(value):
    """Exact, grouped count - the referral card's whole point is the number,
    so 1,284 clicks must not collapse to the same '1.3k' as 1,285."""
    try:
        n = int(str(value).strip() or 0)
    except (TypeError, ValueError):
        n = 0
    return "{:,}".format(n)


def format_size(size_bytes):
    try:
        n = int(size_bytes or 0)
    except (TypeError, ValueError):
        n = 0
    for unit in ("B", "KB", "MB", "GB", "TB"):
        if n < 1024 or unit == "TB":
            if unit == "B":
                return "%d %s" % (n, unit)
            return ("%.1f %s" % (n, unit)).replace(".0 ", " ")
        n /= 1024.0
    return "0 B"


def _ts_seconds(value):
    """Catalog updatedAt may be epoch ms, epoch s, or an ISO string."""
    text = str(value or "").strip()
    if not text:
        return None
    try:
        n = int(float(text))
        if n > 10 ** 11:
            return n // 1000
        if n > 10 ** 8:
            return n
        return None
    except ValueError:
        pass
    try:
        stamp = datetime.fromisoformat(text.replace("Z", "+00:00"))
        if stamp.tzinfo is None:
            stamp = stamp.replace(tzinfo=timezone.utc)
        return int(stamp.timestamp())
    except ValueError:
        return None


def format_age(value, now_s):
    ts = _ts_seconds(value)
    if ts is None:
        return "-"
    delta = max(0, int(now_s) - ts)
    if delta < 60:
        return "now"
    if delta < 3600:
        return "%dm ago" % (delta // 60)
    if delta < 86400:
        return "%dh ago" % (delta // 3600)
    if delta < 30 * 86400:
        return "%dd ago" % (delta // 86400)
    if delta < 365 * 86400:
        return "%dmo ago" % (delta // (30 * 86400))
    return "%dy ago" % (delta // (365 * 86400))


def render_repo_card(info, now_s):
    """Render the 1200x630 OpenGraph PNG for a repo.

    info keys (all optional except owner/repo): owner, repo, description,
    branch, host, stars, mirrors, issues, pulls, commits, branches,
    sizeBytes, updatedAt, followers.
    """
    owner = str(info.get("owner") or "")
    repo = str(info.get("repo") or "")
    canvas = _Canvas(CARD_W, CARD_H, _BG)


    margin = 28
    canvas.fill_rect(margin, margin, CARD_W - 2 * margin, CARD_H - 2 * margin,
                     _CARD)
    canvas.frame_rect(margin, margin, CARD_W - 2 * margin, CARD_H - 2 * margin,
                      2, _BORDER)

    pad = 72
    logo_x = CARD_W - pad - _LOGO_W
    canvas.blit_rgba(logo_x, margin + 40, _LOGO_W, _LOGO_H, _logo_rgba())


    title = "%s/%s" % (owner, repo)
    title_max_w = logo_x - pad - 40
    scale = 6
    while scale > 3 and text_width(title, scale) > title_max_w:
        scale -= 1
    title = _fit(title, scale, title_max_w)
    canvas.text(pad, 96, title, scale, _FG)


    description = str(info.get("description") or "").strip()
    if not description:
        description = "A repository mirrored across the ForkMesh network."
    y = 96 + 7 * scale + 26
    for line in _wrap(description, 3, title_max_w, 2):
        canvas.text(pad, y, line, 3, _TEXT)
        y += 7 * 3 + 12


    canvas.fill_rect(pad, 268, CARD_W - 2 * pad, 2, _BORDER)


    cells = (
        ("STARS", format_count(info.get("stars"))),
        ("ISSUES", format_count(info.get("issues"))),
        ("PULL REQUESTS", format_count(info.get("pulls"))),
        ("MIRRORS", format_count(info.get("mirrors"))),
        ("COMMITS", format_count(info.get("commits"))),
        ("BRANCHES", format_count(info.get("branches"))),
        ("SIZE", format_size(info.get("sizeBytes"))),
        ("UPDATED", format_age(info.get("updatedAt"), now_s)),
    )
    grid_x = pad
    grid_w = CARD_W - 2 * pad
    col_w = grid_w // 4
    for index, (label, value) in enumerate(cells):
        cx = grid_x + (index % 4) * col_w
        cy = 306 + (index // 4) * 110
        value_scale = 5
        while value_scale > 2 and text_width(value, value_scale) > col_w - 24:
            value_scale -= 1
        canvas.text(cx, cy, value, value_scale, _FG)
        canvas.text(cx, cy + 7 * 5 + 12, label, 2, _MUTED)


    footer_y = CARD_H - margin - 48
    host = str(info.get("host") or "forkmesh.com")
    url = _fit("%s/%s/%s" % (host, owner, repo), 3, grid_w - 360)
    canvas.text(pad, footer_y, url, 3, _ACCENT)
    right_bits = []
    branch = str(info.get("branch") or "").strip()
    if branch:
        right_bits.append(_fit(branch, 2, 200))
    try:
        followers = int(info.get("followers") or 0)
    except (TypeError, ValueError):
        followers = 0
    if followers > 0:
        right_bits.append("%s follower%s" % (
            format_count(followers), "" if followers == 1 else "s"))
    right = "  |  ".join(right_bits)
    if right:
        canvas.text(CARD_W - pad - text_width(right, 2), footer_y + 4,
                    right, 2, _MUTED)
    return canvas.png()


def render_referral_card(info, now_s):
    """Render the 1200x630 OpenGraph PNG for a /r/<name> share link.

    The counters are the live values read at request time, so a Mastodon (or
    Slack/Discord/Twitter) unfurl of the share link shows how far it has
    actually travelled instead of the generic signup-page preview.

    info keys: name (required), host, clicks, signups, lastTs.
    """
    name = str(info.get("name") or "")
    canvas = _Canvas(CARD_W, CARD_H, _BG)

    margin = 28
    canvas.fill_rect(margin, margin, CARD_W - 2 * margin, CARD_H - 2 * margin,
                     _CARD)
    canvas.frame_rect(margin, margin, CARD_W - 2 * margin, CARD_H - 2 * margin,
                      2, _BORDER)

    pad = 72
    logo_x = CARD_W - pad - _LOGO_W
    canvas.blit_rgba(logo_x, margin + 40, _LOGO_W, _LOGO_H, _logo_rgba())

    title = "@" + name
    title_max_w = logo_x - pad - 40
    scale = 6
    while scale > 3 and text_width(title, scale) > title_max_w:
        scale -= 1
    canvas.text(pad, 96, _fit(title, scale, title_max_w), scale, _FG)

    y = 96 + 7 * scale + 26
    for line in _wrap("invites you to ForkMesh - distributed Git hosting on a "
                      "mesh of desktop nodes.", 3, title_max_w, 2):
        canvas.text(pad, y, line, 3, _TEXT)
        y += 7 * 3 + 12

    canvas.fill_rect(pad, 268, CARD_W - 2 * pad, 2, _BORDER)


    cells = (
        ("CLICKS", format_exact(info.get("clicks"))),
        ("SIGNUPS", format_exact(info.get("signups"))),
        ("LAST ACTIVITY", format_age(info.get("lastTs"), now_s)),
    )
    grid_x = pad
    grid_w = CARD_W - 2 * pad
    col_w = grid_w // 3
    for index, (label, value) in enumerate(cells):
        cx = grid_x + index * col_w
        value_scale = 10
        while value_scale > 3 and text_width(value, value_scale) > col_w - 24:
            value_scale -= 1
        canvas.text(cx, 330, value, value_scale, _FG)
        canvas.text(cx, 330 + 7 * 10 + 16, label, 2, _MUTED)

    footer_y = CARD_H - margin - 48
    host = str(info.get("host") or "forkmesh.com")
    canvas.text(pad, footer_y,
                _fit("%s/r/%s" % (host, name), 3, grid_w - 360), 3, _ACCENT)
    right = "REFERRAL PROGRAM"
    canvas.text(CARD_W - pad - text_width(right, 2), footer_y + 4,
                right, 2, _MUTED)
    return canvas.png()
