# ForkMesh World Soundtrack and Focus Music

The “ForkMesh Focus Tones” control in ForkMesh World generates an original
downtempo ambient instrumental score locally with the Web Audio API. No audio
recording, provider stream, or third-party composition is downloaded,
embedded, proxied, recorded, or rebroadcast.

The “Listen to the ForkMesh song” control plays *ForkMesh Forever (Indie Pop)*,
a first-party track published by the project at
`cloudflare_worker/public/assets/songs/ForkMeshForever(IndiePop).mp3`. It is
served from ForkMesh’s own origin, starts only after that button is pressed,
stops with the same Mute/stop control, and is never streamed from or relayed to
a third-party provider.

## Bundled focus-music selections

The Media section also includes three long-form ambient instrumentals for
coding and deep work. They play locally, are never relayed through multiplayer
sockets, and run for 22 to 45 minutes before repeating. “Cosmic Waves” is
selected by default. ForkMesh attempts to start the selected track as the World
opens; when a browser requires a user gesture, playback waits for the visitor's
first interaction. Play, pause, stop, mute, and volume controls remain local.

Each track's source page publishes that recording under
[CC0 1.0 Universal](https://creativecommons.org/publicdomain/zero/1.0/).
CC0 permits copying, modification, distribution, and performance, including
commercial use, without requesting permission. Attribution is optional; it is
included here to preserve provenance:

- *Cosmic Waves* by **HoliznaCC0** — 33:04.
  [Source and per-track license declaration](https://freemusicarchive.org/music/holiznacc0/space-sleep-meditation/cosmic-waves/).
- *DreamScape* by **HoliznaCC0** — 21:59.
  [Source and per-track license declaration](https://freemusicarchive.org/music/holiznacc0/space-sleep-meditation/dreamscape/).
- *Too Brief A Time To Be Anything* by **HoliznaCC0** — 45:00.
  [Source and per-track license declaration](https://freemusicarchive.org/music/holiznacc0/space-sleep-meditation/too-brief-a-time-to-be-anything/).

The publisher labels all three tracks instrumental and not AI-generated. The
source MP3 files were transcoded to stereo Ogg Vorbis at 44.1 kHz with a nominal
80 kbit/s target and metadata removed. This reduces the bundled transfer size
from about 208 MiB to about 46 MiB while keeping every static asset below the
deployment platform's 25 MiB per-file limit. Exact source URLs, source and
bundled SHA-256 digests, source and bundled durations, byte sizes, codec
details, and modification notes are recorded in
`cloudflare_worker/public/assets/music/music-manifest.json`.

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
