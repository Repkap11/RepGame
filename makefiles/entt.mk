install: entt_build

# entt_build: | packages
entt_build:
	rm -rf entt.tar.gz
	mkdir -p entt_build
	wget -q https://github.com/skypjack/entt/releases/download/v3.13.2/entt-v3.13.2.tar.gz -O entt.tar.gz
	bsdtar -xvzf entt.tar.gz -C entt_build
	rm -rf entt.tar.gz