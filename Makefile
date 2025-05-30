# Compiler and tool configurations
CC := gcc
BISON := bison
FLEX := flex
PYTHON := python3

# Compiler and linker flags
CFLAGS := -Wall -Wextra -Wpedantic -Werror -O2  -Wuninitialized
LDFLAGS := -lfl -lm

# Source files and directories
SRC_DIR := lib
DEBUG_FLAGS := -g
SRCS := $(SRC_DIR)/hm.c $(SRC_DIR)/mem.c $(SRC_DIR)/input.c $(SRC_DIR)/arena.c  ast.c
GENERATED_SRCS := lang.tab.c lex.yy.c
ALL_SRCS := $(SRCS) $(GENERATED_SRCS)

# Output files
TARGET := brainrot
BISON_OUTPUT := lang.tab.c
FLEX_OUTPUT := lex.yy.c

# Default target
.PHONY: all
all: $(TARGET)

# Debug target
.PHONY: debug
debug: CFLAGS += $(DEBUG_FLAGS)
debug: clean $(TARGET)
	@echo "Debug build compiled with -g. Time to sigma grind with GDB."


# Main executable build
$(TARGET): $(ALL_SRCS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)
	@echo "Skibidi toilet: $(TARGET) compiled with max gyatt."

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
test:
	$(PYTHON) -m pytest -v
	@echo "Tests ran bussin', no cap."

# Clean build artifacts
.PHONY: clean
clean:
	rm -f $(TARGET) $(GENERATED_SRCS) lang.tab.h
	rm -f *.o
	@echo "Blud cleaned up the mess like a true sigma coder."

# Run Valgrind on all .brainrot tests
.PHONY: valgrind
valgrind:
	@./run_valgrind_tests.sh
	@echo "Valgrind check done. If anything was sus, it'll show up with a non-zero exit code. No cap."

# Install target
.PHONY: install
install:
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
