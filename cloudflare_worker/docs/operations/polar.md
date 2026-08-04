# Polar memberships

ForkMesh creates Polar checkout sessions on the server and grants the existing
`supporting_member` platform role only after a signed subscription webhook.
`supporter` and `pro` are stored as billing tiers, not authorization role names.
The browser never receives the Organization Access Token or webhook secret.

## Configure Polar

1. Create recurring Supporter and Pro products in the Polar organization.
2. Create an Organization Access Token with `checkouts:write` and
   `customer_sessions:write`.
3. Add a raw webhook endpoint at
   `https://forkmesh.com/api/integrations/polar/webhook` and give it a fresh
   secret.
4. Subscribe it to `subscription.created`, `subscription.active`,
   `subscription.uncanceled`, `subscription.canceled`,
   `subscription.past_due`, `subscription.updated`, and
   `subscription.revoked`.
5. Put the token, secret, organization UUID, and both product UUIDs in the
   gitignored `.env.production` using `.env.production.example` as the
   template, then run `./deploy.sh secrets` and deploy normally.

For sandbox testing, set `POLAR_API_BASE_URL=https://sandbox-api.polar.sh` and
use sandbox credentials and product IDs. No other API origin is accepted.

## Runtime contract

- `GET /api/integrations/polar/account` returns the signed-in account's small
  membership projection.
- `POST /api/integrations/polar/checkout` accepts `{"tier":"supporter"}` or
  `{"tier":"pro"}` and returns a validated Polar checkout URL.
- `POST /api/integrations/polar/portal` creates a short-lived customer portal
  session for a previously linked customer.
- `POST /api/integrations/polar/webhook` validates the exact raw body with the
  Standard Webhooks headers, rejects stale signatures, and processes each
  webhook ID once.

Provider customer, product, and subscription identifiers are encrypted under
`DATA_KEY`. D1 indexes contain only keyed blind indexes. End-of-period
cancellations retain the role until Polar reports the subscription revoked or
its current period expires; past-due, unpaid, and revoked states remove the
membership role unless another subscription for the account is still active.
Provider modification times keep delayed webhook deliveries from overwriting a
newer subscription state.
