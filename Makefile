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

# ── OpenSSL libcrypto: REQUIRED native build dependency of libstdrot.so ──────
# stdrot/gamba.c calls RAND_bytes for the cryptographically safe gamba()
# (issue #215). A missing OpenSSL must FAIL the native link, never compile a
# gamba-less or rand()-backed interpreter, so these flags are unconditional
# for every native libstdrot.so target. The wasm build (-DSTDROT_STATIC) never
# sees them: gamba is an erroring stub there (issue #175).
#
# The two platforms link libcrypto DIFFERENTLY on purpose:
CRYPTO_CFLAGS := $(shell pkg-config --cflags libcrypto 2>/dev/null)
ifeq ($(UNAME_S),Darwin)
# Homebrew's libcrypto is keg-only, so its dylib install_name is an absolute
# prefix path (e.g. /opt/homebrew/opt/openssl@3/lib/libcrypto.3.dylib). A
# dynamic link bakes that path into libstdrot.so as an LC_LOAD_DYLIB, so the
# shipped stdlib would only dlopen on a machine with that exact Homebrew
# prefix present -- every Brainrot program, not just gamba, would fail to
# load elsewhere. Statically link libcrypto.a instead: the resulting dylib is
# self-contained, still fails the link when OpenSSL is absent, and wasm is
# untouched. Auto-locate keg-only openssl for pkg-config so a plain `make`
# works without the caller pre-exporting PKG_CONFIG_PATH.
OPENSSL_PREFIX := $(shell brew --prefix openssl@3 2>/dev/null)
ifneq ($(OPENSSL_PREFIX),)
export PKG_CONFIG_PATH := $(OPENSSL_PREFIX)/lib/pkgconfig:$(PKG_CONFIG_PATH)
CRYPTO_CFLAGS := $(shell pkg-config --cflags libcrypto 2>/dev/null)
endif
CRYPTO_LIBDIR := $(shell pkg-config --variable=libdir libcrypto 2>/dev/null)
# An empty CRYPTO_LIBDIR yields "/libcrypto.a", which fails the link loudly --
# the intended "OpenSSL missing => build fails" behavior, not a silent skip.
CRYPTO_LIBS := $(CRYPTO_LIBDIR)/libcrypto.a
else
# Linux: libcrypto.so is a normal soname dependency (DT_NEEDED
# libcrypto.so.3), resolved on any machine with OpenSSL's runtime installed
# -- portable across hosts, unlike Homebrew's keg-only absolute path, so no
# static link is needed here. (Distro libcrypto.a is typically non-PIC and
# can't go into a shared object anyway.) Runtime dep is documented in
# README.md / docs §4 alongside libssl-dev.
CRYPTO_LIBS := $(shell pkg-config --libs libcrypto 2>/dev/null || echo -lcrypto)
endif

# Source files and directories
SRC_DIR := lib
DEBUG_FLAGS := -g
SRCS := $(SRC_DIR)/hm.c $(SRC_DIR)/mem.c $(SRC_DIR)/arena.c $(SRC_DIR)/module_path.c ast.c visitor.c semantic_analyzer.c interpreter.c stdrot.c
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

# Native module fixtures for #cooked <name> resolving to a ".so"
# (module_path.h's MODULE_ARTIFACT_NATIVE, stdrot_load_module() in
# stdrot.c) -- see tests/nativemodules/testnative.c's own file comment.
# Built the same way as $(BADNATIVES_LIBS) above: registry.c linked in
# fresh per fixture, since each is its own standalone .so.
NATIVEMODULES_DIR := tests/nativemodules
NATIVEMODULES_SRCS := $(wildcard $(NATIVEMODULES_DIR)/*.c)
NATIVEMODULES_LIBS := $(NATIVEMODULES_SRCS:.c=.so)

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
all: $(STDROT_LIB) $(TARGET) ## Build the interpreter + libstdrot.so (default). Sigma grindset activated.

# Ensure shared library exists for runtime targets
.PHONY: ensure-stdrot
ensure-stdrot:
	@if [ ! -f $(STDROT_LIB) ]; then \
		echo "$(STDROT_LIB) not found. Building it now..."; \
		$(MAKE) $(STDROT_LIB); \
	fi

# Build only the standard library
.PHONY: lib
lib: $(STDROT_LIB) ## Build only libstdrot.so, the builtin fanum tax collection.

# Debug target
.PHONY: debug
debug: CFLAGS += $(DEBUG_FLAGS)
debug: clean all ## Rebuild with -g (sanitizers on). Time to sigma grind with GDB.
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
release: clean all ## Sanitizer-free rpath build for shipped binaries. Certified glizzy gladiator.
	@echo "Release build: $(TARGET) + $(STDROT_LIB) (no sanitizers)."

# stdrot shared library build
$(STDROT_LIB): $(STDROT_SRCS)
	$(CC) $(SO_CFLAGS) $(CRYPTO_CFLAGS) -I. -o $@ $^ -lm $(CRYPTO_LIBS) $(SO_LDFLAGS)
	@echo "libstdrot.so compiled with max rizz."

# Test-only stdrot shared library build (production natives + tests/stdrot/
# test-only natives). -I$(STDROT_DIR) so tests/stdrot/*.c can #include
# "stdrot_api.h" the same bare way every production stdrot/*.c file already
# does, despite living in a different directory.
$(TEST_STDROT_LIB): $(STDROT_SRCS) $(TEST_STDROT_SRCS)
	$(CC) $(SO_CFLAGS) $(CRYPTO_CFLAGS) -I. -I$(STDROT_DIR) -o $@ $^ -lm $(CRYPTO_LIBS) $(SO_LDFLAGS)
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

# Native module fixtures ($(NATIVEMODULES_LIBS)): same registry.c-per-file
# pattern as $(BADNATIVES_DIR) above, but these are valid modules (real
# brainrot_module_init() entrypoints, well-formed tables except
# testnative_internal_dup.c and no_module_init.c, which are deliberately
# broken on purpose -- see their own file comments) used to exercise
# stdrot_load_module() (stdrot.c) end to end, not just its rejection paths.
#
# -DSTDROT_REGISTRY_ENTRYPOINT=brainrot_module_init (registry.c's own
# comment has the full reasoning): makes registry.c export
# brainrot_module_init() directly, under a name no other loaded .so in the
# process shares, instead of stdrot_get_api_v2() -- the same symbol name
# the always-loaded core libstdrot.so already exports, which a same-named
# wrapper calling it from inside a module would silently resolve to
# instead of the module's own copy.
$(NATIVEMODULES_DIR)/%.so: $(NATIVEMODULES_DIR)/%.c $(STDROT_DIR)/registry.c
	$(CC) $(SO_CFLAGS) -DSTDROT_REGISTRY_ENTRYPOINT=brainrot_module_init \
		-I. -I$(STDROT_DIR) -o $@ $(STDROT_DIR)/registry.c $< -lm $(SO_LDFLAGS)

# Exception to the pattern rule above (GNU Make prefers an explicit target
# rule for the same file): deliberately built WITHOUT the entrypoint
# override, so this is a structurally valid .so that exports
# stdrot_get_api_v2() but genuinely has no brainrot_module_init() at all --
# see this fixture's own file comment for what that proves.
$(NATIVEMODULES_DIR)/no_module_init.so: $(NATIVEMODULES_DIR)/no_module_init.c \
	$(STDROT_DIR)/registry.c
	$(CC) $(SO_CFLAGS) -I. -I$(STDROT_DIR) -o $@ $(STDROT_DIR)/registry.c $< \
		-lm $(SO_LDFLAGS)

.PHONY: nativemodules
nativemodules: $(NATIVEMODULES_LIBS)
	@echo "tests/nativemodules/*.so (native module fixtures) compiled."

# ── Optional raylib binding: the first cursed game (Issue #208, Phase 5 Road A)
# brainray/raylib.so is a hand-written native module (brainray/raylib.c) wrapping
# ~20 raylib primitives, loaded at runtime by `#cooked <raylib>`. It links
# against a real raylib resolved via pkg-config, so it is DELIBERATELY excluded
# from `all`, `test`, `valgrind`, `install`, and `wasm`: raylib is an optional
# dependency of THIS target only, and the whole test suite stays green without it
# installed. Built with the same
# -DSTDROT_REGISTRY_ENTRYPOINT=brainrot_module_init pattern as the nativemodules
# fixtures above (registry.c's own comment has the reasoning), so brainray/raylib.c's
# STDROT_EXPORT_SIG() entries are exported as brainrot_module_init().
BRAINRAY_DIR := brainray
BRAINRAY_LIB := $(BRAINRAY_DIR)/raylib.so
RAYLIB_CFLAGS := $(shell pkg-config --cflags raylib 2>/dev/null)
RAYLIB_LIBS := $(shell pkg-config --libs raylib 2>/dev/null)

.PHONY: brainray
brainray: $(BRAINRAY_LIB) ## Build the optional raylib binding brainray/raylib.so (needs raylib; docs/brainray.md). Cursed game unlocked.

$(BRAINRAY_LIB): $(BRAINRAY_DIR)/raylib.c $(STDROT_DIR)/registry.c
	@pkg-config --exists raylib || { \
		echo "Error: raylib not found via pkg-config (pkg-config --exists raylib failed)."; \
		echo "raylib is an OPTIONAL dependency, needed only for 'make brainray'."; \
		echo "Install it, then re-run 'make brainray'. Setup guide (Linux/macOS):"; \
		echo "  docs/brainray.md"; \
		echo "macOS: 'brew install raylib'. Verify with: pkg-config --exists raylib"; \
		exit 1; }
	$(CC) $(SO_CFLAGS) -Wall -Wextra \
		-DSTDROT_REGISTRY_ENTRYPOINT=brainrot_module_init \
		-I. -I$(STDROT_DIR) $(RAYLIB_CFLAGS) -o $@ \
		$(STDROT_DIR)/registry.c $< $(RAYLIB_LIBS) -lm $(SO_LDFLAGS)
	@echo "brainray/raylib.so built. Run the cursed game with:"
	@echo "  BRAINROT_PATH=$(BRAINRAY_DIR) ./brainrot examples/raylib/ohio_engine.brainrot"

# Convenience: build the binding and launch the cursed game in one step. It
# builds brainray/raylib.so and runs the example, so it needs both raylib and a
# display -- with no display the raylib window init fails (it does NOT skip).
# Still an explicit opt-in like `brainray` -- never a prerequisite of `all`/`test`.
# No ASAN_OPTIONS override: brainray brackets raylib's own calls with
# __lsan_disable/enable (issue #267, brainray/raylib.c), so the sanitizer build
# runs the game cleanly while still checking brainray's and the interpreter's
# own allocations.
.PHONY: play
play: $(BRAINRAY_LIB) $(TARGET) ## Build brainray + run the Ohio Engine (needs raylib + a display). It's giving cinema.
	BRAINROT_PATH=$(BRAINRAY_DIR) ./$(TARGET) examples/raylib/ohio_engine.brainrot

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

# Host C sizeof/offsetof oracle for struct/union layout (see that
# file's own comment for the full contract): _Static_assert/offsetof
# checks, compiled by this build's own $(CC), establishing what a real
# C compiler produces for the struct/union shapes several
# test_cases/*.brainrot layout fixtures mirror. This binary never calls
# into ast.c and does not itself verify that Brainrot's maxxing()
# output matches these numbers -- that cross-check is the fixtures'
# own expected output, which a human keeps in sync with this file by
# hand; a fixture shape with no matching struct declared here is not
# covered by this oracle at all.
ABI_CHECK_BIN := tests/abi/struct_layout_abi_check

$(ABI_CHECK_BIN): tests/abi/struct_layout_abi_check.c
	$(CC) $(CFLAGS) -o $@ $<

.PHONY: abi-check
abi-check: $(ABI_CHECK_BIN) ## Run the struct/union ABI layout oracle (host sizeof/offsetof). No bytes left behind, no cap.
	./$(ABI_CHECK_BIN)

# Main executable build
$(TARGET): $(ALL_SRCS) $(STDROT_LIB)
	$(CC) $(CFLAGS) -o $@ $(ALL_SRCS) $(LDFLAGS)
	@echo "Skibidi toilet: $(TARGET) compiled with max gyatt."

# WebAssembly build: stdrot sources go straight into the same binary
# instead of a separate $(STDROT_LIB), so this does NOT depend on it.
.PHONY: wasm
wasm: $(GENERATED_SRCS) ## Build brainrot.wasm/.mjs for the browser (needs emcc). Skibidi in the browser.
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
wasm-test: $(GENERATED_SRCS) ## Build the wasm test binary (production + test-only natives; needs emcc). Browser edition, extra sauce.
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
test: $(TARGET) $(TEST_STDROT_LIB) badnatives nativemodules old-abi-sim abi-check ## Build, then run the pytest suite. Huggy Wuggy approves.
	STDROT_LIB_PATH=$(CURDIR)/$(TEST_STDROT_LIB) $(PYTHON) -m pytest -v
	@echo "Tests ran bussin', no cap."

# Clean build artifacts
.PHONY: clean
clean: ## Remove all build artifacts (never touches source). Amogus sussy imposter mode.
	rm -f $(TARGET) $(STDROT_LIB) $(TEST_STDROT_LIB) $(GENERATED_SRCS) lang.tab.h
	rm -f $(WASM_TARGET) $(WASM_JS)
	rm -f tests/brainrot-test.wasm tests/brainrot-test.mjs
	rm -f $(BADNATIVES_LIBS)
	rm -f $(NATIVEMODULES_LIBS)
	rm -f $(BRAINRAY_LIB)
	rm -f $(OLD_ABI_SIM_LIB)
	rm -f $(ABI_CHECK_BIN)
	rm -f *.o
	@echo "Blud cleaned up the mess like a true sigma coder."

# Run Valgrind on all .brainrot tests
.PHONY: valgrind
valgrind: $(TARGET) $(TEST_STDROT_LIB) ## Run every test_cases/*.brainrot under Valgrind. Checks for sussy memory leaks.
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
install: ensure-stdrot $(TARGET) ## Install brainrot + libstdrot.so under /usr/local (needs root). You're goated with the sauce.
	install -d /usr/local/bin /usr/local/lib
	install -m 755 $(TARGET) /usr/local/bin/
	install -m 755 $(STDROT_LIB) /usr/local/lib/
	ldconfig
	@echo "$(TARGET) installed successfully. You're goated with the sauce!"

# Uninstall target
.PHONY: uninstall
uninstall: ## Remove an installed brainrot + libstdrot.so (needs root). Back to the grind.
	rm -f /usr/local/bin/$(TARGET)
	rm -f /usr/local/lib/$(STDROT_LIB)
	ldconfig
	@echo "$(TARGET) uninstalled successfully. Back to the grind."

# Check dependencies
.PHONY: check-deps
check-deps: ## Verify required bro apps are installed (gcc, bison, flex, python3, pytest, openssl).
	@command -v $(CC) >/dev/null 2>&1 || { echo "Error: gcc not found. Blud, install gcc!"; exit 1; }
	@command -v $(BISON) >/dev/null 2>&1 || { echo "Error: bison not found. Duke Dennis did you pray today?"; exit 1; }
	@command -v $(FLEX) >/dev/null 2>&1 || { echo "Error: flex not found. Ayo, where's flex?"; exit 1; }
	@command -v $(PYTHON) >/dev/null 2>&1 || { echo "Error: python3 not found. Python in Ohio moment."; exit 1; }
	@$(PYTHON) -c "import pytest" >/dev/null 2>&1 || { echo "Error: pytest not found. Install with: pip install pytest. That's the ocky way."; exit 1; }
	@# OpenSSL (libcrypto) is a required dependency of libstdrot.so (gamba(),
	@# issue #215). PKG_CONFIG_PATH is already extended for keg-only Homebrew
	@# openssl above, so this check matches how the real build resolves it.
	@pkg-config --exists libcrypto >/dev/null 2>&1 || { echo "Error: OpenSSL (libcrypto) not found via pkg-config. Install libssl-dev (Ubuntu/Debian), openssl (Arch), or 'brew install openssl@3' (macOS). No gamba without it, no cap."; exit 1; }

# Development helper to rebuild everything from scratch
.PHONY: rebuild
rebuild: clean all ## Clean and re-grind the whole project from scratch. Turbulence cleared.
	@echo "Whole bunch of turbulence cleared. Rebuilt everything."

# Files formatted by clang-format: every tracked .c/.h, minus generated
# Flex/Bison output (lang.tab.c, lang.tab.h, lex.yy.c are gitignored and
# regenerated by `make` — never hand-format or commit them).
FORMAT_FILES := $(shell find . -name "*.c" -o -name "*.h" | \
	grep -v -E '^\./(lang\.tab\.c|lang\.tab\.h|lex\.yy\.c)$$')

CLANG_FORMAT ?= clang-format-15

# Format source files (requires clang-format-15)
.PHONY: format
format: ## Reformat all C sources in-place with clang-format. No cringe, all kino.
	@command -v $(CLANG_FORMAT) >/dev/null 2>&1 || { echo "Error: clang-format not found. Ratioed by clang."; exit 1; }
	$(CLANG_FORMAT) -i $(FORMAT_FILES)
	@echo "Source files got the rizz treatment, goated with the sauce."

# Check formatting without modifying files (used by CI's lint job)
.PHONY: format-check
format-check: ## Check formatting without editing files (CI lint job). Stay drippy, no diffs.
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
cppcheck: ## Static analysis with cppcheck (CI static-analysis; needs >= 2.13). Certified W.
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
tidy: ## Static analysis with clang-tidy (needs clang-tidy-15). Nothing sus, certified W.
	@command -v $(CLANG_TIDY) >/dev/null 2>&1 || { echo "Error: clang-tidy not found. Blud, install clang-tidy-15!"; exit 1; }
	$(CLANG_TIDY) $(CPPCHECK_SRCS) -- $(CFLAGS) -I. -I$(SRC_DIR) -I$(STDROT_DIR)
	@echo "clang-tidy found nothing sus. Certified W."

# Show help. Self-documenting: the target list is generated from the `## `
# annotation on each target line above (a `target: ... ## description` line is
# all it takes to appear here), so this never goes stale by hand. Internal
# fixture builders (badnatives, nativemodules, old-abi-sim, ensure-stdrot) and
# pattern rules are deliberately left un-annotated so they stay out of the list.
# tests/test_docs_consistency.py guards that the key developer-facing targets
# remain represented.
.PHONY: help
help: ## Show this help for n00bs (the list of developer-facing targets).
	@echo "Brainrot make targets (rizzy edition):"
	@echo ""
	@awk 'BEGIN {FS = ":.*## "} \
		/^[a-zA-Z][a-zA-Z0-9_-]*:.*## / {printf "  %-14s %s\n", $$1, $$2}' \
		$(MAKEFILE_LIST)
	@echo ""
	@echo "raylib is optional and only 'make brainray'/'make play' need it."
	@echo "raylib setup guide (Ubuntu/macOS/source): docs/brainray.md"
	@echo ""
	@echo "Configuration (poggers):"
	@echo "  CC      = $(CC)"
	@echo "  CFLAGS  = $(CFLAGS)"
	@echo "  LDFLAGS = $(LDFLAGS)"
	@echo "  TARGET  = $(TARGET)"
