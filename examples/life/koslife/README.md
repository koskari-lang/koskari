
WIP port of siblife. Declare **`packages`** and **`koskari_scripts`** in
**`setup.py`**; **`koskari.extras.setuptools`** adds **`*.ki`** /
**`*.kio`** **`package_data`** for declared packages during the build.

Local validation with **uv** (from **`examples/life/`**):

1. Ensure **koskari** is installable in the build env (**`pyproject.toml`**
   lists it under **`build-system.requires`**).
2. **`uv pip install -e .`** (or **`uv pip install .`**).
3. Inspect **`.dist-info/entry_points.txt`**: **`[koskari_scripts]`** plus
   mirrored **`koslife = koskari.launcher:dispatch`** under
   **`[console_scripts]`**.
4. Run **`koslife`** (or **`uv run koslife`**) and confirm the curses UI
   starts without the old Python **`asyncio.run`** shim.
