# Special Forms

Special forms are compile-time constructs that have no runtime representation. They compile directly to bytecode.

**Implemented forms:** For the subset currently implemented in the koskari compiler (compile/bytecode, compile/begin, compile/call, lambda), see [SPECIALS.md](../SPECIALS.md).

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
  - In Koskari the expression value is always `None` (see [interpreter/with-dyn-with.md](../interpreter/with-dyn-with.md))
  - Transfer across an active `with` is rejected at runtime
- **`dyn-with`**: Multi-shot context manager; participates in continuation transfer (wind/unwind)
  - Same syntax as `with`; value always `None`

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
- **`setq-values`**: Multi-value assignment with nested binding structure
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
- **`recur`**: Tail-recursive call (optimized)
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
- `recur` vs tail-call optimization in general
- Method call syntax (`!` vs `method-call`)

### To Design

- `call/cc` support (bounded and unbounded continuations)
- `let*` (sequential let binding)
- Quasiquote implementation (needs review for ceval compatibility)
