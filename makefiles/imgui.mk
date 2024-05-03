install: imgui_build

imgui_build: | packages
	rm -rf imgui.zip
	mkdir -p imgui_build
	wget -q https://repkap11.com/files/repgame/SDL2-devel-2.0.10-mingw.tar.gz -O imgui.zip
	bsdtar --strip-components=1 -xvzf imgui.zip -C imgui_build
	rm -rf imgui.zip