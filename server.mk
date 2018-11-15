REPSERVER = $(TARGET)Server 

OBJECTS_SERVER = $(patsubst server/src/%.cpp, out/server/%.so, $(wildcard server/src/*.cpp))


$(REPSERVER): $(OBJECTS_SERVER) Makefile
	$(CC_LINUX) -flto $(CFLAGS_LINUX) $(OBJECTS_SERVER) -o $@

server: $(REPSERVER)

out/server/%.so: server/src/%.cpp Makefile 
	$(CC_LINUX) $(CFLAGS_LINUX) -c $< -o $@

.PRECIOUS: $(REPSERVER)

clean-server:
	rm -rf $(REPSERVER)

.PHONY: server
