# compile/define-dataclass
**Syntax:** **`(define-dataclass NAME (members …))`** or with **`SUPERS`**.

**Behaviour:** Imports **`dataclass`** / **`field`**, emits **`@dataclass class`**, then
**`create-instance-check`** for **`NAME?`**.

