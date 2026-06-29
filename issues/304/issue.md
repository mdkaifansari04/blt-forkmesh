---
schema: forkmesh-issue-v1
number: 304
title: Design a release-binary publishing system
status: closed
labels: []
milestone: 
priority: 0
progress: 0
assignees: []
createdAt: 1782703519749
author: 7ZMh_2s_IOTPiKoZ6K5eDe4AclTYh_JzqJ7pK2lovT4
authorName: newnewnode
bountyUsd: 0.00
bountyAddress: 
bountyStatus: 
type: open
id: open-304
ts: 1782703519749
attachments: []
sig: pZZtt6fTdJdPpQ7HfhdpTkAYiXGztCCQcZXQxuMbmOxXuI9Gim1E9zXeQCIf-pVpeK7oQ7G1kGh6--noA6afCg
---

Use this method:

- Release binaries must not be committed into Git repositories.
- Releases are based on immutable Git tags, such as v1.2.3.
- CI builds binaries from a tag.
- CI makes then available for donwload in a similar way the repo is available
- Each release should include checksums, optional signatures, release notes, and per-platform artifacts.
- The Git server should provide a release page with stable download links.

Please design the system architecture, database model, storage layout, API endpoints, permissions model, and CI integration flow.

Assume the Git server already supports:
- repositories
- users and organizations
- Git refs and tags
- authentication tokens

Include:

1. Recommended user workflow:
   - create tag
   - CI builds artifacts
   - CI creates or updates release
   - CI uploads assets
   - users download assets

2. Data model:
   - releases
   - release_assets
   - artifact files
   - checksums
   - signatures
   - audit events

3. Storage strategy:
   - local disk option
   - content-addressed storage if useful
   - deduplication considerations
   - cleanup and retention rules

5. Security:
   - token scopes
   - who can publish releases
   - validating that releases match Git tags
   - immutable releases vs editable metadata
   - checksum generation

6. Download behavior:
   - stable URLs
   - rate limits
   - bandwidth accounting

7. Edge cases:
   - re-uploading an asset with the same name
   - deleting a tag after a release exists
   - force-pushed tags
   - partially failed uploads
   - very large files
   - interrupted downloads
   - concurrent CI jobs
