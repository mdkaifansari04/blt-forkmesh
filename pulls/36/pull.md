---
schema: forkmesh-pull-v1
number: 36
title: Prioritize from README: use Claude Code OAuth, not the metered API
base: main
head: agent/adhoc-47-i-got-this-message-when-prioritzing-even-though
status: open
ts: 1782518213654
author: 7ZMh_2s_IOTPiKoZ6K5eDe4AclTYh_JzqJ7pK2lovT4
authorName: newnewnode
sig: lZTOIL_P_ZUcmi5T1_XXtgt7gaFKIx-b6VB0neMMtqDXFdYN47OiAkwrH_F_gPvaJ3ITO2JprlUHLW6oiO3wAQ
---

When **Claude Code** is selected in the Prioritize-from-README agent picker, the request now authenticates with the claude.ai subscription OAuth token instead of a stored Claude API key.

Previously the code read the Claude API key first and only fell back to the OAuth token when that key was empty, so a user signed in to Claude Code who also had an API key configured hit the metered API and failed with `HTTP 400: Your credit balance is too low`.

- `claude-code` now uses the OAuth token exclusively (ignores any stored API key)
- Clearer "Sign in to Claude Code first" notice when no token is found
- `Claude API` and `OpenAI API` keep using their own keys

