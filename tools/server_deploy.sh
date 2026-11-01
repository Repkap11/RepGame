#!/usr/bin/env bash
# Push HEAD to the server's git remote, rebuild RepGameServer on the server,
# and (re)start it inside a dedicated tmux session so the logs stay visible.
set -euo pipefail

HOST=${SERVER_HOST:-paul@repkap11.com}
REMOTE_DIR=${SERVER_DIR:-/home/paul/Software/RepGame/RepGame}
GIT_REMOTE=${SERVER_GIT_REMOTE:-repkap11}
BRANCH=${SERVER_BRANCH:-master}
SESSION=${SERVER_TMUX_SESSION:-repgame}
ATTACH=${SERVER_ATTACH:-1}

cd "$(git rev-parse --show-toplevel)"

if [ -n "$(git status --porcelain)" ]; then
    echo "WARNING: uncommitted changes are NOT included in this deploy:" >&2
    git status --short >&2
fi

SHA=$(git rev-parse HEAD)
echo "Pushing $(git log -1 --format='%h %s') to $GIT_REMOTE/$BRANCH"
git push "$GIT_REMOTE" "HEAD:refs/heads/$BRANCH"

ssh "$HOST" bash -s -- "$REMOTE_DIR" "$BRANCH" "$SHA" "$SESSION" <<'REMOTE'
set -euo pipefail
DIR=$1 BRANCH=$2 SHA=$3 SESSION=$4
cd "$DIR"

git fetch origin "$BRANCH"
git merge --ff-only "origin/$BRANCH"
[ "$(git rev-parse HEAD)" = "$SHA" ] || { echo "Server HEAD $(git rev-parse --short HEAD) != pushed ${SHA:0:7}" >&2; exit 1; }

# Build before stopping anything, so a broken build leaves the old server up.
make server

if systemctl is-active --quiet RepGameServer.service; then
    echo "RepGameServer.service is active; stop it (make server-systemd-uninstall) or restart it instead." >&2
    exit 1
fi

# The server pane is tagged with @repgame_server (window names get clobbered by
# automatic-rename, so they can't be used to find it again).
PANE=$(tmux list-panes -s -t "=$SESSION" -F '#{pane_id} #{@repgame_server}' 2>/dev/null | awk '$2 == 1 { print $1; exit }' || true)
if [ -n "$PANE" ]; then
    # Ctrl-C runs the server's graceful shutdown (flushAll); wait for it.
    tmux send-keys -t "$PANE" C-c
    for _ in $(seq 60); do
        pgrep -x RepGameServer >/dev/null || break
        sleep 0.5
    done
else
    if tmux has-session -t "=$SESSION" 2>/dev/null; then
        PANE=$(tmux new-window -d -P -F '#{pane_id}' -t "=$SESSION:" -n server -c "$DIR")
    else
        PANE=$(tmux new-session -d -P -F '#{pane_id}' -s "$SESSION" -n server -c "$DIR")
    fi
    tmux set-option -p -t "$PANE" @repgame_server 1
fi

if pgrep -x RepGameServer >/dev/null; then
    echo "A RepGameServer is still running outside tmux session '$SESSION' (pid $(pgrep -x RepGameServer | tr '\n' ' ')). Stop it and re-run." >&2
    exit 1
fi

tmux send-keys -t "$PANE" "cd '$DIR' && make server-run" Enter
tmux select-window -t "$PANE"
echo "Server started in tmux session '$SESSION' at ${SHA:0:7}"
REMOTE

if [ "$ATTACH" = 1 ]; then
    exec ssh -t "$HOST" tmux attach -t "=$SESSION"
fi
echo "Attach with: ssh -t $HOST tmux attach -t $SESSION"
