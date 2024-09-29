#Used to install platform build tools on linux host

REPGAME_PACKAGES := libglm-dev libglm-doc rsync wget ccache clang libarchive-tools

install: packages

packages:
	sudo apt-get install -y $(REPGAME_PACKAGES)
	sudo apt-get install -y bsdtar || sudo apt-get install -y libarchive-tools

.PHONY: install packages