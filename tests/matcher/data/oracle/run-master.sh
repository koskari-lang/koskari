#!/bin/bash
# Run a Python (.py) or Koskari (.ki) file under the master build made
# by build-master.sh.
#
# Usage: tests/matcher/data/oracle/run-master.sh FILE [ARGS...]
#
# The interpreter is master's venv python with -I (isolated: no
# PYTHONPATH, no user site, no script directory on sys.path), so nothing
# from the branch's tree is importable. The file's own directory is not
# added either; a probe that imports a sibling module (e.g. common.py)
# needs MASTER_PATH=<dir> to put that directory on sys.path explicitly.
# The working directory is the master export, so relative paths in the
# probe resolve against it; FILE is resolved to an absolute path first.

set -euo pipefail

here=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
repo=$(cd "$here/../../../.." && pwd)
dest=${MASTER_BUILD:-$repo/tmp/matcher-core/master}
py="$dest/.venv/bin/python"

if [ ! -x "$py" ]; then
    echo "no master build at $dest; run $here/build-master.sh" >&2
    exit 2
fi

[ $# -ge 1 ] || { echo "usage: $0 FILE [ARGS...]" >&2; exit 2; }
file=$(readlink -f "$1"); shift

cd "$dest"
case "$file" in
    *.ki)
        exec "$py" -I -m koskari "$file" "$@" ;;
    *)
        if [ -n "${MASTER_PATH:-}" ]; then
            exec "$py" -I -c '
import runpy, sys
sys.path[:0] = sys.argv[1].split(":")
path = sys.argv[2]
sys.argv = sys.argv[2:]
runpy.run_path(path, run_name="__main__")
' "$MASTER_PATH" "$file" "$@"
        fi
        exec "$py" -I "$file" "$@" ;;
esac
