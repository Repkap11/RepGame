#Linux Cuda
REPGAME_PACKAGES += nvidia-cuda-toolkit

OBJECTS_CUDA := $(patsubst src/linux/cuda/%.cu,out/linux/linux/cuda/%.o, $(wildcard src/linux/cuda/*.cu))
CLEAN_CUDA := $(OBJECTS_CUDA)

CC_CUDA := /usr/bin/nvcc
#If the current system has the cuda compiler allow cuda to be build
ifneq ("$(wildcard $(CC_CUDA))","")

ifeq ($(USE_CCACHE),1)
CC_CUDA := ccache $(CC_CUDA)
endif

LIB_DEVICE_CUDA := out/linux/linux/cuda/lib$(TARGET)_device.o
LIB_TARGET_CUDA := out/linux/linux/cuda/lib$(TARGET).o
CLEAN_CUDA += $(LIB_DEVICE_CUDA) $(LIB_TARGET_CUDA)

CFLAGS_CUDA :=  -arch=sm_50 \
				-gencode=arch=compute_50,code=sm_50 \
				-gencode=arch=compute_52,code=sm_52 \
				-gencode=arch=compute_60,code=sm_60 \
				-gencode=arch=compute_61,code=sm_61 \
				-gencode=arch=compute_70,code=sm_70 \
				-gencode=arch=compute_75,code=sm_75 \
				-gencode=arch=compute_75,code=compute_75 \
				-Xcompiler -fPIC -DREPGAME_LINUX
CFLAGS_CUDA_COMPILE := -x cu -dc -c
CFLAGS_CUDA_LINK_DEVICE := --lib
CFLAGS_CUDA_LINK_HOST := -dlink


SUPPORTS_CUDA := $(shell lspci | grep VGA | grep -i nvidia | wc -l )

# ifeq ($(SUPPORTS_CUDA),1) #If the current linux GPU has cuda, use CUDA to accelerate terrain gen
LIBS_LINUX += -lcudart_static
CFLAGS_LINUX += -DREPGAME_BUILD_WITH_CUDA
OBJECTS_COMMON_LINUX_RELEASE += $(LIB_TARGET_CUDA) $(LIB_DEVICE_CUDA)
OBJECTS_COMMON_LINUX_DEBUG += $(LIB_TARGET_CUDA) $(LIB_DEVICE_CUDA)
# endif


out/linux/linux/cuda/%.o: src/linux/cuda/%.cu $(HEADERS) | out/linux
	$(CC_CUDA) $(CFLAGS_CUDA_COMPILE) $(INCLUDES_COMMON) $(CFLAGS_CUDA) $< -o $@

$(LIB_TARGET_CUDA): $(LIB_DEVICE_CUDA) | out
	$(CC_CUDA) $(CFLAGS_CUDA) $(CFLAGS_CUDA_LINK_HOST) $(LIB_DEVICE_CUDA) -o $@

$(LIB_DEVICE_CUDA): $(OBJECTS_CUDA) | out
	$(CC_CUDA) $(CFLAGS_CUDA) $(CFLAGS_CUDA_LINK_DEVICE) $(OBJECTS_CUDA) -o $@

cuda: $(LIB_TARGET_CUDA) $(LIB_DEVICE_CUDA)

clean-linux: clean-cuda

clean-cuda:
	rm -f $(CLEAN_CUDA)

.PRECIOUS: %.o

.PHONY: cuda clean-cuda

endif # End if has cuda