CC=g++
CCFLAGS=-std=c++17 -g -DBMS_PARSER_VERBOSE=1
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
amalgamate: $(BUILD_PATH)
	python3 scripts/amalgamate.py $(BUILD_PATH)/bms_parser.hpp $(SRC_FILES)
test_amalgamation: amalgamate example/sqlite3.o $(BUILD_PATH)
	@cp $(BUILD_PATH)/bms_parser.hpp example/
	$(CC) $(CCFLAGS) -DWITH_AMALGAMATION=1 -o $(BUILD_PATH)/test_amalgamation example/main.cpp example/sqlite3.o
	./$(BUILD_PATH)/test_amalgamation example/example.bme
clean:
	@rm -rf $(OBJ_PATH) $(BUILD_PATH)
