# This library is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 3 of the License, or
# (at your option) any later version.
#
# This library is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
# General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this library; if not, see <http://www.gnu.org/licenses/>.


"""
tests.reproducers.test_procedural_hygiene_contract

The hygiene contract of a procedural macro arm: its output is marked
except for the caller's pieces it emits inside a Datum, whether the
capture itself or a piece re-wrapped with datum, and a name it builds
from a label reads the label's base

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""

import unittest

import pytest

from koskari.module import load_module_source_async, new_module


pytestmark = pytest.mark.phase_reproducers


async def load(name, src):
    module = new_module(name)
    vars(module)["__file__"] = f"<{name}>"
    await load_module_source_async(module, src)
    return vars(module)


SPELL = """
(syntax spell
  (macro-rules [] [(spell s) (str s.datum)]))
"""


class TestCallerPieces(unittest.IsolatedAsyncioTestCase):
    """
    A caller's form passed back out of a procedural arm stays the
    caller's while it travels in a Datum, and is the macro's own once
    emitted bare
    """

    SRC = SPELL + """
(syntax pass-datum (macro-rules [] [(pass-datum e) e]))
(syntax pass-rewrap
  (macro-rules [] [(pass-rewrap e) (datum e.datum)]))
(syntax pass-rebuilt
  (macro-rules []
    [(pass-rebuilt e) (datum (cons (car e.datum) (cdr e.datum)))]))
(syntax pass-bare (macro-rules [] [(pass-bare e) e.datum]))

(var as-datum (pass-datum (spell foo)))
(var rewrapped (pass-rewrap (spell foo)))
(var rebuilt (pass-rebuilt (spell foo)))
(var bare (pass-bare (spell foo)))
"""

    async def test_capture_emitted_as_its_datum(self):
        ns = await load("phc_datum", self.SRC)
        self.assertEqual(ns["as-datum"], "foo")


    async def test_peeled_form_rewrapped(self):
        ns = await load("phc_rewrap", self.SRC)
        self.assertEqual(ns["rewrapped"], "foo")


    async def test_peeled_form_rebuilt_inside_datum(self):
        ns = await load("phc_rebuilt", self.SRC)
        self.assertEqual(ns["rebuilt"], "foo")


    async def test_peeled_form_emitted_bare_is_marked(self):
        ns = await load("phc_bare", self.SRC)
        self.assertTrue(ns["bare"].startswith("foo#"), ns["bare"])


class TestDatumTail(unittest.IsolatedAsyncioTestCase):
    """
    A Datum in the cdr of a procedural arm's output is the caller's
    material and reaches the compiler unboxed
    """

    SRC = """
(syntax rebuild
  (macro-rules []
    [(rebuild e) (cons (datum (car e.datum)) (datum (cdr e.datum)))]))

(var total (rebuild (+ 1 2)))
"""

    async def test_datum_tail(self):
        ns = await load("phc_tail", self.SRC)
        self.assertEqual(ns["total"], 3)


class TestBuiltName(unittest.IsolatedAsyncioTestCase):
    """
    A dispatcher builds the name of the macro it delegates to from a
    type tag, a label, so the tag's base names the macro whether the
    caller or a template wrote the tag
    """

    SRC = """
(syntax job/cron (syntax-rules [] [(job/cron x) x]))
(syntax job
  (macro-rules []
    [(job tp rest ...) `(,(datum (symbol f"job/{tp.datum.base}")) ,@rest)]))
(syntax pass-rewrap
  (macro-rules [] [(pass-rewrap e) (datum e.datum)]))
(syntax cron-job (syntax-rules [] [(cron-job x) (job cron x)]))

(var direct (job cron 1))
(var passed (pass-rewrap (job cron 2)))
(var templated (cron-job 3))
"""

    async def test_tag_written_by_caller(self):
        ns = await load("phc_name_direct", self.SRC)
        self.assertEqual(ns["direct"], 1)
        self.assertEqual(ns["passed"], 2)


    async def test_tag_written_by_template(self):
        ns = await load("phc_name_template", self.SRC)
        self.assertEqual(ns["templated"], 3)


class TestPassThroughDispatch(unittest.IsolatedAsyncioTestCase):
    """
    A collecting macro passes its job entries through as their Datums;
    each entry dispatches to a procedural variety that tags its
    sub-items through a template, and the names the caller wrote are
    bound at the use site
    """

    SRC = """
(syntax sub
  (syntax-rules [] [(sub (tp name) v) (.append name v)]))
(syntax with-subitems
  (syntax-rules []
    [(with-subitems tag (subhead subrest ...) ...)
     (begin (subhead tag subrest ...) ...)]))
(syntax job/kind
  (macro-rules []
    [(job/kind name sub ...)
     `(begin (var ,name #[]) (with-subitems (kind ,name) ,@sub))]))
(syntax job
  (macro-rules []
    [(job tp rest ...) `(,(datum (symbol f"job/{tp.datum.base}")) ,@rest)]))
(syntax jobset
  (macro-rules []
    [(jobset jsname entry ...)
     (var jobforms #[])
     (var regs #[])
     (for-each [e entry]
       (jobforms.append e)
       (regs.append `(.append ,jsname ,(datum (car (cdr (cdr e.datum)))))))
     `(begin ,@jobforms (var ,jsname #[]) ,@regs)]))

(jobset js (job kind a (sub 1) (sub 2)) (job kind b (sub 3)))
"""

    async def test_entries_register_under_caller_names(self):
        ns = await load("phc_dispatch", self.SRC)
        self.assertEqual(ns["js"], [[1, 2], [3]])
        self.assertEqual(ns["a"], [1, 2])
        self.assertEqual(ns["b"], [3])


class TestClassPredicateInTemplate(unittest.IsolatedAsyncioTestCase):
    """
    A class a template defines has a predicate named for the class
    under the same marks: the template's own call reaches it, and the
    caller's binding of the same name is not captured
    """

    SRC = """
(var Widget? (lambda [v] "use-site"))

(syntax make-widget
  (syntax-rules []
    [(make-widget out)
     (begin
       (define-class Widget [])
       (var out (Widget? (Widget))))]))

(make-widget checked)
(var still (Widget? 1))
"""

    async def test_template_reaches_its_predicate(self):
        ns = await load("phc_predicate", self.SRC)
        self.assertIs(ns["checked"], True)


    async def test_caller_predicate_not_captured(self):
        ns = await load("phc_predicate_caller", self.SRC)
        self.assertEqual(ns["still"], "use-site")


class TestPragmaKeyInTemplate(unittest.IsolatedAsyncioTestCase):
    """
    A pragma key is a pure key: a template that writes one sets and
    reads the key by its plain spelling
    """

    SRC = """
(syntax set-and-read
  (syntax-rules []
    [(set-and-read out)
     (begin
       (pragma [compiler/call-brackets 'warn])
       (var out (pragma compiler/call-brackets)))]))

(set-and-read got)
"""

    async def test_template_reads_the_key_it_set(self):
        ns = await load("phc_pragma", self.SRC)
        self.assertEqual(ns["got"].name, "WARN")


# The end.
