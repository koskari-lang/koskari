# Build and test

## Building from source

Prerequisites:

- Python 3.9 or newer (CPython)
- A C compiler with musttail support for the native extension: clang, or
  GCC 15 or newer. The build picks clang when it is on the PATH; set `CC`
  to choose.
- [tox](https://tox.wiki/) for the tests and the documentation

From the repository root, `make build` compiles the native extension and
produces a wheel under `dist/`, and `make install` installs it. For an
editable install:

```bash
pip install -e ".[repl]"
```

## Make targets

Run `make help` for the full list. Common targets:

| Target | Purpose |
|--------|---------|
| `make build` | Build wheel (native ext + uv) |
| `make test` | Run tox test suite (phased pytest) |
| `make lint` | flake8 on `koskari/` |
| `make docs` | Build this documentation site |
| `make clean` | Remove build artifacts |

## Tests via tox

All Python tests run through tox:

```bash
tox -qe test
```

Run a single file:

```bash
tox -qe py -- tests/reader/test_layout.py
```

Tests are ordered by **phase markers** (lib, infra, compile, specials,
importer, basics, cli, reproducers). See `setup.cfg` `[tool:pytest]`.

## Native memory tests

Containerized C harness tests (optional):

```bash
make test-native
make test-asan
make test-valgrind
```

## Documentation build

```bash
make docs
```

Output: `build/docs/` (dirhtml).
