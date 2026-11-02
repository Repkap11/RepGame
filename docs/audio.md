# Audio

OGG Vorbis decoded by vendored `stb_vorbis` (`include/common/vendor/`).
Two backends in `src/common/audio.cpp`: SDL3 device-bound `SDL_AudioStream`s
(Linux, Windows, ARM, SteamFrame, embedded blobs) and an SDL2 main-thread
mix callback (WASM — no blob embedding, sounds ship in the WASMFS preload).
Android always compiles the no-op stubs from `include/common/audio.hpp`;
other platforms compile them too when `REPGAME_AUDIO=0` or `sounds/` is
absent.

Pipeline (mirrors the bitmap blobs): `tools/sounds_manifest.txt` lists
`name|source|type|title` per sound (`source` is a file under `sounds/`,
which is gitignored — the music is copyrighted and stays local). `make`
runs `tools/convert_sounds.sh convert <name>` per entry (ffmpeg →
`out/sounds/<name>.ogg`, needs the `ffmpeg` package), embeds each as a
`MK_BLOB(out_sounds, <name>, ogg)` object (WASM instead copies it into the
`out/wasm/fs/sounds/` preload tree), and regenerates
`out/include/sounds_manifest.hpp` (music/sfx tables included by
`src/common/audio.cpp` — `REPGAME_WASM` selects path- instead of
blob-carrying tables). If `sounds/` is absent, `REPGAME_AUDIO` defaults
to `0` and the build compiles the stub — set `REPGAME_AUDIO=0` to force it
even with sources present. The flatpak sandbox has no ffmpeg, so
`make flatpak` runs `sounds-prebuilt` on the host first (into
`prebuilt/sounds/`, same pattern as `gpu-prebuilt`) and the sandbox links
those via `SOUND_OGG_DIR=prebuilt/sounds`.

Runtime: `audio_init` opens the default playback device (failure → silent,
logged once). Music is streamed — decoded frames are pulled from stb_vorbis
on demand (SDL3: the audio thread fills the persistent music stream; WASM:
the mix callback, which Emscripten runs on the main thread) — `audio_tick`
picks the next random track after a uniform `[gap/4, gap]` gap (the first
track starts ~3s in). SFX are one-shots (SDL3 binds a stream per play, WASM
claims a voice slot); manifest names `dig_stone_1..4` form
the `dig_stone` pool, so `audio_play_sfx("dig_stone")` picks a random
variant. `audio_block_dig_pool(block_id)` maps BlockID → pool
(dig_stone/grass/gravel/sand/snow/wood/coral, glass) — the break and place
hooks both use it; `audio_block_step_pool(block_id)` is the same map for
footsteps (step_* pools, glass → step_stone). For the currently wired SFX
hooks see the `audio_play_sfx` call sites. The ImGui overlay shows
`Music: <title>` while a track plays.

IMPORTANT: the device is opened with `SDL_OpenAudioDevice` and the music
stream is *bound* to it — do NOT use `SDL_OpenAudioDeviceStream`, whose
device refuses further stream bindings and silently kills all SFX. On WASM
the browser suspends the AudioContext until the first user gesture; SDL2's
Emscripten driver auto-resumes it (the callback keeps running into a
throwaway buffer meanwhile).

Sources: `sounds/mc_pack` is a Minecraft assets tree (WAV) — only the
overworld music (game/, water/, creative/) is in the rotation; nether/end/
menu/records are excluded. `make mc_pack_build` downloads it: a tarball of
just the manifest-listed WAVs hosted at repkap11.com/files/repgame
(copyrighted, so untracked — not an `install` dep).

- `REPGAME_AUDIO` — `0` disables all audio (default on when built with
  audio). Audio is also skipped entirely in capture mode.
- `REPGAME_MUSIC` — `0` mutes music only (SFX still play).
- `REPGAME_MUSIC_VOLUME` / `REPGAME_SFX_VOLUME` — linear gains 0..1
  (defaults 0.5 / 1.0).
- `REPGAME_MUSIC_GAP` — max seconds of silence between tracks (default
  240; gap is uniform in `[gap/4, gap]`, `0` = continuous shuffle).
