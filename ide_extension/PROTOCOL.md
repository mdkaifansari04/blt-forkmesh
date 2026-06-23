# ForkMesh IDE ↔ desktop protocol

The desktop app and this extension talk through plain files under
`~/.forkmesh/ide/`. No sockets, no ports — it works regardless of which folder
the IDE has open and survives either side restarting.

```
~/.forkmesh/ide/
  registration.json     written by the extension; a heartbeat
  requests/<id>.json     written by the desktop app; consumed by the extension
  responses/<id>.json    written by the extension; an ack/result per request
```

## registration.json (extension → desktop)

Rewritten on activation, on a ~20s heartbeat, and on window focus changes;
deleted on deactivation. The desktop app treats the integration as **active**
when this file exists and `ts` is within the last ~90 seconds.

```json
{
  "ide": "Visual Studio Code",
  "version": "1.96.0",
  "pid": 12345,
  "ts": 1782000000000,
  "workspaces": ["/home/me/projects/forkmesh"],
  "agents": { "claude": true, "codex": true }
}
```

## requests/&lt;id&gt;.json (desktop → extension)

The desktop app writes one file per "run this issue in the IDE" action. The
extension watches the directory, reads + deletes the file, loads the issue from
`repoPath/issues/<issueNumber>/`, and starts the agent in a terminal.

```json
{
  "id": "b3f1c2…",
  "ts": 1782000000000,
  "repoPath": "/home/me/projects/forkmesh",
  "issueNumber": 42,
  "issueTitle": "Fix the thing",
  "provider": "claude"
}
```

`provider` is `"claude"` or `"codex"`. `id` should be unique (a UUID).

## responses/&lt;id&gt;.json (extension → desktop)

```json
{ "id": "b3f1c2…", "ts": 1782000000001, "ok": true, "message": "Started Claude Code on issue #42" }
```

The desktop app may read this to confirm the hand-off, then delete it. Stale
responses can be ignored/swept by age.
