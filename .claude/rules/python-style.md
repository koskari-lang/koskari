# Python Style

## Structure

- Favor pure functions for stateless transformations. Introduce a class only
  for an object with mutable behavior or identity; never as a namespace for
  static helpers.
- Namespace packages stay empty (no `__init__.py`).
- `setup.cfg` is authoritative for package config, tox, flake8, and pytest
  settings. `setup.py` holds only a minimal `setup()` call. Poetry files and
  Black configuration are disallowed; do not add `pyproject.toml` content.
- Default to ASCII. Introduce non-ASCII only where a file already relies on
  it.
- Prefer `typing.Union` and related aliases over the `X | Y` syntax. Avoid
  `from __future__ import annotations` unless strictly required.

## File shape

- New modules open with the GPLv3 short-form comment block, a blank line,
  then a module docstring carrying the module name, a short description, and
  the fields `:author: Christopher O'Brien <obriencj@gmail.com>` and
  `:license: GNU General Public License v3`. Do not add per-file
  `:ai-assistant:` or similar attribution; AI-assisted development is
  disclosed once at the project level in
  [docs/project/provenance.md](../../docs/project/provenance.md).
- End every module with `# The end.` followed by a single newline.
- Two blank lines between top-level definitions and between methods.

## Formatting

- Multi-line function and method signatures double-indent continuation
  parameters (8 spaces) beneath the `def`, and close the paren on the last
  parameter line, immediately before the return annotation or colon.
- All other multi-line collections indent one level (4 spaces).
- Single-quoted strings for keyword-like values; double-quoted strings for
  English text.

```python
def long_signature(
        self,
        first_arg,
        second_arg,
        optional_one=None):
    """
    Summary phrase, no period
    """

    return first_arg + second_arg
```

## Docstrings

- Triple double-quoted, with the opening and closing `"""` on their own
  lines, and a blank line between the closing `"""` and the first statement.
- Plain prose, no backtick emphasis.
- A line ends with a period only when another sentence follows in the same
  paragraph. A single-segment docstring takes no trailing period.
- Use Sphinx RestructuredText fields when documenting arguments, return
  values, and expected raises.

```python
def multi_segment(a, b):
    """
    First sentence in the paragraph. Second sentence, same paragraph.

    :param a: the left operand
    :param b: the right operand
    :return: their sum
    """

    return a + b
```

Wrong on three counts (quotes not on their own lines, trailing period on a
single segment, no blank line before the body):

```python
def one_method(self, x):
    """Only one segment here."""
    return x * 2
```

## Koskari Pair Idioms

These apply whenever working with `pair` objects in Python code:

```python
# Destructure a pair — do NOT use car()/cdr()
head, tail = body

# Build a list — cons is variadic, do NOT nest
lst = cons(1, 2, 3, nil)   # correct
lst = cons(1, cons(2, cons(3, nil)))  # wrong
```

# The end.
