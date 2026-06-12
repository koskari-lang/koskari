# FAQ

## Is Koskari stable?

Development status is **Pre-Alpha**. Breaking changes are allowed at this
stage.

## Which Python versions are supported?

Python 3.9 through 3.13 are tested in CI. Python 3.14 is listed as a target
classifier but is not yet released; compatibility is aspirational.

## Is free-threading supported?

Not yet, but eventually

## How do I report bugs or contribute?

Use the project issue tracker and follow `make test` / `make lint` before
submitting changes.


## Common problems

### `koskari: command not found`

The `koskari` entry point is only on your PATH after installing the package.
Run `make install` (system install) or `pip install -e .` (editable/development
install). If you used a virtual environment, make sure it is activated.

### Build fails: no C compiler found

Koskari requires a C compiler to build the native extension. Install one for your
platform:

- **Debian/Ubuntu**: `sudo apt install build-essential`
- **Fedora/RHEL**: `sudo dnf install gcc`
- **macOS**: `xcode-select --install`

### `tox: command not found`

Install tox into your environment: `pip install tox`. If you are using `uv`,
`uv pip install tox` or add it to your dev dependencies.
