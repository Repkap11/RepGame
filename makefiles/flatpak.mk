REPGAME_PACKAGES += flatpak flatpak-builder

flatpak_build:| packages
	flatpak remote-add --user --if-not-exists flathub https://flathub.org/repo/flathub.flatpakrepo
	flatpak install --user -y flathub org.freedesktop.Platform//26.08 org.freedesktop.Sdk//26.08

#gpu-prebuilt builds the HIP/CUDA objects + runtime libs in Docker; the flatpak
#sandbox has no GPU toolchain and links them via GPU_PREBUILT_DIR.
#sounds-prebuilt converts the OGGs on the host for the same reason — the
#sandbox has no ffmpeg and links them via SOUND_OGG_DIR.
flatpak: gpu-prebuilt sounds-prebuilt
	flatpak-builder --user --install --force-clean --repo=out/flatpak/repo out/flatpak/build flatpak/com.repkap11.repgame.yml


flatpak-run:
	flatpak run com.repkap11.repgame

all: flatpak

clean: clean-flatpak
install: flatpak_build



clean-flatpak:
	rm -rf out/flatpak

install: flatpak_build


# deploy: flatpak-deploy

# flatpak-deploy: out/flatpak/$(TARGET)-1-x86_64.flatpak
# 	echo TODO

.PHONY: flatpak clean-flatpak flatpak-run flatpak_build