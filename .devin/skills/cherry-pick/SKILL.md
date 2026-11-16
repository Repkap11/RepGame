---
name: cherry-pick
description: Cherry-pick (never merge) commits from local or remote/agent branches onto master in a dedicated worktree, resolve conflicts the RepGame way (regenerate *_gen files, never renumber block/item ids), and verify with `make linux` plus the extra builds/tests the touched files call for. Use when the user asks to pick changes onto master.
---

# Cherry-pick onto master

The user keeps work spread across local branches (`work`, `worktree`,
`worktree2`, misc topic branches) and remote agent branches
(`origin/devin/*`, `origin/copilot-*`, …). The job: get specific commits
onto `master` via `git cherry-pick` — **never `git merge`** — resolve any
conflicts, and prove the result builds.

Remotes: `origin` (GitHub) is the canonical upstream — `origin/master` is
the real master. `repkap11` is only the user's off-site backup mirror;
never treat its branches as sources of truth or push targets.

## Step 0 — find where master can be checked out

```sh
git fetch --all --prune
git worktree list
```

- `master` is usually **not** checked out anywhere (the main checkout is
  on `work`; worktrees are on their own branches). Never switch the
  branch of a worktree the user is actively using — create or reuse a
  dedicated master worktree instead:
  ```sh
  git worktree add ~/Software/Rep/RepGame_master master   # once; reuse after
  ```
  If `git worktree list` already shows a checkout on `[master]`, work
  there — but only if `git status` is clean. If it's dirty, stop and ask
  the user.
- Do **not** put the worktree under `~/.repgame/rep-dbg/wt/` — rep-dbg's
  gc prunes dirty worktrees in there by age. A sibling of the other
  worktrees (`~/Software/Rep/…`) is safe.
- A fresh worktree can't build until it has the untracked vendored dirs.
  Symlink them from the main checkout (same trick as
  `tools/rep_dbg/worktree.py`), or run `make install` there:
  ```sh
  main=$(git worktree list --porcelain | awk '/^worktree/{print $2; exit}')
  ln -s "$main/imgui_build" "$main/entt_build" ~/Software/Rep/RepGame_master/
  ```

## Step 1 — sync master, pick the commits

```sh
cd ~/Software/Rep/RepGame_master
git status -sb                                  # behind origin/master?
git merge --ff-only origin/master               # fast-forward if behind
```

Figure out exactly what to pick — confirm with the user if the branch has
more commits than they expect:

```sh
git log --oneline master..<ref>                 # what the branch adds
git cherry master <ref>                         # + = not yet on master
git cherry-pick -x $(git rev-list --reverse --no-merges master..<ref>)
```

- `-x` appends `(cherry picked from commit …)` — always use it.
- Pick individual shas when the user names specific commits; use the
  `rev-list` form for a whole branch (skips merge commits).
- Sources with **uncommitted** changes can't be picked — ask the user to
  commit in that worktree first; don't commit in their tree yourself.

## Step 2 — conflicts

`git status` lists unmerged paths; fix, `git add`, `git cherry-pick
--continue`. `git cherry-pick --abort` undoes the whole pick.

RepGame-specific rules:

- **Generated files — never hand-resolve.** `*_gen.*`,
  `include/common/item_definitions_gen.inc`, `bitmaps/blocks.png`,
  `bitmaps/mobs.png`. Resolve the `data/**` JSON side, take either side of
  the generated file, then after the pick completes regenerate and amend:
  `make blocks-import` / `make mobs-import` (see the `add-content` /
  `add-mob` skills). Rerunning the importer must produce a clean diff —
  a dirty diff means the resolution was wrong.
- **Block/item id collisions.** Ids are never renumbered (1249 is
  reserved). If master and the picked commit both allocated the same id,
  the *incoming* content gets fresh ids — bump `data/**` and regenerate,
  don't renumber existing entries.
- Everything else (C++, docs, AGENTS.md): normal manual resolution. When
  the right side isn't obvious, prefer master's structure and re-apply
  the incoming change's intent; ask the user when genuinely ambiguous.

## Step 3 — verify the build

Baseline is always `make linux` **in the master worktree** (ccache is
shared, so it's not a cold build). Then add rows based on what changed:

| Picked commits touched | Also run |
|---|---|
| `src/common/`, `include/` | `make tests` |
| `src/server/`, `include/server/`, net protocol (`docs/multiplayer.md` area) | `make test` (tests + server self-test) |
| `tools/mp_test.py`, `src/common/mp_test.cpp`, entity/net sync code | `make mp-test` |
| `tests/scenarios/` | `make scenarios` |
| `src/wasm/`, WGSL in `src/shaders/` | `make wasm` (needs emsdk — docs/platforms.md) |
| `src/linux/` | `make linux` covers it; add `make linux_debug` if suspicious |
| `src/windows` paths, `makefiles/windows.mk`, `REPGAME_WINDOWS` code | `make windows` (mingw cross — check before assuming POSIX env calls exist) |
| `src/android/`, `makefiles/android.mk` | `make android` — heavy; skip unless the change is Android-related |
| `makefiles/appimage.mk`, `makefiles/flatpak.mk`, packaging only | `make appimage`/`flatpak` — long & slow; skip unless the change targets them |
| `data/**` (blocks/items/recipes/textures) | `make blocks-import`/`mobs-import` then `git diff --exit-code` |
| `CMakeLists.txt`, `cmake/`, `makefiles/` | `make linux` **and** `make server` (separate cmake path) |
| `docs/`, `*.md` only | no build needed |

Stale-pch red herring after cherry-picks touching headers: clang fatal
"modified since the precompiled header was built" → `make pch` (not a
real failure).

## Step 4 — wrap up

- Report: `git log --oneline` of the new commits on master, what
  conflicted and how it was resolved, and which builds/tests ran.
- The picks land on **local** master in the master worktree. Never push
  unless the user asks; never run `make server-deploy` /
  `make server-restart` (live-host side effects).
- If a pick is fundamentally broken (the branch's approach conflicts with
  master design), say so and leave master untouched — don't force it.
