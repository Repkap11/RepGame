install: sqlite_build

# SQLite amalgamation for the world.db storage backend (MapStorage).
# Same fetched-not-committed convention as imgui_build/entt_build.
sqlite_build:
	rm -rf sqlite.zip
	mkdir -p sqlite_build
	wget -q https://sqlite.org/2025/sqlite-amalgamation-3500400.zip -O sqlite.zip
	bsdtar --strip-components=1 -xvzf sqlite.zip -C sqlite_build
	rm -rf sqlite.zip
