# Koskari Macro Hygiene: Middle-Ground Design

**Status:** Proposed (design specification; not yet implemented)

This document describes a hygiene model for procedural macros that reuses the
existing binding-aware compiler pass. Macros may return ordinary source forms plus
**syntax boxes**: opaque segments of source (any expression shape, including full
subtrees) that represent the forms the macro consumed from the call. Hygiene
applies only to **non-boxed** regions of the expanded tree; boxed payloads are
substituted back in at the very end, after binding normalization.

---

## Related documents

- **Matcher hygiene metadata**: [matcher-hygiene-extensions.md](../api/matcher-hygiene-extensions.md) — how specials declare bindings and scopes for generic hygiene discovery
- **Compile-time objects**: [compile-time-objects.md](compile-time-objects.md) — compile-time dispatch and registration model
- **Macros overview**: [archive/macros.md](../archive/macros.md) — historical / broader macro notes

---

## 1. Overview

Koskari currently supports:

- Declarative `define-syntax` (data-driven pattern transforms)
- A binding-aware compiler pass that:
  - identifies declaring forms
  - determines lexical scopes
  - rewrites bindings into gensymmed identities

This document proposes a **middle-ground macro hygiene model** that avoids
introducing a full syntax-object system while still enabling procedural macros
with practical hygiene.

---

## 2. Problem statement

We need to support:

- Procedural macros (arbitrary compile-time logic)
- Identifier transformation (e.g. `foo/bar` → `foo_bar`)
- Safe introduction of bindings (no accidental capture)

But we want to avoid:

- A full secondary syntax-object system
- Complex hygiene machinery duplicated from Scheme

---

## 3. Key insight

We already have a **binding-aware transformation pass**.

Instead of embedding hygiene into macro expansion itself, we can:

> Treat macro output as *intermediate source*, then run it through the existing binding/scope transformer.

Hygiene is therefore achieved by the **post-expansion binding pass**, not the
macro expander.

---

## 4. Core design

### 4.1 Macro output model

Macros return:

- Ordinary source forms (pairs, symbols, literals) — the **macro-produced**
  skeleton, subject to the usual binding/hygiene pass
- PLUS **syntax boxes** wrapping segments of **call-site** source the macro read

Boxes are not limited to identifiers or leaves. A box may hold:

- a symbol, literal, or small form
- an arbitrary nested expression or **entire subtree** (e.g. a body containing
  `lambda`, `let`, and local bindings)

The expander may **split**, **reorder**, or **duplicate** boxes as the macro
logic requires; each box still denotes “this chunk came from the invocation and
must not be rewritten by hygiene while boxed.”

### 4.2 Syntax box

A syntax box is a distinct runtime/compiler type used only during expansion.

It represents:

> This segment is call-site material (or macro-chosen payload carried like it);
> do not treat its interior as subject to the binder’s gensym pass while it
> remains boxed.

**Properties**

Each syntax box contains:

- underlying source segment (any expression type the reader/compiler already
  represents: pairs, atoms, nested forms)
- optional kind/tag (interpretation may also be driven by payload and compiler
  context)

### 4.3 Single syntax box type

There is only **one** syntax box type.

It is a singular placeholder/marker object used during macro expansion and
finalization. It does **not** imply a family of separate boxed node types.
Instead, all boxed values use the same representation, and any interpretation
differences are handled by compiler logic or by the boxed payload itself.

This keeps the model intentionally minimal:

- one special type
- one late-resolution mechanism
- no taxonomy of syntax-object variants

The box exists only to mean:

> Keep this segment opaque to the hygiene/binding pass; splice its payload into
> the final tree only after normalization of everything around it.

---

## 5. Compilation pipeline

### Step 1: Macro expansion

Macro returns a tree containing:

- **Unboxed** structure: the macro’s own emitted forms (templates, wrappers,
  fresh names introduced as ordinary symbols, etc.)
- **Boxed** segments: arguments or fragments read from the call (possibly whole
  bodies), which may be arbitrarily large subtrees

Example: a macro that receives a body with internal `lambda`/`let` can place that
body in a box so inner binders are **not** renamed by the post-expansion hygiene
pass; only the unboxed wrapper the macro built is processed by the binder.

The important point is not multiple box categories, but that the singular syntax
box type marks “hygiene must not descend here; substitute at the end.”

### Step 2: Binding/hygiene pass (existing, scoped)

Run the existing declaring-form matcher and scope transformer on the expanded
tree with this rule:

- **Non-boxed** regions: unchanged behavior — `let`, `lambda`, etc. are
  recognized, scopes determined, gensym substitution applied
- **Boxed** regions: **opaque**. The pass does not treat the interior of a box
  as binding structure to rewrite. A box is a single opaque node for hygiene
  purposes even if its payload is a large subtree

So: the tree walk can see box **nodes**, but **hygiene does not recurse into box
payloads** for binding discovery or renaming; only unboxed structure is processed
like today’s pass.

### Step 3: Finalization pass

After binding normalization of the unboxed skeleton:

- **Substitute** boxed payloads into their positions (unwrap the singular box
  type)
- The spliced-in source is then ordinary tree data for subsequent compilation

How unwrapped segments are treated afterward is normal compiler logic. Until
finalization, boxed vs unboxed must remain distinguishable so hygiene cannot
accidentally flatten or rename inside boxes.

---

## 6. Design constraints

### 6.1 Hygiene applies only to non-boxed content

Anything the macro emits **without** wrapping in a box is subject to the normal
declaring-form and gensym behavior.

Anything kept **inside** a box skips that rewriting until after the surrounding
structure has been normalized, then is spliced in verbatim (from the box’s
payload).

**Implication:** If the macro **wants** a generated `let` or `lambda` to
participate in hygiene (e.g. fresh temps), it should emit that structure
**unboxed**. If it **wants** to preserve call-site bindings and names exactly as
written, it should keep that subtree **boxed** through Step 2.

### 6.2 Boxes may hold any source segment

There is no restriction that boxes contain only leaves. Whole argument
expressions, macro bodies, or nested binding forms may be boxed. The expander may
split one logical argument into multiple boxes or combine boxes if the macro
implementation does so.

### 6.3 Boxes are distinct types

They must NOT degrade into plain pairs or symbols early.

All passes between expansion and finalization must be able to detect:

- boxed vs unboxed

so that hygiene never recurses into boxed payloads by mistake.

---

## 7. Relationship to existing system

**Existing**

- declaring-form matcher
- scope inference
- gensym substitution

**New**

- syntax box type
- final unwrap pass

**No duplication of**

- binding rules
- scope logic

---

## 8. What this solves

- **Safe local bindings in macro-generated code** — unboxed template structure
  is still handled by existing gensym logic
- **Preserving call-site code** — bodies or fragments that include `lambda`,
  `let`, and local definitions stay unmunged by hygiene when carried in boxes
  until final substitution
- **Identifier transformation** — call-site identifiers or macro-built names can
  be carried in boxes when they must not be rewritten with the surrounding scope
- **Procedural power** — arbitrary computation, splitting/recombining boxed
  segments, and structured output
- **Minimal new machinery** — no full syntax-object taxonomy; one box type plus
  late splice

---

## 9. Known limitations

This approach does NOT fully solve:

- Definition-site vs call-site resolution (unless extended)
- Fine-grained lexical provenance tracking
- Complex nested hygiene interactions across macro boundaries

These can be addressed later if needed.

---

## 10. Mental model

Think of macro expansion as producing:

> A skeleton of macro-written source, with **holes** filled by syntax boxes
> holding call-site (or otherwise preserved) segments—any size.

Then:

- Binding pass → normalizes **only the unboxed skeleton** (scopes, gensyms)
- Final pass → **splices** box payloads into place; only then is the full tree
  ordinary source for the rest of the compiler

---

## 11. Future extensions

If needed later, the boxed payload can become richer without introducing more
box *types*.

For example, the payload could optionally carry:

- source provenance
- requested freshness
- definition-site hints

But the external model remains:

- one box type
- one placeholder abstraction

---

## 12. Summary

This design provides:

- Procedural macros
- Hygiene on macro-generated structure without destroying intentional call-site
  subtrees
- Reuse of existing compiler machinery for everything **outside** boxes

By introducing only one core abstraction:

> **Syntax boxes as opaque segments (any shape) that bypass hygiene until a
> final splice**

This is likely the lowest-complexity path to a robust macro system in Koskari.
