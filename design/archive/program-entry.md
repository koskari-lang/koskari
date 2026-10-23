# Program entry

A program names one zero-arity function as its entry. The launcher calls
it after the module has loaded and sealed, and its return value is the
exit status. Code at module level is for definitions; the program runs in
the entry. This replaces the current pair of `when-main` (load-time code)
and `declare-entry` (a name recorded as a string, looked up later).

Status: implemented 2026-10-22 on the `new-arena` branch, in the order
of work below; archived. `declare-entry` and the string slot are gone.

## The slot

`__entry__` in a module's namespace holds the entry function itself, a
callable, never a name. The launcher calls it with no arguments and maps
the return value as `SystemExit` maps its code: None is 0, an int is the
status, anything else is printed to stderr with status 1. A `SystemExit`
raised inside the entry propagates, so `sys.exit` keeps working.
`materialize_exports` already leaves `__entry__` out of `__all__`.

The slot is the contract. Everything below is a spelling that writes it,
as `declare-exports` writes `__all__`.

## How a program writes it

The taught spelling is a decorator over the function:

```koskari
@entry-point
define main [args: (item-slice sys.argv 1)]:
  if args:
    print "hello," args[0]
  else:
    print "usage: tool NAME"
    2
```

`main` stays a public, importable, testable function, and `__entry__`
holds the same object. The decorator is a `compile/decorate-subject` arm,
not a runtime function: a runtime decorator has no clean way to find the
calling module to write the slot, while an arm expands to a global store,
which also serializes into a `.kio` with nothing extra.

```koskari
extend-syntax compile/decorate-subject:
  syntax-rules [entry-point]:
    case (decorate-subject entry-point subject):
      (let [[fn subject]] (define-global __entry__ fn) fn)
```

The name hoist in `compile/decorate` turns `@entry-point` over `define main`
into `(define main (decorate [entry-point] (function main ...)))`, so the
arm sees the function value and returns it for `main` to bind. With the
`reader/decorators` pragma off, `(decorate [entry-point] (define main ...))`
is the same thing in parentheses.

The explicit spelling is the slot itself:

```koskari
define __entry__ []:
  ...
```

It is documented once, on the reference page for the slot, and not taught.

Naming: arm literals match symbolically regardless of binding, so a short
decorator name is hijacked in any module that binds that word itself.
`@entry` over a definition in a module with its own `entry` function would
fire the arm. `entry-point` is safe, and it is Python's own packaging
term, two words, as in `[project.entry-points]`. `@main` has the problem
in its worst form, since `main` is the conventional function name.

Not renamed to `__main__`: `koskari -m pkg` imports `pkg.__main__` and
binds it as an attribute on the package, so a package `__init__.ki` could
never hold an entry under that name; `import __main__` is the Python idiom
for the running program module; and the docs would use `__main__` in two
senses on one page.

### when-main

`when-main` stays as a macro, since it is Python's guard and the
serializer's synthetic-name rule guards against any `__name__` check, not
this macro in particular. It leaves every tool, example and tutorial. The
docs mention it once, on the entry page, as the load-time alternative.
With the entry a function, the "when-main runs first, then the entry"
ordering rule and its paragraph go away.

Migration of the in-tree bodies, by shape:

| Body today | After |
|---|---|
| `(sys.exit (main))` | `@entry-point` over `main`; the status is the return value |
| `(main)` with no status | the same; None maps to 0 |
| an inline program body | the body becomes `main` |

Twelve tools and examples carry the first two shapes; `matching.ki`,
`exception_winding.ki` and the from-python tutorial examples carry the
third.

## What goes away

The string form of `__entry__`, the `NameError` for a declared name that
is never bound, the forward-declaration rule, `declare-entry` itself, and
the docs paragraph ordering `when-main` against the entry. `entry_status`
and the launcher's single call site stay. A non-callable in the slot fails
at the call with `TypeError`, which is the right error.

## Loading an entry

Three hosts call entries, and after this change they share one contract:
a zero-arity callable whose return value is the status.

### koskari (script, -c, stdin)

Unchanged in shape. `run` loads the program as `__main__`, the load
completes and seals, `run_entry` reads `__entry__` and calls it. With no
slot the program exits 0. Under `-i` the entry runs and the session
starts once it returns.

### koskari -m

`main_module_spec` resolves the name through the import machinery and
runs the module, or a package's `__main__` submodule, as `__main__` with
`__package__` set. Unchanged for those two cases.

New: a package with no `__main__` submodule whose `__init__.ki` declares
an entry is runnable. The launcher imports the package under its real
name, as any import would, and calls its `__entry__`. It does not re-run
`__init__.ki` as `__main__`: that would set `__package__` to the parent
and break the package's own relative imports, and it would load the
package twice if anything else imported it. A package with neither a
`__main__` nor an entry keeps today's error.

### koskari_scripts entry points

The installed stub runs `exit(run_resolved_script(spec, name))`, and
`hosted_run` imports the module, calls the named attribute with no
arguments, awaits the result when it is awaitable, and returns it to
`sys.exit`. That is already the entry contract. `koslife.cli:main` with
its `[argv: None]` default fits as it stands; dropping its `when-main`
leaves the decorator line to add.

New: the value `koslife.cli` with no `:callable` means the module's
`__entry__`. Today that form is specified as "script module, `when-main`
runs", the one load-time row left in the entry-point table. A module
without an entry is a packaging error; `hosted_run` raises when the slot
is missing rather than exiting 0, since an installed command that does
nothing is a defect, not a program with no entry.

The `koskari` command itself boots through `hosted_run` into
`koskari.cli.launch:run`, so the CLI's own entry is the same kind of
function.

One difference to close before the docs say the three agree: `hosted_run`
awaits an awaitable result explicitly, while `run_entry` writes `(entry)`
and relies on the evaluator. The two agree for a plain function, which is
every case today. A test that a `define-coroutine` entry gives the same
status through both is needed before the claim is made.

## python -m

Today `KoskariSourceFileLoader.get_code` raises `ImportError` pointing at
`koskari -m`, and `python -m mod` prints that and exits 1. It can run the
program instead.

How `python -m` works, from `runpy._run_module_as_main`: it finds the
spec through `sys.meta_path`, recurses into `pkg.__main__` for a package,
asks `spec.loader.get_code(name)` for a code object, sets `sys.argv[0]` to
the spec's origin, and executes the code object in the real `__main__`
module's dict with `__name__` set to `"__main__"`, `__spec__` to the found
spec, `__package__` to its parent and `__file__` to the origin. Nothing
requires the code object to be the module's own body; it only has to be a
Python code object.

So `get_code` returns the compiled form of a fixed shim:

```python
from koskari.launcher import run_main_module
run_main_module(__spec__, __name__)
```

and `run_main_module` does what `koskari -m` does from the point where
the spec is in hand: pick the file (the origin, or the sibling source when
a `.kio` is stale), build a koskari `Module` named `__name__` with the
spec's parent as package, bind the spec, publish it in `sys.modules`
under that name, load it under `Domain` and the async host, and when the
name is `__main__`, call `run_entry` and `sys.exit` with the status.

Why a fresh `Module` and not the interpreter's `__main__`: a koskari
module's `__dict__` is a `ModuleNamespace`, and the interpreter's
`__main__` is a plain module with a plain dict. `runpy` captured that
dict before the exec, so replacing `sys.modules["__main__"]` is safe, and
`import __main__` and pickling then see the program module. The old
`__main__` held only the dunders `runpy` set.

What lines up without work: `sys.argv[0]` is the origin, as `koskari -m`
sets it; `sys.path[0]` is the working directory under both; a package's
`__init__.ki` is imported through the ordinary loader before `runpy`
looks for `pkg.__main__`; `python -i -m mod` lands in Python's prompt
with the koskari module as `__main__`, so its names are inspectable
there.

Guarding the shim: `get_code` is also reachable from `runpy.run_module`,
which executes the code in a dict of its own under a caller-chosen name.
The shim takes its name from `__name__` and runs the entry only when it
is `__main__`, so `run_module("pkg.mod")` loads the koskari module under
that name and returns, as it would for Python source. Ordinary imports
never reach `get_code`: the loader overrides `exec_module`.

Precondition: the finder must be on `sys.meta_path` before `runpy` runs,
which `koskari.pth` guarantees in an installed environment through its
`import koskari` at interpreter start. An editable install made with `uv`
does not carry the `.pth`, so in the dev venv `python -m mod` reports no
module; `python -c "import koskari, runpy; runpy._run_module_as_main(...)"`
is the probe there.

Not covered by `python -m`: the package-entry fallback. `runpy` raises
its own error for a package without `__main__` before any koskari code
runs; the only hook would be a finder that synthesizes a `pkg.__main__`
spec from `__init__.ki`, which would also make an entry-less package
"run" and exit 0 instead of erroring as Python does. `koskari -m` gets
the fallback; `python -m` keeps Python's rule.

## Order of work

1. The `decorate-subject` arm, `__entry__` as a callable, `run_entry`
   reading it directly; drop `declare-entry`; CLI tests retargeted.
2. The coroutine-entry test across `run_entry` and `hosted_run`.
3. `koskari -m` package-entry fallback; `koskari_scripts` module-only
   values.
4. `get_code` shim and `run_main_module`.
5. Tools, examples, tutorials and reference pages off `when-main`.

See also [kio-setuptools-build.md](interpreter/kio-setuptools-build.md)
and the archived
[koskari-scripts-packaging.md](archive/koskari-scripts-packaging.md).
