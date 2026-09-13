#Linux HIP (AMD GPU)
REPGAME_PACKAGES += hipcc libamdhip64-dev

#Install HIP build dependencies (available even before hipcc is installed)
hip-install:
	sudo apt-get install -y hipcc libamdhip64-dev

OBJECTS_HIP := $(patsubst src/linux/hip/%.hip,out/linux/linux/hip/%.o, $(wildcard src/linux/hip/*.hip))
CLEAN_HIP := $(OBJECTS_HIP)

CC_HIP := /usr/bin/hipcc
#If the current system has the hip compiler allow hip to be build
ifneq ("$(wildcard $(CC_HIP))","")

ifeq ($(USE_CCACHE),1)
CC_HIP := ccache $(CC_HIP)
endif

# Generic code objects target an entire GPU family with one arch, so a single
# binary runs on every supported AMD GPU (including future ones within each
# family) without listing each chip individually. Requires Code Object V6
# (ROCm 6.0+); gfx12-generic / gfx1151 need a newer ROCm on the build machine.
CFLAGS_HIP :=  --offload-arch=gfx9-generic \
				--offload-arch=gfx10-1-generic \
				--offload-arch=gfx10-3-generic \
				--offload-arch=gfx11-generic \
				--offload-arch=gfx12-generic \
				-fPIC -DREPGAME_LINUX
CFLAGS_HIP_COMPILE := -x hip -c -fno-rtti


SUPPORTS_HIP := $(shell lspci | grep VGA | grep -i amd | wc -l )

# ifeq ($(SUPPORTS_HIP),1) #If the current linux GPU is AMD, use HIP to accelerate terrain gen
LIBS_LINUX += -lamdhip64
CFLAGS_LINUX += -DREPGAME_BUILD_WITH_HIP
OBJECTS_COMMON_LINUX_RELEASE += $(OBJECTS_HIP)
OBJECTS_COMMON_LINUX_DEBUG += $(OBJECTS_HIP)
# endif


out/linux/linux/hip/%.o: src/linux/hip/%.hip $(HEADERS) | out/linux
	$(CC_HIP) $(CFLAGS_HIP_COMPILE) $(INCLUDES_COMMON) $(CFLAGS_HIP) $< -o $@

hip: $(OBJECTS_HIP)

clean-linux: clean-hip

clean-hip:
	rm -f $(CLEAN_HIP)

.PRECIOUS: %.o

.PHONY: hip clean-hip

endif # End if has hip

.PHONY: hip-install
