#Linux x86_64 builds
MAKEFILES += linux.mk


LIB_DEVICE_CUDA = out/linux/linux/cuda/lib$(TARGET)_device.so
LIB_TARGET_CUDA = out/linux/linux/cuda/lib$(TARGET).so
CFLAGS_LINUX = $(CFLAGS) -march=native -DREPGAME_LINUX -flto -no-pie
LIBS_LINUX = -L/usr/local/cuda-9.2/lib64 -l m -l GL -l GLU -l GLEW -l glut -pthread

# Server targets might depend on cuda.mk modifications
include cuda.mk
include server.mk

CC_LINUX = g++
LD_LINUX = ld -r
LIB_TARGET_LINUX = out/linux/lib$(TARGET).so

OBJECTS_COMMON_LINUX = $(patsubst src/common/%.cpp, out/linux/common/%.so, $(SRC_COMMON))
OBJECTS_LINUX = $(patsubst src/%.cpp, out/linux/%.so, $(wildcard src/linux/*.cpp))

linux: $(TARGET)

out/linux/%.so: src/%.cpp $(HEADERS) $(MAKEFILES) out
	$(CC_LINUX) $(INCLUDES_COMMON) $(CFLAGS_LINUX) -c $< -o $@

$(TARGET): $(OBJECTS_COMMON_LINUX) $(OBJECTS_LINUX) $(LIB_TARGET_CUDA) $(LIB_DEVICE_CUDA) $(MAKEFILES) out
	$(CC_LINUX) -flto $(CFLAGS_LINUX) $(LIB_TARGET) $(OBJECTS_LINUX) $(OBJECTS_COMMON_LINUX) $(LIB_TARGET_CUDA) $(LIB_DEVICE_CUDA) $(LIBS_LINUX) -o $@

linux-run: linux
	./$(TARGET)

map:
	rm -rf World1

clean-linux: clean-server clean-cuda map
	rm -f $(OBJECTS_COMMON_LINUX)
	rm -f $(OBJECTS_LINUX)
	rm -f $(TARGET)

.PRECIOUS: $(TARGET) $(OBJECTS_LINUX) $(OBJECTS_COMMON_LINUX) $(LIB_TARGET_LINUX)

.PHONY: linux linux-run clean-linux map
