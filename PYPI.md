# <img src="https://koskari.org/_static/koskari-icon.svg" height="42" align="top" alt=""> Koskari

Koskari is a Lisp that lives inside Python: its modules import and are
imported like any other, its values are Python objects, and its programs run
on a bytecode evaluator written to run alongside CPython's own.

```koskari
def greet [name]:
  print f"Hello, {name}!"

@entry-point
def main []:
  greet "world"
```

```bash
koskari hello.ki
# Hello, world!
```

- **Layout or parentheses.** Indented blocks read as forms, so a program looks
  like Python; full s-expressions are there whenever you want them.
- **Python interop in both directions.** A `.ki` file sits beside `.py` files
  and imports through the ordinary import system; Python code imports it
  back and calls into it.
- **Hygienic macros**, `syntax-rules` style, with quasiquote and reader
  fences for foreign literals.
- **Async without the keywords.** The evaluator awaits awaitable values in
  place, so code that uses asyncio reads as straight-line code.
- **Control flow from Scheme.** Tail calls, delimited continuations, `call/cc`,
  dynamic wind and parameterize, alongside Python's exceptions and `with`.
- **Pattern matching** over Python objects and Koskari data.
- **A REPL, a disassembler, and cached compiled modules.**


## Install

```bash
pip install koskari
pip install "koskari[repl]"   # syntax-highlighted REPL input via prompt_toolkit
```

Development snapshots publish as pre-releases; `pip install --pre koskari`
selects the latest one. Koskari supports CPython 3.9 through 3.14 and ships
wheels for Linux and macOS.


## Links

- Documentation: <https://docs.koskari.org/>
- Quickstart: <https://docs.koskari.org/quickstart.html>
- Changelog: <https://docs.koskari.org/changelog.html>
- Source: <https://github.com/koskari-lang/koskari>
- License: GNU General Public License v3 or later
