REP_MAKEFILES += docker.mk

docker-init:
	docker images -aq -f 'dangling=true' | xargs docker rmi || echo
	docker volume ls -q -f 'dangling=true' | xargs docker volume rm || echo
	docker build -t repgame --build-arg repgame_packages="${REPGAME_PACKAGES}" --build-arg user_name=$(shell whoami) .

docker-compile:
	docker run --user $(shell id -u):$(shell id -g) --rm -it -v $(shell pwd):/home/$(shell whoami)/RepGame repgame