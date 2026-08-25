# Compiler and tool configurations
CC := gcc
BISON := bison
FLEX := flex
PYTHON := python3
EMCC := emcc
CPPCHECK ?= cppcheck
CLANG_TIDY ?= clang-tidy-15

# Compiler and linker flags
CFLAGS := -Wall -Wextra -Wpedantic -Werror -O2 -Wuninitialized -fsanitize=address,undefined -fno-omit-frame-pointer -g
LDFLAGS := -lfl -lm -ldl -rdynamic
SO_CFLAGS := -fPIC -shared
SO_LDFLAGS :=

# `make release` ships binaries (GitHub Actions release matrix). Drop
# sanitizers so the artifact doesn't need libasan/libubsan at runtime.
# FLEX_PREFIX is for keg-only Homebrew flex on macOS (the release
# workflow sets it to $(brew --prefix flex)).
UNAME_S := $(shell uname -s)
RELEASE_CFLAGS := -Wall -Wextra -Wpedantic -Werror -O2 -Wuninitialized
FLEX_CPPFLAGS :=
FLEX_LIB :=
ifneq ($(FLEX_PREFIX),)
FLEX_CPPFLAGS := -I$(FLEX_PREFIX)/include
FLEX_LIB := -L$(FLEX_PREFIX)/lib
endif
ifeq ($(UNAME_S),Darwin)
# Mach-O dylibs loaded with dlopen() resolve host-owned globals such as
# g_exec_context at load time. ELF gets the same behavior from the main
# binary's -rdynamic link; Darwin needs the dylib link to allow it.
SO_LDFLAGS := -Wl,-undefined,dynamic_lookup
endif

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

# Sources scanned by cppcheck: the same translation units `all` builds, minus
# the generated Flex/Bison output (never scan or hand-edit lang.tab.c /
# lex.yy.c). tests/ is deliberately excluded: tests/badnatives/*.c are
# intentionally malformed registries (see that directory's own file comment),
# so a clean cppcheck run over them would be meaningless.
CPPCHECK_SRCS := $(SRCS) $(STDROT_SRCS)

# Test-only stdrot library: production natives plus tests/stdrot/*.c
# (bindings that exist solely so test_cases/*.brainrot can exercise ABI
# paths no production builtin uses, e.g. STDROT_PTR -- see that
# directory's own file comment). Deliberately a SEPARATE output from
# $(STDROT_LIB): a self-registering native compiled into libstdrot.so is
# a real, permanently-shipped Brainrot builtin the instant it compiles,
# so test-only natives must never be part of that file, `make install`,
# or the wasm build. `make test`/`make valgrind` point the interpreter at
# this one instead via stdrot_load()'s STDROT_LIB_PATH override
# (stdrot.c); every other target (`all`, `install`, `wasm`) never
# references it and stays exactly as before.
TEST_STDROT_DIR := tests/stdrot
TEST_STDROT_SRCS := $(wildcard $(TEST_STDROT_DIR)/*.c)
TEST_STDROT_LIB := tests/libstdrot.so

# Deliberately malformed native registries (see tests/badnatives/ own file
# comment): each .c file there registers exactly one StdrotEntry that
# validate_native_registry() (stdrot.c) must reject at stdrot_load() time.
# Built as one minimal .so per file -- registry.c (stdrot_get_api_v2()) plus
# that single malformed entry, no production natives -- since these tests
# only care whether loading aborts before any Brainrot program runs.
BADNATIVES_DIR := tests/badnatives
BADNATIVES_SRCS := $(wildcard $(BADNATIVES_DIR)/*.c)
BADNATIVES_LIBS := $(BADNATIVES_SRCS:.c=.so)

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

# Release build: no sanitizers. Add rpath for the leaf-name
# dlopen("libstdrot.so") fallback after stdrot_load() first tries
# cwd-relative ./libstdrot.so ($ORIGIN on ELF, @loader_path on Mach-O).
# Darwin omits -ldl (dlopen lives in libSystem). Same pattern as `debug`:
# clean then all, so a prior sanitizer build can't be reused.
.PHONY: release
ifeq ($(UNAME_S),Darwin)
# Apple Clang enables the same warnings that the wasm/Clang build already
# documents and suppresses above; keep Darwin release CI focused on packaging
# the existing interpreter until those behavior-neutral cleanups land.
release: CFLAGS := $(RELEASE_CFLAGS) -Wno-strict-prototypes \
	-Wno-tautological-negation-compare $(FLEX_CPPFLAGS)
release: LDFLAGS := $(FLEX_LIB) -lfl -lm -rdynamic -Wl,-rpath,@loader_path
else
release: CFLAGS := $(RELEASE_CFLAGS) $(FLEX_CPPFLAGS)
release: LDFLAGS := $(FLEX_LIB) -lfl -lm -ldl -rdynamic -Wl,-rpath,'$$ORIGIN'
endif
release: clean all
	@echo "Release build: $(TARGET) + $(STDROT_LIB) (no sanitizers)."

# stdrot shared library build
$(STDROT_LIB): $(STDROT_SRCS)
	$(CC) $(SO_CFLAGS) -I. -o $@ $^ -lm $(SO_LDFLAGS)
	@echo "libstdrot.so compiled with max rizz."

# Test-only stdrot shared library build (production natives + tests/stdrot/
# test-only natives). -I$(STDROT_DIR) so tests/stdrot/*.c can #include
# "stdrot_api.h" the same bare way every production stdrot/*.c file already
# does, despite living in a different directory.
$(TEST_STDROT_LIB): $(STDROT_SRCS) $(TEST_STDROT_SRCS)
	$(CC) $(SO_CFLAGS) -I. -I$(STDROT_DIR) -o $@ $^ -lm $(SO_LDFLAGS)
	@echo "tests/libstdrot.so (production + test-only natives) compiled."

# Malformed-registry .so's: one per tests/badnatives/*.c, each linked
# against only registry.c (never the rest of $(STDROT_SRCS) -- these don't
# need, and shouldn't get, any production natives alongside the one
# deliberately broken entry).
$(BADNATIVES_DIR)/%.so: $(BADNATIVES_DIR)/%.c $(STDROT_DIR)/registry.c
	$(CC) $(SO_CFLAGS) -I. -I$(STDROT_DIR) -o $@ $(STDROT_DIR)/registry.c $< -lm $(SO_LDFLAGS)

# Two exceptions to the pattern rule above (GNU Make prefers an explicit
# target rule over a pattern rule for the same file, regardless of
# ordering): these implement stdrot_get_api_v2() DIRECTLY themselves,
# returning a hand-crafted malformed StdrotAPI table, rather than going
# through registry.c's normal linker-section self-registration --
# linking registry.c alongside them would collide (both would define
# stdrot_get_api_v2()). See their own file comments.
$(BADNATIVES_DIR)/bad_api_table_negative_count.so: \
	$(BADNATIVES_DIR)/bad_api_table_negative_count.c
	$(CC) $(SO_CFLAGS) -I. -I$(STDROT_DIR) -o $@ $< $(SO_LDFLAGS)

$(BADNATIVES_DIR)/bad_api_table_null_functions.so: \
	$(BADNATIVES_DIR)/bad_api_table_null_functions.c
	$(CC) $(SO_CFLAGS) -I. -I$(STDROT_DIR) -o $@ $< $(SO_LDFLAGS)

.PHONY: badnatives
badnatives: $(BADNATIVES_LIBS)
	@echo "tests/badnatives/*.so (malformed registries) compiled."

# Simulated pre-ABI-versioning libstdrot.so (see tests/old_abi_sim/ own file
# comment): built standalone, with no dependency on stdrot_api.h or
# registry.c, so it genuinely only exports the OLD "stdrot_get_api" symbol
# under the OLD layout -- proving stdrot_load() (stdrot.c) detects the
# missing stdrot_get_api_v2() and fails loudly instead of misreading this
# .so's memory as the current ABI shape.
OLD_ABI_SIM_DIR := tests/old_abi_sim
OLD_ABI_SIM_LIB := $(OLD_ABI_SIM_DIR)/fake_pre_v2_registry.so

$(OLD_ABI_SIM_LIB): $(OLD_ABI_SIM_DIR)/fake_pre_v2_registry.c
	$(CC) $(SO_CFLAGS) -o $@ $< $(SO_LDFLAGS)

.PHONY: old-abi-sim
old-abi-sim: $(OLD_ABI_SIM_LIB)
	@echo "tests/old_abi_sim/fake_pre_v2_registry.so (simulated pre-ABI-versioning .so) compiled."

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

# Test-augmented WebAssembly build: same as `wasm` but with
# tests/stdrot/*.c (test-only natives, see that directory's own file
# comment) statically linked in too, mirroring tests/libstdrot.so's
# relationship to the native $(STDROT_LIB) build. Needed because the
# pointer-ABI/return-type-enforcement fixtures that depend on those
# natives exercise void*/uintptr_t/pointer_level/pointer-sized boxes
# whose representation genuinely differs between wasm32 (ILP32) and
# native (LP64) -- skipping them under wasm entirely would mean the one
# target where that representation difference actually matters never
# runs them. Output (tests/brainrot-test.wasm/.mjs) is a separate
# artifact from `wasm`'s, used only by tests/run_wasm_tests.mjs -- never
# uploaded, installed, or otherwise shipped.
.PHONY: wasm-test
wasm-test: $(GENERATED_SRCS)
	@command -v $(EMCC) >/dev/null 2>&1 || { echo "Error: emcc not found. Install the Emscripten SDK (emsdk) first."; exit 1; }
	$(EMCC) $(WASM_CFLAGS) -I. -I$(STDROT_DIR) -o tests/brainrot-test.mjs \
		$(SRCS) $(STDROT_SRCS) $(TEST_STDROT_SRCS) $(GENERATED_SRCS) \
		$(WASM_LDFLAGS)
	@echo "tests/brainrot-test.wasm (production + test-only natives) compiled."

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
test: $(TARGET) $(TEST_STDROT_LIB) badnatives old-abi-sim
	STDROT_LIB_PATH=$(CURDIR)/$(TEST_STDROT_LIB) $(PYTHON) -m pytest -v
	@echo "Tests ran bussin', no cap."

# Clean build artifacts
.PHONY: clean
clean:
	rm -f $(TARGET) $(STDROT_LIB) $(TEST_STDROT_LIB) $(GENERATED_SRCS) lang.tab.h
	rm -f $(WASM_TARGET) $(WASM_JS)
	rm -f tests/brainrot-test.wasm tests/brainrot-test.mjs
	rm -f $(BADNATIVES_LIBS)
	rm -f $(OLD_ABI_SIM_LIB)
	rm -f *.o
	@echo "Blud cleaned up the mess like a true sigma coder."

# Run Valgrind on all .brainrot tests
.PHONY: valgrind
valgrind: $(TARGET) $(TEST_STDROT_LIB)
	@STDROT_LIB_PATH=$(CURDIR)/$(TEST_STDROT_LIB) ./run_valgrind_tests.sh
	@echo "Valgrind check done. If anything was sus, it'll show up with a non-zero exit code. No cap."

# Install target
# libstdrot.so is dlopen'd by bare filename at runtime (see stdrot.c), so it
# must live somewhere the dynamic linker searches by default. /usr/local/lib
# is on that search path (see /etc/ld.so.conf.d/libc.conf); ldconfig refreshes
# the cache so the plain dlopen("libstdrot.so", ...) fallback finds it from
# any working directory, not just the build tree (whose ./libstdrot.so is
# tried first).
.PHONY: install
install: ensure-stdrot $(TARGET)
	install -d /usr/local/bin /usr/local/lib
	install -m 755 $(TARGET) /usr/local/bin/
	install -m 755 $(STDROT_LIB) /usr/local/lib/
	ldconfig
	@echo "$(TARGET) installed successfully. You're goated with the sauce!"

# Uninstall target
.PHONY: uninstall
uninstall:
	rm -f /usr/local/bin/$(TARGET)
	rm -f /usr/local/lib/$(STDROT_LIB)
	ldconfig
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

# Files formatted by clang-format: every tracked .c/.h, minus generated
# Flex/Bison output (lang.tab.c, lang.tab.h, lex.yy.c are gitignored and
# regenerated by `make` — never hand-format or commit them).
FORMAT_FILES := $(shell find . -name "*.c" -o -name "*.h" | \
	grep -v -E '^\./(lang\.tab\.c|lang\.tab\.h|lex\.yy\.c)$$')

CLANG_FORMAT ?= clang-format-15

# Format source files (requires clang-format-15)
.PHONY: format
format:
	@command -v $(CLANG_FORMAT) >/dev/null 2>&1 || { echo "Error: clang-format not found. Ratioed by clang."; exit 1; }
	$(CLANG_FORMAT) -i $(FORMAT_FILES)
	@echo "Source files got the rizz treatment, goated with the sauce."

# Check formatting without modifying files (used by CI's lint job)
.PHONY: format-check
format-check:
	@command -v $(CLANG_FORMAT) >/dev/null 2>&1 || { echo "Error: clang-format not found. Ratioed by clang."; exit 1; }
	$(CLANG_FORMAT) --dry-run -Werror $(FORMAT_FILES)
	@echo "Formatting check passed, no cap."

# --check-level=exhaustive needs cppcheck >= 2.11; nullPointerOutOfMemory
# needs >= 2.12. Ubuntu 22.04's apt cppcheck (2.7) is too old for either.
CPPCHECK_MIN_VERSION := 2.13

CPPCHECK_FLAGS := \
	--enable=warning,performance,portability \
	--check-level=exhaustive \
	--inline-suppr \
	--suppressions-list=cppcheck-suppressions.txt \
	--error-exitcode=1 \
	--std=c11 --language=c --platform=unix64 \
	-I. -I$(SRC_DIR) -I$(STDROT_DIR) \
	--suppress=missingIncludeSystem \
	-j4

# Static analysis with cppcheck (used by CI's static-analysis job). `style`
# checks are deliberately not enabled here -- see issue #172 for why that's a
# separate follow-up, not this gate.
.PHONY: cppcheck
cppcheck:
	@command -v $(CPPCHECK) >/dev/null 2>&1 || { echo "Error: cppcheck not found. Blud, install cppcheck >= $(CPPCHECK_MIN_VERSION)!"; exit 1; }
	@ver=$$($(CPPCHECK) --version | awk '{print $$2}'); \
	awk -v v="$$ver" -v min="$(CPPCHECK_MIN_VERSION)" 'BEGIN { \
		split(v, a, "."); split(min, b, "."); \
		for (i = 1; i <= 3; i++) { \
			va = (a[i] == "" ? 0 : a[i]) + 0; \
			vb = (b[i] == "" ? 0 : b[i]) + 0; \
			if (va > vb) exit 0; \
			if (va < vb) exit 1; \
		} \
		exit 0; \
	}' || { echo "Error: cppcheck $$ver is too old (need >= $(CPPCHECK_MIN_VERSION)). Ratioed by an ancient toolchain."; exit 1; }
	$(CPPCHECK) $(CPPCHECK_FLAGS) $(CPPCHECK_SRCS)
	@echo "cppcheck found nothing sus. Certified W."

# clang-tidy uses a fixed compiler-flags tail (below) instead of a generated
# compile_commands.json: $(TARGET) compiles+links $(ALL_SRCS) in a single gcc
# invocation rather than one -c per file, so an intercept tool (e.g. bear)
# wouldn't cleanly map to "one compile command per translation unit" here --
# and every file in $(CPPCHECK_SRCS) already builds under the same flags and
# include paths anyway (the same reason cppcheck above works as a flat file
# list), so a real compilation database buys nothing. Checks are configured
# in .clang-tidy (small allowlist, not checks=*; see issue #172).
.PHONY: tidy
tidy:
	@command -v $(CLANG_TIDY) >/dev/null 2>&1 || { echo "Error: clang-tidy not found. Blud, install clang-tidy-15!"; exit 1; }
	$(CLANG_TIDY) $(CPPCHECK_SRCS) -- $(CFLAGS) -I. -I$(SRC_DIR) -I$(STDROT_DIR)
	@echo "clang-tidy found nothing sus. Certified W."

# Show help
.PHONY: help
help:
	@echo "Available targets (rizzy edition):"
	@echo "  all        : Build the main executable (default target). Sigma grindset activated."
	@echo "  release    : Sanitizer-free build with rpath for shipped binaries (GitHub releases)."
	@echo "  install    : Install the binary to /usr/local/bin. Certified W."
	@echo "  uninstall  : Uninstall the binary from /usr/local/bin. Back to square one."
	@echo "  test       : Run the test suite. Huggy Wuggy approves."
	@echo "  wasm       : Build brainrot.wasm/brainrot.mjs for the browser. Requires emcc (Emscripten SDK)."
	@echo "  clean      : Remove all generated files. Amogus sussy imposter mode."
	@echo "  check-deps : Verify all required bro apps are installed."
	@echo "  rebuild    : Clean and re-grind the project."
	@echo "  format     : Format source files using clang-format. No cringe, all kino."
	@echo "  format-check : Check formatting without modifying files (CI lint job)."
	@echo "  cppcheck   : Static analysis with cppcheck (CI static-analysis job)."
	@echo "  tidy       : Static analysis with clang-tidy (CI static-analysis job)."
	@echo "  valgrind   : Checks for sussy memory leaks with Valgrind."
	@echo "  help       : Show this help for n00bs."
	@echo ""
	@echo "Configuration (poggers):"
	@echo "  CC        = $(CC)"
	@echo "  CFLAGS    = $(CFLAGS)"
	@echo "  LDFLAGS   = $(LDFLAGS)"
	@echo "  TARGET    = $(TARGET)"
