# Chat & slash commands

The chat box is a one-line editor over the message scrollback, opened by
`t` or `Tab` (or `/`, which pre-fills a command slash). Enter submits, Escape
closes, Up/Down recalls previously sent lines
(`ChatUi::sent`, capped at `CHAT_HISTORY_MAX`, deduped, with the
in-progress line stashed in `draft`). While open it owns text capture
exactly like the sign editor — `input.text_input_active` releases the
pointer and key events arrive through the shared `input.text_*`
counters, drained each tick by `repgame_tick_chat` into the pure
`chat_edit_apply_input` (test-driven). Opening is refused while the
sign editor, pause menu or inventory is up. Code lives in
`src/common/game/chat_ui.cpp` (input) and `chat_commands.cpp`
(dispatch); state is `state.ui.chat` (`ChatUi`,
`include/common/game/ui_state.hpp`). Android/VR have no text-input path
yet, so opening is compiled out there; the log and rendering compile
everywhere.

## The log

`state.chat` is a `ChatLog` (`include/common/chat.hpp`) — a deque of
`ChatMessage` capped at `CHAT_LOG_MAX`, pr_debugging each line as it
lands. `ChatKind`: wire kinds `PLAYER`, `SYSTEM`, `WHISPER`, `EMOTE`
plus client-only `INFO` (green, local feedback) and `ERROR` (red).
`chat_format` renders one line per kind (`<name> text`,
`* name action`, `name whispers: text`…); `chat_sanitize_text` strips
non-printables, trims, and truncates to `CHAT_TEXT_MAX`.

The draw pass (`repgame_draw_chat`, called in `draw_ui_pass` before the
first text flush) paints a Clay-declared left-anchored panel above the
hotbar: scrollback rows (`chat_wrap`'d to the panel width, newest at
the bottom) plus the input bar with a `_` cursor when open. It's
skipped under `capture` and while menu/inventory/sign screens are up —
see `docs/ui.md`.

## Multiplayer chat

Plain lines go through `chat_submit` → `Multiplayer::send_chat` → wire
`CHAT`; the server stamps the sender's name (client-sent `from` is
ignored), echoes to every client including the sender, and logs it.
Whispers (`/msg`, `/w`, `/tell`) send a `CHAT` with `kind=WHISPER` and
the target name in `target`; the server routes it to the target and
echoes to the sender (a self-whisper lands once), replying with a
private error for unknown names. Emotes (`/me`) send `kind=EMOTE`.
Player names travel in `PLAYER_NAME` — the client sends its configured
name on WELCOME, the server sanitizes and case-insensitively dedupes it
(`alice` vs `Alice` → `alice_2` within `CHAT_NAME_MAX`), replays
existing names to a new joiner, and broadcasts joins/renames/leaves as
SYSTEM lines. The server keeps no history. Full wire detail:
`docs/multiplayer.md` "Chat".

The local display name resolves at init: `player.dat`'s `name` >
`Player` — sanitized; `/name` sets it live and it persists on exit (the
requested name, never the server-deduped one). See
`docs/persistence.md`.

## Commands

`/`-prefixed lines never hit the wire — `chat_commands.cpp` dispatches
them locally and reports through INFO/ERROR log lines:

| Command | Does |
|---|---|
| `/help [command]` | List commands, or one command's usage |
| `/clear` | Empty the log |
| `/pos` | Feet position, block, chunk, facing |
| `/tp <x> <y> <z>` | Teleport feet to coords (`~` keeps current); `/tp <player>` goes to a remote avatar. Zeroes velocity/fall state |
| `/give <item> [count]` | Add items to the hotbar like a pickup |
| `/gamemode <survival\|creative>` (alias `/gm`, `s`/`c`/`0`/`1`) | Switch game mode |
| `/time set\|add <ticks\|day\|noon\|night\|midnight>` | World clock; names keep the current day; bare `/time`/`query` reports it |
| `/fly [on\|off]` | Toggle flight |
| `/peaceful <on\|off>` | Hostile-mob immunity + regen |
| `/setblock <x> <y> <z> <block>` | Place one block (`~` = current pos) |
| `/fill <x1> <y1> <z1> <x2> <y2> <z2> <block>` | Fill an inclusive box (≤ 65536 cells) |
| `/summon <mob> [x y z]` | Spawn a mob (default: 3 blocks ahead) |
| `/name <name>` | Set display name (sanitized, persisted) |
| `/list` | Who is online |
| `/msg <player> <message...>` (aliases `/w`, `/tell`) | Whisper |
| `/me <action...>` | Emote (`* name action`) |

These run client-side on the local world — in multiplayer the world
mutations are optimistic local edits (server-authoritative state can
overrule), and `/time` broadcasts like the PgUp/PgDn keys do.

## Testing

- `make tests TESTS=chat` — sanitizer/wrap/format/log units plus
  `chat_edit_apply_input` and command dispatch
- `python3 tools/mp_test.py -k chat` — real-server scenario: names +
  dedupe (`alice` → `alice_2` while `Alice` is online), broadcast,
  whisper routing, unknown-target error (mp-test skill)
- `make scenarios SCENARIOS=chat` — scripted run exercising the UI open
  path, commands, and a chat screenshot
  (`tests/scenarios/chat.rds`)
