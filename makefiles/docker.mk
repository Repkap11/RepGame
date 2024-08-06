#This create the docker image "repgame"
docker-image: $(call GUARD,DOCKER_ONLY_REPGAME_PACKAGES REPGAME_PACKAGES DOCKER_UBUNTU_VERSION)
	$(call CHECK,DOCKER_ONLY_REPGAME_PACKAGES REPGAME_PACKAGES DOCKER_UBUNTU_VERSION)
	docker images -aq -f 'dangling=true' | xargs -r docker rmi || true
	docker volume ls -q -f 'dangling=true' | xargs -r docker volume rm || true
	docker build -t repgame \
		--build-arg repgame_packages="$(DOCKER_ONLY_REPGAME_PACKAGES) $(REPGAME_PACKAGES)" \
		--build-arg user_name=$(shell whoami) \
		--build-arg user_id=$(shell id -u) \
		--build-arg ubuntu_version="$(DOCKER_UBUNTU_VERSION)" \
		.

#This saves the docker image for repgame as a file so it can be "docker load -i" on another machine.
docker-save: docker-image
	docker save repgame | gzip -c > repgame_docker.tar.gz

#This builds the code using the docker image "repgame".
docker-compile: | docker-image
	docker run \
		--user $(shell id -u):$(shell id -g) \
		--rm -it --init \
		-v $(shell pwd):/home/$(shell whoami)/RepGame \
		--env="DISPLAY" \
		--env="QT_X11_NO_MITSHM=1" \
		--gpus all \
		--volume="/tmp/.X11-unix:/tmp/.X11-unix:rw" \
		--privileged \
	repgame make docker-internal

docker-run: | docker-image
	docker run \
		--user $(shell id -u):$(shell id -g) \
		--rm -it --init \
		-v $(shell pwd):/home/$(shell whoami)/RepGame \
		--env="DISPLAY" \
		--env="QT_X11_NO_MITSHM=1" \
		--gpus all \
		--volume="/tmp/.X11-unix:/tmp/.X11-unix:rw" \
		--privileged \
	repgame make appimage-run

#This is what gets called inside the container when you build via "docker-compile".
#Each makefile adds dependencies to this target to include themselves in the build
docker-internal:

.PHONY: docker-save docker-compile docker-image docker-install docker-internal-build
