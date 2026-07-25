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

The Media section also includes three local, game-style focus tracks. They are
downloaded only after an explicit Play action, loop on that device, and are
never relayed through multiplayer sockets. “Heavenly Loop” is selected by
default, but selection is not playback consent and does not cause autoplay.

All three source pages publish the work under
[CC0 1.0 Universal](https://creativecommons.org/publicdomain/zero/1.0/).
CC0 permits copying, modification, distribution, and performance, including
commercial use, without requesting permission. Attribution is optional; it is
included here to preserve provenance:

- *Heavenly Loop* by **isaiah658** — a short seamless ambient loop.
  [Source and license declaration](https://opengameart.org/content/heavenly-loop).
- *Forgotten Victory* by **yd** — an atmospheric background loop lasting about
  four minutes.
  [Source and license declaration](https://opengameart.org/content/forgotten-victory).
- *Tarlite Trycor Slumber Area* by **Tozan** — an RPG-style background piece
  lasting more than nine minutes.
  [Source and license declaration](https://opengameart.org/content/tarlite-trycor-slumber-area).

The first two source files were transcoded to Ogg Vorbis quality 2 with metadata
removed, reducing their combined transfer size from about 5.1 MB to about
2.7 MB. The Tarlite audio is retained byte-for-byte because its source encoding
is already compact. Exact source URLs, source and bundled SHA-256 digests,
durations, byte sizes, and modification notes are recorded in
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
