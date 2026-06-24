# compile/list-each
**Syntax:** **`(list-each [BINDING SEQ] BODY)`

**Behaviour:** **`define col (#list)`**; **`for-each`** with **`#list-append`**;
return **`(takeq col)`** (clears the gensym accumulator slot after building the
list).

