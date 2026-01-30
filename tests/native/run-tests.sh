#!/bin/bash
#
# Run native memory tests
#
# This script is the entrypoint for the container-based test runner.
# It can also be run directly on a host with the required tools.
#
# Usage: run-tests.sh [all|normal|asan|valgrind]
#
# author: Christopher O'Brien <obriencj@gmail.com>
# license: GPL v.3

set -e

# Determine the directory containing this script
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

MODE="${1:-all}"
FAILED=0

# Colors for output (if terminal supports it)
if [ -t 1 ]; then
    GREEN='\033[0;32m'
    RED='\033[0;31m'
    YELLOW='\033[1;33m'
    NC='\033[0m' # No Color
else
    GREEN=''
    RED=''
    YELLOW=''
    NC=''
fi

log_info() {
    echo -e "${GREEN}===${NC} $1 ${GREEN}===${NC}"
}

log_warn() {
    echo -e "${YELLOW}WARNING:${NC} $1"
}

log_error() {
    echo -e "${RED}ERROR:${NC} $1"
}

run_normal() {
    log_info "Running normal tests"
    make clean >/dev/null 2>&1 || true
    if make test; then
        log_info "Normal tests: PASSED"
    else
        log_error "Normal tests: FAILED"
        FAILED=1
    fi
}

run_asan() {
    log_info "Running AddressSanitizer tests"

    # Check if ASan is available
    if ! gcc -fsanitize=address -x c -c /dev/null -o /dev/null 2>/dev/null; then
        log_warn "AddressSanitizer not available, skipping"
        return 0
    fi

    make clean >/dev/null 2>&1 || true
    if make test-asan; then
        log_info "ASan tests: PASSED"
    else
        log_error "ASan tests: FAILED"
        FAILED=1
    fi
}

run_valgrind() {
    log_info "Running Valgrind tests"

    # Check if Valgrind is available
    if ! command -v valgrind &>/dev/null; then
        log_warn "Valgrind not available, skipping"
        return 0
    fi

    make clean >/dev/null 2>&1 || true
    if make test-valgrind; then
        log_info "Valgrind tests: PASSED"
    else
        log_error "Valgrind tests: FAILED"
        FAILED=1
    fi
}

show_usage() {
    echo "Usage: $0 [MODE]"
    echo ""
    echo "Modes:"
    echo "  all       Run all test modes (default)"
    echo "  normal    Run tests without instrumentation"
    echo "  asan      Run tests with AddressSanitizer"
    echo "  valgrind  Run tests under Valgrind"
    echo ""
    echo "Examples:"
    echo "  $0              # Run all tests"
    echo "  $0 asan         # Run only ASan tests"
    echo "  $0 valgrind     # Run only Valgrind tests"
}

case "$MODE" in
    normal)
        run_normal
        ;;
    asan)
        run_asan
        ;;
    valgrind)
        run_valgrind
        ;;
    all)
        run_normal
        echo ""
        run_asan
        echo ""
        run_valgrind
        ;;
    -h|--help|help)
        show_usage
        exit 0
        ;;
    *)
        log_error "Unknown mode: $MODE"
        show_usage
        exit 1
        ;;
esac

echo ""
if [ $FAILED -eq 0 ]; then
    log_info "All requested tests completed successfully"
    exit 0
else
    log_error "Some tests failed"
    exit 1
fi


# The end.
