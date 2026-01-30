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
- Syntax: `(try EXPR (except: TYPE BODY...) ... [else: BODY...] [finally: BODY...])`
- Exception binding: `((BINDING TYPE) BODY...)`
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

### To Design

- Pattern matching (if desired)
- Comprehensions (list/dict/set comprehensions)
- `match`/`case` style pattern matching
- Control flow with `call/cc`
