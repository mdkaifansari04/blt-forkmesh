"""Normalizers for the world's Twitter/X and Reddit banner feeds.

Neither network exposes a CORS-open browser API the way mastodon.social
does, so the Worker proxies one bounded, edge-cached read of each public
feed and the banners in /world repaint from the normalized result. The
functions here are pure so the parsing rules stay testable without the
Cloudflare/Pyodide runtime.
"""

import json
import re
from email.utils import parsedate_to_datetime
from html import unescape as _unescape


TWITTER_HANDLE = "forkmesh"
TWITTER_PROFILE_URL = "https://x.com/forkmesh"
# X's widget-syndication timeline is the only unauthenticated read left; it
# serves HTML whose __NEXT_DATA__ script carries the timeline JSON. It is
# best-effort: when it stops serving, the banner keeps its static sign.
TWITTER_SYNDICATION_URL = (
    "https://syndication.twitter.com/srv/timeline-profile/screen-name/"
    + TWITTER_HANDLE
)
REDDIT_SUBREDDIT = "forkmesh"
REDDIT_PROFILE_URL = "https://www.reddit.com/r/forkmesh/"
REDDIT_LISTING_URL = (
    "https://www.reddit.com/r/forkmesh/new.json?limit=10&raw_json=1"
)
# Reddit rejects generic user agents; the documented convention is
# platform:app-id:version (by /u/owner).
REDDIT_USER_AGENT = "web:forkmesh-world-banner:v1 (by /u/forkmesh)"
# The blog is one of our own static assets, so the handler reads the index
# through env.ASSETS (no external fetch) and this module only parses it.
BLOG_URL = "https://forkmesh.com/blog"
BLOG_INDEX_ASSET = "blog.html"

SOCIAL_POSTS_LIMIT = 6
MAX_POST_TEXT = 400
MAX_POST_AUTHOR = 40

_NEXT_DATA_RE = re.compile(
    r'<script[^>]*id="__NEXT_DATA__"[^>]*>(.*?)</script>',
    re.DOTALL,
)

# One feature card on the static blog index (blog.html). The cards are
# uniform generated markup: anchor, meta line, title, then the blurb.
_BLOG_CARD_RE = re.compile(
    r'<a class="blog1-card" href="(?P<href>/blog/[^"]+)"[^>]*>'
    r'.*?<p class="blog1-card-meta">(?P<meta>.*?)</p>'
    r'\s*<h3>(?P<title>.*?)</h3>'
    r'\s*<p>(?P<blurb>.*?)</p>',
    re.DOTALL,
)


def _clean_text(value, limit=MAX_POST_TEXT):
    flat = re.sub(r"\s+", " ", str(value or "")).strip()
    return flat[:limit]


def _epoch_ms(value):
    try:
        number = float(value)
    except (TypeError, ValueError):
        return 0
    if number <= 0:
        return 0
    # Reddit reports seconds; anything already in milliseconds passes through.
    return int(number if number > 10**12 else number * 1000)


def normalize_reddit_listing(payload):
    """Bound a public /r/<sub>/new.json listing to the banner's post shape."""
    children = []
    if isinstance(payload, dict):
        data = payload.get("data")
        if isinstance(data, dict) and isinstance(data.get("children"), list):
            children = data["children"]
    posts = []
    for child in children:
        record = child.get("data") if isinstance(child, dict) else None
        if not isinstance(record, dict):
            continue
        title = _clean_text(record.get("title"))
        if not title:
            continue
        permalink = str(record.get("permalink") or "")
        posts.append({
            "id": _clean_text(record.get("id"), 40),
            "text": title,
            "author": _clean_text(record.get("author"), MAX_POST_AUTHOR),
            "score": int(record.get("score") or 0),
            "comments": int(record.get("num_comments") or 0),
            "createdAt": _epoch_ms(record.get("created_utc")),
            "url": (
                "https://www.reddit.com" + permalink
                if permalink.startswith("/")
                else REDDIT_PROFILE_URL
            ),
        })
        if len(posts) >= SOCIAL_POSTS_LIMIT:
            break
    return posts


def extract_next_data(html):
    """Pull the parsed __NEXT_DATA__ JSON object out of a syndication page."""
    match = _NEXT_DATA_RE.search(str(html or ""))
    if not match:
        return None
    try:
        data = json.loads(match.group(1))
    except (ValueError, TypeError):
        return None
    return data if isinstance(data, dict) else None


def _twitter_created_ms(value):
    # created_at arrives either as epoch milliseconds or as Twitter's classic
    # "Mon Apr 01 12:00:00 +0000 2024" string depending on payload vintage.
    ms = _epoch_ms(value)
    if ms:
        return ms
    try:
        return int(parsedate_to_datetime(str(value or "")).timestamp() * 1000)
    except (ValueError, TypeError):
        return 0


def normalize_twitter_timeline(next_data):
    """Bound the syndication timeline entries to the banner's post shape."""
    entries = []
    if isinstance(next_data, dict):
        try:
            candidate = (
                next_data["props"]["pageProps"]["timeline"]["entries"])
        except (KeyError, TypeError):
            candidate = None
        if isinstance(candidate, list):
            entries = candidate
    posts = []
    for entry in entries:
        content = entry.get("content") if isinstance(entry, dict) else None
        tweet = content.get("tweet") if isinstance(content, dict) else None
        if not isinstance(tweet, dict):
            continue
        # A retweet carries the original underneath; show what the account
        # amplified, credited to its author.
        retweeted = tweet.get("retweeted_status")
        record = retweeted if isinstance(retweeted, dict) else tweet
        text = _clean_text(record.get("full_text") or record.get("text"))
        if not text:
            continue
        tweet_id = _clean_text(record.get("id_str") or record.get("id"), 40)
        user = record.get("user") if isinstance(record.get("user"), dict) else {}
        screen_name = _clean_text(
            user.get("screen_name") or TWITTER_HANDLE, MAX_POST_AUTHOR)
        permalink = str(tweet.get("permalink") or "")
        posts.append({
            "id": tweet_id,
            "text": text,
            "author": "@" + screen_name,
            "likes": int(record.get("favorite_count") or 0),
            "retweets": int(record.get("retweet_count") or 0),
            "createdAt": _twitter_created_ms(record.get("created_at")),
            "url": (
                "https://x.com" + permalink
                if permalink.startswith("/")
                else "https://x.com/%s/status/%s" % (screen_name, tweet_id)
                if tweet_id
                else TWITTER_PROFILE_URL
            ),
        })
        if len(posts) >= SOCIAL_POSTS_LIMIT:
            break
    return posts


def normalize_blog_index(html):
    """Bound the blog index's feature cards to the banner's post shape.

    The static cards carry no dates, so createdAt stays 0 and the board's
    staleness plate reads the snapshot age instead of a last-post age.
    """
    posts = []
    for match in _BLOG_CARD_RE.finditer(str(html or "")):
        title = _clean_text(_unescape(match.group("title")), 120)
        if not title:
            continue
        href = match.group("href")
        posts.append({
            "id": _clean_text(href.strip("/").split("/")[-1], 80),
            "text": title,
            "detail": _clean_text(_unescape(match.group("blurb"))),
            "meta": _clean_text(_unescape(match.group("meta")), 80),
            "createdAt": 0,
            "url": "https://forkmesh.com" + href,
        })
        if len(posts) >= SOCIAL_POSTS_LIMIT:
            break
    return posts


def social_posts_payload(now, twitter_posts, reddit_posts, blog_posts,
                         twitter_ok, reddit_ok, blog_ok):
    """One public payload for all three banners; states let a banner keep
    its static sign when its feed is unreachable while the others stay
    live."""
    return {
        "ok": True,
        "now": int(now),
        "twitter": {
            "handle": "@" + TWITTER_HANDLE,
            "url": TWITTER_PROFILE_URL,
            "state": "ready" if twitter_ok else "unavailable",
            "posts": twitter_posts if twitter_ok else [],
        },
        "reddit": {
            "handle": "r/" + REDDIT_SUBREDDIT,
            "url": REDDIT_PROFILE_URL,
            "state": "ready" if reddit_ok else "unavailable",
            "posts": reddit_posts if reddit_ok else [],
        },
        "blog": {
            "handle": "forkmesh.com/blog",
            "url": BLOG_URL,
            "state": "ready" if blog_ok else "unavailable",
            "posts": blog_posts if blog_ok else [],
        },
    }
