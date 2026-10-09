#!/usr/bin/env bash
# Run koskari tests in per-Python-version containers.
#
# Builds and runs a container for each supported Python version, with the
# repository mounted at /src.  Requires podman or docker.
#
# Usage: ./run.sh [CONTAINER_ENGINE] [VERSION] [EXTRA_RUN_ARGS...]
#   CONTAINER_ENGINE defaults to podman if available, else docker.
#   VERSION (optional): run only this version (e.g. 3.11 or py311).
#   EXTRA_RUN_ARGS (optional): extra args passed to podman run (e.g. -it, --network=host).

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
ENGINE="${1:-$(which podman 2>/dev/null || which docker 2>/dev/null || echo podman)}"
VERSION_FILTER="${2:-}"
# shift only what was given: shift 2 with one argument shifts nothing
# and would leave the engine as the container command
shift $(( $# < 2 ? $# : 2 ))

EXTRA_RUN_ARGS=("$@")
if (( ${#EXTRA_RUN_ARGS[@]} == 0 )); then
  EXTRA_RUN_ARGS=("test")
fi

VERSIONS="3.9 3.10 3.11 3.12 3.13 3.14"

for ver in $VERSIONS; do
  tox_env="py$(echo "$ver" | tr -d .)"
  if [[ -n "$VERSION_FILTER" ]]; then
    case "$VERSION_FILTER" in
      "$ver"|"$tox_env") ;;
      *) continue ;;
    esac
  fi
  tag="koskari-${tox_env}"

  echo "=== Building $tag (Python $ver) ==="
  "$ENGINE" build -q --build-arg PYTHON_VERSION="$ver" \
    -t "$tag" -f "$SCRIPT_DIR/Containerfile.pyver" "$SCRIPT_DIR"

  echo "=== Running tests for Python $ver ==="
  # echo "$ENGINE" run -t --rm -v "$REPO_ROOT:/src:z" "$tag" "${EXTRA_RUN_ARGS[@]}"
  "$ENGINE" run -it --rm -v "$REPO_ROOT:/src:z" "$tag" "${EXTRA_RUN_ARGS[@]}"
done


# The end.
