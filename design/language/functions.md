# Functions

## Function Definition

### From python-sibilant

**`function`**: Named function with self-reference
- Syntax: `(function NAME (FORMAL...) BODY...)`
- Creates function bound to NAME
- Function can reference itself via NAME
- Supports TCO (tail-call optimization)

**`lambda`**: Anonymous function
- Syntax: `(lambda (FORMAL...) BODY...)`
- Creates anonymous function
- Supports TCO

### Formals (Parameter Lists)

**Positional parameters**: `(a b c)`
**Default values**: `(a b: default-b c: default-c)`
**Keyword-only**: `(a b * c: default-c d:)`
**Varargs**: `(a b *args)`
**Varkeywords**: `(a b **kwargs)`
**Combined**: `(a b: default-b *args c: default-c **kwargs)`

**Alternative syntax**: `(a [b default-b] *args [c default-c] **kwargs)`

### From sibilant-ceval

**Named lambda**: `(lambda NAME (FORMAL...) BODY...)`
- Creates named anonymous function
- Self-reference capability

## Function Calling

### From python-sibilant

**Direct call**: `(FUNC ARG...)`
- If FUNC is a symbol, looks up and calls
- If FUNC is an expression, evaluates then calls
- Supports positional and keyword arguments

**Method call**: `(! METHOD OBJ ARGS...)` or `(method-call METHOD OBJ ARGS...)`
- Optimized method call
- Falls back to attribute access + call if keyword args needed

### From sibilant-ceval

**`call-object`**: Generic function call
- Handles all calling conventions
- Supports `*args` and `**kwargs`
- Optimized for different argument patterns

**`call-method`**: Method call
- Optimized method invocation
- Supports all argument patterns

## Tail-Call Optimization (TCO)

### From python-sibilant

**TCO enabled by default** for:
- `function` definitions
- `lambda` expressions
- `let` forms

**TCO disabled for**:
- Functions with `yield` (generators)
- Functions with `await` (coroutines)
- Functions explicitly disabled

## Function Features

### Closures

- Functions capture lexical environment
- Variables in outer scopes are captured in closure cells
- `refq` forces cell creation for symbol

### Default Arguments

- Evaluated at function definition time
- Stored in function object
- Can reference outer scope variables

### Documentation

- Functions can have docstrings (via `doc` special form or string literal)
- Docstrings accessible via `__doc__` attribute

## Migration Notes

### To Migrate

- Function definition (`function`, `lambda`)
- Parameter handling (all formal patterns)
- Function calling (all conventions)
- TCO infrastructure
- Closure support
- Default argument evaluation

### To Reconsider

- Direct tail-call opcodes
- Method call optimization strategy
- Function introspection capabilities

### To Design

- Function annotations/type hints
- Decorator syntax
- Partial application syntax
- Function composition operators
- Integration with `call/cc` (continuation capture in functions)
