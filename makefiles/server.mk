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

# (Re)start the deployed binary in tmux session 'repgame' on repkap11.com
# (see tools/server_restart.sh for overrides).
server-restart:
	./tools/server_restart.sh

server-test: server
	./out/server/$(REPSERVER) --selftest

# End-to-end multiplayer scenarios: real server + headless clients driven
# through REPGAME_MP_CTL / REPGAME_MP_DUMP (tools/mp_test.py).
mp-test: linux server
	python3 tools/mp_test.py

server-systemd-install: server
	sudo cp $(REPSERVER).service /etc/systemd/system/$(REPSERVER).service
	sudo systemctl daemon-reload
	sudo systemctl enable $(REPSERVER).service
	sudo systemctl restart $(REPSERVER).service

server-systemd-status:
	sudo systemctl status $(REPSERVER).service

server-systemd-logs:
	journalctl -u $(REPSERVER).service -f

server-systemd-uninstall:
	sudo systemctl disable --now $(REPSERVER).service
	sudo rm /etc/systemd/system/$(REPSERVER).service
	sudo systemctl daemon-reload

clean: clean-server

clean-server:
	rm -rf out/server

.PHONY: server clean-server server-run server-test mp-test server-deploy server-restart server-install
