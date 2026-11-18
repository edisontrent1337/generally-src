# GeneRally 1.2d source reconstruction.
#
#   make lift      once: Ghidra p-code export of the original exe -> recompiled C (build/lift)
#   make -j8       build/gr       = recompiled original + rewritten functions from src/
#                  build/gr_orig  = recompiled original only (reference)
#   make check     run the comparison scenarios (tools/check.py)
#
# Needs: clang, SDL2 (sdl2-config), python3; for `make lift` also Ghidra + JDK (brew).

EXE      := game/orig/GeneRally.exe
PCODE    := build/orig.pcode
LIFT     := build/lift
OBJ      := build/obj
CC       := clang
CXX      := clang++
SDLCF    := $(shell sdl2-config --cflags)
SDLLD    := $(shell sdl2-config --libs) -lobjc
# -ffp-contract=off: no fused multiply-add, float results must match the x86 SSE code bit for bit
COMMON   := -O2 -g -fno-strict-aliasing -ffp-contract=off -fwrapv -Iharness/rt
CFLAGS   := -std=c11 $(COMMON)
CXXFLAGS := -std=c++20 $(COMMON) -Isrc -fno-exceptions -fno-rtti -Wall -Wextra -Wno-unused-parameter
LDFLAGS  := -Wl,-stack_size,0x10000000

GHIDRA   ?= $(shell brew --prefix ghidra 2>/dev/null)/libexec/support/analyzeHeadless
JAVA_HOME ?= $(shell brew --prefix openjdk@25 2>/dev/null)/libexec/openjdk.jdk/Contents/Home
export JAVA_HOME

LIFT_C   := $(wildcard $(LIFT)/f[0-9]*.c) $(LIFT)/weak.c $(LIFT)/functab.c $(LIFT)/image.c
RT_C     := $(wildcard harness/rt/*.c)
SRC_CXX  := $(shell find src -name '*.cpp' 2>/dev/null)
LIFT_O   := $(patsubst $(LIFT)/%.c,$(OBJ)/lift/%.o,$(LIFT_C))
RT_O     := $(patsubst harness/rt/%.c,$(OBJ)/rt/%.o,$(RT_C))
SRC_O    := $(patsubst src/%.cpp,$(OBJ)/src/%.o,$(SRC_CXX))

all: build/gr build/gr_orig

build/gr: $(LIFT_O) $(RT_O) $(SRC_O)
	$(CXX) -o $@ $^ $(SDLLD) $(LDFLAGS)
	@echo built $@

build/gr_orig: $(LIFT_O) $(RT_O)
	$(CC) -o $@ $^ $(SDLLD) $(LDFLAGS)

$(OBJ)/lift/%.o: $(LIFT)/%.c harness/rt/cpu.h
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -w -g0 -c $< -o $@

$(OBJ)/rt/%.o: harness/rt/%.c $(wildcard harness/rt/*.h)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -Wall -Wextra -Wno-unused-parameter $(SDLCF) -c $< -o $@

$(OBJ)/src/%.o: src/%.cpp $(shell find src -name '*.h' 2>/dev/null) $(wildcard harness/rt/*.h) $(LIFT)/funcs.h
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -I$(LIFT) -c $< -o $@

# ---------------------------------------------------------------- one-time recompilation of the original
$(PCODE): $(EXE) tools/ghidra/ExportPcode.java
	@mkdir -p build/ghidra
	$(GHIDRA) build/ghidra GRO -import $(EXE) -overwrite -scriptPath tools/ghidra \
	    -postScript ExportPcode.java $(abspath $(PCODE)) > build/ghidra_orig.log 2>&1

lift: $(PCODE) tools/lift.py tools/gen_image.py
	@mkdir -p $(LIFT)
	python3 tools/lift.py $(PCODE) $(LIFT) $(EXE)
	python3 tools/gen_image.py $(EXE) $(LIFT)/image.c

check: all
	python3 tools/check.py

clean:
	rm -rf $(OBJ) build/gr build/gr_orig

.PHONY: all lift check clean

# ---------------------------------------------------------------- reference material for the rewrite
# build/decomp.c (Ghidra pseudo-C of every function) and build/funcinfo.tsv (callers/callees/strings)
decomp: $(PCODE)
	$(GHIDRA) build/ghidra GRO -process GeneRally.exe -noanalysis -readOnly -scriptPath tools/ghidra \
	    -postScript DumpDecomp.java $(abspath build/decomp.c) \
	    -postScript ExportFuncInfo.java $(abspath build/funcinfo.tsv) > build/ghidra_decomp.log 2>&1

venv:
	python3 -m venv build/venv && build/venv/bin/pip install -q -r tools/requirements.txt

.PHONY: decomp venv
