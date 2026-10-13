# fence/bash
**Symbol:** `fence/bash`

**Use-case:** Capture a bash script verbatim and run it asynchronously,
getting back a result object. The fence body is the script; the surrounding
Koskari code launches it and inspects the outcome.

**Behaviour:** By default the fence evaluates to an async **callable**.
Invoking it runs the script through `bash -c` and returns a
[`ProcessResult`](#processresult). Positional arguments passed to the callable
become the script's positional parameters `$1`, `$2`, ... (and so `$@`), each
coerced to a string. With the `immediate: True` option there is no callable:
the fence expands to the run call in place, so the fence expression itself
evaluates to the `ProcessResult`.

A nonzero exit status is **reported** on the result, never raised.

**Syntax:**

````text
```bash [immediate: True]
SCRIPT LINE
...
```
````

The only accepted option is `immediate:` (a `True`/`False` literal); any other
opener-line option raises `FenceError`.

**Examples:**

```koskari
>>> var rev ```bash
... echo -n "$@" | rev
... ```
>>> var ohai (rev "Hello" 123 "World")
>>> ohai
<ProcessResult 'bash' returncode=0>
>>> ohai.text
"dlroW 321 olleH"
```

````koskari
;; immediate: True -- the fence is the ProcessResult, no callable
var host ```bash immediate: True
hostname -f
```
var name host.text
````

## ProcessResult

`ProcessResult` (from `koskari.asyncio`) is the captured outcome of a finished
process -- the subprocess analogue of an HTTP client's Response: capture
everything, interpret afterwards.

| Member | Meaning |
|--------|---------|
| `args` | the argv tuple that was run |
| `returncode` | the process exit status |
| `stdout` / `stderr` | raw captured bytes |
| `text` / `stderr_text` | stdout / stderr decoded as UTF-8 |
| `(.json r)` | stdout parsed as one JSON document |
| `(.jsonl r)` | stdout parsed as JSON Lines (one document per non-blank line) |
| `(.yaml r)` | stdout parsed as one YAML document (requires PyYAML) |
| `(.raise_for_status r)` | raise `CalledProcessError` on nonzero exit; return `r` on success |

Because `raise_for_status` returns the result, strict-and-parsed reads chain:

```koskari
var summary (.json (.raise_for_status (report)))
```

## Implementation

`fence/bash` is defined in Koskari, in `koskari/bootstrap/_fences.ki`, through
the `define-fence` syntax -- it is not Python code. The handler receives the
raw script text and the metadata dict and returns Koskari source:

```koskari
define-fence fence/bash [raw meta]:
  var options (meta.get "options")
  var exec_now (options.pop "immediate" False)

  when options:
    var names (.join " " (sorted options))
    raise (FenceError f"fence/bash got unknown options: {names}")

  switch exec_now:
    case [True]:
      `(run-process "bash" "-c" ,raw)

    case [False]:
      `function <fence/bash> [*: args]:
        set! args (map str args)
        run-process "bash" "-c" ,raw "bash-fence" *: args

    else:
      raise (FenceError f"fence/bash got unknown exec_now: {exec_now}")
```

Notes:

- **`immediate:` is matched as a literal.** The `switch` compares the source
  value against the `True` / `False` literals, not for truthiness, so only an
  explicit `immediate: True` selects in-place mode; anything else is an error
  rather than a silently-truthy match.
- **`run-process` is a universal builtin** (`koskari.asyncio`), so the
  expansion needs no import at the use site. It wraps
  `asyncio.create_subprocess_exec` plus `communicate` into one awaited call and
  returns a `ProcessResult`.
- **Positional arguments start at `$1`.** The generated callable passes
  `"bash-fence"` as the first word after the script, which bash assigns to
  `$0`; the caller's arguments therefore fill `$1`, `$2`, ... The `(map str
  args)` coercion means non-string arguments (e.g. the `123` above) reach the
  script as their string form.
- **No shell injection surface.** Arguments are passed as discrete `argv`
  entries to `create_subprocess_exec` (an `execve`, not a shell string), so
  they are never re-tokenized by a shell -- the safe, `shell=False`-equivalent
  path.

**Runtime:** `koskari.asyncio` (`run-process`, `ProcessResult`).
