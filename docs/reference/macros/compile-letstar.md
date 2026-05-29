# compile/let*
**Syntax:** **`(let* [[NAME INIT] …] body …)`**

**Behaviour:** Nested **`let`** (empty, single, or recursive **`(let [first] (let* [rest …] body …))`**).

