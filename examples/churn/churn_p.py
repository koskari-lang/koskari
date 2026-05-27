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


# The end.
