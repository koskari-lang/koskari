MASK32 = 0xFFFFFFFF


def churn_loop(n: int, x: int = 1) -> int:
    i = 0
    while i < n:
        x = (x * 1664525 + 1013904223) & MASK32
        x ^= (x << 13) & MASK32
        x ^= (x >> 17)
        x ^= (x << 5) & MASK32
        x = (x + i * 1103515245) & MASK32
        i += 1
    return x


def churn_tree(depth: int, x: int = 1) -> int:
    if depth <= 0:
        x = (x * 1664525 + 1013904223) & MASK32
        x ^= (x << 13) & MASK32
        x ^= (x >> 17)
        x ^= (x << 5) & MASK32
        return x & MASK32

    a = churn_tree(depth - 1, (x * 1664525 + 1013904223) & MASK32)
    b = churn_tree(depth - 1, (x ^ 0x9E3779B9) & MASK32)
    return ((a + ((b ^ x) * 33)) ^ ((a << 7) & MASK32) ^ (b >> 3)) & MASK32


def churn_step(x: int, i: int) -> int:
    x = (x * 1664525 + 1013904223) & MASK32
    x ^= (x << 13) & MASK32
    x ^= (x >> 17)
    x ^= (x << 5) & MASK32
    return (x + i * 1103515245) & MASK32


def churn_list_each(n: int, x: int = 1) -> int:
    acc = 0
    for v in [churn_step(x, i) for i in range(n)]:
        acc ^= v
    return acc & MASK32


# Exercises attribute resolution and bound-method calls: one method
# call per iteration, mutating instance state through self.

class ChurnCounter:

    def __init__(self):
        self.total = 0

    def bump(self, v):
        self.total = (self.total + v) & MASK32
        return self.total


def churn_method(n: int, x: int = 1) -> int:
    c = ChurnCounter()
    for i in range(n):
        x = c.bump(x ^ i)
    return c.total


# Exercises attribute storage directly: every write is an in-place update
# of an instance attribute (read, op, write-back to the same attribute), and
# the loop counter is itself an attribute, so attribute access -- not range
# iteration or method calls -- dominates.

class ChurnBox:

    def __init__(self):
        self.i = 0
        self.a = 0
        self.b = 0


def churn_field(n: int, x: int = 1) -> int:
    box = ChurnBox()
    box.i = 0
    box.a = x & MASK32
    box.b = 1
    while box.i < n:
        box.a += box.b
        box.a &= MASK32
        box.b ^= box.a
        box.i += 1
    return (box.a ^ box.b) & MASK32


# Identical to churn_field but on a __slots__ class: exercises the SLOT
# storage mode (a fixed member offset) rather than the dict path.

class SlotBox:

    __slots__ = ('i', 'a', 'b')

    def __init__(self):
        self.i = 0
        self.a = 0
        self.b = 0


def churn_field_slots(n: int, x: int = 1) -> int:
    box = SlotBox()
    box.i = 0
    box.a = x & MASK32
    box.b = 1
    while box.i < n:
        box.a += box.b
        box.a &= MASK32
        box.b ^= box.a
        box.i += 1
    return (box.a ^ box.b) & MASK32


# Exercises branch density: a four-arm if/elif/else per iteration with
# deliberately light arithmetic so jump/compare dispatch dominates.

def churn_branch(n: int, x: int = 1) -> int:
    acc = 0
    v = x & MASK32
    for i in range(n):
        v = (v + i) & MASK32
        if v % 3 == 0:
            acc = (acc + (v >> 2)) & MASK32
        elif v % 5 == 0:
            acc = (acc ^ ((v << 1) & MASK32)) & MASK32
        elif v & 1:
            acc = (acc + v * 3 + 1) & MASK32
        else:
            acc = (acc + (v >> 1)) & MASK32
    return acc & MASK32


# The end.
