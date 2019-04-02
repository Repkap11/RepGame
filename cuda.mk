#Linux Cuda
MAKEFILES += cuda.mk

OBJECTS_CUDA := $(patsubst src/linux/cuda/%.cu,out/linux/linux/cuda/%.o, $(wildcard src/linux/cuda/*.cu))
CLEAN_CUDA := $(OBJECTS_CUDA)

CC_CUDA := /usr/bin/nvcc
#If the current system has the cuda compiler, use CUDA to accelerate terrain gen
ifneq ("$(wildcard $(CC_CUDA))","")

LIB_DEVICE_CUDA := out/linux/linux/cuda/lib$(TARGET)_device.o
LIB_TARGET_CUDA := out/linux/linux/cuda/lib$(TARGET).o
CLEAN_CUDA += $(LIB_DEVICE_CUDA) $(LIB_TARGET_CUDA)

CFLAGS_CUDA := -arch=sm_35 -Xcompiler -fPIC -DREPGAME_LINUX
CFLAGS_CUDA_COMPILE := -x cu -dc -c
CFLAGS_CUDA_LINK_DEVICE := --lib
CFLAGS_CUDA_LINK_HOST := -dlink

LIBS_LINUX += -lcudart -L/usr/local/cuda-9.2/lib64
CFLAGS_LINUX += -DLOAD_WITH_CUDA
OBJECTS_COMMON_LINUX += $(LIB_TARGET_CUDA) $(LIB_DEVICE_CUDA)

endif

out/linux/linux/cuda/%.o: src/linux/cuda/%.cu $(HEADERS) $(MAKEFILES) | out
	$(CC_CUDA) $(CFLAGS_CUDA_COMPILE) $(INCLUDES_COMMON) $(CFLAGS_CUDA) $< -o $@

$(LIB_TARGET_CUDA): $(LIB_DEVICE_CUDA) $(MAKEFILES) | out
	$(CC_CUDA) $(CFLAGS_CUDA) $(CFLAGS_CUDA_LINK_HOST) $(LIB_DEVICE_CUDA) -o $@

$(LIB_DEVICE_CUDA): $(OBJECTS_CUDA) $(MAKEFILES) | out
	$(CC_CUDA) $(CFLAGS_CUDA) $(CFLAGS_CUDA_LINK_DEVICE) $(OBJECTS_CUDA) -o $@

cuda: $(LIB_TARGET_CUDA) $(LIB_DEVICE_CUDA)

clean-linux: clean-cuda

clean-cuda:
	rm -f $(CLEAN_CUDA)

.PHONY: cuda clean-cuda