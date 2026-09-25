"""Static contracts for ForkMesh's non-custodial user-facing boundary."""

from pathlib import Path


WORKER_ROOT = Path(__file__).resolve().parents[1]
PUBLIC = WORKER_ROOT / "public"
REPOSITORY = WORKER_ROOT.parent
PUBLIC_ROOTS = (
    REPOSITORY / "app" / "public",
    REPOSITORY / "world" / "public",
)


def _lower(relative_path):
    if relative_path.startswith("world/"):
        root = REPOSITORY / "world" / "public"
    else:
        root = PUBLIC
    text = (root / relative_path).read_text(encoding="utf-8").lower()
    return " ".join(text.split())


def test_interactive_crypto_surfaces_state_custody_and_fund_boundaries():
    required = {
        "mirror-payouts.html": (
            "non-custodial payout wallet",
            "user-owned funds",
            "community-pool funds",
            "pending rewards",
            "completed on-chain transfers",
            "never the pool private key",
        ),
        "network.html": (
            "non-custodial",
            "public payout addresses only",
            "community-funded pool",
            "pending allocations",
            "completed on-chain transfers",
        ),
        "world/world.js": (
            "forkmesh is non-custodial",
            "does not hold or control your funds",
            "user-owned funds",
            "community-pool funds",
            "pending rewards",
            "completed transfers",
        ),
        "dashboard/partials/views/settings.html": (
            "non-custodial",
            "wallet keys and recovery phrases remain on your device",
            "user-owned funds",
            "community-pool funds",
            "pending rewards",
            "completed on-chain transfers",
        ),
        "dashboard/partials/modals.html": (
            "non-custodial",
            "keys and recovery phrases stay on your device",
            "user funds",
            "community pool",
            "pending rewards",
            "completed transfers",
        ),
    }
    for relative_path, phrases in required.items():
        text = _lower(relative_path)
        for phrase in phrases:
            assert phrase in text, (relative_path, phrase)


def test_public_assets_do_not_restore_payment_promises_or_custodial_calls_to_action():
    forbidden = (
        "get paid to mirror",
        "getting paid",
        "you get paid",
        "forkmesh learned to pay the people",
        'let anyone "make it rain"',
        "live earnings calculator",
        "sol / join",
        "preserve code for just $1",
        "used for mirror rewards and bounty payouts",
        "issue-bounty escrow (solana deposit / confirm / split)",
        "one-click solana bounty",
        "per-node deposit key",
        "forkmesh never stores wallet private keys",
        "never stores a wallet private key or recovery phrase",
        "forkmesh never asks for a wallet private key",
        "stores no user wallet private keys",
        "never stores users’ wallet private keys",
        "forkmesh never asks for a private key or recovery phrase",
    )
    for root in PUBLIC_ROOTS:
        for path in sorted(root.rglob("*.html")) + sorted(root.rglob("*.js")):
            text = path.read_text(encoding="utf-8").lower()
            for phrase in forbidden:
                assert phrase not in text, (path.relative_to(root), phrase)


def test_public_copy_does_not_describe_repository_websocket_tunnels_as_active():
    stale = (
        "host tunnel",
        "live tunnel",
        "repo serving are already websocket based",
        "requests to the host tunnel instead of storing packfiles",
        "clones through the two-request tunnel",
    )
    for root in PUBLIC_ROOTS:
        for path in sorted(root.rglob("*.html")) + sorted(root.rglob("*.js")):
            text = " ".join(path.read_text(encoding="utf-8").lower().split())
            for phrase in stale:
                assert phrase not in text, (path.relative_to(root), phrase)
