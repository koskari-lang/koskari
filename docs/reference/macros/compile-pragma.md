# compile/pragma

**Syntax:** `(pragma [NS/KEY VALUE ...] ...)` to set, `(pragma NS/KEY)` to
read one setting, `(pragma)` for the module's pragma object.

**Behaviour:** The set form controls the module's reader and compiler
settings. Each bracketed form is one setting: a slash-namespaced key
followed by positional or keyword values, and a bare `[NS/KEY]` selects
the setting's default. It replaces the module's `__pragma__` with one
rebuilt to carry the new settings, behaving like `(setq __pragma__ (.rebuild __pragma__ ...))`,
so the change applies to everything read and compiled after it in this
module and to nothing imported. Values are expressions: a literal symbol is quoted, while
booleans, numbers, and strings are bare.

`(pragma NS/KEY)` returns the current value of the setting from
`__pragma__`, behaving like `(item __pragma__ "NS/KEY")`.
`(help pragma)` lists every setting with its default, and
`(help (pragma NS/KEY))` documents one.

**Examples:**

```koskari
(pragma [compiler/call-brackets 'error]
        [reader/brackets False])

(pragma [compiler/pack-module False])
```

```koskari
>>> (pragma compiler/call-brackets)
<BracketCalls.WARN: 'warn'>
>>> (pragma reader/layout)
True
```

**See also:** [Pragmas and feature settings](../../guide/pragma.md) for
every setting and its values.
