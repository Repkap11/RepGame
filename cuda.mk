#Linux Cuda

OBJECTS_CUDA = $(patsubst src/%.cu, out/cuda/%.so, $(wildcard src/cuda/*.cu))

CC_CUDA = /usr/bin/nvcc
#If the current system has the cuda compiler, use CUDA to accelerate terrain gen
ifneq ("$(wildcard $(CC_CUDA))","")

CFLAGS_CUDA = -arch=sm_35 -Xcompiler -fPIC -DREPGAME_LINUX
CFLAGS_CUDA_COMPILE = -x cu -dc -c
CFLAGS_CUDA_LINK_DEVICE = --lib
CFLAGS_CUDA_LINK_HOST = -dlink
LIBS_LINUX += -lcudart
CFLAGS_LINUX += -DLOAD_WITH_CUDA

out/cuda/%.so: src/%.cu $(HEADERS) Makefile
	$(CC_CUDA) $(CFLAGS_CUDA_COMPILE) $(INCLUDES_LINUX) $(CFLAGS_CUDA) $< -o $@

$(LIB_TARGET_CUDA): $(LIB_DEVICE_CUDA) Makefile
	$(CC_CUDA) $(CFLAGS_CUDA) $(CFLAGS_CUDA_LINK_HOST) $(LIB_DEVICE_CUDA) -o $@

$(LIB_DEVICE_CUDA): $(OBJECTS_CUDA) Makefile
	$(CC_CUDA) $(CFLAGS_CUDA) $(CFLAGS_CUDA_LINK_DEVICE) $(OBJECTS_CUDA) -o $@

cuda: $(LIB_TARGET_CUDA) $(LIB_DEVICE_CUDA)

.PHONY: cuda

endif