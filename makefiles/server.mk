#Linux Server

REPSERVER := $(TARGET)Server

CMAKE_SERVER_DFLAGS := -DCMAKE_CXX_COMPILER=$(LINUX_CXX) -DREPGAME_AUDIO=$(REPGAME_AUDIO)

# Thin wrapper over CMake (see CMakeLists.txt — REPGAME_BUILD_SERVER). The
# server preset is Debug with CLIENT=OFF so hosts without SDL3/GLEW/wayland
# still configure (repkap11.com builds it via tools/server_deploy.sh).
out/server/$(REPSERVER): FORCE
	$(call cmake_build,server,out/server/cmake,$(CMAKE_SERVER_DFLAGS))

all: server

server: out/server/$(REPSERVER)

server-run: server
	./out/server/$(REPSERVER)

# Push HEAD to the repkap11 remote, rebuild on repkap11.com and restart the
# server in tmux session 'repgame' (see tools/server_deploy.sh for overrides).
server-deploy:
	./tools/server_deploy.sh

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

.PHONY: server clean-server server-run server-test mp-test server-deploy
