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


import asyncio
import sys
import traceback

from argparse import ArgumentParser, REMAINDER
from os.path import basename
from traceback import format_exception_only


__all__ = (
    'main',
    'option_parser',
    'cli_async',
    'run_file_async',
    'repl_async',
    'REPLCodeBoundary',
    'format_exception_with_koskari_trace',
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
# REPL code boundary
# ---------------------------------------------------------------------------


REPL_INFRASTRUCTURE_MODULES = frozenset((
    'koskari.cli',
    'koskari._sync',
    'asyncio',
    'asyncio.base_events',
    'asyncio.events',
    'asyncio.runners',
    'concurrent.futures',
    'concurrent.futures._base',
))


def _render_koskari_dynchain(trace_data):
    """
    Render dynchain metadata attached to synthetic koskari native frames.

    :param trace_data: iterable of dict-like frame metadata
    :return: list[str] of traceback-formatted lines
    """

    if not isinstance(trace_data, (list, tuple)) or not trace_data:
        return []

    lines = ['  Koskari dynchain (native eval):\n']
    for frame in trace_data:
        if not isinstance(frame, dict):
            continue

        name = frame.get('name', '<anonymous>')
        filename = frame.get('filename', '<unknown>')
        line = frame.get('line', 0)
        col = frame.get('col', 0)
        pc = frame.get('pc', 0)
        lines.append(f'    at {name} ({filename}:{line}:{col}) [pc={pc}]\n')

    return lines


def format_exception_with_koskari_trace(exc_type, exc_val, tb):
    """
    Format an exception traceback, interleaving koskari native dynchain data
    after synthetic native frames that carry ``__koskari_trace__`` in globals.

    Falls back to normal traceback formatting when no traceback is present.

    :return: list[str], matching traceback.format_exception style
    """

    if tb is None:
        return list(format_exception_only(exc_type, exc_val))

    out = ['Traceback (most recent call last):\n']
    extracted = traceback.extract_tb(tb)
    walked = list(traceback.walk_tb(tb))

    for summary, (frame, _lineno) in zip(extracted, walked):
        out.extend(traceback.format_list([summary]))
        trace_data = frame.f_globals.get('__koskari_trace__')
        out.extend(_render_koskari_dynchain(trace_data))

    out.extend(format_exception_only(exc_type, exc_val))
    return out


def _filter_infrastructure_tb(tb):
    """
    Remove infrastructure frames from a traceback.

    Walks the chain, drops frames whose __name__ is in
    REPL_INFRASTRUCTURE_MODULES, and relinks.  Returns the filtered head.

    :param tb: traceback object (root of chain)
    :return: filtered traceback, or original if all frames would be removed
    """

    if tb is None:
        return tb
    filtered = []
    current = tb
    while current is not None:
        frame = current.tb_frame
        mod = frame.f_globals.get('__name__', '')
        if mod not in REPL_INFRASTRUCTURE_MODULES:
            filtered.append(current)
        current = current.tb_next
    if not filtered:
        return None
    for i in range(len(filtered) - 1):
        filtered[i].tb_next = filtered[i + 1]
    filtered[-1].tb_next = None
    return filtered[0]


class REPLCodeBoundary(Exception):
    """
    Internal marker for the boundary between REPL infrastructure and user
    expression evaluation.  Never shown to the user.
    """


async def _boundary_call_async(fn, /, *a, **kw):
    """
    Await fn with the given arguments.  If an exception is raised, re-raise
    REPLCodeBoundary from it so the cause's traceback can be used for display
    (omitting REPL infrastructure frames).
    """

    try:
        return await fn(*a, **kw)
    except Exception as e:
        raise REPLCodeBoundary() from e


# ---------------------------------------------------------------------------
# REPL
# ---------------------------------------------------------------------------


_INCOMPLETE = object()


async def _eval_expression_async(compiler, line, ns):
    """
    Run one REPL expression.
    """

    from koskari.reader import default_reader, source_str

    stream = source_str(line, '<stdin>')
    source_expr = default_reader.read(stream)
    if source_expr is None:
        return _INCOMPLETE
    fragment = compiler.full_compile(source_expr)
    expr = fragment.as_expression(ns)
    return await expr()


async def repl_async(mod, prompter=None, stdin=None, stdout=None, stderr=None):
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

    stdin = stdin or sys.stdin
    stdout = stdout or sys.stdout
    stderr = stderr or sys.stderr

    if prompter is None:
        prompter = _make_prompter()

    loop = asyncio.get_running_loop()

    # init_module registers compiler specials into the namespace.  When the
    # module already has __file__ set (e.g. after run_file_async), we preserve
    # it; otherwise use the sentinel '<stdin>' so the compiler has a filename.
    if not getattr(mod, '__file__', None):
        init_module(mod, '<stdin>')

    ns = vars(mod)
    compiler = Compiler(
        getattr(mod, '__name__', '__main__'),
        mod,
        filename='<stdin>')

    while True:
        try:
            line = await loop.run_in_executor(None, lambda: prompter('koskari> '))

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
            result = await _boundary_call_async(
                _eval_expression_async, compiler, line, ns)

            if result is _INCOMPLETE:
                continue

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
            if isinstance(exc_val, REPLCodeBoundary) and exc_val.__cause__ is not None:
                cause = exc_val.__cause__
                tb = _filter_infrastructure_tb(cause.__traceback__)
                sys.last_type = type(cause)
                sys.last_value = cause
                sys.last_traceback = tb
                if tb is not None:
                    msg = ''.join(format_exception_with_koskari_trace(type(cause), cause, tb))
                else:
                    msg = ''.join(format_exception_only(type(cause), cause))
            else:
                sys.last_type = exc_type
                sys.last_value = exc_val
                sys.last_traceback = tb
                msg = ''.join(format_exception_with_koskari_trace(exc_type, exc_val, tb))
            print(msg, end='', file=stderr)
            stderr.flush()

        stdout.flush()

    print('', file=stdout)


# ---------------------------------------------------------------------------
# File execution
# ---------------------------------------------------------------------------


async def run_file_async(mod, filename):
    """
    Load and execute a koskari source file into mod.

    Calls init_module to attach file metadata and compiler specials, then
    awaits load_module_async.

    :param mod: koskari Module to execute into
    :param filename: path to the .ki source file
    """

    from koskari.importer.module import init_module, load_module_async

    init_module(mod, filename)
    await load_module_async(mod)


# ---------------------------------------------------------------------------
# CLI orchestration
# ---------------------------------------------------------------------------


async def cli_async(options):
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
        await run_file_async(mod, filename)

        if not options.interactive:
            return

    prompter = _make_prompter(options.histfile)
    await repl_async(mod, prompter=prompter)


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
        asyncio.run(cli_async(options))

    except KeyboardInterrupt:
        print('', file=sys.stderr)
        return 130

    return 0


# The end.
