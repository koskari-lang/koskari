# Countdown

A `while` loop with `decr!` and an `else:` peer that runs when the counter
reaches zero -- used in the from-python [Data and control](../data-and-control.md)
chapter.

## The program

```koskari
def countdown [n]:
  while (> n 0):
    print n
    decr! n
  else:
    print "lift-off"

@entry-point
def main []:
  (countdown 3)
```

## Run

Save as `countdown.ki` and:

```bash
koskari countdown.ki
```

Expected:

```text
3
2
1
lift-off
```

## See also

- [layout/for-each](../../../reference/layouts/layout-for-each.md) (loop `else` peer)
- [incr! / decr!](../../../reference/macros/compile-incr-and-compile-decr.md)
