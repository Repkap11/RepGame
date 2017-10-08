TARGET = RepGame
LIBS = -l GL -l GLU -l glut
CC = gcc
CFLAGS = -g -Wall -Werror

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