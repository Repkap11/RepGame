#Used to install platform build tools on linux host
REP_MAKEFILES += makefiles/install.mk

REPGAME_PACKAGES := libglm-dev libglm-doc rsync wget ccache

ifneq ($(UBUNTU_VERSION),18.04)
	HOST_ONLY_REPGAME_PACKAGES += bsdtar
else
	HOST_ONLY_REPGAME_PACKAGES += libarchive-tools
endif

ifeq ($(DOCKER_UBUNTU_VERSION),14.04)
else ifeq ($(DOCKER_UBUNTU_VERSION),16.04)
	DOCKER_ONLY_REPGAME_PACKAGES += bsdtar
else
	DOCKER_ONLY_REPGAME_PACKAGES += libarchive-tools
endif

install: packages

packages:
	sudo apt-get install -y $(REPGAME_PACKAGES) $(HOST_ONLY_REPGAME_PACKAGES)

.PHONY: install packages