TARGET = RepGame
LIBS = -lm -l GL -l GLU -l GLEW -l glut -pthread
CC = g++
LD = ld
#SHELL = sh -xv
#CFLAGS = -Wall -std=c++98 -Wno-unused-variable -O3 -march=native -flto
CFLAGS = -g -Wall -std=c++98 -Wno-unused-variable -march=native -flto
CPUS ?= $(shell nproc || echo 1)
MAKEFLAGS += --jobs=$(CPUS)
LIB_TARGET = out/lib$(TARGET).o

run: compile
	./$(TARGET)

compile: out $(TARGET)

DIRS = $(patsubst %/, %, $(sort $(dir $(patsubst src/%, out/%,\
	$(wildcard src/*)\
	$(wildcard src/**/*)\
	))))
OBJECTS = $(patsubst src/%.cpp, out/%.o,\
	$(wildcard src/*.cpp)\
	$(wildcard src/abstract/*.cpp)\
	)
OBJECTS_LINUX = $(patsubst src/%.cpp, out/%.o,\
	$(wildcard src/linux/*.cpp)\
	)
HEADERS = $(wildcard include/*.h) $(wildcard include/**/*.h)

$(DIRS):
	mkdir -p $@

out/%.o: src/%.cpp $(HEADERS) Makefile
	$(CC) -I include/ $(CFLAGS) -c $< -o $@

.PRECIOUS: $(TARGET) $(OBJECTS) $(OBJECTS_LINUX)

$(TARGET): $(DIRS) $(LIB_TARGET) $(OBJECTS_LINUX) out Makefile 
	$(CC) $(CFLAGS) $(LIB_TARGET) $(OBJECTS_LINUX) -Wall $(LIBS) -o $@


$(LIB_TARGET): $(DIRS) $(OBJECTS) out Makefile
	$(LD) -r $(OBJECTS) -o $@

clean:
	rm -rf out
	rm -f $(TARGET)

map:
	rm -rf World1

android: $(LIB_TARGET) Makefile
	./android/gradlew -p android installDebug
	adb shell monkey -p com.repkap11.repgame -c android.intent.category.LAUNCHER 1

install:
	apt install freeglut3-dev libglew-dev libglm-dev libglm-doc

#$(info $$VAR is [${VAR}])
.PHONY: all clean install compile run world android
