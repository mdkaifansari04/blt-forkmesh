# ForkMesh issues

This folder is ForkMesh's own issue tracker, stored **inside the repository** so
issues travel with every clone and mirror. The desktop client's **Issues** tab
reads and writes these files; the relay only relays submissions from people who
don't have write access (see "Cross-user" below).

## Layout

One folder per issue, named by its number. Everything is **markdown with
frontmatter** — the structured data lives in each file's `---` frontmatter and
the prose lives in the body. Images go directly in the issue folder.

```
issues/
  labels.json          repo-level label definitions (name + color)
  milestones.json      repo-level milestone definitions (title, due, status)
  1/
    issue.md           frontmatter (issue metadata + the open event) + description
    0002-comment.md    a later event: frontmatter + (for comments) body
    0003-status.md     ...append-only, numbered in order
    <sha8>.png         image attachments, referenced by filename
  2/
    ...
```

## `issues/<N>/issue.md`

The frontmatter carries the issue-level metadata **and** the open event; the
body is the description.

```md
---
schema: forkmesh-issue-v1
number: 1
title: …
status: open            # open | closed
labels: [security]       # names defined in labels.json
milestone: v1            # title in milestones.json, or empty
priority: 1              # 1 highest, 99 lowest, 0 unset
assignees: []            # pubkeys (or node names) responsible
createdAt: 1781600000000
author: <pubkey-b64url>  # raw 32-byte Ed25519 key, base64url
authorName: node1
type: open
id: open-1
ts: 1781600000000
attachments: []          # image filenames in this folder
sig: <base64url>
---

The issue description in **markdown**.
```

### Later events: `issues/<N>/NNNN-<type>.md` (append-only)

Each subsequent change is its own numbered markdown file (ordered by the `NNNN`
prefix). The frontmatter holds the signed event; the body (for `comment`/`edit`)
holds the text. Each event carries `type`, `id`, `author` (pubkey), `authorName`,
`ts` (ms) and `sig`, plus:

| type        | extra fields            | meaning                  |
|-------------|-------------------------|--------------------------|
| `comment`   | `attachments`           | a comment (body in file) |
| `edit`      | `target`, `attachments` | edits event `target` (body) |
| `title`     | `title`                 | renames the issue        |
| `status`    | `status` (`open`/`closed`) | close / reopen        |
| `labels`    | `labels` (array)        | set the issue's labels   |
| `milestone` | `milestone` (string)    | set the issue's milestone|
| `priority`  | `priority` (1–99, or 0) | set/clear numeric priority|
| `assignees` | `assignees` (array)     | set the issue's assignees|
| `delete`    | `target` (`<eventId>`/`self`) | tombstone an event/issue |

Lists use inline `[a, b]` form. `labels.json` / `milestones.json` stay JSON
(small machine-managed config).

## Signatures

Signatures are **raw Ed25519** over an explicit canonical string (not over
canonicalized JSON — that avoids C++/Python serialization drift), encoded
base64url without padding, exactly like the client's `ForkMeshIdentity`.

```
canonical =
  "forkmesh-issue-event-v1" \n
  <type> \n
  <issue number> \n
  <author pubkey> \n
  <ts> \n
  sha256hex(content)
```

`content` is type-specific, with fields joined by a NUL (`\x00`). `body` is the
markdown body of the event's file with leading/trailing newlines (`\n`/`\r`)
stripped — so the blank line after the frontmatter and a trailing newline do not
change the signature. `attachments` is the comma-joined list of image filenames:

| type        | content                                   |
|-------------|-------------------------------------------|
| `open`      | `title \0 body \0 attachments.join(",")`  |
| `comment`   | `body \0 attachments.join(",")`           |
| `edit`      | `body \0 attachments.join(",")`           |
| `title`     | `title`                                   |
| `status`    | `status`                                  |
| `labels`    | `labels.join(",")`                        |
| `milestone` | `milestone` (or "")                       |
| `priority`  | decimal priority (`1`–`99`, or `0`)        |
| `assignees` | `assignees.join(",")`                     |
| `delete`    | `target`                                  |

The relay verifies submissions with the same scheme (`ed25519_verify` in
`cloudflare_worker/src/entry.py`).

## Cross-user

Issues live in the owner's repo. People without write access submit signed
issues/comments to the relay inbox at
`POST /api/repo/<owner>/<repo>/issues`; the owner's node verifies the signature,
merges them into this folder, and commits — so the canonical record is always
the git history.
