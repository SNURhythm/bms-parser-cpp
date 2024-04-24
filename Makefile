CC=g++
CCFLAGS=-std=c++17 -DBMS_PARSER_VERBOSE=0
SRC_FILES = $(wildcard src/*.cpp)
OBJ_PATH=obj
BUILD_PATH=build
OBJ_FILES = $(patsubst src/%.cpp,$(OBJ_PATH)/%.o,$(SRC_FILES))
DEP_DIR=.deps
DEPS = $(SRC_FILES:src/%.cpp=$(DEP_DIR)/%.d)
DEPFLAGS=-MMD -MP -MT $@ -MF $(DEP_DIR)/$*.d
EXE_EXT=
ifeq ($(OS),Windows_NT)
		EXE_EXT=.exe
        CP = copy
        RM = del
        RRM = rmdir /s /q
        MKDIRP = mkdir
        IGNORE_ERRORS = 2>NUL || (exit 0)
        SLASH=\\
        # Attempt to detect a Unix-like shell environment (e.g., Git Bash, Cygwin) by checking for /bin/sh
    ifneq ($(shell if [ -x /bin/sh ]; then echo true; fi),)
        CP = cp
        RM = rm -f
        RRM = rm -rf
        MKDIRP = mkdir -p
		IGNORE_ERRORS = 2>/dev/null || true
		SLASH=/
    endif
else
        CP_RES = cp -r res $(BUILD_DIR)
        CP = cp
        RM = rm
        RRM = rm -rf
        MKDIRP = mkdir -p
        IGNORE_ERRORS = 2>/dev/null || true
        SLASH=/
endif
-include $(DEP_FILES) # Include the dependency files

all: $(OBJ_FILES)
$(DEP_DIR):
	@$(MKDIRP) $(DEP_DIR)
$(OBJ_PATH):
	@$(MKDIRP) $(OBJ_PATH)
$(BUILD_PATH):
	@$(MKDIRP) $(BUILD_PATH)

$(OBJ_PATH)/%.o: src/%.cpp | $(DEP_DIR) $(OBJ_PATH)
	$(CC) $(CCFLAGS) $(DEPFLAGS) -c -o $@ $<
example/sqlite3.o: example/sqlite3.c
	gcc -c -o example/sqlite3.o example/sqlite3.c
example: all example/sqlite3.o $(BUILD_PATH)
	$(CC) $(CCFLAGS) -o $(BUILD_PATH)/main example/main.cpp example/sqlite3.o $(OBJ_FILES)
amalgamate: $(BUILD_PATH)
	python3 scripts/amalgamate.py $(BUILD_PATH)/bms_parser.hpp $(SRC_FILES)
test_amalgamation: amalgamate $(BUILD_PATH)
	$(CP) $(BUILD_PATH)$(SLASH)bms_parser.hpp test
	$(CC) $(CCFLAGS) -DWITH_AMALGAMATION=1 -o test/test_amalgamation test/main.cpp
	cd test && .$(SLASH)test_amalgamation$(EXE_EXT)
test: all $(BUILD_PATH)
	$(CC) $(CCFLAGS) -o test/test test/main.cpp $(OBJ_FILES)
	cd test && .$(SLASH)test$(EXE_EXT)
clean:
	$(RRM) $(OBJ_PATH) $(BUILD_PATH) $(DEP_DIR) test$(SLASH)test$(EXE_EXT) test$(SLASH)test_amalgamation$(EXE_EXT) $(IGNORE_ERRORS)

