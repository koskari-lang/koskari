# Release and versioning

---

## Current version

The current documented package version is **0.1.0**. Treat the `0.x` series as
an early language and tooling series: examples are useful and intentional, but
public compatibility promises are still modest.

---

## Compatibility expectations

For users, the practical rule is:

- Tutorial examples should keep working within the documented version series.
- Reference pages describe the behavior of the current implementation.
- Advanced internals such as bytecode details may change faster than beginner
  syntax and everyday library usage.

When behavior matters for a project, pin the Koskari package version in the
same way you would pin Python package dependencies.

---

## What can change in early releases

During early releases, these areas may evolve:

- Macro and layout conveniences.
- Error wording and diagnostic detail.
- Tooling integrations such as highlighting, LSP behavior, and Sphinx helpers.
- VM and bytecode internals.

User-facing changes should be reflected in the tutorials, guide, reference, and
changelog.

---

## Reporting documentation drift

If an example does not run as shown, include:

1. The Koskari version.
2. The exact command you ran.
3. The full error message or output.
4. The documentation page and example section.

That information makes it much easier to tell whether the implementation,
example, or explanation needs to change.
