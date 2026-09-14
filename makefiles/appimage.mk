#Used to install platform build tools on linux host

# Host-built AppImage (not portable — uses your host's glibc and libs)
appimage-host: $(APPIMAGE_OUT)/$(TARGET)-1-x86_64.AppImage

appimage-host-run: appimage-host
	$(APPIMAGE_OUT)/$(TARGET)-1-x86_64.AppImage

# Portable AppImage built inside Docker (Ubuntu 24.04, glibc 2.38 floor)
# Uses separate output dirs (out/linux-portable, out/appimage-portable) so
# host and Docker artifacts don't clobber each other.
appimage-portable: | docker-image
	docker run \
		--user $(shell id -u):$(shell id -g) \
		--rm --init \
		-v $(shell pwd):/home/$(shell whoami)/RepGame \
		--privileged \
	repgame make appimage-host LINUX_OUT=out/linux-portable APPIMAGE_OUT=out/appimage-portable

appimage-portable-run: appimage-portable
	out/appimage-portable/$(TARGET)-1-x86_64.AppImage

appimage-portable-deploy: appimage-portable
	rsync out/appimage-portable/$(TARGET)-1-x86_64.AppImage paul@repkap11.com:/home/paul/website/${TARGET_LOWER}

all: appimage-host
deploy: appimage-portable-deploy

clean: clean-appimage

clean-appimage:
	rm -rf out/appimage out/appimage-portable

install: appimage_build
appimage_build:
	mkdir -p appimage_build/
	wget https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-x86_64.AppImage -O appimage_build/linuxdeploy-x86_64.AppImage
	chmod +x appimage_build/linuxdeploy-x86_64.AppImage

$(APPIMAGE_OUT)/image/.stamp: $(LINUX_OUT)/release/$(TARGET)_uncompressed src/linux/$(TARGET).desktop src/linux/$(TARGET)AppRun bitmaps/icon_512.png appimage_build | out
	rm -rf $(APPIMAGE_OUT)/image
	mkdir -p $(APPIMAGE_OUT)/image
# Exclude Wayland/libdecor libs from bundling so the AppImage uses the host's
# Wayland stack at runtime. Bundled Wayland libs from the Docker build (Ubuntu
# 24.04) can be incompatible with the host's Wayland compositor (e.g. Ubuntu 26.04).
	appimage_build/linuxdeploy-x86_64.AppImage \
		--verbosity=2 \
		--appdir $(APPIMAGE_OUT)/image \
		--executable $(LINUX_OUT)/release/$(TARGET)_uncompressed \
		--desktop-file src/linux/$(TARGET).desktop \
		--icon-file bitmaps/icon_512.png \
		--exclude-library='libwayland-client.so*' \
		--exclude-library='libwayland-cursor.so*' \
		--exclude-library='libwayland-egl.so*' \
		--exclude-library='libdecor-0.so*'
# Replace linuxdeploy's default AppRun (which ignores the desktop file's
# Exec= args) with a custom one that defaults to World1 + repkap11.com when
# the AppImage is launched with no arguments. Explicit CLI args still win.
	rm -f $(APPIMAGE_OUT)/image/AppRun
	cp src/linux/$(TARGET)AppRun $(APPIMAGE_OUT)/image/AppRun
	chmod +x $(APPIMAGE_OUT)/image/AppRun
	touch $@

$(APPIMAGE_OUT)/$(TARGET)-1-x86_64.AppImage: appimage_build $(APPIMAGE_OUT)/image/.stamp
	VERSION=1 appimage_build/linuxdeploy-x86_64.AppImage \
		--verbosity=2 \
		--desktop-file src/linux/$(TARGET).desktop \
		--appdir $(APPIMAGE_OUT)/image --output appimage \
		--exclude-library='libwayland-client.so*' \
		--exclude-library='libwayland-cursor.so*' \
		--exclude-library='libwayland-egl.so*' \
		--exclude-library='libdecor-0.so*'
	mv $(TARGET)-1-x86_64.AppImage $(APPIMAGE_OUT)/

.PHONY: appimage-host appimage-host-run appimage-portable appimage-portable-run appimage-portable-deploy clean-appimage
