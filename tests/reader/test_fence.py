# This library is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 3 of the License, or
# (at your option) any later version.
#
# This library is distributed in the hope that it will be useful, but
# WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
# General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this library; if not, see <http://www.gnu.org/licenses/>.

"""
tests.reader.test_fence

Unit tests for the triple-backtick fence reader macro, which lowers
fenced foreign source to (apply-fence NAME RAW META) forms.

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
"""

from unittest import IsolatedAsyncioTestCase

import pytest

from koskari.bootstrap import _BUILTIN_REGISTRY
from koskari.compile.helpers import pycons
from koskari.exceptions import ReaderEOF, ReaderSyntaxError
from koskari.lib import keyword, symbol
from koskari.pragma import ModulePragma
from koskari.reader import (
    READ_EOF, IndentAwareStream, LayoutReader, Reader, default_reader,
    source_str)
from koskari.types import get_position


pytestmark = pytest.mark.phase_infra


def parse_source(src_str):
    reader = default_reader
    stream = source_str(src_str, "<unittest>")
    return reader.read(stream)


def parse_layout_all(src_str, **defns):
    builtins = dict(_BUILTIN_REGISTRY)
    builtins.update(defns)

    reader = LayoutReader(builtins)
    stream = IndentAwareStream.from_source(source_str(src_str, "<unittest>"))

    forms = []
    while True:
        form = reader.read(stream)
        if form is READ_EOF:
            break
        forms.append(form)
    return forms


def fence_form(name, raw, line, col=0, options=()):
    """
    Expected (apply-fence ...) tree as nested tuples for pycons
    """

    return (symbol("apply-fence"), symbol(name), raw,
            (keyword("filename"), "<unittest>",
             keyword("line"), line,
             keyword("col"), col,
             keyword("options"), tuple(options)))


class TestFenceRead(IsolatedAsyncioTestCase):
    """
    Fence lowering on the plain s-expression reader surface
    """

    async def test_simple_fence(self):
        src = '```bash\necho hi\n```\n'
        result = parse_source(src)
        expected = pycons(fence_form("bash", "echo hi\n", line=2))
        self.assertEqual(result, expected)

    async def test_fence_name_spaced(self):
        attached = parse_source('```bash\necho hi\n```\n')
        spaced = parse_source('``` bash\necho hi\n```\n')
        self.assertEqual(attached, spaced)

    async def test_fence_position(self):
        result = parse_source('```bash\necho hi\n```\n')
        self.assertEqual(get_position(result), (1, 0))

    async def test_fence_multiline_body(self):
        src = "```bash\ncd foo/bar\necho hi\n\necho done\n```\n"
        result = parse_source(src)
        expected = pycons(fence_form(
            "bash", "cd foo/bar\necho hi\n\necho done\n", line=2))
        self.assertEqual(result, expected)

    async def test_fence_in_pair(self):
        src = '(list ```bash\necho hi\n```\n)'
        result = parse_source(src)
        expected = pycons((
            symbol("list"),
            fence_form("bash", "echo hi\n", line=2)))
        self.assertEqual(result, expected)

    async def test_fence_options(self):
        src = '```bash env: prod retries: 3\necho hi\n```\n'
        result = parse_source(src)
        expected = pycons(fence_form(
            "bash", "echo hi\n", line=2,
            options=(keyword("env"), symbol("prod"),
                     keyword("retries"), 3)))
        self.assertEqual(result, expected)

    async def test_fence_option_string_value(self):
        src = '```bash cwd: "/tmp"\necho hi\n```\n'
        result = parse_source(src)
        expected = pycons(fence_form(
            "bash", "echo hi\n", line=2,
            options=(keyword("cwd"), "/tmp")))
        self.assertEqual(result, expected)

    async def test_fence_opener_comment(self):
        src = '```bash ; a comment\necho hi\n```\n'
        result = parse_source(src)
        expected = pycons(fence_form("bash", "echo hi\n", line=2))
        self.assertEqual(result, expected)

    async def test_fence_dedent(self):
        src = "```text\n    line1\n      line2\n    ```\n"
        result = parse_source(src)
        expected = pycons(fence_form(
            "text", "line1\n  line2\n", line=2, col=4))
        self.assertEqual(result, expected)

    async def test_fence_dedent_underindented(self):
        src = "```text\n    aaa\n  bbb\n    ```\n"
        result = parse_source(src)
        expected = pycons(fence_form(
            "text", "aaa\nbbb\n", line=2, col=4))
        self.assertEqual(result, expected)

    async def test_fence_body_backticks_not_closer(self):
        src = "```text\n``` not a closer\n````\n```\n"
        result = parse_source(src)
        expected = pycons(fence_form(
            "text", "``` not a closer\n````\n", line=2))
        self.assertEqual(result, expected)

    async def test_fence_missing_name(self):
        with self.assertRaises(ReaderSyntaxError):
            parse_source("```\necho hi\n```\n")

    async def test_fence_unterminated(self):
        with self.assertRaises(ReaderEOF):
            parse_source('```bash\necho hi\n')

    async def test_fence_eof_after_name(self):
        with self.assertRaises(ReaderEOF):
            parse_source("```bash")

    async def test_fence_option_missing_value(self):
        with self.assertRaises(ReaderSyntaxError):
            parse_source("```bash env:\necho hi\n```\n")

    async def test_fence_option_not_keyword(self):
        with self.assertRaises(ReaderSyntaxError):
            parse_source("```bash 3\necho hi\n```\n")

    async def test_fence_option_multiline_value(self):
        with self.assertRaises(ReaderSyntaxError):
            parse_source("```bash env: (a\nb)\necho hi\n```\n")

    async def test_quasiquote_unaffected(self):
        result = parse_source("`foo")
        expected = pycons((symbol("quasiquote"), symbol("foo")))
        self.assertEqual(result, expected)

        result = parse_source("`(a b)")
        expected = pycons((symbol("quasiquote"),
                           (symbol("a"), symbol("b"))))
        self.assertEqual(result, expected)

    async def test_double_backtick_unaffected(self):
        result = parse_source("``foo")
        expected = pycons((symbol("quasiquote"),
                           (symbol("quasiquote"), symbol("foo"))))
        self.assertEqual(result, expected)


class TestFenceLayoutRead(IsolatedAsyncioTestCase):
    """
    Fence lowering on the layout reader surface
    """

    async def test_define_script_example(self):
        src = (
            "define script ```bash\n"
            "cd foo/bar/baz\n"
            "curl -s https://example.org/\n"
            "```\n"
            "(script)\n")

        forms = parse_layout_all(src)
        self.assertEqual(len(forms), 2)

        expected = pycons((
            symbol("define"), symbol("script"),
            fence_form(
                "bash", "cd foo/bar/baz\ncurl -s https://example.org/\n",
                line=2)))
        self.assertEqual(forms[0], expected)

        self.assertEqual(forms[1], pycons((symbol("script"),)))

    async def test_fence_in_layout_block_body(self):
        src = (
            "def run []:\n"
            "  process ```bash\n"
            "echo hi\n"
            "```\n")

        forms = parse_layout_all(src)
        self.assertEqual(len(forms), 1)

        expected = pycons((
            symbol("def"), symbol("run"), (),
            (symbol("process"),
             fence_form("bash", "echo hi\n", line=3))))
        self.assertEqual(forms[0], expected)

    async def test_fence_indented_closer_in_block(self):
        src = (
            "def run []:\n"
            "  process ```text\n"
            "  keep relative\n"
            "    indent\n"
            "  ```\n")

        forms = parse_layout_all(src)
        self.assertEqual(len(forms), 1)

        expected = pycons((
            symbol("def"), symbol("run"), (),
            (symbol("process"),
             fence_form("text", "keep relative\n  indent\n",
                        line=3, col=2))))
        self.assertEqual(forms[0], expected)

    async def test_layout_quasiquote_unaffected(self):
        src = (
            "def get-name []:\n"
            "  `hello\n")

        forms = parse_layout_all(src)
        expected = pycons((
            symbol("def"), symbol("get-name"), (),
            (symbol("quasiquote"), symbol("hello"))))
        self.assertEqual(forms[0], expected)


class TestFencePragma(IsolatedAsyncioTestCase):
    """
    The reader/fences pragma switch (baseline on)
    """

    TRIPLE_QQ = pycons((
        symbol("quasiquote"),
        (symbol("quasiquote"),
         (symbol("quasiquote"), symbol("foo")))))

    async def test_default_pragma_enables_fences(self):
        reader = LayoutReader.from_pragma(dict(_BUILTIN_REGISTRY))
        stream = IndentAwareStream.from_source(
            source_str("```bash\necho hi\n```\n", "<unittest>"))
        result = reader.read(stream)
        head, _tail = result
        self.assertIs(head, symbol("apply-fence"))

    async def test_pragma_disables_fences(self):
        pragma = ModulePragma({"reader/fences": False})
        reader = LayoutReader.from_pragma(dict(_BUILTIN_REGISTRY), pragma)

        stream = IndentAwareStream.from_source(
            source_str("```foo\n", "<unittest>"))
        result = reader.read(stream)
        self.assertEqual(result, self.TRIPLE_QQ)

    async def test_reload_pragma_reenables_fences(self):
        pragma = ModulePragma({"reader/fences": False})
        reader = LayoutReader.from_pragma(dict(_BUILTIN_REGISTRY), pragma)

        reader.reload_pragma(ModulePragma())
        stream = IndentAwareStream.from_source(
            source_str("```bash\necho hi\n```\n", "<unittest>"))
        result = reader.read(stream)
        head, _tail = result
        self.assertIs(head, symbol("apply-fence"))

    async def test_plain_reader_switch(self):
        reader = Reader()
        reader.fences_enabled = False
        stream = IndentAwareStream.from_source(
            source_str("```foo\n", "<unittest>"))
        result = reader.read(stream)
        self.assertEqual(result, self.TRIPLE_QQ)

        reader.fences_enabled = True
        stream = source_str("```bash\necho hi\n```\n", "<unittest>")
        result = reader.read(stream)
        head, _tail = result
        self.assertIs(head, symbol("apply-fence"))


# The end.
