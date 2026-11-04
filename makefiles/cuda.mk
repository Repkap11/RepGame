#Linux Cuda
REPGAME_PACKAGES += nvidia-cuda-toolkit

# CUDA objects are built by cmake/RepGameGpu.cmake (custom commands around
# nvcc, same flags as before). This target configures a GPU-only build dir
# (no client/server); REPGAME_GPU_OUT keeps the old layout
# $(LINUX_OUT)/linux/cuda/*.o so `make gpu-prebuilt` (docker.mk) still fills
# prebuilt/gpu for the flatpak. The client links the objects via
# repgame_gpu_link() in CMakeLists.txt; when nvcc is absent and
# GPU_PREBUILT_DIR is set it links the prebuilt objects instead.
# Shared GPU-only configure — a file target so parallel `make hip cuda`
# configures once instead of racing on the same build dir. CUDA/HIP
# auto-detect by toolchain presence, so no per-target -D flags are needed.
$(LINUX_OUT)/cmake-gpu/CMakeCache.txt:
	cmake -S . -B $(LINUX_OUT)/cmake-gpu -DREPGAME_BUILD_CLIENT=OFF -DREPGAME_BUILD_SERVER=OFF \
		-DREPGAME_GPU_OUT=$(abspath $(LINUX_OUT)) $(CMAKE_GPU_LAUNCHER)

cuda: $(LINUX_OUT)/cmake-gpu/CMakeCache.txt
	MAKEFLAGS="$(CMAKE_MAKEFLAGS)" cmake --build $(LINUX_OUT)/cmake-gpu --target repgame_cuda

clean-linux: clean-cuda

clean-cuda:
	rm -rf $(LINUX_OUT)/cmake-gpu $(LINUX_OUT)/linux/cuda

.PHONY: cuda clean-cuda
