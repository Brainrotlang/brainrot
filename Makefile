# Compiler and tool configurations
CC := gcc
BISON := bison
FLEX := flex
PYTHON := python3
EMCC := emcc

# Compiler and linker flags
CFLAGS := -Wall -Wextra -Wpedantic -Werror -O2 -Wuninitialized -fsanitize=address,undefined -fno-omit-frame-pointer -g
LDFLAGS := -lfl -lm -ldl -rdynamic
SO_CFLAGS := -fPIC -shared

# Source files and directories
SRC_DIR := lib
DEBUG_FLAGS := -g
SRCS := $(SRC_DIR)/hm.c $(SRC_DIR)/mem.c $(SRC_DIR)/arena.c ast.c visitor.c semantic_analyzer.c interpreter.c stdrot.c
GENERATED_SRCS := lang.tab.c lex.yy.c
ALL_SRCS := $(SRCS) $(GENERATED_SRCS)

# stdrot shared library
STDROT_DIR := stdrot
STDROT_SRCS := $(wildcard $(STDROT_DIR)/*.c) $(SRC_DIR)/input.c
STDROT_LIB := libstdrot.so

# Output files
TARGET := brainrot
BISON_OUTPUT := lang.tab.c
FLEX_OUTPUT := lex.yy.c

# WebAssembly build (see issue #175): stdrot is statically linked in
# (-DSTDROT_STATIC, see stdrot.c) instead of built as a .so and dlopen'd —
# wasm has no dynamic loader worth using for a single-artifact build.
WASM_TARGET := brainrot.wasm
WASM_JS := brainrot.mjs
# -Wno-strict-prototypes: emcc's clang enables -Wstrict-prototypes under
# -Wextra (gcc's -Wextra does not), which fires on this codebase's existing
# K&R-style `void foo();` declarations (ast.h, lib/hm.h, lang.y). Fixing
# those is a mechanical but wide-reaching cleanup across shared headers,
# out of scope for adding a build target; suppressed here rather than
# silently dropping -Werror for the whole target.
#
# -Wno-tautological-negation-compare: clang (not gcc, so native -Werror
# never caught it) flags `if (matched || !matched)` in
# execute_switch_statement (ast.c) as always-true. It's real — `based`
# placed before a matching numbered case fires unconditionally instead of
# only as a true fallthrough/default — but it's a switch/default
# *semantics* bug, not a build-target issue; fixing it is a behavior
# change to the interpreter that needs its own test_cases fixture and
# review, not something to fold into -Werror for a build target. See
# issue #179.
WASM_CFLAGS := -Wall -Wextra -Wpedantic -Werror -O2 -DSTDROT_STATIC \
	-Wno-strict-prototypes -Wno-tautological-negation-compare
# MAXIMUM_MEMORY caps how far ALLOW_MEMORY_GROWTH can grow a single
# module instance — defense in depth against a runaway Brainrot program
# (e.g. an unbounded array) in a browser tab.
WASM_LDFLAGS := -lm \
	-sMODULARIZE=1 -sEXPORT_ES6=1 -sEXPORT_NAME=createBrainrotModule \
	-sINVOKE_RUN=0 -sEXIT_RUNTIME=1 \
	-sEXPORTED_RUNTIME_METHODS=callMain,FS \
	-sALLOW_MEMORY_GROWTH=1 -sMAXIMUM_MEMORY=256MB

# Default target
.PHONY: all
all: $(STDROT_LIB) $(TARGET)

# Ensure shared library exists for runtime targets
.PHONY: ensure-stdrot
ensure-stdrot:
	@if [ ! -f $(STDROT_LIB) ]; then \
		echo "$(STDROT_LIB) not found. Building it now..."; \
		$(MAKE) $(STDROT_LIB); \
	fi

# Build only the standard library
.PHONY: lib
lib: $(STDROT_LIB)

# Debug target
.PHONY: debug
debug: CFLAGS += $(DEBUG_FLAGS)
debug: clean all
	@echo "Debug build compiled with -g. Time to sigma grind with GDB."

# stdrot shared library build
$(STDROT_LIB): $(STDROT_SRCS)
	$(CC) $(SO_CFLAGS) -I. -o $@ $^ -lm
	@echo "libstdrot.so compiled with max rizz."

# Main executable build
$(TARGET): $(ALL_SRCS) $(STDROT_LIB)
	$(CC) $(CFLAGS) -o $@ $(ALL_SRCS) $(LDFLAGS)
	@echo "Skibidi toilet: $(TARGET) compiled with max gyatt."

# WebAssembly build: stdrot sources go straight into the same binary
# instead of a separate $(STDROT_LIB), so this does NOT depend on it.
.PHONY: wasm
wasm: $(GENERATED_SRCS)
	@command -v $(EMCC) >/dev/null 2>&1 || { echo "Error: emcc not found. Install the Emscripten SDK (emsdk) first."; exit 1; }
	$(EMCC) $(WASM_CFLAGS) -I. -o $(WASM_JS) $(SRCS) $(STDROT_SRCS) $(GENERATED_SRCS) $(WASM_LDFLAGS)
	@echo "brainrot.wasm compiled. Skibidi in the browser."

# Generate parser files using Bison
$(BISON_OUTPUT): lang.y
	$(BISON) -d -Wcounterexamples $< -o $@
	@echo "Bison is sigma grinding with $(BISON_OUTPUT)."

# Generate lexer files using Flex
$(FLEX_OUTPUT): lang.l
	$(FLEX) $<
	@echo "Flex is literally hitting the griddy to generate $(FLEX_OUTPUT)."

# Run tests
.PHONY: test
test: ensure-stdrot $(TARGET)
	$(PYTHON) -m pytest -v
	@echo "Tests ran bussin', no cap."

# Clean build artifacts
.PHONY: clean
clean:
	rm -f $(TARGET) $(STDROT_LIB) $(GENERATED_SRCS) lang.tab.h
	rm -f $(WASM_TARGET) $(WASM_JS)
	rm -f *.o
	@echo "Blud cleaned up the mess like a true sigma coder."

# Run Valgrind on all .brainrot tests
.PHONY: valgrind
valgrind: ensure-stdrot $(TARGET)
	@./run_valgrind_tests.sh
	@echo "Valgrind check done. If anything was sus, it'll show up with a non-zero exit code. No cap."

# Install target
.PHONY: install
install: ensure-stdrot $(TARGET)
	install -d /usr/local/bin
	install -m 755 $(TARGET) /usr/local/bin/
	@echo "$(TARGET) installed successfully. You're goated with the sauce!"

# Uninstall target
.PHONY: uninstall
uninstall:
	rm -f /usr/local/bin/$(TARGET)
	@echo "$(TARGET) uninstalled successfully. Back to the grind."

# Check dependencies
.PHONY: check-deps
check-deps:
	@command -v $(CC) >/dev/null 2>&1 || { echo "Error: gcc not found. Blud, install gcc!"; exit 1; }
	@command -v $(BISON) >/dev/null 2>&1 || { echo "Error: bison not found. Duke Dennis did you pray today?"; exit 1; }
	@command -v $(FLEX) >/dev/null 2>&1 || { echo "Error: flex not found. Ayo, where's flex?"; exit 1; }
	@command -v $(PYTHON) >/dev/null 2>&1 || { echo "Error: python3 not found. Python in Ohio moment."; exit 1; }
	@$(PYTHON) -c "import pytest" >/dev/null 2>&1 || { echo "Error: pytest not found. Install with: pip install pytest. That's the ocky way."; exit 1; }

# Development helper to rebuild everything from scratch
.PHONY: rebuild
rebuild: clean all
	@echo "Whole bunch of turbulence cleared. Rebuilt everything."

# Format source files (requires clang-format)
.PHONY: format
format:
	@command -v clang-format >/dev/null 2>&1 || { echo "Error: clang-format not found. Ratioed by clang."; exit 1; }
	find . -name "*.c" -o -name "*.h" | xargs clang-format -i
	@echo "Source files got the rizz treatment, goated with the sauce."

# Show help
.PHONY: help
help:
	@echo "Available targets (rizzy edition):"
	@echo "  all        : Build the main executable (default target). Sigma grindset activated."
	@echo "  install    : Install the binary to /usr/local/bin. Certified W."
	@echo "  uninstall  : Uninstall the binary from /usr/local/bin. Back to square one."
	@echo "  test       : Run the test suite. Huggy Wuggy approves."
	@echo "  wasm       : Build brainrot.wasm/brainrot.mjs for the browser. Requires emcc (Emscripten SDK)."
	@echo "  clean      : Remove all generated files. Amogus sussy imposter mode."
	@echo "  check-deps : Verify all required bro apps are installed."
	@echo "  rebuild    : Clean and re-grind the project."
	@echo "  format     : Format source files using clang-format. No cringe, all kino."
	@echo "  valgrind   : Checks for sussy memory leaks with Valgrind."
	@echo "  help       : Show this help for n00bs."
	@echo ""
	@echo "Configuration (poggers):"
	@echo "  CC        = $(CC)"
	@echo "  CFLAGS    = $(CFLAGS)"
	@echo "  LDFLAGS   = $(LDFLAGS)"
	@echo "  TARGET    = $(TARGET)"
