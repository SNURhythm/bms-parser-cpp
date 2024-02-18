CC=g++
CCFLAGS=-std=c++17 -g
SRC_FILES = $(wildcard src/*.cpp)
OBJ_PATH=obj
BUILD_PATH=build
OBJ_FILES = $(patsubst src/%.cpp,$(OBJ_PATH)/%.o,$(SRC_FILES))

all: $(OBJ_FILES)
$(OBJ_PATH):
	@mkdir -p $(OBJ_PATH)
$(BUILD_PATH):
	@mkdir -p $(BUILD_PATH)

$(OBJ_PATH)/%.o: src/%.cpp $(OBJ_PATH)
	$(CC) $(CCFLAGS) -c -o $@ $<
example/sqlite3.o: example/sqlite3.c
	gcc -c -o example/sqlite3.o example/sqlite3.c
example: all example/sqlite3.o $(BUILD_PATH)
	$(CC) $(CCFLAGS) -o $(BUILD_PATH)/main example/main.cpp example/sqlite3.o $(OBJ_FILES)
clean:
	@rm -rf $(OBJ_PATH) $(BUILD_PATH)
