CXX = g++
CXXFLAGS = -std=c++17 -Wall -Wextra -Werror -O3 -flto -DNDEBUG
# Optional extra flags, e.g. `make EXTRA_CXXFLAGS=-DCUFF_DISABLE_NETWORK` (see docs/contributing/extending.md).
CXXFLAGS += $(EXTRA_CXXFLAGS) $(DLC_FLAGS)

# Libraries to leave out of the build, e.g. `make EXCLUDE_DLC="network hardware"`: each one is then unknown, as if it
# had never existed (see docs/contributing/building.md). DLC:convert holds built-ins and cannot be left out. After changing
# the list, build with `make -B` (the binary does not depend on it).
DLC_NAMES = math string time datetime random list map json network filesystem csv hash path hardware
EXCLUDE_DLC ?=
ifneq ($(filter-out $(DLC_NAMES),$(EXCLUDE_DLC)),)
$(error EXCLUDE_DLC cannot leave out $(filter-out $(DLC_NAMES),$(EXCLUDE_DLC)) (those that can be left out are: $(DLC_NAMES)))
endif
dlc_upper = $(subst a,A,$(subst b,B,$(subst c,C,$(subst d,D,$(subst e,E,$(subst f,F,$(subst g,G,$(subst h,H,$(subst i,I,$(subst j,J,$(subst k,K,$(subst l,L,$(subst m,M,$(subst n,N,$(subst o,O,$(subst p,P,$(subst q,Q,$(subst r,R,$(subst s,S,$(subst t,T,$(subst u,U,$(subst v,V,$(subst w,W,$(subst x,X,$(subst y,Y,$(subst z,Z,$(1)))))))))))))))))))))))))))
DLC_FLAGS = $(foreach d,$(EXCLUDE_DLC),-DCUFF_EXCLUDE_DLC_$(call dlc_upper,$(d)))

TARGET = cuffc
SOURCES = main.cpp

# Strip symbols from the release binary (~45% smaller, no speed change). Apple's
# linker ignores -s with a warning, so skip it there.
ifneq ($(shell uname -s),Darwin)
LDFLAGS = -s
endif

$(TARGET): $(SOURCES) $(wildcard engine/**/*.h engine/*.h)
	$(CXX) $(CXXFLAGS) -o $(TARGET) $(SOURCES) $(LDFLAGS)

clean:
	rm -f $(TARGET)

EMCC = em++
WASM_ENTRY = wasm/bindings.cpp
WASM_OUT_DIR = npm/dist
WASM_OUT = $(WASM_OUT_DIR)/cuffscript.mjs

EMFLAGS = -std=c++17 -O3 -flto -fexceptions --bind -DNDEBUG \
	--no-entry \
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
EMFLAGS += $(EXTRA_CXXFLAGS) $(DLC_FLAGS)

wasm: $(WASM_ENTRY) $(wildcard engine/**/*.h engine/*.h)
	mkdir -p $(WASM_OUT_DIR)
	$(EMCC) $(EMFLAGS) $(WASM_ENTRY) -o $(WASM_OUT)

wasm-clean:
	rm -f $(WASM_OUT_DIR)/cuffscript.mjs $(WASM_OUT_DIR)/cuffscript.wasm

.PHONY: clean wasm wasm-clean
