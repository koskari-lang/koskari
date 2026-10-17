# Architecture map

High-level pipeline from source text to execution:

```mermaid
flowchart LR
  source[Source text] --> reader[Reader and layouts]
  reader --> expand[Macro expansion]
  expand --> compile[Compiler specials]
  compile --> bytecode[KosCode bytecode]
  bytecode --> eval[Evaluator VM]
```

## Python package layout

| Area | Package path | Role |
|------|--------------|------|
| Reader | `koskari.reader` | S-expressions, layout blocks |
| Compiler | `koskari.compile` | Special forms, bytecode emission |
| Syntax | `koskari.syntax` | Syntax-rule transformers |
| Pattern matcher | `koskari.matcher` | Pattern matching for specials and `match` |
| VM | `koskari.lib` | Native types, bytecode, evaluator |
| CLI | `koskari.cli` | REPL and script runner |
| Builtins | `koskari.builtins` | Standard library (virtual module) |

Bootstrap `.ki` sources compile into `koskari.builtins`; that loading
machinery is internal and not part of this user documentation.
