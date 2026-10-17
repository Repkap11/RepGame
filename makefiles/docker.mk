#Version of Ubuntu to target when build with docker
# 24.04 (noble) is the oldest LTS with ROCm repo support for HIP 7.x.
# SDL3 is built from source in the Dockerfile since 24.04 doesn't ship libsdl3-dev.
DOCKER_UBUNTU_VERSION ?= 24.04

# Packages wanted only inside the build container (extension point; currently
# none). Referenced in the docker-image build-arg below.
DOCKER_ONLY_REPGAME_PACKAGES ?=

#This create the docker image "repgame"
#nvidia-cuda-toolkit and hipcc are installed so the image has both GPU
#toolchains; libamdhip64-dev and libsdl3-dev are excluded because the Dockerfile
#gets them from the ROCm repo and from source respectively.
docker-image:
	docker images -aq -f 'dangling=true' | xargs -r docker rmi || true
	docker volume ls -q -f 'dangling=true' | xargs -r docker volume rm || true
	docker build -t repgame \
		--build-arg repgame_packages="$(filter-out libsdl3-dev libamdhip64-dev,$(DOCKER_ONLY_REPGAME_PACKAGES) $(REPGAME_PACKAGES))" \
		--build-arg user_name=$(shell whoami) \
		--build-arg user_id=$(shell id -u) \
		--build-arg ubuntu_version="$(DOCKER_UBUNTU_VERSION)" \
		.

#This saves the docker image for repgame as a file so it can be "docker load -i" on another machine.
docker-save: docker-image
	docker save repgame | gzip -c > repgame_docker.tar.gz

docker-shell: | docker-image
	docker run \
		--user $(shell id -u):$(shell id -g) \
		--rm -it --init \
		-v $(shell pwd):/home/$(shell whoami)/RepGame \
	repgame bash

#This runs the code inside a container, which is rather silly.
docker-run: | docker-image
	docker run \
		--user $(shell id -u):$(shell id -g) \
		--rm -it --init \
		-v $(shell pwd):/home/$(shell whoami)/RepGame \
		--env="DISPLAY" \
		--env="QT_X11_NOMITSHM=1" \
		--gpus all \
		--volume="/tmp/.X11-unix:/tmp/.X11-unix:rw" \
		--privileged \
	repgame make appimage-host-run

GPU_PREBUILT_OUT ?= prebuilt/gpu

#Compile the HIP/CUDA objects inside the container (which has both toolchains)
#and harvest the runtime libraries they need into $(GPU_PREBUILT_OUT)/lib. The
#flatpak build links these objects — its sandbox has no hipcc/nvcc — via
#GPU_PREBUILT_DIR in hip.mk/cuda.mk.
gpu-prebuilt: | docker-image
	docker run \
		--user $(shell id -u):$(shell id -g) \
		--rm --init \
		-v $(shell pwd):/home/$(shell whoami)/RepGame \
	repgame make gpu-prebuilt-in-docker

#Runs inside the container. Objects land under $(GPU_PREBUILT_OUT)/linux and
#libs under $(GPU_PREBUILT_OUT)/lib; the flatpak source copy picks both up.
#The lib list mirrors what the AppImage bundles for libamdhip64's dependency
#tree — the whole set is needed for -rpath-link at link time, but the flatpak
#manifest installs only the ones the runtime lacks (see
#flatpak/com.repkap11.repgame.yml).
gpu-prebuilt-in-docker:
	$(MAKE) hip cuda LINUX_OUT=$(GPU_PREBUILT_OUT)
	mkdir -p $(GPU_PREBUILT_OUT)/lib
	for pat in \
		'/opt/rocm/lib/libamdhip64.so*' \
		'/opt/rocm/lib/libhsa-runtime64.so*' \
		'/opt/rocm/lib/librocprofiler-register.so*' \
		'/opt/rocm/lib/libamd_comgr.so*' \
		'/usr/lib/x86_64-linux-gnu/libdrm_amdgpu.so*' \
		'/usr/lib/x86_64-linux-gnu/libelf.so*' \
		'/usr/lib/x86_64-linux-gnu/libnuma.so*' \
		'/usr/lib/x86_64-linux-gnu/libzstd.so*' \
		'/usr/lib/x86_64-linux-gnu/libbsd.so*' \
		'/usr/lib/x86_64-linux-gnu/libmd.so*' \
		'/usr/lib/x86_64-linux-gnu/libffi.so*' \
	; do \
		cp -a $$pat $(GPU_PREBUILT_OUT)/lib/ 2>/dev/null || echo "gpu-prebuilt: no match for $$pat"; \
	done
	cudart=$$(find /usr -name 'libcudart_static.a' | head -1); \
		[ -n "$$cudart" ] && cp -a "$$cudart" $(GPU_PREBUILT_OUT)/lib/ || echo "gpu-prebuilt: libcudart_static.a not found"

.PHONY: docker-save docker-image docker-install gpu-prebuilt gpu-prebuilt-in-docker
