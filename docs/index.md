# Koskari documentation

Koskari is a Lisp that runs on its own stackless evaluator inside CPython.
A `.ki` source file combines an indentation layout with s-expressions,
expands through macros, and compiles to bytecode that runs beside ordinary
Python. Any Python module imports, any Python class can be subclassed, and
objects pass in both directions.

## Where to start

**[Quickstart](quickstart.md)**: install, open the REPL, run a file, and
read one small program.

**[Tutorials](tutorials/index.md)**: two tracks, each read in order.
[Little Koskari](tutorials/beginner/index.md) assumes no programming
background. [From Python](tutorials/from-python/index.md) puts Python and
Koskari side by side and ends with worked examples.

**[Guide](guide/index.md)**: one page per mechanism, how it works and why:
layout, name resolution, bindings and refs, functions, macros, async,
modules, pragmas, hosting Koskari from Python, and packaging. Read a page
when its topic comes up.

**[Reference](reference/index.md)**: the catalogues. Grammar and layout
rules, every special form, macro, layout handler, builtin, and opcode, the
command line, the error classes, and the Python API.

**Project**: [building and testing](project/build-test.md) from a source
checkout, the [architecture](project/architecture-map.md), how the project
is [developed](project/provenance.md), [versioning](project/release-and-versioning.md),
the [example corpus](corpus/index.md), and the [changelog](changelog.md).

```{toctree}
:maxdepth: 1
:caption: Start

quickstart
faq
```

```{toctree}
:maxdepth: 1
:caption: Tutorials

tutorials/index
tutorials/beginner/index
tutorials/from-python/index
```

```{toctree}
:maxdepth: 1
:caption: Guide

guide/index
```

```{toctree}
:maxdepth: 1
:caption: Reference

reference/index
```

```{toctree}
:maxdepth: 1
:caption: Project

project/build-test
project/architecture-map
project/provenance
project/release-and-versioning
corpus/index
changelog
```
