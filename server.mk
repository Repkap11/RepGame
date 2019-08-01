#Linux Server

REP_MAKEFILES += server.mk

#This make file is probably broken, since the code doesn't even work...
REPSERVER := out/linux/server/$(TARGET)Server

OBJECTS_SERVER = $(patsubst src/server/%.cpp,out/server/%.so, $(wildcard src/server/*.cpp))


$(REPSERVER): $(OBJECTS_SERVER) $(REP_MAKEFILES) out
	$(CC_LINUX) -flto $(CFLAGS_LINUX) $(OBJECTS_SERVER) -o $@

server: $(REPSERVER)

out/server/%.so: src/server/%.cpp $(REP_MAKEFILES) out
	$(CC_LINUX) $(CFLAGS_LINUX) -c $< -o $@

.PRECIOUS: $(REPSERVER)

clean-server:
	rm -f $(OBJECTS_SERVER)
	rm -rf out/server

.PHONY: server clean-server
