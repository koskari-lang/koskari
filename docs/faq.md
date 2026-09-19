# FAQ

## Is Koskari stable?

Koskari 0.1.0 is a beta, the first public release. The language surface
may change before 1.0. See
[Release and versioning](project/release-and-versioning.md).

## Which Python versions are supported?

Python 3.9 through 3.14 are tested in CI.

## Is free-threading supported?

Not yet, but eventually

## How do I report bugs or contribute?

Use the project issue tracker and follow `make test` / `make lint` before
submitting changes.


## Common problems

### `koskari: command not found`

The `koskari` command is only on your PATH after the package is installed:
`pip install koskari` from PyPI, or `pip install -e .` in a source checkout.
If you installed into a virtual environment, activate it first.

### Build fails: no C compiler found, or "koskari requires a compiler with musttail"

Koskari's evaluator threads opcode dispatch through guaranteed tail calls,
so the native extension needs clang, or GCC 15 or newer. The build picks
clang when it is on the PATH; set `CC` to choose explicitly. Install one for
your platform:

- **Debian/Ubuntu**: `sudo apt install clang lld`
- **Fedora/RHEL**: `sudo dnf install clang lld` (or GCC 15+ from a toolset)
- **macOS**: `xcode-select --install` (Apple clang)

### `tox: command not found`

Install tox into your environment: `pip install tox`. If you are using `uv`,
`uv pip install tox` or add it to your dev dependencies.
