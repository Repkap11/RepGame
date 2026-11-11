#!/usr/bin/env bash
# (Re)start the deployed RepGameServer on repkap11.com inside a dedicated
# tmux session so the logs stay visible. The binary is shipped separately
# by `make server-deploy` (rsync to $REMOTE_BIN_DIR); this script only
# restarts it.
set -euo pipefail

HOST=${SERVER_HOST:-paul@repkap11.com}
REMOTE_BIN_DIR=${SERVER_BIN_DIR:-/home/paul/website/repgame}
SESSION=${SERVER_TMUX_SESSION:-repgame}
ATTACH=${SERVER_ATTACH:-1}

ssh "$HOST" bash -s -- "$REMOTE_BIN_DIR" "$SESSION" <<'REMOTE'
set -euo pipefail
BIN_DIR=$1 SESSION=$2

if systemctl is-active --quiet RepGameServer.service; then
    echo "RepGameServer.service is active; stop it (make server-systemd-uninstall) or restart it instead." >&2
    exit 1
fi

[ -x "$BIN_DIR/RepGameServer" ] || { echo "No binary at $BIN_DIR/RepGameServer — run 'make server-deploy' first." >&2; exit 1; }

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
        PANE=$(tmux new-window -d -P -F '#{pane_id}' -t "=$SESSION:" -n server)
    else
        PANE=$(tmux new-session -d -P -F '#{pane_id}' -s "$SESSION" -n server)
    fi
    tmux set-option -p -t "$PANE" @repgame_server 1
fi

if pgrep -x RepGameServer >/dev/null; then
    echo "A RepGameServer is still running outside tmux session '$SESSION' (pid $(pgrep -x RepGameServer | tr '\n' ' ')). Stop it and re-run." >&2
    exit 1
fi

tmux send-keys -t "$PANE" "cd '$BIN_DIR' && ./RepGameServer" Enter
tmux select-window -t "$PANE"
echo "Server started in tmux session '$SESSION' from $BIN_DIR/RepGameServer"
REMOTE

if [ "$ATTACH" = 1 ]; then
    exec ssh -t "$HOST" tmux attach -t "=$SESSION"
fi
echo "Attach with: ssh -t $HOST tmux attach -t $SESSION"
