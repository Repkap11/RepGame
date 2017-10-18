TARGET = RepGame
LIBS = -lm -l GL -l GLU -l glut -pthread
CC = gcc
CFLAGS = -Wall -Werror -std=gnu99 -Wno-unused-variable -Ofast -march=native -flto
DEBUG_FLAGS = -g

run: compile
	./$(TARGET)

compile: out $(TARGET)

out:
	mkdir out
OBJECTS = $(patsubst %.c, out/%.o, $(wildcard *.c))
HEADERS = $(wildcard *.h)

out/%.o: %.c $(HEADERS) Makefile
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