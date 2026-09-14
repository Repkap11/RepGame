#Version of Ubuntu to target when build with docker
# 24.04 (noble) is the oldest LTS with ROCm repo support for HIP 7.x.
# SDL3 is built from source in the Dockerfile since 24.04 doesn't ship libsdl3-dev.
DOCKER_UBUNTU_VERSION ?= 24.04

#This create the docker image "repgame"
docker-image:
	docker images -aq -f 'dangling=true' | xargs -r docker rmi || true
	docker volume ls -q -f 'dangling=true' | xargs -r docker volume rm || true
	docker build -t repgame \
		--build-arg repgame_packages="$(filter-out libsdl3-dev nvidia-cuda-toolkit libamdhip64-dev,$(DOCKER_ONLY_REPGAME_PACKAGES) $(REPGAME_PACKAGES))" \
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

.PHONY: docker-save docker-image docker-install
