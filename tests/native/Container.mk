# Container.mk - native memory test build rules
#
# Build and run the C test harness that verifies memory management
# in Koskari's native types. Intended for use inside the container
# (run-tests.sh) where Valgrind and ASan are available; invoke with
# make -f Container.mk <target>.

# Python configuration
PYTHON ?= python3
PYTHON_CONFIG ?= $(PYTHON)-config

# Compiler
CC ?= gcc

# Test and library sources discovered via globs (stay in sync with tree)
LIB_DIR := ../../koskari/lib
TEST_SRCS := $(wildcard test_*.c)
LIB_SRCS := $(wildcard $(LIB_DIR)/*.c)

# All sources
SRCS := $(TEST_SRCS) $(LIB_SRCS)

# Object files
TEST_OBJS := $(TEST_SRCS:.c=.o)
LIB_OBJS := $(notdir $(LIB_SRCS:.c=.o))
OBJS := $(TEST_OBJS) $(LIB_OBJS)

# Output binary
TARGET := test_harness

# Compiler flags
PYTHON_CFLAGS := $(shell $(PYTHON_CONFIG) --cflags 2>/dev/null || echo "-I/usr/include/python3")
PYTHON_LDFLAGS := $(shell $(PYTHON_CONFIG) --ldflags --embed 2>/dev/null || $(PYTHON_CONFIG) --ldflags 2>/dev/null)

CFLAGS := -std=c99 -Wall -Werror -g
CFLAGS += $(PYTHON_CFLAGS)
CFLAGS += -I$(LIB_DIR)

LDFLAGS := $(PYTHON_LDFLAGS)

# ASan flags (used by test-asan target)
ASAN_CFLAGS := -fsanitize=address -fno-omit-frame-pointer
ASAN_LDFLAGS := -fsanitize=address

# ASan options
# Note: detect_leaks=0 because Python's allocator causes false positives.
# We're testing for use-after-free, double-free, buffer overflows - not leaks.
# Leak detection in C extensions embedded in Python is unreliable.
ASAN_OPTIONS := detect_leaks=0:print_stats=1

# Valgrind options
# Note: We only fail on actual errors, not leaks, because Python's allocator
# and internal structures cause many false positive leak reports.
VALGRIND := valgrind
VALGRIND_OPTS := --leak-check=full \
                 --show-leak-kinds=definite,indirect \
                 --track-origins=yes \
                 --error-exitcode=1 \
                 --suppressions=valgrind-python.supp

##@ Build Targets

.PHONY: all clean help

.DEFAULT_GOAL := help

help:  ## Display this help
	@awk 'BEGIN {FS = ":.*##"; printf "\nUsage:\n  make \033[36m<target>\033[0m\n"} /^[a-zA-Z0-9_-]+:.*?##/ { printf "  \033[36m%-20s\033[0m %s\n", $$1, $$2 } /^##@/ { printf "\n\033[1m%s\033[0m\n", substr($$0, 5) } ' $(MAKEFILE_LIST)

all: $(TARGET)  ## Build the test harness

$(TARGET): $(TEST_OBJS) $(LIB_OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

# Test source compilation
%.o: %.c test_harness.h
	$(CC) $(CFLAGS) -c -o $@ $<

# Library source compilation (from lib directory)
# Pattern rule: build .o from .c and matching .h (if exists) in LIB_DIR
# Assumes: .o and .c in same basename, .h optional, LIB_DIR already set
%.o: $(LIB_DIR)/%.c $(LIB_DIR)/%.h $(LIB_DIR)/lib.h
	$(CC) $(CFLAGS) -c -o $@ $<


clean:  ## Remove build artifacts
	rm -f $(TARGET) $(TEST_OBJS) $(LIB_OBJS)
	rm -f $(TARGET)-asan

##@ Test Targets

.PHONY: test test-asan test-valgrind

test: $(TARGET)  ## Run tests normally
	@echo "Running native memory tests..."
	@echo "Make sure the extension is built: cd ../.. && make build-ext"
	@echo ""
	./$(TARGET)

# ASan build - compile all sources with sanitizer flags
$(TARGET)-asan: CFLAGS += $(ASAN_CFLAGS)
$(TARGET)-asan: LDFLAGS += $(ASAN_LDFLAGS)
$(TARGET)-asan: $(TEST_SRCS) $(LIB_SRCS) test_harness.h
	$(CC) $(CFLAGS) -o $@ $(TEST_SRCS) $(LIB_SRCS) $(LDFLAGS)

test-asan: $(TARGET)-asan  ## Run tests with AddressSanitizer
	@echo "Running native memory tests with AddressSanitizer..."
	@echo "Make sure the extension is built: cd ../.. && make build-ext"
	@echo ""
	ASAN_OPTIONS=$(ASAN_OPTIONS) ./$(TARGET)-asan

test-valgrind: $(TARGET)  ## Run tests under Valgrind
	@echo "Running native memory tests under Valgrind..."
	@echo "Make sure the extension is built: cd ../.. && make build-ext"
	@echo ""
	@if [ ! -f valgrind-python.supp ]; then \
		echo "Warning: valgrind-python.supp not found, expect false positives"; \
	fi
	$(VALGRIND) $(VALGRIND_OPTS) ./$(TARGET)

##@ Development

.PHONY: rebuild check-python

rebuild: clean all  ## Clean and rebuild

check-python:  ## Show Python configuration being used
	@echo "Python: $(PYTHON)"
	@echo "Python config: $(PYTHON_CONFIG)"
	@echo "CFLAGS: $(CFLAGS)"
	@echo "LDFLAGS: $(LDFLAGS)"
