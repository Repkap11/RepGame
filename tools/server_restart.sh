#!/usr/bin/env bash
# Restart the deployed RepGameServer on repkap11.com via its user-level
# systemd unit (installed by `make server-systemd-install`). SIGTERM from
# systemctl stop triggers the server's graceful flush. Logs:
# `make server-logs-live` (journalctl --user -u RepGameServer -f).
set -euo pipefail

HOST=${SERVER_HOST:-paul@repkap11.com}
SERVICE=${SERVER_SERVICE:-RepGameServer}

ssh "$HOST" "systemctl --user restart '$SERVICE' && systemctl --user is-active '$SERVICE'"
echo "Attach logs with: ssh -t $HOST journalctl --user -u $SERVICE -f"
