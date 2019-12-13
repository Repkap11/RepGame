#Used to install platform build tools on linux host
REP_MAKEFILES += makefiles/install.mk

REPGAME_PACKAGES := libglm-dev libglm-doc rsync libarchive-tools wget ccache

linux_build:
	mkdir -p linux_build/
	wget https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-x86_64.AppImage -O linux_build/linuxdeploy-x86_64.AppImage
	chmod +x linux_build/linuxdeploy-x86_64.AppImage


windows_build:
	rm -rf freeglut.zip
	rm -rf glew.zip
	rm -rf windows_build
	mkdir -p windows_build/glew
	wget -q https://www.transmissionzero.co.uk/files/software/development/GLUT/freeglut-MinGW.zip -O freeglut.zip
	wget -q http://www.grhmedia.com/glew/glew-2.1.0-mingw-w64.zip -O glew.zip
	bsdtar --strip-components=1 -xvf glew.zip -C windows_build/glew
	bsdtar  -xvf freeglut.zip -C windows_build
	rm -rf freeglut.zip
	rm -rf glew.zip

install: windows_build linux_build
	sudo apt-get install -y $(REPGAME_PACKAGES)

.PHONY: install