#!/usr/bin/env python3
"""
Extract opcode handlers from koskari/lib/eval.c into koskari/lib/opcode/*.c
fragments. Run from repo root. Reads opcodes.incl for order, eval.c for content.
"""
# GPL and project rules apply; this is a build-time helper.

import re
import os
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
OPCODES_INCL = REPO_ROOT / "koskari/lib/opcodes.incl"
EVAL_C = REPO_ROOT / "koskari/lib/eval.c"
OPCODE_DIR = REPO_ROOT / "koskari/lib/opcode"

GPL_HEADER = """/*
  This library is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 3 of the License, or
  (at your option) any later version.

  This library is distributed in the hope that it will be useful, but
  WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this library; if not, see <http://www.gnu.org/licenses/>.
*/

"""


def get_opcode_order():
    content = OPCODES_INCL.read_text()
    return re.findall(r"^OP\(([A-Za-z0-9_]+)\s*,", content, re.MULTILINE)


def find_handler_section(eval_content):
    start_marker = "/* === NOOP / LABEL / POSITION === */"
    end_marker = "/* Dispatch table generated from opcodes.incl"
    start = eval_content.find(start_marker)
    end = eval_content.find(end_marker)
    if start == -1 or end == -1:
        raise SystemExit("Could not find handler section boundaries in eval.c")
    return eval_content[start:end], start, end


def extract_docstring_and_handler(section, opname):
    """Find KOS_OP_HANDLER(OPNAME) in section; return (docstring, handler_text) or None."""
    needle = "KOS_OP_HANDLER(" + opname + ")"
    pos = section.find(needle)
    if pos == -1:
        return None
    # Find the opening brace of the handler body
    brace_start = section.find("{", pos)
    if brace_start == -1:
        return None
    depth = 1
    i = brace_start + 1
    while i < len(section) and depth > 0:
        if section[i] == "{":
            depth += 1
        elif section[i] == "}":
            depth -= 1
        i += 1
    handler_end = i  # position after closing }
    handler_text = section[pos:handler_end]
    # Docstring: last /** ... */ or /* ... */ block before this handler
    before = section[:pos].rstrip()
    docstring = ""
    idx = before.rfind("/**")
    if idx == -1:
        idx = before.rfind("/*")
    if idx != -1:
        end_com = before.find("*/", idx) + 2
        if end_com > 1:
            docstring = before[idx:end_com] + "\n\n"
    return docstring, handler_text


def expanded_op_unary(name, func):
    return '''/**
   %s: pop TOS, call %s, push result.
*/
KOS_OP_HANDLER(%s) {
    PyObject *val = KosStackBlock_PopRef(&CURRENT_STACK(ctx));
    if (! val) return -1;
    PyObject *res = %s(val);
    Py_DECREF(val);
    if (! res) return -1;
    (void) insn;
    return push_result(ctx, res);
}
''' % (name, func, name, func)


def expanded_op_unary_predicate(name, pred_fn):
    return '''/**
   %s: pop TOS, call %s, push Py_True/Py_False.
*/
KOS_OP_HANDLER(%s) {
    PyObject *val = KosStackBlock_PopRef(&CURRENT_STACK(ctx));
    if (! val) return -1;
    int r = %s(val);
    Py_DECREF(val);
    if (r < 0) return -1;
    PyObject *res = (r ? Py_True : Py_False);
    Py_INCREF(res);
    (void) insn;
    return push_result(ctx, res);
}
''' % (name, pred_fn, name, pred_fn)


def expanded_op_binary_num(name, func):
    return '''/**
   %s: pop right, left; call %s; push result.
*/
KOS_OP_HANDLER(%s) {
    if (insn->arg != 2) {
      PyErr_SetString(PyExc_RuntimeError, "%s: only arg 2 supported");
      return -1;
    }
    PyObject *right = KosStackBlock_PopRef(&CURRENT_STACK(ctx));
    if (! right) return -1;
    PyObject *left = KosStackBlock_PopRef(&CURRENT_STACK(ctx));
    if (! left) { Py_DECREF(right); return -1; }
    PyObject *res = %s(left, right);
    Py_DECREF(left);
    Py_DECREF(right);
    if (! res) return -1;
    return push_result(ctx, res);
}
''' % (name, func, name, name, func)


MACRO_EXPANSIONS = {
    "OBJECT_REPR": ("OP_UNARY", "PyObject_Repr", expanded_op_unary("OBJECT_REPR", "PyObject_Repr")),
    "OBJECT_ITER": ("OP_UNARY", "PyObject_GetIter", expanded_op_unary("OBJECT_ITER", "PyObject_GetIter")),
    "OBJECT_BIT_INVERT": ("OP_UNARY", "PyNumber_Invert", expanded_op_unary("OBJECT_BIT_INVERT", "PyNumber_Invert")),
    "OBJECT_DIR": ("OP_UNARY", "PyObject_Dir", expanded_op_unary("OBJECT_DIR", "PyObject_Dir")),
    "OBJECT_NOT": ("OP_UNARY_PREDICATE", "PyObject_Not", expanded_op_unary_predicate("OBJECT_NOT", "PyObject_Not")),
    "OBJECT_TRUTH": ("OP_UNARY_PREDICATE", "PyObject_IsTrue", expanded_op_unary_predicate("OBJECT_TRUTH", "PyObject_IsTrue")),
    "OBJECT_MODULO": ("OP_BINARY_NUM", "PyNumber_Remainder", expanded_op_binary_num("OBJECT_MODULO", "PyNumber_Remainder")),
    "OBJECT_DIVMOD": ("OP_BINARY_NUM", "PyNumber_Divmod", expanded_op_binary_num("OBJECT_DIVMOD", "PyNumber_Divmod")),
}


def main():
    opcodes = get_opcode_order()
    eval_content = EVAL_C.read_text()
    section, sect_start, sect_end = find_handler_section(eval_content)

    OPCODE_DIR.mkdir(parents=True, exist_ok=True)

    for opname in opcodes:
        fname = opname.lower() + ".c"
        outpath = OPCODE_DIR / fname

        if opname in MACRO_EXPANSIONS:
            _, _, body = MACRO_EXPANSIONS[opname]
            content = GPL_HEADER + body
        else:
            result = extract_docstring_and_handler(section, opname)
            if result is None:
                print("Warning: no handler found for", opname)
                content = GPL_HEADER + "\n/* Opcode %s: handler not found in eval.c */\nKOS_OP_HANDLER(%s) {\n  return op_unimplemented_stub(ctx, out_ev, insn);\n}\n" % (opname, opname)
            else:
                docstring, handler_text = result
                content = GPL_HEADER + docstring + handler_text + "\n"

        outpath.write_text(content)

    print("Wrote %d opcode files to %s" % (len(opcodes), OPCODE_DIR))


if __name__ == "__main__":
    main()
