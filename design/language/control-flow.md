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
- Syntax: `(reset EXPR)` or `(reset TAG EXPR)`
- Pushes a prompt marker onto the dynamic chain
- Evaluates EXPR
- On normal completion, pops prompt and returns value
- TAG is an optional prompt tag for matching (default: 0)

**Semantics**:
- A prompt does NOT create a new frame; it is a lightweight marker
- Multiple prompts with different tags can be active simultaneously
- Prompts are matched by tag identity (numeric equality)

### shift (Continuation Capture)

**`shift`**: Capture delimited continuation
- Syntax: `(shift K EXPR)` or `(shift TAG K EXPR)`
- Locates nearest prompt with matching tag
- Captures continuation from current point to prompt
- Binds captured continuation to K
- Aborts to prompt and evaluates EXPR in that context

**Semantics**:
- Captured continuation K is a callable object
- Calling `(K VALUE)` resumes at the capture point with VALUE
- Continuations are multi-shot: can be called multiple times
- Each call clones the captured state
- Capture fails if Python/C boundary exists between current and prompt

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
(reset 'outer
  (reset 'inner
    (+ 1 (shift 'outer k (k 10)))))
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
1. Unwind: AFTER hooks from current toward LCA (inner-to-outer)
2. Install: Splice target chain
3. Wind: BEFORE hooks from LCA toward target (outer-to-inner)

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
; => (after before after before)
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
