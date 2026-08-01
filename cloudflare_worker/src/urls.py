"""URL route patterns for the ForkMesh relay Worker.

This module holds every compiled path regex the Worker's router (`Default._route`
in ``entry.py``) matches requests against, plus the git smart-HTTP endpoint
patterns. Keeping the route table in one small module makes the request surface
easy to scan without paging through the 11k-line worker, and lets the Worker and
tests import a single source of truth for "which path shape maps to which
handler".

Only pure ``re``-compiled patterns live here — no runtime state, no js/workers
imports — so this module loads standalone (the test suite parses it the same way
it parses ``entry.py``).
"""

import re



ROOM_RE = re.compile(r"^/api/room/([^/]+)/(?:ws|clients)$")
REPO_ROOM_RE = re.compile(r"^/api/repo/([^/]+)/([^/]+)/rooms/([^/]+)/(?:ws|clients)$")


CHAT_CHANNELS_RE = re.compile(r"^/api/chat/channels/?$")
CHAT_CHANNEL_MEMBERS_RE = re.compile(
    r"^/api/chat/channels/([0-9a-f]{32})/members/?$")
CHAT_CHANNEL_ROOM_ACCESS_RE = re.compile(
    r"^/api/chat/channels/([0-9a-f]{32})/room-access/?$")



CHAT_CHANNEL_HISTORY_RE = re.compile(
    r"^/api/chat/channels/([0-9a-f]{32})/history/?$")
CHAT_CHANNEL_WS_RE = re.compile(
    r"^/api/chat/channels/([0-9a-f]{32})/ws/?$")


CHAT_DIRECT_MESSAGES_RE = re.compile(r"^/api/chat/direct-messages/?$")
CHAT_DIRECT_MESSAGE_USERS_RE = re.compile(
    r"^/api/chat/direct-messages/users/?$")
CHAT_DIRECT_MESSAGE_ROOM_ACCESS_RE = re.compile(
    r"^/api/chat/direct-messages/([0-9a-f]{32})/room-access/?$")
CHAT_DIRECT_MESSAGE_READ_RE = re.compile(
    r"^/api/chat/direct-messages/([0-9a-f]{32})/read/?$")
CHAT_DIRECT_MESSAGE_WS_RE = re.compile(
    r"^/api/chat/direct-messages/([0-9a-f]{32})/ws/?$")

REPO_ISSUES_RE = re.compile(r"^/api/repo/([^/]+)/([^/]+)/issues$")

REPO_PULLS_RE = re.compile(r"^/api/repo/([^/]+)/([^/]+)/pulls$")



REPO_PULL_MERGE_RE = re.compile(
    r"^/api/repo/([^/]+)/([^/]+)/pulls/([1-9][0-9]{0,8})/merge$")



REPO_ACTION_RUNS_RE = re.compile(
    r"^/api/repo/([^/]+)/([^/]+)/actions/runs$")


REPO_DISCUSSIONS_RE = re.compile(r"^/api/repo/([^/]+)/([^/]+)/discussions$")





REPO_PENDING_RE = re.compile(r"^/api/repo/([^/]+)/([^/]+)/pending$")


REPO_SUBSCRIBE_RE = re.compile(r"^/api/repo/([^/]+)/([^/]+)/subscribe$")


REPO_BOUNTY_RE = re.compile(r"^/api/repo/([^/]+)/([^/]+)/bounty$")


REPO_SHARES_RE = re.compile(r"^/api/repo/([^/]+)/([^/]+)/shares$")




REPO_SECURITY_SCANS_RE = re.compile(
    r"^/api/repo/([^/]+)/([^/]+)/security-scans/"
    r"(lease|ingest|latest|history|triage)$")

REPO_MIRRORS_RE = re.compile(r"^/api/repo/([^/]+)/([^/]+)/mirrors$")
# Exact-node content reachability. Unlike the ordinary repository read route,
# this never fails over to another mirror: operators use it to verify that the
# named node itself can return the repository README.
REPO_MIRROR_REACHABILITY_RE = re.compile(
    r"^/api/repo/([^/]+)/([^/]+)/mirrors/([^/]+)/reachability$")
# Catalog-facing About details editable from the dashboard by the source owner.
REPO_ABOUT_RE = re.compile(r"^/api/repo/([^/]+)/([^/]+)/about$")
REPO_LOGO_RE = re.compile(r"^/api/repo/([^/]+)/([^/]+)/logo$")
REPO_LOGO_SUGGESTIONS_RE = re.compile(
    r"^/api/repo/([^/]+)/([^/]+)/logo-suggestions$")




REPO_AGENTS_RE = re.compile(r"^/api/repo/([^/]+)/([^/]+)/agents$")
REPO_AGENTS_LIST_RE = re.compile(r"^/api/repo/([^/]+)/([^/]+)/agents/list$")
REPO_AGENTS_ACK_RE = re.compile(r"^/api/repo/([^/]+)/([^/]+)/agents/ack$")
REPO_AGENTS_PROMPT_RE = re.compile(
    r"^/api/repo/([^/]+)/([^/]+)/agents/([^/]+)/prompt$")



REPO_AGENTS_TRANSCRIPT_RE = re.compile(
    r"^/api/repo/([^/]+)/([^/]+)/agents/([^/]+)/transcript$")



ORG_AGENT_BOTS_RE = re.compile(
    r"^/api/orgs/([^/]+)/repos/([^/]+)/agent-bots$")
ORG_AGENT_BOT_RE = re.compile(
    r"^/api/orgs/([^/]+)/repos/([^/]+)/agent-bots/([^/]+)$")
REPO_ORG_AGENT_JOBS_RE = re.compile(
    r"^/api/repo/([^/]+)/([^/]+)/org-agent-jobs$")
REPO_ORG_AGENT_JOB_RESULT_RE = re.compile(
    r"^/api/repo/([^/]+)/([^/]+)/org-agent-jobs/([1-9][0-9]{0,18})/result$")



REPO_PRIVACY_RE = re.compile(r"^/api/repo/([^/]+)/([^/]+)/privacy$")




PRIVATE_REPLICA_ACCESS_RE = re.compile(
    r"^/api/private-replicas/([0-9a-f]{64})$")



REPO_HOST_RE = re.compile(
    r"^/api/repo/([^/]+)/([^/]+)/(host|tree|blobs|blob|raw|history|commit|compare|branches|search|stats|sizes)$")


RELEASE_BLOB_RE = re.compile(
    r"^/api/repo/([^/]+)/([^/]+)/releases/blob/sha256/([0-9a-f]{64})$")

REPO_RELEASE_DOWNLOADS_RE = re.compile(
    r"^/api/repo/([^/]+)/([^/]+)/releases/downloads$")

GIT_INFO_RE = re.compile(r"^/([^/]+)/([^/]+)/info/refs$")
GIT_PACK_RE = re.compile(r"^/([^/]+)/([^/]+)/git-upload-pack$")


GIT_RECEIVE_RE = re.compile(r"^/([^/]+)/([^/]+)/git-receive-pack$")





ORGS_RE = re.compile(r"^/api/orgs$")
ORG_RE = re.compile(r"^/api/orgs/([^/]+)$")
ORG_MEMBERS_RE = re.compile(r"^/api/orgs/([^/]+)/members$")
ORG_TEAMS_RE = re.compile(r"^/api/orgs/([^/]+)/teams$")
ORG_TEAM_MEMBERS_RE = re.compile(r"^/api/orgs/([^/]+)/teams/([^/]+)/members$")
ORG_REPOS_RE = re.compile(r"^/api/orgs/([^/]+)/repos$")
ORG_BOT_TOKENS_RE = re.compile(r"^/api/orgs/([^/]+)/bot-tokens$")




ORG_DISCORD_RE = re.compile(
    r"^/api/orgs/([^/]+)/discord(?:/(messages|oauth/start))?$")
DISCORD_OAUTH_CALLBACK_RE = re.compile(
    r"^/api/integrations/discord/callback$")
BOT_SESSION_RE = re.compile(r"^/api/bot/session$")


ORG_SUCCESSION_RE = re.compile(
    r"^/api/orgs/([^/]+)/succession(?:/([^/]+))?$")



BADGES_RE = re.compile(r"^/api/badges$")
BADGE_ACCOUNT_RE = re.compile(r"^/api/badges/([^/]+)$")



ORG_FEDIVERSE_RE = re.compile(r"^/api/orgs/([^/]+)/fediverse$")



REPO_API_PREFIX_RE = re.compile(r"^/api/repo/([^/]+)/([^/]+)(?:/.*)?$")


ACCOUNTS_RE = re.compile(r"^/api/accounts/([^/]+)$")

ACCOUNT_CONTRIBUTIONS_RE = re.compile(
    r"^/api/accounts/([^/]+)/contributions$"
)

ACCOUNT_FOLLOW_RE = re.compile(r"^/api/accounts/([^/]+)/follow$")

REFERRAL_LINK_RE = re.compile(r"^/r/([^/]+)$")



REFERRAL_CARD_RE = re.compile(r"^/api/referrals/([^/]+)/card\.png$")


AP_USER_RE = re.compile(r"^/ap/users/([^/]+)$")
AP_USER_SUB_RE = re.compile(
    r"^/ap/users/([^/]+)/(inbox|outbox|followers|following)$")

AP_REPO_RE = re.compile(r"^/ap/repos/([^/]+)/([^/]+)$")
AP_REPO_SUB_RE = re.compile(
    r"^/ap/repos/([^/]+)/([^/]+)/(inbox|outbox|followers|following)$")

AP_OBJECT_RE = re.compile(r"^/ap/o/([0-9a-f]{32})$")


AP_OBJECT_MEDIA_RE = re.compile(r"^/ap/o/([0-9a-f]{32})/media/([0-9]+)$")

REPO_FEDI_COMMENTS_RE = re.compile(
    r"^/api/repo/([^/]+)/([^/]+)/fedi-comments$")


REPO_AP_PUBLISH_RE = re.compile(r"^/api/repo/([^/]+)/([^/]+)/ap-publish$")



REPO_AP_DIGEST_RE = re.compile(
    r"^/api/repo/([^/]+)/([^/]+)/fediverse-digest$")



REPO_AP_POSTS_RE = re.compile(r"^/api/repo/([^/]+)/([^/]+)/ap-posts$")


REPO_MEDIA_RE = re.compile(r"^/api/repo/([^/]+)/([^/]+)/media/(logo|banner)\.png$")


REPO_CARD_RE = re.compile(r"^/api/repo/([^/]+)/([^/]+)/card\.png$")



REPO_STAR_RE = re.compile(r"^/api/repo/([^/]+)/([^/]+)/star$")
