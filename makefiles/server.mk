#Linux Server

REP_MAKEFILES += makefiles/server.mk

REPSERVER := $(TARGET)Server

OBJECTS_SERVER = $(patsubst src/server/%.cpp,out/server/%.o, $(wildcard src/server/*.cpp))
DEPS_SERVER := $(patsubst src/server/%.cpp,out/server/%.d, $(wildcard src/server/*.cpp))
SERVER_DIRS := $(patsubst src/server%,out/server%,$(shell find src/server -type d))


out/server/$(REPSERVER): $(OBJECTS_SERVER) $(REP_MAKEFILES) $(call GUARD,CC_LINUX CFLAGS_LINUX) | out/server
	$(call CHECK,CC_LINUX CFLAGS_LINUX)
	$(CC_LINUX) -flto $(CFLAGS_LINUX) $(OBJECTS_SERVER) -o $@

all: server

server: out/server/$(REPSERVER)

out/server/%.o: src/server/%.cpp $(call GUARD,CC_LINUX INCLUDES_COMMON CFLAGS_LINUX) | out/server
	$(call CHECK,CC_LINUX INCLUDES_COMMON CFLAGS_LINUX)
	@#Use g++ to build o file and a dependecy tree .d file for every cpp file
	$(CC_LINUX) $(INCLUDES_COMMON) $(CFLAGS_LINUX) -MMD -MP -MF $(patsubst %.o,%.d,$@) -MT $(patsubst %.d,%.o,$@) -c $< -o $@

#Include these .d files, so the dependicies are known for secondary builds.
-include $(DEPS_SERVER)

.PRECIOUS: out/server/$(REPSERVER) $(OBJECTS_SERVER)

out/server: | out
	mkdir -p $(SERVER_DIRS)
	touch $@

server-run: server
	./out/server/$(REPSERVER)

clean: clean-server

clean-server:
	rm -rf out/server

.PHONY: server clean-server server-run
