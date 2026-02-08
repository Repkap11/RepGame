#Linux Server

REP_MAKEFILES += makefiles/server.mk

REPSERVER := $(TARGET)Server

OBJECTS_SERVER = $(patsubst src/server/%.cpp,out/server/%.o, $(wildcard src/server/*.cpp) out/linux/debug/common/utils/file_utils.o out/linux/debug/common/utils/map_storage.o)
DEPS_SERVER := $(patsubst src/server/%.cpp,out/server/%.d, $(wildcard src/server/*.cpp))
SERVER_DIRS := $(patsubst src/server%,out/server%,$(shell find src/server -type d))


out/server/$(REPSERVER): $(OBJECTS_SERVER) $(REP_MAKEFILES) | out/server
	$(CC_LINUX) -flto $(CFLAGS_LINUX) $(CFLAGS_LINUX_DEBUG) $(OBJECTS_SERVER) -o $@

all: server

server: out/server/$(REPSERVER)

out/server/%.o: src/server/%.cpp | out/server
	@#Use g++ to build o file and a dependecy tree .d file for every cpp file
	$(CC_LINUX) $(INCLUDES_COMMON) $(CFLAGS_LINUX) $(CFLAGS_LINUX_DEBUG) -MMD -MP -MF $(patsubst %.o,%.d,$@) -MT $(patsubst %.d,%.o,$@) -c $< -o $@

#Include these .d files, so the dependicies are known for secondary builds.
-include $(DEPS_SERVER)

.PRECIOUS: out/server/$(REPSERVER) $(OBJECTS_SERVER)

out/server: | out
	mkdir -p $(SERVER_DIRS)
	touch $@

server-run: server
	./out/server/$(REPSERVER)

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

.PHONY: server clean-server server-run
