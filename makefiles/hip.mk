#Linux HIP (AMD GPU)
REPGAME_PACKAGES += hipcc libamdhip64-dev

#Install HIP build dependencies (available even before hipcc is installed)
hip-install:
	sudo apt-get install -y hipcc libamdhip64-dev

# HIP objects are built by cmake/RepGameGpu.cmake — see cuda.mk for the
# GPU-only configure and the prebuilt/gpu layout note.
hip: $(LINUX_OUT)/cmake-gpu/CMakeCache.txt
	MAKEFLAGS="$(CMAKE_MAKEFLAGS)" cmake --build $(LINUX_OUT)/cmake-gpu --target repgame_hip

clean-linux: clean-hip

clean-hip:
	rm -rf $(LINUX_OUT)/cmake-gpu $(LINUX_OUT)/linux/hip

.PHONY: hip hip-install clean-hip
