#Used to install platform build tools on linux host
REP_MAKEFILES += makefiles/appimage.mk

appimage-run: appimage
	./$(TARGET)-1-x86_64.AppImage

appimage: $(TARGET)-1-x86_64.AppImage

clean: clean-appimage

clean-appimage:
	rm -rf out/appimage
	rm -f $(TARGET)-1-x86_64.AppImage

out/appimage: | out
	mkdir -p appimage

out/appimage/AppRun: src/linux/$(TARGET).desktop | out/appimage
	linux_build/linuxdeploy-x86_64.AppImage\
		--appdir out/appimage \
		--executable out/linux/$(TARGET)

out/appimage/$(TARGET).desktop: out/linux/$(TARGET) out/appimage/AppRun src/linux/$(TARGET).desktop linux_build | out/appimage
	linux_build/linuxdeploy-x86_64.AppImage\
		--appdir out/appimage \
		--desktop-file src/linux/$(TARGET).desktop

out/appimage/usr/share/icons/hicolor/512x512/apps/icon_512.png: out/linux/$(TARGET) bitmaps/icon_512.png linux_build | out/appimage
	linux_build/linuxdeploy-x86_64.AppImage\
		--appdir out/appimage \
		--icon-file bitmaps/icon_512.png

$(TARGET)-1-x86_64.AppImage: out/appimage/AppRun out/appimage/$(TARGET).desktop out/appimage/usr/share/icons/hicolor/512x512/apps/icon_512.png linux_build | out/appimage
	VERSION=1 linux_build/linuxdeploy-x86_64.AppImage\
		--appdir out/appimage --output appimage

.PHONY: appimage clean-appimage appimage-run