#Used to install platform build tools on linux host
REP_MAKEFILES += makefiles/appimage.mk

appimage-run: appimage
	./$(TARGET)-1-x86_64.AppImage

appimage: $(TARGET)-1-x86_64.AppImage

clean: clean-appimage

out/appimage: | out
	mkdir -p out/appimage

clean-appimage:
	rm -rf out/appimage
	rm -f $(TARGET)-1-x86_64.AppImage

$(TARGET)-1-x86_64.AppImage: out/linux/$(TARGET) src/linux/$(TARGET).desktop bitmaps/icon_512.png linux_build | out/appimage
	VERSION=1 linux_build/linuxdeploy-x86_64.AppImage\
		--appdir out/appimage \
		--executable out/linux/$(TARGET) \
		--desktop-file src/linux/$(TARGET).desktop \
		--icon-file bitmaps/icon_512.png \
		--appdir out/appimage --output appimage

.PHONY: appimage clean-appimage appimage-run