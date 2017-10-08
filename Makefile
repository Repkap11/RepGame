TARGET = RepGame
LIBS = -l glut
CC = gcc
CFLAGS = -g -Wall


all: out $(TARGET)

run: all
	./$(TARGET)
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

.PHONY: all clean install