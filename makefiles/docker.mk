REP_MAKEFILES += makefiles/docker.mk

#This create the docker image "repgame"
docker-image:
	docker images -aq -f 'dangling=true' | xargs -r docker rmi || true
	docker volume ls -q -f 'dangling=true' | xargs -r docker volume rm || true
	docker build -t repgame --build-arg repgame_packages="${REPGAME_PACKAGES}" --build-arg user_name=$(shell whoami) .

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
	repgame

#This is what gets called whenin the container when you build via "docker-compile".
docker-internal: windows linux-run

.PHONY: docker-save docker-compile docker-image docker-install docker-internal-pre-build docker-internal-build
