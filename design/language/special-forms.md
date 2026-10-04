# Special Forms

Special forms are compile-time constructs that have no runtime representation. They compile directly to bytecode.

**Implemented forms:** For the subset currently implemented in the koskari compiler (compile/bytecode, compile/begin, compile/call, compile/with, compile/call/cc, compile/let/cc, compile/capture/cc, compile/reset, compile/shift, lambda, etc.), see [SPECIALS.md](../SPECIALS.md).

## From python-sibilant

### Core Special Forms

- **`quote`** / **`'`**: Returns form without evaluation
- **`quasiquote`** / **`` ` ``**: Template literal with `unquote` and `unquote-splicing`
- **`begin`**: Sequential evaluation, returns last value
- **`lambda`**: Anonymous function creation
- **`function`**: Named function with self-reference capability
- **`let`**: Lexical scoping with bindings
  - Supports named let (recursive binding)
  - Alternative binding syntax: `(EXPR as: BINDING)`
- **`define`**: Variable/function definition (local or global depending on context)
- **`define-global`** / **`setq-global`**: Global scope definition/assignment
- **`setq`**: Variable assignment
- **`delq`**: Variable deletion
- **`refq`**: Force closure cell creation for symbol
- **`global`**: Access global scope (bypasses local shadowing)
- **`delq-global`**: Remove global binding

### Control Flow

- **`cond`**: Conditional execution with `else:` keyword support
- **`while`**: Loop with test expression
  - Supports `break` and `continue` with optional values
  - Result from `continue` or `break`, or final body expression
- **`for-each`**: Iteration over sequences
  - Supports `break` and `continue`
  - Alternative binding syntax supported
- **`try`**: Exception handling
  - Multiple `except:` clauses
  - Exception binding: `((BINDING EXCEPTION_TYPE) BODY...)`
  - `else:` clause (executes if no exception)
  - `finally:` clause
  - Can be used as expression (evaluates to last executed block)

### Context Management

- **`with`**: Single-shot context manager entry/exit
  - Syntax: `(with [name? expr] body ...)`; optional binding of enter result
  - The expression value is the body's value, or `None` when exit swallows an exception (see [interpreter/with-dyn-with.md](../interpreter/with-dyn-with.md))
  - Sync and async managers supported (VM auto-awaits enter/exit when they return awaitables)
  - A transfer leaving through an active `with` runs its exit and finishes the instance; a landed segment does not re-enter it
  - Implemented via compile/with; see [SPECIALS.md](../SPECIALS.md) §with. Opcode tests: `tests/lib/opcode/test_with.py`; special-form tests: `tests/compile/specials/test_with.py` (both cover sync and async)
- **`dyn-with`**: Multi-shot context manager; participates in continuation transfer (wind/unwind)
  - Same syntax as `with`; the value is the body's, or `None` after a swallowed exception

### Generators and Coroutines

- **`yield`**: Generator yield
- **`yield-from`**: Delegate to another generator/coroutine
  - Optional `send:` keyword argument
- **`await`**: Await async operation
- **`declare-async`**: Flag function/generator as coroutine/async generator

### Attributes and Methods

- **`attr`**: Get object attribute
- **`set-attr`**: Set object attribute (also available via `setf`)
- **`del-attr`**: Delete object attribute
- **`method-call`** / **`!`**: Method call syntax `(! METHOD OBJ ARGS...)`

### Values and Unpacking

- **`define-values`**: Multi-value definition with nested binding structure
  (implemented as `compile/define-values`; see [SPECIALS.md](../SPECIALS.md))
- **`setq-values`**: Multi-value assignment with nested binding structure
  (implemented as `compile/setq-values`; see [SPECIALS.md](../SPECIALS.md))
- Supports `*` keyword for rest arguments in bindings

### Documentation

- **`doc`**: Set docstring for current scope (module, class, or function)

### Import System

- **`import`**: Import module (supports relative imports with `.` prefix)
- **`import-from`**: Import specific members from module

### Return and Control

- **`return`**: Return from function (supports TCO)
- **`break`**: Break from loop with optional value
- **`continue`**: Continue loop with optional value

## From sibilant-ceval

### Additional Special Forms

- **`#bytecode`**: Direct bytecode injection (for testing/debugging)
- **`#tuple`**: Build argument tuple
- **`await-for-each`**: Async iteration
- **`await-with`**: Async context manager
- **`class`**: Class definition
- **`super`**: Superclass access
- **`letrec`**: Recursive let binding
- **`collect-let`**: Let with collection semantics
- **`call-object`**: Generic function call
- **`call-method`**: Method call
- **`check-exception`**: Exception type matching
- **`clear-exception`**: Clear exception state
- **`load-exception`**: Load current exception
- **`load-globals`**: Load global namespace
- **`load-locals`**: Load local namespace
- **`bindq`**: Bind variable (alternative to define)
- **`ref-attr`**: Get attribute reference (cell)
- **`ref-item`**: Get item reference (cell)
- **`item`**: Get item by key (with constant key optimization)
- **`set-item`**: Set item by key (with constant key optimization)
- **`del-item`**: Delete item by key (with constant key optimization)

## Migration Notes

### To Migrate

- All core special forms from python-sibilant
- Exception handling (`try` with full feature set)
- Generator/coroutine support
- Multi-value binding
- Import system integration
- Context managers

### To Reconsider

- `#bytecode` direct injection (useful for testing, but security concern?)
- `collect-let` vs `let` semantics
- Method call syntax (`!` vs `method-call`)

### To Design

- `let*` (sequential let binding)
- Quasiquote implementation (needs review for ceval compatibility)
