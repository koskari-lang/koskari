#!/usr/bin/env bash
# Run koskari tests in per-Python-version containers.
#
# Builds and runs a container for each supported Python version, with the
# repository mounted at /src.  Requires podman or docker.
#
# Usage: ./run.sh [CONTAINER_ENGINE] [VERSION]
#   CONTAINER_ENGINE defaults to podman if available, else docker.
#   VERSION (optional): run only this version (e.g. 3.11 or py311).

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
ENGINE="${1:-$(which podman 2>/dev/null || which docker 2>/dev/null || echo podman)}"
VERSION_FILTER="${2:-}"

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
    -t "$tag" -f "$SCRIPT_DIR/Containerfile" "$SCRIPT_DIR"

  echo "=== Running tests for Python $ver ($tox_env) ==="
  echo "$ENGINE" run --rm -v "$REPO_ROOT:/src:Z" "$tag" "$tox_env"
  "$ENGINE" run --rm -v "$REPO_ROOT:/src:Z" "$tag" "$tox_env"
done

echo "=== All version tests complete ==="
