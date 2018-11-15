#Linux x86_64 builds


LIB_DEVICE_CUDA = out/cuda/lib$(TARGET)_device.so
LIB_TARGET_CUDA = out/cuda/lib$(TARGET).so
CFLAGS_LINUX = $(CFLAGS) -march=native -DREPGAME_LINUX -flto -no-pie
INCLUDES_LINUX = -I include/ -I /usr/include/glm
LIBS_LINUX = -L/usr/local/cuda-9.2/lib64 -l m -l GL -l GLU -l GLEW -l glut -pthread

linux.mk: cuda.mk server.mk

include cuda.mk
include server.mk

CC_LINUX = g++
LD_LINUX = ld -r
LIB_TARGET_LINUX = out/linux/lib$(TARGET).so

OBJECTS_SHARED_LINUX = $(patsubst src/%.cpp, out/linux/%.so, $(ALL_SHARED))
OBJECTS_LINUX = $(patsubst src/%.cpp, out/linux/%.so, $(wildcard src/linux/*.cpp))

linux: $(TARGET)

out/linux/%.so: src/%.cpp $(HEADERS) Makefile
	$(CC_LINUX) $(INCLUDES_LINUX) $(CFLAGS_LINUX) -c $< -o $@

$(TARGET): $(OBJECTS_SHARED_LINUX) $(OBJECTS_LINUX) $(LIB_TARGET_CUDA) $(LIB_DEVICE_CUDA) Makefile
	$(CC_LINUX) -flto $(CFLAGS_LINUX) $(LIB_TARGET) $(OBJECTS_LINUX) $(OBJECTS_SHARED_LINUX) $(LIB_TARGET_CUDA) $(LIB_DEVICE_CUDA) $(LIBS_LINUX) -o $@

linux-run: linux
	./$(TARGET)

clean-linux: map
	rm -f $(TARGET)

.PRECIOUS: $(TARGET) $(OBJECTS_LINUX) $(OBJECTS_SHARED_LINUX) $(LIB_TARGET_LINUX)

.PHONY: linux linux-run clean-linux
