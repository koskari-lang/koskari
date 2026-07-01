# compile/pragma

**Symbol:** `compile/pragma` (surface: `pragma`)

**Syntax:**

- `(pragma [NS/KEY VALUE …] …)` — set one or more settings.
- `(pragma NS/KEY)` — read the current value of one setting.
- `(pragma)` — the module's pragma object.

**Behaviour:** The set form controls module-scoped reader and compiler feature
settings. Each bracketed form is one setting — a slash-namespaced key followed
by positional and/or keyword values; a bare **`[NS/KEY]`** uses the setting's
default. It expands to **`(setq __pragma__ (.rebuild __pragma__ …))`**,
rebuilding the module's pragma; the change is forward-only and does not affect
imported modules. **`(pragma NS/KEY)`** reads the current value (`(item
__pragma__ "NS/KEY")`) and **`(pragma)`** is the `__pragma__` object.

**`(help pragma)`** reports the current settings and every registered setting's
default; **`(help (pragma NS/KEY))`** shows one setting's signature and
documentation.

See [Pragmas and feature settings](../concepts/pragma.md) for the full list of
settings and what each value means.
