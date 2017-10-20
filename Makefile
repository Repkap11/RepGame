TARGET = RepGame
LIBS = -lm -l GL -l GLU -l glut -pthread
CC = gcc
CFLAGS = -Wall -Werror -std=gnu99 -Wno-unused-variable -Ofast -march=native -flto
#CFLAGS = -g -Wall -Werror -std=gnu99 -Wno-unused-variable
export CPATH = include/

run: compile
	./$(TARGET)

compile: out $(TARGET)

out:
	mkdir out
OBJECTS = $(patsubst src/%.c, out/%.o, $(wildcard src/*.c))
HEADERS = $(wildcard include/*.h)

out/%.o: src/%.c $(HEADERS) Makefile
	$(CC) $(CFLAGS) -c $< -o $@

.PRECIOUS: $(TARGET) $(OBJECTS)

$(TARGET): $(OBJECTS) Makefile
	$(CC) $(OBJECTS) -Wall $(LIBS) -o $@

clean:
	rm -rf out
	rm -f $(TARGET)

install:
	apt install freeglut3-dev

.PHONY: all clean install compile run