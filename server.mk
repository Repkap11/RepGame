#Linux Server

REP_MAKEFILES += server.mk

REPSERVER := out/server/$(TARGET)Server

OBJECTS_SERVER = $(patsubst src/server/%.cpp,out/server/%.o, $(wildcard src/server/*.cpp))
DEPS_SERVER := $(patsubst src/server/%.cpp,out/server/%.d, $(wildcard src/server/*.cpp))
SERVER_DIRS := $(patsubst src/server%,out/server%,$(shell find src/server -type d))


$(REPSERVER): $(OBJECTS_SERVER) $(REP_MAKEFILES) | out/server
	$(CC_LINUX) -flto $(CFLAGS_LINUX) $(OBJECTS_SERVER) -o $@

server: $(REPSERVER)


out/server/%.o: src/server/%.cpp $(REP_MAKEFILES) | out/server
	@#Use g++ to build o file and a dependecy tree .d file for every cpp file
	$(CC_LINUX) $(INCLUDES_COMMON) $(CFLAGS_LINUX) -MMD -MP -MF $(patsubst %.o,%.d,$@) -MT $(patsubst %.d,%.o,$@) -c $< -o $@

#Include these .d files, so the dependicies are known for secondary builds.
-include $(DEPS_SERVER)

.PRECIOUS: $(REPSERVER) $(OBJECTS_SERVER)

out/server: | out
	mkdir -p $(SERVER_DIRS)

server-run: server
	./$(REPSERVER)

clean-server:
	rm -f $(OBJECTS_SERVER)
	rm -rf out/server

.PHONY: server clean-server server-run
