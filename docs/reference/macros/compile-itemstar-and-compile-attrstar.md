# compile/item* and compile/attr*

**Syntax:** `(item* OBJ KEY ...)`, `(attr* OBJ NAME ...)`

**Behaviour:** Left-nested `item` and `attr` chains: `(item* d "a" "b")`
is `(item (item d "a") "b")`, and `(attr* os path sep)` is
`(attr (attr os path) sep)`. Each step is a plain lookup that raises
when a key or attribute is missing. For a chained lookup with a default
or a presence test, the runtime functions
[`getitem*` and `hasitem*`](../runtime/getitemstar-and-hasitemstar.md)
and [`getattr*` and `hasattr*`](../runtime/getattrstar-and-hasattrstar.md)
take the path at run time.

**Examples:**

```koskari
>>> (item* #{"a": #{"b": 2}} "a" "b")
2
>>> (import os)
>>> (attr* os path sep)
"/"
```

The suffix spellings `d["a"]["b"]` and `os.path.sep` read to the same
chains.
