# Where next

You can write a `.ki` file with the shapes you already use in Python:
functions, classes, modules, exceptions, comprehensions, and imports in
both directions.

:::{dialogue}
Where are whole programs?
+++
The [worked examples](examples/index.md): countdown, a script entry
point, five Fibonaccis, a dataclass with a property, and property
accessors. Each is one file with its output.
:::

:::{dialogue}
What should I read in the guide?
+++
[Koskari and s-expressions](../../guide/layout-and-s-expressions.md)
first, for the reader rules beside the forms they produce. Then
[Name resolution](../../guide/name-resolution-and-prefixes.md) for why
`+` is both an opcode and a function, and
[Demand, async, and defer](../../guide/demand-async-and-defer.md) before
you hand a Koskari function to a Python library.
:::

:::{dialogue}
Which reference pages matter day to day?
+++
[Layouts](../../reference/layouts/index.md) for the block heads you
write, [Macros](../../reference/macros/index.md) for `define`, `set!`,
and the `-each` family, and the [Glossary](../../reference/glossary.md).
:::

:::{dialogue}
I want to embed Koskari in a Python app.
+++
[Hosting Koskari from Python](../../guide/hosting-from-python.md)
covers the import hook, running a file into a namespace, and awaiting
Koskari functions from Python.
[Packaging a Koskari application](../../guide/packaging.md) ships a
`.ki` program as a pip command.
:::
