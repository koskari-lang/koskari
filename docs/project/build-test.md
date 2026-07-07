# Build and test

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
