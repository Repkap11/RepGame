install: imgui_build

imgui_build: | packages
	rm -rf imgui.zip
	mkdir -p imgui_build
	wget -q https://github.com/ocornut/imgui/archive/refs/tags/v1.91.9b.zip -O imgui.zip
	bsdtar --strip-components=1 -xvzf imgui.zip -C imgui_build
	rm -rf imgui.zip