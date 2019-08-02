REP_MAKEFILES += makefiles/docker.mk

#This create the docker image "repgame"
docker-image:
	docker images -aq -f 'dangling=true' | xargs docker rmi || echo
	docker volume ls -q -f 'dangling=true' | xargs docker volume rm || echo
	docker build -t repgame --build-arg repgame_packages="${REPGAME_PACKAGES}" --build-arg user_name=$(shell whoami) .

#This saves the docker image for repgame as a file so it can be "docker load -i" on another machine.
docker-save: docker-image
	docker save repgame | gzip -c > repgame_docker.tar.gz

#This builds the code using the docker image "repgame". A complicated way to do recurvice make I guess.
docker-compile: #Depends on docker-image building docker image "repgame", but you don't want to run it every time you need to compile...
	docker run --user $(shell id -u):$(shell id -g) --rm -it -v $(shell pwd):/home/$(shell whoami)/RepGame repgame

#When you compile with docker-compile, this target is run within the container to make sure you everything is ready for your build
#it should be fast after the first time. The @ > /dev/null just makes the build always quiet.
docker-internal-pre-build: windows_build
	@ > /dev/null

#This is what gets called whenin the container when you build via "docker-compile".
docker-internal-build: windows linux
	@ > /dev/null

.PHONY: docker-save docker-compile docker-image docker-install docker-internal-pre-build docker-internal-build
