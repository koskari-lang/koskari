# Control Flow

## Conditionals

### From python-sibilant

**`cond`**: Multi-way conditional
- Syntax: `(cond (TEST BODY...) ... (else: BODY...))`
- Evaluates tests in order
- Returns value from first matching body
- `else:` keyword is always true
- Returns `None` if no match and no else

**`if`**: Not present as special form (use `cond`)

## Loops

### From python-sibilant

**`while`**: Test-based loop
- Syntax: `(while TEST BODY...)`
- Evaluates TEST, if truthy executes BODY and repeats
- Supports `break` and `continue` with optional values
- Result: value from `break`, most recent `continue`, or final BODY expression
- If loop never executes, result is `None`

**`for-each`**: Iteration loop
- Syntax: `(for-each (BINDING ITEREXPR) BODY...)`
- Alternative: `(for-each (ITEREXPR as: BINDING) BODY...)`
- Iterates over ITEREXPR
- Supports `break` and `continue` with optional values
- Result: value from `break`, most recent `continue`, or `None` if loop completes

### From sibilant-ceval

**`await-for-each`**: Async iteration
- Syntax: `(await-for-each (BINDING ITEREXPR) BODY... [else: ELSEBODY...])`
- Iterates over async iterable
- Supports `else:` clause for when iteration completes normally

## Exception Handling

### From python-sibilant

**`try`**: Exception handling
- Syntax: `(try EXPR (except: BODY...) (TYPEEXPR BODY...) ((SYM TYPEEXPR) BODY...) ... [else: BODY...] [finally: BODY...])`
- Layout: `except:` (bare), `except [TYPEEXPR]:` (type-only), `except [SYM TYPEEXPR]:` or `except [TYPEEXPR as: SYM]:` (binding). All typed/binding forms use bracketed USE specifiers.
- Multiple exception handlers
- `else:` executes if no exception
- `finally:` always executes
- Can be used as expression (returns value)

**`raise`**: Raise exception
- Syntax: `(raise EXCEPTION)` or `(raise)` for re-raise
- Can raise exception with traceback

### From sibilant-ceval

**`check-exception`**: Exception type matching
- Syntax: `(check-exception TYPE...)`
- Checks if current exception matches types
- Used internally by `try` expansion

**`load-exception`**: Get current exception
- Syntax: `(load-exception)`
- Returns current exception object

**`clear-exception`**: Clear exception state
- Syntax: `(clear-exception)`

## Context Managers

### From python-sibilant

**`with`**: Context manager
- Syntax: `(with (BINDING EXPR) BODY...)` or `(with (EXPR as: BINDING) BODY...)`
- Enters context, binds result, executes body, exits context
- Returns last value of body

### From sibilant-ceval

**`await-with`**: Async context manager
- Syntax: `(await-with (BINDING EXPR) BODY...)`
- Async version of `with`

## Jump and Control

### From python-sibilant

**`return`**: Return from function
- Syntax: `(return [VALUE])`
- Supports tail-call optimization
- Defaults to `None` if no value

**`break`**: Break from loop
- Syntax: `(break [VALUE])`
- Terminates innermost loop
- Returns VALUE (defaults to `None`)

**`continue`**: Continue loop
- Syntax: `(continue [VALUE])`
- Jumps to next iteration of innermost loop
- Stores VALUE as potential loop result

## Delimited Continuations (Implemented)

### reset (Prompt Delimiter)

**`reset`**: Establish continuation delimiter
- Syntax: `(reset EXPR ...)`, or `(reset-at TAG EXPR ...)` for a tagged prompt
- The body is a function of no arguments; reset pushes a prompt marker onto
  the dynamic chain and calls the body above it
- The body's return pops the prompt, and its value is the reset's
- TAG is any object; `None` names the default tag, the one `reset` uses

**Semantics**:
- The prompt is a marker node; the body runs in a frame of its own on it
- In the body, `return` gives the reset its value, `break` and `continue`
  reach only loops opened in the body, `var` is local, and `yield` is a
  compile error
- Multiple prompts with different tags can be active simultaneously
- Prompts are matched by tag identity

### shift (Continuation Capture)

**`shift`**: Capture delimited continuation
- Syntax: `(shift K EXPR ...)`, or `(shift-at TAG K EXPR ...)` for a tagged prompt
- Locates nearest prompt with matching tag, crossing prompts of other tags
- Captures the continuation from the current point to the prompt: whole
  frames, the reset's body included
- Aborts to the prompt: the extents in between leave
- Calls the handler, a function of K whose body is EXPR ..., above the
  prompt; its value is the reset's

**Semantics**:
- Captured continuation K is a callable object
- Calling `(K VALUE)` resumes at the capture point with VALUE; the call
  returns the value the resumed body returns
- Continuations are multi-shot: can be called multiple times
- Each call attaches a fresh copy of the captured frames on the caller
- The handler is a function as the reset body is: `return`, `break`,
  `continue`, `var` and `yield` follow the same rules
- Capture fails if a generator boundary lies between the shift and the
  prompt; a prompt beyond a Python/C call is on another chain and is not
  found

**Example**:
```scheme
;; Simple reset/shift
(reset (+ 1 (shift k (k 10))))  ; => 11

;; Multiple resumptions
(reset
  (let ((x (shift k (list (k 1) (k 2) (k 3)))))
    (* x 10)))
; => (10 20 30)

;; Tagged prompts
(reset-at 'outer
  (reset-at 'inner
    (+ 1 (shift-at 'outer k (k 10)))))
; => 11 (captured to outer prompt)
```

## Dynamic Wind (Implemented)

### dynamic-wind

**`dynamic-wind`**: Protected dynamic extent
- Syntax: `(dynamic-wind BEFORE THUNK AFTER)`
- BEFORE: bytecode block executed on entry
- THUNK: main body to execute
- AFTER: bytecode block executed on exit

**Semantics**:
- BEFORE executes when entering the dynamic extent (normally or via continuation)
- AFTER executes when leaving the dynamic extent (normally, via exception, or via continuation)
- Hooks are bytecode blocks, NOT Python callables
- Hooks execute with isolated operand stacks
- Hooks terminate with `END_WIND` opcode

**Ordering during continuation transfer**:
1. Leaving: AFTER hooks from the current top down to the seam (inner-to-outer)
2. Attaching and entering alternate: each piece of the target is attached
   up to the next wind, and its BEFORE hook runs with only the extents
   outside it on the chain (outer-to-inner)

`dyn-with` and `parameterize` take part in the same ordering. A plain
`with`, `try` and `finally` are local: a transfer runs nothing of them.

**Example**:
```scheme
;; Resource protection
(let ((resource (acquire-resource)))
  (dynamic-wind
    (lambda () (lock resource))
    (lambda () (use resource))
    (lambda () (unlock resource))))

;; Even with continuations
(let ((log '()))
  (reset
    (dynamic-wind
      (lambda () (set! log (cons 'before log)))
      (lambda ()
        (shift k
          (k 'first)
          (k 'second)))
      (lambda () (set! log (cons 'after log)))))
  log)
; => (after before after before after before)
;    the first after is the abort to the prompt; each call of k
;    re-enters (before) and its body's return leaves (after)
```

## Migration Notes

### To Migrate

- All loop constructs (`while`, `for-each`)
- Exception handling (`try` with full feature set)
- Context managers (`with`)
- Control flow (`return`, `break`, `continue`)
- Conditional (`cond`)

### To Reconsider

- `if` as special form (currently only `cond` exists)
- Loop `else:` clauses (only in `await-for-each` currently)
- Exception chaining and `from` clause
- Multiple exception types in single handler

### Implemented

- **Delimited continuations**: `reset` and `shift` with tagged prompts
- **Dynamic wind**: `dynamic-wind` with VM-native hook blocks
- **Python boundary constraint**: continuations cannot span Python/C calls

### Future Considerations

- Pattern matching (if desired)
- Comprehensions (list/dict/set comprehensions)
- `match`/`case` style pattern matching
- One-shot continuation optimization
