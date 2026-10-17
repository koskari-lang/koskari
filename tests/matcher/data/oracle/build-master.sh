#!/bin/bash
# Build master's koskari into an isolated environment for the
# differential oracle.
#
# Exports master's tree with `git show master:<path>` only (no index,
# ref, worktree or stash change), then creates a uv venv inside the
# export and installs it editable, so the extension is built in place
# under $MASTER_BUILD (default tmp/matcher-core/master/).
#
# Usage: tests/matcher/data/oracle/build-master.sh [REF]
#   REF defaults to master. The resolved commit is written to
#   $MASTER_BUILD/.oracle-commit; rerunning re-exports and
#   rebuilds from scratch.
#
# Env: PYTHON (default python3.12), the interpreter uv builds the venv on.

set -euo pipefail

here=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
repo=$(cd "$here/../../../.." && pwd)
ref=${1:-master}
dest=${MASTER_BUILD:-$repo/tmp/matcher-core/master}
python=${PYTHON:-python3.12}

# The paths the build and the runtime need. docs/, design/, tests/,
# examples/ and tools/ are left out: setup.py skips the version regen
# without tools/regen_version.py and koskari falls back to 0+unknown.
paths=(koskari .eggs setup.py setup.cfg pyproject.toml MANIFEST.in koskari.pth
       README.md LICENSE)

export_path() {
    local p=$1
    local first
    # sed, not head: head closing the pipe early fails it under pipefail
    first=$(git -C "$repo" --no-pager show "$ref:$p" | sed -n 1p)
    if [ "$first" = "tree $ref:$p" ]; then
        mkdir -p "$dest/$p"
        git -C "$repo" --no-pager show "$ref:$p" | tail -n +3 |
        while IFS= read -r e; do
            [ -z "$e" ] && continue
            export_path "${p%/}/${e%/}"
        done
    else
        mkdir -p "$(dirname "$dest/$p")"
        git -C "$repo" --no-pager show "$ref:$p" > "$dest/$p"
    fi
}

rm -rf "$dest"
mkdir -p "$dest"

commit=$(git -C "$repo" --no-pager log -1 --format=%H "$ref")
echo "exporting $ref ($commit) to $dest"
for p in "${paths[@]}"; do
    export_path "$p"
done
echo "$commit" > "$dest/.oracle-commit"

cd "$dest"
echo "creating venv ($python)"
uv venv -q --python "$python" "$dest/.venv"
echo "installing master koskari (editable, builds the extension)"
VIRTUAL_ENV="$dest/.venv" uv pip install -q \
    --python "$dest/.venv/bin/python" -e "$dest"

echo "checking import"
"$dest/.venv/bin/python" -I -c \
    'import koskari, koskari.lib, koskari.matcher as m; print("ok", koskari.__file__, hasattr(m, "make_matcher"))'

echo "exported files: $(find "$dest" -path "$dest/.venv" -prune -o -type f -print | wc -l)"
echo "done: run files with $here/run-master.sh FILE [ARGS...]"
