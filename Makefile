CC=g++
CCFLAGS=-std=c++17
SRC_FILES = $(wildcard src/*.cpp)
OBJ_PATH=obj
OBJ_FILES = $(patsubst src/%.cpp,$(OBJ_PATH)/%.o,$(SRC_FILES))

all: $(OBJ_FILES)
	$(CC) $(CCFLAGS) -shared -o libbmsparser.so $(OBJ_FILES)
$(OBJ_PATH):
	@mkdir -p $(OBJ_PATH)
$(OBJ_PATH)/%.o: src/%.cpp $(OBJ_PATH)
	$(CC) $(CCFLAGS) -c -o $@ $<
clean:
	@rm -f $(OBJ_PATH)/*.o main
