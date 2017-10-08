TARGET = RepGame
LIBS = -l GL -l GLU -l glut
CC = gcc
CFLAGS = -g -Wall

run: compile
	./$(TARGET)

compile: out $(TARGET)

out:
	mkdir out
OBJECTS = $(patsubst %.c, out/%.o, $(wildcard *.c))
HEADERS = $(wildcard *.h)

out/%.o: %.c $(HEADERS)
	$(CC) $(CFLAGS) -c $< -o $@

.PRECIOUS: $(TARGET) $(OBJECTS)

$(TARGET): $(OBJECTS)
	$(CC) $(OBJECTS) -Wall $(LIBS) -o $@

clean:
	rm -rf out
	rm -f $(TARGET)

install:
	apt install freeglut3-dev

.PHONY: all clean install compile run