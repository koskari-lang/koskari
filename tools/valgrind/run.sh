#!/bin/bash
# run.sh - valgrind wrapper for koskari memory analysis
#
# Invoked as the container ENTRYPOINT.  The caller passes the command
# to profile as positional arguments, e.g.:
#
#   koskari /work/test.ki
#   koslife
#   python3 /work/bench.py
#
# PYTHONMALLOC=malloc is mandatory: without it Python uses its own arena
# allocator for small objects and valgrind cannot see those allocations.
#
# author: Christopher O'Brien <obriencj@gmail.com>
# license: GPL v.3

set -uo pipefail

LOG=/work/valgrind.log

env PYTHONMALLOC=malloc \
    valgrind \
    --leak-check=full \
    --show-leak-kinds=definite,indirect \
    --track-origins=yes \
    --num-callers=32 \
    --suppressions=/valgrind/koskari.supp \
    --log-file="$LOG" \
    -- "$@"
STATUS=$?
cat "$LOG" >&2
exit "$STATUS"
