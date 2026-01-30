# Syntax

## S-Expressions

Koskari uses S-expressions (sexprs) as its primary syntax:
- Lists: `(item1 item2 item3)`
- Improper lists: `(item1 item2 . item3)`
- Atoms: symbols, keywords, numbers, strings, etc.

## Literals

### From python-sibilant

**Numbers**:
- Integers: `123`, `-456`
- Binary: `0b1010`
- Octal: `0o755`
- Hex: `0xFF`
- Floats: `3.14`, `1.5e-10`, `2.0f`
- Decimals: `123.45d` (Decimal type)
- Fractions: `3/4` (Fraction type)
- Complex: `1+2i`, `3.5+4.2j`

**Strings**:
- Regular: `"hello"`, `'world'`
- Unicode escapes: `\uXXXX`, `\UXXXXXXXX`
- Format strings: `f"value: {x}"` (f-string syntax)

**Booleans**:
- `True`, `False`
- `nil` (also false)

**Special**:
- `nil`: Empty list
- `...`: Ellipsis
- `None`: Python None

**Symbols**:
- Identifiers: `foo`, `bar-baz`, `_private`
- Qualified: `module.function`

**Keywords**:
- Prefix: `:keyword`
- Suffix: `keyword:`
- Used for: `else:`, `except:`, `finally:`, `as:`, `*:`

## Reader Macros

### Character Macros

- **`'`**: Quote - `'form` → `(quote form)`
- **`` ` ``**: Quasiquote - `` `form `` → `(quasiquote form)`
- **`,`**: Unquote - `,expr` → `(unquote expr)`
- **`,@`**: Unquote-splicing - `,@expr` → `(unquote-splicing expr)`

### Custom Reader Macros

Defined via `set-macro-character`:
- Character-based dispatch
- Can read arbitrary tokens
- Can modify reader state

## Comments

- **Line comments**: `; comment text`
- **Block comments**: Not standard (use `#| ... |#` if implemented)

## Binding Syntax

### Standard Binding

`(SYM EXPR)` - Bind EXPR to SYM

### Alternative Binding

`(EXPR as: SYM)` - Bind EXPR to SYM (alternative syntax)

Used in:
- `let` bindings
- `with` context managers
- `for-each` iterations

## Formals (Parameter Lists)

### Positional

`(a b c)` - Three positional parameters

### With Defaults

`(a b: default-b c: default-c)` - Keyword defaults

### Keyword-Only

`(a b * c: default-c d:)` - `*` separates positional from keyword-only

### Varargs

`(a b *args)` - `*args` collects remaining positional args

### Varkeywords

`(a b **kwargs)` - `**kwargs` collects keyword args

### Combined

`(a b: default-b *args c: default-c **kwargs)` - All features

## Multi-Value Binding

### Values Return

Functions can return multiple values via `values()`:
- `(values a b c)` - Returns three values

### Values Binding

`(define-values (a b c) (values 1 2 3))` - Bind multiple values

Supports nested structures:
- `(define-values ((a b) c) (values (values 1 2) 3))`

### Rest in Values

`(define-values (a *rest c) ...)` - `*` keyword for rest in values

## Special Syntax

### Method Call

`(! METHOD OBJ ARGS...)` - Method call syntax

### Attribute Access

`(attr OBJ SYM)` - Get attribute
`(set-attr OBJ SYM VALUE)` - Set attribute

### Item Access

`(item OBJ KEY)` - Get item
`(set-item OBJ KEY VALUE)` - Set item

## Migration Notes

### To Migrate

- All literal syntax
- Reader macro characters
- Binding syntax variants
- Formals syntax
- Multi-value syntax

### To Reconsider

- F-string syntax (Python-specific)
- Decimal/Fraction literals (Python-specific)
- Comment syntax
- Reader macro character set

### To Design

- Pattern matching syntax (if desired)
- Comprehensions syntax (if desired)
- Additional literal types
- Syntax extensions
