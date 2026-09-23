# Koskari Design: namespace externs

## Status

Concept only, recorded 2026-09-24 during the hygiene rework. Not
planned for branch `hygiene-rework`.

## Problem

A template's free name resolves at the use site. A caller who binds
the same name locally around a macro use captures it, and a macro
whose module is not imported at the use site cannot reach its own
module's bindings except by emitting an import. Full hygiene resolves
a template's free name in the macro's definition environment. For a
module-level macro that environment has a name; for one defined
inside a let or a function body it is a frame, which no compiled form
can address, so the frame case is out of scope and keeps today's rule.

## Concept

`NamespaceSpec` gains a fourth list beside `local_syms`,
`closure_syms`, and `global_syms`: `extern_syms`, each entry the
local spelling the code reads under and the fully qualified module
and attribute it comes from. The compiler emits ordinary reads of the
local spelling; the runtime supplies the value from the named module.

Two runtime shapes, to be chosen by measurement:

- Bind at frame creation, as closure names are bound from the
  enclosing namespace today: import the module, read the attribute,
  store the cell. Simple, one lookup per call.
- Resolve lazily through a read opcode with a per-site cache slot, so
  a frame pays only for externs it reads. Fits the existing cache
  framework; needs the compiler to know at emission which reads are
  externs, which the gensym gives it.

## Where the qualifier comes from

A scoped name reaches the compiler as a gensym. The compiler needs to
know, for a gensym no visible declaration matches, which module the
template that wrote it lives in and whether that module binds the
base. Two ways to carry that:

- The gensym carries the module qualifier, set by the transformer at
  expansion from its own definition module. Marks stay lexical; the
  qualifier rides with the symbol through captures and nested
  expansions.
- `note_mark` records the transformer per expansion mark, and the
  compiler maps a gensym's newest mark to a module. No change to the
  atom; the mapping is per compile turn.

Either way the transformer must know its module, which the
`define-syntax` lowering can pass to the builder.

## Resolution rule

`resolve_marks` runs as it does. A scoped name a visible declaration
matches is that declaration, so a template's own binders are
unaffected. A scoped name nothing visible declares, whose qualifier
names a module that binds the base, becomes an extern entry; one
whose module does not bind the base falls to the bare name as today.

## Serialization

An extern entry is two strings and serializes in the `NamespaceSpec`
pool record. Loading a `.kio` that reads an extern requires the named
module to be importable at load, the same dependency an emitted
import has. This is linking against the macro's module, and it is the
whole capability: a `.kio` cannot hold a reference to a binding cell.

## Non-goals

Capturing a frame environment for a macro defined under a let. The
reference cell that would make it work in-process has no coordinate
in a `.kio`, and no other Lisp offers a globally usable macro that
reaches a let's bindings from outside it.

## Open

- Which cell a creation-time bind captures when the module binds the
  name lazily or rebinds it later.
- Whether the outer-import macros in the tree become extern users or
  stay as they are.
- Cost on frame creation for functions that read no externs, which
  decides between the two runtime shapes.
