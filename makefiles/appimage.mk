#Used to install platform build tools on linux host
REP_MAKEFILES += makefiles/appimage.mk

appimage-run: appimage
	./$(TARGET)-1-x86_64.AppImage

all: appimage
docker-internal: appimage
appimage: $(TARGET)-1-x86_64.AppImage

clean: clean-appimage

clean-appimage:
	rm -rf out/appimage
	rm -f $(TARGET)-1-x86_64.AppImage

install: appimage_build
appimage_build:
	mkdir -p appimage_build/
	wget https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-x86_64.AppImage -O appimage_build/linuxdeploy-x86_64.AppImage
	chmod +x appimage_build/linuxdeploy-x86_64.AppImage

out/appimage: out/linux/$(TARGET) src/linux/$(TARGET).desktop bitmaps/icon_512.png appimage_build $(REP_MAKEFILES) | out
	mkdir -p out/appimage
	appimage_build/linuxdeploy-x86_64.AppImage --appdir out/appimage \
		--executable out/linux/$(TARGET) \
		--desktop-file src/linux/$(TARGET).desktop \
		--icon-file bitmaps/icon_512.png

$(TARGET)-1-x86_64.AppImage: $(REP_MAKEFILES) out/appimage
	VERSION=1 appimage_build/linuxdeploy-x86_64.AppImage \
		--appdir out/appimage --output appimage

.PHONY: appimage clean-appimage appimage-run