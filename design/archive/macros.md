# Macro System

Koskari supports two types of macros: reader macros (parse-time) and compile-time macros.

## Reader Macros (Parse-Time)

### From python-sibilant

Reader macros are defined via `set-macro-character` and transform the source stream before parsing.

**Features:**
- Character-based dispatch
- Can read arbitrary tokens/forms
- Can modify reader state
- Examples: `'` (quote), `` ` `` (quasiquote), `,` (unquote), `,@` (unquote-splicing)

**Implementation:**
- Reader maintains a table of macro characters
- When character is encountered, macro function is called with reader stream
- Macro returns parsed form or special keyword (VALUE, ATOM, SKIP, etc.)

## Compile-Time Macros

### From python-sibilant

**`defmacro`**: Defines a compile-time macro
- Takes name, formals, and body
- Body is a function that receives the source form
- Returns expanded form (cons list or symbol)
- Can be proper or improper (handled via `_proper` attribute)

**`defalias`**: Defines an alias macro
- Takes name and body
- Body is a function that receives no arguments
- Returns expanded form (must be a symbol)
- Used for symbol-level transformations

**`macroexpand-1`**: Debugging utility
- Expands macro once (doesn't expand nested macros)
- Useful for macro development

### Macro Expansion Process

1. Compiler checks if head of form is a symbol
2. Looks up symbol in compile-time namespace
3. If found and is a Macro instance, calls `compile_impl`
4. Macro function receives source form and returns expansion
5. Compiler restarts compilation with expanded form

### Macro Features

- **Hygiene**: Macros operate on source forms, not evaluated values
- **Proper vs. Improper**: Macros can be marked as proper (normal) or improper (special syntax)
- **Self-reference**: Macros can reference themselves via their name
- **Compile-time evaluation**: Macros can perform arbitrary computation at compile time

## From sibilant-ceval

Similar macro system, but integrated with custom bytecode compiler.

## Migration Notes

### To Migrate

- Reader macro system (character-based dispatch)
- `defmacro` for compile-time macros
- `defalias` for symbol-level macros
- Macro expansion infrastructure
- Proper/improper macro distinction

### To Reconsider

- Macro hygiene mechanisms (gensym support)
- Macro debugging tools (`macroexpand-1` and variants)
- Reader macro character set (which characters are available?)

### To Design

- Syntax-case style macros (if desired)
- Macro compilation to bytecode (for faster expansion)
- Macro caching/invalidation strategy
- Integration with `call/cc` (if macros can capture continuations)

## Related design (current tree)

- **Unified matcher & transformer model (proposal):** [../language/unified-matcher-transformer-model.md](../language/unified-matcher-transformer-model.md)
- **Procedural hygiene / syntax boxes (proposal):** [../language/macro-hygiene-syntax-boxes.md](../language/macro-hygiene-syntax-boxes.md)
