# Security Policy

ForkMesh is an early-stage, open-source desktop node and relay prototype. We
take security seriously and appreciate reports that help keep the network and
its users safe.

## Supported Versions

ForkMesh is pre-1.0 and ships from `main`. Only the latest published build and
the current `main` branch receive security fixes. Please reproduce issues
against the most recent version before reporting.

| Version        | Supported          |
| -------------- | ------------------ |
| `main` (latest)| :white_check_mark: |
| Older builds   | :x:                |

## Reporting a Vulnerability

**Please do not report security vulnerabilities through public issues.**
ForkMesh issues are signed and published in-repo under [`issues/`](issues/), so
a public report would disclose the problem before a fix is available.

Instead, report privately:

- Email the maintainers at **security@forkmesh.com**, or
- Reach out through the project website at https://forkmesh.com

Please include:

- a description of the vulnerability and its impact,
- the affected component (desktop node, Cloudflare relay, identity/crypto,
  mirroring, or website),
- steps to reproduce or a proof of concept,
- the version or commit you tested against.

We aim to acknowledge reports within a few days. Once a fix is available we will
coordinate disclosure and credit reporters who wish to be named.

## Scope

Security-relevant areas of ForkMesh include:

- **Identity and signing** — local Ed25519 keys used to sign profile,
  repository, issue, and pull-request metadata.
- **Relay chat** — payloads are encrypted client-side with AES-256-GCM.
  Default rooms use a relay-derived authenticated shared key: clients apply
  PBKDF2-HMAC-SHA256 with 210,000 rounds, while the relay can derive the same
  key and therefore can read message content. The relay retains at most 500
  persisted frames per room for 7 days. A participant-supplied passphrase,
  shared out of band in a client that supports it, is required for
  confidentiality from the relay. Coves and encrypted mirrors use separate
  per-object encryption boundaries.
- **Mirroring** — bare Git mirrors fetched from remotes and served on demand.
- **Donations** — Solana donation addresses published on profiles and
  repositories.

Issues that are not directly exploitable (for example, missing best-practice
hardening with no demonstrated impact) are still welcome but may be triaged at
lower priority.

## Out of Scope

- Vulnerabilities in third-party dependencies should be reported upstream,
  though we appreciate a heads-up if ForkMesh is affected.
- Social-engineering, physical attacks, and denial-of-service against a single
  self-hosted node.

Thank you for helping keep ForkMesh and its community secure.
</content>
</invoke>
