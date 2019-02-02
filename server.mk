#Linux Server

MAKEFILES += server.mk

REPSERVER := $(TARGET)Server

OBJECTS_SERVER = $(patsubst src/server/%.cpp,out/server/%.so, $(wildcard src/server/*.cpp))


$(REPSERVER): $(OBJECTS_SERVER) $(MAKEFILES) out
	$(CC_LINUX) -flto $(CFLAGS_LINUX) $(OBJECTS_SERVER) -o $@

server: $(REPSERVER)

out/server/%.so: src/server/%.cpp $(MAKEFILES) out
	$(CC_LINUX) $(CFLAGS_LINUX) -c $< -o $@

.PRECIOUS: $(REPSERVER)

clean-server:
	rm -f $(OBJECTS_SERVER)
	rm -rf out/server

.PHONY: server clean-server
