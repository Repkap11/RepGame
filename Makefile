TARGET = RepGame
LIBS = -lm -l GL -l GLU -l GLEW -l glut -pthread
CC = clang
#CFLAGS = -Wall -Werror -std=gnu99 -Wno-unused-variable -O3 -march=native -flto
CFLAGS = -g -Wall -std=gnu99 -Wno-unused-variable -O0 -march=native -flto
CPUS ?= $(shell nproc || echo 1)
MAKEFLAGS += --jobs=$(CPUS)

run: compile
	./$(TARGET)

compile: out $(TARGET)

DIRS    = $(patsubst %/, %, $(sort $(dir $(patsubst src/%, out/%, $(wildcard src/*) $(wildcard src/**/*)))))
OBJECTS = $(patsubst src/%.c, out/%.o, $(wildcard src/*.c) $(wildcard src/**/*.c))
HEADERS = $(wildcard include/*.h) $(wildcard include/**/*.h)

$(DIRS):
	mkdir -p $@

out/%.o: src/%.c $(HEADERS) Makefile
	$(CC) -I include/ $(CFLAGS) -c $< -o $@

.PRECIOUS: $(TARGET) $(OBJECTS)

$(TARGET): $(DIRS) $(OBJECTS) Makefile
	$(CC) $(CFLAGS) $(OBJECTS) -Wall $(LIBS) -o $@

clean:
	rm -rf out
	rm -f $(TARGET)

install:
	apt install freeglut3-dev libglew-dev

#$(info $$VAR is [${VAR}])
.PHONY: all clean install compile run