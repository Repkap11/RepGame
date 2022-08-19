#Used to install platform build tools on linux host

appimage-run: appimage
	out/appimage/$(TARGET)-1-x86_64.AppImage

all: appimage
docker-internal: appimage
appimage: out/appimage/$(TARGET)-1-x86_64.AppImage

clean: clean-appimage

clean-appimage:
	rm -rf out/appimage

install: appimage_build
appimage_build:
	mkdir -p appimage_build/
	wget https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-x86_64.AppImage -O appimage_build/linuxdeploy-x86_64.AppImage
	chmod +x appimage_build/linuxdeploy-x86_64.AppImage

out/appimage/image: out/linux/release/$(TARGET) src/linux/$(TARGET).desktop bitmaps/icon_512.png appimage_build | out
	mkdir -p out/appimage/image
	appimage_build/linuxdeploy-x86_64.AppImage \
		--verbosity=2 \
		--appdir out/appimage/image \
		--executable out/linux/release/$(TARGET) \
		--desktop-file src/linux/$(TARGET).desktop \
		--icon-file bitmaps/icon_512.png

out/appimage/$(TARGET)-1-x86_64.AppImage: appimage_build out/appimage/image
	VERSION=1 appimage_build/linuxdeploy-x86_64.AppImage \
		--verbosity=2 \
		--desktop-file src/linux/$(TARGET).desktop \
		--appdir out/appimage/image --output appimage
	mv $(TARGET)-1-x86_64.AppImage out/appimage/

deploy: appimage-deploy

appimage-deploy: out/appimage/$(TARGET)-1-x86_64.AppImage
	rsync $< paul@repkap11.com:/home/paul/website/${TARGET_LOWER}

.PHONY: appimage clean-appimage appimage-run