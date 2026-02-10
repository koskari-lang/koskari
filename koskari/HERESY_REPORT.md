# Further heretical code in koskari native lib

Code that was to be copied from sibilant-ceval with faith to the original intent but has diverged. Identified after the func.c purge.

---

## 1. formals.c: Duplicated namespace-spec logic

**Where:** [koskari/lib/formals.c](lib/formals.c): `KosFormalsSpec_NamespaceSpec` (lines 304–401) and `KosFormals_NamespaceSpec` (lines 405–497).

**What is wrong:** Two nearly identical implementations (~95 lines each). Both: count local bindings from formals (pos, pos_only, kwd, kwd_only, star, starstar), handle the empty case (return base_nspec or empty nspec), build a list of local symbols, merge with base nspec’s locals/name/closure_syms, return `KosNamespaceSpec_New(name, lcls, clsr)`. Only the source of the six sequences differs (FormalsSpec vs Formals accessors).

**Faithful shape:** In sibilant-ceval there is only **SibFormalsSpec_NamespaceSpec**; there is no `SibFormals_NamespaceSpec` because the frame gets nspec from `SibByteCode_BASE_NSPEC(code)`. Koskari needs a path from Formals to nspec for the function call path. The heresy is duplication, not the existence of the second entry point. Add a single static helper, e.g. `formals_namespace_spec_impl(PyObject *pos, PyObject *pos_only, PyObject *kwd, PyObject *kwd_only, PyObject *star, PyObject *starstar, PyObject *base_nspec)`, implementing the shared logic once. Have `KosFormalsSpec_NamespaceSpec` and `KosFormals_NamespaceSpec` both call it with the appropriate accessors.

---

## 2. func.c: PyObject_CallMethod in function-call hot path

**Where:** [koskari/lib/func.c](lib/func.c) lines 185–186 inside `func_obtain_call_namespace`.

**What is wrong:** Every function call does `PyObject_CallMethod(nspec, "get_namespace", "O", fn->base_ns)` instead of a C API. Ceval builds the frame namespace in C via `SibFrame_New` / `SibFrame_PushNamespace`; there is no Python method call in that path.

**Faithful shape:** (Already noted as optional in the purge plan.) Add a C API, e.g. `PyObject *KosNamespace_FromSpec(PyObject *nspec, PyObject *parent)` in namespace.h/c, implementing what `nspec_m_namespace` does (e.g. `KosNamespace_NewFull(KosNamespaceSpec_NAME(nspec), parent, KosNamespaceSpec_LOCAL_SYMS(nspec), KosNamespaceSpec_CLOSURE_SYMS(nspec))`). Use it in `func_obtain_call_namespace` so the hot path does not go through `PyObject_CallMethod`.

---

## 3. func.c: KosPyContinuation call path

**Where:** [koskari/lib/func.c](lib/func.c) `continuation_call` (lines 775–818).

**What is wrong:** The type’s `tp_call` is a long static function that inlines: argument validation, `kos_continuation_clone`, `kos_context_new`, `KosCoroutine_New`, and a TODO about the dynamic chain. There is no public **KosPyContinuation_Call**; the pattern of “tp_call delegates to Named_Call” is violated. (Ceval has no Continuation type; this is koskari-specific, but the same design principle applies.)

**Faithful shape:** Add `PyObject *KosPyContinuation_Call(PyObject *self, PyObject *args, PyObject *kwds)` that performs the current logic (validate args, clone, create context, return `KosCoroutine_New(ctx)`). Set `tp_call` to a thin wrapper: `return KosPyContinuation_Call(self, args, kwds);`. Declare `KosPyContinuation_Call` in func.h.

---

## 4. bytecode.c: Duplicated IntEnum creation

**Where:** [koskari/lib/bytecode.c](lib/bytecode.c): `create_opstyle_enum` (around 1118–1141) and `create_opcode_enum` (1148–1178).

**What is wrong:** Two almost identical blocks: import `enum`, create a dict, fill it via a macro (OpStyle vs OpCode), then `PyObject_CallMethod(enum_module, "IntEnum", "sO", name, enum_dict)`, DECREFs, return. Only the enum name and the macro used to fill the dict differ.

**Faithful shape:** One static helper, e.g. `static PyObject *create_int_enum(const char *name, void (*fill)(PyObject *dict))`, or a macro that takes the enum name and the int_enum macro to apply. Call it from both places to remove duplicated import/dict/CallMethod/DECREF logic.

---

## Summary

| Location   | Heresy                                      | Fix                                                                 |
|-----------|---------------------------------------------|---------------------------------------------------------------------|
| formals.c | Duplicated KosFormalsSpec_NamespaceSpec / KosFormals_NamespaceSpec | Single internal helper; both entry points call it with the right accessors |
| func.c    | get_namespace via PyObject_CallMethod       | Add KosNamespace_FromSpec(nspec, parent) and use it in func_obtain_call_namespace |
| func.c    | continuation_call inlines logic, no API     | Add KosPyContinuation_Call; tp_call = KosPyContinuation_Call(self, args, kwds) |
| bytecode.c| Two near-identical IntEnum creation blocks  | Single helper or macro for “create IntEnum with name and filled dict” |

---

## Not heretical (verified)

- **formals.c** `formals_call` / `formals_spec_call`: Both ceval and koskari delegate to `formals_m_apply` / `KosFormals_New`; pattern is faithful.
- **bytecode.c** `code_m_expression` / `code_m_function`: Already delegate to `KosExpression_New` and `KosFunction_NewFromFormals`.
- **ref.c** `KosRef_Deref` / `KosRef_Store` / `KosRef_Clear`: C API exists and is used; the `else` branch using `PyObject_CallMethodObjArgs` is a fallback for subclasses, consistent with ceval’s approach.
- **namespace.c** / **stack.c**: No additional call-path or API heresies identified beyond the get_namespace C API already noted.
