# tests.mk - native memory test build rules
#
# Build and run the C test harness that verifies memory management
# in Koskari's native types. Intended for use inside the container
# or directly; invoke with make -f tests.mk <target>.
#
# Invoke from the top level using:
#   make test-native
#   make test-asan
#   make test-valgrind


# Python configuration
PYTHON ?= python3
PYTHON_CONFIG ?= $(PYTHON)-config

# Compiler
CC ?= gcc

# Test and library sources discovered via globs (stay in sync with tree)
LIB_DIR := ../../koskari/lib
TEST_SRCS := $(wildcard test_*.c)
LIB_SRCS := $(wildcard $(LIB_DIR)/*.c)

# Build output directory (empty = current dir; set e.g. BUILD_DIR=/build in container)
BUILD_DIR ?=
OBJDIR := $(or $(BUILD_DIR),.)
VPATH := . $(LIB_DIR)

# All sources
SRCS := $(TEST_SRCS) $(LIB_SRCS)

# Object files (under OBJDIR when BUILD_DIR is set)
TEST_OBJS := $(addprefix $(OBJDIR)/, $(notdir $(TEST_SRCS:.c=.o)))
LIB_OBJS := $(addprefix $(OBJDIR)/, $(notdir $(LIB_SRCS:.c=.o)))
OBJS := $(TEST_OBJS) $(LIB_OBJS)

# Output binary
TARGET := $(OBJDIR)/test_harness

# Compiler flags
PYTHON_CFLAGS := $(shell $(PYTHON_CONFIG) --cflags 2>/dev/null || echo "-I/usr/include/python3")
PYTHON_LDFLAGS := $(shell $(PYTHON_CONFIG) --ldflags --embed 2>/dev/null || $(PYTHON_CONFIG) --ldflags 2>/dev/null)

CFLAGS := -std=c99 -Wall -Werror -g
CFLAGS += $(PYTHON_CFLAGS)
CFLAGS += -I$(LIB_DIR)

# Optional tracing flags (e.g. make KOS_TRACE_FREELIST=1)
ifneq ($(KOS_TRACE_FREELIST),)
CFLAGS += -DKOS_TRACE_FREELIST=$(KOS_TRACE_FREELIST)
endif
ifneq ($(KOS_TRACE_EVENTS),)
CFLAGS += -DKOS_TRACE_EVENTS=$(KOS_TRACE_EVENTS)
endif
ifneq ($(KOS_TRACE_TIMING),)
CFLAGS += -DKOS_TRACE_TIMING=$(KOS_TRACE_TIMING)
endif
ifneq ($(KOS_TRACE_EVAL_SYNC),)
CFLAGS += -DKOS_TRACE_EVAL_SYNC=$(KOS_TRACE_EVAL_SYNC)
endif
ifneq ($(VERBOSE_DEBUG),)
CFLAGS += -DVERBOSE_DEBUG=$(VERBOSE_DEBUG)
endif

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
                 --show-leak-kinds=definite,indirect,possible \
                 --errors-for-leak-kinds=none \
                 --track-origins=yes \
                 --error-exitcode=1 \
                 --num-callers=64 \
                 --suppressions=valgrind-python.supp

##@ Build Targets

.PHONY: all build clean help normal asan valgrind

.DEFAULT_GOAL := help

help:  ## Display this help
	@awk 'BEGIN {FS = ":.*##"; printf "\nUsage:\n  make \033[36m<target>\033[0m\n"} /^[a-zA-Z0-9_-]+:.*?##/ { printf "  \033[36m%-20s\033[0m %s\n", $$1, $$2 } /^##@/ { printf "\n\033[1m%s\033[0m\n", substr($$0, 5) } ' $(MAKEFILE_LIST)

build: $(TARGET)  ## Build the test harness

normal: test  ## Run tests normally (default for container)
asan: test-asan  ## Run tests with AddressSanitizer
valgrind: test-valgrind  ## Run tests under Valgrind
all: normal asan valgrind  ## Run all test modes

$(OBJDIR):
	mkdir -p $@

$(TARGET): $(TEST_OBJS) $(LIB_OBJS) | $(OBJDIR)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

# Test source compilation
$(OBJDIR)/%.o: %.c test_harness.h | $(OBJDIR)
	$(CC) $(CFLAGS) -c -o $@ $<

# Library source compilation (from lib directory)
# Pattern rule: build .o from .c and matching .h (if exists) in LIB_DIR
# Assumes: .o and .c in same basename, .h optional, LIB_DIR already set
$(OBJDIR)/%.o: $(LIB_DIR)/%.c $(LIB_DIR)/%.h $(LIB_DIR)/lib.h | $(OBJDIR)
	$(CC) $(CFLAGS) -c -o $@ $<


clean:  ## Remove build artifacts
	rm -f $(TARGET) $(TEST_OBJS) $(LIB_OBJS)
	rm -f $(TARGET)-asan

##@ Test Targets

.PHONY: test test-asan test-valgrind

test: $(TARGET)  ## Run tests normally
	@echo "Running native memory tests..."
	@echo ""
	$(TARGET)

# ASan build - compile all sources with sanitizer flags
$(TARGET)-asan: CFLAGS += $(ASAN_CFLAGS)
$(TARGET)-asan: LDFLAGS += $(ASAN_LDFLAGS)
$(TARGET)-asan: $(TEST_SRCS) $(LIB_SRCS) test_harness.h | $(OBJDIR)
	$(CC) $(CFLAGS) -o $@ $(TEST_SRCS) $(LIB_SRCS) $(LDFLAGS)

test-asan: $(TARGET)-asan  ## Run tests with AddressSanitizer
	@echo "Running native memory tests with AddressSanitizer..."
	@echo ""
	ASAN_OPTIONS=$(ASAN_OPTIONS) $(TARGET)-asan

test-valgrind: $(TARGET)  ## Run tests under Valgrind
	@echo "Running native memory tests under Valgrind..."
	@echo ""
	@if [ ! -f valgrind-python.supp ]; then \
		echo "Warning: valgrind-python.supp not found, expect false positives"; \
	fi
	PYTHONMALLOC=malloc $(VALGRIND) $(VALGRIND_OPTS) $(TARGET)

##@ Development

.PHONY: rebuild check-python

rebuild: clean build  ## Clean and rebuild

check-python:  ## Show Python configuration being used
	@echo "Python: $(PYTHON)"
	@echo "Python config: $(PYTHON_CONFIG)"
	@echo "CFLAGS: $(CFLAGS)"
	@echo "LDFLAGS: $(LDFLAGS)"


# The end.
