CC=g++
CCFLAGS=-Wall -Werror -std=c++17 -O2 -DBMS_PARSER_VERBOSE=0 
SRC_FILES = $(wildcard src/*.cpp)
OBJ_PATH=obj
BUILD_PATH=build
CARGO ?= cargo
PKG_CONFIG ?= pkg-config
SIMDUTF_CFLAGS ?= $(shell $(PKG_CONFIG) --cflags simdutf 2>/dev/null)
SIMDUTF_LIBS ?= $(shell $(PKG_CONFIG) --libs simdutf 2>/dev/null)
ifeq ($(strip $(SIMDUTF_LIBS)),)
SIMDUTF_LIBS = -lsimdutf
endif
ENCODING_C_DIR = third_party/encoding_c
RUST_TARGET_DIR ?= $(BUILD_PATH)/encoding_c
ENCODING_C_LIB ?= $(RUST_TARGET_DIR)/release/libbms_encoding_c.a
ifeq ($(OS),Windows_NT)
RUST_NATIVE_LIBS ?= -lws2_32 -lbcrypt -lntdll -luserenv -ladvapi32
else ifeq ($(shell uname -s),Linux)
RUST_NATIVE_LIBS ?= -ldl -lpthread -lm -lrt -lutil
endif
TEXT_LIBS = $(SIMDUTF_LIBS) $(ENCODING_C_LIB) $(RUST_NATIVE_LIBS)
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
all: $(OBJ_FILES) $(ENCODING_C_LIB)
-include $(DEPS) # Keep all as the default target before including dependencies

$(DEP_DIR):
	@$(MKDIRP) $(DEP_DIR)
$(OBJ_PATH):
	@$(MKDIRP) $(OBJ_PATH)
$(BUILD_PATH):
	@$(MKDIRP) $(BUILD_PATH)

$(ENCODING_C_LIB): $(ENCODING_C_DIR)/Cargo.toml $(ENCODING_C_DIR)/Cargo.lock $(ENCODING_C_DIR)/src/lib.rs
	$(CARGO) build --release --locked --manifest-path $(ENCODING_C_DIR)/Cargo.toml --target-dir $(RUST_TARGET_DIR)

$(OBJ_PATH)/%.o: src/%.cpp | $(DEP_DIR) $(OBJ_PATH)
	$(CC) $(CPPFLAGS) $(SIMDUTF_CFLAGS) $(CCFLAGS) $(DEPFLAGS) -c -o $@ $<
example/sqlite3.o: example/sqlite3.c
	gcc -c -o example/sqlite3.o example/sqlite3.c
example: all example/sqlite3.o $(BUILD_PATH)
	$(CC) $(CPPFLAGS) $(SIMDUTF_CFLAGS) $(CCFLAGS) $(LDFLAGS) -o $(BUILD_PATH)/main example/main.cpp example/sqlite3.o $(OBJ_FILES) $(TEXT_LIBS) $(LDLIBS)
amalgamate: $(BUILD_PATH)
	python3 scripts/amalgamate.py $(BUILD_PATH)/bms_parser.hpp $(BUILD_PATH)/bms_parser.cpp $(SRC_FILES)
test_amalgamation: amalgamate $(BUILD_PATH) $(ENCODING_C_LIB)
	$(CP) $(BUILD_PATH)$(SLASH)bms_parser.hpp test
	$(CC) $(CPPFLAGS) $(SIMDUTF_CFLAGS) $(CCFLAGS) $(LDFLAGS) -DWITH_AMALGAMATION=1 -o test/test_amalgamation test/main.cpp build/bms_parser.cpp $(TEXT_LIBS) $(LDLIBS)
	cd test && .$(SLASH)test_amalgamation$(EXE_EXT)
test: all $(BUILD_PATH)
	$(CC) $(CPPFLAGS) $(SIMDUTF_CFLAGS) $(CCFLAGS) $(LDFLAGS) -o test/test test/main.cpp $(OBJ_FILES) $(TEXT_LIBS) $(LDLIBS)
	cd test && .$(SLASH)test$(EXE_EXT)
test_ms932: all $(BUILD_PATH)
	$(CC) $(CPPFLAGS) $(SIMDUTF_CFLAGS) $(CCFLAGS) $(LDFLAGS) -o $(BUILD_PATH)/ms932_decode test/encoding/decode.cpp $(OBJ_PATH)/ShiftJISConverter.o $(TEXT_LIBS) $(LDLIBS)
	python3 test/encoding/check_ms932.py $(BUILD_PATH)/ms932_decode$(EXE_EXT)
clean:
	$(RRM) $(OBJ_PATH) $(BUILD_PATH) $(DEP_DIR) test$(SLASH)test$(EXE_EXT) test$(SLASH)test_amalgamation$(EXE_EXT) $(IGNORE_ERRORS)
