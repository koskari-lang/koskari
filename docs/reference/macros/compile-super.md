# compile/super
**Syntax:** **`(super)`**, **`(super ARGS ...)`**

**Behaviour:** **`(super)`** -> **`(call super __class__ self)`**;
**`(super ARGS ...)`** -> **`(call super ARGS ...)`**.

`__class__` is the class a method was defined in. The class body binds
it once the class object exists, and every method defined in that body
closes over it, so `(super)` inside `A.__init__` resolves to
`(super A self)` however deep the hierarchy of the instance is. This
is the same model as Python's zero-argument `super()` and its
`__class__` cell.

The zero-argument form names the first parameter `self`. A method whose
first parameter has another name, a classmethod for instance, uses the
explicit form: `(super __class__ cls)`.

```koskari
define-class A []:
  def __init__ [self]:
    (set-attr self a 1)

define-class B (A):
  def __init__ [self]:
    (.__init__ (super))
    (set-attr self b 2)

define-class C (B):
  def __init__ [self]:
    (.__init__ (super))
    (set-attr self c 3)

(vars (C))   ; {'a': 1, 'b': 2, 'c': 3}
```
