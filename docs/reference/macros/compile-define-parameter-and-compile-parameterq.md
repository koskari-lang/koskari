# compile/define-parameter and compile/parameterq
**Syntax:** **`(define-parameter NAME [DEFAULT])`**, **`(parameterq NAME [DEFAULT])`**.

**Behaviour:** **`compile/define`** + **`ParamRef`** binding; **`parameterq`** → **`parameter`**.

**Scope guidance:** The bound `ParamRef` is a re-resolving accessor -- every
read and store finds the innermost binding of the context it runs in -- so a
`define-parameter` binding is safe to use anywhere, including module scope.
Parameter *state* is another matter: each context owns its bindings, forked
from its creator at creation, and each top-level module form evaluates in
its own context. A value stored at module scope therefore does not persist
into later forms or into the importing program. Set defaults with the
`DEFAULT` argument (or a host-side pre-define in a `Domain`), and treat
stored parameter values as scoped to the function or task that stored them.

The related `parameter-cell` builtin returns the live binding cell itself.
A held cell is a shared object: every holder sees every store, including
after the extent that installed it has exited. It does not track later
`parameterize` rebinding of the name -- capture it deliberately, for
storage channels or hot loops, and use `parameterq` / `parameter` for
ordinary dynamic reads.

