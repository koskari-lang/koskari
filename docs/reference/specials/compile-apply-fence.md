# compile/apply-fence
**Symbol:** `compile/apply-fence`

**Syntax:** `(compile/apply-fence NAME RAW META)` — normally produced by the
triple-backtick fence reader syntax rather than written by hand.

**Use-case:** Compile-time expansion of a fenced foreign literal. A fence
captures raw source in a declared foreign format (JSON, a bash script, ...)
and lowers it into ordinary Koskari source before compilation continues.

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
fences, and handlers returning non-source; `FenceError` (positioned into
the fence body where possible) for foreign-source parse errors and invalid
fence options.

**Built-in fences:**

- `fence/json` — extended JSON. Standard JSON lowered to `#dict` / `#list`
  builder source, with one narrow extension: a bare identifier in value
  position lowers to an ordinary Koskari name reference. Object keys remain
  quoted strings; `true`, `false`, and `null` always mean the JSON
  literals. No expressions, interpolation, or dotted member access. Accepts
  no options.

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

Fence expansions are self-contained: they reference only universal
builtins — `fence/json` lowers to the `#dict` / `#list` builders,
`fence/bash` to `run-process` — so a fence works without any
pre-existing helper bindings in the surrounding module's namespace.

**Trust model:** fence handlers are compile-time plugins; compiling a
module that uses a custom fence executes that fence's handler, exactly as
with other compile-time extensibility mechanisms.

**Examples:**

````text
;; JSON literal with a name reference in value position
define config ```json
{ "host": server-host, "port": 8080, "tls": true }
```

;; a runnable script value; each (report) launches the subprocess and
;; yields a ProcessResult
define report ```bash
curl -s https://example.org/api/summary/
```
define summary (.json (.raise_for_status (report)))

;; positional arguments become $1, $2, ...
define greet ```bash
echo "hello, $1"
```
define greeting (greet "world")
greeting.text                   ; "hello, world\n"

;; immediate: True runs the script where the fence sits; the fence
;; expression evaluates directly to the ProcessResult
define lookup ```bash immediate: True
hostname -f
```
define hostname lookup.text
````
