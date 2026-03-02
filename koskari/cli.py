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
koskari.cli

Command-line interface for koskari.

Provides file execution, import-hook installation, and a REPL with a
pluggable input backend that supports both readline and prompt_toolkit.

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
:ai-assistant: claude-4.6-sonnet-high via Cursor
"""


import sys

from argparse import ArgumentParser, REMAINDER
from os.path import basename
from traceback import format_exception, format_exception_only


__all__ = (
    'main',
    'option_parser',
    'cli',
    'run_file',
    'repl',
)


# ---------------------------------------------------------------------------
# Prompt backend selection
# ---------------------------------------------------------------------------


def _make_prompter(histfile=None):
    """
    Return a callable suitable for prompting the user in the REPL.

    Tries prompt_toolkit first, then readline, then falls back to the
    built-in input().  When histfile is provided and readline is used,
    the history file is loaded on startup and saved on exit.

    :param histfile: path to the readline history file, or None
    :return: a callable with the same signature as input(prompt)
    """

    try:
        from prompt_toolkit import PromptSession
        from prompt_toolkit.history import FileHistory

        history = FileHistory(histfile) if histfile else None
        session = PromptSession(history=history)
        return session.prompt

    except ImportError:
        pass

    try:
        import readline

        if histfile:
            try:
                readline.read_history_file(histfile)
            except OSError:
                pass

            import atexit
            atexit.register(readline.write_history_file, histfile)

    except ImportError:
        pass

    return input


# ---------------------------------------------------------------------------
# REPL
# ---------------------------------------------------------------------------


REPL_FILTER_MODULES = frozenset((
    'koskari.cli',
    'koskari.compile',
    'koskari.compile.compiler',
    'koskari.compile.helpers',
    'koskari.reader',
    'koskari.reader.parser',
    'koskari.importer.module',
    'koskari._sync',
))


def _iter_traceback(tb):
    """
    Yield each frame in a traceback chain.

    :param tb: a traceback object or None
    """

    while tb is not None:
        yield tb
        tb = tb.tb_next


def _filter_internal_tb(tb):
    """
    Return a copy of the traceback with koskari-internal frames removed.

    Walks the original chain, skips frames whose __name__ is in
    REPL_FILTER_MODULES, and relinks the remaining frames.  Returns the
    filtered head, or the original tb when every frame would be removed.

    :param tb: the original traceback object
    :return: filtered traceback object, possibly the same as tb
    """

    filtered = [
        frame for frame in _iter_traceback(tb)
        if frame.tb_frame.f_globals.get('__name__') not in REPL_FILTER_MODULES
    ]

    if not filtered:
        return tb

    for i in range(len(filtered) - 1):
        filtered[i].tb_next = filtered[i + 1]
    filtered[-1].tb_next = None

    return filtered[0]


def repl(mod, prompter=None, stdin=None, stdout=None, stderr=None):
    """
    Run an interactive read-eval-print loop for a koskari module.

    Each line entered by the user is compiled and executed as a single
    top-level expression.  Non-None results are printed to stdout.  The
    last result is also stored as '_' in the module namespace.

    :param mod: koskari Module to use as the execution context
    :param prompter: callable with the same signature as input(); when
        None, _make_prompter() is called with no arguments
    :param stdin: input stream, defaults to sys.stdin
    :param stdout: output stream, defaults to sys.stdout
    :param stderr: error stream, defaults to sys.stderr
    """

    from koskari.compile import Compiler
    from koskari.importer.module import init_module
    from koskari.reader import default_reader, source_str
    from koskari._sync import run_sync

    stdin = stdin or sys.stdin
    stdout = stdout or sys.stdout
    stderr = stderr or sys.stderr

    if prompter is None:
        prompter = _make_prompter()

    # init_module registers compiler specials into the namespace.  When the
    # module already has __file__ set (e.g. after run_file), we preserve it;
    # otherwise use the sentinel '<stdin>' so the compiler has a filename.
    if not getattr(mod, '__file__', None):
        init_module(mod, '<stdin>')

    ns = vars(mod)
    compiler = Compiler(
        getattr(mod, '__name__', '__main__'),
        mod,
        filename='<stdin>')

    while True:
        try:
            line = prompter('koskari> ')

        except KeyboardInterrupt:
            print('', file=stderr)
            stderr.flush()
            break

        except EOFError:
            print('', file=stderr)
            stderr.flush()
            break

        if not line.strip():
            continue

        try:
            stream = source_str(line, '<stdin>')
            source_expr = default_reader.read(stream)

            if source_expr is None:
                continue

            fragment = compiler.full_compile(source_expr)
            expr = fragment.as_expression(ns)

            async def _eval():
                return await expr()

            result = run_sync(_eval())

            ns.store('_', result) if hasattr(ns, 'store') else ns.__setitem__('_', result)

            if result is not None:
                print(repr(result), file=stdout)

        except KeyboardInterrupt:
            print('', file=stderr)
            stderr.flush()

        except EOFError:
            print('', file=stderr)
            stderr.flush()
            break

        except SyntaxError:
            exc_type, exc_val, _ = sys.exc_info()
            sys.last_type = exc_type
            sys.last_value = exc_val
            sys.last_traceback = None
            msg = ''.join(format_exception_only(exc_type, exc_val))
            print(msg, end='', file=stderr)
            stderr.flush()

        except Exception:
            exc_type, exc_val, tb = sys.exc_info()
            sys.last_type = exc_type
            sys.last_value = exc_val
            tb = _filter_internal_tb(tb)
            sys.last_traceback = tb
            msg = ''.join(format_exception(exc_type, exc_val, tb))
            print(msg, end='', file=stderr)
            stderr.flush()

        stdout.flush()

    print('', file=stdout)


# ---------------------------------------------------------------------------
# File execution
# ---------------------------------------------------------------------------


def run_file(mod, filename):
    """
    Load and execute a koskari source file into mod.

    Calls init_module to attach file metadata and compiler specials, then
    drives load_module_async synchronously via run_sync.

    :param mod: koskari Module to execute into
    :param filename: path to the .ki source file
    """

    from koskari.importer.module import init_module, load_module_async
    from koskari._sync import run_sync

    init_module(mod, filename)
    run_sync(load_module_async(mod))


# ---------------------------------------------------------------------------
# CLI orchestration
# ---------------------------------------------------------------------------


def cli(options):
    """
    Execute the koskari command-line session described by options.

    Installs the import hook, optionally tweaks sys.path, runs any
    specified file, then drops into the REPL when --interactive is given
    or no filename was provided.

    :param options: namespace returned by option_parser().parse_args()
    """

    from koskari.importer import install
    from koskari.importer.module import new_module

    install()

    filename = options.filename

    if filename:
        sys.argv = [filename] + list(options.args)
    else:
        sys.argv = list(options.args)

    if options.tweakpath:
        sys.path.insert(0, '.')

    mod = new_module('__main__')

    if filename:
        run_file(mod, filename)

        if not options.interactive:
            return

    prompter = _make_prompter(options.histfile)
    repl(mod, prompter=prompter)


# ---------------------------------------------------------------------------
# Argument parser
# ---------------------------------------------------------------------------


def option_parser(name):
    """
    Build and return the ArgumentParser for the koskari CLI.

    :param name: the program name (typically argv[0])
    :return: configured ArgumentParser instance
    """

    parser = ArgumentParser(prog=basename(name))

    parser.add_argument(
        '--no-tweak-path',
        dest='tweakpath',
        action='store_false',
        default=True,
        help="Do not add the current directory to sys.path")

    parser.add_argument(
        '--histfile',
        dest='histfile',
        action='store',
        default=None,
        help="REPL history file path")

    parser.add_argument(
        '-i', '--interactive',
        dest='interactive',
        action='store_true',
        default=False,
        help="Enter interactive mode after executing the given script")

    parser.add_argument(
        'filename',
        nargs='?',
        default=None,
        help="Run filename as a script")

    parser.add_argument(
        'args',
        nargs=REMAINDER,
        default=[],
        help="Arguments forwarded to the script or REPL session")

    return parser


# ---------------------------------------------------------------------------
# Entry point
# ---------------------------------------------------------------------------


def main(argv=None):
    """
    Entry point for the koskari command-line interface.

    :param argv: argument list; defaults to sys.argv when None
    :return: integer exit code
    """

    if argv is None:
        argv = sys.argv

    name, *args = argv
    parser = option_parser(name)
    options = parser.parse_args(args)

    try:
        cli(options)

    except KeyboardInterrupt:
        print('', file=sys.stderr)
        return 130

    return 0


# The end.
