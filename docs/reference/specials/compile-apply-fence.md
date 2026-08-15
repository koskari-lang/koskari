# compile/apply-fence
**Symbol:** `compile/apply-fence`

**Syntax:** `(apply-fence NAME RAW META)` — normally produced by the
triple-backtick fence reader syntax rather than written by hand.

**Use-case:** Compile-time expansion of a fenced foreign literal. A fence
captures raw source in a declared foreign format — a bash script, and in
principle any notation awkward to write as s-expressions — and lowers it
into ordinary Koskari source before compilation continues.

**Reader shorthand:**

````text
```NAME OPTION: VALUE ...
raw fence body
```
````

The fence name is the first word after the opening backticks (whitespace
optional, so ```` ```bash ```` and ```` ``` bash ```` are equivalent). The
rest of the opener line is read as keyword/value option pairs; each option
requires a value on the same line. The body is captured as raw text,
verbatim, until a line containing only three backticks closes the fence.
Body lines are dedented by the closing line's indentation, so an indented
fence does not leak its indentation into the payload.

Fence reading is governed by the `reader/fences` pragma (default on);
`(pragma [reader/fences False])` restores backtick's plain quasiquote
readings for the module.

A fence reads as a single value, so it may appear in expression position:

````text
define script ```bash
cd foo/bar/baz
curl -s https://example.org/api/data/
```
(script)
````

**Behaviour:** `NAME` and `RAW` are required; `META` is an optional keyword
plist. The special:

1. resolves the handler `fence/NAME` in the module namespace
2. calls it as `(handler RAW META-DICT)` where `META-DICT` is a dict with
   `name`, `filename`, `line`, `col`, and `options` entries
3. requires the handler to return valid Koskari source
4. compiles the returned source in place of the fence

`META` carries `filename:`, `line:` and `col:` (the payload's origin line
and dedent base, used to remap diagnostics into the fence body), and
`options:` (the opener-line pairs as a nested keyword plist).

**Handler resolution:** a bare-name binding resolves only when it is a
`FenceHandler` (the wrapper used by built-in fences); a `fence/`-prefixed
binding resolves when it is a `FenceHandler` or any callable. A module can
therefore define its own fence by binding a two-argument function to
`fence/NAME`. Handlers run at compile time and must not suspend; a
suspending Koskari handler raises `KoskariSyncViolation`.

**Errors:** `SpecialError` for malformed `apply-fence` forms, unknown
fences, and handlers returning non-source; `FenceError` for invalid fence
options and foreign-source parse errors. A handler may attach a source
position (see the `line`/`col` entries in `META-DICT`) so its `FenceError`
points into the fence body.

**Built-in fences:**

- `fence/bash` — bash script capture, built on the `run-process`
  builtin. By default the fence evaluates to an async **callable**:
  invoking it runs the script via `bash -c`, and any positional
  arguments passed to the callable are forwarded as the script's
  positional parameters — `(script "alpha" "beta")` makes `$1` be
  `alpha` and `$2` be `beta`, with `$@` behaving accordingly. With the
  `immediate: True` option there is no callable at all: the fence
  expands to the `run-process` call in place, so the fence expression
  itself evaluates to the finished result.

  Either way a run produces a `ProcessResult`, the subprocess analogue
  of an HTTP client's Response: it carries `args`, `returncode`, and
  the raw `stdout` / `stderr` bytes, with `text` / `stderr_text` for
  decoded output and `(.json result)`, `(.jsonl result)`, and
  `(.yaml result)` (requires PyYAML) for parsed stdout. A nonzero exit
  status is **reported, never raised** — call `(.raise_for_status
  result)` to raise `subprocess.CalledProcessError` instead; it returns
  the result on success, so strict-and-parsed chains as
  `(.json (.raise_for_status (script)))`.

Built-in fences are defined in Koskari (`koskari/bootstrap/_fences.ki`,
via the `define-fence` syntax). Fence expansions are self-contained:
they reference only universal builtins (`fence/bash` lowers to
`run-process`), so a fence works without any pre-existing helper
bindings in the surrounding module's namespace.

**Trust model:** fence handlers are compile-time plugins; compiling a
module that uses a custom fence executes that fence's handler, exactly as
with other compile-time extensibility mechanisms.

**Examples:**

Positional arguments passed to the callable become the script's `$1`,
`$2`, ... (and so `$@`), each coerced to a string; the call returns a
`ProcessResult` whose `text` is the decoded stdout:

````text
>>> define rev ```bash
... echo -n "$@" | rev
... ```
>>> define ohai (rev "Hello" 123 "World")
>>> ohai
<ProcessResult 'bash' returncode=0>
>>> ohai.text
"dlroW 321 olleH"
````

A script that returns JSON, parsed and status-checked in one chain; and
`immediate: True`, which runs the script where the fence sits so the fence
expression evaluates directly to the result:

````text
;; each (report) launches the subprocess and yields a ProcessResult
define report ```bash
curl -s https://example.org/api/summary/
```
define summary (.json (.raise_for_status (report)))

;; immediate: True — no callable; the fence is the ProcessResult
define lookup ```bash immediate: True
hostname -f
```
define hostname lookup.text
````
