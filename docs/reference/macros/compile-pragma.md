# compile/pragma

**Symbol:** `compile/pragma` (surface: `pragma`)

**Syntax:** `(pragma [NS/KEY VALUE …] …)`

**Behaviour:** Sets module-scoped reader and compiler feature settings. Each
bracketed form is one setting — a slash-namespaced key followed by positional
and/or keyword values; a bare **`[NS/KEY]`** uses the setting's default. Expands
to **`(setq __pragma__ (.rebuild __pragma__ …))`**, rebuilding the module's
pragma. The change is forward-only and does not affect imported modules.
**`(help pragma)`** reports the current settings.

See [Pragmas and feature settings](../concepts/pragma.md) for the full list of
settings and what each value means.
