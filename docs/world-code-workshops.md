# World Code Workshops

ForkMesh World can run a bounded browser-side inspection of an authorized
repository snapshot and persist the resulting recommendation as a
collaborative Code Workshop.

## Commit-matched inputs

The first tree response resolves a full Git commit. Every later tree, batch
blob, statistics, dependency, and coverage read carries that commit as `ref`.
The local mirror gateway resolves relative imports/includes only to files in
that committed tree and reports direct adjacency plus bounded transitive
dependency depth. It reads at most 320 source/artifact blobs and 6 MiB per
commit, from a bounded 20,000-file repository index. Larger snapshots are
marked partial. Supported committed coverage formats are LCOV,
Istanbul/coverage.py JSON, Cobertura XML, and JaCoCo XML.

The file graph does not infer security state from a filename. It requests the
Worker's latest authoritative scan and applies per-file finding/review state
only when an owner-authorized rich artifact names the exact graph commit.
Public clipboard totals remain visible, but cannot be used to invent per-file
findings.

Contributor nodes come only from the commit-matched statistics response.
Issue and pull-request nodes come from bounded listings of the committed
`.forkmesh/issues/{open,closed}` and `pulls/` record directories. Each becomes
a distinct selectable Three.js object linked to the file or metadata-directory
node that actually contains it. If a directory cannot be read at the same
commit, the scene may show an explicitly aggregate reported-count node, but it
does not invent individual records or reuse data from another revision.

## Persistence and authorization

Authenticated clients use `/api/world/workshops`:

- `GET /api/world/workshops?repository=owner/repo&commit=<hash>` lists sessions
  explicitly shared with the caller.
- `POST /api/world/workshops` saves a repository + commit + run scoped session
  and its first immutable result ID.
- `GET|PATCH /api/world/workshops/<session-id>` loads or updates a session.
- `POST /api/world/workshops/<session-id>/results` saves another immutable
  result version.
- `GET /api/world/workshops/<session-id>/results/<result-id>` loads one result.
- `POST|DELETE /api/world/workshops/<session-id>/participants` changes explicit
  viewer/editor membership; only the session owner can do this.
- `GET|POST /api/world/workshops/<session-id>/events` reads incremental events
  using the returned integer cursor or adds a bounded participant comment.

Every request requires an account session and an explicit participant row.
Owners and editors may save results; viewers may read and comment. Platform
administrators do not silently inherit workshop access.

Storage is bounded to 100 sessions per owner, 30 participants per session, 50
immutable result versions per session, and 1,000 collaboration events per
session. API responses are bounded more tightly.

Repository names, source paths, result reports, participant labels, and event
bodies are encrypted with the Worker's existing row encryption. D1 stores a
keyed repository blind index for exact lookup. Commit hashes and random run
IDs are scoped by that blind index and are not sufficient to discover a
private repository.

## Collaboration links

The World report exposes an authorization-checked participant link. Its live
chat link includes repository and run scope and reuses the dashboard's existing
encrypted chat WebSocket rather than opening another socket. Persisted updates
use cursor-based incremental reads and an explicit refresh.

An owner can continue a saved result at
`/<owner>/<repo>/agents?workshopSession=...&workshopResult=...&run=...`.
The repository dashboard validates all three opaque values, opens its existing
owner-only agent composer, and prefills context. Nothing is sent to an agent
until the owner submits that prompt.
