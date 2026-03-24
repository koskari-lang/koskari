# Core Types

Types are defined in the native extension `koskari.lib`; the Python module `koskari.types` imports those and adds predicates and helpers (`is_symbol`, `build_proper`, `unpack`, etc.). Base exceptions live in `koskari.exceptions`. Import from the defining module: `koskari.lib`, `koskari.types`, or `koskari.exceptions` as appropriate. See [package-layout.md](package-layout.md) for the full layout and import policy.

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

**Methods (on pair values)** (see koskari `koskari/lib/pair.c`):
- `pair.length()`: Number of elements in the chain (stops at cycle for recursive pairs)
- `pair.follow()`: Return an iterator over the pair chain (yields each cons cell)
- `pair.unpack()`: Return an iterator that yields the car of each cell (list elements only)
- `pair.__copy__()`: Shallow copy of the chain; preserves circular structure in the copy
- `pair.is_proper()`: True if the pair is a proper list (ends with nil or is circular)
- `pair.is_recursive()`: True if the chain is circular
- `pair.append(obj)`: Mutate in place: find last cons, then add obj at the end (new cell `(last_cdr . obj)` or `(obj . nil)` if proper). Raises TypeError on nil.
- `pair.extend(obj)`: Mutate in place: find last cons, then splice a *copy* of obj onto the end (obj may be a pair or an iterable; pairs are copied, other iterables converted to proper list).
- `pair.splice(obj)`: Like extend but if obj is a pair, reuse it in place (shared structure); otherwise convert iterable to proper list and splice. Raises TypeError on nil.
- `pair.clear_position(follow=False)`: Clear position on this pair; if follow true, clear along entire cdr chain
- `pair.set_position(position, follow=False)`: Set position; if follow true, set on entire cdr chain
- `pair.fill_position(position, follow=False)`: Set position only where unset; if follow true, walk chain and fill
- `pair.get_position()`: Return position object or None

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
- For Python `inspect` / typing ecosystem interop (`__signature__`, bound methods, annotations), see [inspect-interop-kos-callables.md](inspect-interop-kos-callables.md)

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
- `cons()`: Variadic pair builder (last arg is tail); no recursive keyword
- `make_recursive(pair)`, `break_recursive(pair)`: Make or break circular structure
- `car()`, `cdr()`, `setcar()`, `setcdr()`: Pair accessors/mutators
- `build_unpack_pair()`: Build pair from unpacked iterables
- Pair instance methods: `length()`, `follow()`, `unpack()`, `__copy__`, `is_proper()`, `is_recursive()`, `append()`, `extend()`, `splice()`, `clear_position()`, `set_position()`, `fill_position()`, `get_position()` (see koskari `koskari/lib/pair.c`)

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

**Defer Type** (async handle, implemented in koskari):
- `defer`: Single-shot await-handle; stores callable and args/kwds for application on first explicit await
- Not eagerly awaited: CALL return and other implicit await sites push it as a plain value; only the AWAIT opcode drives it
- Builtin: `defer(fn, *args, **kwds)`; Python type: `DeferCoroutine` (koskari/lib/defer.c)
- See design/api/defer-coroutine.md for lifecycle and invariants

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

NOTE: Control flow metadata (block_style, enabled, labels) lives in the region
nodes on the dynamic chain (TRY, FINALLY, LOOP, FOREACH). See evaluator.md for details.

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
- `DelimitedContinuation`: Captured delimited continuation (shift); multi-shot
  - Callable from Python: always raises `TypeError` (invocation only via CALL inside koskari_eval)
  - Delimited: captures up to prompt delimiter, not entire stack
  - Supports resume value or exception injection
  - Cannot span Python/C call boundaries
- `Continuation`: Captured undelimited continuation (call/cc); multi-shot
  - Callable from Python: raises `KoskariContinuationApply` to perform upwards transfer via exception (does not return a coroutine)
  - When invoked inside koskari_eval with matching token: direct apply
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
  - Kinds: FRAME, PROMPT, WIND, TRY, FINALLY, LOOP, FOREACH, NS, CC, CC_RAISE, YIELD_FROM, PARAM, WITH, DYN_WITH, SHIFT
  - `KosDynNode_IS_REGION` matches TRY, FINALLY, LOOP, FOREACH
- `KosDynFrame`: Function activation record (code, pc, stack, namespace)
- `KosDynPrompt`: Delimiter for delimited continuations (prompt_id, handler)
- `KosDynWind`: Dynamic-wind extent (before/after thunk callables)
- Region kinds (formerly `KosDynRegion` with `KosRegionStyle`; now separate kinds):
  - `KOS_DYN_TRY`: try block (handler_pc, enabled)
  - `KOS_DYN_FINALLY`: finally block (handler_pc, enabled, handling_raise)
  - `KOS_DYN_LOOP`: while loop (break_pc, continue_pc, enabled)
  - `KOS_DYN_FOREACH`: for-each loop (extends LOOP with iter, exhausted, iter_next_else_pc)
 `KosTransferPlan`: Action list for continuation transfer with hook execution.
  Single allocation (plan + flexible actions array). Default 32 slots; realloc
  when needed. One cached plan reused on free.

### Not Migrating

- TCO types (`tailcall`) - will be handled directly in native evaluator
- PyCell wrappers - replaced by Ref types

### To Consider

- Lazy gensym semantics (not yet migrated)
- Additional type predicates (`is_symbol`, `is_pair`, etc.) - may be Python-side
- Type system integration
- Pattern matching on types
