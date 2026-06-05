# compile/define-class
**Syntax:** **`(define-class NAME SUPERS BODY …)`**

**Behaviour:** **`(define NAME (class NAME SUPERS BODY …))`** plus **`create-instance-check`**
(defines **`NAME?`** as **`lambda [value] (compile/instance? value NAME)`**).

