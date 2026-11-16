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
track starts ~3s in). A `music_set` change (dimension hop) drops a queued
old-set pick, cuts the in-flight track via `music_skip`, and shortens the
gap to ~2 s so the new dimension's music takes over promptly. SFX are one-shots (SDL3 binds a stream per play, WASM
claims a voice slot); manifest names `dig_stone_1..4` form
the `dig_stone` pool, so `audio_play_sfx("dig_stone")` picks a random
variant. `audio_block_dig_pool(block_id)` maps BlockID → break pool via
`block_material()` (audio.cpp) — stone/grass/gravel/sand/snow/wood/coral
plus cloth (wool/carpets), wet_grass (mud/moss), scaffold, bamboo, vine,
waterlily, sweet_berry_bush, chain, lantern, honey, and the nether sets
(netherrack, nylium, soul_sand/soil, basalt, nether_bricks/ore, netherite,
ancient_debris, stem, fungus, roots, nether_sprouts, netherwart,
bone_block, shroomlight); glass → the `glass` shatter. The `dig_*` pools
fold the pack's break and place variants together.
`audio_block_place_pool(block_id)` is the same map for placement except
glass-type blocks → dig_stone (the pack's `random/glass*` is the shatter
only — there is no glass place "tink"); `audio_block_step_pool(block_id)`
is the same map for footsteps (step_* pools, glass → step_stone, and the
no-step materials fall back to step_grass/step_stone). For the
currently wired SFX hooks see the `audio_play_sfx` call sites — mechanisms
(doors, pistons, buttons, pressure plates, fence gates, repeaters), boats
(paddle strokes on a cadence, `vehicle_water_sfx` in vehicles.cpp —
water under the hull picks paddle_water, a beached boat paddle_land),
buckets, flint & steel + portals, mob melee (swing whoosh plus a
crit/knockback/strong hit tier), farming (hoe till, crop plant), anvil
landings, eating (eat + burp), and damage (drown/fire_hurt, classic_hurt
is one of the player_hurt variants).
Looping clips (there is no looped-playback API) are emulated by
re-triggering the one-shot once its clip length has elapsed — a
retriggered clip is the loop. Plays that need to end before their clip
does are tracked: `audio_play_sfx_handle` returns an id and
`audio_sfx_stop` fades the stream out over ~50 ms (stale/zero handles
are safe no-ops). Minecarts do this in
`vehicle_rail_sfx` (vehicles.cpp): a RAIL-physics vehicle on a rail
moving faster than ~0.1 blocks/s plays
`minecart_base` every ~1.9 s with gain following a sqrt ramp of
horizontal speed, and
the cart the local player rides adds the `minecart_inside` loop on a
~5.7 s retrigger. The clips are mastered ~20dB hotter than the rest of
the pack, so convert_sounds.sh normalizes them to -34/-38dB mean and
`CART_SFX_VOLUME` applies a further in-code cap; stopping, derailing,
dismounting or removing the vehicle stops the
in-flight plays instead of letting them tail out. Ambience uses the same
retrigger trick: while the player's head is under water `process_movement`
(player.cpp) plays underwater_enter/exit on the transition, restarts
underwater_ambience on a ~55 s timer and sprinkles underwater_addition on
a 4–13 s random timer; `audio_tick` does the nether equivalent — the
nether_mood pool every ~30–75 s and nether_addition every ~6–20 s, keyed
off `music_set == DIM_MUSIC_NETHER` so it follows the same dimension
signal as the music. Cave ambience rides the same
mood mechanism: `advance_clocks` (game_tick.cpp) probes the packed light
volume once per tick — `light_probe` on the sky channel at the head cell,
an O(1) read with no scan — and passes `in_cave` (sky 0 = fully enclosed)
to `audio_tick`, which plays the `cave` pool every ~60–120 s after a
~10 s entry delay, overworld only (the nether band always reads sky 0
and has its own mood pool); the timer resets when the player steps back
into the open. Positional
SFX share `audio_distance_gain(dist)` (audio.hpp): an inverse-distance
falloff — full gain within 4 blocks, 0.5 at 8, 0.25 at 16, 0.125 at 32 —
silent past `AUDIO_SFX_RANGE` (64); `audio_play_sfx_gain` drops gain<=0
plays without claiming a stream. The ImGui
overlay shows `Music: <title>` while a track plays.

IMPORTANT: the device is opened with `SDL_OpenAudioDevice` and the music
stream is *bound* to it — do NOT use `SDL_OpenAudioDeviceStream`, whose
device refuses further stream bindings and silently kills all SFX. On WASM
the browser suspends the AudioContext until the first user gesture; SDL2's
Emscripten driver auto-resumes it (the callback keeps running into a
throwaway buffer meanwhile).

Sources: `sounds/mc_pack` is a Minecraft assets tree (WAV) — the
overworld tracks (game/, water/, creative/) rotate in the overworld and
the nether tracks rotate in the nether band; the manifest's `set` column
tags each entry and `audio.cpp` picks the set from the player's dimension
(`docs/dimensions.md`) — switching dimensions cuts the current track and
starts the new set. End/menu/records are excluded.
`make mc_pack_build` downloads it: a tarball of
just the manifest-listed WAVs hosted at repkap11.com/files/repgame
(copyrighted, so untracked — not an `install` dep).

Sourcing policy: repkap11.com is the only upstream for sound assets.
Never download WAVs/OGGs from Mojang's asset CDN (or any third party)
except with repkap11's explicit permission. When the manifest gains
sources, pack them into `mc_pack_full.tar.gz` and ask repkap11 to
re-upload it before merging so fresh checkouts stay complete.

- `REPGAME_AUDIO` — `0` disables all audio (default on when built with
  audio). Audio is also skipped entirely in capture mode.
- `REPGAME_MUSIC` — `0` mutes music only (SFX still play).
- `REPGAME_MUSIC_VOLUME` / `REPGAME_SFX_VOLUME` — linear gains 0..1
  (defaults 0.5 / 1.0).
- `REPGAME_MUSIC_GAP` — max seconds of silence between tracks (default
  240; gap is uniform in `[gap/4, gap]`, `0` = continuous shuffle).
