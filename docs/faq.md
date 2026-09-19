# FAQ

## What is Koskari for?

Writing programs in a Lisp that has Python's libraries, objects, and
runtime. It suits code that wants macros, first-class continuations, or
ambient async, and code that wants to sit inside an existing Python
project: a `.ki` module imports from Python and imports Python back.

## What is it not?

A replacement for Python, or a faster Python. Koskari runs its own
bytecode on its own evaluator inside CPython, and every value is a
Python object. Programs that are all library calls run at the speed of
those libraries; programs that are all Koskari run slower than the same
Python.

## Is it stable?

Koskari 0.1.0 is a beta and the first public release. The language
surface may change before 1.0, and the [changelog](changelog.md) records
what did. [Release and versioning](project/release-and-versioning.md)
says what kinds of change to expect.

## Which Python versions and platforms?

CPython 3.9 through 3.14, on Linux and macOS, with wheels on PyPI for
both. Windows is untested.

## Is free-threading supported?

Not yet.

## Can Python code call Koskari code?

Yes. After `import koskari`, a `.ki` module imports like any other, and
its functions and classes are ordinary Python callables. A function that
Python calls directly is defined with `define-demanding` so it returns a
value rather than a coroutine; see
[Interop](tutorials/from-python/interop.md).

## Where do I report a bug?

The issue tracker on GitHub. Include the Koskari version, the exact
command, the full output, and the smallest `.ki` file that shows it.
For a documentation example that does not run as shown, name the page
and the section.

## Common problems

### `koskari: command not found`

The command is on your PATH once the package is installed:
`pip install koskari` from PyPI, or `pip install -e .` in a checkout. A
virtual environment has to be activated first.

### Build fails: no C compiler, or "requires a compiler with musttail"

The evaluator threads opcode dispatch through guaranteed tail calls, so
building from source needs clang, or GCC 15 or newer. The build picks
clang when it is on the PATH; set `CC` to choose.

- Debian and Ubuntu: `sudo apt install clang lld`
- Fedora and RHEL: `sudo dnf install clang lld`, or GCC 15 from a toolset
- macOS: `xcode-select --install`

Installing the PyPI wheel needs no compiler.

### A Koskari function returns a coroutine when Python calls it

Every Koskari function can suspend, so a call from plain Python yields
a coroutine. Wrap the function with `define-demanding`, or `@demand`, at
its definition, or `demand` at the call. Python's `map`, `filter`,
`min`, `max`, and `list.sort` calling a Koskari lambda have the same
cause. See
[Demand, async, and defer](guide/demand-async-and-defer.md).

### `#{name: "x"}` raises `NameError`

Dict keys are expressions, so `name:` looks up a variable. Quote a
string key: `#{"name": "x"}`.

### `[1 2 3]` is not a list

Square brackets read like parentheses, so that is a call of `1`. A
list literal is `#[1 2 3]`.
