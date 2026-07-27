# External repository imports

ForkMesh keeps GitHub, GitLab, and Codeberg metadata imports in a separate data
model from its live mirror catalog:

- `/api/repositories` contains published ForkMesh mirrors.
- `/api/repository-imports` contains external metadata entries and stubs.

An external entry is never routed, cloned, or described as mirrored until its
status is linked to a separately published and verified ForkMesh mirror.
ForkMesh does not own or control an external repository.

## Importing metadata

An authenticated user can submit:

```http
POST /api/repository-imports
Content-Type: application/json

{
  "sourceUrl": "https://github.com/owner/repository",
  "providerToken": "request-only token, if needed",
  "targetOwner": "user-or-organization",
  "mode": "import",
  "sessionToken": "ForkMesh account session"
}
```

`mode` is `import` or `stub`. Only canonical `github.com`, `gitlab.com`, and
`codeberg.org` HTTPS repository URLs are accepted. `targetOwner` may be the
signed-in user or an organization where that user is an owner or administrator.
Provider access tokens are used for that request and never written to D1, a
Worker log, the response, or browser storage. Private repositories require a
token that can read the repository.

The World importer can expand a Codeberg user/organization profile into its
public repositories before importing them one at a time:

```http
POST /api/repository-imports/discover
Content-Type: application/json

{
  "sourceUrl": "https://codeberg.org/m33",
  "sessionToken": "ForkMesh account session"
}
```

Discovery is authenticated, paginated, restricted to canonical Codeberg URLs,
and bounded to 200 repositories. It never imports a private repository.

A community member can list a public stub without claiming ownership. If a
provider administrator later imports the same stable provider repository id
with a current token, they can adopt moderation of the entry; the original
lister remains attributed. A read-only collaborator cannot adopt or overwrite a
private import.

The importer calls only bounded metadata endpoints for the repository,
languages, branches, contributors, recent commits, issues, pull/merge requests,
and releases. It does not call file, blob, raw-content, archive, or source-code
endpoints. Private source is not sent to an image generator or other external
model.

Provider rate-limit headers are honored. When a limit is reached, further
metadata calls stop and the response identifies the incomplete sections.

## Visibility and statuses

Anonymous listing queries filter to `is_private=0` in D1 before any record is
decrypted. A private id returns the same `404` response when it is absent,
guessed, or requested by the wrong account.

Supported status values are:

- `external_repository`
- `stub_only`
- `mirror_requested`
- `partially_mirrored`
- `actively_mirrored`
- `mirror_unavailable`
- `archived`

The two mirrored states require a matching catalog record. `actively_mirrored`
also requires a current live-host heartbeat.

An owner or organization administrator can delete an external listing with
`DELETE /api/repository-imports/{id}`. The website provides select-all and bulk
deletion controls for only the imports the current account can manage. Deleting
the listing never deletes anything from the source provider.

## Mirror volunteers

`POST /api/repository-imports/{id}/mirror-volunteers` records a request only
when the authenticated account supplies a mirror node it owns. Public reads
show the number of volunteers, not their node/device names.

## Contributor invitations

Only an importer whose provider metadata proves Maintainer/administrator
permission (or a ForkMesh moderator) can create invitations:

```http
POST /api/repository-imports/{id}/invitations
```

The contributor must occur in imported contributor metadata. The inviter must
reference a durable eligibility record; a request-time `basisConfirmed`
boolean is rejected. The supported evidence sources are:

- `public_for_invitations` — a public provider contributor snapshot included an
  explicit public contact address. ForkMesh immediately converts it to a keyed
  blind index and does not copy the raw address into the import record.
- `prior_consent` — the recipient used a signed-in account with a verified
  email to create an import-specific consent record at
  `POST /api/repository-imports/{id}/invitation-consent`.
- `owner_supplied` — a current provider-verified administrator registered the
  address and a source reference at
  `POST /api/repository-imports/{id}/invitation-provenance`. ForkMesh retains a
  keyed digest of the reference, not the raw reference.

The invitation request supplies the returned `provenanceId`. The Worker
matches that record to the exact repository, contributor, and recipient blind
indexes both before the preview and atomically again when reserving the send.
Revoked, mismatched, guessed, and boolean-only evidence fails closed.

Sending requires an explicit `confirm: true`; otherwise the endpoint returns an
invitation preview. Limits apply per inviter, per repository, and per recipient.
Every message identifies the inviter and includes token-protected opt-out and
abuse-report controls. The raw invitation address exists only during the
delivery call; the durable invitation keeps a masked address, a keyed blind
index for matching/cooldown/opt-out checks, and a one-way action-token digest.
Provenance creation, consent/revocation, and sends append metadata-only
sensitive-action audit records.

Provider administrator proof expires after 24 hours. Refresh the import with a
new one-request token before another owner-only action; ForkMesh never retains
the token to refresh permission silently.

Email links use a non-mutating `GET` confirmation response. The recipient must
submit `POST` to opt out or report, so automated email-link scanners cannot
change state.

## Repository logos

Every external entry has a deterministic, locally generated abstract SVG based
on non-sensitive repository name, language, topic, and description metadata.
Owner-authorized local imports may also supply bounded file-structure labels,
framework names, and a project category; file contents are never logo inputs.
It does not copy provider/framework marks and does not use an external model.
The response marks this as `aiGenerated: false`.

Authenticated users can submit PNG, JPEG, or WebP alternatives through:

```http
POST /api/repository-imports/{id}/logo-suggestions
```

The uploader must confirm that they hold the necessary rights. Suggestions
remain pending until a provider-verified owner or ForkMesh moderator approves
one. A verified owner can also replace the generated logo directly. Uploaded
SVG is rejected to avoid script-capable image content, and only one approved
official logo can exist per external entry.
