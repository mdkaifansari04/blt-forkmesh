# ForkMesh Procedural World Score

The “ForkMesh Focus Tones” control in ForkMesh World generates an original
downtempo ambient instrumental score locally with the Web Audio API. No audio
recording, provider stream, or third-party composition is downloaded,
embedded, proxied, recorded, or rebroadcast.

The score’s source is `cloudflare_worker/public/world/world.js`, function
`createProceduralWorldSoundtrack`. ForkMesh’s project authors dedicate the
generated score and its sequencing code, to the extent they own copyright in
them, to the public domain under
[CC0 1.0 Universal](https://creativecommons.org/publicdomain/zero/1.0/).
No attribution is required. “ForkMesh” remains a project name and is not
licensed as a trademark by this notice.

The deterministic arrangement spans the shared four-hour World day. Sixteen
15-minute chapters vary voicing, chord rotation, pulse density, and timbre;
playback begins at the current shared-world offset and wraps only when the
four-hour cycle wraps. Sound is synthesized in small look-ahead windows after
an explicit user gesture, so there is no mandatory media payload. Mute/stop is
local, and browsers without a usable AudioContext receive an accurate
unavailable state.
