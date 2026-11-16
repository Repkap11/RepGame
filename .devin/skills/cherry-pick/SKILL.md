---
name: cherry-pick
description: Cherry-pick (never merge) commits from local or remote/agent branches onto master in the current checkout, resolve conflicts the RepGame way (regenerate *_gen files, never renumber block/item ids), and verify with `make linux` plus the extra builds/tests the touched files call for. Use when the user asks to pick changes onto master.
---

# Cherry-pick onto master

The user keeps work spread across local branches (`work`, `worktree`,
`worktree2`, misc topic branches) and remote agent branches
(`origin/devin/*`, `origin/copilot-*`, …). The job: get pending commits
onto `master` via `git cherry-pick` — **never `git merge`** — resolve any
conflicts, and prove the result builds.

**Default scope is every commit on every branch based on master** — the
user expects all queued work landed, not just the branch that prompted
the session (e.g. the one a `git fetch` just pulled down, or the one
visible in the IDE). Only narrow the scope when the user names a
specific branch or commits. Step 1 shows how to enumerate the queue.

Remotes: `origin` (GitHub) is the canonical upstream — `origin/master` is
the real master. `repkap11` is only the user's off-site backup mirror;
never treat its branches as sources of truth or push targets.

## Step 0 — confirm this checkout is master

```sh
git fetch --all --prune
git status -sb          # branch + cleanliness
```

- `master` is normally checked out **in the directory this skill is run
  in** — work there. Do **not** create new worktrees: the user's
  worktree layout is theirs to manage, and a fresh worktree lacks the
  untracked vendored dirs (`imgui_build`, `entt_build`,
  `bedrock-samples`) needed to build.
- If this checkout is on a different branch, or `git worktree list`
  shows `master` checked out in another worktree, stop and ask the user
  — never switch the branch of a checkout they may be using, and never
  make a new worktree to hold master.
- If it's on `master` but dirty, stop and ask the user.

## Step 1 — sync master, pick the commits

```sh
git status -sb                                  # behind origin/master?
git merge --ff-only origin/master               # fast-forward if behind
```

Enumerate everything queued for master — every ref with commits ahead
where master is an ancestor of the tip:

```sh
for ref in $(git for-each-ref --format='%(refname:short)' refs/heads refs/remotes); do
  case "$ref" in master|origin/master|repkap11/*|origin/HEAD) continue;; esac
  ahead=$(git rev-list --count master..$ref 2>/dev/null) || continue
  [ "$ahead" -gt 0 ] || continue
  if git merge-base --is-ancestor master $ref; then
    echo "PICK      $ahead $ref"
  else
    echo "diverged  $ahead $ref"
  fi
done
```

- **PICK** = based on current master — pick ALL of these by default;
  that's the job. Confirm with the user only if the list is longer or
  different than they described.
- **diverged** = forked off older history (old topic branches). Do NOT
  pick these unless the user names them — their diff vs master contains
  unrelated reverts and conflict resolution would be guesswork.
- `repkap11/*` is excluded even when based — it's the backup mirror; the
  same commits should exist under `origin/*` or local branches.

Then dedupe and pick each branch's commits oldest-first:

```sh
git log --oneline master..<ref>                 # what the branch adds
git cherry master <ref>                         # + = not yet on master
git cherry-pick -x $(git rev-list --reverse --no-merges master..<ref>)
```

- `-x` appends `(cherry picked from commit …)` — always use it.
- The same patch often sits on two branches (a commit plus a cherry-pick
  of it, or two agent branches with the same change). `git cherry` uses
  patch-id, so once a patch is on master its twins show `-` — pick each
  unique patch once.
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

Baseline is always `make linux` **in this checkout** (ccache is
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
- The picks land on **local** master in this checkout. Never push
  unless the user asks; never run `make server-deploy` /
  `make server-restart` (live-host side effects).
- If a pick is fundamentally broken (the branch's approach conflicts with
  master design), say so and leave master untouched — don't force it.
