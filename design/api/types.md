# Core Types

## From python-sibilant

### Symbol

**`symbol`**: Symbol type
- Immutable identifier
- Created via `symbol("name")` or reader
- Used for variable names, function names, etc.
- Symbols are interned (same name = same object)

**Predicates**:
- `is_symbol(obj)`: Check if object is symbol
- `symbol?(obj)`: Type predicate

### Keyword

**`keyword`**: Keyword type
- Immutable identifier with `:` prefix or suffix
- Created via `keyword("name")` or reader (`:name` or `name:`)
- Used for special markers (e.g., `else:`, `except:`)
- Keywords are interned

**Predicates**:
- `is_keyword(obj)`: Check if object is keyword
- `keyword?(obj)`: Type predicate

### Pair (Cons Cell)

**`pair`**: Cons cell type
- Two-element cell: `(car . cdr)`
- Created via `cons(car, cdr)`
- Can be proper (ends with `nil`) or improper
- Supports recursive detection

**Operations**:
- `car(pair)`: Get first element
- `cdr(pair)`: Get rest
- `setcar(pair, value)`: Set first element
- `setcdr(pair, value)`: Set rest
- `cons(car, cdr)`: Create pair
- `build-proper(*values)`: Create proper list

**Predicates**:
- `is_pair(obj)`: Check if object is pair
- `is_proper(pair)`: Check if proper list
- `is_recursive(pair)`: Check if circular

**Accessors**:
- `cadr`, `caddr`, `cadddr`, etc.: Composed accessors
- `first`, `second`, `third`, etc.: Named accessors

### Nil

**`nil`**: Empty list / false value
- Singleton object representing empty list
- Used as false in conditionals
- End marker for proper lists

**Predicates**:
- `is_nil(obj)`: Check if object is nil
- `nil?(obj)`: Type predicate

### Gensym

**`gensym([prefix])`**: Generate unique symbol
- Creates unique symbol for macro hygiene
- Optional prefix string
- Guaranteed unique within compilation unit

**`lazygensym(name)`**: Lazy gensym
- Creates symbol that looks up name at runtime
- Used for forward references

**Predicates**:
- `is_lazygensym(obj)`: Check if lazy gensym

## From sibilant-ceval

### Namespace

**`Namespace`**: Lexical scope container
- Holds variable bindings
- Supports local and closure variables
- Fast indexing via opcodes

**`ModuleNamespace`**: Module-level namespace
- Extends Namespace
- Acts like attributes for references

### Ref (Reference Cell)

**`Ref`**: Mutable reference cell
- Wraps value for mutation
- Used in closures for shared state
- Created lazily (only when needed)

**Operations**:
- `ref.deref()`: Get value
- `ref.store(value)`: Set value
- `ref.empty`: Check if unbound

### Function

**`Function`**: Compiled function object
- Contains bytecode and namespace spec
- Supports calling conventions
- Can be async/generator

### Module

**`Module`**: Module object
- Contains ModuleNamespace
- Supports import system
- Has `__dict__`-like interface

## Migration Status

### Completed

The following types have been migrated to koskari and are fully implemented
with C extension support and comprehensive test coverage:

**Atom Types** (from python-sibilant):
- `symbol`: Interned immutable identifier with split/rsplit support
- `keyword`: Interned keyword type with automatic colon stripping
- `gensym()`: Unique symbol generation with optional predicate

**Pair Types** (from python-sibilant):
- `pair`: Mutable cons cell with position tracking, recursion detection
- `nil`: Singleton empty list/false value (subtype of pair)
- `cons()`: Variadic pair builder with recursive option
- `car()`, `cdr()`, `setcar()`, `setcdr()`: Pair accessors/mutators
- `build_unpack_pair()`: Build pair from unpacked iterables

**Namespace Types** (from sibilant-ceval):
- `Namespace`: Lexical scope with local/closure bindings and ref conversion
- `NamespaceSpec`: Specification for namespace creation
- `ModuleNamespace`: Module-level namespace with builtins fallback
- `CollectorNamespace`: Namespace that blocks closure capture
- `Module`: Module wrapper providing attribute access to namespace

**Reference Types** (from sibilant-ceval):
- `Ref`: Base mutable reference cell with empty/store/deref/clear
- `AttributeRef`: Reference backed by object attribute
- `ItemRef`: Reference backed by mapping/sequence item
- `DynamicRef`: Reference backed by getter/setter/deleter functions
- `ConstantRef`: Immutable reference (store/clear raise ValueError)
- `MemoizedRef`: Lazy reference that caches first deref result

**Formals Types** (from sibilant-ceval):
- `Formals`: Formal parameter specification with apply() for binding
- `FormalsSpec`: Template for creating Formals instances

**Values Type** (from python-sibilant):
- `values`: Multi-value return object with positional and keyword support

**Parser Types** (from python-sibilant):
- `BaseSourceStream`: Native C base class for efficient character-level I/O
  - `read(count)`: Read characters with position tracking
  - `peek(count)`: Peek without consuming
  - `position()`: Get (line, col) tuple
  - `read_until(testf)`: Read until callable returns true
  - `read_until_chars(terminators)`: Read until terminator character
  - `skip_whitespace()`: Skip and return whitespace
  - `readline()`: Read line including newline
- `SourceStream`: Python subclass with error handling and iteration
- `Reader`: Extensible s-expression reader with macro support
  - Reader macros for custom syntax
  - Atom patterns for numeric literals
  - Quote/quasiquote/unquote handling
  - Format string interpolation
  - Position tracking for error messages

**Stack Types** (from sibilant-ceval, refactored):
- `Stack`: Block-structured value stack for the evaluator (storage-only)
  - `push(val)`: Append value to current block
  - `pop()`: Remove and return top value
  - `peek()`: Borrow reference to top value
  - `peek_at(index)`: Access by index (negative for end-relative)
  - `extend(seq)`: Add multiple elements from sequence
  - `pull(count)`: Copy values from parent block to current
  - `drop(count)`: Remove elements without returning
  - `duplicate(count)`: Copy top elements onto stack
  - `replace(index, value)`: Update element at index
  - `pop_tail(count)`: Remove and return multiple as tuple
  - `pop_tail_dict(count)`: Pop key/value pairs into dict
  - `push_block()`: Create new block as current
  - `pop_block(count)`: Transfer values to parent and remove block
  - `drop_block()`: Remove block without transferring
  - `length()`: Size of current block
  - `depth()`: Count of blocks
  - `total()`: Sum of all block lengths
  - `view()`: Tuple of current block contents
  - `clear()`: Pop all blocks except root, empty it
  - `copy()`: Create COW clone of stack

NOTE: Control flow metadata (block_style, enabled, labels) has been moved to
`KosDynRegion` nodes in the dynamic chain. See evaluator.md for details.

**Bytecode Types** (from sibilant-ceval, redesigned):
- `Code`: Compiled bytecode storage object
  - Compact 4-byte instructions (uint16_t opcode + uint16_t arg)
  - Delta-encoded position table (only records changes)
  - Read-only memory protection via `mprotect()` after init
  - Constant pool with deduplication
  - `KOS_CODE_YIELDS` flag for generators
  - `length()`: Instruction count
  - `oparg(i)`: Get (opcode, arg) tuple at index
  - `position(i)`: Get (line, col) for instruction
  - `constant(i)`: Get constant from pool
- `OpCode`: Enum of all bytecode opcodes
  - Attached `style` attribute for argument interpretation
- `OpStyle`: Enum of opcode argument styles
  - COMPILE, LABEL, NOARG, COUNT, PAIR_COUNT, STAR, CONST, VAR, FASTVAR

**Evaluator Types** (new for Koskari VM):
- `Continuation`: Captured delimited continuation (multi-shot)
  - Callable: returns `Coroutine` that resumes at captured point
  - Multi-shot: each call clones captured state
  - Delimited: captures up to prompt delimiter, not entire stack
  - Supports resume value or exception injection
  - Cannot span Python/C call boundaries
- `Coroutine`: Koskari coroutine wrapper
  - Implements Python awaitable protocol (`__await__`)
  - `send(value)`: Resume with value
  - `throw(exc)`: Inject exception
  - `close()`: Close coroutine
  - States: NEW, RUNNING, WAITING_PY, DONE
- `AsyncGenerator`: Koskari async generator wrapper
  - Implements async iterator protocol (`__aiter__`, `__anext__`)
  - Yields values via StopIteration
  - Supports close on exhaustion

**Dynamic Chain Types** (internal, for evaluator infrastructure):
- `KosDynNode`: Base structure for dynamic chain nodes
  - Types: FRAME, PROMPT, WIND, REGION, PYCALL
- `KosDynFrame`: Function activation record (code, pc, stack, namespace)
- `KosDynPrompt`: Delimiter for delimited continuations (prompt_id, handler)
- `KosDynWind`: Dynamic-wind extent (before/after hook bytecode blocks)
- `KosDynRegion`: Block-local control (try/finally/loop/with)
  - Styles: TRY, FINALLY, LOOP, WITH, FOREACH, AWAIT_WITH, AWAIT_FOREACH, NS, BEGIN
- `KosHookRunner`: Isolated execution context for wind/unwind hooks
- `KosTransferPlan`: Action list for continuation transfer with hook execution

### Not Migrating

- TCO types (`tailcall`, `Trampoline`) - will be handled directly in native evaluator
- PyCell wrappers - replaced by Ref types

### To Consider

- Lazy gensym semantics (not yet migrated)
- Additional type predicates (`is_symbol`, `is_pair`, etc.) - may be Python-side
- Type system integration
- Pattern matching on types
