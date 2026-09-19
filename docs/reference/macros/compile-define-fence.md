# compile/define-fence

**Syntax:** `(define-fence NAME [RAW META] BODY ...)`

**Behaviour:** Defines a fence handler. NAME is the binding the fence dispatch
looks up, normally `fence/<format>`; RAW and META name the handler's two
parameters. The macro expands to a `define` of NAME bound to a `FenceHandler`
wrapping the body as a demanding function, so the handler runs at compile time.

RAW is the fence body text, verbatim. META is a dict carrying `name`,
`filename`, `line`, `col`, and `options`, where `options` is the opener-line
keyword plist as a dict. The body returns Koskari source, which is compiled in
place of the fence -- usually a quasiquoted template.

A handler validates its own options and raises `FenceError` for ones it does
not accept. Handlers must not suspend.

**Examples:**

```koskari
>>> define-fence fence/shout [raw meta]:
...   `(.upper ,(raw.strip))
```

Used as a triple-backtick fence:

````koskari
define loud ```shout
hello there
```
````

```koskari
>>> loud
"HELLO THERE"
```

A handler that rejects unknown options and builds a list:

```koskari
>>> define-fence fence/lines [raw meta]:
...   define options (meta.get "options")
...   when options:
...     raise (FenceError f"fence/lines got unknown options: {(sorted options)}")
...   `(#list ,@(map str (.splitlines (raw.strip))))
```

````koskari
define ls ```lines
a
b
```
````

```koskari
>>> ls
#["a" "b"]
```

**See also:** [Fence handlers](../fences/index.md),
[`compile/apply-fence`](../specials/compile-apply-fence.md),
[Fences](../../guide/fences.md).
