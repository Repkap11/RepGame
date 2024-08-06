REPGAME_PACKAGES += flatpak flatpak-builder

flatpak_build:| packages
	flatpak install --user -y flathub org.freedesktop.Platform//23.08 org.freedesktop.Sdk//23.08

flatpak:
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