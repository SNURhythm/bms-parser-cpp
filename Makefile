CC=g++
CCFLAGS=-std=c++17 -DBMS_PARSER_VERBOSE=1
SRC_FILES = $(wildcard src/*.cpp)
OBJ_PATH=obj
BUILD_PATH=build
OBJ_FILES = $(patsubst src/%.cpp,$(OBJ_PATH)/%.o,$(SRC_FILES))
DEP_DIR=.deps
DEPS = $(SRC_FILES:src/%.cpp=$(DEP_DIR)/%.d)
DEPFLAGS=-MMD -MP -MT $@ -MF $(DEP_DIR)/$*.d

-include $(DEP_FILES) # Include the dependency files

all: $(OBJ_FILES)
$(DEP_DIR):
	@mkdir -p $(DEP_DIR)
$(OBJ_PATH):
	@mkdir -p $(OBJ_PATH)
$(BUILD_PATH):
	@mkdir -p $(BUILD_PATH)

$(OBJ_PATH)/%.o: src/%.cpp | $(DEP_DIR) $(OBJ_PATH)
	$(CC) $(CCFLAGS) $(DEPFLAGS) -c -o $@ $<
example/sqlite3.o: example/sqlite3.c
	gcc -c -o example/sqlite3.o example/sqlite3.c
example: all example/sqlite3.o $(BUILD_PATH)
	$(CC) $(CCFLAGS) -o $(BUILD_PATH)/main example/main.cpp example/sqlite3.o $(OBJ_FILES)
amalgamate: $(BUILD_PATH)
	python3 scripts/amalgamate.py $(BUILD_PATH)/bms_parser.hpp $(SRC_FILES)
test_amalgamation: amalgamate $(BUILD_PATH)
	@cp $(BUILD_PATH)/bms_parser.hpp test/
	$(CC) $(CCFLAGS) -DWITH_AMALGAMATION=1 -o test/test_amalgamation test/main.cpp
	cd test && ./test_amalgamation
test: all $(BUILD_PATH)
	$(CC) $(CCFLAGS) -o test/test test/main.cpp $(OBJ_FILES)
	cd test && ./test
clean:
	@rm -rf $(OBJ_PATH) $(BUILD_PATH) $(DEP_DIR) test/test test/test_amalgamation

