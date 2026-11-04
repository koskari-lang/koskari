#!/usr/bin/env bash
# Entry point of the Pyodide container: cross-compile the wheel from the
# repository mounted at /src, then deliver it or test it.
#
# Usage: koskari-pyodide [wheel|test] [PYTEST_ARGS...]
#   wheel: copy the wheel to dist/pyodide in the repository.
#   test: run pytest under Pyodide in node, on tests/ by default.

set -e

mode="${1:-test}"
shift || true

source "$PYODIDE_XBUILDENV_PATH"/*/emsdk/emsdk_env.sh > /dev/null

# build from a copy, so .pyodide_build and build objects stay out of the
# repository
mkdir -p /build
tar -C /src --exclude=.git --exclude=.tox --exclude=.venv \
    --exclude=build --exclude=dist -cf - . | tar -C /build -xf -
cd /build
pyodide build -o /build/wheelhouse .

case "$mode" in
  wheel)
    mkdir -p /src/dist/pyodide
    cp /build/wheelhouse/*.whl /src/dist/pyodide/
    ;;
  test)
    pyodide venv /build/pyodide-venv
    wheel=$(echo /build/wheelhouse/*.whl)
    /build/pyodide-venv/bin/pip install pytest PyYAML jsonschema setuptools \
        "$wheel[repl,lsp,pygments,sphinx]"
    /build/pyodide-venv/bin/python -m pytest "${@:-tests/}"
    ;;
  *)
    echo "usage: koskari-pyodide wheel|test [pytest args]" >&2
    exit 2
    ;;
esac


# The end.
