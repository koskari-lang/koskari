# Operators

Operators in Koskari have both compile-time and runtime representations. They compile to optimized bytecode when possible, but can also be used as runtime functions.

## From python-sibilant

### Arithmetic Operators

- **`+`** / **`add`**: Addition (reducing, unary positive)
- **`-`** / **`subtract`**: Subtraction (reducing, unary negative)
- **`*`** / **`multiply`**: Multiplication (reducing)
- **`/`** / **`divide`**: Division (binary)
- **`//`** / **`floor-divide`**: Floor division (binary)
- **`%`** / **`modulo`**: Modulo (binary, string formatting)
- **`**`** / **`power`**: Exponentiation (binary)
- **`@`** / **`matrix-multiply`**: Matrix multiplication (binary)

### Bitwise Operators

- **`&`** / **`bitwise-and`**: Bitwise AND (reducing)
- **`|`** / **`bitwise-or`**: Bitwise OR (reducing)
- **`^`** / **`bitwise-xor`**: Bitwise XOR (reducing)
- **`~`** / **`invert`**: Bitwise inversion (unary)
- **`<<`** / **`shift-left`**: Left shift (binary)
- **`>>`** / **`shift-right`**: Right shift (binary)

### Comparison Operators

- **`==`** / **`eq`**: Equality (binary)
- **`!=`** / **`not-eq`**: Inequality (binary)
- **`<`** / **`lt`**: Less than (binary)
- **`<=`** / **`le`**: Less than or equal (binary)
- **`>`** / **`gt`**: Greater than (binary)
- **`>=`** / **`ge`**: Greater than or equal (binary)
- **`is`**: Identity comparison (binary)
- **`is-not`**: Non-identity comparison (binary)
- **`in`**: Membership test (binary)
- **`not-in`**: Non-membership test (binary)
- **`contains`**: Reverse membership (SEQUENCE VALUE)
- **`not-contains`**: Reverse non-membership (SEQUENCE VALUE)

### Logical Operators

- **`and`**: Short-circuit AND (reducing)
- **`or`**: Short-circuit OR (reducing)
- **`not`**: Logical negation (unary)

### Item/Attribute Access

- **`item`**: Get item by key (binary: OBJ KEY)
- **`set-item`**: Set item by key (ternary: OBJ KEY VALUE)
- **`del-item`**: Delete item by key (binary: OBJ KEY)

### Collection Builders

- **`build-tuple`** / **`#tuple`**: Build tuple from values
- **`build-list`** / **`#list`**: Build list from values
- **`build-set`** / **`#set`**: Build set from values
- **`build-dict`** / **`#dict`**: Build dict from key-value pairs
- **`build_str`** / **`#str`**: String concatenation (collapses adjacent literals)
- **`build-slice`** / **`#slice`**: Create slice object (2 or 3 args)

### Other Operators

- **`iter`**: Create iterator (unary or binary with sentinel)
- **`format`**: Format value (unary or binary with format spec)
- **`raise`**: Raise exception (unary or nullary for re-raise)
- **`pass`**: No-op (evaluates to None, sub-expressions not evaluated)

## From sibilant-ceval

### Additional Operators

- **`divmod`**: Division and modulo (binary)
- **`repr`**: Object representation (unary)
- **`truth`**: Truth value (unary)
- **`isinstance`**: Type checking (reducing)
- **`issubclass`**: Subclass checking (reducing)

## Compile-Time Optimizations

### Constant Folding

- Arithmetic operations on compile-time constants
- String literal concatenation in `#str`
- Tuple/list/set building from constants

### Special Cases

- Unary `+` and `-` on numeric literals
- Unary `*` (multiply by 1)
- Unary `/` (divide 1 by value)
- Unary `//` (floor divide 1 by value)
- Constant key optimization in `item`/`set-item`/`del-item`

### Reducing Operators

Operators that can take multiple arguments and reduce:
- `+`, `-`, `*`, `&`, `|`, `^`, `and`, `or`

These compile to a sequence of binary operations, with constant folding where possible.

## Migration Notes

### To Migrate

- All arithmetic, bitwise, and comparison operators
- Logical operators with short-circuit behavior
- Collection builders with constant folding
- Item/attribute access operators
- Constant key optimization for item access

### To Reconsider

- Operator naming (symbolic vs. word forms)
- Reducing operator semantics (left-associative vs. right-associative)
- String formatting integration with `%` operator

### To Design

- Operator overloading and custom operators
- Pattern matching operators (if desired)
- Range/slice syntax sugar
