#Linux Server

REPSERVER := $(TARGET)Server

CMAKE_SERVER_DFLAGS := -DCMAKE_CXX_COMPILER=$(LINUX_CXX) -DREPGAME_AUDIO=$(REPGAME_AUDIO)

# Thin wrapper over CMake (see CMakeLists.txt — REPGAME_BUILD_SERVER). The
# server preset is RelWithDebInfo with CLIENT=OFF so hosts without SDL3/GLEW/wayland
# still configure. Deploys ship this binary (make server-deploy); repkap11.com
# no longer builds — it's too small to compile optimized clang builds.
out/server/$(REPSERVER): FORCE
	$(call cmake_build,server,out/server/cmake,$(CMAKE_SERVER_DFLAGS))

all: server

server: out/server/$(REPSERVER)

# Server-only host setup (repkap11.com): just what `make server` and
# tools/server_deploy.sh need — no SDL3/GLEW/CUDA/HIP/flatpak/emsdk like the
# full `make install`. entt is the only third-party source the server uses.
SERVER_PACKAGES := clang ccache cmake ninja-build wget libarchive-tools tmux

# entt_build runs after apt (it needs wget + bsdtar), hence the sub-make.
server-install:
	sudo apt-get install -y $(SERVER_PACKAGES)
	$(MAKE) entt_build

server-run: server
	./out/server/$(REPSERVER)

# Ship the release binary to repkap11.com's website dir like the other
# deploy targets — the VPS is too small to compile optimized builds, and
# both ends run the same toolchain. rsync's temp-file+rename is safe on a
# running binary. Restart it afterwards with `make server-restart`.
SERVER_DEPLOY_REMOTE := paul@repkap11.com:/home/paul/website/${TARGET_LOWER}
server-deploy: server
	rsync out/server/$(REPSERVER) $(SERVER_DEPLOY_REMOTE)/
	@echo "Deployed. Run 'make server-restart' to restart the live server."

# Restart the deployed binary on repkap11.com via its user-level systemd
# unit (see tools/server_restart.sh for overrides).
server-restart:
	./tools/server_restart.sh

server-test: server
	./out/server/$(REPSERVER) --selftest

# End-to-end multiplayer scenarios: real server + headless clients driven
# through REPGAME_MP_CTL / REPGAME_MP_DUMP (tools/mp_test.py).
mp-test: linux server
	python3 tools/mp_test.py

# User-level systemd service on repkap11.com (systemctl --user) — no sudo
# needed. Linger makes it start on boot without a login session.
SERVER_SSH := paul@repkap11.com
server-systemd-install:
	ssh $(SERVER_SSH) 'mkdir -p ~/.config/systemd/user'
	scp $(REPSERVER).service $(SERVER_SSH):~/.config/systemd/user/
	ssh $(SERVER_SSH) 'systemctl --user daemon-reload && systemctl --user enable $(REPSERVER).service'
	ssh $(SERVER_SSH) 'loginctl enable-linger $(USER) 2>/dev/null || echo "NOTE: run sudo loginctl enable-linger $(USER) on the VPS for boot persistence"'

server-systemd-status:
	ssh $(SERVER_SSH) 'systemctl --user status $(REPSERVER).service'

server-systemd-logs:
	ssh -t $(SERVER_SSH) 'journalctl --user -u $(REPSERVER).service -f'

server-systemd-uninstall:
	ssh $(SERVER_SSH) 'systemctl --user disable --now $(REPSERVER).service || true; rm -f ~/.config/systemd/user/$(REPSERVER).service; systemctl --user daemon-reload'

clean: clean-server

clean-server:
	rm -rf out/server

.PHONY: server clean-server server-run server-test mp-test server-deploy server-restart server-install server-systemd-install server-systemd-status server-systemd-logs server-systemd-uninstall
