#Used to install platform build tools on linux host

REPGAME_PACKAGES := libglm-dev libglm-doc rsync wget ccache clang

ifeq ($(DOCKER_UBUNTU_VERSION),14.04)
	DOCKER_ONLY_REPGAME_PACKAGES += bsdtar
else ifeq ($(DOCKER_UBUNTU_VERSION),16.04)
	DOCKER_ONLY_REPGAME_PACKAGES += bsdtar
else
	DOCKER_ONLY_REPGAME_PACKAGES += libarchive-tools
endif

install: packages

packages:
	sudo apt-get install -y $(REPGAME_PACKAGES)
	sudo apt-get install -y bsdtar || sudo apt-get install -y libarchive-tools

.PHONY: install packages