# FAQ

## Is Koskari stable?

No. Development status is **Pre-Alpha**. Expect breaking changes.

## Which Python versions are supported?

Python 3.9 through 3.14 (see `setup.cfg` classifiers). CI exercises multiple
versions via containerized tox runs.

## How do I report bugs or contribute?

Use the project issue tracker and follow `make test` / `make lint` before
submitting changes.

## Why does `koskari.builtins` look odd in API docs?

`koskari.builtins` is a virtual module assembled at import time from compiled
bootstrap sources. The API reference documents its exported names; the bootstrap
loader itself is intentionally not documented here.
