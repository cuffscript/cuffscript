CXX = g++
CXXFLAGS = -std=c++17 -Wall -Wextra -Werror -O2

TARGET = cuffc
SOURCES = main.cpp

$(TARGET): $(SOURCES) $(wildcard engine/**/*.h engine/*.h)
	$(CXX) $(CXXFLAGS) -o $(TARGET) $(SOURCES)

clean:
	@if exist $(TARGET) del /f /q $(TARGET)

EMCC = em++
WASM_ENTRY = wasm/bindings.cpp
WASM_OUT_DIR = npm/dist
WASM_OUT = $(WASM_OUT_DIR)/cuffscript.mjs

EMFLAGS = -std=c++17 -O3 -fexceptions --bind \
	-s MODULARIZE=1 \
	-s EXPORT_ES6=1 \
	-s EXPORT_NAME=createCuffScriptModule \
	-s ENVIRONMENT=web,worker \
	-s ALLOW_MEMORY_GROWTH=1 \
	-s INITIAL_MEMORY=33554432 \
	-s MAXIMUM_MEMORY=268435456 \
	-s STACK_SIZE=16777216 \
	-s FORCE_FILESYSTEM=1 \
	-s EXPORTED_RUNTIME_METHODS="['FS']" \
	-s NO_EXIT_RUNTIME=1

wasm: $(WASM_ENTRY) $(wildcard engine/**/*.h engine/*.h)
	@if not exist "npm\dist" mkdir "npm\dist"
	$(EMCC) $(EMFLAGS) $(WASM_ENTRY) -o $(WASM_OUT)

wasm-clean:
	@if exist "npm\dist" rmdir /s /q "npm\dist"

.PHONY: clean wasm wasm-clean
