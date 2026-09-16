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
tests.importer.test_active_pragma

The active-pragma read inside a macro body answers for the module being
compiled, not the module the macro was defined in. A provider module
defines a macro whose expansion captures an active-pragma setting; two
consumers with different pragma values each see their own.

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
"""


from textwrap import dedent

import importlib
import os
import shutil
import sys
import tempfile
import unittest

import pytest

from koskari.importer import install, uninstall


pytestmark = pytest.mark.phase_importer


PROVIDER = """
(define-syntax tco-setting
  (macro-rules []
    [(tco-setting) (datum (active-pragma compiler/tco-enable))]))
"""


class ActivePragmaCase(unittest.IsolatedAsyncioTestCase):

    def setUp(self):
        uninstall()
        install()
        self._tmpdir = tempfile.mkdtemp(prefix='koskari_active_pragma_')
        self._prev_path = sys.path[:]
        sys.path.insert(0, self._tmpdir)


    def tearDown(self):
        uninstall()
        install()
        sys.path[:] = self._prev_path
        to_remove = [
            name for name, mod in list(sys.modules.items())
            if hasattr(mod, '__file__')
            and mod.__file__
            and mod.__file__.startswith(self._tmpdir)
        ]
        for name in to_remove:
            del sys.modules[name]
        shutil.rmtree(self._tmpdir, ignore_errors=True)


    def write_ki(self, modname, content):
        path = os.path.join(self._tmpdir, modname + '.ki')
        with open(path, 'w', encoding='utf-8') as fh:
            fh.write(dedent(content))
        return path


class TestActivePragma(ActivePragmaCase):


    async def test_consumer_default_pragma(self):
        self.write_ki('ki_ap_provider', PROVIDER)
        self.write_ki('ki_ap_default', """
        (import-from ki_ap_provider compile/tco-setting)
        (define result (tco-setting))
        """)

        mod = importlib.import_module('ki_ap_default')
        self.assertIs(mod.result, True)


    async def test_consumer_overridden_pragma(self):
        self.write_ki('ki_ap_provider2', PROVIDER)
        self.write_ki('ki_ap_altered', """
        (import-from ki_ap_provider2 compile/tco-setting)
        (pragma [compiler/tco-enable False])
        (define result (tco-setting))
        """)

        mod = importlib.import_module('ki_ap_altered')
        self.assertIs(mod.result, False)


    async def test_assert_setting_does_not_cross_contaminate(self):
        # a module that disables asserts must not disarm assert in any
        # other module: each expansion answers for the pragma of the
        # module being compiled
        self.write_ki('ki_ap_quiet', """
        (pragma [compiler/enable-asserts False])
        (define quiet-check (lambda quiet-check [] (begin (assert False) 1)))
        """)
        self.write_ki('ki_ap_loud', """
        (import-from ki_ap_quiet quiet-check)
        (define loud-check (lambda loud-check [] (begin (assert False) 1)))
        """)

        mod = importlib.import_module('ki_ap_loud')

        quiet_check = getattr(mod, 'quiet-check')
        loud_check = getattr(mod, 'loud-check')

        # the disabled module's assert expanded to nothing
        self.assertEqual(await quiet_check(), 1)

        # the enabled module's assert still fires, even though a module
        # loaded earlier in the process turned asserts off for itself
        with self.assertRaises(AssertionError):
            await loud_check()


    async def test_bare_control_word_error_mode(self):
        self.write_ki('ki_ap_strict', """
        (pragma [compiler/bare-control-words 'error])
        (define f (lambda f [x] (begin (when x return) 99)))
        """)

        from koskari.exceptions import CompilerError
        with self.assertRaises(CompilerError):
            importlib.import_module('ki_ap_strict')


    async def test_bare_control_word_warn_mode(self):
        import warnings
        self.write_ki('ki_ap_warned', """
        (pragma [compiler/bare-control-words 'warn])
        (define f (lambda f [x] (begin (when x return) 99)))
        """)

        with warnings.catch_warnings(record=True) as caught:
            warnings.simplefilter("always")
            mod = importlib.import_module('ki_ap_warned')

        hits = [
            w for w in caught
            if issubclass(w.category, SyntaxWarning)
            and "bare return" in str(w.message)]
        self.assertTrue(hits)

        # the warning names the koskari source, not the Python frame
        # that ran the macro body
        self.assertTrue(hits[0].filename.endswith("ki_ap_warned.ki"))
        self.assertGreater(hits[0].lineno, 0)

        # warn still converts: the function works
        self.assertEqual(await mod.f(False), 99)
        self.assertIsNone(await mod.f(True))


    async def test_bare_control_word_warn_peer_attribution(self):
        # a bare word inside a peer clause reports the peer's line, not
        # the primary block's
        import warnings
        self.write_ki('ki_ap_peer', """(pragma [compiler/bare-control-words 'warn])
define foobar [x]:
    if x:
        1
    elif (== x 0):
        2
    else:
        return
""")

        with warnings.catch_warnings(record=True) as caught:
            warnings.simplefilter("always")
            importlib.import_module('ki_ap_peer')

        hits = [
            w for w in caught
            if issubclass(w.category, SyntaxWarning)
            and "bare return" in str(w.message)]
        self.assertTrue(hits)
        self.assertEqual(hits[0].lineno, 7)


    async def test_bare_control_word_warn_handler_attribution(self):
        # a bare word inside a try handler reports the except clause's
        # line
        import warnings
        self.write_ki('ki_ap_handler', """(pragma [compiler/bare-control-words 'warn])
define f []:
    try:
        (ValueError "boom")
    except [e ValueError]:
        raise
""")

        with warnings.catch_warnings(record=True) as caught:
            warnings.simplefilter("always")
            importlib.import_module('ki_ap_handler')

        hits = [
            w for w in caught
            if issubclass(w.category, SyntaxWarning)
            and "bare raise" in str(w.message)]
        self.assertTrue(hits)
        self.assertEqual(hits[0].lineno, 5)


    async def test_provider_pragma_does_not_leak(self):
        self.write_ki('ki_ap_provider3', """
        (pragma [compiler/tco-enable False])
        (define-syntax tco-setting
          (macro-rules []
            [(tco-setting) (datum (active-pragma compiler/tco-enable))]))
        """)
        self.write_ki('ki_ap_clean', """
        (import-from ki_ap_provider3 compile/tco-setting)
        (define result (tco-setting))
        """)

        mod = importlib.import_module('ki_ap_clean')
        self.assertIs(mod.result, True)


# The end.
